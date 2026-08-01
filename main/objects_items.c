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
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_config.h"
#include "aprs_coord.h"
#include "aprs_service.h"
#include "igate.h"
#include "json_escape.h" // json_write_escaped()
#include "json_store.h"  // shared JSON-file store scaffolding
#include "objects_items.h"
#include "sched_time.h" // sched_mono_seconds() / sched_clamp_interval()
#include "storage.h"    // storage_write_lock() / storage_generation()

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
#define OBJITEM_MIN_INTERVAL_S     30  // sanity floor
#define OBJITEM_DEFAULT_INTERVAL_S 600 // 10 min, used when interval_s == 0

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

static void lock(void) {
    json_store_lock_take(&s_lock);
}

static void unlock(void) {
    json_store_lock_give(&s_lock);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

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
        // PHG height defaults to the smallest code-table value (10 ft) so the
        // Station-page height <select> always has a matching selected option.
        out->item[i].phg_height = 10;
    }

    cJSON *doc = NULL;
    json_store_status_t st = json_store_read(OBJITEMS_PATH, TAG, "objects/items", &doc);
    if (st != JSON_STORE_OK) {
        // Only an absent file tells the caller to write the defaults out; an
        // empty or unparseable one leaves the existing file alone so the
        // operator can see it.
        if (out_missing && st == JSON_STORE_MISSING)
            *out_missing = true;
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

            // -- Area object (YAAC "Area type, color, and offset"). --
            v = cJSON_GetObjectItem(o, "atype");
            if (cJSON_IsNumber(v)) {
                int t = (int)v->valuedouble;
                if (t < 0)
                    t = 0;
                if (t > 9)
                    t = 9;
                b->area_type = (uint8_t)t;
            }
            v = cJSON_GetObjectItem(o, "acol");
            if (cJSON_IsNumber(v)) {
                int c = (int)v->valuedouble;
                if (c < 0)
                    c = 0;
                if (c > 15)
                    c = 15;
                b->area_color = (uint8_t)c;
            }
            v = cJSON_GetObjectItem(o, "alat");
            if (cJSON_IsNumber(v) && v->valuedouble >= 0)
                b->area_lat_off = (float)v->valuedouble;
            v = cJSON_GetObjectItem(o, "alon");
            if (cJSON_IsNumber(v) && v->valuedouble >= 0)
                b->area_lon_off = (float)v->valuedouble;

            // -- Signpost (YAAC "Signpost"). --
            v = cJSON_GetObjectItem(o, "sign");
            if (cJSON_IsString(v) && v->valuestring)
                clamp_str(b->signpost, v->valuestring, OBJITEM_SIGNPOST_MAX);

            // -- DF report (APRS101 ch.16 "/BRG/NRQ" extension). --
            v = cJSON_GetObjectItem(o, "dfEn");
            b->df_enable = cJSON_IsTrue(v);
            v = cJSON_GetObjectItem(o, "dfBrg");
            if (cJSON_IsNumber(v) && v->valuedouble >= 0)
                b->df_bearing = (uint16_t)((int)v->valuedouble % 360);
            v = cJSON_GetObjectItem(o, "dfN");
            if (cJSON_IsNumber(v)) {
                int n = (int)v->valuedouble;
                if (n < 0)
                    n = 0;
                if (n > 9)
                    n = 9;
                b->df_nrq_n = (uint8_t)n;
            }
            v = cJSON_GetObjectItem(o, "dfR");
            if (cJSON_IsNumber(v)) {
                int r = (int)v->valuedouble;
                if (r < 0)
                    r = 0;
                if (r > 9)
                    r = 9;
                b->df_nrq_r = (uint8_t)r;
            }
            v = cJSON_GetObjectItem(o, "dfQ");
            if (cJSON_IsNumber(v)) {
                int q = (int)v->valuedouble;
                if (q < 0)
                    q = 0;
                if (q > 9)
                    q = 9;
                b->df_nrq_q = (uint8_t)q;
            }

            // -- Repeater radio parameters (YAAC "Monitor frequency, duplex
            //    direction, and subaudible tone"). --
            v = cJSON_GetObjectItem(o, "freq");
            if (cJSON_IsNumber(v) && v->valuedouble > 0)
                b->freq_mhz = (float)v->valuedouble;
            v = cJSON_GetObjectItem(o, "ofs");
            if (cJSON_IsNumber(v) && v->valuedouble >= 0)
                b->offset_khz = (uint16_t)v->valuedouble;
            v = cJSON_GetObjectItem(o, "dup");
            if (cJSON_IsNumber(v)) {
                int d = (int)v->valuedouble;
                b->duplex = (int8_t)(d > 0 ? 1 : (d < 0 ? -1 : 0));
            }
            v = cJSON_GetObjectItem(o, "tone");
            if (cJSON_IsNumber(v) && v->valuedouble >= 0)
                b->tone_tenths = (uint16_t)v->valuedouble;

            // -- Digipeat paths (YAAC "Digipeat paths"). --
            v = cJSON_GetObjectItem(o, "pmask");
            if (cJSON_IsNumber(v)) {
                int m = (int)v->valuedouble;
                b->path_mask = (uint8_t)(m & ((1 << OBJITEM_PATH_PRESETS) - 1));
            }

            // -- QRU group membership (YAAC "QRU group membership"). --
            v = cJSON_GetObjectItem(o, "qru");
            if (cJSON_IsString(v) && v->valuestring)
                clamp_str(b->qru, v->valuestring, OBJITEM_QRU_MAX);

            v = cJSON_GetObjectItem(o, "int_s");
            if (cJSON_IsNumber(v) && v->valuedouble > 0)
                b->interval_s = (uint32_t)v->valuedouble;

            // -- Decay ratio + slow repeat rate (YAAC). --
            v = cJSON_GetObjectItem(o, "slow_s");
            if (cJSON_IsNumber(v) && v->valuedouble > 0)
                b->slow_interval_s = (uint32_t)v->valuedouble;
            v = cJSON_GetObjectItem(o, "decay");
            if (cJSON_IsNumber(v) && v->valuedouble > 0)
                b->decay_x10 = (uint16_t)v->valuedouble;

            // -- PHG block (mirrors the Station page's "My Station" PHG). --
            v = cJSON_GetObjectItem(o, "phgEn");
            b->phg_enable = cJSON_IsTrue(v);
            v = cJSON_GetObjectItem(o, "phgUS");
            b->phg_use_station = cJSON_IsTrue(v);
            v = cJSON_GetObjectItem(o, "phgP");
            if (cJSON_IsNumber(v) && v->valuedouble >= 0)
                b->phg_power = (uint16_t)v->valuedouble;
            v = cJSON_GetObjectItem(o, "phgG");
            if (cJSON_IsNumber(v) && v->valuedouble >= 0)
                b->phg_gain = (float)v->valuedouble;
            v = cJSON_GetObjectItem(o, "phgH");
            if (cJSON_IsNumber(v) && v->valuedouble > 0)
                b->phg_height = (uint16_t)v->valuedouble;
            v = cJSON_GetObjectItem(o, "phgD");
            if (cJSON_IsNumber(v) && v->valuedouble >= 0)
                b->phg_dir = (uint8_t)v->valuedouble;
            v = cJSON_GetObjectItem(o, "phg");
            if (cJSON_IsString(v) && v->valuestring)
                clamp_str(b->phg, v->valuestring, sizeof(b->phg) - 1);

            v = cJSON_GetObjectItem(o, "compress");
            b->compress = cJSON_IsTrue(v);

            v = cJSON_GetObjectItem(o, "kill_left");
            if (cJSON_IsNumber(v) && v->valuedouble > 0)
                b->kill_left = (uint8_t)v->valuedouble;
        }
    }

    cJSON_Delete(doc);
    return true;
}

