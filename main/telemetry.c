// @file telemetry.c
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
// @brief Own-station APRS Telemetry subsystem: resolves the operator's
// Binary (digital B1-B8) channel mapping (telemetry_config_t.tlm_bit_channel[],
// Telemetry page "Binary" section) from the sensors_local registry once per
// second, and encodes/transmits a "T#..." Telemetry Data Report at
// data_interval, plus PARM/UNIT/BITS metadata at info_interval.
//
// Configuration is stored in its own LittleFS file (/storage/telemetry.json),
// NOT in g_config/config.json - see the persistence section below and
// telemetry.h for the rationale (same pattern bulletins.c uses).

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_config.h"
#include "aprs_path.h" // aprs_path_build_suffix_from_config()
#include "aprs_service.h"
#include "beacon_scheduler.h" // beacon_scheduler_jitter()
#include "igate.h"
#include "json_escape.h" // json_write_escaped()
#include "json_store.h"  // shared JSON-file store scaffolding
#include "sched_time.h"  // sched_mono_seconds() / sched_clamp_interval()
#include "sensors_local.h"
#include "storage.h" // storage_write_lock() / storage_generation()
#include "telemetry.h"
#include "weather_telemetry.h"

static const char *TAG = "telemetry";

// Same software-identifier destination call used by beacon.c / weather.c,
// for consistency across the firmware.
#define TLM_DEST APRS_TOCALL

#define TLM_MIN_INTERVAL_S      30  // sanity floor for data_interval
#define TLM_DEFAULT_INTERVAL_S  600 // used when data_interval == 0
#define TLM_INFO_MIN_INTERVAL_S 60  // sanity floor for info_interval

#define TELEMETRY_PATH     "/storage/telemetry.json"
#define TELEMETRY_TMP_PATH "/storage/telemetry.json.tmp"

// Resolved-this-cycle state for both banks, refreshed at 1 Hz by
// telemetry_refresh_now() from the sensors_local registry channels the
// operator picked on the Telemetry page ("Source" columns).
static bool s_bit_val[TLM_BIT_NUM];
static bool s_bit_present[TLM_BIT_NUM]; // channel mapped ("(none)" excluded) this cycle
static double s_ana_val[TLM_CH];        // last resolved RAW analog reading (pre-EQNS; see build_tlm_data_packet())
static bool s_ana_present[TLM_CH];      // channel mapped and resolved this cycle

// "T#..." Telemetry Data Report sequence number, shared with the base-91
// comment telemetry group (telemetry_build_comment_tlm()) so both forms
// always advance the same count together.
static uint32_t s_sequence = 0;

// In-RAM copy of just the callsign, kept in sync on every load/save so
// telemetry_get_mycall() (called from aprs_service.c's inet_line_is_own_report(),
// once per APRS-IS line received - potentially many per second on a busy
// IGate) never has to hit LittleFS, and can answer without copying the whole
// configuration structure the way telemetry_config_load() does.
static char s_mycall_cache[10];

static SemaphoreHandle_t s_lock;

// Serializes LittleFS load/save of telemetry.json between the web save
// handler and the beacon-scheduler service call, and guards s_bit_val[]/
// s_bit_present[] - same dual role app_config.c's per-module locks split
// into two mutexes; here one is enough since neither critical section is
// held across the other.
static void telemetry_lock(void) {
    json_store_lock_take(&s_lock);
}

static void telemetry_unlock(void) {
    json_store_lock_give(&s_lock);
}

// -------------------------------------------------------------------------
// Persistence: /storage/telemetry.json (own file, not g_config/config.json)
// -------------------------------------------------------------------------

void telemetry_config_set_defaults(telemetry_config_t *out) {
    memset(out, 0, sizeof(*out));
    out->data_interval = 600;
    out->info_interval = 3600;

    // Report Parameters / definition-message defaults (match the Telemetry
    // page's out-of-the-box selections).
    strncpy(out->tocall, "APRS", sizeof(out->tocall) - 1);
    out->auto_seq = true;
    out->field_width = 0; // 0 = minimal/as-needed
    out->analog_count = TLM_CH;
    out->digital_count = TLM_BIT_NUM;
    out->gen_parm = true;
    out->gen_unit = true;
    out->gen_eqns = true;
    out->gen_bits = true;

    for (int i = 0; i < TLM_CH; i++) {
        out->ana_enable[i] = true;
        out->tlm_ana_channel[i] = SENSOR_LOCAL_CH_NONE; // "(none)" - unassigned until mapped on the web page
        out->ana_b[i] = 1.0f;                           // identity slope by default
        out->ana_raw_max[i] = 1023;
        out->ana_dec[i] = 0;
    }
    for (int i = 0; i < TLM_BIT_NUM; i++) {
        out->tlm_bit_channel[i] = SENSOR_LOCAL_CH_NONE; // "(none)" - unassigned until mapped on the web page
        // Default enabled/Normal so that loading an OLDER telemetry.json (which
        // has none of these keys) leaves every bit transmitting exactly as it
        // did before these fields existed - i.e. no silent behaviour change.
        out->bit_enable[i] = true;
        out->bit_sense[i] = true; // Normal
    }
}

// Sensor mappings are stored as driver NAMES, not registry positions: a
// position only holds within the firmware image that produced it (see the
// persistence contract in sensors_local.h), so an index would re-point every
// bit and analog channel at a different sensor as soon as a driver is enabled
// or disabled in Kconfig. Resolve the name against the registry this image
// actually built, and report a mapping whose driver is gone as unmapped
// instead of aiming it at whatever now occupies that slot. A plain number is a
// telemetry.json that predates name-based mappings and is still read as a
// position, valid only while the registry reaches that far.
static uint8_t channel_from_json(const cJSON *it, const char *what, int idx, uint8_t def) {
    if (cJSON_IsString(it)) {
        uint8_t ch = sensors_local_channel_from_name(it->valuestring);
        if (ch == SENSOR_LOCAL_CH_NONE && it->valuestring != NULL && it->valuestring[0] != 0)
            ESP_LOGW(TAG, "%s%d: sensor '%s' is not registered, left unmapped", what, idx, it->valuestring);
        return ch;
    }
    if (cJSON_IsNumber(it)) {
        unsigned pos = (unsigned)it->valuedouble;
        if (pos != SENSOR_LOCAL_CH_NONE && pos >= sensors_local_count()) {
            ESP_LOGW(TAG, "%s%d: stored sensor channel %u no longer exists, left unmapped", what, idx, pos);
            return SENSOR_LOCAL_CH_NONE;
        }
        return (uint8_t)pos;
    }
    return def;
}

