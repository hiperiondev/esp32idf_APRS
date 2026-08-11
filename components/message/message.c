// @file message.c
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
// @brief APRS text messaging implementation: outgoing message and ACK
// formatting, incoming message parsing and acknowledgement, the Reply-ACK
// algorithm of APRS 1.1, and retry/timeout handling of the in-memory queue.

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "BMP180.h" // bmp180_gpio_is_reserved(): keep the I2C pins out of the alarm pin
#include "afsk.h"   // afsk_ptt_gpio_is_valid(), MODEM_ADC_GPIO / MODEM_DAC_GPIO (checked internally by afsk_ptt_gpio_is_valid())
#include "app_config.h"
#include "aprs_path.h"                          // aprs_path_build_suffix_from_config()
#include "aprs_service.h"                       // APRS_TOCALL: this station's destination call, same one every other packet type uses
#include "esp32idf_radioamateur_modem_config.h" // MODEM_PTT_GPIO: the fixed PTT pin, checked directly below
#include "json_escape.h"                        // json_escape()
#include "message.h"
#include "query.h" // query_process_directed(): second consumer of the ::ADDRESSEE: payload, for "CALL:?query?"
#include "str_append.h"

static const char *TAG = "message";

#define MSG_ALARM_PULSE_MS 1000

static msg_entry_t s_queue[MSG_QUEUE_SIZE];

// Pieces of one serialized queue entry that are not one of the two escaped text
// fields, used to check MSG_JSON_ENTRY_MAX against the field widths of
// msg_entry_t. Both escaped fields are bounded by their own destination buffer
// in message_next_json(), which json_escape() never overruns, so those two
// widths doubled plus these three numbers bound the whole object.
#define MSG_JSON_TEMPLATE_LEN 50 // punctuation and key names of the object template
#define MSG_JSON_TIME_LEN     20 // widest "time" value: int64 seconds, sign included
#define MSG_JSON_STATUS_LEN   7  // longest "dir"/"status" word ("pending")

_Static_assert(MSG_JSON_ENTRY_MAX >= MSG_JSON_TEMPLATE_LEN + MSG_JSON_TIME_LEN + 2 * MSG_JSON_STATUS_LEN + (sizeof(((msg_entry_t *)0)->callsign) * 2 - 1) +
                                         (MSG_TEXT_MAX * 2 - 1) + 1,
               "MSG_JSON_ENTRY_MAX is too small for the message field widths");
static uint32_t s_seq = 0;
static uint16_t s_msgID = 0;
static void (*s_txHandler)(const char *packet, size_t len, uint8_t channels) = NULL;

// ---------------------------------------------------------------------------
// Reply-ACK (APRS 1.1): the acknowledgement owed to each correspondent
//
// The number of the last message received from a station is remembered here
// and rides out as the "}AA" free acknowledgement on the next message sent to
// that station, which is what saves the round trip a separate "ackNN" would
// need. It stays owed until that station sends a newer numbered message: the
// algorithm asks for the latest outstanding acknowledgement on every
// transmission, retries included, and repeating it costs two characters while
// giving the acknowledgement another chance to arrive.
// ---------------------------------------------------------------------------
typedef struct {
    char callsign[11]; // correspondent, upper case (empty slot when NUL at [0])
    char mm[3];        // that station's latest message number, as received
    uint32_t seq;      // update order, for reuse of the least recently written slot
} reply_ack_t;

static reply_ack_t s_replyAck[MSG_REPLY_ACK_STATIONS];
static uint32_t s_replyAckSeq = 0;

void message_set_tx_handler(void (*handler)(const char *packet, size_t len, uint8_t channels)) {
    s_txHandler = handler;
}

void message_init(void) {
    memset(s_queue, 0, sizeof(s_queue));
    s_seq = 0;
    memset(s_replyAck, 0, sizeof(s_replyAck));
    s_replyAckSeq = 0;
}

// ---------------------------------------------------------------------------
// Message Alarm GPIO: driven to 1 for MSG_ALARM_PULSE_MS whenever a direct
// message addressed to g_config.msg_mycall is received, then back to 0 until
// the next one. Disabled by default (g_config.msg_alarm_enable == false /
// g_config.msg_alarm_gpio == -1).
// ---------------------------------------------------------------------------
static int8_t s_alarmGpio = -1;
static esp_timer_handle_t s_alarmTimer = NULL;

bool message_alarm_gpio_is_valid(int8_t gpio) {
    if (gpio == -1)
        return true; // "disabled" is always accepted

    // Output-capable, not the input-only pads, not the internal flash/PSRAM
    // pads, and not colliding with the audio modem's ADC/DAC - same rules
    // as the PTT pin (see afsk_ptt_gpio_is_valid()).
    if (!afsk_ptt_gpio_is_valid(gpio))
        return false;

    // Reject a GPIO already used by the PTT pin, a fixed compile-time
    // constant (MODEM_PTT_GPIO).
    if (gpio == MODEM_PTT_GPIO)
        return false;

    // Not already used by any sensors_local peripheral bus. (These fields are
    // config-struct-only placeholders with no driver behind them yet - see
    // the same note in web_gpio_collect_used() - so they're not checked here;
    // only pins that are genuinely wired to something (PTT above, BMP180 I2C
    // via bmp180_gpio_is_reserved()) can make a GPIO invalid.)
    if (bmp180_gpio_is_reserved(gpio))
        return false;

    return true;
}

