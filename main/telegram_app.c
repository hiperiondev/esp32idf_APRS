// @file telegram_app.c
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
// @brief Telegram bot subsystem: the /storage/telegram.json store, the
// supervised bring-up of the telegram_service component, and the diagnosis the
// web admin renders.
//
// See telegram_app.h for the design rationale (why the bot's settings live in
// their own file instead of config.json, and why bring-up runs on a supervisor
// task instead of being called inline).

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "app_config.h"
#include "aprs_service.h"     // APRS_SOFTWARE_NAME
#include "esp_telegram_bot.h" // telegram_bot_get_me()
#include "json_escape.h"      // json_write_escaped()
#include "json_store.h"       // shared JSON-file store scaffolding
#include "net_state.h"        // net_state_is_connected()
#include "sensors_local.h"    // sensors_local_channel_name()
#include "storage.h"          // storage_write_lock()
#include "str_append.h"       // str_append()
#include "telegram_app.h"
#include "telegram_service.h"
#include "telemetry.h" // telemetry_config_load(), telemetry_get_values()
#include "weather.h"   // weather_field_label()/_snapshot()/_format()

static const char *TAG = "telegram_app";

#define TELEGRAM_APP_TMP_PATH "/storage/telegram.json.tmp"

// Worker task geometry.
//
// The stack is sized for a TLS handshake, which the getMe probe performs on
// this task, and that is the only reason it is this large. The task exists
// only while a bring-up or a teardown is in progress and exits as soon as the
// step is done, so those bytes are borrowed for a few seconds rather than held
// for the life of the bot: on this board the difference is most of the margin
// a polling connection needs to survive being rebuilt.
//
// Everything the bot needs while it is merely running - copying six counters,
// noticing that the uplink went away, noticing that a retry is due - is done
// by telegram_app_tick_1hz() on the existing 1 Hz service tick, which needs no
// stack of its own.
#define TELEGRAM_WORKER_STACK_BYTES 8192
#define TELEGRAM_WORKER_PRIORITY    4

// Pause before a failed bring-up is attempted again. Applied only to the
// failures that can clear on their own - no route yet, a TLS call that did not
// complete, a heap that was momentarily too fragmented. A configuration error
// is never retried: nothing changes until the operator changes it, and a
// station that re-reported the same fault every minute would bury the log.
#define TELEGRAM_RETRY_SECONDS 60

// Heap a bring-up must find before it is attempted, as two separate tests.
//
// They are separate because a TLS handshake asks the heap for two different
// shapes at once. The record buffers are single allocations sized by
// CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN and _OUT_CONTENT_LEN, so what governs them
// is the largest CONTIGUOUS block; a heap with plenty free in small pieces
// fails on those. Everything else the handshake consumes - the key exchange
// scalars, the certificate parse, the session context - is a crowd of small
// allocations, so what governs those is TOTAL free heap and fragmentation
// barely matters.
//
// Testing one figure against both needs would either reject a heap that could
// have served (a large total in moderate pieces) or accept one that cannot (a
// single big block with nothing behind it). So each requirement is checked
// against the figure that actually decides it.
//
// The contiguous floor is the input record buffer plus slack for its header
// and allocator overhead. The total floor is set from the measured peak of a
// successful handshake on this firmware with room to spare, since running the
// heap to its last few hundred bytes is survivable exactly once.
#define TELEGRAM_MIN_FREE_BLOCK (CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN + 4096)
#define TELEGRAM_MIN_FREE_HEAP  32768

// Host and port the bot talks to, used by the pre-flight probe below.
#define TELEGRAM_PROBE_HOST "api.telegram.org"
#define TELEGRAM_PROBE_PORT "443"

// Deadline for each stage of the pre-flight probe. Shorter than the transport's
// own 15 s so a station with no route reports which stage stalled instead of
// spending the transport's whole budget and reporting only that something did.
#define TELEGRAM_PROBE_DNS_MS     5000
#define TELEGRAM_PROBE_CONNECT_MS 5000

// Bound on the getMe answer. The document carries the bot's identifiers and
// its capability flags and nothing else, so this is several times what
// Telegram actually returns; the transport reports truncation explicitly, and
// the probe below treats it as a failed probe rather than guessing.
#define TELEGRAM_GETME_BUF 768

// Bound on one rendered sensor reading, measurement unit included. Wide enough
// for a telemetry channel carrying the longest unit an operator can enter
// alongside its value; a longer rendering is truncated rather than overflowing.
#define TELEGRAM_APP_VALUE_MAX 32

// Renders a service switch the way /status reports it.
#define TELEGRAM_ON_OFF(flag) ((flag) ? "on" : "off")

// Serializes load/save between the web save handler and the supervisor task.
static SemaphoreHandle_t s_lock;

// Guards the published snapshot. Held only across the handful of stores that
// build one, never across anything that can block.
static SemaphoreHandle_t s_status_lock;

static telegram_app_status_t s_status = {
    .state = TELEGRAM_APP_STATE_DISABLED,
    .reason = TELEGRAM_APP_REASON_DISABLED,
};

// Configuration the supervisor is working from. Loaded by
// telegram_app_apply_config() before the task is created, so the task never
// touches the filesystem itself, and static so the pointers handed to
// telegram_init() stay valid for as long as the service holds them.
static telegram_app_config_t s_cfg;

// What the worker task should do the next time one is spawned. Written by
// telegram_app_apply_config() and by the tick, read by the worker.
typedef enum {
    TELEGRAM_ACTION_NONE = 0, /**< Nothing pending. */
    TELEGRAM_ACTION_START,    /**< Bring the service up. */
    TELEGRAM_ACTION_STOP,     /**< Release the service. */
} telegram_action_t;

static volatile telegram_action_t s_action;
static volatile bool s_worker_alive;

// True between a successful bring-up and the teardown that follows it. Says
// that the service owns a task, a client pair and a TLS session, which is what
// decides whether a stop has real work to do.
static volatile bool s_service_up;

// Seconds remaining before a deferred bring-up is attempted again, or
// TELEGRAM_RETRY_NEVER when the fault is one only the operator can clear.
#define TELEGRAM_RETRY_NEVER INT32_MAX
static volatile int32_t s_retry_in;