static bool load_locked(telemetry_config_t *out, bool *out_missing) {
    telemetry_config_set_defaults(out);
    if (out_missing)
        *out_missing = false;

    cJSON *doc = NULL;
    json_store_status_t st = json_store_read(TELEMETRY_PATH, TAG, "telemetry configuration", &doc);
    if (st != JSON_STORE_OK) {
        // An absent file and a zero-length one are both reported as "missing"
        // so the caller writes a valid default out over either; a corrupt file
        // is left alone for the operator to look at, and *out stays at the
        // defaults set above.
        if (out_missing && (st == JSON_STORE_MISSING || st == JSON_STORE_EMPTY))
            *out_missing = true;
        return false;
    }

    cJSON *v;
    v = cJSON_GetObjectItemCaseSensitive(doc, "en");
    out->en = cJSON_IsTrue(v);
    v = cJSON_GetObjectItemCaseSensitive(doc, "tx2rf");
    out->tx2rf = cJSON_IsTrue(v);
    v = cJSON_GetObjectItemCaseSensitive(doc, "tx2inet");
    out->tx2inet = cJSON_IsTrue(v);
    v = cJSON_GetObjectItemCaseSensitive(doc, "ssid");
    if (cJSON_IsNumber(v))
        out->ssid = (uint8_t)v->valuedouble;
    v = cJSON_GetObjectItemCaseSensitive(doc, "mycall");
    if (cJSON_IsString(v) && v->valuestring) {
        strncpy(out->mycall, v->valuestring, sizeof(out->mycall) - 1);
        out->mycall[sizeof(out->mycall) - 1] = 0;
    }
    v = cJSON_GetObjectItemCaseSensitive(doc, "path");
    if (cJSON_IsNumber(v))
        out->path = (uint8_t)v->valuedouble;
    v = cJSON_GetObjectItemCaseSensitive(doc, "dataInv");
    if (cJSON_IsNumber(v))
        out->data_interval = (uint16_t)v->valuedouble;
    v = cJSON_GetObjectItemCaseSensitive(doc, "infoInv");
    if (cJSON_IsNumber(v))
        out->info_interval = (uint16_t)v->valuedouble;

    cJSON *parm = cJSON_GetObjectItemCaseSensitive(doc, "PARM");
    cJSON *unit = cJSON_GetObjectItemCaseSensitive(doc, "UNIT");
    for (int i = 0; i < TLM_PARM_NUM; i++) {
        cJSON *it;
        if (parm && (it = cJSON_GetArrayItem(parm, i)) && cJSON_IsString(it)) {
            strncpy(out->PARM[i], it->valuestring, sizeof(out->PARM[i]) - 1);
            out->PARM[i][sizeof(out->PARM[i]) - 1] = 0;
        }
        if (unit && (it = cJSON_GetArrayItem(unit, i)) && cJSON_IsString(it)) {
            strncpy(out->UNIT[i], it->valuestring, sizeof(out->UNIT[i]) - 1);
            out->UNIT[i][sizeof(out->UNIT[i]) - 1] = 0;
        }
    }

    cJSON *name = cJSON_GetObjectItemCaseSensitive(doc, "bitName");
    cJSON *chan = cJSON_GetObjectItemCaseSensitive(doc, "bitCh");
    cJSON *igate = cJSON_GetObjectItemCaseSensitive(doc, "bitIgate");
    cJSON *rf = cJSON_GetObjectItemCaseSensitive(doc, "bitRF");
    for (int i = 0; i < TLM_BIT_NUM; i++) {
        cJSON *it;
        if (name && (it = cJSON_GetArrayItem(name, i)) && cJSON_IsString(it)) {
            strncpy(out->tlm_bit_name[i], it->valuestring, sizeof(out->tlm_bit_name[i]) - 1);
            out->tlm_bit_name[i][sizeof(out->tlm_bit_name[i]) - 1] = 0;
        }
        if (chan && (it = cJSON_GetArrayItem(chan, i)))
            out->tlm_bit_channel[i] = channel_from_json(it, "B", i + 1, out->tlm_bit_channel[i]);
        if (igate && (it = cJSON_GetArrayItem(igate, i)))
            out->tlm_bit_igate[i] = cJSON_IsTrue(it);
        if (rf && (it = cJSON_GetArrayItem(rf, i)))
            out->tlm_bit_rf[i] = cJSON_IsTrue(it);
    }

    // ---- Report Parameters / definition-message toggles ----
    // Every field below is optional: a telemetry.json written before these
    // existed simply leaves the default from telemetry_config_set_defaults()
    // in place (that is why load starts by calling it).
    v = cJSON_GetObjectItemCaseSensitive(doc, "useStation");
    if (v)
        out->use_station = cJSON_IsTrue(v);
    v = cJSON_GetObjectItemCaseSensitive(doc, "rptPath");
    if (cJSON_IsString(v) && v->valuestring) {
        strncpy(out->report_path, v->valuestring, sizeof(out->report_path) - 1);
        out->report_path[sizeof(out->report_path) - 1] = 0;
    }
    v = cJSON_GetObjectItemCaseSensitive(doc, "tocall");
    if (cJSON_IsString(v) && v->valuestring) {
        strncpy(out->tocall, v->valuestring, sizeof(out->tocall) - 1);
        out->tocall[sizeof(out->tocall) - 1] = 0;
    }
    v = cJSON_GetObjectItemCaseSensitive(doc, "autoSeq");
    if (v)
        out->auto_seq = cJSON_IsTrue(v);
    v = cJSON_GetObjectItemCaseSensitive(doc, "fieldW");
    if (cJSON_IsNumber(v))
        out->field_width = (uint8_t)v->valuedouble;
    v = cJSON_GetObjectItemCaseSensitive(doc, "omitTrail");
    if (v)
        out->omit_trailing = cJSON_IsTrue(v);
    v = cJSON_GetObjectItemCaseSensitive(doc, "trailCmt");
    if (cJSON_IsString(v) && v->valuestring) {
        strncpy(out->trail_comment, v->valuestring, sizeof(out->trail_comment) - 1);
        out->trail_comment[sizeof(out->trail_comment) - 1] = 0;
    }
    v = cJSON_GetObjectItemCaseSensitive(doc, "anaCount");
    if (cJSON_IsNumber(v))
        out->analog_count = (uint8_t)v->valuedouble;
    v = cJSON_GetObjectItemCaseSensitive(doc, "digCount");
    if (cJSON_IsNumber(v))
        out->digital_count = (uint8_t)v->valuedouble;
    v = cJSON_GetObjectItemCaseSensitive(doc, "genPARM");
    if (v)
        out->gen_parm = cJSON_IsTrue(v);
    v = cJSON_GetObjectItemCaseSensitive(doc, "genUNIT");
    if (v)
        out->gen_unit = cJSON_IsTrue(v);
    v = cJSON_GetObjectItemCaseSensitive(doc, "genEQNS");
    if (v)
        out->gen_eqns = cJSON_IsTrue(v);
    v = cJSON_GetObjectItemCaseSensitive(doc, "genBITS");
    if (v)
        out->gen_bits = cJSON_IsTrue(v);

    v = cJSON_GetObjectItemCaseSensitive(doc, "anaRF");
    if (v)
        out->analog_tx2rf = cJSON_IsTrue(v);
    v = cJSON_GetObjectItemCaseSensitive(doc, "anaInet");
    if (v)
        out->analog_tx2inet = cJSON_IsTrue(v);
    v = cJSON_GetObjectItemCaseSensitive(doc, "digRF");
    if (v)
        out->digital_tx2rf = cJSON_IsTrue(v);
    v = cJSON_GetObjectItemCaseSensitive(doc, "digInet");
    if (v)
        out->digital_tx2inet = cJSON_IsTrue(v);
    v = cJSON_GetObjectItemCaseSensitive(doc, "projTitle");
    if (cJSON_IsString(v) && v->valuestring) {
        strncpy(out->proj_title, v->valuestring, sizeof(out->proj_title) - 1);
        out->proj_title[sizeof(out->proj_title) - 1] = 0;
    }
    v = cJSON_GetObjectItemCaseSensitive(doc, "cmtTlm");
    if (v)
        out->comment_telemetry = cJSON_IsTrue(v);

    // ---- Analog channels A1-A5 (parallel arrays) ----
    cJSON *anaEn = cJSON_GetObjectItemCaseSensitive(doc, "anaEn");
    cJSON *anaCh = cJSON_GetObjectItemCaseSensitive(doc, "anaCh");
    cJSON *anaA = cJSON_GetObjectItemCaseSensitive(doc, "anaA");
    cJSON *anaB = cJSON_GetObjectItemCaseSensitive(doc, "anaB");
    cJSON *anaC = cJSON_GetObjectItemCaseSensitive(doc, "anaC");
    cJSON *anaMin = cJSON_GetObjectItemCaseSensitive(doc, "anaRawMin");
    cJSON *anaMax = cJSON_GetObjectItemCaseSensitive(doc, "anaRawMax");
    cJSON *anaDec = cJSON_GetObjectItemCaseSensitive(doc, "anaDec");
    for (int i = 0; i < TLM_CH; i++) {
        cJSON *it;
        if (anaEn && (it = cJSON_GetArrayItem(anaEn, i)))
            out->ana_enable[i] = cJSON_IsTrue(it);
        if (anaCh && (it = cJSON_GetArrayItem(anaCh, i)))
            out->tlm_ana_channel[i] = channel_from_json(it, "A", i + 1, out->tlm_ana_channel[i]);
        if (anaA && (it = cJSON_GetArrayItem(anaA, i)) && cJSON_IsNumber(it))
            out->ana_a[i] = (float)it->valuedouble;
        if (anaB && (it = cJSON_GetArrayItem(anaB, i)) && cJSON_IsNumber(it))
            out->ana_b[i] = (float)it->valuedouble;
        if (anaC && (it = cJSON_GetArrayItem(anaC, i)) && cJSON_IsNumber(it))
            out->ana_c[i] = (float)it->valuedouble;
        if (anaMin && (it = cJSON_GetArrayItem(anaMin, i)) && cJSON_IsNumber(it))
            out->ana_raw_min[i] = (int32_t)it->valuedouble;
        if (anaMax && (it = cJSON_GetArrayItem(anaMax, i)) && cJSON_IsNumber(it))
            out->ana_raw_max[i] = (int32_t)it->valuedouble;
        if (anaDec && (it = cJSON_GetArrayItem(anaDec, i)) && cJSON_IsNumber(it))
            out->ana_dec[i] = (uint8_t)it->valuedouble;
    }

    // ---- Digital bank extras (parallel arrays) ----
    cJSON *bitEn = cJSON_GetObjectItemCaseSensitive(doc, "bitEn");
    cJSON *bitSense = cJSON_GetObjectItemCaseSensitive(doc, "bitSense");
    for (int i = 0; i < TLM_BIT_NUM; i++) {
        cJSON *it;
        if (bitEn && (it = cJSON_GetArrayItem(bitEn, i)))
            out->bit_enable[i] = cJSON_IsTrue(it);
        if (bitSense && (it = cJSON_GetArrayItem(bitSense, i)))
            out->bit_sense[i] = cJSON_IsTrue(it);
    }

    cJSON_Delete(doc);
    return true;
}

