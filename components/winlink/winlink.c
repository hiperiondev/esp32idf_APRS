// @file winlink.c
//
// @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
// @date 2026
// @copyright GNU General Public License v3
// @see https://github.com/hiperiondev/esp32idf_APRS
//
// @note
// This is based on other projects:
//     VP-Digi: https://github.com/sq8vps/vp-digi
//     ESP32APRS: https://github.com/nakhonthai/ESP32APRS_Audio
//     LibAPRS: https://github.com/markqvist/LibAPRS
//
//     please contact their authors for more information.
//
// @brief Winlink radio e-mail over APRSLink: session state machine, command
// queue and mailbox.
//
// The whole exchange with the service is ordinary APRS messaging. Commands go
// out through sendAPRSMessage() and replies arrive through the observer this
// module registers with message_set_rx_observer(), which is what keeps the
// messaging engine free of any knowledge of Winlink: the dependency runs one
// way, from here to there.
//
// Two invariants hold everywhere below and are what make the state machine
// tractable. First, at most one command is outstanding at a time: the next one
// leaves only after the service has acknowledged its predecessor, so every
// forward transition is anchored to something the service actually said.
// Second, no function here blocks or touches the filesystem on the 1 Hz tick;
// the mailbox is written from the observer, which runs on the receive path,
// and only when it has something new to store.

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "app_config.h"
#include "json_escape.h" // json_write_escaped()
#include "json_store.h"  // shared JSON-file store scaffolding
#include "message.h"
#include "storage.h"    // storage_write_lock() / storage_generation()
#include "str_append.h" // str_append(), str_copy_strip_line_breaks(), str_copy_utf8_safe()
#include "winlink.h"

static const char *TAG = "winlink";

#define WINLINK_PATH     "/storage/winlink.json"
#define WINLINK_TMP_PATH "/storage/winlink.json.tmp"

// The command that opens a session. Any command at all makes the service start
// a login, so the word itself carries no meaning to it; a recognisable one is
// used so that an operator watching the traffic can see where a session began.
#define WL_CMD_LOGIN "Start"

// The command that closes a session, and the terminator that hands a composed
// message to the service.
#define WL_CMD_LOGOFF "B"
#define WL_CMD_ENDMSG "/EX"

// Seconds of quiet required after a reply before the next command is sent. The
// service answers a command with one or more messages of its own, and starting
// the next exchange while those are still arriving is what turns a session into
// two stations talking over each other.
#define WL_POST_RX_HOLD_SEC 3

// Longest a state that is waiting on the service may wait before the pending
// command is retransmitted. Measured from the last transmission, so a service
// that is merely slow costs a retry rather than the session.
#define WL_REPLY_TIMEOUT_SEC 45

// Transmissions of one command before the session is abandoned. The first is
// the command itself, so this allows two retries.
#define WL_CMD_TRIES 3

// Widest challenge the service issues. The documented form is three digits;
// the extra room means an unexpected fourth is answered rather than rejected,
// while anything longer than the answer field can carry is refused outright.
#define WL_CHALLENGE_MAX 5

// Characters of the operator's password quoted back, plus the three arbitrary
// ones the scheme asks for, plus the terminator.
#define WL_ANSWER_MAX (WL_CHALLENGE_MAX + 3 + 1)

#define WL_ANSWER_PAD 3

_Static_assert(WL_MAIL_JSON_ENTRY_MAX >= 64 + 20 + 20 + (WL_MAIL_TEXT_MAX * 2 - 1) + 1, "WL_MAIL_JSON_ENTRY_MAX is too small for the mailbox field widths");

typedef struct {
    time_t time;                 // wall-clock time the reply was stored
    uint32_t seq;                // position in the mailbox; higher means newer
    char text[WL_MAIL_TEXT_MAX]; // reply text, as the service sent it
    bool used;
} wl_mail_t;

static wl_mail_t s_mail[WL_MAIL_MAX];
static uint32_t s_mail_seq = 0;
static SemaphoreHandle_t s_lock = NULL;

static winlink_state_t s_state = WL_STATE_DISABLED;
static time_t s_session_started = 0;
static time_t s_last_tx = 0;
static time_t s_last_rx = 0;
static time_t s_last_poll = 0;

static char s_outstanding[APRS_MSG_TEXT_STD_MAX + 1];
static uint8_t s_tries_left = 0;