static bool save_locked(const objitems_t *in) {
    // Entered with s_lock held (see objitems_save() below), which is what
    // json_store_open_tmp() asserts before handing back a stream whose stdio
    // buffer is already pinned.
    FILE *f = json_store_open_tmp(OBJITEMS_TMP_PATH, TAG, s_lock);
    if (!f)
        return false;

    // Written token-by-token straight to the file: no cJSON tree and no second
    // serialized buffer ever exist, so a save costs essentially only littlefs's
    // own write buffer on top of the stream buffer above.
    fputs("{\"objitems\":[", f);
    for (int i = 0; i < OBJITEM_COUNT; i++) {
        const objitem_t *b = &in->item[i];
        char name[OBJITEM_NAME_MAX + 1];
        char cmt[OBJITEM_COMMENT_MAX + 1];
        char sign[OBJITEM_SIGNPOST_MAX + 1];
        char qru[OBJITEM_QRU_MAX + 1];
        char sym[3];
        clamp_str(name, b->name, OBJITEM_NAME_MAX);
        clamp_str(cmt, b->comment, OBJITEM_COMMENT_MAX);
        clamp_str(sign, b->signpost, OBJITEM_SIGNPOST_MAX);
        clamp_str(qru, b->qru, OBJITEM_QRU_MAX);
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
        json_write_escaped(f, name);
        fprintf(f, ",\"lat\":%.6f", (double)b->lat);
        fprintf(f, ",\"lon\":%.6f", (double)b->lon);
        fputs(",\"sym\":", f);
        json_write_escaped(f, sym);
        fprintf(f, ",\"crs\":%u", (unsigned)b->course);
        fprintf(f, ",\"spd\":%u", (unsigned)b->speed);
        fprintf(f, ",\"scope\":%d", (int)b->scope);
        fputs(",\"cmt\":", f);
        json_write_escaped(f, cmt);
        fprintf(f, ",\"atype\":%u", (unsigned)b->area_type);
        fprintf(f, ",\"acol\":%u", (unsigned)b->area_color);
        fprintf(f, ",\"alat\":%.4f", (double)b->area_lat_off);
        fprintf(f, ",\"alon\":%.4f", (double)b->area_lon_off);
        fputs(",\"sign\":", f);
        json_write_escaped(f, sign);
        fprintf(f, ",\"dfEn\":%s", b->df_enable ? "true" : "false");
        fprintf(f, ",\"dfBrg\":%u", (unsigned)b->df_bearing);
        fprintf(f, ",\"dfN\":%u", (unsigned)b->df_nrq_n);
        fprintf(f, ",\"dfR\":%u", (unsigned)b->df_nrq_r);
        fprintf(f, ",\"dfQ\":%u", (unsigned)b->df_nrq_q);
        fprintf(f, ",\"freq\":%.4f", (double)b->freq_mhz);
        fprintf(f, ",\"ofs\":%u", (unsigned)b->offset_khz);
        fprintf(f, ",\"dup\":%d", (int)b->duplex);
        fprintf(f, ",\"tone\":%u", (unsigned)b->tone_tenths);
        fprintf(f, ",\"pmask\":%u", (unsigned)b->path_mask);
        fputs(",\"qru\":", f);
        json_write_escaped(f, qru);
        fprintf(f, ",\"int_s\":%u", (unsigned)b->interval_s);
        fprintf(f, ",\"slow_s\":%u", (unsigned)b->slow_interval_s);
        fprintf(f, ",\"decay\":%u", (unsigned)b->decay_x10);
        fprintf(f, ",\"phgEn\":%s", b->phg_enable ? "true" : "false");
        fprintf(f, ",\"phgUS\":%s", b->phg_use_station ? "true" : "false");
        fprintf(f, ",\"phgP\":%u", (unsigned)b->phg_power);
        fprintf(f, ",\"phgG\":%.1f", (double)b->phg_gain);
        fprintf(f, ",\"phgH\":%u", (unsigned)b->phg_height);
        fprintf(f, ",\"phgD\":%u", (unsigned)b->phg_dir);
        {
            char phg[sizeof(b->phg)];
            clamp_str(phg, b->phg, sizeof(phg) - 1);
            fputs(",\"phg\":", f);
            json_write_escaped(f, phg);
        }
        fprintf(f, ",\"compress\":%s", b->compress ? "true" : "false");
        fprintf(f, ",\"kill_left\":%u", (unsigned)b->kill_left);
        fputc('}', f);
    }
    fputs("]}", f);

    return json_store_commit(f, OBJITEMS_TMP_PATH, OBJITEMS_PATH, TAG, "objects/items");
}

