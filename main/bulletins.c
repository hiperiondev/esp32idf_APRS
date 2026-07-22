/**
 * @file bulletins.c
 *
 * @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
 * @date 2026
 * @copyright GNU General Public License v3
 * @see https://github.com/hiperiondev/esp32idf_APRS
 *
 * @note
 * This is based on other projects:
 *     VP-Digi: https://github.com/sq8vps/vp-digi
 *     ESP32APRS: https://github.com/nakhonthai/ESP32APRS_Audio
 *     LibAPRS: https://github.com/markqvist/LibAPRS
 *
 *     please contact their authors for more information.
 *
 * @brief APRS bulletin store (LittleFS-backed) and periodic transmitter.
 *
 * See bulletins.h for the design rationale (why bulletins live in their own
 * /storage/bulletins.json file instead of g_config, and how expiry works).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_config.h"
#include "aprs_service.h"
#include "bulletins.h"
#include "igate.h"

static const char *TAG = "bulletins";

// The APRS bulletin group id is a single character (BLN1..BLNn), and
// build_info_field() emits it as one digit ('0' + n). Keep the count within a
// single decimal digit so that assumption holds.
_Static_assert(BULLETIN_COUNT <= 9, "BULLETIN_COUNT must stay a single digit for the BLNn addressee");

#define BULLETINS_PATH     "/storage/bulletins.json"
#define BULLETINS_TMP_PATH "/storage/bulletins.json.tmp"

// Same software-identifier destination call used by the beacon and message
// components, for consistency across the firmware.
#define BULLETIN_DEST "APE32L"

// Per-bulletin transmit interval. Each bulletin now carries its own
// "Beacon interval (s)" (bulletin_t.interval_s). These bound it: a 0 (or
// unset) interval falls back to the default, and anything below the floor is
// raised to it - mirroring beacon.c's clampInterval() so bulletins can't be
// configured to hammer RF/APRS-IS.
#define BULLETIN_MIN_INTERVAL_S     30   // sanity floor
#define BULLETIN_DEFAULT_INTERVAL_S 1800 // 30 min, used when interval_s == 0

// Upper bound on how long the task sleeps between wake-ups. Even when every
// bulletin's interval is long, the task re-loads config at least this often so
// web edits (interval/enable/text) and expiry are picked up promptly.
#define BULLETIN_POLL_CAP_S 60

// One-time settle delay after boot before the first transmit pass, so WiFi/
// APRS-IS association and the modem have a chance to come up first.
#define BULLETIN_START_DELAY_S 60

// Small gap between consecutive bulletin transmissions, so a burst of enabled
// bulletins that come due together don't hit the modem/APRS-IS all at once.
#define BULLETIN_INTER_TX_MS 1500

// Stack sized like the beacon tasks: bulletin TX walks the same float-free but
// snprintf-heavy TNC2 build + modem_send_tnc2/ax25_encode chain, and the
// beacon tasks were bumped to 10240 for exactly that call tree (see beacon.c).
#define BULLETIN_TASK_STACK_BYTES 10240

// Wall-clock sanity floor (2020-09-13). time() below this means NTP hasn't
// synced yet, so absolute expiry deadlines can't be trusted/armed.
#define BULLETIN_TIME_VALID_THRESHOLD 1600000000LL

// Serializes LittleFS load/save between the web save handler and the TX task.
static SemaphoreHandle_t s_lock;

static void ensure_lock(void) {
    // main.c is single-threaded at init time (bulletins_start / first page
    // load happen well after the scheduler is up but never concurrently at the
    // very first call), so a lazy create with no double-init guard is fine.
    if (!s_lock)
        s_lock = xSemaphoreCreateMutex();
}

static void lock(void) {
    ensure_lock();
    if (s_lock)
        xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void) {
    if (s_lock)
        xSemaphoreGive(s_lock);
}

static bool clock_valid(void) {
    return (int64_t)time(NULL) >= BULLETIN_TIME_VALID_THRESHOLD;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

// Emit a JSON string literal with the same minimal escaping app_config.c uses,
// so text round-trips through cJSON_Parse on reload.
static void write_json_string(FILE *f, const char *v) {
    fputc('"', f);
    if (v) {
        for (const unsigned char *p = (const unsigned char *)v; *p; p++) {
            unsigned char ch = *p;
            switch (ch) {
                case '"':  fputs("\\\"", f); break;
                case '\\': fputs("\\\\", f); break;
                case '\b': fputs("\\b", f); break;
                case '\f': fputs("\\f", f); break;
                case '\n': fputs("\\n", f); break;
                case '\r': fputs("\\r", f); break;
                case '\t': fputs("\\t", f); break;
                default:
                    if (ch < 0x20)
                        fprintf(f, "\\u%04x", ch);
                    else
                        fputc(ch, f);
            }
        }
    }
    fputc('"', f);
}

static bool load_locked(bulletins_t *out, bool *out_missing) {
    memset(out, 0, sizeof(*out));
    if (out_missing)
        *out_missing = false;

    FILE *f = fopen(BULLETINS_PATH, "r");
    if (!f) {
        ESP_LOGI(TAG, "%s not present - starting with empty bulletins", BULLETINS_PATH);
        if (out_missing)
            *out_missing = true;
        return false;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        return false;
    }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        ESP_LOGW(TAG, "OOM reading bulletins");
        return false;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = 0;
    fclose(f);

    cJSON *doc = cJSON_Parse(buf);
    free(buf);
    if (!doc) {
        ESP_LOGW(TAG, "%s corrupt - ignoring", BULLETINS_PATH);
        return false;
    }

    cJSON *arr = cJSON_GetObjectItem(doc, "bulletins");
    if (cJSON_IsArray(arr)) {
        int n = cJSON_GetArraySize(arr);
        if (n > BULLETIN_COUNT)
            n = BULLETIN_COUNT;
        for (int i = 0; i < n; i++) {
            cJSON *o = cJSON_GetArrayItem(arr, i);
            if (!cJSON_IsObject(o))
                continue;
            bulletin_t *b = &out->item[i];

            cJSON *v;
            v = cJSON_GetObjectItem(o, "en");
            b->enable = cJSON_IsTrue(v);
            v = cJSON_GetObjectItem(o, "rf");
            b->send_rf = cJSON_IsTrue(v);
            v = cJSON_GetObjectItem(o, "inet");
            b->send_inet = cJSON_IsTrue(v);
            v = cJSON_GetObjectItem(o, "text");
            if (cJSON_IsString(v) && v->valuestring) {
                strncpy(b->text, v->valuestring, BULLETIN_TEXT_MAX);
                b->text[BULLETIN_TEXT_MAX] = 0;
            }
            v = cJSON_GetObjectItem(o, "int_s");
            if (cJSON_IsNumber(v) && v->valuedouble > 0)
                b->interval_s = (uint32_t)v->valuedouble;
            v = cJSON_GetObjectItem(o, "exp_h");
            if (cJSON_IsNumber(v) && v->valuedouble > 0)
                b->expire_hours = (uint32_t)v->valuedouble;
            v = cJSON_GetObjectItem(o, "exp_at");
            if (cJSON_IsNumber(v) && v->valuedouble > 0)
                b->expire_at = (int64_t)v->valuedouble;
        }
    }

    cJSON_Delete(doc);
    return true;
}

static bool save_locked(const bulletins_t *in) {
    FILE *f = fopen(BULLETINS_TMP_PATH, "w");
    if (!f) {
        ESP_LOGE(TAG, "open tmp for write failed");
        return false;
    }

    // Written token-by-token straight to the file (no cJSON tree, no second
    // serialized buffer) - the same low-RAM approach app_config_save() uses.
    fputs("{\"bulletins\":[", f);
    for (int i = 0; i < BULLETIN_COUNT; i++) {
        const bulletin_t *b = &in->item[i];
        char text[BULLETIN_TEXT_MAX + 1];
        strncpy(text, b->text, BULLETIN_TEXT_MAX);
        text[BULLETIN_TEXT_MAX] = 0;

        fputs(i ? ",{" : "{", f);
        fprintf(f, "\"en\":%s,", b->enable ? "true" : "false");
        fprintf(f, "\"rf\":%s,", b->send_rf ? "true" : "false");
        fprintf(f, "\"inet\":%s,", b->send_inet ? "true" : "false");
        fputs("\"text\":", f);
        write_json_string(f, text);
        fprintf(f, ",\"int_s\":%u", (unsigned)b->interval_s);
        fprintf(f, ",\"exp_h\":%u,", (unsigned)b->expire_hours);
        fprintf(f, "\"exp_at\":%lld", (long long)b->expire_at);
        fputc('}', f);
    }
    fputs("]}", f);

    bool ok = (fflush(f) == 0) && (ferror(f) == 0);
    if (fclose(f) != 0)
        ok = false;
    if (!ok) {
        ESP_LOGE(TAG, "write error while saving bulletins");
        remove(BULLETINS_TMP_PATH);
        return false;
    }

    remove(BULLETINS_PATH);
    if (rename(BULLETINS_TMP_PATH, BULLETINS_PATH) != 0) {
        ESP_LOGE(TAG, "rename tmp->bulletins failed");
        return false;
    }
    ESP_LOGI(TAG, "Bulletins saved");
    return true;
}

bool bulletins_load(bulletins_t *out) {
    if (!out)
        return false;
    lock();
    bool missing = false;
    bool ok = load_locked(out, &missing);
    unlock();
    if (missing) {
        // First boot / file lost: persist the empty-default set now so
        // /storage/bulletins.json exists on disk instead of only living
        // in RAM until something else happens to trigger a save.
        if (!bulletins_save(out))
            ESP_LOGW(TAG, "Failed to write default %s", BULLETINS_PATH);
    }
    return ok;
}

bool bulletins_save(const bulletins_t *in) {
    if (!in)
        return false;
    lock();
    bool ok = save_locked(in);
    unlock();
    return ok;
}

void bulletins_arm_expiry(bulletins_t *b) {
    if (!b)
        return;
    int64_t now = (int64_t)time(NULL);
    bool valid = now >= BULLETIN_TIME_VALID_THRESHOLD;
    for (int i = 0; i < BULLETIN_COUNT; i++) {
        bulletin_t *it = &b->item[i];
        if (it->enable && it->expire_hours > 0 && valid) {
            it->expire_at = now + (int64_t)it->expire_hours * 3600;
        } else {
            it->expire_at = 0; // never / can't arm (disabled, no window, or no clock)
            if (it->enable && it->expire_hours > 0 && !valid)
                ESP_LOGW(TAG, "bulletin %d: clock not synced, expiry not armed", i + 1);
        }
    }
}

// ---------------------------------------------------------------------------
// Transmission
// ---------------------------------------------------------------------------

// Resolves the station callsign used as the bulletin source. Prefers the
// shared "My Station" callsign; falls back to the IGate APRS callsign+SSID.
static void resolve_source_call(char *out, size_t out_size) {
    out[0] = 0;
    if (g_config.my_callsign[0]) {
        strncpy(out, g_config.my_callsign, out_size - 1);
        out[out_size - 1] = 0;
    } else if (g_config.aprs_mycall[0]) {
        if (g_config.aprs_ssid > 0)
            snprintf(out, out_size, "%s-%d", g_config.aprs_mycall, (int)g_config.aprs_ssid);
        else
            snprintf(out, out_size, "%s", g_config.aprs_mycall);
    }
    // Uppercase (callsigns are case-insensitive on-air; keep it canonical).
    for (char *p = out; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p -= 32;
}

// Builds the ":BLNx     :text" APRS message info field for bulletin index i.
static void build_info_field(int idx, const char *text, char *out, size_t out_size) {
    // APRS message addressee is exactly 9 chars, space-padded. Bulletin groups
    // are BLN1..BLN5 here; no message number/ack is appended (bulletins never
    // carry one). Built char-by-char rather than snprintf("BLN%d") so the
    // group digit stays a single character (BULLETIN_COUNT <= 9) and the
    // compiler's -Werror=format-truncation can't flag a worst-case %d.
    char addr[10];
    addr[0] = 'B';
    addr[1] = 'L';
    addr[2] = 'N';
    addr[3] = (char)('0' + (idx + 1)); // idx 0..4 -> '1'..'5'
    addr[4] = ' ';
    addr[5] = ' ';
    addr[6] = ' ';
    addr[7] = ' ';
    addr[8] = ' ';
    addr[9] = 0;

    snprintf(out, out_size, ":%s:%s", addr, text);
}

static void tx_one(int idx, const bulletin_t *b, const char *src) {
    char info[128];
    build_info_field(idx, b->text, info, sizeof(info));

    if (b->send_rf) {
        // Sent direct (no digipeater path). Bulletins here intentionally carry
        // no unproto path - the page exposes only enable/RF/Internet/text/
        // expire, matching the requested field set.
        char packet[160];
        int len = snprintf(packet, sizeof(packet), "%s>%s:%s", src, BULLETIN_DEST, info);
        if (len > 0) {
            if (aprs_service_send_tnc2(packet, (size_t)len))
                ESP_LOGI(TAG, "Bulletin %d TX (RF): %s", idx + 1, packet);
            else
                ESP_LOGW(TAG, "Bulletin %d NOT sent over RF - modem not ready or busy", idx + 1);
        }
    }
    if (b->send_inet) {
        // Locally-originated APRS-IS traffic carries the TCPIP* q-construct,
        // never an RF unproto path (see the same note in message.c).
        char packet[160];
        int len = snprintf(packet, sizeof(packet), "%s>%s,TCPIP*:%s", src, BULLETIN_DEST, info);
        if (len > 0) {
            if (igate_send_raw(packet, (size_t)len))
                ESP_LOGI(TAG, "Bulletin %d TX (INET): %s", idx + 1, packet);
            else
                ESP_LOGW(TAG, "Bulletin %d NOT sent over INET - APRS-IS not connected yet", idx + 1);
        }
    }
}

// Applies expiry to a freshly-loaded set: any enabled bulletin whose deadline
// has passed is disabled. Returns true if anything changed (caller persists).
bool bulletins_apply_expiry(bulletins_t *b) {
    if (!b || !clock_valid())
        return false; // don't expire against an unsynced clock
    int64_t now = (int64_t)time(NULL);
    bool changed = false;
    for (int i = 0; i < BULLETIN_COUNT; i++) {
        bulletin_t *it = &b->item[i];
        if (it->enable && it->expire_at > 0 && now >= it->expire_at) {
            it->enable = false;
            it->expire_at = 0;
            changed = true;
            ESP_LOGI(TAG, "Bulletin %d expired - disabled", i + 1);
        }
    }
    return changed;
}

// Bound a bulletin's configured interval into [floor, ...], with 0 meaning
// "use the default" (same policy as beacon.c's clampInterval()).
static uint32_t clamp_interval(uint32_t interval) {
    if (interval == 0)
        return BULLETIN_DEFAULT_INTERVAL_S;
    if (interval < BULLETIN_MIN_INTERVAL_S)
        return BULLETIN_MIN_INTERVAL_S;
    return interval;
}

// Monotonic seconds since boot - used for scheduling so an NTP step of the
// wall clock (which expiry uses) never disturbs the transmit cadence.
static int64_t mono_seconds(void) {
    return (int64_t)(esp_timer_get_time() / 1000000);
}

// Per-bulletin next-due timestamps (monotonic seconds). 0 = due now, so every
// enabled bulletin transmits once on the first pass after start. File-scope
// now that the transmitter is a serviced pass (bulletins_service) driven by
// the shared beacon scheduler instead of its own task loop.
static int64_t s_bln_next_due[BULLETIN_COUNT] = { 0 };

// One serviced pass of the bulletin transmitter. Called by the shared beacon
// scheduler (beacon_scheduler.c); returns the number of seconds until the
// transmitter next wants servicing (>= 1). Body is the old bulletin_task loop
// body, with the per-bulletin timers hoisted to file scope and the one-time
// boot settle delay returned on the first call instead of a blocking sleep.
uint32_t bulletins_service(void) {
    // One-time settle delay after boot before the first transmit pass, so
    // WiFi/APRS-IS association and the modem have a chance to come up first.
    // The old task slept this once before entering its loop.
    static bool started = false;
    if (!started) {
        started = true;
        return BULLETIN_START_DELAY_S;
    }

    bulletins_t set;
    bulletins_load(&set);

    // Enforce expiry first, and persist the disable so the web UI reflects
    // it even if nothing is transmitted this pass.
    if (bulletins_apply_expiry(&set))
        bulletins_save(&set);

    char src[16];
    resolve_source_call(src, sizeof(src));

    int64_t now = mono_seconds();
    int64_t soonest = now + BULLETIN_POLL_CAP_S;

    for (int i = 0; i < BULLETIN_COUNT; i++) {
        const bulletin_t *b = &set.item[i];
        bool sendable = b->enable && b->text[0] && (b->send_rf || b->send_inet);

        if (!sendable || !src[0]) {
            // Reset so that (re-)enabling or setting a callsign fires an
            // immediate transmit on the next pass instead of waiting out a
            // stale timer.
            s_bln_next_due[i] = 0;
            continue;
        }

        if (now >= s_bln_next_due[i]) {
            tx_one(i, b, src);
            s_bln_next_due[i] = now + (int64_t)clamp_interval(b->interval_s);
            vTaskDelay(pdMS_TO_TICKS(BULLETIN_INTER_TX_MS));
            now = mono_seconds(); // account for the inter-TX gap
        }

        if (s_bln_next_due[i] < soonest)
            soonest = s_bln_next_due[i];
    }

    ESP_LOGD(TAG, "bulletins_service stack free: %u bytes", (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));

    // Sleep until the soonest bulletin is due, capped so config edits and
    // expiry are still picked up promptly.
    int64_t sleep_s = soonest - mono_seconds();
    if (sleep_s < 1)
        sleep_s = 1;
    if (sleep_s > BULLETIN_POLL_CAP_S)
        sleep_s = BULLETIN_POLL_CAP_S;
    return (uint32_t)sleep_s;
}

void bulletins_start(void) {
    // No task creation here any more: the bulletin transmitter is driven by
    // the shared beacon scheduler (beacon_scheduler_start()) via
    // bulletins_service(). Just make sure the LittleFS lock exists.
    ensure_lock();
    ESP_LOGI(TAG, "Bulletins configured (per-bulletin interval, default=%us; driven by beacon scheduler)", (unsigned)BULLETIN_DEFAULT_INTERVAL_S);
}
