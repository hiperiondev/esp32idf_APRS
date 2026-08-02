// @file app_config.c
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
// @brief Persistent application configuration: defaults, load/save of
// /storage/config.json on LittleFS (via cJSON) and the global g_config instance
// shared by every component and web admin page.

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h> // strtod() - shortest round-tripping precision for the JSON number writer
#include <string.h>

#include "app_config.h"
// RF_TX_BUFFERS_MIN/MAX, RF_PREAMBLE_MS_MIN/MAX, RF_TX_TIMESLOT_MS_MAX,
// PTT_MIN_UNKEY_MS_MAX, CSMA_PERSIST_MIN: the same bounds the Radiomodem form
// enforces, so what is loaded from flash and what is saved from the web admin
// can never accept different values.
#include "aprs_service.h"
#include "cJSON.h"
// MODEM_PTT_ACTIVE_HIGH: the board's PTT polarity, #ifndef-guarded in that
// header and overridden from the top-level CMakeLists.txt. Both it and
// MODEM_PTT_GPIO (the PTT pin itself) are fixed compile-time constants with
// no g_config counterpart - see aprs_service_build_modem_config().
#include "esp32idf_radioamateur_modem_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "json_escape.h"   // json_write_escaped()
#include "json_store.h"    // shared JSON-file store scaffolding
#include "sensors_local.h" // sensors_local_channel_name() / _from_name() - WX field mappings are stored by driver name, not registry index
#include "storage.h"       // storage_write_lock() - keeps a save from overlapping a whole-partition format

static const char *TAG = "app_config";
#define CONFIG_PATH     "/storage/config.json"
#define CONFIG_TMP_PATH "/storage/config.json.tmp"

app_config_t g_config;

// Serializes every save/load of the underlying config.json file. Without
// this, two overlapping POSTs (e.g. a user clicking Save twice quickly, or a
// page auto-refresh racing a save) could both end up inside
// app_config_save() at once, doing redundant work and each rewriting the
// live config from under the other. (Note: esp_littlefs already takes a
// per-instance lock around every VFS op, so overlapping saves cannot corrupt
// the filesystem's own metadata, and the stdio buffer allocation that a save
// would otherwise make on its first write is handled separately - see
// json_store_open_tmp(). This mutex is here because serializing saves is
// correct on its own terms: it avoids wasted flash writes and it is what lets
// a save reuse a single static stdio buffer.) Created lazily so this file has
// no init-order dependency.
static SemaphoreHandle_t s_config_mutex = NULL;

static SemaphoreHandle_t config_mutex(void) {
    if (!s_config_mutex) {
        static portMUX_TYPE creation_lock = portMUX_INITIALIZER_UNLOCKED;
        taskENTER_CRITICAL(&creation_lock);
        if (!s_config_mutex)
            s_config_mutex = xSemaphoreCreateMutex();
        taskEXIT_CRITICAL(&creation_lock);
    }
    return s_config_mutex;
}

// Short-held lock guarding concurrent access to the live g_config struct
// itself. Distinct from s_config_mutex above (which is held across the entire
// flash serialization in app_config_save() and would stall readers): this one
// is a strict LEAF lock, only ever held long enough to mutate/copy a few
// fields. See the app_config_lock() contract in app_config.h. Created lazily
// with the same one-time-init guard as config_mutex().
static SemaphoreHandle_t s_data_mutex = NULL;

static SemaphoreHandle_t data_mutex(void) {
    if (!s_data_mutex) {
        static portMUX_TYPE creation_lock = portMUX_INITIALIZER_UNLOCKED;
        taskENTER_CRITICAL(&creation_lock);
        if (!s_data_mutex)
            s_data_mutex = xSemaphoreCreateMutex();
        taskEXIT_CRITICAL(&creation_lock);
    }
    return s_data_mutex;
}

void app_config_lock(void) {
    xSemaphoreTake(data_mutex(), portMAX_DELAY);
}

void app_config_unlock(void) {
    xSemaphoreGive(data_mutex());
}

static void set_str(char *dst, size_t sz, const char *val) {
    if (!val) {
        dst[0] = 0;
        return;
    }
    strncpy(dst, val, sz - 1);
    dst[sz - 1] = 0;
}