static void alarmTimerCb(void *arg) {
    (void)arg;
    if (s_alarmGpio >= 0)
        gpio_set_level((gpio_num_t)s_alarmGpio, 0);
}

void message_alarm_configure(bool enable, int8_t gpio) {
    int8_t new_gpio = (enable && message_alarm_gpio_is_valid(gpio)) ? gpio : -1;

    if (s_alarmTimer)
        esp_timer_stop(s_alarmTimer); // no-op if not running

    // Release the previous pin (disabling, or switching to a different one)
    // so a stale output isn't left driving.
    if (s_alarmGpio >= 0 && s_alarmGpio != new_gpio) {
        gpio_set_level((gpio_num_t)s_alarmGpio, 0);
        gpio_set_direction((gpio_num_t)s_alarmGpio, GPIO_MODE_INPUT);
    }

    s_alarmGpio = new_gpio;

    if (s_alarmGpio < 0)
        return;

    if (!s_alarmTimer) {
        const esp_timer_create_args_t args = {
            .callback = alarmTimerCb,
            .name = "msg_alarm",
        };
        if (esp_timer_create(&args, &s_alarmTimer) != ESP_OK) {
            ESP_LOGW(TAG, "Message Alarm: failed to create timer, disabling");
            s_alarmGpio = -1;
            return;
        }
    }

    gpio_reset_pin((gpio_num_t)s_alarmGpio);
    gpio_set_direction((gpio_num_t)s_alarmGpio, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)s_alarmGpio, 0); // idle low until a message arrives
    ESP_LOGI(TAG, "Message Alarm: GPIO%d enabled", (int)s_alarmGpio);
}

// Pulses the alarm pin high for MSG_ALARM_PULSE_MS. Called each time a direct
// message for g_config.msg_mycall is received. Re-arms the timer on every
// call, so back-to-back messages keep the pin high without flickering, and it
// only drops back to 0 once MSG_ALARM_PULSE_MS elapses with no new message.
static void message_alarm_pulse(void) {
    if (s_alarmGpio < 0 || !s_alarmTimer)
        return;

    gpio_set_level((gpio_num_t)s_alarmGpio, 1);
    esp_timer_stop(s_alarmTimer); // no-op if not currently running
    esp_timer_start_once(s_alarmTimer, (uint64_t)MSG_ALARM_PULSE_MS * 1000ULL);
}

// ---------------------------------------------------------------------------
// Helpers: trim, uppercase
// ---------------------------------------------------------------------------
static void trimUpper(char *s) {
    // trim
    char *start = s;
    while (*start && isspace((unsigned char)*start))
        start++;
    if (start != s)
        memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = 0;
    for (size_t i = 0; i < len; i++)
        s[i] = toupper((unsigned char)s[i]);
}

// Compares two callsigns ignoring any "-SSID" suffix, so a message sent to
// "N0CALL", "N0CALL-0".."N0CALL-15" (or any other/garbled SSID) is treated
// as addressed to station "N0CALL". Case-insensitive; SSID digits themselves
// are not validated, only stripped.
static bool callsignBaseMatch(const char *a, const char *b) {
    size_t na = 0, nb = 0;
    while (a[na] && a[na] != '-')
        na++;
    while (b[nb] && b[nb] != '-')
        nb++;
    if (na == 0 || na != nb)
        return false;
    return strncasecmp(a, b, na) == 0;
}