static char s_answer[WL_ANSWER_MAX];
static char s_error[WL_ERROR_MAX + 1];

// True between the command that opens a message and the terminator that hands
// it to the service. Tracked separately from the session state because a
// composition can be started before the login has completed: the commands are
// queued in order either way, and the state only shows composition once the
// session is actually in command mode.
static bool s_composing = false;

static char s_queue[WL_CMD_QUEUE_SIZE][APRS_MSG_TEXT_STD_MAX + 1];
static uint8_t s_q_head = 0;
static uint8_t s_q_count = 0;

static void lock(void) {
    json_store_lock_ensure(&s_lock);
    json_store_lock_take(&s_lock);
}

static void unlock(void) {
    json_store_lock_give(&s_lock);
}

// ---------------------------------------------------------------------------
// Mailbox
// ---------------------------------------------------------------------------

// Entered with s_lock held, the contract json_store_open_tmp() checks before
// handing back a stream whose stdio buffer is already pinned.
static bool mail_save_locked(void) {
    FILE *f = json_store_open_tmp(WINLINK_TMP_PATH, TAG, s_lock);
    if (!f)
        return false;

    // Written token-by-token straight to the file: no cJSON tree and no second
    // serialized buffer ever exist, so a save costs only the pinned stream
    // buffer on top of littlefs's own.
    fprintf(f, "{\"seq\":%u,\"mail\":[", (unsigned)s_mail_seq);
    bool first = true;
    for (int i = 0; i < WL_MAIL_MAX; i++) {
        if (!s_mail[i].used)
            continue;
        fputs(first ? "{" : ",{", f);
        first = false;
        fprintf(f, "\"t\":%lld,\"n\":%u,\"x\":", (long long)s_mail[i].time, (unsigned)s_mail[i].seq);
        json_write_escaped(f, s_mail[i].text);
        fputc('}', f);
    }
    fputs("]}", f);

    return json_store_commit(f, WINLINK_TMP_PATH, WINLINK_PATH, TAG, "winlink mailbox");
}

static bool mail_save(void) {
    // Module lock first, filesystem-wide writer gate second (storage.h): the
    // temp-file + rename sequence must not overlap the whole-partition format
    // the web Storage page can start.
    storage_write_lock();
    bool ok = mail_save_locked();
    storage_write_unlock();
    return ok;
}

// Entered with s_lock held.
static void mail_load_locked(void) {
    memset(s_mail, 0, sizeof(s_mail));
    s_mail_seq = 0;

    cJSON *doc = NULL;
    if (json_store_read(WINLINK_PATH, TAG, "winlink mailbox", &doc) != JSON_STORE_OK)
        return;

    cJSON *seq = cJSON_GetObjectItemCaseSensitive(doc, "seq");
    if (cJSON_IsNumber(seq) && seq->valuedouble > 0)
        s_mail_seq = (uint32_t)seq->valuedouble;

    cJSON *arr = cJSON_GetObjectItemCaseSensitive(doc, "mail");
    int slot = 0;
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, arr) {
        if (slot >= WL_MAIL_MAX)
            break;
        cJSON *t = cJSON_GetObjectItemCaseSensitive(it, "t");
        cJSON *n = cJSON_GetObjectItemCaseSensitive(it, "n");
        cJSON *x = cJSON_GetObjectItemCaseSensitive(it, "x");
        if (!cJSON_IsString(x))
            continue;
        s_mail[slot].time = cJSON_IsNumber(t) ? (time_t)t->valuedouble : 0;
        s_mail[slot].seq = cJSON_IsNumber(n) ? (uint32_t)n->valuedouble : (uint32_t)(slot + 1);
        strncpy(s_mail[slot].text, x->valuestring, sizeof(s_mail[slot].text) - 1);
        s_mail[slot].text[sizeof(s_mail[slot].text) - 1] = 0;
        s_mail[slot].used = true;
        if (s_mail[slot].seq > s_mail_seq)
            s_mail_seq = s_mail[slot].seq;
        slot++;
    }

    cJSON_Delete(doc);
}