void app_config_set_defaults(app_config_t *c) {
    memset(c, 0, sizeof(*c));

    c->timeZone = 0.0f;
    c->synctime = true;
    c->cpuFreq = 240;

    set_str(c->my_callsign, sizeof(c->my_callsign), "NOCALL");
    c->my_lat = 0.0f;
    c->my_lon = 0.0f;
    c->my_alt = 0.0f;

    c->my_phg_power = 1;
    c->my_phg_gain = 6.0f;
    c->my_phg_height = 10;
    c->my_phg_dir = 0;
    set_str(c->my_phg, sizeof(c->my_phg), "");

    c->pos_ambiguity = 0;
    c->status_grid_en = false;

    c->wifi_mode = 2; // AP_STA equivalent default (matches original shipping as AP)
    c->wifi_power = 20;
    for (int i = 0; i < WIFI_STA_NUM; i++) {
        c->wifi_sta[i].enable = false;
        set_str(c->wifi_sta[i].wifi_ssid, sizeof(c->wifi_sta[i].wifi_ssid), "WIFI_AP");
        set_str(c->wifi_sta[i].wifi_pass, sizeof(c->wifi_sta[i].wifi_pass), "");
    }
    c->wifi_ap_ch = WIFI_AP_CH_DEFAULT;
    set_str(c->wifi_ap_ssid, sizeof(c->wifi_ap_ssid), "esp32idf_APRS");
    set_str(c->wifi_ap_pass, sizeof(c->wifi_ap_pass), "esp32idf_APRS");

    // IGATE
    c->igate_en = true;
    c->rf2inet = true;
    c->inet2rf = false;
    c->igate_loc2rf = false;
    c->igate_loc2inet = true;
    c->aprs_ssid = 10;
    c->aprs_port = APRS_PORT_DEFAULT;
    set_str(c->aprs_mycall, sizeof(c->aprs_mycall), "NOCALL");
    set_str(c->aprs_passcode, sizeof(c->aprs_passcode), "-1");
    set_str(c->aprs_host, sizeof(c->aprs_host), "aprs.dprns.com");
    set_str(c->aprs_filter, sizeof(c->aprs_filter), "");
    c->igate_bcn = true;
    c->igate_lat = 0.000f;
    c->igate_lon = 0.000f;
    c->igate_alt = 0;
    c->igate_interval = 30;
    set_str(c->igate_symbol, sizeof(c->igate_symbol), "N&");
    c->igate_path = ACTIVATE_IGATE;
    set_str(c->igate_comment, sizeof(c->igate_comment), "esp32idf_APRS IGate");
    c->igate_sts_interval = 0;
    c->igate_compress = false;
    c->igate_phg_enable = false;
    c->igate_phg_use_station = false;
    c->igate_phg_power = 1;
    c->igate_phg_gain = 6.0f;
    c->igate_phg_height = 10;
    c->igate_phg_dir = 0;
    c->igate_ext_type = APRS_EXT_PHG;
    c->igate_range_miles = 0;
    c->igate_dfs_strength = 0;
    c->rf2inetFilter = IGATE_FILT_MESSAGE | IGATE_FILT_STATUS | IGATE_FILT_TELEMETRY | IGATE_FILT_WEATHER | IGATE_FILT_OBJECT | IGATE_FILT_ITEM |
                       IGATE_FILT_QUERY | IGATE_FILT_BUOY | IGATE_FILT_POSITION;
    c->inet2rfFilter = IGATE_FILT_MESSAGE;
    c->rf2inet_budlist_mode = BUDLIST_OFF;
    c->inet2rf_budlist_mode = BUDLIST_OFF;
    for (int i = 0; i < IGATE_BUDLIST_MAX; i++)
        set_str(c->budlist[i], sizeof(c->budlist[i]), "");

    // Satellite/ISS digipeater gate-call list: the same 6 calls this firmware
    // always used before this list became web-configurable. Remaining slots
    // (if IGATE_SATGATE_MAX ever grows past 6) default to empty/unused.
    {
        static const char *satGateDefaults[] = { "RS0ISS", "YBOX", "YBSAT", "PSAT", "W3ADO", "BJ1SI" };
        for (int i = 0; i < IGATE_SATGATE_MAX; i++)
            set_str(c->satgate[i], sizeof(c->satgate[i]), (i < (int)(sizeof(satGateDefaults) / sizeof(satGateDefaults[0]))) ? satGateDefaults[i] : "");
    }
    c->dup_cache_size = DUP_CACHE_SIZE_DEFAULT;
    c->dup_cache_timeout_ms = DUP_CACHE_TIMEOUT_MS_DEFAULT;

    c->rf2inet_range_en = false;
    c->rf2inet_range_km = 0.0f;
    c->rf2inet_prefix_en = false;
    set_str(c->rf2inet_prefixes, sizeof(c->rf2inet_prefixes), "");
    c->inet2rf_3rdparty_unwrap_en = false;

    // DIGI
    c->digi_en = false;
    c->digi_ssid = 1;
    set_str(c->digi_mycall, sizeof(c->digi_mycall), "NOCALL");
    c->digi_path = ACTIVATE_DIGI;
    c->digi_delay = 0;
    c->digi_bcn = true;
    c->digi_compress = false;
    c->digi_interval = 30;
    set_str(c->digi_symbol, sizeof(c->digi_symbol), "N&");
    set_str(c->digi_comment, sizeof(c->digi_comment), "esp32idf_APRS Digi");

    // TRACKER
    c->trk_en = false;
    c->trk_ssid = 9;
    set_str(c->trk_mycall, sizeof(c->trk_mycall), "NOCALL");
    c->trk_path = ACTIVATE_TRACKER;
    c->trk_interval = 60;
    c->trk_compress = false;
    c->trk_mice = false;
    set_str(c->trk_symbol, sizeof(c->trk_symbol), "\\>");
    set_str(c->trk_symmove, sizeof(c->trk_symmove), "/>");
    set_str(c->trk_symstop, sizeof(c->trk_symstop), "\\>");
    set_str(c->trk_comment, sizeof(c->trk_comment), "esp32idf_APRS Tracker");

    // WX
    c->wx_en = false;
    c->wx_ssid = 13;
    set_str(c->wx_mycall, sizeof(c->wx_mycall), "NOCALL");
    c->wx_path = ACTIVATE_WX;
    c->wx_interval = 300;
    set_str(c->wx_comment, sizeof(c->wx_comment), "ESP32APRS WX");
    // Enable the WX fields a typical station reports; the rest stay off until
    // the operator maps a sensor channel to them on the Weather page.
    //
    // wx_sensor_ch[] defaults to SENSOR_LOCAL_CH_NONE ("(none)" - see page_wx.c's
    // wx_channel_select()/wx_field_present() and weather.c's
    // weather_refresh_now(), which both treat it as "no source channel
    // picked"). Channel 0 is a REAL registry slot (the first registered
    // driver), not a placeholder, so defaulting to 0 here would wire the
    // fields below straight to whatever driver happened to enumerate first -
    // transmitting live sensor data on first boot even though the operator
    // never mapped a channel in Sensor Mapping. Enable/Averaged/Channel must
    // all be explicitly set by the operator before a field is ever sampled or
    // put on-air; only Channel needs the non-zero default to make that true,
    // since weather_refresh_now() already skips any field whose channel is
    // SENSOR_LOCAL_CH_NONE regardless of its Enable bit.
    for (int i = 0; i < WX_SENSOR_NUM; i++) {
        c->wx_sensor_enable[i] = false;
        c->wx_sensor_avg[i] = false;
        c->wx_sensor_ch[i] = SENSOR_LOCAL_CH_NONE;
    }
    c->wx_sensor_enable[WX_FIELD_WIND_DIRECTION] = true;
    c->wx_sensor_enable[WX_FIELD_WIND_SPEED] = true;
    c->wx_sensor_enable[WX_FIELD_WIND_GUST] = true;
    c->wx_sensor_enable[WX_FIELD_TEMPERATURE] = true;
    c->wx_sensor_enable[WX_FIELD_HUMIDITY] = true;
    c->wx_sensor_enable[WX_FIELD_PRESSURE] = true;

    // Telemetry defaults live in telemetry_config_set_defaults()
    // (telemetry.c); telemetry configuration is not part of g_config.

    // AFSK / TNC
    c->audio_modem_en = true;
    c->audio_lpf = true;
    c->preamble = 300;
    c->afsk_modem_type = 1; // default 1200 Bd (AFSK/Bell202) - standard APRS audio modem
    c->fx25_mode = 0;
    c->tx_timeslot = 2000;
    c->csma_persist = 63;    // ~25% transmit chance per clear slot, the standard AX.25/KISS Persist default
    c->rf_tx_buffers = 1;    // see RF_TX_BUFFERS_MIN/MAX in aprs_service.h
    c->ptt_min_unkey_ms = 0; // see PTT_MIN_UNKEY_MS_MIN/MAX in aprs_service.h
    set_str(c->ntp_host[0], sizeof(c->ntp_host[0]), "pool.ntp.org");
    set_str(c->ntp_host[1], sizeof(c->ntp_host[1]), "time.google.com");
    set_str(c->ntp_host[2], sizeof(c->ntp_host[2]), "time.cloudflare.com");
    c->ntp_resync_sec = 3600;

    // System / HTTP auth  (README documented default: admin/admin)
    set_str(c->http_username, sizeof(c->http_username), "admin");
    set_str(c->http_password, sizeof(c->http_password), "admin");
    set_str(c->host_name, sizeof(c->host_name), "esp32idf_APRS");
    c->reset_timeout = 0;
    for (int i = 0; i < 4; i++)
        set_str(c->path[i], sizeof(c->path[i]), "");
    set_str(c->path[0], sizeof(c->path[0]), "WIDE1-1,WIDE2-1");

    // Audio modem PTT.
    //
    // The modem component takes its ADC/DAC pins, PTT pin, PTT active level,
    // ADC attenuation and LED pins as compile-time constants (MODEM_* macros,
    // set in the top-level CMakeLists.txt). None of them are g_config fields
    // any more - see aprs_service_build_modem_config().

    // Message
    c->msg_enable = true;
    set_str(c->msg_mycall, sizeof(c->msg_mycall), "NOCALL");
    c->msg_path = 9;
    c->msg_rf = true;
    c->msg_inet = true;
    c->msg_retry = 3;
    c->msg_interval = 30;
    c->msg_alarm_enable = false; // disabled by default
    c->msg_alarm_gpio = -1;

    // Query responder
    c->query_en = false; // opt-in, like msg_enable
    c->query_rf = true;
    c->query_inet = false; // avoid answering into APRS-IS by default
    c->query_aprs_en = true;
    c->query_wx_en = true;
    c->query_igate_en = true;
    c->query_directed_en = true;
    c->query_ext_en = true;
    c->query_min_interval_sec = 30;
}

// ---- streaming JSON writer -----------------------------------------------
// The configuration is serialized by writing tokens straight to the open
// config file, one field at a time. Building a cJSON tree of the whole config
// in RAM and printing that tree into a second full-size string buffer would
// cost hundreds of tiny cJSON nodes (~40+ KB) plus a ~7 KB contiguous print
// buffer, all live at once - on this device's small, fragmentable heap the
// single largest memory event in the firmware, enough to drive the "minimum
// free heap" watermark down to a few KB on every save. Streaming keeps the
// extra RAM a save needs to essentially just littlefs's own write buffer.
//
// The schema has only one object level and single-level (scalar) arrays, so a
// single "need a comma before the next item" flag for each context is enough.
typedef struct {
    FILE *f;
    bool obj_comma; // a member has already been written at object level
    bool arr_comma; // an element has already been written in the current array
} jw_t;

// Emit a number: integers without a decimal point, non-integers at the
// shortest precision that still round-trips (mirrors cJSON's number printer
// closely enough that reload via cJSON_Parse yields the same double).
static void jw_num_val(jw_t *w, double v) {
    if (!isfinite(v)) {
        fputs("0", w->f);
        return;
    }
    if (floor(v) == v && fabs(v) < 1e15) {
        fprintf(w->f, "%.0f", v);
        return;
    }
    char tmp[32];
    for (int prec = 7; prec <= 17; prec++) {
        snprintf(tmp, sizeof(tmp), "%.*g", prec, v);
        if (strtod(tmp, NULL) == v)
            break;
    }
    fputs(tmp, w->f);
}