static bool save_locked(const telemetry_config_t *in) {
    // Entered with s_lock held (see telemetry_config_save() below), which is
    // what json_store_open_tmp() asserts before handing back a stream whose
    // stdio buffer is already pinned.
    FILE *f = json_store_open_tmp(TELEMETRY_TMP_PATH, TAG, s_lock);
    if (!f)
        return false;

    // Written token-by-token straight to the file: no cJSON tree and no second
    // serialized buffer ever exist, so a save costs essentially only littlefs's
    // own write buffer on top of the stream buffer above.
    fputc('{', f);
    fprintf(f, "\"en\":%s,", in->en ? "true" : "false");
    fprintf(f, "\"tx2rf\":%s,", in->tx2rf ? "true" : "false");
    fprintf(f, "\"tx2inet\":%s,", in->tx2inet ? "true" : "false");
    fprintf(f, "\"ssid\":%u,", (unsigned)in->ssid);
    fputs("\"mycall\":", f);
    json_write_escaped(f, in->mycall);
    fprintf(f, ",\"path\":%u,", (unsigned)in->path);
    fprintf(f, "\"dataInv\":%u,", (unsigned)in->data_interval);
    fprintf(f, "\"infoInv\":%u,", (unsigned)in->info_interval);

    fputs("\"PARM\":[", f);
    for (int i = 0; i < TLM_PARM_NUM; i++) {
        if (i)
            fputc(',', f);
        json_write_escaped(f, in->PARM[i]);
    }
    fputs("],\"UNIT\":[", f);
    for (int i = 0; i < TLM_PARM_NUM; i++) {
        if (i)
            fputc(',', f);
        json_write_escaped(f, in->UNIT[i]);
    }
    fputs("],", f);

    fputs("\"bitName\":[", f);
    for (int i = 0; i < TLM_BIT_NUM; i++) {
        if (i)
            fputc(',', f);
        json_write_escaped(f, in->tlm_bit_name[i]);
    }
    fputs("],\"bitCh\":[", f);
    for (int i = 0; i < TLM_BIT_NUM; i++) {
        if (i)
            fputc(',', f);
        json_write_escaped(f, sensors_local_channel_name(in->tlm_bit_channel[i]));
    }
    fputs("],\"bitIgate\":[", f);
    for (int i = 0; i < TLM_BIT_NUM; i++)
        fputs(i ? (in->tlm_bit_igate[i] ? ",true" : ",false") : (in->tlm_bit_igate[i] ? "true" : "false"), f);
    fputs("],\"bitRF\":[", f);
    for (int i = 0; i < TLM_BIT_NUM; i++)
        fputs(i ? (in->tlm_bit_rf[i] ? ",true" : ",false") : (in->tlm_bit_rf[i] ? "true" : "false"), f);
    fputs("],", f);

    // ---- Report Parameters / definition-message toggles ----
    fprintf(f, "\"useStation\":%s,", in->use_station ? "true" : "false");
    fputs("\"rptPath\":", f);
    json_write_escaped(f, in->report_path);
    fputs(",\"tocall\":", f);
    json_write_escaped(f, in->tocall);
    fprintf(f, ",\"autoSeq\":%s,", in->auto_seq ? "true" : "false");
    fprintf(f, "\"fieldW\":%u,", (unsigned)in->field_width);
    fprintf(f, "\"omitTrail\":%s,", in->omit_trailing ? "true" : "false");
    fputs("\"trailCmt\":", f);
    json_write_escaped(f, in->trail_comment);
    fprintf(f, ",\"anaCount\":%u,", (unsigned)in->analog_count);
    fprintf(f, "\"digCount\":%u,", (unsigned)in->digital_count);
    fprintf(f, "\"genPARM\":%s,", in->gen_parm ? "true" : "false");
    fprintf(f, "\"genUNIT\":%s,", in->gen_unit ? "true" : "false");
    fprintf(f, "\"genEQNS\":%s,", in->gen_eqns ? "true" : "false");
    fprintf(f, "\"genBITS\":%s,", in->gen_bits ? "true" : "false");

    fprintf(f, "\"anaRF\":%s,", in->analog_tx2rf ? "true" : "false");
    fprintf(f, "\"anaInet\":%s,", in->analog_tx2inet ? "true" : "false");
    fprintf(f, "\"digRF\":%s,", in->digital_tx2rf ? "true" : "false");
    fprintf(f, "\"digInet\":%s,", in->digital_tx2inet ? "true" : "false");
    fputs("\"projTitle\":", f);
    json_write_escaped(f, in->proj_title);
    fputc(',', f);
    fprintf(f, "\"cmtTlm\":%s,", in->comment_telemetry ? "true" : "false");

    // ---- Analog channels A1-A5 (parallel arrays) ----
    fputs("\"anaEn\":[", f);
    for (int i = 0; i < TLM_CH; i++)
        fputs(i ? (in->ana_enable[i] ? ",true" : ",false") : (in->ana_enable[i] ? "true" : "false"), f);
    fputs("],\"anaCh\":[", f);
    for (int i = 0; i < TLM_CH; i++) {
        if (i)
            fputc(',', f);
        json_write_escaped(f, sensors_local_channel_name(in->tlm_ana_channel[i]));
    }
    fputs("],\"anaA\":[", f);
    for (int i = 0; i < TLM_CH; i++)
        fprintf(f, i ? ",%g" : "%g", (double)in->ana_a[i]);
    fputs("],\"anaB\":[", f);
    for (int i = 0; i < TLM_CH; i++)
        fprintf(f, i ? ",%g" : "%g", (double)in->ana_b[i]);
    fputs("],\"anaC\":[", f);
    for (int i = 0; i < TLM_CH; i++)
        fprintf(f, i ? ",%g" : "%g", (double)in->ana_c[i]);
    fputs("],\"anaRawMin\":[", f);
    for (int i = 0; i < TLM_CH; i++)
        fprintf(f, i ? ",%d" : "%d", (int)in->ana_raw_min[i]);
    fputs("],\"anaRawMax\":[", f);
    for (int i = 0; i < TLM_CH; i++)
        fprintf(f, i ? ",%d" : "%d", (int)in->ana_raw_max[i]);
    fputs("],\"anaDec\":[", f);
    for (int i = 0; i < TLM_CH; i++)
        fprintf(f, i ? ",%u" : "%u", (unsigned)in->ana_dec[i]);
    fputs("],", f);

    // ---- Digital bank extras (parallel arrays) ----
    fputs("\"bitEn\":[", f);
    for (int i = 0; i < TLM_BIT_NUM; i++)
        fputs(i ? (in->bit_enable[i] ? ",true" : ",false") : (in->bit_enable[i] ? "true" : "false"), f);
    fputs("],\"bitSense\":[", f);
    for (int i = 0; i < TLM_BIT_NUM; i++)
        fputs(i ? (in->bit_sense[i] ? ",true" : ",false") : (in->bit_sense[i] ? "true" : "false"), f);
    fputs("]}", f);

    return json_store_commit(f, TELEMETRY_TMP_PATH, TELEMETRY_PATH, TAG, "telemetry configuration");
}