// Entered with s_lock held. Returns the slot the entry landed in, evicting the
// oldest one when the table is full - the same rule the message queue applies
// to a conversation that has outgrown its slots.
static int mail_store_locked(const char *text) {
    int slot = -1;
    for (int i = 0; i < WL_MAIL_MAX; i++) {
        if (!s_mail[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        uint32_t oldest = 0;
        for (int i = 0; i < WL_MAIL_MAX; i++) {
            if (slot < 0 || s_mail[i].seq < oldest) {
                oldest = s_mail[i].seq;
                slot = i;
            }
        }
    }

    s_mail[slot].time = time(NULL);
    s_mail[slot].seq = ++s_mail_seq;
    str_copy_utf8_safe(text, s_mail[slot].text, sizeof(s_mail[slot].text));
    s_mail[slot].used = true;
    return slot;
}

size_t winlink_mail_next_json(uint32_t after_seq, char *out, size_t out_size, uint32_t *out_seq) {
    if (out_seq)
        *out_seq = after_seq;
    if (!out || out_size < WL_MAIL_JSON_ENTRY_MAX)
        return 0;
    out[0] = 0;

    lock();
    const wl_mail_t *best = NULL;
    for (int i = 0; i < WL_MAIL_MAX; i++) {
        if (!s_mail[i].used || s_mail[i].seq <= after_seq)
            continue;
        if (best == NULL || s_mail[i].seq < best->seq)
            best = &s_mail[i];
    }
    if (best == NULL) {
        unlock();
        return 0;
    }

    char esc[WL_MAIL_TEXT_MAX * 2 + 1];
    json_escape(best->text, esc, sizeof(esc));
    time_t t = best->time;
    uint32_t seq = best->seq;
    unlock();

    size_t used = 0;
    str_append(out, out_size, &used, "{\"time\":%lld,\"seq\":%u,\"text\":\"%s\"}", (long long)t, (unsigned)seq, esc);
    if (str_append_truncated(used, out_size)) {
        out[0] = 0;
        return 0;
    }
    if (out_seq)
        *out_seq = seq;
    return used;
}

int winlink_mail_count(void) {
    int n = 0;
    lock();
    for (int i = 0; i < WL_MAIL_MAX; i++) {
        if (s_mail[i].used)
            n++;
    }
    unlock();
    return n;
}

bool winlink_mail_clear(void) {
    lock();
    memset(s_mail, 0, sizeof(s_mail));
    unlock();

    storage_write_lock();
    bool ok = storage_delete(WINLINK_PATH);
    storage_write_unlock();
    return ok;
}

// ---------------------------------------------------------------------------
// Session helpers
// ---------------------------------------------------------------------------

const char *winlink_state_name(winlink_state_t s) {
    switch (s) {
        case WL_STATE_DISABLED:
            return "disabled";
        case WL_STATE_IDLE:
            return "idle";
        case WL_STATE_LOGIN_SENT:
            return "login_sent";
        case WL_STATE_WAIT_CHALLENGE:
            return "wait_challenge";
        case WL_STATE_CHALLENGE_SENT:
            return "challenge_sent";
        case WL_STATE_WAIT_VALID:
            return "wait_valid";
        case WL_STATE_LOGGED_IN:
            return "logged_in";
        case WL_STATE_COMPOSING:
            return "composing";
        case WL_STATE_LOGGING_OFF:
            return "logging_off";
        case WL_STATE_ERROR:
            return "error";
    }
    return "unknown";
}

static void set_state(winlink_state_t s) {
    if (s_state == s)
        return;
    ESP_LOGI(TAG, "session %s -> %s", winlink_state_name(s_state), winlink_state_name(s));
    s_state = s;
}

static void queue_clear(void) {
    s_q_head = 0;
    s_q_count = 0;
}

static void session_fail(const char *why) {
    strncpy(s_error, why, sizeof(s_error) - 1);
    s_error[sizeof(s_error) - 1] = 0;
    s_outstanding[0] = 0;
    s_tries_left = 0;
    s_session_started = 0;
    s_composing = false;
    queue_clear();
    set_state(WL_STATE_ERROR);
    ESP_LOGW(TAG, "session abandoned: %s", why);
}

static void session_close(void) {
    s_outstanding[0] = 0;
    s_tries_left = 0;
    s_session_started = 0;
    s_composing = false;
    queue_clear();
    set_state(WL_STATE_IDLE);
}

// The identity the service keys the mailbox on. The messaging callsign is what
// the outgoing frame actually carries, so it is also what the service sees;
// wl_mycall exists only for the station that wants its Winlink account to be a
// different callsign from the one its messages are addressed to.
static const char *winlink_identity(void) {
    if (g_config.wl_use_msg_call || g_config.wl_mycall[0] == 0)
        return g_config.msg_mycall;
    return g_config.wl_mycall;
}

// Base callsign comparison: everything up to the '-' on either side, case
// insensitive, the same rule the messaging engine applies to an addressee.
static bool base_call_match(const char *a, const char *b) {
    if (!a || !b || a[0] == 0 || b[0] == 0)
        return false;
    size_t na = strcspn(a, "-");
    size_t nb = strcspn(b, "-");
    if (na != nb || na == 0)
        return false;
    return strncasecmp(a, b, na) == 0;
}

bool winlink_is_service_call(const char *call) {
    return base_call_match(call, g_config.wl_service_call);
}

static bool client_usable(void) {
    if (!g_config.wl_enable || !g_config.msg_enable)
        return false;
    if (g_config.wl_service_call[0] == 0)
        return false;
    if (winlink_identity()[0] == 0)
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// Command queue
// ---------------------------------------------------------------------------

// Copies src into dst dropping anything an APRS message field cannot carry,
// then trims the result to the on-air text limit on a whole-character
// boundary. Returns false when nothing usable is left, which is what keeps an
// empty or unrepresentable command out of the queue.
static bool sanitize_command(const char *src, char *dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0)
        return false;

    char stripped[APRS_MSG_TEXT_STD_MAX * 2 + 1];
    str_copy_strip_line_breaks(src, stripped, sizeof(stripped));

    // '{' is the message-number delimiter (APRS101 ch.14) and '|' and '~' are
    // reserved for the base-91 comment telemetry group (ch.13); none of them
    // can appear in text this station puts on the air, and a command carrying
    // one would be truncated or misread by whatever received it.
    size_t w = 0;
    for (size_t r = 0; stripped[r] && w + 1 < sizeof(stripped); r++) {
        char c = stripped[r];
        if (c == '{' || c == '|' || c == '~')
            continue;
        stripped[w++] = c;
    }
    stripped[w] = 0;

    while (w > 0 && isspace((unsigned char)stripped[w - 1]))
        stripped[--w] = 0;
    const char *start = stripped;
    while (*start && isspace((unsigned char)*start))
        start++;
    if (*start == 0)
        return false;

    size_t cap = dst_size < (size_t)APRS_MSG_TEXT_STD_MAX + 1 ? dst_size : (size_t)APRS_MSG_TEXT_STD_MAX + 1;
    str_copy_utf8_safe(start, dst, cap);
    return dst[0] != 0;
}

// Appends one already-sanitized command. The caller has checked the state.
static bool queue_push(const char *cmd) {
    if (s_q_count >= WL_CMD_QUEUE_SIZE) {
        ESP_LOGW(TAG, "command queue full, dropping: %s", cmd);
        return false;
    }
    uint8_t slot = (uint8_t)((s_q_head + s_q_count) % WL_CMD_QUEUE_SIZE);
    strncpy(s_queue[slot], cmd, sizeof(s_queue[slot]) - 1);
    s_queue[slot][sizeof(s_queue[slot]) - 1] = 0;
    s_q_count++;
    return true;
}

static bool queue_pop(char *out, size_t out_size) {
    if (s_q_count == 0)
        return false;
    strncpy(out, s_queue[s_q_head], out_size - 1);
    out[out_size - 1] = 0;
    s_q_head = (uint8_t)((s_q_head + 1) % WL_CMD_QUEUE_SIZE);
    s_q_count--;
    return true;
}

int winlink_queue_depth(void) {
    return s_q_count;
}

// ---------------------------------------------------------------------------
// Challenge / response
// ---------------------------------------------------------------------------

// Each digit of the challenge is a 1-based character position in the
// operator's password; the answer quotes those characters back and adds three
// arbitrary ones, in any order, so the password itself never travels. A
// challenge that names a position the password does not have is refused rather
// than answered with a substitute character: a wrong answer costs the session
// anyway and hides the real problem, which is that the configured password is
// shorter than the account's.
static bool build_response(const char *digits, const char *password, char *out, size_t out_size) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    if (!digits || !password || !out)
        return false;

    size_t pwlen = strlen(password);
    size_t ndigits = strlen(digits);
    if (pwlen == 0 || ndigits == 0 || ndigits > WL_CHALLENGE_MAX)
        return false;
    if (out_size < ndigits + WL_ANSWER_PAD + 1)
        return false;

    size_t n = 0;
    for (size_t i = 0; i < ndigits; i++) {
        if (digits[i] < '1' || digits[i] > '9')
            return false;
        size_t pos = (size_t)(digits[i] - '0');
        if (pos > pwlen)
            return false;
        out[n++] = password[pos - 1];
    }
    for (int i = 0; i < WL_ANSWER_PAD; i++)
        out[n++] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
    out[n] = 0;
    return true;
}

// Extracts the digits of a "Login [NNN]" challenge into out. Returns false when
// the brackets are absent or hold more than a challenge can be.
static bool parse_challenge(const char *text, char *out, size_t out_size) {
    const char *open = strchr(text, '[');
    if (!open)
        return false;
    const char *close = strchr(open + 1, ']');
    if (!close)
        return false;
    size_t n = (size_t)(close - open - 1);
    if (n == 0 || n >= out_size)
        return false;
    memcpy(out, open + 1, n);
    out[n] = 0;
    return true;
}

// ---------------------------------------------------------------------------
// Transmission
// ---------------------------------------------------------------------------

static void transmit(const char *cmd) {
    strncpy(s_outstanding, cmd, sizeof(s_outstanding) - 1);
    s_outstanding[sizeof(s_outstanding) - 1] = 0;
    s_last_tx = time(NULL);
    sendAPRSMessage(g_config.wl_service_call, s_outstanding);
}

// ---------------------------------------------------------------------------
// Public session control
// ---------------------------------------------------------------------------

bool winlink_login(void) {
    if (!client_usable())
        return false;
    if (s_state != WL_STATE_IDLE && s_state != WL_STATE_ERROR)
        return false;

    s_error[0] = 0;
    queue_clear();
    s_outstanding[0] = 0;
    s_tries_left = WL_CMD_TRIES;
    s_session_started = time(NULL);
    set_state(WL_STATE_LOGIN_SENT);
    transmit(WL_CMD_LOGIN);
    return true;
}

bool winlink_logoff(void) {
    if (s_state != WL_STATE_LOGGED_IN && s_state != WL_STATE_COMPOSING)
        return false;
    // Whatever is still queued belonged to the session being closed, so it goes
    // with it rather than trailing the log-off out onto the channel.
    queue_clear();
    if (!queue_push(WL_CMD_LOGOFF))
        return false;
    set_state(WL_STATE_LOGGING_OFF);
    return true;
}

bool winlink_send_command(const char *cmd) {
    if (!client_usable())
        return false;

    char clean[APRS_MSG_TEXT_STD_MAX + 1];
    if (!sanitize_command(cmd, clean, sizeof(clean)))
        return false;

    if (s_state == WL_STATE_IDLE || s_state == WL_STATE_ERROR) {
        if (!g_config.wl_auto_login)
            return false;
        if (!winlink_login())
            return false;
        return queue_push(clean);
    }

    if (s_state == WL_STATE_DISABLED || s_state == WL_STATE_LOGGING_OFF)
        return false;

    return queue_push(clean);
}

bool winlink_list(void) {
    return winlink_send_command("L");
}

bool winlink_read(unsigned n) {
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "R%u", n);
    return winlink_send_command(cmd);
}