// Parsed copy of objitems.json, kept in RAM so a scheduler pass costs nothing
// on the filesystem. objitems_service() runs on every pass of the shared beacon
// scheduler - as often as every 5 s while the other services are idle - and
// each pass called objitems_load(), i.e. an fopen + fread + a full
// cJSON_Parse of this file, on the order of ten thousand times a day. That
// parse builds and tears down a tree of small heap nodes, the exact allocation
// pattern the streaming writers exist to avoid; holding the result
// costs ~sizeof(objitems_t) of static RAM once and removes the churn.
//
// The cache is only allowed to answer when nothing can have changed underneath
// it: objitems_save() drops it (every web edit and every kill-sequence rewrite
// goes through there), and s_cache_gen catches the changes made from outside
// this module - a whole-partition format, a delete, or a file uploaded over
// this one from the web Storage page (see storage_generation()).
static objitems_t s_cache;
static bool s_cache_valid = false;
static bool s_cache_ok = false; // what load_locked() reported for the cached content
static uint32_t s_cache_gen = 0;

bool objitems_load(objitems_t *out) {
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
    // Module lock first, filesystem-wide writer gate second (storage.h): the
    // temp-file + rename sequence inside save_locked() must not overlap the
    // whole-partition format the web Storage page can start.
    storage_write_lock();
    bool ok = save_locked(in);
    storage_write_unlock();
    // Drop the cache rather than filling it from *in: save_locked() clamps the
    // name and comment it writes, so the file can legitimately differ from the
    // caller's struct. The next reader re-reads once and caches exactly what
    // the file says.
    s_cache_valid = false;
    unlock();
    return ok;
}