// Refresh the RAM-only mycall cache. Caller must hold s_lock.
static void update_mycall_cache_locked(const char *mycall) {
    strncpy(s_mycall_cache, mycall ? mycall : "", sizeof(s_mycall_cache) - 1);
    s_mycall_cache[sizeof(s_mycall_cache) - 1] = 0;
}

// Parsed copy of telemetry.json, kept in RAM so a scheduler pass costs nothing
// on the filesystem. telemetry_beacon_service() runs on every pass of the
// shared beacon scheduler - as often as every 5 s while the subsystem is
// disabled - and each pass called telemetry_config_load(), i.e. an fopen +
// fread + a full cJSON_Parse of this file, on the order of ten thousand times
// a day. That parse builds and tears down a tree of small heap nodes, the
// exact allocation pattern the streaming writers exist to avoid;
// holding the result costs ~sizeof(telemetry_config_t) of static RAM once and
// removes the churn. It is the same reasoning s_mycall_cache above already
// applies to the callsign, extended to the whole structure.
//
// The cache is only allowed to answer when nothing can have changed underneath
// it: telemetry_config_save() drops it (every web edit goes through there),
// and s_cfg_cache_gen catches the changes made from outside this module - a
// whole-partition format, a delete, or a file uploaded over this one from the
// web Storage page (see storage_generation()).
static telemetry_config_t s_cfg_cache;
static bool s_cfg_cache_valid = false;
static bool s_cfg_cache_ok = false; // what load_locked() reported for the cached content
static uint32_t s_cfg_cache_gen = 0;

bool telemetry_config_load(telemetry_config_t *out) {
    if (!out)
        return false;
    telemetry_lock();
    if (s_cfg_cache_valid && s_cfg_cache_gen == storage_generation()) {
        *out = s_cfg_cache;
        bool cached_ok = s_cfg_cache_ok;
        telemetry_unlock();
        return cached_ok;
    }
    bool missing = false;
    bool ok = load_locked(out, &missing);
    update_mycall_cache_locked(out->mycall);
    // Cache the defaults substituted for a missing/corrupt file too: they are
    // what every caller would get from a re-read anyway, and doing so keeps a
    // subsystem that is simply not configured from re-reading the filesystem
    // on every scheduler pass.
    s_cfg_cache = *out;
    s_cfg_cache_ok = ok;
    s_cfg_cache_gen = storage_generation();
    s_cfg_cache_valid = true;
    telemetry_unlock();
    if (missing) {
        // First boot / file lost: persist the default set now so
        // /storage/telemetry.json exists on disk instead of only living in
        // RAM until something else happens to trigger a save.
        if (!telemetry_config_save(out))
            ESP_LOGW(TAG, "Failed to write default %s", TELEMETRY_PATH);
    }
    return ok;
}

bool telemetry_config_save(const telemetry_config_t *in) {
    if (!in)
        return false;
    telemetry_lock();
    // Module lock first, filesystem-wide writer gate second (storage.h): the
    // temp-file + rename sequence inside save_locked() must not overlap the
    // whole-partition format the web Storage page can start.
    storage_write_lock();
    bool ok = save_locked(in);
    storage_write_unlock();
    if (ok)
        update_mycall_cache_locked(in->mycall);
    // Drop the cache rather than filling it from *in: what lands on disk is
    // what a later load will parse back, so the next reader re-reads the file
    // once and caches exactly what the file says.
    s_cfg_cache_valid = false;
    telemetry_unlock();
    return ok;
}

void telemetry_get_mycall(char *out, size_t out_size) {
    if (!out || out_size == 0)
        return;
    telemetry_lock();
    strncpy(out, s_mycall_cache, out_size - 1);
    out[out_size - 1] = 0;
    telemetry_unlock();
}

// -------------------------------------------------------------------------
// 1 Hz refresh: for EACH Binary bit independently, read the one local
// driver the operator picked in cfg.tlm_bit_channel[bit] (Telemetry page
// "Binary" section, "Channel" column) and copy only that bit's value.
// Mirrors weather.c's per-field resolution against wx_sensor_ch[], and for
// the same reason: with more than one telemetry-capable driver registered,
// a single aggregate sensors_local_save() call would let whichever driver
// runs last silently overwrite bits already resolved from a different,
// operator-selected driver.
//
// Uses the channel mapping copied on the last telemetry_beacon_service()
// pass (s_cached_bit_channel[]) rather than calling telemetry_config_load()
// here: this runs at 1 Hz off the APRS service tick, and the mapping only
// changes when the operator saves the web page, so a snapshot of just the two
// channel arrays is all this path needs - the scheduler-driven beacon service
// refreshes it on every pass.
// -------------------------------------------------------------------------
static uint8_t s_cached_bit_channel[TLM_BIT_NUM];
static uint8_t s_cached_ana_channel[TLM_CH];
static bool s_cache_valid = false;