// True once Telegram has accepted this token. Cleared whenever the operator
// saves the page, which is the only way the token can change.
//
// The getMe probe is what turns "configured" into "connected", and it is worth
// its cost the first time. On a retry it is not: the token has not changed, and
// the probe opens a second TLS session whose handshake peak lands on the same
// heap the session being retried needs. Skipping it there removes that peak
// from every attempt after the first.
static volatile bool s_token_verified;

// The operator's intent, kept separate from whether the service actually
// reached Telegram. Written under s_lock alongside s_cfg.
static bool s_enabled;

static void lock(void) {
    json_store_lock_take(&s_lock);
}

static void unlock(void) {
    json_store_lock_give(&s_lock);
}

// ---------------------------------------------------------------------------
// Published status
// ---------------------------------------------------------------------------

static void status_lock_ensure(void) {
    json_store_lock_ensure(&s_status_lock);
}

// Replaces the state and the diagnosis, leaving the counters alone. detail may
// be NULL for the reasons that need no free-form text.
static void status_set(telegram_app_state_t state, telegram_app_reason_t reason, const char *detail) {
    status_lock_ensure();
    if (s_status_lock != NULL)
        xSemaphoreTake(s_status_lock, portMAX_DELAY);

    s_status.state = state;
    s_status.reason = reason;
    if (detail != NULL)
        snprintf(s_status.detail, sizeof(s_status.detail), "%s", detail);
    else
        s_status.detail[0] = 0;

    if (s_status_lock != NULL)
        xSemaphoreGive(s_status_lock);
}

// Clears everything a previous run published, including the bot name and the
// counters, so a service that is taken down does not leave figures on screen
// that no longer describe anything.
static void status_reset(telegram_app_state_t state, telegram_app_reason_t reason) {
    status_lock_ensure();
    if (s_status_lock != NULL)
        xSemaphoreTake(s_status_lock, portMAX_DELAY);

    memset(&s_status, 0, sizeof(s_status));
    s_status.state = state;
    s_status.reason = reason;

    if (s_status_lock != NULL)
        xSemaphoreGive(s_status_lock);
}

static void status_set_bot_name(const char *name) {
    status_lock_ensure();
    if (s_status_lock != NULL)
        xSemaphoreTake(s_status_lock, portMAX_DELAY);

    snprintf(s_status.bot_name, sizeof(s_status.bot_name), "%s", name != NULL ? name : "");

    if (s_status_lock != NULL)
        xSemaphoreGive(s_status_lock);
}

// Folds one reading of the service's own counters into the snapshot.
static void status_set_counters(const telegram_stats_t *st) {
    status_lock_ensure();
    if (s_status_lock != NULL)
        xSemaphoreTake(s_status_lock, portMAX_DELAY);

    s_status.has_counters = true;
    s_status.updates_received = st->updates_received;
    s_status.commands_handled = st->commands_handled;
    s_status.messages_sent = st->messages_sent;
    s_status.rejected = st->rejected;
    s_status.poll_errors = st->poll_errors;
    s_status.uptime_seconds = st->uptime_seconds;

    if (s_status_lock != NULL)
        xSemaphoreGive(s_status_lock);
}

void telegram_app_status(telegram_app_status_t *out) {
    if (out == NULL)
        return;

    status_lock_ensure();
    if (s_status_lock != NULL)
        xSemaphoreTake(s_status_lock, portMAX_DELAY);

    *out = s_status;

    if (s_status_lock != NULL)
        xSemaphoreGive(s_status_lock);
}

bool telegram_app_enabled(void) {
    lock();
    bool en = s_enabled;
    unlock();
    return en;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

// Copies a bounded string member out of a JSON object, leaving the destination
// empty when the member is absent or is not a string.
static void get_string(const cJSON *obj, const char *key, char *out, size_t out_size) {
    out[0] = 0;
    const cJSON *v = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(v) && v->valuestring != NULL)
        snprintf(out, out_size, "%s", v->valuestring);
}

// Reads a Telegram identifier. Both forms a hand-edited file can carry are
// accepted: the JSON number the example uses, and a quoted string, which is
// what a person copying an identifier out of a Telegram client is most likely
// to paste. valuedouble is what cJSON exposes for a number, and a double holds
// every Telegram identifier exactly - they are far below 2^53 - so the cast is
// lossless for any value the servers issue.
static int64_t get_id(const cJSON *obj, const char *key) {
    const cJSON *v = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(v))
        return (int64_t)v->valuedouble;
    if (cJSON_IsString(v) && v->valuestring != NULL)
        return (int64_t)strtoll(v->valuestring, NULL, 10);
    return 0;
}

// Reads one "users" / "chats" array into a bounded table, reporting entries
// that did not fit rather than dropping them in silence.
static uint8_t load_peers(const cJSON *doc, const char *key, telegram_app_peer_t *out, uint8_t max) {
    const cJSON *arr = cJSON_GetObjectItem(doc, key);
    if (!cJSON_IsArray(arr))
        return 0;

    int n = cJSON_GetArraySize(arr);
    if (n > (int)max) {
        ESP_LOGW(TAG, "%s holds %d entries, only the first %u are kept", key, n, (unsigned)max);
        n = (int)max;
    }

    uint8_t count = 0;
    for (int i = 0; i < n; i++) {
        const cJSON *o = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsObject(o))
            continue;
        int64_t id = get_id(o, "id");
        if (id == 0)
            continue;
        out[count].id = id;
        get_string(o, "name", out[count].name, sizeof(out[count].name));
        count++;
    }
    return count;
}

// Reported by load_locked() so the reason published to the page distinguishes
// a file that is absent from one that is present and unusable.
static json_store_status_t s_last_read_status = JSON_STORE_MISSING;