static void jw_key(jw_t *w, const char *k) {
    if (w->obj_comma)
        fputc(',', w->f);
    w->obj_comma = true;
    json_write_escaped(w->f, k);
    fputc(':', w->f);
}

// Object members. The jadd_*(d, "key", value) call signature is what the
// hundreds of call sites below are written against.
static void jadd_str(jw_t *o, const char *k, const char *v) {
    jw_key(o, k);
    json_write_escaped(o->f, v ? v : "");
}
static void jadd_num(jw_t *o, const char *k, double v) {
    jw_key(o, k);
    jw_num_val(o, v);
}
static void jadd_bool(jw_t *o, const char *k, bool v) {
    jw_key(o, k);
    fputs(v ? "true" : "false", o->f);
}

// Scalar arrays: every array written by config_write_json() holds strings or
// booleans, so those are the two element writers this needs.
static void jarr_begin(jw_t *o, const char *k) {
    jw_key(o, k);
    fputc('[', o->f);
    o->arr_comma = false;
}
static void jarr_end(jw_t *o) {
    fputc(']', o->f);
}
static void jarr_str(jw_t *o, const char *v) {
    if (o->arr_comma)
        fputc(',', o->f);
    o->arr_comma = true;
    json_write_escaped(o->f, v ? v : "");
}
static void jarr_bool(jw_t *o, bool v) {
    if (o->arr_comma)
        fputc(',', o->f);
    o->arr_comma = true;
    fputs(v ? "true" : "false", o->f);
}

static const char *jget_str(cJSON *o, const char *k, const char *def) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (v && cJSON_IsString(v))
        return v->valuestring;
    return def;
}
static double jget_num(cJSON *o, const char *k, double def) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (v && cJSON_IsNumber(v))
        return v->valuedouble;
    return def;
}
static bool jget_bool(cJSON *o, const char *k, bool def) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (v && cJSON_IsBool(v))
        return cJSON_IsTrue(v);
    return def;
}

