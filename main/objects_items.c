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

    // Pin the stdio buffer before the first write. Otherwise newlib sizes it
    // from st_blksize (esp_littlefs reports the 4096-byte flash block), so the
    // first fputs() triggers a transient malloc(4096) that, on this device's
    // small/fragmented heap, intermittently crashed the save through the
    // unbuffered per-byte path (__swbuf_r) and a corrupted-length memset. See
    // the fuller note in app_config_save(). Static + reused safely: every save
    // runs under this module's lock() held across save_locked().
    static char s_save_buf[512];
    setvbuf(f, s_save_buf, _IOFBF, sizeof(s_save_buf));

    // Written token-by-token straight to the file (no cJSON tree, no second
    // serialized buffer) - the same low-RAM approach app_config_save() and
    // bulletins_save() use.
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
        fprintf(f, ",\"atype\":%u", (unsigned)b->area_type);
        fprintf(f, ",\"acol\":%u", (unsigned)b->area_color);
        fprintf(f, ",\"alat\":%.4f", (double)b->area_lat_off);
        fprintf(f, ",\"alon\":%.4f", (double)b->area_lon_off);
        fputs(",\"sign\":", f);
        write_json_string(f, sign);
        fprintf(f, ",\"freq\":%.4f", (double)b->freq_mhz);
        fprintf(f, ",\"ofs\":%u", (unsigned)b->offset_khz);
        fprintf(f, ",\"dup\":%d", (int)b->duplex);
        fprintf(f, ",\"tone\":%u", (unsigned)b->tone_tenths);
        fprintf(f, ",\"pmask\":%u", (unsigned)b->path_mask);
        fputs(",\"qru\":", f);
        write_json_string(f, qru);
        fprintf(f, ",\"int_s\":%u", (unsigned)b->interval_s);
        fprintf(f, ",\"slow_s\":%u", (unsigned)b->slow_interval_s);
        fprintf(f, ",\"decay\":%u", (unsigned)b->decay_x10);
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

// Builds the APRS Object or Item info field for one element into `out`.
//
// `live` overrides b->active for the transmit-time live/kill decision (so the
// kill sequence can force a kill report even while the stored element is still
// nominally "active" pending the user's next edit). `out` should be >= 160 to
// hold the frequency block plus a full comment.
static void objitem_build_info_field(const objitem_t *b, bool live, char *out, size_t out_size) {
    char latStr[10], lonStr[11];
    lat_lon_to_aprs(b->lat, b->lon, latStr, sizeof(latStr), lonStr, sizeof(lonStr));

    char sym_table = b->sym[0] ? b->sym[0] : '/';
    char sym_code = b->sym[1] ? b->sym[1] : '-';

    // The 7-byte data-extension slot right after the symbol code. Which
    // descriptor goes here depends on the symbol:
    //   Area symbol   ("\l") -> "Tyy/Cxx" area descriptor (YAAC Area).
    //   Signpost      ("\m") -> "{TEXT}"  signpost text (YAAC Signpost).
    //   anything else        -> CSE/SPD, only when speed > 0 (as before; YAAC:
    //                           "if the speed is set to zero, speed and course
    //                           will not be included").
    // Sized for the longest of these ("{TEXT}" / "NNN/NNN" / "Tyy/Cxx").
    char ext[16];
    ext[0] = 0;
    if (objitem_is_area(b)) {
        unsigned t = b->area_type > 9 ? 9 : b->area_type;
        unsigned color = b->area_color > 15 ? 15 : b->area_color;
        // Colours 0..9 use "/C"; 10..15 replace the '/' with '1' and C = C-10.
        char sep = color <= 9 ? '/' : '1';
        unsigned cdig = color <= 9 ? color : color - 10;
        snprintf(ext, sizeof(ext), "%u%02u%c%u%02u", t, area_offset_code(b->area_lat_off), sep, cdig, area_offset_code(b->area_lon_off));
    } else if (objitem_is_signpost(b)) {
        char sp[OBJITEM_SIGNPOST_MAX + 1];
        clamp_str(sp, b->signpost, OBJITEM_SIGNPOST_MAX);
        snprintf(ext, sizeof(ext), "{%s}", sp);
    } else if (b->speed > 0) {
        unsigned crs = (unsigned)(b->course % 360);       // 0..359
        unsigned spd = b->speed > 999 ? 999u : b->speed;  // APRS speed field is 3 digits
        snprintf(ext, sizeof(ext), "%03u/%03u", crs, spd);
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
        snprintf(out, out_size, ")%s%c%s%c%s%c%s%s", name, live ? '!' : '_', latStr, sym_table, lonStr, sym_code, ext, text);
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

        snprintf(out, out_size, ";%s%c%s%s%c%s%c%s%s", name9, live ? '*' : '_', ts, latStr, sym_table, lonStr, sym_code, ext, text);
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
        char packet[256];
        int len;
        if (path && path[0])
            len = snprintf(packet, sizeof(packet), "%s>%s,%s:%s", src, OBJITEM_DEST, path, info);
        else
            len = snprintf(packet, sizeof(packet), "%s>%s:%s", src, OBJITEM_DEST, info);
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
        char packet[256];
        int len = snprintf(packet, sizeof(packet), "%s>%s,TCPIP*:%s", src, OBJITEM_DEST, info);
        if (len > 0 && len < (int)sizeof(packet)) {
            if (igate_send_raw(packet, (size_t)len))
                ESP_LOGI(TAG, "%s %d TX (INET, %s): %s", kind, idx + 1, state, packet);
            else
                ESP_LOGW(TAG, "%s %d NOT sent over INET - APRS-IS not connected yet", kind, idx + 1);
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
static uint32_t clamp_interval(uint32_t interval); // defined below
static uint32_t objitem_decay_step(uint32_t cur, const objitem_t *b) {
    if (b->decay_x10 < 10 || b->slow_interval_s == 0)
        return cur;
    uint32_t initial = clamp_interval(b->interval_s);
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

    int64_t now = mono_seconds();
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
            s_cur_interval[i] = clamp_interval(b->interval_s);

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
