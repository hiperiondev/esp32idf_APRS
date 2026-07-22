/**
 * @file objects_items.c
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
 * @brief APRS Object/Item store (LittleFS-backed) and periodic transmitter.
 *
 * See objects_items.h for the design rationale (why these live in their own
 * /storage/objitems.json file instead of g_config, the on-air wire format, and
 * how kill reports work). The persistence and scheduling structure deliberately
 * mirrors bulletins.c so the two subsystems stay easy to reason about together.
 */

#include <math.h>
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
#include "igate.h"
#include "objects_items.h"

static const char *TAG = "objitems";

#define OBJITEMS_PATH     "/storage/objitems.json"
#define OBJITEMS_TMP_PATH "/storage/objitems.json.tmp"

// Same software-identifier destination call used by the beacon, message and
// bulletin components, for consistency across the firmware.
#define OBJITEM_DEST "APE32L"

// Per-element transmit interval bounds. Each element carries its own interval;
// 0 (or unset) falls back to the default, and anything below the floor is
// raised to it - mirroring beacon.c/bulletins.c so an Object/Item can't be
// configured to hammer RF/APRS-IS.
#define OBJITEM_MIN_INTERVAL_S     30   // sanity floor
#define OBJITEM_DEFAULT_INTERVAL_S 600  // 10 min, used when interval_s == 0

// Upper bound on how long the transmitter asks to sleep between passes. Even
// when every element's interval is long, config is re-loaded at least this
// often so web edits (position/interval/enable/kill) are picked up promptly.
#define OBJITEM_POLL_CAP_S 60

// One-time settle delay after boot before the first transmit pass, so WiFi/
// APRS-IS association and the modem have a chance to come up first.
#define OBJITEM_START_DELAY_S 60

// Small gap between consecutive element transmissions, so a burst of enabled
// elements that come due together don't hit the modem/APRS-IS all at once.
#define OBJITEM_INTER_TX_MS 1500

// Serializes LittleFS load/save between the web save handler and the TX pass.
static SemaphoreHandle_t s_lock;