// ---- serialize ------------------------------------------------------------
static void config_write_json(jw_t *d, const app_config_t *c) {
    fputc('{', d->f);
    jadd_num(d, "cpuFreq", c->cpuFreq);
    jadd_str(d, "myCallsign", c->my_callsign);
    jadd_num(d, "myLAT", c->my_lat);
    jadd_num(d, "myLON", c->my_lon);
    jadd_num(d, "myALT", c->my_alt);
    jadd_num(d, "myPHGPower", c->my_phg_power);
    jadd_num(d, "myPHGGain", c->my_phg_gain);
    jadd_num(d, "myPHGHeight", c->my_phg_height);
    jadd_num(d, "myPHGDir", c->my_phg_dir);
    jadd_str(d, "myPHG", c->my_phg);
    jadd_num(d, "myAmbiguity", c->pos_ambiguity);
    jadd_bool(d, "myStatusGrid", c->status_grid_en);
    jadd_num(d, "txTimeSlot", c->tx_timeslot);
    jadd_num(d, "csmaPersist", c->csma_persist);
    jadd_bool(d, "syncTime", c->synctime);
    jadd_num(d, "timeZone", c->timeZone);
    jadd_str(d, "ntpHost0", c->ntp_host[0]);
    jadd_str(d, "ntpHost1", c->ntp_host[1]);
    jadd_str(d, "ntpHost2", c->ntp_host[2]);
    jadd_num(d, "ntpResync", c->ntp_resync_sec);
    jadd_num(d, "WiFiMode", c->wifi_mode);
    jadd_num(d, "WiFiPwr", c->wifi_power);
    jadd_num(d, "WiFiAPCH", c->wifi_ap_ch);
    jadd_str(d, "WiFiAP_SSID", c->wifi_ap_ssid);
    jadd_str(d, "WiFiAP_PASS", c->wifi_ap_pass);
    jarr_begin(d, "WiFiSTA");
    for (int i = 0; i < WIFI_STA_NUM; i++) {
        jarr_bool(d, c->wifi_sta[i].enable);
        jarr_str(d, c->wifi_sta[i].wifi_ssid);
        jarr_str(d, c->wifi_sta[i].wifi_pass);
    }
    jarr_end(d);

    jadd_num(d, "fx25Mode", c->fx25_mode);
    jadd_num(d, "afskModem", c->afsk_modem_type);
    jadd_num(d, "rfPreamble", c->preamble);
    jadd_bool(d, "audioModemEn", c->audio_modem_en);
    jadd_bool(d, "audioLPF", c->audio_lpf);
    jadd_num(d, "rfTxBuffers", c->rf_tx_buffers);
    jadd_num(d, "pttMinUnkeyMs", c->ptt_min_unkey_ms);

    jadd_bool(d, "igateEn", c->igate_en);
    jadd_bool(d, "igateBcn", c->igate_bcn);
    jadd_bool(d, "rf2inet", c->rf2inet);
    jadd_bool(d, "inet2rf", c->inet2rf);
    jadd_bool(d, "igatePos2rf", c->igate_loc2rf);
    jadd_bool(d, "igatePos2inet", c->igate_loc2inet);
    jadd_num(d, "rf2inetFilter", c->rf2inetFilter);
    jadd_num(d, "inet2rfFilter", c->inet2rfFilter);
    jadd_num(d, "rf2inetBudlistMode", c->rf2inet_budlist_mode);
    jadd_num(d, "inet2rfBudlistMode", c->inet2rf_budlist_mode);
    jarr_begin(d, "budlist");
    for (int i = 0; i < IGATE_BUDLIST_MAX; i++)
        jarr_str(d, c->budlist[i]);
    jarr_end(d);
    jarr_begin(d, "satgate");
    for (int i = 0; i < IGATE_SATGATE_MAX; i++)
        jarr_str(d, c->satgate[i]);
    jarr_end(d);
    jadd_num(d, "dupCacheSize", c->dup_cache_size);
    jadd_num(d, "dupCacheTimeoutMs", c->dup_cache_timeout_ms);
    jadd_bool(d, "rf2inetRangeEn", c->rf2inet_range_en);
    jadd_num(d, "rf2inetRangeKm", c->rf2inet_range_km);
    jadd_bool(d, "rf2inetPrefixEn", c->rf2inet_prefix_en);
    jadd_str(d, "rf2inetPrefixes", c->rf2inet_prefixes);
    jadd_bool(d, "inet2rf3rdPartyUnwrapEn", c->inet2rf_3rdparty_unwrap_en);
    jadd_num(d, "igateSSID", c->aprs_ssid);
    jadd_num(d, "igatePort", c->aprs_port);
    jadd_str(d, "igateMycall", c->aprs_mycall);
    jadd_bool(d, "igateUseStation", c->igate_use_station);
    jadd_str(d, "igatePasscode", c->aprs_passcode);
    jadd_str(d, "igateHost", c->aprs_host);
    jadd_str(d, "igateFilter", c->aprs_filter);
    jadd_num(d, "igateLAT", c->igate_lat);
    jadd_num(d, "igateLON", c->igate_lon);
    jadd_num(d, "igateALT", c->igate_alt);
    jadd_num(d, "igateINV", c->igate_interval);
    jadd_str(d, "igateSymbol", c->igate_symbol);
    jadd_str(d, "igateObject", c->igate_object);
    jadd_str(d, "igatePHG", c->igate_phg);
    jadd_num(d, "igatePath", c->igate_path);
    jadd_str(d, "igateComment", c->igate_comment);
    jadd_num(d, "igateSTSIntv", c->igate_sts_interval);
    jadd_str(d, "igateStatus", c->igate_status);
    jadd_bool(d, "igateTimestamp", c->igate_timestamp);
    jadd_bool(d, "igateCompress", c->igate_compress);
    jadd_bool(d, "igatePHGEn", c->igate_phg_enable);
    jadd_bool(d, "igatePHGUseStation", c->igate_phg_use_station);
    jadd_num(d, "igatePHGPower", c->igate_phg_power);
    jadd_num(d, "igatePHGGain", c->igate_phg_gain);
    jadd_num(d, "igatePHGHeight", c->igate_phg_height);
    jadd_num(d, "igatePHGDir", c->igate_phg_dir);
    jadd_num(d, "igateExtType", c->igate_ext_type);
    jadd_num(d, "igateRng", c->igate_range_miles);
    jadd_num(d, "igateDfsS", c->igate_dfs_strength);

    jadd_bool(d, "digiEn", c->digi_en);
    jadd_bool(d, "digiAuto", c->digi_auto);
    jadd_bool(d, "digiPos2rf", c->digi_loc2rf);
    jadd_bool(d, "digiPos2inet", c->digi_loc2inet);
    jadd_bool(d, "digiTime", c->digi_timestamp);
    jadd_num(d, "digiSSID", c->digi_ssid);
    jadd_str(d, "digiMycall", c->digi_mycall);
    jadd_bool(d, "digiUseStation", c->digi_use_station);
    jadd_num(d, "digiPath", c->digi_path);
    jadd_num(d, "digiDelay", c->digi_delay);
    jadd_num(d, "digiFilter", c->digiFilter);
    jadd_bool(d, "digiBcn", c->digi_bcn);
    jadd_bool(d, "digiCompress", c->digi_compress);
    jadd_num(d, "digiAlt", c->digi_alt);
    jadd_num(d, "digiLAT", c->digi_lat);
    jadd_num(d, "digiLON", c->digi_lon);
    jadd_num(d, "digiINV", c->digi_interval);
    jadd_str(d, "digiSymbol", c->digi_symbol);
    jadd_str(d, "digiPHG", c->digi_phg);
    jadd_str(d, "digiComment", c->digi_comment);
    jadd_num(d, "digiSTSIntv", c->digi_sts_interval);
    jadd_str(d, "digiStatus", c->digi_status);

    jadd_bool(d, "trkEn", c->trk_en);
    jadd_bool(d, "trkPos2rf", c->trk_loc2rf);
    jadd_bool(d, "trkPos2inet", c->trk_loc2inet);
    jadd_bool(d, "trkTime", c->trk_timestamp);
    jadd_num(d, "trkSSID", c->trk_ssid);
    jadd_str(d, "trkMycall", c->trk_mycall);
    jadd_bool(d, "trkUseStation", c->trk_use_station);
    jadd_num(d, "trkPath", c->trk_path);
    jadd_num(d, "trkLAT", c->trk_lat);
    jadd_num(d, "trkLON", c->trk_lon);
    jadd_num(d, "trkALT", c->trk_alt);
    jadd_num(d, "trkINV", c->trk_interval);
    jadd_bool(d, "trkCompress", c->trk_compress);
    jadd_bool(d, "trkMice", c->trk_mice);
    jadd_bool(d, "trkOptAlt", c->trk_altitude);
    jadd_bool(d, "trkLog", c->trk_log);
    jadd_bool(d, "trkOptRSSI", c->trk_rssi);
    jadd_str(d, "trkSymbol", c->trk_symbol);
    jadd_str(d, "trkSymbolMove", c->trk_symmove);
    jadd_str(d, "trkSymbolStop", c->trk_symstop);
    jadd_str(d, "trkItem", c->trk_item);
    jadd_str(d, "trkComment", c->trk_comment);
    jadd_num(d, "trkSTSIntv", c->trk_sts_interval);
    jadd_str(d, "trkStatus", c->trk_status);

    jadd_bool(d, "wxEn", c->wx_en);
    jadd_bool(d, "wxTx2rf", c->wx_2rf);
    jadd_bool(d, "wxTx2inet", c->wx_2inet);
    jadd_bool(d, "wxTime", c->wx_timestamp);
    jadd_num(d, "wxSSID", c->wx_ssid);
    jadd_str(d, "wxMycall", c->wx_mycall);
    jadd_bool(d, "wxUseStation", c->wx_use_station);
    jadd_num(d, "wxPath", c->wx_path);
    jadd_num(d, "wxLAT", c->wx_lat);
    jadd_num(d, "wxLON", c->wx_lon);
    jadd_num(d, "wxALT", c->wx_alt);
    jadd_num(d, "wxInv", c->wx_interval);
    jadd_str(d, "wxObject", c->wx_object);
    jadd_str(d, "wxComment", c->wx_comment);
    jarr_begin(d, "wxSenEn");
    for (int i = 0; i < WX_SENSOR_NUM; i++)
        jarr_bool(d, c->wx_sensor_enable[i]);
    jarr_end(d);
    jarr_begin(d, "wxSenAvg");
    for (int i = 0; i < WX_SENSOR_NUM; i++)
        jarr_bool(d, c->wx_sensor_avg[i]);
    jarr_end(d);
    // Sensor mappings travel as driver NAMES, not registry positions: a
    // position only means anything within the firmware image that produced it
    // (see the persistence contract in sensors_local.h), so storing the index
    // would re-point all 13 WX fields at different sensors the moment a driver
    // is enabled or disabled in Kconfig. An unmapped field writes "".
    jarr_begin(d, "wxSenCH");
    for (int i = 0; i < WX_SENSOR_NUM; i++)
        jarr_str(d, sensors_local_channel_name(c->wx_sensor_ch[i]));
    jarr_end(d);

    // Telemetry configuration (channel 0/1, Binary B1-B8 mapping) is no
    // longer part of config.json - see telemetry.h/.c and /storage/telemetry.json.

    jadd_str(d, "httpUser", c->http_username);
    jadd_str(d, "httpPass", c->http_password);
    jarr_begin(d, "path");
    for (int i = 0; i < 4; i++)
        jarr_str(d, c->path[i]);
    jarr_end(d);

    // rfPTT (PTT GPIO) and rfPTTAct (PTT active-high) are not serialized:
    // both are fixed compile-time constants (MODEM_PTT_GPIO /
    // MODEM_PTT_ACTIVE_HIGH), not stored settings.

    jadd_num(d, "logFile", c->log);
    jadd_str(d, "hostName", c->host_name);
    jadd_num(d, "resetTimeout", c->reset_timeout);

    jadd_bool(d, "msgEnable", c->msg_enable);
    jadd_str(d, "msgMycall", c->msg_mycall);
    jadd_bool(d, "msgUseStation", c->msg_use_station);
    jadd_bool(d, "msgRf", c->msg_rf);
    jadd_bool(d, "msgInet", c->msg_inet);
    jadd_num(d, "msgPath", c->msg_path);
    jadd_num(d, "msgRetry", c->msg_retry);
    jadd_num(d, "msgInterval", c->msg_interval);
    jadd_bool(d, "msgAlarmEn", c->msg_alarm_enable);
    jadd_num(d, "msgAlarmGpio", c->msg_alarm_gpio);

    jadd_bool(d, "queryEn", c->query_en);
    jadd_bool(d, "queryRf", c->query_rf);
    jadd_bool(d, "queryInet", c->query_inet);
    jadd_bool(d, "queryAprsEn", c->query_aprs_en);
    jadd_bool(d, "queryWxEn", c->query_wx_en);
    jadd_bool(d, "queryIgateEn", c->query_igate_en);
    jadd_bool(d, "queryDirectedEn", c->query_directed_en);
    jadd_bool(d, "queryExtEn", c->query_ext_en);
    jadd_num(d, "queryMinInterval", c->query_min_interval_sec);

    fputc('}', d->f);
}