static bool load_locked(telegram_app_config_t *out) {
    memset(out, 0, sizeof(*out));

    cJSON *doc = NULL;
    json_store_status_t st = json_store_read(TELEGRAM_APP_PATH, TAG, "telegram settings", &doc);
    s_last_read_status = st;
    if (st != JSON_STORE_OK)
        return false;

    // Absent means off. A file copied verbatim from the project's example
    // carries no "enabled" key, and a station that has never been configured
    // must not start reaching the internet because a file appeared.
    const cJSON *v = cJSON_GetObjectItem(doc, "enabled");
    out->enable = cJSON_IsTrue(v);

    get_string(doc, "bot_token", out->bot_token, sizeof(out->bot_token));
    get_string(doc, "web_app_url", out->web_app_url, sizeof(out->web_app_url));
    out->admin_id = get_id(doc, "admin_id");
    out->user_count = load_peers(doc, "users", out->users, TELEGRAM_APP_USERS_MAX);
    out->chat_count = load_peers(doc, "chats", out->chats, TELEGRAM_APP_CHATS_MAX);

    cJSON_Delete(doc);
    return true;
}

// Writes one "users" / "chats" array.
static void save_peers(FILE *f, const char *key, const telegram_app_peer_t *in, uint8_t count) {
    fprintf(f, ",\"%s\":[", key);
    for (uint8_t i = 0; i < count; i++) {
        fputs(i ? ",{\"id\":" : "{\"id\":", f);
        fprintf(f, "%" PRId64 ",\"name\":", in[i].id);
        json_write_escaped(f, in[i].name);
        fputc('}', f);
    }
    fputc(']', f);
}

static bool save_locked(const telegram_app_config_t *in) {
    // Entered with s_lock held (see telegram_app_save() below), which is what
    // json_store_open_tmp() asserts before handing back a stream whose stdio
    // buffer is already pinned.
    FILE *f = json_store_open_tmp(TELEGRAM_APP_TMP_PATH, TAG, s_lock);
    if (f == NULL)
        return false;

    // Written token by token straight to the file: no cJSON tree and no second
    // serialized buffer ever exist, so a save costs essentially only littlefs's
    // own write buffer on top of the stream buffer above.
    //
    // The member order matches the project's example file, with the enable
    // switch first, so a file this firmware writes and a file the operator
    // hand-edits read the same way.
    fputs("{\"enabled\":", f);
    fputs(in->enable ? "true" : "false", f);
    fputs(",\"bot_token\":", f);
    json_write_escaped(f, in->bot_token);
    fprintf(f, ",\"admin_id\":%" PRId64, in->admin_id);
    fputs(",\"web_app_url\":", f);
    json_write_escaped(f, in->web_app_url);
    save_peers(f, "users", in->users, in->user_count);
    save_peers(f, "chats", in->chats, in->chat_count);
    fputc('}', f);

    return json_store_commit(f, TELEGRAM_APP_TMP_PATH, TELEGRAM_APP_PATH, TAG, "telegram settings");
}

bool telegram_app_load(telegram_app_config_t *out) {
    if (out == NULL)
        return false;
    lock();
    bool ok = load_locked(out);
    unlock();
    return ok;
}

bool telegram_app_save(const telegram_app_config_t *in) {
    if (in == NULL)
        return false;
    lock();
    // Module lock first, filesystem-wide writer gate second (storage.h): the
    // temp-file + rename sequence inside save_locked() must not overlap the
    // whole-partition format the web Storage page can start.
    storage_write_lock();
    bool ok = save_locked(in);
    storage_write_unlock();
    unlock();
    return ok;
}

// ---------------------------------------------------------------------------
// Bring-up
// ---------------------------------------------------------------------------

// A token issued by @BotFather is "<numeric bot id>:<secret>". Checking the
// shape here is worth the few lines: it separates a token that was pasted
// short, wrapped, or copied with the surrounding quotes from one Telegram will
// actually look at, and those two faults need entirely different advice.
static bool token_well_formed(const char *token) {
    const char *colon = strchr(token, ':');
    if (colon == NULL || colon == token)
        return false;
    for (const char *p = token; p < colon; p++) {
        if (*p < '0' || *p > '9')
            return false;
    }
    return colon[1] != '\0';
}

#if !CONFIG_TELEGRAM_BOT_CERT_BUNDLE
// Checked before the transport is initialized purely so the failure can be
// named. The transport applies its own, stricter test when it loads the file;
// what it cannot do is tell the operator which of the two things to do about
// it, because by then the distinction between "no file" and "not a
// certificate" has collapsed into one error code.
static telegram_app_reason_t cert_precheck(char *detail, size_t detail_size) {
    const char *path = CONFIG_TELEGRAM_BOT_CERT_PATH;

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        snprintf(detail, detail_size, "%s", path);
        return TELEGRAM_APP_REASON_CERT_MISSING;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);

    if (size <= 0 || size > CONFIG_TELEGRAM_BOT_CERT_MAX_LEN) {
        snprintf(detail, detail_size, "%s (%ld bytes)", path, size);
        return TELEGRAM_APP_REASON_CERT_INVALID;
    }
    return TELEGRAM_APP_REASON_CONNECTED;
}
#endif