bool winlink_kill(unsigned n) {
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "K%u", n);
    return winlink_send_command(cmd);
}

bool winlink_reply(unsigned n) {
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "Y%u", n);
    return winlink_send_command(cmd);
}

bool winlink_forward(unsigned n, const char *addressee) {
    if (!addressee || addressee[0] == 0)
        return false;
    char cmd[APRS_MSG_TEXT_STD_MAX + 1];
    snprintf(cmd, sizeof(cmd), "F%u %s", n, addressee);
    return winlink_send_command(cmd);
}

bool winlink_compose_begin(const char *addressee, const char *subject) {
    if (!addressee || addressee[0] == 0)
        return false;
    if (s_state == WL_STATE_COMPOSING)
        return false;

    char cmd[APRS_MSG_TEXT_STD_MAX + 1];
    snprintf(cmd, sizeof(cmd), "SP %s %s", addressee, subject ? subject : "");
    if (!winlink_send_command(cmd))
        return false;
    // The service is in message-entry mode from the moment it reads that
    // command, so everything queued after it is body text and must not be
    // mistaken for a command by anything reading this state.
    s_composing = true;
    if (s_state == WL_STATE_LOGGED_IN)
        set_state(WL_STATE_COMPOSING);
    return true;
}

bool winlink_compose_line(const char *line) {
    if (!s_composing)
        return false;
    return winlink_send_command(line);
}