// ---- deserialize ------------------------------------------------------------
static void config_from_json(cJSON *d, app_config_t *c) {
    // Start from defaults so every key not present in an older config file
    // still ends up with a sane, documented value (never zero-garbage).
    app_config_t def;
    app_config_set_defaults(&def);
    *c = def;

    c->cpuFreq = (uint8_t)jget_num(d, "cpuFreq", def.cpuFreq);
    set_str(c->my_callsign, sizeof(c->my_callsign), jget_str(d, "myCallsign", def.my_callsign));
    c->my_lat = (float)jget_num(d, "myLAT", def.my_lat);
    c->my_lon = (float)jget_num(d, "myLON", def.my_lon);
    c->my_alt = (float)jget_num(d, "myALT", def.my_alt);
    c->my_phg_power = (uint16_t)jget_num(d, "myPHGPower", def.my_phg_power);
    c->my_phg_gain = (float)jget_num(d, "myPHGGain", def.my_phg_gain);
    c->my_phg_height = (uint16_t)jget_num(d, "myPHGHeight", def.my_phg_height);
    c->my_phg_dir = (uint8_t)jget_num(d, "myPHGDir", def.my_phg_dir);
    set_str(c->my_phg, sizeof(c->my_phg), jget_str(d, "myPHG", def.my_phg));
    c->pos_ambiguity = (uint8_t)jget_num(d, "myAmbiguity", def.pos_ambiguity);
    if (c->pos_ambiguity > POS_AMBIGUITY_MAX) {
        ESP_LOGW(TAG, "myAmbiguity %u out of range, clamped to %d", (unsigned)c->pos_ambiguity, POS_AMBIGUITY_MAX);
        c->pos_ambiguity = POS_AMBIGUITY_MAX;
    }
    c->status_grid_en = jget_bool(d, "myStatusGrid", def.status_grid_en);
    // Channel-access timing: bound every value coming off flash to the same
    // range the Radiomodem form accepts (aprs_service.h), so a hand-edited or
    // imported config.json cannot hand aprs_service_build_modem_config() a
    // setting the radio should never transmit with - see the note there on
    // what an unbounded preamble does to a shared channel.
    c->tx_timeslot = (uint16_t)jget_num(d, "txTimeSlot", def.tx_timeslot);
    if (c->tx_timeslot > RF_TX_TIMESLOT_MS_MAX) {
        ESP_LOGW(TAG, "txTimeSlot %u out of range, clamped to %d ms", (unsigned)c->tx_timeslot, RF_TX_TIMESLOT_MS_MAX);
        c->tx_timeslot = RF_TX_TIMESLOT_MS_MAX;
    }
    c->csma_persist = (uint8_t)jget_num(d, "csmaPersist", def.csma_persist);
    if (c->csma_persist < CSMA_PERSIST_MIN)
        c->csma_persist = CSMA_PERSIST_MIN;
    c->synctime = jget_bool(d, "syncTime", def.synctime);
    c->timeZone = (float)jget_num(d, "timeZone", def.timeZone);
    set_str(c->ntp_host[0], sizeof(c->ntp_host[0]), jget_str(d, "ntpHost0", jget_str(d, "ntpHost", def.ntp_host[0])));
    set_str(c->ntp_host[1], sizeof(c->ntp_host[1]), jget_str(d, "ntpHost1", def.ntp_host[1]));
    set_str(c->ntp_host[2], sizeof(c->ntp_host[2]), jget_str(d, "ntpHost2", def.ntp_host[2]));
    c->ntp_resync_sec = (uint16_t)jget_num(d, "ntpResync", def.ntp_resync_sec);
    if (c->ntp_resync_sec < NTP_RESYNC_MIN_SEC)
        c->ntp_resync_sec = NTP_RESYNC_MIN_SEC;
    c->wifi_mode = (uint8_t)jget_num(d, "WiFiMode", def.wifi_mode);
    c->wifi_power = (int8_t)jget_num(d, "WiFiPwr", def.wifi_power);
    c->wifi_ap_ch = (uint8_t)jget_num(d, "WiFiAPCH", def.wifi_ap_ch);
    // The file on flash is not a trusted input: it can arrive from a crafted
    // POST, a hand edit over the Storage page, or a backup taken from a build
    // with a different regulatory range. A channel outside WIFI_AP_CH_MIN..MAX
    // is rejected by esp_wifi_set_config(), which would take the access point
    // - the only way back into the device - down with it, so it is folded back
    // to the default here rather than carried into wifi_init().
    if (c->wifi_ap_ch < WIFI_AP_CH_MIN || c->wifi_ap_ch > WIFI_AP_CH_MAX) {
        ESP_LOGW(TAG, "stored SoftAP channel %u outside %u-%u, using %u", (unsigned)c->wifi_ap_ch, (unsigned)WIFI_AP_CH_MIN, (unsigned)WIFI_AP_CH_MAX,
                 (unsigned)WIFI_AP_CH_DEFAULT);
        c->wifi_ap_ch = WIFI_AP_CH_DEFAULT;
    }
    set_str(c->wifi_ap_ssid, sizeof(c->wifi_ap_ssid), jget_str(d, "WiFiAP_SSID", def.wifi_ap_ssid));
    set_str(c->wifi_ap_pass, sizeof(c->wifi_ap_pass), jget_str(d, "WiFiAP_PASS", def.wifi_ap_pass));
    {
        cJSON *arr = cJSON_GetObjectItemCaseSensitive(d, "WiFiSTA");
        if (arr && cJSON_IsArray(arr)) {
            for (int i = 0; i < WIFI_STA_NUM; i++) {
                cJSON *e = cJSON_GetArrayItem(arr, i * 3);
                cJSON *s = cJSON_GetArrayItem(arr, i * 3 + 1);
                cJSON *p = cJSON_GetArrayItem(arr, i * 3 + 2);
                c->wifi_sta[i].enable = e ? cJSON_IsTrue(e) : false;
                set_str(c->wifi_sta[i].wifi_ssid, sizeof(c->wifi_sta[i].wifi_ssid), (s && cJSON_IsString(s)) ? s->valuestring : def.wifi_sta[i].wifi_ssid);
                set_str(c->wifi_sta[i].wifi_pass, sizeof(c->wifi_sta[i].wifi_pass), (p && cJSON_IsString(p)) ? p->valuestring : def.wifi_sta[i].wifi_pass);
            }
        }
    }

    c->fx25_mode = (uint8_t)jget_num(d, "fx25Mode", def.fx25_mode);
    c->afsk_modem_type = (uint8_t)jget_num(d, "afskModem", def.afsk_modem_type);
    c->preamble = (uint16_t)jget_num(d, "rfPreamble", def.preamble);
    if (c->preamble < RF_PREAMBLE_MS_MIN || c->preamble > RF_PREAMBLE_MS_MAX) {
        ESP_LOGW(TAG, "rfPreamble %u out of range, clamped to %d..%d ms", (unsigned)c->preamble, RF_PREAMBLE_MS_MIN, RF_PREAMBLE_MS_MAX);
        c->preamble = (c->preamble < RF_PREAMBLE_MS_MIN) ? RF_PREAMBLE_MS_MIN : RF_PREAMBLE_MS_MAX;
    }
    c->audio_modem_en = jget_bool(d, "audioModemEn", def.audio_modem_en);
    c->audio_lpf = jget_bool(d, "audioLPF", def.audio_lpf);
    c->rf_tx_buffers = (uint8_t)jget_num(d, "rfTxBuffers", def.rf_tx_buffers);
    if (c->rf_tx_buffers < RF_TX_BUFFERS_MIN)
        c->rf_tx_buffers = RF_TX_BUFFERS_MIN;
    else if (c->rf_tx_buffers > RF_TX_BUFFERS_MAX)
        c->rf_tx_buffers = RF_TX_BUFFERS_MAX;
    c->ptt_min_unkey_ms = (uint16_t)jget_num(d, "pttMinUnkeyMs", def.ptt_min_unkey_ms);
    if (c->ptt_min_unkey_ms > PTT_MIN_UNKEY_MS_MAX)
        c->ptt_min_unkey_ms = PTT_MIN_UNKEY_MS_MAX;

    c->igate_en = jget_bool(d, "igateEn", def.igate_en);
    c->igate_bcn = jget_bool(d, "igateBcn", def.igate_bcn);
    c->rf2inet = jget_bool(d, "rf2inet", def.rf2inet);
    c->inet2rf = jget_bool(d, "inet2rf", def.inet2rf);
    c->igate_loc2rf = jget_bool(d, "igatePos2rf", def.igate_loc2rf);
    c->igate_loc2inet = jget_bool(d, "igatePos2inet", def.igate_loc2inet);
    c->rf2inetFilter = (uint16_t)jget_num(d, "rf2inetFilter", def.rf2inetFilter);
    // "inet2rfFiltger" was a legacy misspelling of the key used when saving;
    // fall back to it so configs written by older firmware still load correctly.
    c->inet2rfFilter = (uint16_t)jget_num(d, "inet2rfFilter", (double)jget_num(d, "inet2rfFiltger", def.inet2rfFilter));
    c->rf2inet_budlist_mode = (budlist_mode_t)jget_num(d, "rf2inetBudlistMode", def.rf2inet_budlist_mode);
    c->inet2rf_budlist_mode = (budlist_mode_t)jget_num(d, "inet2rfBudlistMode", def.inet2rf_budlist_mode);
    {
        cJSON *bl = cJSON_GetObjectItemCaseSensitive(d, "budlist");
        for (int i = 0; i < IGATE_BUDLIST_MAX; i++) {
            cJSON *v = bl ? cJSON_GetArrayItem(bl, i) : NULL;
            set_str(c->budlist[i], sizeof(c->budlist[i]), (v && cJSON_IsString(v)) ? v->valuestring : def.budlist[i]);
        }
    }
    {
        cJSON *sg = cJSON_GetObjectItemCaseSensitive(d, "satgate");
        for (int i = 0; i < IGATE_SATGATE_MAX; i++) {
            cJSON *v = sg ? cJSON_GetArrayItem(sg, i) : NULL;
            set_str(c->satgate[i], sizeof(c->satgate[i]), (v && cJSON_IsString(v)) ? v->valuestring : def.satgate[i]);
        }
    }
    c->dup_cache_size = (uint8_t)jget_num(d, "dupCacheSize", def.dup_cache_size);
    if (c->dup_cache_size < DUP_CACHE_SIZE_MIN || c->dup_cache_size > DUP_CACHE_SIZE_MAX) {
        ESP_LOGW(TAG, "dupCacheSize %u out of range, clamped to %d..%d", (unsigned)c->dup_cache_size, DUP_CACHE_SIZE_MIN, DUP_CACHE_SIZE_MAX);
        c->dup_cache_size = (c->dup_cache_size < DUP_CACHE_SIZE_MIN) ? DUP_CACHE_SIZE_MIN : DUP_CACHE_SIZE_MAX;
    }
    c->dup_cache_timeout_ms = (uint32_t)jget_num(d, "dupCacheTimeoutMs", def.dup_cache_timeout_ms);
    if (c->dup_cache_timeout_ms < DUP_CACHE_TIMEOUT_MS_MIN || c->dup_cache_timeout_ms > DUP_CACHE_TIMEOUT_MS_MAX) {
        ESP_LOGW(TAG, "dupCacheTimeoutMs %u out of range, clamped to %d..%d ms", (unsigned)c->dup_cache_timeout_ms, DUP_CACHE_TIMEOUT_MS_MIN,
                 DUP_CACHE_TIMEOUT_MS_MAX);
        c->dup_cache_timeout_ms = (c->dup_cache_timeout_ms < DUP_CACHE_TIMEOUT_MS_MIN) ? DUP_CACHE_TIMEOUT_MS_MIN : DUP_CACHE_TIMEOUT_MS_MAX;
    }
    c->rf2inet_range_en = jget_bool(d, "rf2inetRangeEn", def.rf2inet_range_en);
    c->rf2inet_range_km = (float)jget_num(d, "rf2inetRangeKm", def.rf2inet_range_km);
    c->rf2inet_prefix_en = jget_bool(d, "rf2inetPrefixEn", def.rf2inet_prefix_en);
    set_str(c->rf2inet_prefixes, sizeof(c->rf2inet_prefixes), jget_str(d, "rf2inetPrefixes", def.rf2inet_prefixes));
    c->inet2rf_3rdparty_unwrap_en = jget_bool(d, "inet2rf3rdPartyUnwrapEn", def.inet2rf_3rdparty_unwrap_en);
    c->aprs_ssid = (uint8_t)jget_num(d, "igateSSID", def.aprs_ssid);
    c->aprs_port = (uint16_t)jget_num(d, "igatePort", def.aprs_port);
    // Same two-layer clamp as the SoftAP channel above: the file on flash is
    // not a trusted input, and port 0 would send the IGate into a five-second
    // reconnect loop against an address it can never connect to. Only the low
    // bound needs testing: APRS_PORT_MAX is the full range of the uint16_t the
    // value is already narrowed to.
    if (c->aprs_port < APRS_PORT_MIN) {
        ESP_LOGW(TAG, "stored APRS-IS port %u outside %u-%u, using %u", (unsigned)c->aprs_port, (unsigned)APRS_PORT_MIN, (unsigned)APRS_PORT_MAX,
                 (unsigned)APRS_PORT_DEFAULT);
        c->aprs_port = APRS_PORT_DEFAULT;
    }
    set_str(c->aprs_mycall, sizeof(c->aprs_mycall), jget_str(d, "igateMycall", def.aprs_mycall));
    c->igate_use_station = jget_bool(d, "igateUseStation", def.igate_use_station);
    set_str(c->aprs_passcode, sizeof(c->aprs_passcode), jget_str(d, "igatePasscode", def.aprs_passcode));
    set_str(c->aprs_host, sizeof(c->aprs_host), jget_str(d, "igateHost", def.aprs_host));
    set_str(c->aprs_filter, sizeof(c->aprs_filter), jget_str(d, "igateFilter", def.aprs_filter));
    c->igate_lat = (float)jget_num(d, "igateLAT", def.igate_lat);
    c->igate_lon = (float)jget_num(d, "igateLON", def.igate_lon);
    c->igate_alt = (float)jget_num(d, "igateALT", def.igate_alt);
    c->igate_interval = (uint16_t)jget_num(d, "igateINV", def.igate_interval);
    set_str(c->igate_symbol, sizeof(c->igate_symbol), jget_str(d, "igateSymbol", def.igate_symbol));
    set_str(c->igate_object, sizeof(c->igate_object), jget_str(d, "igateObject", def.igate_object));
    set_str(c->igate_phg, sizeof(c->igate_phg), jget_str(d, "igatePHG", def.igate_phg));
    c->igate_path = (uint8_t)jget_num(d, "igatePath", def.igate_path);
    set_str(c->igate_comment, sizeof(c->igate_comment), jget_str(d, "igateComment", def.igate_comment));
    c->igate_timestamp = jget_bool(d, "igateTimestamp", def.igate_timestamp);
    c->igate_compress = jget_bool(d, "igateCompress", def.igate_compress);
    c->igate_phg_enable = jget_bool(d, "igatePHGEn", def.igate_phg_enable);
    c->igate_phg_use_station = jget_bool(d, "igatePHGUseStation", def.igate_phg_use_station);
    c->igate_phg_power = (uint16_t)jget_num(d, "igatePHGPower", def.igate_phg_power);
    c->igate_phg_gain = (float)jget_num(d, "igatePHGGain", def.igate_phg_gain);
    c->igate_phg_height = (uint16_t)jget_num(d, "igatePHGHeight", def.igate_phg_height);
    c->igate_phg_dir = (uint8_t)jget_num(d, "igatePHGDir", def.igate_phg_dir);
    c->igate_ext_type = (uint8_t)jget_num(d, "igateExtType", def.igate_ext_type);
    if (c->igate_ext_type > APRS_EXT_DFS) {
        ESP_LOGW(TAG, "igateExtType %u unknown, using PHG", (unsigned)c->igate_ext_type);
        c->igate_ext_type = APRS_EXT_PHG;
    }
    c->igate_range_miles = (uint16_t)jget_num(d, "igateRng", def.igate_range_miles);
    if (c->igate_range_miles > APRS_EXT_RANGE_MILES_MAX) {
        ESP_LOGW(TAG, "igateRng %u out of range, clamped to %d", (unsigned)c->igate_range_miles, APRS_EXT_RANGE_MILES_MAX);
        c->igate_range_miles = APRS_EXT_RANGE_MILES_MAX;
    }
    c->igate_dfs_strength = (uint8_t)jget_num(d, "igateDfsS", def.igate_dfs_strength);
    if (c->igate_dfs_strength > APRS_EXT_DFS_STRENGTH_MAX) {
        ESP_LOGW(TAG, "igateDfsS %u out of range, clamped to %d", (unsigned)c->igate_dfs_strength, APRS_EXT_DFS_STRENGTH_MAX);
        c->igate_dfs_strength = APRS_EXT_DFS_STRENGTH_MAX;
    }
    c->igate_sts_interval = (uint16_t)jget_num(d, "igateSTSIntv", def.igate_sts_interval);
    set_str(c->igate_status, sizeof(c->igate_status), jget_str(d, "igateStatus", def.igate_status));

    c->digi_en = jget_bool(d, "digiEn", def.digi_en);
    c->digi_auto = jget_bool(d, "digiAuto", def.digi_auto);
    c->digi_loc2rf = jget_bool(d, "digiPos2rf", def.digi_loc2rf);
    c->digi_loc2inet = jget_bool(d, "digiPos2inet", def.digi_loc2inet);
    c->digi_timestamp = jget_bool(d, "digiTime", def.digi_timestamp);
    c->digi_ssid = (uint8_t)jget_num(d, "digiSSID", def.digi_ssid);
    set_str(c->digi_mycall, sizeof(c->digi_mycall), jget_str(d, "digiMycall", def.digi_mycall));
    c->digi_use_station = jget_bool(d, "digiUseStation", def.digi_use_station);
    c->digi_path = (uint8_t)jget_num(d, "digiPath", def.digi_path);
    c->digi_delay = (uint16_t)jget_num(d, "digiDelay", def.digi_delay);
    c->digiFilter = (uint16_t)jget_num(d, "digiFilter", def.digiFilter);
    c->digi_bcn = jget_bool(d, "digiBcn", def.digi_bcn);
    c->digi_compress = jget_bool(d, "digiCompress", def.digi_compress);
    c->digi_alt = (float)jget_num(d, "digiAlt", def.digi_alt);
    c->digi_lat = (float)jget_num(d, "digiLAT", def.digi_lat);
    c->digi_lon = (float)jget_num(d, "digiLON", def.digi_lon);
    c->digi_interval = (uint16_t)jget_num(d, "digiINV", def.digi_interval);
    set_str(c->digi_symbol, sizeof(c->digi_symbol), jget_str(d, "digiSymbol", def.digi_symbol));
    set_str(c->digi_phg, sizeof(c->digi_phg), jget_str(d, "digiPHG", def.digi_phg));
    set_str(c->digi_comment, sizeof(c->digi_comment), jget_str(d, "digiComment", def.digi_comment));
    c->digi_sts_interval = (uint16_t)jget_num(d, "digiSTSIntv", def.digi_sts_interval);
    set_str(c->digi_status, sizeof(c->digi_status), jget_str(d, "digiStatus", def.digi_status));

    c->trk_en = jget_bool(d, "trkEn", def.trk_en);
    c->trk_loc2rf = jget_bool(d, "trkPos2rf", def.trk_loc2rf);
    c->trk_loc2inet = jget_bool(d, "trkPos2inet", def.trk_loc2inet);
    c->trk_timestamp = jget_bool(d, "trkTime", def.trk_timestamp);
    c->trk_ssid = (uint8_t)jget_num(d, "trkSSID", def.trk_ssid);
    set_str(c->trk_mycall, sizeof(c->trk_mycall), jget_str(d, "trkMycall", def.trk_mycall));
    c->trk_use_station = jget_bool(d, "trkUseStation", def.trk_use_station);
    c->trk_path = (uint8_t)jget_num(d, "trkPath", def.trk_path);
    c->trk_lat = (float)jget_num(d, "trkLAT", def.trk_lat);
    c->trk_lon = (float)jget_num(d, "trkLON", def.trk_lon);
    c->trk_alt = (float)jget_num(d, "trkALT", def.trk_alt);
    c->trk_interval = (uint16_t)jget_num(d, "trkINV", def.trk_interval);
    c->trk_compress = jget_bool(d, "trkCompress", def.trk_compress);
    c->trk_mice = jget_bool(d, "trkMice", def.trk_mice);
    c->trk_altitude = jget_bool(d, "trkOptAlt", def.trk_altitude);
    c->trk_log = jget_bool(d, "trkLog", def.trk_log);
    c->trk_rssi = jget_bool(d, "trkOptRSSI", def.trk_rssi);
    set_str(c->trk_symbol, sizeof(c->trk_symbol), jget_str(d, "trkSymbol", def.trk_symbol));
    set_str(c->trk_symmove, sizeof(c->trk_symmove), jget_str(d, "trkSymbolMove", def.trk_symmove));
    set_str(c->trk_symstop, sizeof(c->trk_symstop), jget_str(d, "trkSymbolStop", def.trk_symstop));
    set_str(c->trk_item, sizeof(c->trk_item), jget_str(d, "trkItem", def.trk_item));
    set_str(c->trk_comment, sizeof(c->trk_comment), jget_str(d, "trkComment", def.trk_comment));
    c->trk_sts_interval = (uint16_t)jget_num(d, "trkSTSIntv", def.trk_sts_interval);
    set_str(c->trk_status, sizeof(c->trk_status), jget_str(d, "trkStatus", def.trk_status));

    c->wx_en = jget_bool(d, "wxEn", def.wx_en);
    c->wx_2rf = jget_bool(d, "wxTx2rf", def.wx_2rf);
    c->wx_2inet = jget_bool(d, "wxTx2inet", def.wx_2inet);
    c->wx_timestamp = jget_bool(d, "wxTime", def.wx_timestamp);
    c->wx_ssid = (uint8_t)jget_num(d, "wxSSID", def.wx_ssid);
    set_str(c->wx_mycall, sizeof(c->wx_mycall), jget_str(d, "wxMycall", def.wx_mycall));
    c->wx_use_station = jget_bool(d, "wxUseStation", def.wx_use_station);
    c->wx_path = (uint8_t)jget_num(d, "wxPath", def.wx_path);
    c->wx_lat = (float)jget_num(d, "wxLAT", def.wx_lat);
    c->wx_lon = (float)jget_num(d, "wxLON", def.wx_lon);
    c->wx_alt = (float)jget_num(d, "wxALT", def.wx_alt);
    c->wx_interval = (uint16_t)jget_num(d, "wxInv", def.wx_interval);
    set_str(c->wx_object, sizeof(c->wx_object), jget_str(d, "wxObject", def.wx_object));
    set_str(c->wx_comment, sizeof(c->wx_comment), jget_str(d, "wxComment", def.wx_comment));
    {
        cJSON *a1 = cJSON_GetObjectItemCaseSensitive(d, "wxSenEn"), *a2 = cJSON_GetObjectItemCaseSensitive(d, "wxSenAvg"),
              *a3 = cJSON_GetObjectItemCaseSensitive(d, "wxSenCH");
        for (int i = 0; i < WX_SENSOR_NUM; i++) {
            cJSON *v;
            if (a1 && (v = cJSON_GetArrayItem(a1, i)))
                c->wx_sensor_enable[i] = cJSON_IsTrue(v);
            if (a2 && (v = cJSON_GetArrayItem(a2, i)))
                c->wx_sensor_avg[i] = cJSON_IsTrue(v);
            if (a3 && (v = cJSON_GetArrayItem(a3, i))) {
                if (cJSON_IsString(v)) {
                    // Driver name (see the writer above): resolve it against
                    // the registry this image actually built. A name that is
                    // no longer registered leaves the field unmapped and says
                    // so, rather than aiming it at whatever sensor now sits at
                    // that position.
                    c->wx_sensor_ch[i] = sensors_local_channel_from_name(v->valuestring);
                    if (c->wx_sensor_ch[i] == SENSOR_LOCAL_CH_NONE && v->valuestring != NULL && v->valuestring[0] != 0)
                        ESP_LOGW(TAG, "WX field %d: sensor '%s' is not registered, left unmapped", i, v->valuestring);
                } else if (cJSON_IsNumber(v)) {
                    // A config.json that predates name-based mappings: the
                    // number is a registry position, and it is only meaningful
                    // if the registry still reaches that far.
                    unsigned idx = (unsigned)v->valuedouble;
                    if (idx != SENSOR_LOCAL_CH_NONE && idx >= sensors_local_count()) {
                        ESP_LOGW(TAG, "WX field %d: stored sensor channel %u no longer exists, left unmapped", i, idx);
                        idx = SENSOR_LOCAL_CH_NONE;
                    }
                    c->wx_sensor_ch[i] = (uint8_t)idx;
                }
            }
        }
    }

    // Telemetry configuration (channel 0/1, Binary B1-B8 mapping) is no
    // longer part of config.json - loaded separately via
    // telemetry_config_load() from /storage/telemetry.json. Any leftover
    // tlm0*/tlm1*/tlmBit* keys in an old config.json are simply ignored here
    // (config_from_json() already ignores unknown keys generally).

    set_str(c->http_username, sizeof(c->http_username), jget_str(d, "httpUser", def.http_username));
    set_str(c->http_password, sizeof(c->http_password), jget_str(d, "httpPass", def.http_password));
    {
        cJSON *p = cJSON_GetObjectItemCaseSensitive(d, "path");
        for (int i = 0; i < 4; i++) {
            cJSON *v = p ? cJSON_GetArrayItem(p, i) : NULL;
            set_str(c->path[i], sizeof(c->path[i]), (v && cJSON_IsString(v)) ? v->valuestring : def.path[i]);
        }
    }

    // rfPTT (PTT GPIO) and rfPTTAct (PTT active-high) are not read back:
    // a config.json may still contain either key, but config_from_json()
    // ignores unknown keys, so both are simply skipped - the values come
    // from MODEM_PTT_GPIO / MODEM_PTT_ACTIVE_HIGH instead.

    c->log = (uint16_t)jget_num(d, "logFile", def.log);
    set_str(c->host_name, sizeof(c->host_name), jget_str(d, "hostName", def.host_name));
    c->reset_timeout = (uint16_t)jget_num(d, "resetTimeout", def.reset_timeout);

    if (!cJSON_GetObjectItemCaseSensitive(d, "msgEnable")) {
        // old-version file compatibility -> keep documented defaults
        c->msg_enable = true;
        c->msg_rf = true;
        c->msg_inet = true;
        c->msg_retry = 3;
        c->msg_interval = 30;
        c->msg_path = 9;
        set_str(c->msg_mycall, sizeof(c->msg_mycall), "NOCALL");
    } else {
        c->msg_enable = jget_bool(d, "msgEnable", def.msg_enable);
        c->msg_path = (uint8_t)jget_num(d, "msgPath", def.msg_path);
        c->msg_rf = jget_bool(d, "msgRf", def.msg_rf);
        c->msg_inet = jget_bool(d, "msgInet", def.msg_inet);
        c->msg_retry = (uint8_t)jget_num(d, "msgRetry", def.msg_retry);
        c->msg_interval = (uint16_t)jget_num(d, "msgInterval", def.msg_interval);
        set_str(c->msg_mycall, sizeof(c->msg_mycall), jget_str(d, "msgMycall", def.msg_mycall));
        c->msg_use_station = jget_bool(d, "msgUseStation", def.msg_use_station);
    }
    c->msg_alarm_enable = jget_bool(d, "msgAlarmEn", def.msg_alarm_enable);
    c->msg_alarm_gpio = (int8_t)jget_num(d, "msgAlarmGpio", def.msg_alarm_gpio);

    c->query_en = jget_bool(d, "queryEn", def.query_en);
    c->query_rf = jget_bool(d, "queryRf", def.query_rf);
    c->query_inet = jget_bool(d, "queryInet", def.query_inet);
    c->query_aprs_en = jget_bool(d, "queryAprsEn", def.query_aprs_en);
    c->query_wx_en = jget_bool(d, "queryWxEn", def.query_wx_en);
    c->query_igate_en = jget_bool(d, "queryIgateEn", def.query_igate_en);
    c->query_directed_en = jget_bool(d, "queryDirectedEn", def.query_directed_en);
    c->query_ext_en = jget_bool(d, "queryExtEn", def.query_ext_en);
    c->query_min_interval_sec = (uint16_t)jget_num(d, "queryMinInterval", def.query_min_interval_sec);
    if (c->query_min_interval_sec < 5) // floor: airtime/loop safety, matches the webconfig page's own clamp
        c->query_min_interval_sec = 5;
}

