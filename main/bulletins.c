// @file bulletins.c
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
// @brief APRS bulletin store (LittleFS-backed) and periodic transmitter.
//
// See bulletins.h for the design rationale (why bulletins live in their own
// /storage/bulletins.json file instead of g_config, and how expiry works).

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_config.h"
#include "aprs_service.h"
#include "bulletins.h"
#include "igate.h"
#include "json_escape.h" // json_write_escaped()
#include "json_store.h"  // shared JSON-file store scaffolding
#include "sched_time.h"  // sched_mono_seconds() / sched_clamp_interval()
#include "storage.h"     // storage_write_lock() / storage_generation()

static const char *TAG = "bulletins";

// A bulletin slot with no identifier configured falls back to the digit of its
// own slot number ('1' + index), which bulletins_build_addressee() emits as a
// single character. Keep the count within a single decimal digit so that
// fallback stays one character wide.
_Static_assert(BULLETIN_COUNT <= 9, "BULLETIN_COUNT must stay a single digit for the default BLNn addressee");

#define BULLETINS_PATH     "/storage/bulletins.json"
#define BULLETINS_TMP_PATH "/storage/bulletins.json.tmp"

// Same software-identifier destination call used by the beacon and message
// components, for consistency across the firmware.
#define BULLETIN_DEST "APE32L"

// Per-bulletin transmit interval. Each bulletin carries its own "Beacon
// interval (s)" (bulletin_t.interval_s); these are the bounds
// sched_clamp_interval() applies to it, so a 0 (or unset) interval falls back
// to the default and anything below the floor is raised to it, and bulletins
// cannot be configured to hammer RF/APRS-IS.
#define BULLETIN_MIN_INTERVAL_S     30   // sanity floor
#define BULLETIN_DEFAULT_INTERVAL_S 1800 // 30 min, used when interval_s == 0

// Upper bound on how long the scheduler waits between passes. Even when every
// bulletin's interval is long, the config is re-read at least this often so web
// edits (interval/enable/text) and expiry are picked up promptly.
#define BULLETIN_POLL_CAP_S 60

// One-time settle delay after boot before the first transmit pass, so WiFi/
// APRS-IS association and the modem have a chance to come up first.
#define BULLETIN_START_DELAY_S 60

// Small gap between consecutive bulletin transmissions, so a burst of enabled
// bulletins that come due together don't hit the modem/APRS-IS all at once.
#define BULLETIN_INTER_TX_MS 1500

// Wall-clock sanity floor (2020-09-13). time() below this means NTP hasn't
// synced yet, so absolute expiry deadlines can't be trusted/armed.
#define BULLETIN_TIME_VALID_THRESHOLD 1600000000LL

// Serializes LittleFS load/save between the web save handler and the TX task.
static SemaphoreHandle_t s_lock;

static void lock(void) {
    json_store_lock_take(&s_lock);
}

static void unlock(void) {
    json_store_lock_give(&s_lock);
}