// Extra lines appended to the built-in /status answer, so the bot reports the
// station rather than only the board it runs on. Called from the service task
// with a buffer it owns.
//
// Every service that has a master switch is listed, whether it is on or off:
// an absent line would be indistinguishable from a service this firmware does
// not carry, and the whole point of the answer is to say what this station is
// doing right now. The telemetry switch is the one that does not live in
// g_config - the telemetry subsystem keeps its settings in their own file -
// so it is read through its own loader.
static void telegram_status_lines(char *buffer, size_t size, void *ctx) {
    (void)ctx;

    app_config_lock();
    char call[sizeof(g_config.aprs_mycall)];
    snprintf(call, sizeof(call), "%s", g_config.aprs_mycall);
    bool igate = g_config.igate_en;
    bool digi = g_config.digi_en;
    bool trk = g_config.trk_en;
    bool wx = g_config.wx_en;
    bool msg = g_config.msg_enable;
    bool query = g_config.query_en;
    bool bm = g_config.bm_en;
    bool gps = g_config.gps_en;
    bool modem = g_config.audio_modem_en;
    bool duty = g_config.duty_cycle_en;
    bool ntp = g_config.synctime;
    app_config_unlock();

    // Loaded on the heap: the telemetry settings are a large structure and
    // this runs on the service task, whose stack is sized for a TLS handshake
    // rather than for a copy of it.
    bool tlm = false;
    telemetry_config_t *tcfg = malloc(sizeof(*tcfg));
    if (tcfg != NULL) {
        telemetry_config_load(tcfg);
        tlm = tcfg->en;
        free(tcfg);
    }

    size_t used = 0;
    str_append(buffer, size, &used, "Callsign: %s\n", call);
    str_append(buffer, size, &used, "IGate: %s\n", TELEGRAM_ON_OFF(igate));
    str_append(buffer, size, &used, "Digipeater: %s\n", TELEGRAM_ON_OFF(digi));
    str_append(buffer, size, &used, "Tracker: %s\n", TELEGRAM_ON_OFF(trk));
    str_append(buffer, size, &used, "Weather: %s\n", TELEGRAM_ON_OFF(wx));
    str_append(buffer, size, &used, "Telemetry: %s\n", TELEGRAM_ON_OFF(tlm));
    str_append(buffer, size, &used, "Messaging: %s\n", TELEGRAM_ON_OFF(msg));
    str_append(buffer, size, &used, "Query responder: %s\n", TELEGRAM_ON_OFF(query));
    str_append(buffer, size, &used, "BrandMeister: %s\n", TELEGRAM_ON_OFF(bm));
    str_append(buffer, size, &used, "GNSS receiver: %s\n", TELEGRAM_ON_OFF(gps));
    str_append(buffer, size, &used, "AFSK modem: %s\n", TELEGRAM_ON_OFF(modem));
    str_append(buffer, size, &used, "TX duty-cycle limiter: %s\n", TELEGRAM_ON_OFF(duty));
    str_append(buffer, size, &used, "SNTP time sync: %s\n", TELEGRAM_ON_OFF(ntp));
    str_append(buffer, size, &used, "Free heap: %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

// Lines appended to the built-in /sensors answer: every weather field and
// every telemetry channel the operator has enabled, with the sensor driver
// each one is mapped to and its current reading.
//
// Built from the live configuration on every call rather than from sensors
// registered once at bring-up, because the set is the operator's to change:
// a save on the Weather or Telemetry page re-maps channels while the bot
// keeps running, and the next answer must describe what is mapped then.
//
// Enabled but unresolved is reported as such instead of being left out. A
// field whose source driver is missing from this image, or whose sensor did
// not answer on the last refresh, is exactly what an operator asks this
// command about, and a silent omission would read as "not configured".
static void telegram_sensor_lines(char *buffer, size_t size, void *ctx) {
    (void)ctx;

    size_t used = 0;

    // Weather: the per-field enable and channel mapping live in g_config,
    // snapshotted here so no configuration lock is held while the weather
    // container's own lock is taken below.
    bool wx_enabled[WX_SENSOR_NUM];
    uint8_t wx_channel[WX_SENSOR_NUM];
    app_config_lock();
    memcpy(wx_enabled, g_config.wx_sensor_enable, sizeof(wx_enabled));
    memcpy(wx_channel, g_config.wx_sensor_ch, sizeof(wx_channel));
    app_config_unlock();

    bool header = false;
    for (int f = 0; f < WX_SENSOR_NUM; f++) {
        if (!wx_enabled[f])
            continue;

        if (!header) {
            str_append(buffer, size, &used, "Weather sensors:\n");
            header = true;
        }

        const char *label = weather_field_label((wx_field_id_t)f);
        uint8_t ch = wx_channel[f];
        const char *source = (ch == SENSOR_LOCAL_CH_NONE) ? "" : sensors_local_channel_name(ch);

        double value = 0.0;
        char rendered[TELEGRAM_APP_VALUE_MAX];
        if (ch != SENSOR_LOCAL_CH_NONE && weather_field_snapshot((wx_field_id_t)f, &value))
            weather_field_format((wx_field_id_t)f, value, rendered, sizeof(rendered));
        else
            snprintf(rendered, sizeof(rendered), "no reading");

        if (source[0] != '\0')
            str_append(buffer, size, &used, "- %s: %s (%s)\n", label, rendered, source);
        else
            str_append(buffer, size, &used, "- %s: %s (no sensor mapped)\n", label, rendered);
    }

    // Telemetry: settings in their own file, readings from the snapshot the
    // 1 Hz refresh publishes.
    telemetry_config_t *tcfg = malloc(sizeof(*tcfg));
    if (tcfg == NULL) {
        // The weather part of the answer, if any, still stands; the telemetry
        // part is named as missing rather than silently absent.
        str_append(buffer, size, &used, "Telemetry channels: unavailable, not enough memory to read the settings\n");
        return;
    }
    telemetry_config_load(tcfg);

    telemetry_values_t values;
    telemetry_get_values(&values);

    header = false;
    for (int a = 0; a < TLM_CH; a++) {
        if (!tcfg->ana_enable[a])
            continue;

        if (!header) {
            str_append(buffer, size, &used, "Telemetry analog channels:\n");
            header = true;
        }

        uint8_t ch = tcfg->tlm_ana_channel[a];
        const char *source = (ch == SENSOR_LOCAL_CH_NONE) ? "" : sensors_local_channel_name(ch);
        const char *name = tcfg->PARM[a];
        const char *unit = tcfg->UNIT[a];

        char rendered[TELEGRAM_APP_VALUE_MAX];
        if (values.analog_present[a])
            snprintf(rendered, sizeof(rendered), "%.*f %s", (int)tcfg->ana_dec[a], values.analog_raw[a], unit);
        else
            snprintf(rendered, sizeof(rendered), "no reading");

        str_append(buffer, size, &used, "- A%d %s: %s (%s)\n", a + 1, name, rendered, source[0] ? source : "no sensor mapped");
    }

    header = false;
    for (int bit = 0; bit < TLM_BIT_NUM; bit++) {
        if (!tcfg->bit_enable[bit])
            continue;

        if (!header) {
            str_append(buffer, size, &used, "Telemetry binary channels:\n");
            header = true;
        }

        uint8_t ch = tcfg->tlm_bit_channel[bit];
        const char *source = (ch == SENSOR_LOCAL_CH_NONE) ? "" : sensors_local_channel_name(ch);
        const char *name = tcfg->tlm_bit_name[bit];

        // Reported as it is transmitted: bit_sense selects whether a raw 1
        // means asserted, so an inverted bit reads the same way here as it
        // does on air.
        char rendered[TELEGRAM_APP_VALUE_MAX];
        if (values.digital_present[bit]) {
            bool state = tcfg->bit_sense[bit] ? values.digital_value[bit] : !values.digital_value[bit];
            snprintf(rendered, sizeof(rendered), "%d", state ? 1 : 0);
        } else {
            snprintf(rendered, sizeof(rendered), "no reading");
        }

        str_append(buffer, size, &used, "- B%d %s: %s (%s)\n", bit + 1, name, rendered, source[0] ? source : "no sensor mapped");
    }

    free(tcfg);

    if (used == 0)
        str_append(buffer, size, &used, "No weather or telemetry channel is enabled.\n");
}

// Asks Telegram who this bot is. This is the only step that proves the token
// end to end - the transport builds a URL out of any string, and the service
// starts a polling task whether or not the servers ever answer - so it is what
// turns "configured" into "connected" on the page, and it is where a rejected
// token is caught with the servers' own wording instead of a timeout.
// Appends the two heap figures that decide whether a TLS session can be set up
// at all. Free heap on its own is misleading here: mbedtls_ssl_setup() needs
// its record buffers in one contiguous piece each, so a fragmented heap fails
// with tens of kilobytes still free. Reporting both is what lets an operator
// tell "this board is out of memory" from "this board has memory but not in
// one piece".
static void append_heap_figures(char *detail, size_t detail_size) {
    size_t used = strlen(detail);
    if (used >= detail_size)
        return;
    snprintf(detail + used, detail_size - used, "%s(free heap %u, largest block %u)", used ? " " : "", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

// Logs what the radio and the IP stack believe about the uplink.
//
// Reported at INFO on purpose: this firmware is built with the log ceiling at
// INFO, so anything logged at DEBUG does not exist in the binary. These four
// facts are the ones that separate "the link is fine" from "the link is up in
// name only", and they cost one line per bring-up attempt.
static void log_link_state(void) {
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
        ESP_LOGI(TAG, "Uplink: SSID \"%s\", channel %u, RSSI %d dBm", (const char *)ap.ssid, (unsigned)ap.primary, ap.rssi);
    else
        ESP_LOGW(TAG, "Uplink: not associated to an access point");

    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (sta != NULL && esp_netif_get_ip_info(sta, &ip) == ESP_OK)
        ESP_LOGI(TAG, "Address " IPSTR ", gateway " IPSTR ", netmask " IPSTR, IP2STR(&ip.ip), IP2STR(&ip.gw), IP2STR(&ip.netmask));

    esp_netif_dns_info_t dns;
    if (sta != NULL && esp_netif_get_dns_info(sta, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK)
        ESP_LOGI(TAG, "Name server " IPSTR, IP2STR(&dns.ip.u_addr.ip4));
    else
        ESP_LOGW(TAG, "No name server configured - every host lookup will run to its timeout");
}

// Opens a plain TCP connection to api.telegram.org:443 and closes it again,
// timing the name lookup and the connect separately.
//
// This exists because the three ways the bot can fail to reach Telegram are
// indistinguishable from the transport's error: a name that will not resolve,
// a route that will not carry a packet, and a TLS handshake the far end
// refuses all surface as one ESP_ERR_HTTP_CONNECT after the full timeout. The
// probe costs one socket and a few hundred milliseconds on a healthy station,
// and on an unhealthy one it says which of the three it is, with the number of
// milliseconds each stage took.
//
// A probe that succeeds and a handshake that then fails is itself the answer:
// it means name resolution and routing are sound and the fault is in TLS.
static telegram_app_reason_t net_probe(char *detail, size_t detail_size) {
    log_link_state();

    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;

    int64_t t0 = esp_timer_get_time();
    int rc = getaddrinfo(TELEGRAM_PROBE_HOST, TELEGRAM_PROBE_PORT, &hints, &res);
    int dns_ms = (int)((esp_timer_get_time() - t0) / 1000);

    if (rc != 0 || res == NULL) {
        if (res != NULL)
            freeaddrinfo(res);
        snprintf(detail, detail_size, "DNS failed after %d ms ", dns_ms);
        ESP_LOGE(TAG, "Probe: name lookup of %s failed after %d ms (getaddrinfo %d)", TELEGRAM_PROBE_HOST, dns_ms, rc);
        return TELEGRAM_APP_REASON_DNS_FAILED;
    }

    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
    char ip[16];
    esp_ip4_addr_t a = { .addr = addr->sin_addr.s_addr };
    esp_ip4addr_ntoa(&a, ip, sizeof(ip));
    ESP_LOGI(TAG, "Probe: %s resolved to %s in %d ms", TELEGRAM_PROBE_HOST, ip, dns_ms);

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        snprintf(detail, detail_size, "no socket ");
        ESP_LOGE(TAG, "Probe: no socket available (errno %d)", errno);
        return TELEGRAM_APP_REASON_TCP_FAILED;
    }

    // Non-blocking connect plus select, because a blocking connect() ignores
    // the socket timeouts and would sit for the stack's own retry schedule.
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);

    t0 = esp_timer_get_time();
    rc = connect(sock, res->ai_addr, res->ai_addrlen);
    bool connected = (rc == 0);

    if (!connected && errno == EINPROGRESS) {
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(sock, &wset);
        struct timeval tv = {
            .tv_sec = TELEGRAM_PROBE_CONNECT_MS / 1000,
            .tv_usec = (TELEGRAM_PROBE_CONNECT_MS % 1000) * 1000,
        };
        if (select(sock + 1, NULL, &wset, NULL, &tv) > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            connected = (getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0);
            if (!connected)
                errno = err;
        }
    }

    int connect_ms = (int)((esp_timer_get_time() - t0) / 1000);
    int saved_errno = errno;

    close(sock);
    freeaddrinfo(res);

    if (!connected) {
        snprintf(detail, detail_size, "TCP to %s:443 failed after %d ms (errno %d) ", ip, connect_ms, saved_errno);
        ESP_LOGE(TAG, "Probe: TCP connect to %s:443 failed after %d ms (errno %d)", ip, connect_ms, saved_errno);
        return TELEGRAM_APP_REASON_TCP_FAILED;
    }

    ESP_LOGI(TAG, "Probe: TCP connect to %s:443 succeeded in %d ms - name resolution and routing are sound", ip, connect_ms);
    return TELEGRAM_APP_REASON_CONNECTED;
}

static telegram_app_reason_t verify_token(char *detail, size_t detail_size) {
    char *buf = malloc(TELEGRAM_GETME_BUF);
    if (buf == NULL)
        return TELEGRAM_APP_REASON_NO_MEMORY;
    buf[0] = 0;

    // Probed over a client of its own, with keep-alive disabled, which is then
    // destroyed. The transport's default handle would outlive this question and
    // hold its TLS session for the whole life of the bot, and this station
    // cannot afford a session that exists only to have answered one question at
    // start-up: a session costs its record buffers plus the contiguous block a
    // later handshake will need, and the service keeps exactly one alive at any
    // instant. Releasing this one before the service starts is what leaves that
    // budget intact.
    telegram_bot_client_config_t probe_cfg = {
        .timeout_ms = 15000,
        .rx_buffer_size = 1024,
        .disable_keep_alive = true,
    };

    telegram_bot_client_handle_t probe = NULL;
    esp_err_t err = telegram_bot_client_create(&probe_cfg, &probe);
    if (err != ESP_OK) {
        snprintf(detail, detail_size, "%s ", esp_err_to_name(err));
        append_heap_figures(detail, detail_size);
        free(buf);
        return (err == ESP_ERR_NO_MEM) ? TELEGRAM_APP_REASON_NO_MEMORY : TELEGRAM_APP_REASON_CONNECT_FAILED;
    }

    telegram_bot_request_t request = {
        .method = "getMe",
    };
    telegram_bot_response_t response = {
        .buffer = buf,
        .buffer_size = TELEGRAM_GETME_BUF,
    };

    err = telegram_bot_client_call(probe, &request, &response);

    // Released before anything is decided, so the session is gone whichever
    // way the probe went.
    telegram_bot_client_destroy(probe);

    if (err != ESP_OK) {
        // A refused TLS handshake and an exhausted heap both surface here as
        // one transport error, because the allocation that failed happened
        // several layers below this call. The figures are what separate them.
        snprintf(detail, detail_size, "%s ", esp_err_to_name(err));
        append_heap_figures(detail, detail_size);
        free(buf);
        return (err == ESP_ERR_NO_MEM) ? TELEGRAM_APP_REASON_NO_MEMORY : TELEGRAM_APP_REASON_CONNECT_FAILED;
    }

    cJSON *doc = cJSON_Parse(buf);
    free(buf);
    if (doc == NULL) {
        snprintf(detail, detail_size, "unreadable answer from api.telegram.org");
        return TELEGRAM_APP_REASON_CONNECT_FAILED;
    }

    telegram_app_reason_t reason;
    const cJSON *ok = cJSON_GetObjectItem(doc, "ok");
    if (cJSON_IsTrue(ok)) {
        const cJSON *result = cJSON_GetObjectItem(doc, "result");
        char name[TELEGRAM_APP_BOTNAME_MAX + 1] = "";
        if (cJSON_IsObject(result))
            get_string(result, "username", name, sizeof(name));
        status_set_bot_name(name);
        detail[0] = 0;
        reason = TELEGRAM_APP_REASON_CONNECTED;
    } else {
        // Telegram's own description is passed through untranslated. It is the
        // single most useful string in this whole path - "Unauthorized" for a
        // revoked or mistyped token, "Not Found" for one whose bot no longer
        // exists - and rewording it would only put this firmware between the
        // operator and the answer.
        const cJSON *code = cJSON_GetObjectItem(doc, "error_code");
        const cJSON *desc = cJSON_GetObjectItem(doc, "description");
        int code_n = cJSON_IsNumber(code) ? (int)code->valuedouble : 0;
        const char *desc_s = (cJSON_IsString(desc) && desc->valuestring != NULL) ? desc->valuestring : "no description";
        snprintf(detail, detail_size, "%d %s", code_n, desc_s);
        reason = TELEGRAM_APP_REASON_API_REJECTED;
    }

    cJSON_Delete(doc);
    return reason;
}

// Performs one full bring-up attempt, publishing the step that failed. On
// success the service is initialized, verified and polling.
//
// Called only from the worker task: it runs a TLS handshake on the caller's
// stack and must never run on the service tick.
static telegram_app_reason_t bring_up(void) {
    char detail[TELEGRAM_APP_DETAIL_MAX + 1] = "";

    // Snapshotted once, under the lock, before anything else runs. A save
    // landing on s_cfg while this attempt is mid-flight (net_probe() and the
    // TLS handshake below can take tens of seconds) must not be able to hand
    // this function a token, user list or chat list that is half-old,
    // half-new. `local` is an automatic variable that lives for the rest of
    // this call, so cfg.bot_token below can point straight into it: it stays
    // valid for as long as telegram_init()/telegram_start() need it, and it
    // can never be rewritten out from under them by telegram_app_apply_config().
    telegram_app_config_t local;
    lock();
    local = s_cfg;
    unlock();

    if (local.bot_token[0] == '\0')
        return TELEGRAM_APP_REASON_NO_TOKEN;
    if (!token_well_formed(local.bot_token))
        return TELEGRAM_APP_REASON_TOKEN_MALFORMED;

#if !CONFIG_TELEGRAM_BOT_CERT_BUNDLE
    telegram_app_reason_t cert = cert_precheck(detail, sizeof(detail));
    if (cert != TELEGRAM_APP_REASON_CONNECTED) {
        status_set(TELEGRAM_APP_STATE_ERROR, cert, detail);
        return cert;
    }
#endif

    // No route means no name resolution and no TLS. The wait itself belongs to
    // the tick, which spawned this task only once a route existed, so finding
    // none here means it went away in between and the attempt is simply
    // deferred rather than waited out on this stack.
    if (!net_state_is_connected()) {
        status_set(TELEGRAM_APP_STATE_STARTING, TELEGRAM_APP_REASON_WAITING_NETWORK, NULL);
        return TELEGRAM_APP_REASON_WAITING_NETWORK;
    }

    // Checked before anything is allocated, so a station that is simply too
    // busy right now retries later instead of taking memory from services that
    // are already running.
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < TELEGRAM_MIN_FREE_BLOCK || heap_caps_get_free_size(MALLOC_CAP_8BIT) < TELEGRAM_MIN_FREE_HEAP) {
        append_heap_figures(detail, sizeof(detail));
        ESP_LOGW(TAG, "Bring-up deferred, not enough contiguous memory for a TLS session: %s", detail);
        status_set(TELEGRAM_APP_STATE_ERROR, TELEGRAM_APP_REASON_NO_MEMORY, detail);
        return TELEGRAM_APP_REASON_NO_MEMORY;
    }

    // Run before anything is allocated, so a station that cannot reach the
    // servers says so cheaply instead of building a service it will have to
    // tear down again.
    telegram_app_reason_t probe = net_probe(detail, sizeof(detail));
    if (probe != TELEGRAM_APP_REASON_CONNECTED) {
        append_heap_figures(detail, sizeof(detail));
        status_set(TELEGRAM_APP_STATE_ERROR, probe, detail);
        return probe;
    }

    status_set(TELEGRAM_APP_STATE_STARTING, TELEGRAM_APP_REASON_CONNECTED, NULL);

    telegram_service_config_t cfg = TELEGRAM_SERVICE_DEFAULT_CONFIG();
    cfg.bot_token = local.bot_token;
    cfg.admin_id = local.admin_id;
    cfg.device_name = APRS_SOFTWARE_NAME;
    cfg.status_cb = telegram_status_lines;
    cfg.sensors_cb = telegram_sensor_lines;
    // The reboot command is left out: this station carries a transmitter and a
    // scheduler with timed obligations, and the web admin already offers a
    // restart behind the admin login.
    cfg.allow_reboot = false;

    // Both of these send the instant the polling connection comes up, so they
    // ask for a second TLS session while the first one's buffers are still
    // held. On a station that also carries the radio modem's DMA buffers that
    // second session does not fit, and the only result is a pair of errors in
    // the log at every start-up. The command list they publish is a
    // convenience - the commands themselves work whether or not Telegram has
    // been told about them - and the start-up notice tells an administrator
    // something the web admin already shows.
    //
    // Both are worth turning back on if this firmware ever runs where two
    // concurrent sessions fit: a board with PSRAM, or one built with
    // CONFIG_MBEDTLS_DYNAMIC_BUFFER.
    cfg.publish_commands = false;
    cfg.announce_start = false;

    esp_err_t err = telegram_init(&cfg);
    if (err != ESP_OK) {
        snprintf(detail, sizeof(detail), "%s ", esp_err_to_name(err));
        append_heap_figures(detail, sizeof(detail));

        // ESP_ERR_NO_MEM out of the service means one of two unrelated
        // things: the heap could not satisfy an allocation, or one of the
        // service's fixed-size tables was too small for what it registers.
        // The heap figures decide which. Blaming the heap when it is plainly
        // healthy sends the operator hunting for memory that is already
        // there, so a failure with room to spare is reported as an
        // initialization fault whose detail carries the numbers that rule
        // memory out.
        bool heap_is_healthy =
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= TELEGRAM_MIN_FREE_BLOCK && heap_caps_get_free_size(MALLOC_CAP_8BIT) >= TELEGRAM_MIN_FREE_HEAP;
        telegram_app_reason_t reason = (err == ESP_ERR_NO_MEM && !heap_is_healthy) ? TELEGRAM_APP_REASON_NO_MEMORY : TELEGRAM_APP_REASON_INIT_FAILED;
        status_set(TELEGRAM_APP_STATE_ERROR, reason, detail);
        return reason;
    }

    if (!s_token_verified) {
        telegram_app_reason_t verified = verify_token(detail, sizeof(detail));
        if (verified != TELEGRAM_APP_REASON_CONNECTED) {
            telegram_deinit();
            status_set(TELEGRAM_APP_STATE_ERROR, verified, detail);
            return verified;
        }
        s_token_verified = true;
    }

    // Seeded after the token is known good, so a rejected token never leaves
    // half a service configured. The administrator is already in the list -
    // telegram_init() adds admin_id itself - and re-adding an identifier the
    // component already holds updates that entry rather than consuming a slot.
    for (uint8_t i = 0; i < local.user_count; i++)
        telegram_add_user(local.users[i].id, local.users[i].name, false);
    for (uint8_t i = 0; i < local.chat_count; i++)
        telegram_allow_chat(local.chats[i].id, local.chats[i].name);

    err = telegram_start();
    if (err != ESP_OK) {
        snprintf(detail, sizeof(detail), "%s ", esp_err_to_name(err));
        append_heap_figures(detail, sizeof(detail));
        telegram_deinit();
        status_set(TELEGRAM_APP_STATE_ERROR, TELEGRAM_APP_REASON_TASK_FAILED, detail);
        return TELEGRAM_APP_REASON_TASK_FAILED;
    }

    status_set(TELEGRAM_APP_STATE_RUNNING, TELEGRAM_APP_REASON_CONNECTED, NULL);
    ESP_LOGI(TAG, "Telegram bot running");
    return TELEGRAM_APP_REASON_CONNECTED;
}

// True for the faults that can clear without the operator touching anything,
// and which are therefore worth attempting again after a pause.
static bool reason_is_transient(telegram_app_reason_t reason) {
    return reason == TELEGRAM_APP_REASON_WAITING_NETWORK || reason == TELEGRAM_APP_REASON_NO_MEMORY || reason == TELEGRAM_APP_REASON_CONNECT_FAILED ||
           reason == TELEGRAM_APP_REASON_INIT_FAILED || reason == TELEGRAM_APP_REASON_TASK_FAILED || reason == TELEGRAM_APP_REASON_DNS_FAILED ||
           reason == TELEGRAM_APP_REASON_TCP_FAILED;
}

// Carries out one pending action and exits.
//
// The task is spawned by the tick, does its one job and deletes itself, which
// is what returns its stack to the heap between jobs. It is also the only
// place telegram_deinit() may run: that call waits for the service's polling
// task to leave a long poll, which must not happen on the service tick or on
// the web server's stack.
static void telegram_worker_task(void *arg) {
    telegram_action_t action = (telegram_action_t)(intptr_t)arg;

    if (action == TELEGRAM_ACTION_STOP || s_service_up) {
        if (s_service_up) {
            telegram_deinit();
            s_service_up = false;
            ESP_LOGI(TAG, "Telegram bot stopped");
        }
        if (action == TELEGRAM_ACTION_STOP) {
            status_reset(TELEGRAM_APP_STATE_DISABLED, TELEGRAM_APP_REASON_DISABLED);
            s_retry_in = TELEGRAM_RETRY_NEVER;
            s_worker_alive = false;
            vTaskDelete(NULL);
            return;
        }
    }

    telegram_app_reason_t reason = bring_up();

    if (reason == TELEGRAM_APP_REASON_CONNECTED) {
        s_service_up = true;
        s_retry_in = TELEGRAM_RETRY_NEVER;
    } else if (reason_is_transient(reason)) {
        s_retry_in = TELEGRAM_RETRY_SECONDS;
    } else {
        // Nothing changes until the operator changes it, and changing it goes
        // through the Telegram page, which arms a fresh attempt. So the fault
        // stays published and no retry is scheduled.
        ESP_LOGE(TAG, "Telegram bring-up stopped, waiting for a configuration change");
        s_retry_in = TELEGRAM_RETRY_NEVER;
    }

    // Reported so the stack above can be cut to what this task actually uses
    // instead of the figure it was guessed at. Sized for a TLS handshake, this
    // is the largest stack the bot allocates, and it is the one worth
    // measuring before trimming.
    ESP_LOGI(TAG, "Worker finished, stack high-water %u bytes free of %d", (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)),
             TELEGRAM_WORKER_STACK_BYTES);

    s_worker_alive = false;
    vTaskDelete(NULL);
}

// Spawns the worker for one action, if one is not already running.
static void worker_spawn(telegram_action_t action) {
    if (s_worker_alive)
        return;

    s_worker_alive = true;
    if (xTaskCreate(telegram_worker_task, "telegram_wk", TELEGRAM_WORKER_STACK_BYTES, (void *)(intptr_t)action, TELEGRAM_WORKER_PRIORITY, NULL) != pdPASS) {
        s_worker_alive = false;
        ESP_LOGE(TAG, "Worker task could not be created");
        status_reset(TELEGRAM_APP_STATE_ERROR, TELEGRAM_APP_REASON_TASK_FAILED);
        s_retry_in = TELEGRAM_RETRY_SECONDS;
    }
}

void telegram_app_tick_1hz(void) {
    // A pending enable or disable takes priority over anything else: it is the
    // operator's most recent instruction.
    telegram_action_t pending = s_action;
    if (pending != TELEGRAM_ACTION_NONE) {
        if (s_worker_alive)
            return;
        s_action = TELEGRAM_ACTION_NONE;
        worker_spawn(pending);
        return;
    }

    if (!s_enabled || s_worker_alive)
        return;

    if (s_service_up) {
        telegram_stats_t st;
        if (telegram_get_stats(&st) == ESP_OK)
            status_set_counters(&st);

        // A service whose polling task has gone needs a teardown before
        // anything can be rebuilt, and the worker is the only place that may
        // run one.
        if (!telegram_is_running()) {
            ESP_LOGW(TAG, "Service stopped unexpectedly, restarting");
            status_set(TELEGRAM_APP_STATE_ERROR, TELEGRAM_APP_REASON_CONNECT_FAILED, "polling task ended");
            worker_spawn(TELEGRAM_ACTION_START);
            return;
        }

        // Said out loud rather than left to show as a polling-error count that
        // climbs for no stated reason.
        if (!net_state_is_connected())
            status_set(TELEGRAM_APP_STATE_STARTING, TELEGRAM_APP_REASON_WAITING_NETWORK, NULL);
        else
            status_set(TELEGRAM_APP_STATE_RUNNING, TELEGRAM_APP_REASON_CONNECTED, NULL);
        return;
    }

    if (s_retry_in == TELEGRAM_RETRY_NEVER)
        return;

    if (s_retry_in > 0) {
        s_retry_in--;
        return;
    }

    // The route is checked here, on a task that costs nothing to keep waiting,
    // so a station that boots before its access point does not hold a
    // handshake-sized stack while it waits.
    if (!net_state_is_connected()) {
        status_set(TELEGRAM_APP_STATE_STARTING, TELEGRAM_APP_REASON_WAITING_NETWORK, NULL);
        return;
    }

    worker_spawn(TELEGRAM_ACTION_START);
}

void telegram_app_apply_config(void) {
    telegram_app_config_t cfg;
    bool read_ok = telegram_app_load(&cfg);
    json_store_status_t read_status = s_last_read_status;

    lock();
    s_cfg = cfg;
    s_enabled = cfg.enable;
    unlock();

    if (!cfg.enable) {
        // A file that could not be read is reported even with the switch off,
        // because "off" and "unreadable" call for different actions and the
        // page has no other way to tell them apart.
        if (!read_ok && read_status != JSON_STORE_MISSING) {
            telegram_app_reason_t reason = (read_status == JSON_STORE_OOM) ? TELEGRAM_APP_REASON_FILE_UNREADABLE : TELEGRAM_APP_REASON_FILE_CORRUPT;
            status_reset(TELEGRAM_APP_STATE_ERROR, reason);
            status_set(TELEGRAM_APP_STATE_ERROR, reason, TELEGRAM_APP_PATH);
        }
        // Queued rather than performed here. Releasing the service waits for
        // its polling task to leave a long poll, which can take tens of
        // seconds, and this runs on the web server's stack while a browser
        // waits for the save to answer.
        s_action = TELEGRAM_ACTION_STOP;
        return;
    }

    // Settings may have moved under a running bot, and the token is only read
    // at initialization, so the queued action restarts it: the worker tears
    // down whatever is up before bringing the new configuration on.
    status_reset(TELEGRAM_APP_STATE_STARTING, TELEGRAM_APP_REASON_WAITING_NETWORK);
    s_token_verified = false;
    s_retry_in = 0;
    s_action = TELEGRAM_ACTION_START;
}