// Reports whether toCall (already trimmed and upper-cased) is one of the
// message-group addressees this station reads: the built-in "ALL"/"QST"/"CQ"
// set every station reads per APRS101 chapter 14, or one of the operator's
// own group names in g_config.msg_group[]. Compared whole, unlike a callsign:
// a group name carries no SSID and is matched exactly.
static bool isGroupAddressee(const char *toCall) {
    if (strcmp(toCall, "ALL") == 0 || strcmp(toCall, "QST") == 0 || strcmp(toCall, "CQ") == 0)
        return true;

    for (int i = 0; i < MSG_USER_GROUPS; i++) {
        if (g_config.msg_group[i][0] == 0)
            continue;
        char group[10];
        strncpy(group, g_config.msg_group[i], sizeof(group) - 1);
        group[sizeof(group) - 1] = 0;
        trimUpper(group);
        if (group[0] && strcmp(toCall, group) == 0)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Reply-ACK: reading a received identifier and building an outgoing one
// ---------------------------------------------------------------------------

// Reads a message number written as plain decimal digits. Anything else - an
// empty field, an alphanumeric identifier another client may have issued, a
// value past the width of the field - is refused, because the only numbers
// matched against the outbound queue are the ones this station issued itself.
static bool parseMsgNumber(const char *s, uint16_t *out) {
    if (s == NULL || s[0] == 0)
        return false;

    unsigned long v = 0;
    for (size_t i = 0; s[i]; i++) {
        if (i >= 5 || s[i] < '0' || s[i] > '9')
            return false;
        v = v * 10 + (unsigned long)(s[i] - '0');
    }
    if (v > UINT16_MAX)
        return false;

    *out = (uint16_t)v;
    return true;
}

// Checks whether s is a valid message-number field as APRS101 chapter 14
// defines it for an "ack"/"rej": 1-5 alphanumeric characters, optionally
// followed by a single '}' and up to two more characters (the Reply-ACK free
// acknowledgement), with nothing else before the end of the string. Ordinary
// message text that merely happens to start with "ack"/"rej" - "acknowledged,
// thanks", "rejoining net" - fails this check and is left as plain text.
static bool isValidMsgNoField(const char *s) {
    if (s == NULL || s[0] == 0)
        return false;

    size_t i = 0;
    while (i < 5 && isalnum((unsigned char)s[i]))
        i++;
    if (i == 0)
        return false;

    if (s[i] == 0)
        return true; // plain "MM", 1-5 alphanumeric characters, nothing else

    if (s[i] != '}')
        return false; // more than 5 characters before any '}', or other junk

    return strlen(s + i + 1) <= 2; // "MM}" or "MM}AA"
}

// Splits a received message identifier into the sender's own number and the
// free acknowledgement it carries: "MM}AA" gives both, "MM}" gives the number
// and an empty acknowledgement, and a plain "MM" gives the number alone. Both
// outputs are always NUL-terminated.
static void splitReplyAck(const char *msgNo, char *own, size_t own_size, char *ack, size_t ack_size) {
    if (own_size == 0 || ack_size == 0)
        return;

    own[0] = 0;
    ack[0] = 0;

    const char *sep = strchr(msgNo, '}');
    size_t n = sep ? (size_t)(sep - msgNo) : strlen(msgNo);
    if (n > own_size - 1)
        n = own_size - 1;
    memcpy(own, msgNo, n);
    own[n] = 0;

    if (sep) {
        strncpy(ack, sep + 1, ack_size - 1);
        ack[ack_size - 1] = 0;
    }
}

// Records the acknowledgement now owed to a station. A station already in the
// table keeps its slot and has its number replaced, so only the latest one is
// ever owed; a new station takes a free slot, or the one written longest ago.
static void replyAckRemember(const char *call, const char *mm) {
    if (call[0] == 0 || mm[0] == 0)
        return;

    int slot = -1;
    uint32_t oldest = 0;
    for (int i = 0; i < MSG_REPLY_ACK_STATIONS; i++) {
        if (strcmp(s_replyAck[i].callsign, call) == 0) {
            slot = i;
            break;
        }
        if (s_replyAck[i].callsign[0] == 0) {
            slot = i;
            break;
        }
        if (slot < 0 || s_replyAck[i].seq < oldest) {
            oldest = s_replyAck[i].seq;
            slot = i;
        }
    }

    memset(s_replyAck[slot].callsign, 0, sizeof(s_replyAck[slot].callsign));
    strncpy(s_replyAck[slot].callsign, call, sizeof(s_replyAck[slot].callsign) - 1);
    memset(s_replyAck[slot].mm, 0, sizeof(s_replyAck[slot].mm));
    strncpy(s_replyAck[slot].mm, mm, sizeof(s_replyAck[slot].mm) - 1);
    s_replyAck[slot].seq = ++s_replyAckSeq;
}

// Builds the message-number suffix of an outgoing message: "{MM}" on its own,
// or "{MM}AA" when this station owes that correspondent an acknowledgement.
// Called at the moment a frame is built rather than when the message is queued,
// so a retry carries whatever is owed by then and not what was owed when the
// operator wrote the message. The trailing brace is present either way: it is
// what tells the other end that a Reply-ACK can be sent back.
//
// Callsigns are matched exactly, the same rule pkgMsg_Find() applies to an
// incoming ack, so an acknowledgement owed to one SSID of a station is never
// attached to a message addressed to another.
static void buildMsgNumberSuffix(const char *call, uint16_t msgID, char *out, size_t out_size) {
    const char *owed = "";
    for (int i = 0; i < MSG_REPLY_ACK_STATIONS; i++) {
        if (s_replyAck[i].callsign[0] && strcmp(s_replyAck[i].callsign, call) == 0) {
            owed = s_replyAck[i].mm;
            break;
        }
    }
    snprintf(out, out_size, "{%02u}%s", (unsigned)msgID, owed);
}

// ---------------------------------------------------------------------------
// Queue management
// ---------------------------------------------------------------------------
int pkgMsg_Find(const char *call, uint16_t msgID, bool rxtx) {
    // Exact match only. s_queue[i].callsign is always NUL-terminated (see
    // pkgMsgStore(), which memset()s the field before strncpy()) and always
    // upper case, as is every callsign reaching this function, so a plain
    // strcmp() is safe here. A substring search would be wrong: a stored
    // "N0CALL" would match an incoming "N0CALL-9" and vice versa, and two
    // unrelated callsigns could coincidentally overlap, letting an ack/reply
    // from the wrong station mark another station's queued message as
    // acknowledged.
    for (int i = 0; i < MSG_QUEUE_SIZE; i++) {
        if (s_queue[i].used && s_queue[i].msgID == msgID && s_queue[i].rxtx == rxtx && strcmp(s_queue[i].callsign, call) == 0)
            return i;
    }
    return -1;
}

// Picks the slot the next message goes into: a free one while the queue is
// filling up, otherwise the slot holding the oldest message of the whole
// conversation, received or outbound. Age is taken from the insertion counter
// rather than from the wall clock, so the choice stays right while the system
// clock is still unset and when NTP steps it.
static int pkgMsgEvictSlot(void) {
    int ret = 0;
    uint32_t oldest = 0;
    for (int i = 0; i < MSG_QUEUE_SIZE; i++) {
        if (!s_queue[i].used)
            return i;
        if (i == 0 || s_queue[i].seq < oldest) {
            oldest = s_queue[i].seq;
            ret = i;
        }
    }
    return ret;
}

// Appends one message to the conversation. Every call takes a slot of its own,
// so a received message sits in the history next to the ones this station sent
// and stays there until MSG_QUEUE_SIZE newer messages have pushed it out. group
// is meaningful for RX entries only: true when the message was addressed to a
// message group rather than to this station's own callsign, which is what
// keeps a group message and a direct message that happen to share the same
// sender and message number in separate slots.
static int pkgMsgStore(const char *call, const char *text, uint16_t msgID, int8_t ack, bool rxtx, bool group) {
    if (!call[0] || !text[0])
        return -1;

    int i = pkgMsgEvictSlot();

    s_queue[i].used = true;
    s_queue[i].seq = ++s_seq;
    s_queue[i].time = time(NULL);
    s_queue[i].last_tx = s_queue[i].time;
    s_queue[i].msgID = msgID;
    s_queue[i].ack = ack;
    s_queue[i].rxtx = rxtx;
    s_queue[i].group = group;
    memset(s_queue[i].callsign, 0, sizeof(s_queue[i].callsign));
    strncpy(s_queue[i].callsign, call, sizeof(s_queue[i].callsign) - 1);
    memset(s_queue[i].text, 0, sizeof(s_queue[i].text));
    strncpy(s_queue[i].text, text, sizeof(s_queue[i].text) - 1);
    return i;
}

// Tells a retransmission from a new message. A sender that gets no ack sends
// the very same message again - same station, same APRS message number, same
// text - and that repeat belongs in the history once, not once per copy heard.
// Anything else is a new line of the conversation, including a second message
// carrying the number the sender used before (numbering restarts when the
// sending station reboots), a message with no number at all, which arrives
// with number 0 the way every other unnumbered message does, and a group
// message that happens to share its sender and number with an unrelated
// direct message, which is a different line of the conversation and is kept
// in a slot of its own. Text is compared as stored, i.e. after the truncation
// to MSG_TEXT_MAX bytes.
static int pkgMsgFindRepeat(const char *call, const char *text, uint16_t msgID, bool rxtx, bool group) {
    for (int i = 0; i < MSG_QUEUE_SIZE; i++) {
        if (!s_queue[i].used || s_queue[i].rxtx != rxtx || s_queue[i].msgID != msgID || s_queue[i].group != group)
            continue;
        if (strcmp(s_queue[i].callsign, call) != 0)
            continue;
        if (strncmp(s_queue[i].text, text, sizeof(s_queue[i].text) - 1) == 0)
            return i;
    }
    return -1;
}

size_t message_next_json(uint32_t after_seq, char *out, size_t out_size, uint32_t *out_seq) {
    if (out_seq)
        *out_seq = after_seq;
    if (out == NULL || out_size < MSG_JSON_ENTRY_MAX)
        return 0;

    // Walking by the insertion counter is what puts the thread in conversation
    // order: an entry keeps the position it was given when it entered the queue,
    // which is what makes a message that is still being retried stay where the
    // operator wrote it instead of climbing back to the bottom of the panel on
    // every attempt. Driving the walk off the counter rather than off slot order
    // also means a message evicted part-way through a response is skipped
    // instead of repeated or misplaced.
    const msg_entry_t *e = NULL;
    for (int i = 0; i < MSG_QUEUE_SIZE; i++) {
        const msg_entry_t *c = &s_queue[i];
        if (!c->used || c->seq <= after_seq)
            continue;
        if (!e || c->seq < e->seq)
            e = c;
    }
    if (!e)
        return 0;

    char call_esc[sizeof(e->callsign) * 2];
    char text_esc[MSG_TEXT_MAX * 2];
    json_escape(e->callsign, call_esc, sizeof(call_esc));
    json_escape(e->text, text_esc, sizeof(text_esc));

    const char *status = e->rxtx ? "rx" : (e->ack > 0 ? "pending" : "sent");

    // out_size is at least MSG_JSON_ENTRY_MAX, which the static assertion above
    // ties to these field widths, so this cannot truncate.
    int len = snprintf(out, out_size, "{\"time\":%lld,\"dir\":\"%s\",\"call\":\"%s\",\"text\":\"%s\",\"status\":\"%s\"}", (long long)e->time,
                       e->rxtx ? "rx" : "tx", call_esc, text_esc, status);
    if (len < 0)
        return 0;

    if (out_seq)
        *out_seq = e->seq;
    return (size_t)len;
}

// Builds and sends one packet per enabled channel from a shared "info"
// field (":ADDRESSEE:text{id" or ":ADDRESSEE:ackNNN"), rather than one
// packet reused verbatim on both channels.
//
// The RF leg gets the operator-configured digipeater path (g_config.msg_path,
// resolved by aprs_path_build_suffix_from_config()). The APRS-IS leg does not:
// WIDEn-N aliases are RF-only and meaningless (and misleading to other IS
// clients/servers) once a packet is injected straight into APRS-IS. Per
// APRS-IS convention, locally-originated traffic sent to the IS network
// carries a "TCPIP*" q-construct tag in its path instead of an RF unproto path
// - see aprsc/javAPRSSrvr behavior and the APRS-IS server spec - so the IS leg
// carries "TCPIP*" rather than something like "WIDE1-1,WIDE2-1".
static void txPacket(const char *myCall, const char *info) {
    if (!s_txHandler) {
        ESP_LOGW(TAG, "No TX handler registered, dropping: %s", info);
        return;
    }
    // str_append() reports whether the whole frame fit and leaves len at the
    // number of characters actually written. Both matter here: len is handed
    // straight to the TX handler, which memcpy()s and send()s exactly that
    // many bytes from packet[], so it has to be a count of what is really in
    // the buffer. A frame that does not fit is dropped with a warning rather
    // than sent short - the same discipline the beacon builders follow -
    // because a truncated APRS message loses its trailing "{id" sequence
    // suffix and would never be acked.
    if (g_config.msg_rf) {
        // The path presets are snapshotted under app_config_lock() inside the
        // builder; msg_path itself is a single byte, read here the same way the
        // msg_rf/msg_inet flags around it are.
        char path[80];
        aprs_path_build_suffix_from_config(g_config.msg_path, path, sizeof(path));
        char packet[400];
        size_t len = 0;
        if (str_append(packet, sizeof(packet), &len, "%s>" APRS_TOCALL "%s:%s", myCall, path, info))
            s_txHandler(packet, len, MSG_CHANNEL_RF);
        else
            ESP_LOGW(TAG, "RF message too long for a %u byte frame, dropped: %s", (unsigned)sizeof(packet), info);
    }
    if (g_config.msg_inet) {
        char packet[400];
        size_t len = 0;
        if (str_append(packet, sizeof(packet), &len, "%s>" APRS_TOCALL ",TCPIP*:%s", myCall, info))
            s_txHandler(packet, len, MSG_CHANNEL_INET);
        else
            ESP_LOGW(TAG, "INET message too long for a %u byte frame, dropped: %s", (unsigned)sizeof(packet), info);
    }
}

// Copies as much of src into dst as fits in APRS_MSG_TEXT_STD_MAX bytes,
// dropping any '|', '~' and '{' along the way: APRS101 chapter 14 reserves
// those characters for telemetry and the message-number delimiter, so none of
// them may appear inside ordinary message text regardless of what the caller
// passed in. dst is always NUL-terminated; dst_size must be at least 1.
static void sanitizeOutgoingText(const char *src, char *dst, size_t dst_size) {
    size_t out = 0;
    for (size_t i = 0; src[i] && out < dst_size - 1 && out < APRS_MSG_TEXT_STD_MAX; i++) {
        char c = src[i];
        if (c == '|' || c == '~' || c == '{')
            continue;
        dst[out++] = c;
    }
    dst[out] = 0;
}

void sendAPRSMessage(const char *toCall, const char *text) {
    if (!toCall[0] || !text[0])
        return;
    // Bump the outbound APRS message number. It must always increment and must
    // never come out as 0: a "{0" suffix is read by many APRS clients (UI-View
    // / APRSIS32 / Xastir) as "no message number", so the message would never
    // get acked. Past MSG_ID_MAX it wraps back to 1, which keeps the number two
    // digits wide and leaves room for the Reply-ACK suffix inside the five
    // characters APRS101 allows a message identifier.
    if (++s_msgID > MSG_ID_MAX)
        s_msgID = 1;

    char myCallUp[10];
    app_config_lock();
    strncpy(myCallUp, g_config.msg_mycall, sizeof(myCallUp) - 1);
    app_config_unlock();
    myCallUp[sizeof(myCallUp) - 1] = 0;
    trimUpper(myCallUp);

    char toCallUp[10];
    strncpy(toCallUp, toCall, sizeof(toCallUp) - 1);
    toCallUp[sizeof(toCallUp) - 1] = 0;
    trimUpper(toCallUp);

    char toCallFixed[10];
    memset(toCallFixed, ' ', 9);
    toCallFixed[9] = 0;
    memcpy(toCallFixed, toCallUp, strlen(toCallUp) > 9 ? 9 : strlen(toCallUp));

    // Enforced here rather than trusted to the caller, so the on-air text
    // length and character set stay correct regardless of where the call
    // came from: truncated to the standard APRS message text length and
    // stripped of the characters APRS101 reserves for telemetry and the
    // message-number delimiter.
    char payload[APRS_MSG_TEXT_STD_MAX + 1];
    sanitizeOutgoingText(text, payload, sizeof(payload));

    char suffix[8];
    buildMsgNumberSuffix(toCallUp, s_msgID, suffix, sizeof(suffix));

    char info[320];
    snprintf(info, sizeof(info), ":%s:%s%s", toCallFixed, payload, suffix);

    txPacket(myCallUp, info);
    ESP_LOGD(TAG, "Send APRS message to %s msgID %u: %s", toCall, (unsigned)s_msgID, info);

    int8_t ackVal = (g_config.msg_retry == 0) ? -2 : (int8_t)g_config.msg_retry;
    // The trimmed upper-case addressee is what goes in the queue, matching the
    // form the callsign takes on the air. An incoming ack is matched against
    // this field by exact comparison, and it is also the name the chat page
    // shows next to the message. The sanitized payload is stored rather than
    // the caller's raw text, so a retry re-sends exactly what was already put
    // on the air instead of re-deriving it.
    pkgMsgStore(toCallUp, payload, s_msgID, ackVal, false, false);
}

void sendAPRSAck(const char *toCall, const char *msgNo) {
    char toCallFixed[10];
    memset(toCallFixed, ' ', 9);
    toCallFixed[9] = 0;
    size_t n = strlen(toCall);
    memcpy(toCallFixed, toCall, n > 9 ? 9 : n);

    char myCall[10];
    app_config_lock();
    memcpy(myCall, g_config.msg_mycall, sizeof(myCall));
    app_config_unlock();
    // The copy above takes the full field width, so termination depends on
    // what the config loader stored. Force it: everything downstream treats
    // this as a C string, and a field filled edge to edge would send it
    // reading past the end of the local buffer.
    myCall[sizeof(myCall) - 1] = 0;

    char info[160];
    snprintf(info, sizeof(info), ":%s:ack%s", toCallFixed, msgNo);
    txPacket(myCall, info);
    ESP_LOGD(TAG, "Send APRS ACK to %s msgNo %s", toCall, msgNo);
}

int message_send_pending_to(const char *toCall) {
    if (toCall == NULL || toCall[0] == 0)
        return 0;

    char myCall[10];
    app_config_lock();
    memcpy(myCall, g_config.msg_mycall, sizeof(myCall));
    app_config_unlock();
    // The copy above takes the full field width, so termination depends on
    // what the config loader stored. Force it: everything downstream treats
    // this as a C string, and a field filled edge to edge would send it
    // reading past the end of the local buffer.
    myCall[sizeof(myCall) - 1] = 0;

    int sent = 0;
    int held = 0;
    for (int i = 0; i < MSG_QUEUE_SIZE; i++) {
        // ack > 0 is the "outbound and still waiting for an ack" state; an
        // acked (-2), rejected (-3) or received (-1) entry is not something
        // this station is holding for the querying operator.
        if (!s_queue[i].used || s_queue[i].rxtx || s_queue[i].ack <= 0)
            continue;
        if (!callsignBaseMatch(s_queue[i].callsign, toCall))
            continue;

        // One query answers with a handful of frames, not with the whole
        // queue: MSG_QUERY_BURST_MAX bounds how long the transmitter can stay
        // keyed on behalf of a single question. The rest of the loop only
        // counts what is left over - those slots keep their retry state, so
        // sendAPRSMessageRetry() carries them at the configured interval,
        // which is the paced path this traffic already has.
        if (sent >= MSG_QUERY_BURST_MAX) {
            held++;
            continue;
        }

        // The retry counter and timestamp are deliberately left alone: this
        // transmission answers a query, so it must not spend one of the
        // message's own delivery attempts nor push the next scheduled retry
        // out by one interval.
        char toCallFixed[10];
        memset(toCallFixed, ' ', 9);
        toCallFixed[9] = 0;
        size_t n = strlen(s_queue[i].callsign);
        memcpy(toCallFixed, s_queue[i].callsign, n > 9 ? 9 : n);

        char payload[300];
        strncpy(payload, s_queue[i].text, sizeof(payload) - 1);
        payload[sizeof(payload) - 1] = 0;

        char suffix[8];
        buildMsgNumberSuffix(s_queue[i].callsign, s_queue[i].msgID, suffix, sizeof(suffix));

        char info[320];
        snprintf(info, sizeof(info), ":%s:%s%s", toCallFixed, payload, suffix);
        txPacket(myCall, info);
        sent++;
    }

    if (sent > 0) {
        if (held > 0)
            ESP_LOGI(TAG, "?APRSM from %s answered with %d pending message(s), %d left to the retry timer", toCall, sent, held);
        else
            ESP_LOGI(TAG, "?APRSM from %s answered with %d pending message(s)", toCall, sent);
    }
    return sent;
}

void sendAPRSMessageRetry(void) {
    time_t now = time(NULL);

    // Snapshot the own-call once, so a web save can't rewrite it mid-loop
    // while frames are being built.
    char myCall[10];
    app_config_lock();
    memcpy(myCall, g_config.msg_mycall, sizeof(myCall));
    app_config_unlock();
    // The copy above takes the full field width, so termination depends on
    // what the config loader stored. Force it: everything downstream treats
    // this as a C string, and a field filled edge to edge would send it
    // reading past the end of the local buffer.
    myCall[sizeof(myCall) - 1] = 0;

    for (int i = 0; i < MSG_QUEUE_SIZE; i++) {
        if (!s_queue[i].used || s_queue[i].ack <= 0)
            continue;
        if ((now - s_queue[i].last_tx) <= g_config.msg_interval)
            continue;

        // Stamp the moment this retry is sent. The guard above compares
        // (now - last_tx) against msg_interval, so the timestamp must be the
        // instant of the last transmission for the next attempt to fall
        // exactly one msg_interval later. It is a separate field from the
        // creation time the chat page displays, which stays put: a message is
        // dated when it was written, not when it was last put on the air.
        if (--s_queue[i].ack > 0)
            s_queue[i].last_tx = now;

        char toCallFixed[10];
        memset(toCallFixed, ' ', 9);
        toCallFixed[9] = 0;
        size_t n = strlen(s_queue[i].callsign);
        memcpy(toCallFixed, s_queue[i].callsign, n > 9 ? 9 : n);

        char payload[300];
        strncpy(payload, s_queue[i].text, sizeof(payload) - 1);
        payload[sizeof(payload) - 1] = 0;

        char suffix[8];
        buildMsgNumberSuffix(s_queue[i].callsign, s_queue[i].msgID, suffix, sizeof(suffix));

        char info[320];
        snprintf(info, sizeof(info), ":%s:%s%s", toCallFixed, payload, suffix);
        txPacket(myCall, info);
        ESP_LOGD(TAG, "Retry APRS message[%d] to %s msgID %u ack left %d", i, s_queue[i].callsign, (unsigned)s_queue[i].msgID, s_queue[i].ack);
    }
}

// ---------------------------------------------------------------------------
// Incoming
// ---------------------------------------------------------------------------
void handleIncomingAPRS(const char *line, query_source_t source) {
    const char *msgMarker = strstr(line, "::");
    if (msgMarker == NULL || msgMarker == line)
        return;

    char fromCall[16] = { 0 };
    const char *gt = strchr(line, '>');
    if (gt) {
        size_t n = (size_t)(gt - line);
        if (n >= sizeof(fromCall))
            n = sizeof(fromCall) - 1;
        memcpy(fromCall, line, n);
    }
    // Callsigns travel upper case on the air; normalising the sender here keeps
    // one spelling for the whole path it feeds - the queue entry the chat page
    // shows, the exact match that pairs an incoming ack with the outbound
    // message it acknowledges, and the addressee of the ack sent back.
    trimUpper(fromCall);

    const char *payload = msgMarker + 2;
    if (strlen(payload) < 10)
        return;

    char toCall[12] = { 0 };
    memcpy(toCall, payload, 9);
    trimUpper(toCall);

    if (strcasecmp(fromCall, toCall) == 0)
        return; // message to self, ignore

    const char *colon = strchr(payload + 9, ':');
    if (colon == NULL)
        return;

    // Directed query ("CALL:?query?") shares this same ":ADDRESSEE:" framing
    // but is not an APRS message: hand it to the query responder's own
    // addressee-parsing entry point instead of duplicating it here, and skip
    // the message-specific logic below entirely. Checked - and dispatched -
    // regardless of g_config.msg_enable, since the two features are
    // independently enabled (g_config.query_en gates it inside query.c).
    if (colon[1] == '?') {
        // The whole line goes along with the split-out fields: a "?APRST" /
        // "?PING?" answer reports the route the query travelled, which only
        // the unparsed line still carries. The source travels with it too,
        // since it decides both whether the query is answered and where the
        // answer goes.
        query_process_directed(fromCall, toCall, colon + 1, line, source);
        return;
    }

    if (!g_config.msg_enable)
        return;

    char message[300] = { 0 };
    strncpy(message, colon + 1, sizeof(message) - 1);
    // trim trailing whitespace/CR
    size_t mlen = strlen(message);
    while (mlen > 0 && isspace((unsigned char)message[mlen - 1]))
        message[--mlen] = 0;

    char msgNo[12] = { 0 };
    char *brace = strchr(message, '{');
    if (brace && brace[1]) {
        strncpy(msgNo, brace + 1, sizeof(msgNo) - 1);
        *brace = 0;
        mlen = strlen(message);
        while (mlen > 0 && isspace((unsigned char)message[mlen - 1]))
            message[--mlen] = 0;
    }

    // A message text starting with "ack"/"rej" is only classified as such when
    // the remainder is a valid message-number field, exactly as APRS101 defines
    // the literal "ack"/"rej" followed by the message number and nothing else;
    // otherwise it is ordinary text ("acknowledged, thanks", "rejoining net")
    // and falls through to the message-storing path below.
    bool isAck = (strncmp(message, "ack", 3) == 0) && isValidMsgNoField(message + 3);
    if (isAck) {
        strncpy(msgNo, message + 3, sizeof(msgNo) - 1);
        msgNo[sizeof(msgNo) - 1] = 0;
    }

    bool isRej = (strncmp(message, "rej", 3) == 0) && isValidMsgNoField(message + 3);
    if (isRej) {
        strncpy(msgNo, message + 3, sizeof(msgNo) - 1);
        msgNo[sizeof(msgNo) - 1] = 0;
    }

    // Reply-ACK split. msgNo keeps the identifier whole, because an ack has to
    // quote it back exactly as it arrived; ownNo is the sender's own number and
    // replyAck the free acknowledgement riding with it, empty when the sender
    // sent none. On an "ackMM}AA" line the trailing part is this station's own
    // free acknowledgement quoted back, not an acknowledgement of anything, so
    // only ownNo is used there.
    char ownNo[8];
    char replyAck[8];
    splitReplyAck(msgNo, ownNo, sizeof(ownNo), replyAck, sizeof(replyAck));

    ESP_LOGD(TAG, "Message from %s to %s: %s", fromCall, toCall, message);

    // Accept the message either addressed to this station - the addressee's
    // base callsign matches ours regardless of SSID on either side, so a
    // message to "N0CALL", "N0CALL-7", etc. is accepted as long as the
    // configured mycall is "N0CALL", with or without its own SSID - or
    // addressed to a message group this station reads (APRS101 chapter 14):
    // the built-in "ALL"/"QST"/"CQ" set, or one of g_config.msg_group[].
    // isDirect is what the rest of this function routes every "acknowledge
    // it" / "remember a Reply-ACK for it" / "pulse the alarm for it" decision
    // on, never on acceptance alone: a group has no single owner to send an
    // ack back, and every member reading it would otherwise answer at once.
    bool isDirect = callsignBaseMatch(toCall, g_config.msg_mycall);
    bool isGroup = !isDirect && isGroupAddressee(toCall);
    if (!isDirect && !isGroup)
        return;

    // An ack or rej always carries a message ID (it's the ID of the queued
    // message being acknowledged/rejected); without one there is nothing to
    // match against a queued outgoing message, so it can't be processed. A
    // group address can't carry one either: an ack/rej is a reply to this
    // station's own outbound message, which is only ever sent to a callsign,
    // never to a group.
    if ((isAck || isRej) && (msgNo[0] == 0 || isGroup))
        return;

    uint16_t number;

    if (isAck) {
        int i = parseMsgNumber(ownNo, &number) ? pkgMsg_Find(fromCall, number, false) : -1;
        if (i >= 0)
            s_queue[i].ack = -2; // acked
        return;
    }

    if (isRej) {
        int i = parseMsgNumber(ownNo, &number) ? pkgMsg_Find(fromCall, number, false) : -1;
        if (i >= 0)
            s_queue[i].ack = -3; // rejected by recipient: stop retrying, don't count as delivered
        return;
    }

    char decoded[300];
    strncpy(decoded, message, sizeof(decoded) - 1);
    decoded[sizeof(decoded) - 1] = 0;

    size_t dlen = strlen(decoded);
    while (dlen > 0 && isspace((unsigned char)decoded[dlen - 1]))
        decoded[--dlen] = 0;
    if (dlen == 0)
        return;

    // A free acknowledgement riding with the message clears the outbound
    // message it names, so a reply doubles as the ack for what it replies to
    // and no separate "ackNN" has to survive the return path. Meaningless for
    // a group message - this station never sent an outbound message to a
    // group for one to acknowledge - so skipped entirely for one.
    if (!isGroup && parseMsgNumber(replyAck, &number)) {
        int i = pkgMsg_Find(fromCall, number, false);
        if (i >= 0)
            s_queue[i].ack = -2;
    }

    // Every received message is a new line of the conversation and gets a queue
    // slot of its own; only a copy of one already in the history is left out,
    // and even then the ack below still goes back out for a direct message,
    // since a repeat means the sender never heard the first one. The message
    // is identified by the sender's own number and its direct/group status, so
    // two copies carrying different free acknowledgements are still one
    // message, while a group message and a direct message that happen to
    // share a sender and number are two.
    uint16_t rxID = 0;
    parseMsgNumber(ownNo, &rxID);
    if (pkgMsgFindRepeat(fromCall, decoded, rxID, true, isGroup) < 0)
        pkgMsgStore(fromCall, decoded, rxID, -1, true, isGroup);

    // A group message is never acked, never retransmitted and never
    // auto-replied to, "{id" suffix or not - it has no single owner to answer
    // it, and answering on behalf of the whole group would key the
    // transmitter for every member that heard it. Per APRS101 the message ID
    // is otherwise optional: for a direct message, only send an ack when the
    // sender actually requested one. That ack quotes the identifier whole,
    // Reply-ACK suffix included, and the sender's own number becomes the
    // acknowledgement owed back to that station.
    if (!isGroup) {
        if (msgNo[0] != 0) {
            sendAPRSAck(fromCall, msgNo);
            replyAckRemember(fromCall, ownNo);
        }
        message_alarm_pulse();
    }
}