static bool clock_valid(void) {
    return (int64_t)time(NULL) >= BULLETIN_TIME_VALID_THRESHOLD;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

static bool load_locked(bulletins_t *out, bool *out_missing) {
    memset(out, 0, sizeof(*out));
    if (out_missing)
        *out_missing = false;

    cJSON *doc = NULL;
    json_store_status_t st = json_store_read(BULLETINS_PATH, TAG, "bulletins", &doc);
    if (st != JSON_STORE_OK) {
        // Only an absent file tells the caller to write the defaults out; an
        // empty or unparseable one leaves the existing file alone so the
        // operator can see it.
        if (out_missing && st == JSON_STORE_MISSING)
            *out_missing = true;
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
            v = cJSON_GetObjectItem(o, "id");
            if (cJSON_IsString(v) && v->valuestring && v->valuestring[0])
                b->ident = v->valuestring[0];
            v = cJSON_GetObjectItem(o, "grp");
            if (cJSON_IsString(v) && v->valuestring) {
                strncpy(b->group, v->valuestring, BULLETIN_GROUP_MAX);
                b->group[BULLETIN_GROUP_MAX] = 0;
            }
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
    // Entered with s_lock held (see bulletins_save() below), which is what
    // json_store_open_tmp() asserts before handing back a stream whose stdio
    // buffer is already pinned.
    FILE *f = json_store_open_tmp(BULLETINS_TMP_PATH, TAG, s_lock);
    if (!f)
        return false;

    // Written token-by-token straight to the file: no cJSON tree and no second
    // serialized buffer ever exist, so a save costs essentially only littlefs's
    // own write buffer on top of the stream buffer above.
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
        char ident[2] = { b->ident, 0 };
        char group[BULLETIN_GROUP_MAX + 1];
        strncpy(group, b->group, BULLETIN_GROUP_MAX);
        group[BULLETIN_GROUP_MAX] = 0;
        fputs("\"id\":", f);
        json_write_escaped(f, ident);
        fputs(",\"grp\":", f);
        json_write_escaped(f, group);
        fputc(',', f);
        fputs("\"text\":", f);
        json_write_escaped(f, text);
        fprintf(f, ",\"int_s\":%u", (unsigned)b->interval_s);
        fprintf(f, ",\"exp_h\":%u,", (unsigned)b->expire_hours);
        fprintf(f, "\"exp_at\":%lld", (long long)b->expire_at);
        fputc('}', f);
    }
    fputs("]}", f);

    return json_store_commit(f, BULLETINS_TMP_PATH, BULLETINS_PATH, TAG, "bulletins");
}

// Parsed copy of bulletins.json, kept in RAM so a scheduler pass costs nothing
// on the filesystem. bulletins_service() runs on every pass of the shared
// beacon scheduler - as often as every 5 s while the other services are idle -
// and each pass called bulletins_load(), i.e. an fopen + fread + a full
// cJSON_Parse of this file, on the order of ten thousand times a day. That
// parse builds and tears down a tree of small heap nodes, the exact allocation
// pattern the streaming writers exist to avoid; holding the result
// costs ~sizeof(bulletins_t) of static RAM once and removes the churn.
//
// The cache is only allowed to answer when nothing can have changed underneath
// it: bulletins_save() drops it (every web edit and every expiry-driven
// rewrite goes through there), and s_cache_gen catches the changes made from
// outside this module - a whole-partition format, a delete, or a file uploaded
// over this one from the web Storage page (see storage_generation()).
static bulletins_t s_cache;
static bool s_cache_valid = false;
static bool s_cache_ok = false; // what load_locked() reported for the cached content
static uint32_t s_cache_gen = 0;

bool bulletins_load(bulletins_t *out) {
    if (!out)
        return false;
    lock();
    if (s_cache_valid && s_cache_gen == storage_generation()) {
        *out = s_cache;
        bool cached_ok = s_cache_ok;
        unlock();
        return cached_ok;
    }
    bool missing = false;
    bool ok = load_locked(out, &missing);
    // Cache the defaults substituted for a missing/corrupt file too: they are
    // what every caller would get from a re-read anyway, and doing so keeps a
    // subsystem that is simply not configured from re-reading the filesystem
    // on every scheduler pass.
    s_cache = *out;
    s_cache_ok = ok;
    s_cache_gen = storage_generation();
    s_cache_valid = true;
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
    // Module lock first, filesystem-wide writer gate second (storage.h): the
    // temp-file + rename sequence inside save_locked() must not overlap the
    // whole-partition format the web Storage page can start.
    storage_write_lock();
    bool ok = save_locked(in);
    storage_write_unlock();
    // Drop the cache rather than filling it from *in: what lands on disk is
    // what a later load will parse back, and save_locked() bounds the text it
    // writes, so the next reader re-reads the file once and caches exactly
    // what the file says.
    s_cache_valid = false;
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

void bulletins_build_addressee(const bulletin_t *b, int idx, char *out, size_t out_size) {
    if (out == NULL || out_size == 0)
        return;

    // APRS message addressee is exactly 9 chars, space-padded. Built
    // char-by-char rather than through a format string so the identifier
    // stays a single character whatever is stored, and the compiler's
    // -Werror=format-truncation has no worst-case width to complain about.
    char addr[10];
    addr[0] = 'B';
    addr[1] = 'L';
    addr[2] = 'N';

    char id = b->ident;
    if (id >= 'a' && id <= 'z')
        id = (char)(id - 32);
    bool announcement = (id >= 'A' && id <= 'Z');
    bool bulletin = (id >= '0' && id <= '9');
    if (!announcement && !bulletin) {
        id = (char)('0' + (idx + 1)); // idx 0..8 -> '1'..'9'
        bulletin = true;
    }
    addr[3] = id;

    size_t n = 4;
    // Only numbered bulletins carry a group name: an announcement's identifier
    // letter already occupies the whole discriminator field in APRS101.
    if (bulletin) {
        for (size_t i = 0; i < BULLETIN_GROUP_MAX && b->group[i] && n < 9; i++) {
            char c = b->group[i];
            if (c >= 'a' && c <= 'z')
                c = (char)(c - 32);
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
                addr[n++] = c;
        }
    }
    while (n < 9)
        addr[n++] = ' ';
    addr[9] = 0;

    snprintf(out, out_size, "%s", addr);
}

// Builds the ":BLNx     :text" APRS message info field for bulletin index i.
static void build_info_field(int idx, const bulletin_t *b, const char *text, char *out, size_t out_size) {
    // No message number/ack is appended: bulletins never carry one.
    char addr[10];
    bulletins_build_addressee(b, idx, addr, sizeof(addr));
    snprintf(out, out_size, ":%s:%s", addr, text);
}

static void tx_one(int idx, const bulletin_t *b, const char *src) {
    char info[128];
    build_info_field(idx, b, b->text, info, sizeof(info));

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

// Per-bulletin next-due timestamps (monotonic seconds). 0 = due now, so every
// enabled bulletin transmits once on the first pass after start. These live at
// file scope because the transmitter is a serviced pass (bulletins_service)
// driven by the shared beacon scheduler rather than a task loop of its own, so
// the deadlines must survive between calls.
static int64_t s_bln_next_due[BULLETIN_COUNT] = { 0 };

// One serviced pass of the bulletin transmitter. Called by the shared beacon
// scheduler (beacon_scheduler.c); returns the number of seconds until the
// transmitter next wants servicing (>= 1). The per-bulletin timers live at file
// scope and the one-time boot settle delay is returned from the first call
// rather than slept through, so a pass never blocks the shared scheduler.
uint32_t bulletins_service(void) {
    // One-time settle delay after boot before the first transmit pass, so
    // WiFi/APRS-IS association and the modem have a chance to come up first.
    static bool started = false;
    if (!started) {
        started = true;
        return BULLETIN_START_DELAY_S;
    }

    // A false load means the file was missing or unusable and empty defaults
    // were substituted; the pass runs on those either way. The result is
    // cached until the next write, so the report is made on the transition
    // rather than on every pass, which at this cadence would be a log flood.
    static bool warned_load = false;
    bulletins_t set;
    bool loaded = bulletins_load(&set);
    if (!loaded && !warned_load)
        ESP_LOGW(TAG, "%s unusable, transmitting from substituted defaults", BULLETINS_PATH);
    warned_load = !loaded;

    // Enforce expiry first, and persist the disable so the web UI reflects
    // it even if nothing is transmitted this pass.
    if (bulletins_apply_expiry(&set)) {
        if (!bulletins_save(&set))
            ESP_LOGE(TAG, "expired bulletins could not be written to %s", BULLETINS_PATH);
    }

    char src[16];
    resolve_source_call(src, sizeof(src));

    int64_t now = sched_mono_seconds();
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
            s_bln_next_due[i] = now + (int64_t)sched_clamp_interval(b->interval_s, BULLETIN_MIN_INTERVAL_S, BULLETIN_DEFAULT_INTERVAL_S);
            vTaskDelay(pdMS_TO_TICKS(BULLETIN_INTER_TX_MS));
            now = sched_mono_seconds(); // account for the inter-TX gap
        }

        if (s_bln_next_due[i] < soonest)
            soonest = s_bln_next_due[i];
    }

    ESP_LOGD(TAG, "bulletins_service stack free: %u bytes", (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));

    // Sleep until the soonest bulletin is due, capped so config edits and
    // expiry are still picked up promptly.
    int64_t sleep_s = soonest - sched_mono_seconds();
    if (sleep_s < 1)
        sleep_s = 1;
    if (sleep_s > BULLETIN_POLL_CAP_S)
        sleep_s = BULLETIN_POLL_CAP_S;
    return (uint32_t)sleep_s;
}

void bulletins_start(void) {
    // The bulletin transmitter is driven by the shared beacon scheduler
    // (beacon_scheduler_start()) via bulletins_service(), so there is no task
    // to create here - only the LittleFS lock to bring up.
    json_store_lock_ensure(&s_lock);
    ESP_LOGI(TAG, "Bulletins configured (per-bulletin interval, default=%us; driven by beacon scheduler)", (unsigned)BULLETIN_DEFAULT_INTERVAL_S);
}