static void telemetry_refresh_now(void) {
    telemetry_lock();

    if (!s_cache_valid) {
        telemetry_unlock();
        return; // no mapping known yet (first tick before the scheduler's first pass)
    }

    uint8_t ch_snapshot[TLM_BIT_NUM];
    memcpy(ch_snapshot, s_cached_bit_channel, sizeof(ch_snapshot));
    uint8_t ana_snapshot[TLM_CH];
    memcpy(ana_snapshot, s_cached_ana_channel, sizeof(ana_snapshot));
    telemetry_unlock();

    for (int bit = 0; bit < TLM_BIT_NUM; bit++) {
        bool present = false;
        bool val = false;

        uint8_t ch = ch_snapshot[bit];
        if (ch != SENSOR_LOCAL_CH_NONE) { // "(none)" - no source channel picked
            bool digital_enabled[APRS_TELEMETRY_DIGITAL_CHANNELS] = { 0 };
            bool digital[APRS_TELEMETRY_DIGITAL_CHANNELS] = { 0 };
            aprs_telemetry_report_t scratch_tlm = { 0 };
            scratch_tlm.digital_count = APRS_TELEMETRY_DIGITAL_CHANNELS;
            scratch_tlm.digital_enabled = digital_enabled;
            scratch_tlm.digital = digital;

            weather_telemetry_data_t scratch_data = { 0 };
            scratch_data.telemetry_report = &scratch_tlm;
            scratch_data.telemetry_report_qty = 1;

            // bit indexes B1..B8 the same way it indexes cfg.tlm_bit_*; the
            // selected driver's digital[] array uses the identical B1..B8
            // layout (sensor_local_properties_has_tlm()/_tlm_label() on the
            // Telemetry page's Channel picker enforce that a channel only
            // appears as a choice for a bit row it actually produces).
            if (sensors_local_save_one((size_t)ch, &scratch_data, SENSOR_LOCAL_DATA_TELEMETRY) == ESP_OK && digital_enabled[bit]) {
                val = digital[bit];
                present = true;
            }
        }

        telemetry_lock();
        s_bit_val[bit] = val;
        s_bit_present[bit] = present;
        telemetry_unlock();
    }

    // Analog A1-A5: same per-row resolution as the digital loop above, and
    // for the same reason (telemetry_refresh_now() note) - each row reads
    // only the ONE driver channel the operator picked for THAT row, mirroring
    // page_tlm_values_get()'s live-preview read. The value cached here is the
    // RAW sensor reading (before the a*x^2+b*x+c calibration): APRS101 defines
    // the on-air "T#..." analog fields as raw transmitted values, with the
    // EQNS. metadata message carrying the coefficients so any receiving
    // station can recover the engineering value itself - see
    // build_tlm_data_packet()/build_tlm_eqns_packet() below.
    for (int a = 0; a < TLM_CH; a++) {
        bool present = false;
        double val = 0.0;

        uint8_t ch = ana_snapshot[a];
        if (ch != SENSOR_LOCAL_CH_NONE) { // "(none)" - no source channel picked
            bool analog_enabled[APRS_TELEMETRY_ANALOG_CHANNELS] = { 0 };
            double analog[APRS_TELEMETRY_ANALOG_CHANNELS] = { 0 };
            aprs_telemetry_report_t scratch_tlm = { 0 };
            scratch_tlm.analog_count = APRS_TELEMETRY_ANALOG_CHANNELS;
            scratch_tlm.analog_enabled = analog_enabled;
            scratch_tlm.analog = analog;

            weather_telemetry_data_t scratch_data = { 0 };
            scratch_data.telemetry_report = &scratch_tlm;
            scratch_data.telemetry_report_qty = 1;

            // Row a (A1..A5) maps 1:1 onto analog[0..4] - same reasoning as
            // page_tlm_values_get(): the "Source" <select> for this row only
            // ever offers drivers whose properties advertise THIS analog slot.
            if (sensors_local_save_one((size_t)ch, &scratch_data, SENSOR_LOCAL_DATA_TELEMETRY) == ESP_OK && analog_enabled[a]) {
                val = analog[a];
                present = true;
            }
        }

        telemetry_lock();
        s_ana_val[a] = val;
        s_ana_present[a] = present;
        telemetry_unlock();
    }
}

// -------------------------------------------------------------------------
// Encoding helpers
// -------------------------------------------------------------------------

static void call_field(const telemetry_config_t *s, char *out, size_t outMax) {
    if (s->ssid > 0)
        snprintf(out, outMax, "%s-%d", s->mycall, (int)s->ssid);
    else
        snprintf(out, outMax, "%s", s->mycall);
}

// Destination address (TOCALL): honors the operator-configurable "Destination"
// field from the Report Parameters section (cfg->tocall), falling back to the
// software-identifier TLM_DEST only if the operator left the field empty.
static void tlm_dest_field(const telemetry_config_t *s, char *out, size_t outMax) {
    snprintf(out, outMax, "%s", s->tocall[0] ? s->tocall : TLM_DEST);
}

// Digipeater path: prefers the free-text "Path (digipeaters)" picker from the
// Report Parameters section (cfg->report_path, e.g. "WIDE1-1,WIDE2-1" - a
// literal alias value copied from g_config.path[]). Falls back to the
// Beacon-section path bitmask (aprs_path_build_suffix_from_config()) when
// report_path is empty,
// so configurations that only set the bitmask keep working.
static void tlm_path_field(const telemetry_config_t *s, char *out, size_t outMax) {
    if (s->report_path[0]) {
        int n = snprintf(out, outMax, ",%s", s->report_path);
        if (n < 0 || (size_t)n >= outMax)
            out[0] = 0;
        return;
    }
    aprs_path_build_suffix_from_config(s->path, out, outMax);
}

// Clamps a raw analog reading to the 0-8280 window a two-character base-91
// pair can hold (91*91 - 1) and rounds it to the nearest integer. Shared by
// the base-91 comment-telemetry encoder below: base-91 comment telemetry has
// no metadata channel of its own to carry an out-of-range indication, unlike
// field_width == 3's "T#..." form, so an out-of-range raw reading is clamped
// rather than wrapped, the same reasoning format_analog_field() applies to
// the 3-digit "T#..." form.
static long clamp_analog_raw_base91(double raw) {
    long v = lround(raw);
    if (v < 0)
        v = 0;
    if (v > 91 * 91 - 1)
        v = 91 * 91 - 1;
    return v;
}

// Encodes one non-negative value below 91*91 as a two-character base-91 pair
// (APRS 1.2 comment telemetry, aprs12/spec.txt): each digit is the value's
// base-91 digit plus the ASCII code for '!' (33), most significant digit
// first. @p out must hold at least 2 bytes; a terminating NUL is not
// written, so callers append it (or the next pair) themselves.
static void encode_base91_pair(long v, char *out) {
    out[0] = (char)('!' + (v / 91));
    out[1] = (char)('!' + (v % 91));
}

// Formats one analog channel's RAW transmitted value per the "Analog Field
// Width" Report Parameters option:
//   - field_width == 3 : 3-digit zero-padded encoding, unsigned integer,
//     000-999 per APRS 1.2 (out-of-range raw readings are clamped rather
//     than silently wrapped, so a mis-set raw_min/max never produces an
//     on-air value a receiver would reject). An operator whose receiver only
//     understands the original APRS101 000-255 window can restore it
//     explicitly with that channel's ana_raw_min/ana_raw_max.
//   - field_width == 0 (or anything else) : community/extended ("Kenneth's
//     Proposed") format - plain decimal number with ana_dec[i] fractional
//     digits, no padding, negative values allowed.
// Returns the formatted length, or 0 (empty field) if @p present is false -
// an empty field is valid on-air (APRS101 Ch.13: unused channels are simply
// blank between the commas).
static int format_analog_field(const telemetry_config_t *s, int i, double raw, bool present, char *out, size_t outMax) {
    if (!present || outMax == 0) {
        if (outMax)
            out[0] = 0;
        return 0;
    }
    if (s->field_width == 3) {
        long v = lround(raw);
        if (v < 0)
            v = 0;
        if (v > 999)
            v = 999;
        return snprintf(out, outMax, "%03ld", v);
    }
    uint8_t dec = s->ana_dec[i];
    if (dec > 6)
        dec = 6; // sanity bound - keeps the field short and the buffer maths simple
    return snprintf(out, outMax, "%.*f", (int)dec, raw);
}