bool winlink_compose_end(void) {
    if (!s_composing)
        return false;
    if (!winlink_send_command(WL_CMD_ENDMSG))
        return false;
    s_composing = false;
    if (s_state == WL_STATE_COMPOSING)
        set_state(WL_STATE_LOGGED_IN);
    return true;
}

bool winlink_compose_abort(void) {
    if (!s_composing)
        return false;
    queue_clear();
    s_composing = false;
    if (s_state == WL_STATE_COMPOSING)
        set_state(WL_STATE_LOGGED_IN);
    return true;
}

winlink_state_t winlink_state(void) {
    return s_state;
}

const char *winlink_last_error(void) {
    return s_error;
}

uint32_t winlink_session_remaining_sec(void) {
    if (s_session_started == 0)
        return 0;
    time_t limit = (time_t)g_config.wl_session_max_min * 60;
    time_t elapsed = time(NULL) - s_session_started;
    if (elapsed < 0 || elapsed >= limit)
        return 0;
    return (uint32_t)(limit - elapsed);
}

bool winlink_comment_active(void) {
    return g_config.wl_enable && g_config.wl_comment_en;
}

// ---------------------------------------------------------------------------
// Reply handling
// ---------------------------------------------------------------------------

// True when the outstanding command has been acknowledged. The service quotes
// the number of the message it is acknowledging, and the messaging engine has
// already matched it against the outbound queue by the time this runs, so the
// acknowledgement only has to be recognised as belonging to this session.
static void on_service_ack(void) {
    s_last_rx = time(NULL);
    s_outstanding[0] = 0;
    s_tries_left = WL_CMD_TRIES;

    switch (s_state) {
        case WL_STATE_LOGIN_SENT:
            set_state(WL_STATE_WAIT_CHALLENGE);
            break;
        case WL_STATE_CHALLENGE_SENT:
            set_state(WL_STATE_WAIT_VALID);
            break;
        default:
            break;
    }
}