static void ensure_lock(void) {
    // Init happens single-threaded (objitems_start / first page load run well
    // after the scheduler is up but never concurrently at the very first
    // call), so a lazy create with no double-init guard is fine.
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

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

// Emit a JSON string literal with the same minimal escaping app_config.c and
// bulletins.c use, so text round-trips through cJSON_Parse on reload.
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

static void clamp_str(char *dst, const char *src, size_t max_chars) {
    if (!src)
        src = "";
    // Copy at most max_chars characters, always NUL-terminating. Written with
    // an explicit measured length + memcpy (not strncpy) so the ESP-IDF build,
    // which treats -Wstringop-truncation as an error, is satisfied for the
    // exactly-max-length case.
    size_t n = 0;
    while (n < max_chars && src[n])
        n++;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static bool load_locked(objitems_t *out, bool *out_missing) {
    memset(out, 0, sizeof(*out));
    if (out_missing)
        *out_missing = false;
    // Sane symbol default for any element the file doesn't fully specify.
    for (int i = 0; i < OBJITEM_COUNT; i++) {
        out->item[i].sym[0] = '/';
        out->item[i].sym[1] = '-';
        out->item[i].active = true;
    }

    FILE *f = fopen(OBJITEMS_PATH, "r");
    if (!f) {
        ESP_LOGI(TAG, "%s not present - starting with empty objects/items", OBJITEMS_PATH);
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
        ESP_LOGW(TAG, "OOM reading objects/items");
        return false;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = 0;
    fclose(f);

    cJSON *doc = cJSON_Parse(buf);
    free(buf);
    if (!doc) {
        ESP_LOGW(TAG, "%s corrupt - ignoring", OBJITEMS_PATH);
        return false;
    }

    cJSON *arr = cJSON_GetObjectItem(doc, "objitems");
    if (cJSON_IsArray(arr)) {
        int n = cJSON_GetArraySize(arr);
        if (n > OBJITEM_COUNT)
            n = OBJITEM_COUNT;
        for (int i = 0; i < n; i++) {
            cJSON *o = cJSON_GetArrayItem(arr, i);
            if (!cJSON_IsObject(o))
                continue;
            objitem_t *b = &out->item[i];

            cJSON *v;
            v = cJSON_GetObjectItem(o, "en");
            b->enable = cJSON_IsTrue(v);
            v = cJSON_GetObjectItem(o, "rf");
            b->send_rf = cJSON_IsTrue(v);
            v = cJSON_GetObjectItem(o, "inet");
            b->send_inet = cJSON_IsTrue(v);
            v = cJSON_GetObjectItem(o, "item");
            b->is_item = cJSON_IsTrue(v);
            v = cJSON_GetObjectItem(o, "act");
            // Default to active(=live) when the key is absent.
            b->active = v ? cJSON_IsTrue(v) : true;

            v = cJSON_GetObjectItem(o, "name");
            if (cJSON_IsString(v) && v->valuestring)
                clamp_str(b->name, v->valuestring, OBJITEM_NAME_MAX);

            v = cJSON_GetObjectItem(o, "lat");
            if (cJSON_IsNumber(v))
                b->lat = (float)v->valuedouble;
            v = cJSON_GetObjectItem(o, "lon");
            if (cJSON_IsNumber(v))
                b->lon = (float)v->valuedouble;

            v = cJSON_GetObjectItem(o, "sym");
            if (cJSON_IsString(v) && v->valuestring && v->valuestring[0]) {
                b->sym[0] = v->valuestring[0];
                b->sym[1] = v->valuestring[1] ? v->valuestring[1] : '-';
            }

            v = cJSON_GetObjectItem(o, "crs");
            if (cJSON_IsNumber(v) && v->valuedouble >= 0)
                b->course = (uint16_t)((int)v->valuedouble % 360);
            v = cJSON_GetObjectItem(o, "spd");
            if (cJSON_IsNumber(v) && v->valuedouble > 0)
                b->speed = (uint16_t)v->valuedouble;

            v = cJSON_GetObjectItem(o, "scope");
            if (cJSON_IsNumber(v)) {
                int s = (int)v->valuedouble;
                if (s < OBJITEM_SCOPE_PRIVATE)
                    s = OBJITEM_SCOPE_PRIVATE;
                if (s > OBJITEM_SCOPE_GLOBAL)
                    s = OBJITEM_SCOPE_GLOBAL;
                b->scope = (objitem_scope_t)s;
            } else {
                b->scope = OBJITEM_SCOPE_GLOBAL;
            }

            v = cJSON_GetObjectItem(o, "cmt");
            if (cJSON_IsString(v) && v->valuestring)
                clamp_str(b->comment, v->valuestring, OBJITEM_COMMENT_MAX);

            v = cJSON_GetObjectItem(o, "int_s");
            if (cJSON_IsNumber(v) && v->valuedouble > 0)
                b->interval_s = (uint32_t)v->valuedouble;

            v = cJSON_GetObjectItem(o, "kill_left");
            if (cJSON_IsNumber(v) && v->valuedouble > 0)
                b->kill_left = (uint8_t)v->valuedouble;
        }
    }

    cJSON_Delete(doc);
    return true;
}

static bool save_locked(const objitems_t *in) {
    FILE *f = fopen(OBJITEMS_TMP_PATH, "w");
    if (!f) {
        ESP_LOGE(TAG, "open tmp for write failed");
        return false;
    }

    // Written token-by-token straight to the file (no cJSON tree, no second
    // serialized buffer) - the same low-RAM approach app_config_save() and
    // bulletins_save() use.
    fputs("{\"objitems\":[", f);
    for (int i = 0; i < OBJITEM_COUNT; i++) {
        const objitem_t *b = &in->item[i];
        char name[OBJITEM_NAME_MAX + 1];
        char cmt[OBJITEM_COMMENT_MAX + 1];
        char sym[3];
        clamp_str(name, b->name, OBJITEM_NAME_MAX);
        clamp_str(cmt, b->comment, OBJITEM_COMMENT_MAX);
        sym[0] = b->sym[0] ? b->sym[0] : '/';
        sym[1] = b->sym[1] ? b->sym[1] : '-';
        sym[2] = 0;

        fputs(i ? ",{" : "{", f);
        fprintf(f, "\"en\":%s,", b->enable ? "true" : "false");
        fprintf(f, "\"rf\":%s,", b->send_rf ? "true" : "false");
        fprintf(f, "\"inet\":%s,", b->send_inet ? "true" : "false");
        fprintf(f, "\"item\":%s,", b->is_item ? "true" : "false");
        fprintf(f, "\"act\":%s,", b->active ? "true" : "false");
        fputs("\"name\":", f);
        write_json_string(f, name);
        fprintf(f, ",\"lat\":%.6f", (double)b->lat);
        fprintf(f, ",\"lon\":%.6f", (double)b->lon);
        fputs(",\"sym\":", f);
        write_json_string(f, sym);
        fprintf(f, ",\"crs\":%u", (unsigned)b->course);
        fprintf(f, ",\"spd\":%u", (unsigned)b->speed);
        fprintf(f, ",\"scope\":%d", (int)b->scope);
        fputs(",\"cmt\":", f);
        write_json_string(f, cmt);
        fprintf(f, ",\"int_s\":%u", (unsigned)b->interval_s);
        fprintf(f, ",\"kill_left\":%u", (unsigned)b->kill_left);
        fputc('}', f);
    }
    fputs("]}", f);

    bool ok = (fflush(f) == 0) && (ferror(f) == 0);
    if (fclose(f) != 0)
        ok = false;
    if (!ok) {
        ESP_LOGE(TAG, "write error while saving objects/items");
        remove(OBJITEMS_TMP_PATH);
        return false;
    }

    remove(OBJITEMS_PATH);
    if (rename(OBJITEMS_TMP_PATH, OBJITEMS_PATH) != 0) {
        ESP_LOGE(TAG, "rename tmp->objitems failed");
        return false;
    }
    ESP_LOGI(TAG, "Objects/Items saved");
    return true;
}

bool objitems_load(objitems_t *out) {
    if (!out)
        return false;
    lock();
    bool missing = false;
    bool ok = load_locked(out, &missing);
    unlock();
    if (missing) {
        // First boot / file lost: persist the empty-default set now so
        // /storage/objitems.json exists on disk instead of only living
        // in RAM until something else happens to trigger a save.
        if (!objitems_save(out))
            ESP_LOGW(TAG, "Failed to write default %s", OBJITEMS_PATH);
    }
    return ok;
}

bool objitems_save(const objitems_t *in) {
    if (!in)
        return false;
    lock();
    bool ok = save_locked(in);
    unlock();
    return ok;
}

// ---------------------------------------------------------------------------
// Wire format
// ---------------------------------------------------------------------------

// Decimal degrees -> APRS uncompressed "DDMM.mmN" / "DDDMM.mmW" fields,
// identical to beacon.c's latLonToAprs().
static void lat_lon_to_aprs(float lat, float lon, char *latOut, size_t latMax, char *lonOut, size_t lonMax) {
    float alat = fabsf(lat);
    int dLat = (int)alat;
    float mLat = (alat - dLat) * 60.0f;
    snprintf(latOut, latMax, "%02d%05.2f%c", dLat, mLat, lat >= 0 ? 'N' : 'S');

    float alon = fabsf(lon);
    int dLon = (int)alon;
    float mLon = (alon - dLon) * 60.0f;
    snprintf(lonOut, lonMax, "%03d%05.2f%c", dLon, mLon, lon >= 0 ? 'E' : 'W');
}

// Builds the APRS Object or Item info field for one element into `out`.
//
// `live` overrides b->active for the transmit-time live/kill decision (so the
// kill sequence can force a kill report even while the stored element is still
// nominally "active" pending the user's next edit). `out` should be >= 96.
static void objitem_build_info_field(const objitem_t *b, bool live, char *out, size_t out_size) {
    char latStr[10], lonStr[11];
    lat_lon_to_aprs(b->lat, b->lon, latStr, sizeof(latStr), lonStr, sizeof(lonStr));

    char sym_table = b->sym[0] ? b->sym[0] : '/';
    char sym_code = b->sym[1] ? b->sym[1] : '-';

    // Course/speed extension: only when speed > 0 (matches YAAC: "If the speed
    // is set to zero, speed and course will not be included"). APRS CSE/SPD is
    // "CCC/SSS" with course in degrees (0..359) and speed in knots, both
    // exactly 3 digits. Both values are clamped so each field is always 3
    // digits, keeping the fixed "NNN/NNN\0" (8-byte) buffer from truncating -
    // the ESP-IDF build treats -Wformat-truncation as an error.
    char cse_spd[8];
    cse_spd[0] = 0;
    if (b->speed > 0) {
        unsigned crs = (unsigned)(b->course % 360);        // 0..359
        unsigned spd = b->speed > 999 ? 999u : b->speed;   // APRS speed field is 3 digits
        snprintf(cse_spd, sizeof(cse_spd), "%03u/%03u", crs, spd);
    }

    if (b->is_item) {
        // Item: ) NAME (3..9, variable) then '!'(live)/'_'(kill) then position.
        char name[OBJITEM_NAME_MAX + 1];
        clamp_str(name, b->name, OBJITEM_NAME_MAX);
        snprintf(out, out_size, ")%s%c%s%c%s%c%s%s", name, live ? '!' : '_', latStr, sym_table, lonStr, sym_code, cse_spd, b->comment);
    } else {
        // Object: ; NAME (exactly 9, space-padded) then '*'(live)/'_'(kill)
        // then DDHHMMz timestamp then position.
        char name9[OBJITEM_NAME_MAX + 1];
        // Space-pad the name to exactly 9 characters.
        size_t nl = 0;
        while (nl < OBJITEM_NAME_MAX && b->name[nl])
            nl++;
        memset(name9, ' ', OBJITEM_NAME_MAX);
        memcpy(name9, b->name, nl);
        name9[OBJITEM_NAME_MAX] = 0;

        char ts[8];
        time_t now = time(NULL);
        struct tm tmv;
        gmtime_r(&now, &tmv);
        snprintf(ts, sizeof(ts), "%02d%02d%02dz", tmv.tm_mday, tmv.tm_hour, tmv.tm_min);

        snprintf(out, out_size, ";%s%c%s%s%c%s%c%s%s", name9, live ? '*' : '_', ts, latStr, sym_table, lonStr, sym_code, cse_spd,
                 b->comment);
    }
}

// ---------------------------------------------------------------------------
// Transmission
// ---------------------------------------------------------------------------

// Resolves the station callsign used as the report source. Prefers the shared
// "My Station" callsign; falls back to the IGate APRS callsign+SSID. Same
// policy as bulletins.c's resolve_source_call(), but written as an explicit
// bounded copy (no strncpy / no "%s-%d" snprintf) so the ESP-IDF build - which
// treats -Wstringop-truncation and -Wformat-truncation as errors - can prove
// the destination never overflows regardless of the source lengths.
static void append_bounded(char *out, size_t out_size, size_t *used, const char *src) {
    if (!src)
        return;
    while (*src && *used + 1 < out_size)
        out[(*used)++] = *src++;
    out[*used] = 0;
}

static void resolve_source_call(char *out, size_t out_size) {
    out[0] = 0;
    if (out_size == 0)
        return;
    size_t used = 0;
    if (g_config.my_callsign[0]) {
        append_bounded(out, out_size, &used, g_config.my_callsign);
    } else if (g_config.aprs_mycall[0]) {
        append_bounded(out, out_size, &used, g_config.aprs_mycall);
        if (g_config.aprs_ssid > 0) {
            int ssid = (int)g_config.aprs_ssid;
            if (ssid > 15)
                ssid = 15; // AX.25 SSID is 0..15
            char suf[4]; // "-15\0" max
            suf[0] = '-';
            if (ssid >= 10) {
                suf[1] = (char)('0' + ssid / 10);
                suf[2] = (char)('0' + ssid % 10);
                suf[3] = 0;
            } else {
                suf[1] = (char)('0' + ssid);
                suf[2] = 0;
            }
            append_bounded(out, out_size, &used, suf);
        }
    }
    for (char *p = out; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p -= 32;
}

// Effective RF / INET flags: the AND of the per-element checkbox and the
// element's scope (PRIVATE never transmits; LOCAL is RF-only; GLOBAL allows
// both). This lets the two checkboxes act as the fine control the task asked
// for while scope sets an upper bound (YAAC semantics).
static bool objitem_effective_rf(const objitem_t *b) {
    if (b->scope == OBJITEM_SCOPE_PRIVATE)
        return false;
    return b->send_rf; // LOCAL and GLOBAL both allow RF
}

static bool objitem_effective_inet(const objitem_t *b) {
    if (b->scope != OBJITEM_SCOPE_GLOBAL)
        return false; // PRIVATE and LOCAL never reach APRS-IS
    return b->send_inet;
}

static void tx_one(int idx, const objitem_t *b, const char *src, bool live) {
    char info[96];
    objitem_build_info_field(b, live, info, sizeof(info));

    const char *kind = b->is_item ? "Item" : "Object";
    const char *state = live ? "live" : "KILL";

    if (objitem_effective_rf(b)) {
        // Sent direct (no digipeater path), matching the bulletin transmitter:
        // the page exposes only enable/RF/Internet/scope/position/symbol/
        // course-speed/comment/interval, no unproto path.
        char packet[160];
        int len = snprintf(packet, sizeof(packet), "%s>%s:%s", src, OBJITEM_DEST, info);
        if (len > 0 && len < (int)sizeof(packet)) {
            if (aprs_service_send_tnc2(packet, (size_t)len))
                ESP_LOGI(TAG, "%s %d TX (RF, %s): %s", kind, idx + 1, state, packet);
            else
                ESP_LOGW(TAG, "%s %d NOT sent over RF - modem not ready or busy", kind, idx + 1);
        }
    }
    if (objitem_effective_inet(b)) {
        // Locally-originated APRS-IS traffic carries the TCPIP* q-construct,
        // never an RF unproto path (same note as message.c / bulletins.c).
        char packet[160];
        int len = snprintf(packet, sizeof(packet), "%s>%s,TCPIP*:%s", src, OBJITEM_DEST, info);
        if (len > 0 && len < (int)sizeof(packet)) {
            if (igate_send_raw(packet, (size_t)len))
                ESP_LOGI(TAG, "%s %d TX (INET, %s): %s", kind, idx + 1, state, packet);
            else
                ESP_LOGW(TAG, "%s %d NOT sent over INET - APRS-IS not connected yet", kind, idx + 1);
        }
    }
}

// Bound a configured interval into [floor, ...], with 0 meaning "use the
// default" (same policy as beacon.c/bulletins.c).
static uint32_t clamp_interval(uint32_t interval) {
    if (interval == 0)
        return OBJITEM_DEFAULT_INTERVAL_S;
    if (interval < OBJITEM_MIN_INTERVAL_S)
        return OBJITEM_MIN_INTERVAL_S;
    return interval;
}

// Monotonic seconds since boot - scheduling uses this so an NTP step of the
// wall clock (used only for the Object timestamp) never disturbs cadence.
static int64_t mono_seconds(void) {
    return (int64_t)(esp_timer_get_time() / 1000000);
}

// Per-element next-due timestamps (monotonic seconds). 0 = due now, so every
// enabled element transmits once on the first pass after start.
static int64_t s_next_due[OBJITEM_COUNT] = { 0 };

uint32_t objitems_service(void) {
    // One-time settle delay after boot before the first transmit pass.
    static bool started = false;
    if (!started) {
        started = true;
        return OBJITEM_START_DELAY_S;
    }

    objitems_t set;
    objitems_load(&set);

    char src[16];
    resolve_source_call(src, sizeof(src));

    int64_t now = mono_seconds();
    int64_t soonest = now + OBJITEM_POLL_CAP_S;
    bool dirty = false; // set true if any kill sequence advanced -> persist once

    for (int i = 0; i < OBJITEM_COUNT; i++) {
        objitem_t *b = &set.item[i];

        // An element is transmittable if enabled, named, has some destination,
        // and its scope isn't PRIVATE.
        bool has_dest = objitem_effective_rf(b) || objitem_effective_inet(b);
        bool sendable = b->enable && b->name[0] && has_dest;

        if (!sendable || !src[0]) {
            // Reset so re-enabling / naming / setting a callsign fires an
            // immediate transmit on the next pass instead of waiting a stale
            // timer.
            s_next_due[i] = 0;
            continue;
        }

        if (now >= s_next_due[i]) {
            if (!b->active || b->kill_left > 0) {
                // Kill path: element is being retired. Force a kill report and
                // count it down. When the last kill report goes out, clear the
                // element's enable flag so it leaves the air and shows disabled
                // in the UI (mirrors bulletins' expiry auto-disable).
                if (b->kill_left == 0)
                    b->kill_left = OBJITEM_KILL_REPEATS; // first kill pass arms the repeat count
                tx_one(i, b, src, false /* kill report */);
                b->kill_left--;
                if (b->kill_left == 0) {
                    b->enable = false;
                    ESP_LOGI(TAG, "%s %d kill complete - disabled", b->is_item ? "Item" : "Object", i + 1);
                }
                dirty = true;
            } else {
                // Normal live report.
                tx_one(i, b, src, true);
            }
            s_next_due[i] = now + (int64_t)clamp_interval(b->interval_s);
            vTaskDelay(pdMS_TO_TICKS(OBJITEM_INTER_TX_MS));
            now = mono_seconds(); // account for the inter-TX gap
        }

        if (s_next_due[i] < soonest)
            soonest = s_next_due[i];
    }

    if (dirty)
        objitems_save(&set);

    ESP_LOGD(TAG, "objitems_service stack free: %u bytes", (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));

    int64_t sleep_s = soonest - mono_seconds();
    if (sleep_s < 1)
        sleep_s = 1;
    if (sleep_s > OBJITEM_POLL_CAP_S)
        sleep_s = OBJITEM_POLL_CAP_S;
    return (uint32_t)sleep_s;
}

void objitems_start(void) {
    // No task creation here: the transmitter is driven by the shared beacon
    // scheduler (beacon_scheduler_start()) via objitems_service(). Just make
    // sure the LittleFS lock exists.
    ensure_lock();
    ESP_LOGI(TAG, "Objects/Items configured (per-element interval, default=%us; driven by beacon scheduler)", (unsigned)OBJITEM_DEFAULT_INTERVAL_S);
}