// Builds one "T#sss,a1,a2,a3,a4,a5,bbbbbbbb" Telemetry Data Report (APRS101
// Ch.13). `for_rf` selects RF vs INET so both the analog bank, the digital
// bank, and each individual bit can be routed to one leg, both, or neither
// (cfg.analog_tx2rf/analog_tx2inet, cfg.digital_tx2rf/digital_tx2inet,
// cfg.tlm_bit_igate[]/tlm_bit_rf[]).
//
// Analog fields carry the RAW resolved sensor reading (see the comment on
// the analog resolution loop in telemetry_refresh_now()); EQNS. metadata
// (build_tlm_eqns_packet()) carries the a/b/c coefficients a receiver needs
// to recover the engineering value - this is the standard APRS101 split
// between report and metadata, and matches how field_width's "3-digit"
// strict mode expects values in 0-255 raw units.
//
// Trailing fields (rightmost analog channels with no value, and/or the
// whole digital byte) are omitted together with their separating comma
// when "Omit Trailing Channels" (cfg->omit_trailing) is enabled, per the
// APRS101 Ch.13 shorthand ("only trailing channels may be omitted"); the
// digital byte is additionally cropped to cfg->digital_count characters
// before that trim is applied. cfg->trail_comment, if set, is appended
// verbatim after the last field with no separator (conventional free-text
// telemetry comment).
//
// IMPORTANT (on-air naming): this packet intentionally never includes
// tlm_bit_name[]/PARM[]/UNIT[] or any other channel label - per APRS101
// Ch.13 the "T#..." report carries only the sequence number and values.
// Names/units/equations/sense are only ever sent from the PARM/UNIT/EQNS/
// BITS builders below, inside separate Message packets, at the slower
// info_interval cadence.
static int build_tlm_data_packet(const telemetry_config_t *s, uint32_t seq, bool for_rf, char *out, size_t outMax) {
    if (!s->mycall[0])
        return 0;

    char callField[16];
    call_field(s, callField, sizeof(callField));
    char dest[8];
    tlm_dest_field(s, dest, sizeof(dest));
    char path[80];
    tlm_path_field(s, path, sizeof(path));

    bool ana_bank_on = for_rf ? s->analog_tx2rf : s->analog_tx2inet;
    bool dig_bank_on = for_rf ? s->digital_tx2rf : s->digital_tx2inet;

    uint8_t ana_count = s->analog_count > TLM_CH ? TLM_CH : s->analog_count;
    uint8_t dig_count = s->digital_count > TLM_BIT_NUM ? TLM_BIT_NUM : s->digital_count;

    char anaField[TLM_CH][20]; // generous headroom: "-" + up to 6 int digits + "." + up to 6 dec digits still fits well under 20
    bool anaPresent[TLM_CH];
    char bits[TLM_BIT_NUM + 1];

    telemetry_lock();
    for (int i = 0; i < TLM_CH; i++) {
        bool present = ana_bank_on && (i < ana_count) && s->ana_enable[i] && s_ana_present[i];
        anaPresent[i] = present;
        format_analog_field(s, i, present ? s_ana_val[i] : 0.0, present, anaField[i], sizeof(anaField[i]));
    }
    for (int i = 0; i < dig_count; i++) {
        bool routed = for_rf ? s->tlm_bit_rf[i] : s->tlm_bit_igate[i];
        bool present = dig_bank_on && s->bit_enable[i] && routed && s_bit_present[i];
        bits[i] = (present && s_bit_val[i]) ? '1' : '0';
    }
    telemetry_unlock();
    bits[dig_count] = '\0';

    // How many of the 6 fields (5 analog + 1 digital) to actually emit.
    //
    // The digital field is emitted only when the bank is routed to this leg
    // and has at least one channel (haveDigital below); a disabled or
    // zero-length bank contributes nothing at all, rather than a run of '0's.
    // That is deliberate: an all-'0' bits field is indistinguishable on air
    // from eight real bits that all read low, so a station whose digital bank
    // is simply switched off would otherwise keep reporting eight false
    // readings to anyone plotting them.
    //
    // The analog fields are emitted in full, with an empty field still holding
    // its comma placeholder so channel N always stays in position N. Only
    // omit_trailing trims them, and only off the right-hand end: consecutive
    // not-present analog fields are dropped until a present one is reached.
    // The leading decrement below is what lets that trim reach the analog
    // fields at all, by first giving up the (already unused) digital slot.
    int fieldsToEmit = ana_count + 1; // + 1 for the digital field slot
    bool haveDigital = dig_bank_on && dig_count > 0;
    if (s->omit_trailing) {
        if (!haveDigital)
            fieldsToEmit--;
        while (fieldsToEmit > 0 && fieldsToEmit <= ana_count && !anaPresent[fieldsToEmit - 1])
            fieldsToEmit--;
    }

    char fields[TLM_CH * 20 + TLM_BIT_NUM + 8] = "";
    size_t u = 0;
    for (int i = 0; i < ana_count && i < fieldsToEmit; i++) {
        int n = snprintf(fields + u, sizeof(fields) - u, ",%s", anaField[i]);
        if (n < 0 || (size_t)n >= sizeof(fields) - u)
            break;
        u += (size_t)n;
    }
    if (haveDigital && fieldsToEmit > ana_count)
        snprintf(fields + u, sizeof(fields) - u, ",%s", bits);

    char info[TLM_CH * 20 + TLM_BIT_NUM + 32 + sizeof(s->trail_comment)];
    if (snprintf(info, sizeof(info), "T#%03u%s%s", (unsigned)(seq % 1000u), fields, s->trail_comment) < 0)
        return 0;

    int n = snprintf(out, outMax, "%s>%s%s:%s", callField, dest, path, info);
    if (n < 0)
        return 0;
    if (outMax > 0 && (size_t)n >= outMax)
        n = (int)outMax - 1;
    return n;
}

size_t telemetry_build_comment_tlm(char *out, size_t out_max) {
    if (!out || out_max == 0)
        return 0;
    out[0] = 0;

    telemetry_config_t cfg;
    telemetry_config_load(&cfg);
    if (!cfg.en || !cfg.comment_telemetry || !cfg.mycall[0])
        return 0;

    uint8_t ana_count = cfg.analog_count > TLM_CH ? TLM_CH : cfg.analog_count;

    // Same seq/value snapshot build_tlm_data_packet() takes, so the comment
    // form can never disagree with the concurrent "T#..." report: the
    // sequence number is base-91 encoded whole (0-8280 window, matching the
    // "T#..." field's own 0-999 modulo window in spirit but with more
    // headroom), and each enabled, currently resolved analog channel is
    // base-91 encoded from the identical raw reading.
    //
    // The APRS 1.2 comment-telemetry group is positional: the n-th value pair
    // after the sequence pair is channel An, with no per-pair channel
    // identifier and no placeholder-comma shorthand like the "T#..." report
    // has. A pair may therefore be emitted for An only once pairs have been
    // emitted for A1..An-1: the encoder walks the channels in order and stops
    // at the first one that is disabled or not currently resolved, so the
    // group always carries an unambiguous, contiguous prefix of channels
    // rather than a gap that would shift every later channel one slot left.
    telemetry_lock();
    long seqVal = (long)(s_sequence % (91u * 91u));
    long anaVal[TLM_CH];
    bool anaPresent[TLM_CH];
    for (int i = 0; i < TLM_CH; i++) {
        anaPresent[i] = (i < ana_count) && cfg.ana_enable[i] && s_ana_present[i];
        anaVal[i] = anaPresent[i] ? clamp_analog_raw_base91(s_ana_val[i]) : 0;
    }
    telemetry_unlock();

    char group[1 + 2 + TLM_CH * 2 + 1 + 1]; // '|' + seq pair + up to 5 value pairs + '|' + NUL
    size_t u = 0;
    group[u++] = '|';
    encode_base91_pair(seqVal, group + u);
    u += 2;
    int anaEmitted = 0;
    for (int i = 0; i < TLM_CH; i++) {
        if (!anaPresent[i])
            break;
        encode_base91_pair(anaVal[i], group + u);
        u += 2;
        anaEmitted++;
    }
    if (anaEmitted < ana_count)
        ESP_LOGD(TAG, "comment telemetry group truncated at channel A%d (not enabled or not resolved)", anaEmitted + 1);
    group[u++] = '|';
    group[u] = 0;

    if (u >= out_max) {
        ESP_LOGW(TAG, "comment telemetry group (%u bytes) does not fit the remaining comment room (%u) - dropped", (unsigned)u, (unsigned)out_max);
        return 0;
    }
    memcpy(out, group, u + 1);
    return u;
}