bool app_config_save(void) {
    // Streams the configuration straight to the file, one field at a time -
    // see the note on the JSON writer above for why no in-RAM document is
    // built.
    //
    // Serialize the whole save against any other save/load in flight (see
    // s_config_mutex comment above). Block indefinitely: a save must never
    // be silently dropped, and the critical section below is short.
    xSemaphoreTake(config_mutex(), portMAX_DELAY);

    // Second, filesystem-wide gate (storage.h): config_mutex() only keeps two
    // config saves apart, while the temp-file + rename sequence below must
    // also not overlap the whole-partition erase the web Storage page can
    // trigger, nor a save being made by another subsystem. Module lock first,
    // this gate second - the order storage.h's contract requires.
    storage_write_lock();

    // config_mutex() is held across this whole function, which is what
    // json_store_open_tmp() asserts before handing back a stream whose stdio
    // buffer is already pinned.
    FILE *f = json_store_open_tmp(CONFIG_TMP_PATH, TAG, config_mutex());
    if (!f) {
        storage_write_unlock();
        xSemaphoreGive(config_mutex());
        return false;
    }

    jw_t w = { .f = f, .obj_comma = false, .arr_comma = false };
    config_write_json(&w, &g_config);

    if (!json_store_commit(f, CONFIG_TMP_PATH, CONFIG_PATH, TAG, "configuration")) {
        storage_write_unlock();
        xSemaphoreGive(config_mutex());
        return false;
    }

    // How close the calling task (normally the httpd task) came to overflowing
    // its stack during this save, so the config.stack_size in web_server.c can
    // be sized from real numbers instead of a guess. Remove once a safe margin
    // is confirmed.
    ESP_LOGI(TAG, "Caller stack high-water mark: %u bytes free", (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
    storage_write_unlock();
    xSemaphoreGive(config_mutex());
    return true;
}

bool app_config_load(void) {
    cJSON *doc = NULL;
    json_store_status_t st = json_store_read(CONFIG_PATH, TAG, "configuration", &doc);

    switch (st) {
        case JSON_STORE_OK:
            break;

        case JSON_STORE_OOM:
            // The file is very probably intact - there was simply no RAM to
            // read it into. Leave it exactly as it is and report the failure,
            // rather than writing defaults over a configuration that a later
            // attempt would have loaded fine.
            return false;

        case JSON_STORE_MISSING:
        case JSON_STORE_EMPTY:
        case JSON_STORE_CORRUPT:
        default:
            // The boot configuration is the one file the device cannot come up
            // without, so anything unusable here is replaced with the factory
            // set and written back immediately. That costs an operator a
            // corrupt config.json they might have wanted to inspect, and buys
            // a station that always boots into a reachable web admin instead
            // of one that needs a serial flash to recover.
            ESP_LOGW(TAG, "%s unusable, writing defaults", CONFIG_PATH);
            app_config_set_defaults(&g_config);
            return app_config_save();
    }

    config_from_json(doc, &g_config);
    cJSON_Delete(doc);
    ESP_LOGI(TAG, "Configuration loaded");
    return true;
}

bool app_config_factory_reset(void) {
    // Web-reachable (/default) at runtime, so it rewrites the whole g_config
    // out from under the beacon/igate/message tasks. Hold the data lock across
    // the wholesale rewrite so no reader can sample a half-reset struct; drop
    // it before the (slow, self-locking) flash save.
    app_config_lock();
    app_config_set_defaults(&g_config);
    app_config_unlock();
    return app_config_save();
}

uint8_t app_config_path_hop_count(uint8_t pathBitmask, const char pathPreset[4][72]) {
    uint8_t hops = 0;
    for (int bit = 0; bit < 4; bit++) {
        if (!(pathBitmask & (1u << bit)) || !pathPreset[bit][0])
            continue;
        hops++; // the alias itself is at least 1 hop
        for (const char *p = pathPreset[bit]; *p; p++)
            if (*p == ',')
                hops++;
    }
    return hops;
}

uint8_t app_config_path_mask_clamp(uint8_t pathBitmask, const char pathPreset[4][72]) {
    if (app_config_path_hop_count(pathBitmask, pathPreset) <= 8)
        return pathBitmask; // within budget already - nothing to drop

    // Over AX.25's 8-via limit once every selected preset's own comma-joined
    // hops are counted (e.g. two 4-hop presets can exceed 8 well before all 4
    // checkboxes are ticked). Keep presets low-bit-first until the budget is
    // used up and drop the rest, instead of silently saving a bitmask that
    // would later be clipped differently (or reach further than intended) at
    // transmit time - see aprs_path_build_suffix(), which enforces the same
    // limit again as a belt-and-suspenders check for a configuration that
    // reached the device without passing through this form.
    uint8_t clamped = 0;
    uint8_t hopsUsed = 0;
    for (int bit = 0; bit < 4; bit++) {
        if (!(pathBitmask & (1u << bit)) || !pathPreset[bit][0])
            continue;

        uint8_t presetHops = 1;
        for (const char *p = pathPreset[bit]; *p; p++)
            if (*p == ',')
                presetHops++;

        if (hopsUsed + presetHops > 8) {
            ESP_LOGW(TAG, "path bitmask 0x%02X exceeds AX.25 8-hop limit, dropping preset %d and beyond", pathBitmask, bit + 1);
            break;
        }
        clamped |= (uint8_t)(1u << bit);
        hopsUsed += presetHops;
    }
    return clamped;
}