static void on_challenge(const char *text) {
    char digits[WL_CHALLENGE_MAX + 1];
    if (!parse_challenge(text, digits, sizeof(digits))) {
        session_fail("challenge could not be read");
        return;
    }
    if (!build_response(digits, g_config.wl_password, s_answer, sizeof(s_answer))) {
        session_fail("password does not cover the challenge");
        return;
    }
    // The answer replaces whatever was outstanding: the service asked for it
    // instead of acting on the command that opened the session, and that
    // command is re-sent by the operator once the session is up.
    s_tries_left = WL_CMD_TRIES;
    set_state(WL_STATE_CHALLENGE_SENT);
    transmit(s_answer);
}

// Stores one reply and persists the mailbox. Runs on the receive path rather
// than on the 1 Hz tick, which is what keeps filesystem work off that tick.
static void store_reply(const char *text) {
    lock();
    mail_store_locked(text);
    bool ok = mail_save();
    unlock();
    if (!ok)
        ESP_LOGW(TAG, "mailbox could not be written to %s", WINLINK_PATH);
}

// The observer the messaging engine calls for every message this station
// accepts. Only traffic from the service callsign is Winlink's; everything else
// is left to the ordinary conversation queue, which is what returning false
// means.
static bool winlink_rx_observer(const char *sender, const char *text, uint16_t msgID, bool is_ack) {
    (void)msgID;

    if (!client_usable() || !winlink_is_service_call(sender))
        return false;
    if (s_state == WL_STATE_DISABLED)
        return false;

    if (is_ack) {
        on_service_ack();
        return true;
    }

    s_last_rx = time(NULL);

    if (strncmp(text, "Login [", 7) == 0) {
        on_challenge(text);
        return true;
    }

    if (strstr(text, "Login valid") != NULL) {
        s_error[0] = 0;
        s_outstanding[0] = 0;
        s_tries_left = WL_CMD_TRIES;
        s_session_started = time(NULL);
        set_state(s_composing ? WL_STATE_COMPOSING : WL_STATE_LOGGED_IN);
        return true;
    }

    if (strncmp(text, "Log off successful", 18) == 0) {
        session_close();
        return true;
    }

    // An account with secure login switched off is never challenged: the
    // service simply answers the command that opened the session. Reaching
    // command mode from any of the waiting states is therefore a normal
    // outcome, not a protocol error.
    if (s_state == WL_STATE_LOGIN_SENT || s_state == WL_STATE_WAIT_CHALLENGE || s_state == WL_STATE_CHALLENGE_SENT || s_state == WL_STATE_WAIT_VALID) {
        s_outstanding[0] = 0;
        s_tries_left = WL_CMD_TRIES;
        s_session_started = time(NULL);
        set_state(s_composing ? WL_STATE_COMPOSING : WL_STATE_LOGGED_IN);
    }

    store_reply(text);
    return true;
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void winlink_tick_1hz(void) {
    if (!client_usable()) {
        if (s_state != WL_STATE_DISABLED) {
            queue_clear();
            s_outstanding[0] = 0;
            s_session_started = 0;
            s_composing = false;
            set_state(WL_STATE_DISABLED);
        }
        return;
    }

    if (s_state == WL_STATE_DISABLED)
        set_state(WL_STATE_IDLE);

    time_t now = time(NULL);

    // The service expires a session about two hours after it opened.
    // wl_session_max_min sits below that, so this station gives a session up
    // fractionally before the service does and never sends a command into one
    // that no longer exists.
    if (s_session_started != 0 && (now - s_session_started) >= (time_t)g_config.wl_session_max_min * 60) {
        ESP_LOGI(TAG, "session lifetime reached, logging off locally");
        session_close();
        return;
    }

    // Nothing else moves while a command is outstanding: one command at a time
    // is what anchors every forward transition to a reply from the service.
    if (s_outstanding[0] != 0) {
        if ((now - s_last_tx) < WL_REPLY_TIMEOUT_SEC)
            return;
        if (s_tries_left > 0)
            s_tries_left--;
        if (s_tries_left == 0) {
            session_fail("the service did not answer");
            return;
        }
        ESP_LOGI(TAG, "no answer to \"%s\", retransmitting", s_outstanding);
        transmit(s_outstanding);
        return;
    }

    // A reply is often several messages; starting the next exchange before the
    // last of them has arrived is what makes two stations talk over each other.
    if ((now - s_last_rx) < WL_POST_RX_HOLD_SEC)
        return;

    if (s_q_count > 0) {
        char cmd[APRS_MSG_TEXT_STD_MAX + 1];
        if (queue_pop(cmd, sizeof(cmd))) {
            s_tries_left = WL_CMD_TRIES;
            transmit(cmd);
        }
        return;
    }

    // Periodic listing: the operator has asked to be told about waiting mail
    // without having to open a session by hand. It only runs from idle, so it
    // never interrupts a session that is doing something.
    if (g_config.wl_poll_min > 0 && s_state == WL_STATE_IDLE) {
        time_t interval = (time_t)g_config.wl_poll_min * 60;
        if (s_last_poll == 0 || (now - s_last_poll) >= interval) {
            s_last_poll = now;
            if (winlink_login())
                queue_push("L");
        }
    }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void winlink_apply_config(void) {
    // The messaging engine transmits to this addressee over APRS-IS alone when
    // the operator has asked for it: this station is its own IGate, so a
    // command it puts on the air is a transmission nothing on the local channel
    // needs to hear, and the answer comes back over the same Internet link
    // either way. Re-registered on every save so the switch takes effect
    // without a reboot.
    message_set_inet_only_peer(g_config.wl_inet_only ? g_config.wl_service_call : NULL);
}

void winlink_init(void) {
    lock();
    mail_load_locked();
    unlock();

    memset(s_queue, 0, sizeof(s_queue));
    s_outstanding[0] = 0;
    s_error[0] = 0;
    s_composing = false;
    s_state = WL_STATE_DISABLED;
    s_last_poll = time(NULL);

    winlink_apply_config();
    message_set_rx_observer(winlink_rx_observer);

    ESP_LOGI(TAG, "Winlink client initialized (enabled=%d service=%s mail=%d)", g_config.wl_enable, g_config.wl_service_call, winlink_mail_count());
}