// Shared "addressee" builder for the four telemetry metadata Messages
// (PARM/UNIT/EQNS/BITS): left-justified, space-padded to 9 chars, as
// required by APRS101 Ch.13/14 for the Message addressee field.
static void tlm_addressee(const telemetry_config_t *s, const char *callField, char *out, size_t outMax) {
    snprintf(out, outMax, "%-9.9s", callField);
}

// Builds the ":addressee:PARM.name1,...,name13" Parameter Name Message
// (APRS101 Ch.13): the 5 analog channel names (cfg->PARM[0..4]) followed by
// the 8 digital bit labels (cfg->tlm_bit_name[0..7], stored at
// PARM[TLM_CH..TLM_PARM_NUM-1] per telemetry.h's on-air PARM/UNIT layout).
// Only gen_parm-gated; only analog_count/digital_count channels are
// considered "active" (channels beyond those counts are sent as empty
// fields so the trailing-comma trim below can still drop them).
static int build_tlm_parm_packet(const telemetry_config_t *s, char *out, size_t outMax) {
    if (!s->mycall[0] || !s->gen_parm)
        return 0;

    char callField[16];
    call_field(s, callField, sizeof(callField));
    char dest[8];
    tlm_dest_field(s, dest, sizeof(dest));
    char addressee[APRS_CALLSIGN_SSID_LEN + 1];
    tlm_addressee(s, callField, addressee, sizeof(addressee));

    uint8_t ana_count = s->analog_count > TLM_CH ? TLM_CH : s->analog_count;
    uint8_t dig_count = s->digital_count > TLM_BIT_NUM ? TLM_BIT_NUM : s->digital_count;

    char body[16 + TLM_PARM_NUM * 11];
    size_t u = 0;
    int n = snprintf(body, sizeof(body), "PARM.");
    if (n > 0)
        u = (size_t)n;
    for (int i = 0; i < TLM_PARM_NUM; i++) {
        const char *name = "";
        if (i < ana_count)
            name = s->PARM[i];
        else if (i >= TLM_CH && (i - TLM_CH) < dig_count)
            name = s->tlm_bit_name[i - TLM_CH];
        n = snprintf(body + u, sizeof(body) - u, "%.10s,", name);
        if (n < 0 || (size_t)n >= sizeof(body) - u)
            break;
        u += (size_t)n;
    }
    while (u > 0 && body[u - 1] == ',')
        body[--u] = '\0';

    int len = snprintf(out, outMax, "%s>%s::%s:%s", callField, dest, addressee, body);
    if (len < 0)
        return 0;
    if (outMax > 0 && (size_t)len >= outMax)
        len = (int)outMax - 1;
    return len;
}

// Builds the ":addressee:UNIT.unit1,...,unit13" Unit/Label Message (APRS101
// Ch.13): the 5 analog channel units (cfg->UNIT[0..4]) followed by the 8
// digital ON-state labels (cfg->UNIT[TLM_CH..TLM_PARM_NUM-1]). gen_unit-gated.
static int build_tlm_unit_packet(const telemetry_config_t *s, char *out, size_t outMax) {
    if (!s->mycall[0] || !s->gen_unit)
        return 0;

    char callField[16];
    call_field(s, callField, sizeof(callField));
    char dest[8];
    tlm_dest_field(s, dest, sizeof(dest));
    char addressee[APRS_CALLSIGN_SSID_LEN + 1];
    tlm_addressee(s, callField, addressee, sizeof(addressee));

    uint8_t ana_count = s->analog_count > TLM_CH ? TLM_CH : s->analog_count;
    uint8_t dig_count = s->digital_count > TLM_BIT_NUM ? TLM_BIT_NUM : s->digital_count;

    char body[16 + TLM_PARM_NUM * 9];
    size_t u = 0;
    int n = snprintf(body, sizeof(body), "UNIT.");
    if (n > 0)
        u = (size_t)n;
    for (int i = 0; i < TLM_PARM_NUM; i++) {
        const char *unit = "";
        if (i < ana_count || (i >= TLM_CH && (i - TLM_CH) < dig_count))
            unit = s->UNIT[i];
        n = snprintf(body + u, sizeof(body) - u, "%.8s,", unit);
        if (n < 0 || (size_t)n >= sizeof(body) - u)
            break;
        u += (size_t)n;
    }
    while (u > 0 && body[u - 1] == ',')
        body[--u] = '\0';

    int len = snprintf(out, outMax, "%s>%s::%s:%s", callField, dest, addressee, body);
    if (len < 0)
        return 0;
    if (outMax > 0 && (size_t)len >= outMax)
        len = (int)outMax - 1;
    return len;
}

// Builds the ":addressee:EQNS.a1,b1,c1,...,a5,b5,c5" Equation Coefficients
// Message (APRS101 Ch.13): one {a,b,c} triplet per analog channel
// (cfg->ana_a/ana_b/ana_c[0..analog_count-1]), so a receiving station can
// recover the engineering value from the raw "T#..." field via
// value = a*raw^2 + b*raw + c. gen_eqns-gated.
static int build_tlm_eqns_packet(const telemetry_config_t *s, char *out, size_t outMax) {
    if (!s->mycall[0] || !s->gen_eqns)
        return 0;

    char callField[16];
    call_field(s, callField, sizeof(callField));
    char dest[8];
    tlm_dest_field(s, dest, sizeof(dest));
    char addressee[APRS_CALLSIGN_SSID_LEN + 1];
    tlm_addressee(s, callField, addressee, sizeof(addressee));

    uint8_t ana_count = s->analog_count > TLM_CH ? TLM_CH : s->analog_count;

    char body[16 + TLM_CH * 3 * 16];
    size_t u = 0;
    int n = snprintf(body, sizeof(body), "EQNS.");
    if (n > 0)
        u = (size_t)n;
    for (int i = 0; i < ana_count; i++) {
        n = snprintf(body + u, sizeof(body) - u, "%s%g,%g,%g", i ? "," : "", (double)s->ana_a[i], (double)s->ana_b[i], (double)s->ana_c[i]);
        if (n < 0 || (size_t)n >= sizeof(body) - u)
            break;
        u += (size_t)n;
    }

    int len = snprintf(out, outMax, "%s>%s::%s:%s", callField, dest, addressee, body);
    if (len < 0)
        return 0;
    if (outMax > 0 && (size_t)len >= outMax)
        len = (int)outMax - 1;
    return len;
}

// Builds the ":addressee:BITS.b1b2...b8,Project Title" Bit Sense / Project
// Name Message (APRS101 Ch.13). Per spec the 8 characters immediately after
// "BITS." are '1'/'0' SENSE flags - one per digital channel, true meaning
// "a transmitted 1 on this bit represents the labeled condition being true"
// (cfg->bit_sense[i]) - NOT the bit labels (those belong in PARM., built by
// build_tlm_parm_packet() above; the previous implementation of this
// function sent tlm_bit_name[] here instead of sense flags, and never sent
// the project title at all - both fixed here). gen_bits-gated.
static int build_tlm_bits_packet(const telemetry_config_t *s, char *out, size_t outMax) {
    if (!s->mycall[0] || !s->gen_bits)
        return 0;

    char callField[16];
    call_field(s, callField, sizeof(callField));
    char dest[8];
    tlm_dest_field(s, dest, sizeof(dest));
    char addressee[APRS_CALLSIGN_SSID_LEN + 1];
    tlm_addressee(s, callField, addressee, sizeof(addressee));

    char sense[TLM_BIT_NUM + 1];
    for (int i = 0; i < TLM_BIT_NUM; i++)
        sense[i] = s->bit_sense[i] ? '1' : '0';
    sense[TLM_BIT_NUM] = '\0';

    char body[8 + TLM_BIT_NUM + 1 + sizeof(s->proj_title)];
    snprintf(body, sizeof(body), "BITS.%s,%s", sense, s->proj_title);

    int len = snprintf(out, outMax, "%s>%s::%s:%s", callField, dest, addressee, body);
    if (len < 0)
        return 0;
    if (outMax > 0 && (size_t)len >= outMax)
        len = (int)outMax - 1;
    return len;
}