// ---------------------------------------------------------------------------
// Wire format
// ---------------------------------------------------------------------------

// True when the element's symbol is the APRS Area symbol ('\l') or Signpost
// symbol ('\m'). Both use the 7-byte data-extension slot (normally CSE/SPD)
// for their own descriptor, so course/speed is suppressed for them.
static bool objitem_is_area(const objitem_t *b) {
    return b->sym[0] == '\\' && b->sym[1] == 'l';
}

static bool objitem_is_signpost(const objitem_t *b) {
    return b->sym[0] == '\\' && b->sym[1] == 'm';
}

// Encode an Area corner offset (degrees, >= 0) into the APRS 2-digit "yy"/"xx"
// code. Per the APRS symbols spec the code is the square root of the offset
// expressed in 1/100ths of a degree, so the on-air offset is (code^2)/100
// degrees. Clamped to 00..99.
static unsigned area_offset_code(float deg) {
    if (deg <= 0.0f)
        return 0;
    double code = sqrt((double)deg * 100.0);
    if (code < 0.0)
        code = 0.0;
    if (code > 99.0)
        code = 99.0;
    return (unsigned)(code + 0.5);
}

// Build the standard APRS frequency block ("FFF.FFFMHz Tnnn ±nnn") into `out`,
// or the empty string when no monitor frequency is configured. This is what
// carries YAAC's monitor frequency, subaudible tone and duplex direction; by
// convention it must be the first thing in the comment text so other stations'
// radios can auto-tune from it.
static void build_freq_block(const objitem_t *b, char *out, size_t out_size) {
    out[0] = 0;
    if (b->freq_mhz <= 0.0f || out_size == 0)
        return;

    int n = snprintf(out, out_size, "%.3fMHz", (double)b->freq_mhz);
    if (n < 0 || (size_t)n >= out_size) {
        out[0] = 0;
        return;
    }
    size_t used = (size_t)n;

    // Subaudible tone: "Tnnn" (integer Hz) when set, else "Toff".
    if (used < out_size) {
        if (b->tone_tenths > 0)
            n = snprintf(out + used, out_size - used, " T%03u", (unsigned)(b->tone_tenths / 10u));
        else
            n = snprintf(out + used, out_size - used, " Toff");
        if (n > 0 && (size_t)n < out_size - used)
            used += (size_t)n;
    }

    // Duplex direction + shift: "±nnn" in units of 10 kHz (e.g. 600 kHz => 060).
    if (b->duplex != 0 && used < out_size) {
        unsigned nnn = (unsigned)(b->offset_khz / 10u);
        if (nnn > 999)
            nnn = 999;
        n = snprintf(out + used, out_size - used, " %c%03u", b->duplex > 0 ? '+' : '-', nnn);
        if (n > 0 && (size_t)n < out_size - used)
            used += (size_t)n;
    }
}

// Build the 7-character APRS "PHGphgd" Data Extension from the element's stored
// PHG sub-fields, using the standard APRS code tables:
//   P = SQR(power)      -> digit = round(sqrt(Watts))          (0..9)
//   H = LOG2(height/10) -> digit = round(log2(feet/10))        (0..; feet=10*2^H)
//   G = gain in dB      -> digit = gain                        (0..9)
//   D = directivity     -> digit (0=omni, 1..8 = 45*D degrees) (0..8)
// Each field is a single character '0'+digit. Per APRS101/APRSdos the Height
// character may extend past '9' for very tall sites (balloons/aircraft), so it
// is not capped at 9 (only kept in a sane range). This mirrors exactly the
// PHG string the Station/Objects web pages compute and display. `out` must be
// at least 8 bytes.
static void objitem_build_phg(const objitem_t *b, char *out, size_t out_size) {
    int P = (int)lroundf(sqrtf((float)b->phg_power));
    if (P < 0)
        P = 0;
    if (P > 9)
        P = 9;

    float hf = (b->phg_height >= 10) ? (float)b->phg_height : 10.0f;
    int H = (int)lroundf(log2f(hf / 10.0f));
    if (H < 0)
        H = 0;
    if (H > 13) // 10*2^13 ft; keeps the height character a single printable ASCII byte
        H = 13;

    int G = (int)lroundf(b->phg_gain);
    if (G < 0)
        G = 0;
    if (G > 9)
        G = 9;

    int D = (int)b->phg_dir;
    if (D < 0)
        D = 0;
    if (D > 8)
        D = 8;

    snprintf(out, out_size, "PHG%c%c%c%c", '0' + P, '0' + H, '0' + G, '0' + D);
}