// Metadata (PARM/UNIT/EQNS/BITS) interval. Not sched_clamp_interval(): here 0
// is a meaningful setting rather than "unset", because switching the metadata
// reports off while the data reports keep running is a configuration an
// operator legitimately wants.
static uint32_t clamp_info_interval(uint32_t s) {
    if (s == 0)
        return 0; // 0 = metadata sending disabled (data report still sent)
    if (s < TLM_INFO_MIN_INTERVAL_S)
        return TLM_INFO_MIN_INTERVAL_S;
    return s;
}

// -------------------------------------------------------------------------
// Tasks / scheduler-service entry points
// -------------------------------------------------------------------------

static int64_t s_data_next_due = 0;
static int64_t s_info_next_due = 0;

void telemetry_service_1hz(void) {
    telemetry_refresh_now();
}

static void send_packet(const char *label, bool to_rf_leg, bool to_inet_leg, const char *packet, size_t len) {
    if (len == 0)
        return;
    if (to_rf_leg) {
        if (aprs_service_send_tnc2(packet, len))
            ESP_LOGI(TAG, "%s TX (RF): %s", label, packet);
        else
            ESP_LOGW(TAG, "%s NOT sent over RF - modem not ready or busy: %s", label, packet);
    }
    if (to_inet_leg) {
        if (igate_send_raw(packet, len))
            ESP_LOGI(TAG, "%s TX (INET): %s", label, packet);
        else
            ESP_LOGW(TAG, "%s NOT sent over INET - APRS-IS not connected yet: %s", label, packet);
    }
}

uint32_t telemetry_beacon_service(void) {
    telemetry_config_t cfg;
    telemetry_config_load(&cfg);

    // Refresh the 1 Hz refresh's channel-mapping cache from what was just
    // loaded, so telemetry_refresh_now() picks up mapping changes without
    // itself touching LittleFS every second (see the comment above it).
    telemetry_lock();
    memcpy(s_cached_bit_channel, cfg.tlm_bit_channel, sizeof(s_cached_bit_channel));
    memcpy(s_cached_ana_channel, cfg.tlm_ana_channel, sizeof(s_cached_ana_channel));
    s_cache_valid = true;
    telemetry_unlock();

    if (!cfg.en || (!cfg.tx2rf && !cfg.tx2inet)) {
        s_data_next_due = 0; // reset so (re-)enabling fires an immediate TX
        s_info_next_due = 0;
        return 5; // idle re-check cadence
    }

    int64_t now = sched_mono_seconds();

    if (now >= s_data_next_due) {
        // Build one packet per leg: the Binary section routes each bit to
        // IGate and/or RF independently (tlm_bit_igate[]/tlm_bit_rf[]), so
        // the on-air bit pattern can legitimately differ between the two;
        // the Analog/Digital "Beacon via RF/Internet" toggles further gate
        // each whole bank per leg (build_tlm_data_packet()).
        char packet[280];
        if (cfg.tx2rf) {
            int len = build_tlm_data_packet(&cfg, s_sequence, true, packet, sizeof(packet));
            if (len > 0)
                send_packet("TLM data", true, false, packet, (size_t)len);
            else
                ESP_LOGW(TAG, "Telemetry enabled but no callsign configured - skipping RF leg");
        }
        if (cfg.tx2inet) {
            int len = build_tlm_data_packet(&cfg, s_sequence, false, packet, sizeof(packet));
            if (len > 0)
                send_packet("TLM data", false, true, packet, (size_t)len);
            else
                ESP_LOGW(TAG, "Telemetry enabled but no callsign configured - skipping INET leg");
        }
        // "Auto-increment Sequence Number" (cfg.auto_seq): when off, keep
        // resending the same sequence number - some receiving software uses
        // a change in T# sequence to detect a genuinely new sample, so a
        // fixed sequence is the correct behaviour for a station that wants
        // every report treated as a refresh of the same reading rather than
        // a new one.
        if (cfg.auto_seq)
            s_sequence++;

        ESP_LOGD(TAG, "tlm_beacon stack free: %u bytes", (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));

        s_data_next_due = now + (int64_t)beacon_scheduler_jitter(sched_clamp_interval(cfg.data_interval, TLM_MIN_INTERVAL_S, TLM_DEFAULT_INTERVAL_S));
    }

    uint32_t info_iv = clamp_info_interval(cfg.info_interval);
    if (info_iv > 0 && now >= s_info_next_due) {
        // Definition Messages: each of PARM./UNIT./EQNS./BITS. is sent as
        // its own Message packet, independently gated by its "Generate ..."
        // checkbox (cfg.gen_parm/gen_unit/gen_eqns/gen_bits).
        char packet[280];
        int len = build_tlm_parm_packet(&cfg, packet, sizeof(packet));
        if (len > 0)
            send_packet("TLM PARM", cfg.tx2rf, cfg.tx2inet, packet, (size_t)len);
        len = build_tlm_unit_packet(&cfg, packet, sizeof(packet));
        if (len > 0)
            send_packet("TLM UNIT", cfg.tx2rf, cfg.tx2inet, packet, (size_t)len);
        len = build_tlm_eqns_packet(&cfg, packet, sizeof(packet));
        if (len > 0)
            send_packet("TLM EQNS", cfg.tx2rf, cfg.tx2inet, packet, (size_t)len);
        len = build_tlm_bits_packet(&cfg, packet, sizeof(packet));
        if (len > 0)
            send_packet("TLM BITS", cfg.tx2rf, cfg.tx2inet, packet, (size_t)len);
        s_info_next_due = now + (int64_t)info_iv;
    } else if (info_iv == 0) {
        s_info_next_due = now; // keep re-checking cheaply in case the operator sets an interval later
    }

    int64_t next[2];
    next[0] = s_data_next_due - now;
    next[1] = (info_iv > 0) ? (s_info_next_due - now) : (int64_t)TLM_MIN_INTERVAL_S;
    int64_t rem = (next[0] < next[1]) ? next[0] : next[1];
    if (rem < 1)
        rem = 1;
    return (uint32_t)rem;
}

void telemetry_start(void) {
    memset(s_bit_val, 0, sizeof(s_bit_val));
    memset(s_bit_present, 0, sizeof(s_bit_present));
    memset(s_ana_val, 0, sizeof(s_ana_val));
    memset(s_ana_present, 0, sizeof(s_ana_present));
    memset(s_cached_bit_channel, SENSOR_LOCAL_CH_NONE, sizeof(s_cached_bit_channel));
    memset(s_cached_ana_channel, SENSOR_LOCAL_CH_NONE, sizeof(s_cached_ana_channel));
    s_cache_valid = false;
    s_sequence = 0;
    s_data_next_due = 0;
    s_info_next_due = 0;

    json_store_lock_ensure(&s_lock);

    // Make sure /storage/telemetry.json exists from the very first boot,
    // same as bulletins_start() ensures for bulletins.json - the config page
    // would otherwise only create it the first time someone saves it.
    telemetry_config_t cfg;
    telemetry_config_load(&cfg);

    // No sensors_local_init()/sensors_local_init_all() call here: weather_start()
    // (called before this, from aprs_service.c) already brought the shared
    // registry up. This subsystem only reads it.
    ESP_LOGI(TAG, "Telemetry subsystem started (en=%d rf=%d inet=%d data_interval=%us info_interval=%us)", cfg.en, cfg.tx2rf, cfg.tx2inet,
             (unsigned)cfg.data_interval, (unsigned)cfg.info_interval);
}