// Builds the APRS Object or Item info field for one element into `out`.
//
// `live` overrides b->active for the transmit-time live/kill decision (so the
// kill sequence can force a kill report even while the stored element is still
// nominally "active" pending the user's next edit). `out` should be >= 160 to
// hold the frequency block plus a full comment.
static void objitem_build_info_field(const objitem_t *b, bool live, char *out, size_t out_size) {
    char sym_table = b->sym[0] ? b->sym[0] : '/';
    char sym_code = b->sym[1] ? b->sym[1] : '-';

    // The 7-byte data-extension slot right after the symbol code. Which
    // descriptor goes here depends on the symbol:
    //   Area symbol   ("\l") -> "Tyy/Cxx" area descriptor (YAAC Area).
    //   Signpost      ("\m") -> "{TEXT}"  signpost text (YAAC Signpost).
    //   anything else        -> CSE/SPD, only when speed > 0 (YAAC:
    //                           "if the speed is set to zero, speed and course
    //                           will not be included"), optionally extended
    //                           to CSE/SPD/BRG/NRQ when df_enable is set
    //                           (APRS101 ch.16 DF report).
    // Sized for the longest of these: "NNN/NNN/NNN/NNN" (DF report, 15 bytes)
    // is now the longest, so ext[] is grown to fit it plus NUL.
    char ext[16];
    ext[0] = 0;
    bool isArea = objitem_is_area(b);
    bool isSignpost = objitem_is_signpost(b);
    if (isArea) {
        unsigned t = b->area_type > 9 ? 9 : b->area_type;
        unsigned color = b->area_color > 15 ? 15 : b->area_color;
        // Colours 0..9 use "/C"; 10..15 replace the '/' with '1' and C = C-10.
        char sep = color <= 9 ? '/' : '1';
        unsigned cdig = color <= 9 ? color : color - 10;
        snprintf(ext, sizeof(ext), "%u%02u%c%u%02u", t, area_offset_code(b->area_lat_off), sep, cdig, area_offset_code(b->area_lon_off));
    } else if (isSignpost) {
        char sp[OBJITEM_SIGNPOST_MAX + 1];
        clamp_str(sp, b->signpost, OBJITEM_SIGNPOST_MAX);
        snprintf(ext, sizeof(ext), "{%s}", sp);
    } else if (b->speed > 0) {
        unsigned crs = (unsigned)(b->course % 360);      // 0..359
        unsigned spd = b->speed > 999 ? 999u : b->speed; // APRS speed field is 3 digits
        if (b->df_enable) {
            // DF report (APRS101 ch.16): CSE/SPD extended with /BRG/NRQ. BRG
            // is the 3-digit signal bearing; NRQ packs the antenna-type digit
            // (N), signal-strength digit (R) and bearing-accuracy digit (Q)
            // into one 3-digit field.
            unsigned brg = (unsigned)(b->df_bearing % 360);
            unsigned n = b->df_nrq_n > 9 ? 9 : b->df_nrq_n;
            unsigned r = b->df_nrq_r > 9 ? 9 : b->df_nrq_r;
            unsigned q = b->df_nrq_q > 9 ? 9 : b->df_nrq_q;
            snprintf(ext, sizeof(ext), "%03u/%03u/%03u/%u%u%u", crs, spd, brg, n, r, q);
        } else {
            snprintf(ext, sizeof(ext), "%03u/%03u", crs, spd);
        }
    } else if (b->df_enable) {
        // DF report with no course/speed data: CSE/SPD is still required by
        // the spec to carry the BRG/NRQ extension, so it is emitted as
        // "000/000" (APRS101 ch.16: course/speed of 000/000 with a DF
        // extension is valid and means "no course/speed data").
        unsigned brg = (unsigned)(b->df_bearing % 360);
        unsigned n = b->df_nrq_n > 9 ? 9 : b->df_nrq_n;
        unsigned r = b->df_nrq_r > 9 ? 9 : b->df_nrq_r;
        unsigned q = b->df_nrq_q > 9 ? 9 : b->df_nrq_q;
        snprintf(ext, sizeof(ext), "000/000/%03u/%u%u%u", brg, n, r, q);
    } else if (b->phg_enable) {
        // PHG shares the 7-byte data-extension slot with CSE/SPD (they are
        // mutually exclusive), so it is emitted only for a normal symbol that
        // is not moving (speed == 0) and is not an Area/Signpost object - i.e.
        // a fixed transmitter whose coverage PHG describes. It must sit
        // immediately after the symbol and before any free-text comment, which
        // this slot is. Transmitted only when the element is enabled (this
        // per-element phg_enable flag); the element itself is only ever sent
        // when its own enable/scope/RF/INET gating already allows it.
        objitem_build_phg(b, ext, sizeof(ext));
    }

    // Compressed position format is used only when requested and the 7-byte
    // ext[] slot above isn't already carrying an Area/Signpost descriptor -
    // both of those repurpose that slot for their own encoding, which has no
    // compressed-format equivalent in the spec, so compression is silently
    // ignored for them (falls back to uncompressed) rather than dropping the
    // descriptor. It is likewise ignored whenever ext[] carries a PHG token,
    // since the compressed format has no PHG equivalent either (APRS101
    // ch.9: "this format does not support PHG"), and whenever a DF report is
    // enabled, since the compressed format's cs/T slot has no /BRG/NRQ
    // equivalent either (APRS101 ch.16 defines DF reports only for the
    // uncompressed CSE/SPD layout). Course/speed on its own (df_enable off)
    // has a compressed equivalent and is folded into the compressed field's
    // own cs/T slot below instead of the uncompressed ext[] one.
    bool useCompressed = b->compress && !isArea && !isSignpost && !b->phg_enable && !b->df_enable;

    // Sized for the larger of the two layouts: uncompressed is up to 21
    // bytes (9-char latStr content + symTable + 10-char lonStr content +
    // symCode), compressed is a fixed 13 bytes (symTable + 4 lat + 4 lon +
    // symCode + 3 cs/T), plus NUL either way.
    char posField[22];
    if (useCompressed) {
        char csT[3] = { ' ', ' ', ' ' };
        if (b->speed > 0) {
            aprs_compressed_cs_from_course_speed(b->course, b->speed, csT);
            ext[0] = 0; // folded into the compressed field's own cs/T slot instead
        }
        aprs_coord_format_compressed(b->lat, b->lon, sym_table, sym_code, csT, posField, sizeof(posField));
    } else {
        char latStr[10], lonStr[11];
        aprs_coord_format(b->lat, b->lon, latStr, sizeof(latStr), lonStr, sizeof(lonStr));
        snprintf(posField, sizeof(posField), "%s%c%s%c", latStr, sym_table, lonStr, sym_code);
    }

    // Comment text: the APRS frequency block (repeater objects) comes first, so
    // it is the leading token other stations parse; then the free-text comment.
    char freq[40];
    build_freq_block(b, freq, sizeof(freq));
    char text[OBJITEM_COMMENT_MAX + sizeof(freq) + 2];
    if (freq[0] && b->comment[0])
        snprintf(text, sizeof(text), "%s %s", freq, b->comment);
    else if (freq[0])
        snprintf(text, sizeof(text), "%s", freq);
    else
        snprintf(text, sizeof(text), "%s", b->comment);

    if (b->is_item) {
        // Item: ) NAME (3..9, variable) then '!'(live)/'_'(kill) then position.
        char name[OBJITEM_NAME_MAX + 1];
        clamp_str(name, b->name, OBJITEM_NAME_MAX);
        snprintf(out, out_size, ")%s%c%s%s%s", name, live ? '!' : '_', posField, ext, text);
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

        snprintf(out, out_size, ";%s%c%s%s%s%s", name9, live ? '*' : '_', ts, posField, ext, text);
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
            char suf[4];   // "-15\0" max
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

// `path` is the RF digipeat path to insert (e.g. "WIDE1-1,WIDE2-1"), or NULL/
// empty to send direct. It applies to the RF copy only; APRS-IS traffic always
// carries TCPIP* instead of an RF path.
static void tx_one(int idx, const objitem_t *b, const char *src, bool live, const char *path) {
    char info[200];
    objitem_build_info_field(b, live, info, sizeof(info));

    const char *kind = b->is_item ? "Item" : "Object";
    const char *state = live ? "live" : "KILL";

    if (objitem_effective_rf(b)) {
        // Digipeat path (YAAC "Digipeat paths"): inserted when the element
        // selects one or more of the shared path presets; otherwise direct.
        // Sized by the RF leg's own limit, so the length test below is the
        // same one aprs_service_send_tnc2() applies: a line that does not fit
        // an AX.25 frame is refused here, with a reason, instead of being
        // assembled and then dropped further down the transmit path.
        char packet[APRS_TNC2_BUF_SIZE];
        int len;
        if (path && path[0])
            len = snprintf(packet, sizeof(packet), "%s>%s,%s:%s", src, OBJITEM_DEST, path, info);
        else
            len = snprintf(packet, sizeof(packet), "%s>%s:%s", src, OBJITEM_DEST, info);
        if (len > 0 && len <= APRS_TNC2_MAX_LEN) {
            if (aprs_service_send_tnc2(packet, (size_t)len))
                ESP_LOGI(TAG, "%s %d TX (RF, %s): %s", kind, idx + 1, state, packet);
            else
                ESP_LOGW(TAG, "%s %d NOT sent over RF - modem not ready or busy", kind, idx + 1);
        } else if (len > APRS_TNC2_MAX_LEN) {
            ESP_LOGW(TAG, "%s %d NOT sent over RF - line too long (%d bytes, max %d)", kind, idx + 1, len, APRS_TNC2_MAX_LEN);
        }
    }
    if (objitem_effective_inet(b)) {
        // Locally-originated APRS-IS traffic carries the TCPIP* q-construct,
        // never an RF unproto path (same note as message.c / bulletins.c).
        // Same buffer size as the RF copy above, and the same length test, so
        // an element that is too long to reach the air is not quietly relayed
        // to APRS-IS either - the two legs either both carry the element or
        // both report why they did not.
        char packet[APRS_TNC2_BUF_SIZE];
        int len = snprintf(packet, sizeof(packet), "%s>%s,TCPIP*:%s", src, OBJITEM_DEST, info);
        if (len > 0 && len <= APRS_TNC2_MAX_LEN) {
            if (igate_send_raw(packet, (size_t)len))
                ESP_LOGI(TAG, "%s %d TX (INET, %s): %s", kind, idx + 1, state, packet);
            else
                ESP_LOGW(TAG, "%s %d NOT sent over INET - APRS-IS not connected yet", kind, idx + 1);
        } else if (len > APRS_TNC2_MAX_LEN) {
            ESP_LOGW(TAG, "%s %d NOT sent over INET - line too long (%d bytes, max %d)", kind, idx + 1, len, APRS_TNC2_MAX_LEN);
        }
    }
}

// Resolves the element's selected digipeat-path presets (the g_config.path[0..3]
// slots whose bit is set in path_mask) into `out` in ascending bit order,
// returning the count. Empty presets are skipped. Snapshotted under the config
// lock so a concurrent web save can't tear a preset string mid-copy.
static int objitem_paths(const objitem_t *b, char out[OBJITEM_PATH_PRESETS][72]) {
    int n = 0;
    app_config_lock();
    for (int i = 0; i < OBJITEM_PATH_PRESETS; i++) {
        if (!(b->path_mask & (1u << i)) || !g_config.path[i][0])
            continue;
        size_t k = 0;
        while (g_config.path[i][k] && k < 71) {
            out[n][k] = g_config.path[i][k];
            k++;
        }
        out[n][k] = 0;
        n++;
    }
    app_config_unlock();
    return n;
}

// One decay step: multiply the current interval by the decay ratio, bounded by
// the slow repeat rate. No-op unless a ratio >= 1.0 and a slow rate above the
// initial rate are both configured (YAAC "Decay ratio" + "Slow repeat rate").
static uint32_t objitem_decay_step(uint32_t cur, const objitem_t *b) {
    if (b->decay_x10 < 10 || b->slow_interval_s == 0)
        return cur;
    uint32_t initial = sched_clamp_interval(b->interval_s, OBJITEM_MIN_INTERVAL_S, OBJITEM_DEFAULT_INTERVAL_S);
    if (b->slow_interval_s <= initial)
        return cur;
    uint64_t next = (uint64_t)cur * (uint64_t)b->decay_x10 / 10u;
    if (next <= cur)
        next = (uint64_t)cur + 1; // guarantee forward progress
    if (next > b->slow_interval_s)
        next = b->slow_interval_s;
    return (uint32_t)next;
}

// A change token over an element's user-editable fields (everything up to the
// runtime kill_left counter). Any edit changes it, which the scheduler uses to
// restart the decay ramp at the initial rate and transmit promptly - matching
// YAAC's "edits cause transmission to begin again at the initial rate". The
// struct is fully zeroed on load (memset in load_locked), so padding bytes are
// stable and don't cause spurious resets.
static uint32_t objitem_signature(const objitem_t *b) {
    const uint8_t *p = (const uint8_t *)b;
    size_t n = offsetof(objitem_t, kill_left);
    uint32_t h = 2166136261u; // FNV-1a
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

// Per-element next-due timestamps (monotonic seconds). 0 = due now, so every
// enabled element transmits once on the first pass after start.
static int64_t s_next_due[OBJITEM_COUNT] = { 0 };

// Per-element runtime decay/path state (transient; not persisted - a reboot or
// any edit restarts the decay ramp at the initial rate, like YAAC):
//   s_cur_interval - the live (possibly decayed) interval; 0 => re-seed from
//                    the element's initial repeat rate on next use.
//   s_path_rot     - proportional-pathing rotation index into the element's
//                    selected path presets.
//   s_sig          - last-seen change token (see objitem_signature); a change
//                    means the element was edited and its schedule is reset.
static uint32_t s_cur_interval[OBJITEM_COUNT] = { 0 };
static uint8_t s_path_rot[OBJITEM_COUNT] = { 0 };
static uint32_t s_sig[OBJITEM_COUNT] = { 0 };

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

    int64_t now = sched_mono_seconds();
    int64_t soonest = now + OBJITEM_POLL_CAP_S;
    bool dirty = false; // set true if any kill sequence advanced -> persist once

    for (int i = 0; i < OBJITEM_COUNT; i++) {
        objitem_t *b = &set.item[i];

        // Restart the schedule (and decay ramp) if the element was edited since
        // the last pass, so an edit transmits promptly at the initial rate.
        uint32_t sig = objitem_signature(b);
        if (sig != s_sig[i]) {
            s_sig[i] = sig;
            s_cur_interval[i] = 0; // re-seed from the initial rate below
            s_path_rot[i] = 0;
            s_next_due[i] = 0; // transmit on this pass
        }

        // An element is transmittable if enabled, named, has some destination,
        // and its scope isn't PRIVATE.
        bool has_dest = objitem_effective_rf(b) || objitem_effective_inet(b);
        bool sendable = b->enable && b->name[0] && has_dest;

        if (!sendable || !src[0]) {
            // Reset so re-enabling / naming / setting a callsign fires an
            // immediate transmit on the next pass instead of waiting a stale
            // timer, and so the decay ramp starts fresh.
            s_next_due[i] = 0;
            s_cur_interval[i] = 0;
            s_path_rot[i] = 0;
            continue;
        }

        // Seed the live interval from the element's initial repeat rate.
        if (s_cur_interval[i] == 0)
            s_cur_interval[i] = sched_clamp_interval(b->interval_s, OBJITEM_MIN_INTERVAL_S, OBJITEM_DEFAULT_INTERVAL_S);

        if (now >= s_next_due[i]) {
            // Resolve the proportional-path set and pick this cycle's path.
            char paths[OBJITEM_PATH_PRESETS][72];
            int np = objitem_paths(b, paths);
            const char *path = (np > 0) ? paths[s_path_rot[i] % np] : NULL;

            if (!b->active || b->kill_left > 0) {
                // Kill path: element is being retired. Force a kill report and
                // count it down. When the last kill report goes out, clear the
                // element's enable flag so it leaves the air and shows disabled
                // in the UI (mirrors bulletins' expiry auto-disable).
                if (b->kill_left == 0)
                    b->kill_left = OBJITEM_KILL_REPEATS; // first kill pass arms the repeat count
                tx_one(i, b, src, false /* kill report */, path);
                b->kill_left--;
                if (b->kill_left == 0) {
                    b->enable = false;
                    ESP_LOGI(TAG, "%s %d kill complete - disabled", b->is_item ? "Item" : "Object", i + 1);
                }
                dirty = true;
            } else {
                // Normal live report.
                tx_one(i, b, src, true, path);
            }

            // Advance proportional pathing; apply one decay step after each
            // full cycle through the selected paths (YAAC semantics). With one
            // or no path, every transmission is itself a full cycle.
            if (np > 1) {
                s_path_rot[i] = (uint8_t)((s_path_rot[i] + 1) % np);
                if (s_path_rot[i] == 0)
                    s_cur_interval[i] = objitem_decay_step(s_cur_interval[i], b);
            } else {
                s_cur_interval[i] = objitem_decay_step(s_cur_interval[i], b);
            }

            s_next_due[i] = now + (int64_t)s_cur_interval[i];
            vTaskDelay(pdMS_TO_TICKS(OBJITEM_INTER_TX_MS));
            now = sched_mono_seconds(); // account for the inter-TX gap
        }

        if (s_next_due[i] < soonest)
            soonest = s_next_due[i];
    }

    if (dirty)
        objitems_save(&set);

    ESP_LOGD(TAG, "objitems_service stack free: %u bytes", (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));

    int64_t sleep_s = soonest - sched_mono_seconds();
    if (sleep_s < 1)
        sleep_s = 1;
    if (sleep_s > OBJITEM_POLL_CAP_S)
        sleep_s = OBJITEM_POLL_CAP_S;
    return (uint32_t)sleep_s;
}

void objitems_start(void) {
    // The transmitter is driven by the shared beacon scheduler
    // (beacon_scheduler_start()) via objitems_service(), so there is no task to
    // create here - only the LittleFS lock to bring up.
    json_store_lock_ensure(&s_lock);
    ESP_LOGI(TAG, "Objects/Items configured (per-element interval, default=%us; driven by beacon scheduler)", (unsigned)OBJITEM_DEFAULT_INTERVAL_S);
}
