/**
 * @file app_config.c
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
 * @brief Persistent application configuration: defaults, load/save of
 * /storage/config.json on LittleFS (via cJSON) and the global g_config instance
 * shared by every component and web admin page.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

#include "app_config.h"
#include "cJSON.h"
// MODEM_PTT_GPIO / MODEM_PTT_ACTIVE_HIGH: the board's PTT wiring. Both are
// #ifndef-guarded in that header and are overridden from the top-level
// CMakeLists.txt, which is the single source of truth for them - the factory
// default for g_config.rf_ptt_gpio / .rf_ptt_active is derived from these so
// the two can never disagree. See app_config_set_defaults().
#include "esp32idf_radioamateur_modem_config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

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
// the filesystem's own metadata - the intermittent memset/double-exception
// "save crash" was NOT this race but the stdio buffer allocation on the first
// write; see the setvbuf() note in app_config_save(). This mutex is kept
// because serializing saves is still correct, avoids wasted flash writes, and
// lets app_config_save() safely reuse a single static stdio buffer.) Created
// lazily so this file has no init-order dependency.
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

    c->wifi_mode = 2; // AP_STA equivalent default (matches original shipping as AP)
    c->wifi_power = 20;
    for (int i = 0; i < WIFI_STA_NUM; i++) {
        c->wifi_sta[i].enable = false;
        set_str(c->wifi_sta[i].wifi_ssid, sizeof(c->wifi_sta[i].wifi_ssid), "WIFI_AP");
        set_str(c->wifi_sta[i].wifi_pass, sizeof(c->wifi_sta[i].wifi_pass), "");
    }
    c->wifi_ap_ch = 1;
    set_str(c->wifi_ap_ssid, sizeof(c->wifi_ap_ssid), "esp32idf_APRS");
    set_str(c->wifi_ap_pass, sizeof(c->wifi_ap_pass), "esp32idf_APRS");

    c->rf_en = false;
    c->rf_type = RF_SX1278;
    c->freq_rx = 144.800f;
    c->freq_tx = 144.800f;

    // IGATE
    c->igate_en = true;
    c->rf2inet = true;
    c->inet2rf = false;
    c->igate_loc2rf = false;
    c->igate_loc2inet = true;
    c->aprs_ssid = 10;
    c->aprs_port = 14580;
    set_str(c->aprs_mycall, sizeof(c->aprs_mycall), "NOCALL");
    set_str(c->aprs_passcode, sizeof(c->aprs_passcode), "-1");
    set_str(c->aprs_host, sizeof(c->aprs_host), "aprs.dprns.com");
    set_str(c->aprs_filter, sizeof(c->aprs_filter), "");
    c->igate_bcn = true;
    c->igate_gps = false;
    c->igate_lat = 0.000f;
    c->igate_lon = 0.000f;
    c->igate_alt = 0;
    c->igate_interval = 30;
    set_str(c->igate_symbol, sizeof(c->igate_symbol), "N&");
    c->igate_path = ACTIVATE_IGATE;
    set_str(c->igate_comment, sizeof(c->igate_comment), "esp32idf_APRS IGate");
    c->igate_sts_interval = 0;
    c->igate_phg_power = 1;
    c->igate_phg_gain = 6.0f;
    c->igate_phg_height = 10;
    c->igate_phg_dir = 0;
    c->rf2inetFilter = IGATE_FILT_MESSAGE | IGATE_FILT_STATUS | IGATE_FILT_TELEMETRY | IGATE_FILT_WEATHER | IGATE_FILT_OBJECT | IGATE_FILT_ITEM |
                       IGATE_FILT_QUERY | IGATE_FILT_BUOY | IGATE_FILT_POSITION;
    c->inet2rfFilter = IGATE_FILT_MESSAGE;

    // DIGI
    c->digi_en = false;
    c->digi_ssid = 1;
    set_str(c->digi_mycall, sizeof(c->digi_mycall), "NOCALL");
    c->digi_path = ACTIVATE_DIGI;
    c->digi_delay = 0;
    c->digi_bcn = true;
    c->digi_interval = 30;
    set_str(c->digi_symbol, sizeof(c->digi_symbol), "N&");
    set_str(c->digi_comment, sizeof(c->digi_comment), "esp32idf_APRS Digi");

    // TRACKER
    c->trk_en = false;
    c->trk_ssid = 9;
    set_str(c->trk_mycall, sizeof(c->trk_mycall), "NOCALL");
    c->trk_path = ACTIVATE_TRACKER;
    c->trk_gps = true;
    c->trk_interval = 60;
    c->trk_smartbeacon = false;
    c->trk_compress = false;
    c->trk_hspeed = 120;
    c->trk_lspeed = 2;
    c->trk_maxinterval = 15;
    c->trk_mininterval = 5;
    c->trk_minangle = 25;
    c->trk_slowinterval = 600;
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
    for (int i = 0; i < WX_SENSOR_NUM; i++) {
        c->wx_sensor_enable[i] = false;
        c->wx_sensor_avg[i] = false;
        c->wx_sensor_ch[i] = 0;
    }
    c->wx_sensor_enable[WX_FIELD_WIND_DIRECTION] = true;
    c->wx_sensor_enable[WX_FIELD_WIND_SPEED] = true;
    c->wx_sensor_enable[WX_FIELD_WIND_GUST] = true;
    c->wx_sensor_enable[WX_FIELD_TEMPERATURE] = true;
    c->wx_sensor_enable[WX_FIELD_HUMIDITY] = true;
    c->wx_sensor_enable[WX_FIELD_PRESSURE] = true;

    // Telemetry defaults
    c->tlm0_data_interval = 600;
    c->tlm0_info_interval = 3600;
    c->tlm1_data_interval = 600;
    c->tlm1_info_interval = 3600;

    // AFSK / TNC
    c->audio_modem_en = true;
    c->audio_lpf = true;
    c->preamble = 300;
    c->modem_type = 0;
    c->afsk_modem_type = 1; // default 1200 Bd (AFSK/Bell202) - standard APRS audio modem
    c->fx25_mode = 0;
    c->tx_timeslot = 2000;
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
    // The modem component takes its ADC/DAC pins, ADC attenuation and LED pins
    // as compile-time constants (MODEM_* macros, set in the top-level
    // CMakeLists.txt). PTT is the one pin it accepts at runtime, so it is the
    // one pin still stored here - see aprs_service_build_modem_config().
    //
    // The factory default is taken straight from the same macros so there is
    // exactly ONE source of truth for the board's PTT wiring: change it in
    // CMakeLists.txt and both the component default and this factory default
    // move together. MODEM_PTT_ACTIVE_HIGH is 0/1; rf_ptt_active is the same
    // polarity flag (true = active high).
    c->rf_ptt_gpio = (int8_t)MODEM_PTT_GPIO;
    c->rf_ptt_active = (MODEM_PTT_ACTIVE_HIGH != 0);

    // Message
    c->msg_enable = true;
    set_str(c->msg_mycall, sizeof(c->msg_mycall), "NOCALL");
    c->msg_path = 9;
    c->msg_rf = true;
    c->msg_inet = true;
    c->msg_encrypt = false;
    set_str(c->msg_key, sizeof(c->msg_key), "8EC8233E91D59B0164C24E771BA66307");
    c->msg_retry = 3;
    c->msg_interval = 30;
    c->msg_alarm_enable = false; // disabled by default
    c->msg_alarm_gpio = -1;
}

// ---- streaming JSON writer -----------------------------------------------
// The configuration is serialized by writing tokens straight to the open
// config file, one field at a time, instead of first building a cJSON tree of
// the whole config in RAM and then printing that tree into a second full-size
// string buffer. On this device's small, fragmentable heap that old
// double-allocation (hundreds of tiny cJSON nodes, ~40+ KB, plus a ~7 KB
// contiguous print buffer, all live at once) was the single largest memory
// event in the firmware and was what drove the "minimum free heap" watermark
// down to a few KB after every save. Streaming keeps the extra RAM used during
// a save to essentially just littlefs's own write buffer.
//
// The schema has only one object level and single-level (scalar) arrays, so a
// single "need a comma before the next item" flag for each context is enough.
typedef struct {
    FILE *f;
    bool obj_comma; // a member has already been written at object level
    bool arr_comma; // an element has already been written in the current array
} jw_t;

// Emit a JSON string literal (quotes + minimal escaping), matching cJSON's
// unformatted escaping rules so values round-trip through cJSON_Parse on load.
static void jw_str_val(jw_t *w, const char *v) {
    fputc('"', w->f);
    if (v) {
        for (const unsigned char *p = (const unsigned char *)v; *p; p++) {
            unsigned char ch = *p;
            switch (ch) {
                case '"':  fputs("\\\"", w->f); break;
                case '\\': fputs("\\\\", w->f); break;
                case '\b': fputs("\\b", w->f); break;
                case '\f': fputs("\\f", w->f); break;
                case '\n': fputs("\\n", w->f); break;
                case '\r': fputs("\\r", w->f); break;
                case '\t': fputs("\\t", w->f); break;
                default:
                    if (ch < 0x20)
                        fprintf(w->f, "\\u%04x", ch);
                    else
                        fputc(ch, w->f);
            }
        }
    }
    fputc('"', w->f);
}

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
    jw_str_val(w, k);
    fputc(':', w->f);
}

// Object members - identical call signatures to the old cJSON helpers, so the
// hundreds of jadd_*(d, "key", value) call sites below are unchanged.
static void jadd_str(jw_t *o, const char *k, const char *v) {
    jw_key(o, k);
    jw_str_val(o, v ? v : "");
}
static void jadd_num(jw_t *o, const char *k, double v) {
    jw_key(o, k);
    jw_num_val(o, v);
}
static void jadd_bool(jw_t *o, const char *k, bool v) {
    jw_key(o, k);
    fputs(v ? "true" : "false", o->f);
}

// Scalar arrays.
static void jarr_begin(jw_t *o, const char *k) {
    jw_key(o, k);
    fputc('[', o->f);
    o->arr_comma = false;
}
static void jarr_end(jw_t *o) {
    fputc(']', o->f);
}
static void jarr_num(jw_t *o, double v) {
    if (o->arr_comma)
        fputc(',', o->f);
    o->arr_comma = true;
    jw_num_val(o, v);
}
static void jarr_str(jw_t *o, const char *v) {
    if (o->arr_comma)
        fputc(',', o->f);
    o->arr_comma = true;
    jw_str_val(o, v ? v : "");
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
    jadd_num(d, "txTimeSlot", c->tx_timeslot);
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
    jadd_bool(d, "rfEnable", c->rf_en);
    jadd_num(d, "rfType", c->rf_type);
    jadd_num(d, "rfModem", c->modem_type);
    jadd_num(d, "afskModem", c->afsk_modem_type);
    jadd_num(d, "rfPreamble", c->preamble);
    jadd_num(d, "rfFreqRX", c->freq_rx);
    jadd_num(d, "rfFreqTX", c->freq_tx);
    jadd_num(d, "rfToneRX", c->tone_rx);
    jadd_num(d, "rfToneTX", c->tone_tx);
    jadd_bool(d, "audioModemEn", c->audio_modem_en);
    jadd_bool(d, "audioLPF", c->audio_lpf);

    jadd_bool(d, "igateEn", c->igate_en);
    jadd_bool(d, "igateBcn", c->igate_bcn);
    jadd_bool(d, "rf2inet", c->rf2inet);
    jadd_bool(d, "inet2rf", c->inet2rf);
    jadd_bool(d, "igatePos2rf", c->igate_loc2rf);
    jadd_bool(d, "igatePos2inet", c->igate_loc2inet);
    jadd_num(d, "rf2inetFilter", c->rf2inetFilter);
    jadd_num(d, "inet2rfFilter", c->inet2rfFilter);
    jadd_num(d, "igateSSID", c->aprs_ssid);
    jadd_num(d, "igatePort", c->aprs_port);
    jadd_str(d, "igateMycall", c->aprs_mycall);
    jadd_bool(d, "igateUseStation", c->igate_use_station);
    jadd_str(d, "igatePasscode", c->aprs_passcode);
    jadd_str(d, "igateHost", c->aprs_host);
    jadd_str(d, "igateFilter", c->aprs_filter);
    jadd_bool(d, "igateGPS", c->igate_gps);
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
    jadd_num(d, "igatePHGPower", c->igate_phg_power);
    jadd_num(d, "igatePHGGain", c->igate_phg_gain);
    jadd_num(d, "igatePHGHeight", c->igate_phg_height);
    jadd_num(d, "igatePHGDir", c->igate_phg_dir);
    jarr_begin(d, "igateTlmAvg");
    for (int i = 0; i < TLM_CH; i++)
        jarr_bool(d, c->igate_tlm_avg[i]);
    jarr_end(d);
    jarr_begin(d, "igateTlmSen");
    for (int i = 0; i < TLM_CH; i++)
        jarr_num(d, c->igate_tlm_sensor[i]);
    jarr_end(d);
    jarr_begin(d, "igateTlmPrec");
    for (int i = 0; i < TLM_CH; i++)
        jarr_num(d, c->igate_tlm_precision[i]);
    jarr_end(d);
    jarr_begin(d, "igateTlmOffset");
    for (int i = 0; i < TLM_CH; i++)
        jarr_num(d, c->igate_tlm_offset[i]);
    jarr_end(d);
    jarr_begin(d, "igateTlmPARM");
    for (int i = 0; i < TLM_CH; i++)
        jarr_str(d, c->igate_tlm_PARM[i]);
    jarr_end(d);
    jarr_begin(d, "igateTlmUNIT");
    for (int i = 0; i < TLM_CH; i++)
        jarr_str(d, c->igate_tlm_UNIT[i]);
    jarr_end(d);

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
    jadd_num(d, "digiAlt", c->digi_alt);
    jadd_bool(d, "digiGPS", c->digi_gps);
    jadd_num(d, "digiLAT", c->digi_lat);
    jadd_num(d, "digiLON", c->digi_lon);
    jadd_num(d, "digiINV", c->digi_interval);
    jadd_str(d, "digiSymbol", c->digi_symbol);
    jadd_str(d, "digiPHG", c->digi_phg);
    jadd_str(d, "digiComment", c->digi_comment);
    jadd_num(d, "digiSTSIntv", c->digi_sts_interval);
    jadd_str(d, "digiStatus", c->digi_status);
    jarr_begin(d, "digiTlmAvg");
    for (int i = 0; i < TLM_CH; i++)
        jarr_bool(d, c->digi_tlm_avg[i]);
    jarr_end(d);
    jarr_begin(d, "digiTlmSen");
    for (int i = 0; i < TLM_CH; i++)
        jarr_num(d, c->digi_tlm_sensor[i]);
    jarr_end(d);
    jarr_begin(d, "digiTlmPrec");
    for (int i = 0; i < TLM_CH; i++)
        jarr_num(d, c->digi_tlm_precision[i]);
    jarr_end(d);
    jarr_begin(d, "digiTlmOffset");
    for (int i = 0; i < TLM_CH; i++)
        jarr_num(d, c->digi_tlm_offset[i]);
    jarr_end(d);
    jarr_begin(d, "digiTlmPARM");
    for (int i = 0; i < TLM_CH; i++)
        jarr_str(d, c->digi_tlm_PARM[i]);
    jarr_end(d);
    jarr_begin(d, "digiTlmUNIT");
    for (int i = 0; i < TLM_CH; i++)
        jarr_str(d, c->digi_tlm_UNIT[i]);
    jarr_end(d);

    jadd_bool(d, "trkEn", c->trk_en);
    jadd_bool(d, "trkPos2rf", c->trk_loc2rf);
    jadd_bool(d, "trkPos2inet", c->trk_loc2inet);
    jadd_bool(d, "trkTime", c->trk_timestamp);
    jadd_num(d, "trkSSID", c->trk_ssid);
    jadd_str(d, "trkMycall", c->trk_mycall);
    jadd_bool(d, "trkUseStation", c->trk_use_station);
    jadd_num(d, "trkPath", c->trk_path);
    jadd_bool(d, "trkGPS", c->trk_gps);
    jadd_num(d, "trkLAT", c->trk_lat);
    jadd_num(d, "trkLON", c->trk_lon);
    jadd_num(d, "trkALT", c->trk_alt);
    jadd_num(d, "trkINV", c->trk_interval);
    jadd_bool(d, "trkSmart", c->trk_smartbeacon);
    jadd_bool(d, "trkCompress", c->trk_compress);
    jadd_bool(d, "trkOptAlt", c->trk_altitude);
    jadd_bool(d, "trkLog", c->trk_log);
    jadd_bool(d, "trkOptRSSI", c->trk_rssi);
    jadd_num(d, "trkLSpeed", c->trk_lspeed);
    jadd_num(d, "trkHSpeed", c->trk_hspeed);
    jadd_num(d, "trkMaxInv", c->trk_maxinterval);
    jadd_num(d, "trkMinInv", c->trk_mininterval);
    jadd_num(d, "trkMinDir", c->trk_minangle);
    jadd_num(d, "trkSlowInv", c->trk_slowinterval);
    jadd_str(d, "trkSymbol", c->trk_symbol);
    jadd_str(d, "trkSymbolMove", c->trk_symmove);
    jadd_str(d, "trkSymbolStop", c->trk_symstop);
    jadd_str(d, "trkItem", c->trk_item);
    jadd_str(d, "trkComment", c->trk_comment);
    jadd_num(d, "trkSTSIntv", c->trk_sts_interval);
    jadd_str(d, "trkStatus", c->trk_status);
    jarr_begin(d, "trkTlmAvg");
    for (int i = 0; i < TLM_CH; i++)
        jarr_bool(d, c->trk_tlm_avg[i]);
    jarr_end(d);
    jarr_begin(d, "trkTlmSen");
    for (int i = 0; i < TLM_CH; i++)
        jarr_num(d, c->trk_tlm_sensor[i]);
    jarr_end(d);
    jarr_begin(d, "trkTlmPrec");
    for (int i = 0; i < TLM_CH; i++)
        jarr_num(d, c->trk_tlm_precision[i]);
    jarr_end(d);
    jarr_begin(d, "trkTlmOffset");
    for (int i = 0; i < TLM_CH; i++)
        jarr_num(d, c->trk_tlm_offset[i]);
    jarr_end(d);
    jarr_begin(d, "trkTlmPARM");
    for (int i = 0; i < TLM_CH; i++)
        jarr_str(d, c->trk_tlm_PARM[i]);
    jarr_end(d);
    jarr_begin(d, "trkTlmUNIT");
    for (int i = 0; i < TLM_CH; i++)
        jarr_str(d, c->trk_tlm_UNIT[i]);
    jarr_end(d);

    jadd_bool(d, "wxEn", c->wx_en);
    jadd_bool(d, "wxTx2rf", c->wx_2rf);
    jadd_bool(d, "wxTx2inet", c->wx_2inet);
    jadd_bool(d, "wxTime", c->wx_timestamp);
    jadd_num(d, "wxSSID", c->wx_ssid);
    jadd_str(d, "wxMycall", c->wx_mycall);
    jadd_bool(d, "wxUseStation", c->wx_use_station);
    jadd_num(d, "wxPath", c->wx_path);
    jadd_bool(d, "wxGPS", c->wx_gps);
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
    jarr_begin(d, "wxSenCH");
    for (int i = 0; i < WX_SENSOR_NUM; i++)
        jarr_num(d, c->wx_sensor_ch[i]);
    jarr_end(d);

    // Telemetry ch0/ch1
    for (int ch = 0; ch < 2; ch++) {
        const char *pfx = ch == 0 ? "tlm0" : "tlm1";
        char key[24];
        bool en = ch == 0 ? c->tlm0_en : c->tlm1_en;
        bool rf = ch == 0 ? c->tlm0_2rf : c->tlm1_2rf;
        bool inet = ch == 0 ? c->tlm0_2inet : c->tlm1_2inet;
        uint8_t ssid = ch == 0 ? c->tlm0_ssid : c->tlm1_ssid;
        const char *mycall = ch == 0 ? c->tlm0_mycall : c->tlm1_mycall;
        uint8_t path = ch == 0 ? c->tlm0_path : c->tlm1_path;
        uint16_t info_iv = ch == 0 ? c->tlm0_info_interval : c->tlm1_info_interval;
        uint16_t data_iv = ch == 0 ? c->tlm0_data_interval : c->tlm1_data_interval;
        snprintf(key, sizeof(key), "%sEn", pfx);
        jadd_bool(d, key, en);
        snprintf(key, sizeof(key), "%sTx2rf", pfx);
        jadd_bool(d, key, rf);
        snprintf(key, sizeof(key), "%sTx2inet", pfx);
        jadd_bool(d, key, inet);
        snprintf(key, sizeof(key), "%sSSID", pfx);
        jadd_num(d, key, ssid);
        snprintf(key, sizeof(key), "%sMycall", pfx);
        jadd_str(d, key, mycall);
        snprintf(key, sizeof(key), "%sPath", pfx);
        jadd_num(d, key, path);
        snprintf(key, sizeof(key), "%sInfoInv", pfx);
        jadd_num(d, key, info_iv);
        snprintf(key, sizeof(key), "%sDataInv", pfx);
        jadd_num(d, key, data_iv);
        {
            const char(*P)[10] = ch == 0 ? c->tlm0_PARM : c->tlm1_PARM;
            const char(*U)[8] = ch == 0 ? c->tlm0_UNIT : c->tlm1_UNIT;
            snprintf(key, sizeof(key), "%sPARM", pfx);
            jarr_begin(d, key);
            for (int i = 0; i < TLM_PARM_NUM; i++)
                jarr_str(d, P[i]);
            jarr_end(d);
            snprintf(key, sizeof(key), "%sUNIT", pfx);
            jarr_begin(d, key);
            for (int i = 0; i < TLM_PARM_NUM; i++)
                jarr_str(d, U[i]);
            jarr_end(d);
        }
    }

    jadd_str(d, "httpUser", c->http_username);
    jadd_str(d, "httpPass", c->http_password);
    jarr_begin(d, "path");
    for (int i = 0; i < 4; i++)
        jarr_str(d, c->path[i]);
    jarr_end(d);

    jadd_num(d, "rfPTT", c->rf_ptt_gpio);
    jadd_bool(d, "rfPTTAct", c->rf_ptt_active);

    jadd_num(d, "logFile", c->log);
    jadd_str(d, "hostName", c->host_name);
    jadd_num(d, "resetTimeout", c->reset_timeout);

    jadd_bool(d, "msgEnable", c->msg_enable);
    jadd_str(d, "msgMycall", c->msg_mycall);
    jadd_bool(d, "msgUseStation", c->msg_use_station);
    jadd_bool(d, "msgRf", c->msg_rf);
    jadd_bool(d, "msgInet", c->msg_inet);
    jadd_num(d, "msgPath", c->msg_path);
    jadd_bool(d, "msgEncrypt", c->msg_encrypt);
    jadd_str(d, "msgAESKey", c->msg_key);
    jadd_num(d, "msgRetry", c->msg_retry);
    jadd_num(d, "msgInterval", c->msg_interval);
    jadd_bool(d, "msgAlarmEn", c->msg_alarm_enable);
    jadd_num(d, "msgAlarmGpio", c->msg_alarm_gpio);

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
    c->tx_timeslot = (uint16_t)jget_num(d, "txTimeSlot", def.tx_timeslot);
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
    c->rf_en = jget_bool(d, "rfEnable", def.rf_en);
#ifndef ENABLE_RF_MODULE
    c->rf_en = false;
#endif
    c->rf_type = (uint8_t)jget_num(d, "rfType", def.rf_type);
    c->modem_type = (uint8_t)jget_num(d, "rfModem", def.modem_type);
    c->afsk_modem_type = (uint8_t)jget_num(d, "afskModem", def.afsk_modem_type);
    c->preamble = (uint16_t)jget_num(d, "rfPreamble", def.preamble);
    c->freq_rx = (float)jget_num(d, "rfFreqRX", def.freq_rx);
    c->freq_tx = (float)jget_num(d, "rfFreqTX", def.freq_tx);
    c->tone_rx = (int)jget_num(d, "rfToneRX", def.tone_rx);
    c->tone_tx = (int)jget_num(d, "rfToneTX", def.tone_tx);
    c->audio_modem_en = jget_bool(d, "audioModemEn", def.audio_modem_en);
    c->audio_lpf = jget_bool(d, "audioLPF", def.audio_lpf);

    c->igate_en = jget_bool(d, "igateEn", def.igate_en);
    c->igate_bcn = jget_bool(d, "igateBcn", def.igate_bcn);
    c->rf2inet = jget_bool(d, "rf2inet", def.rf2inet);
    c->inet2rf = jget_bool(d, "inet2rf", def.inet2rf);
    c->igate_loc2rf = jget_bool(d, "igatePos2rf", def.igate_loc2rf);
    c->igate_loc2inet = jget_bool(d, "igatePos2inet", def.igate_loc2inet);
    c->rf2inetFilter = (uint16_t)jget_num(d, "rf2inetFilter", def.rf2inetFilter);
    // "inet2rfFiltger" was a legacy misspelling of the key used when saving;
    // fall back to it so configs written by older firmware still load correctly.
    c->inet2rfFilter =
        (uint16_t)jget_num(d, "inet2rfFilter", (double)jget_num(d, "inet2rfFiltger", def.inet2rfFilter));
    c->aprs_ssid = (uint8_t)jget_num(d, "igateSSID", def.aprs_ssid);
    c->aprs_port = (uint16_t)jget_num(d, "igatePort", def.aprs_port);
    set_str(c->aprs_mycall, sizeof(c->aprs_mycall), jget_str(d, "igateMycall", def.aprs_mycall));
    c->igate_use_station = jget_bool(d, "igateUseStation", def.igate_use_station);
    set_str(c->aprs_passcode, sizeof(c->aprs_passcode), jget_str(d, "igatePasscode", def.aprs_passcode));
    set_str(c->aprs_host, sizeof(c->aprs_host), jget_str(d, "igateHost", def.aprs_host));
    set_str(c->aprs_filter, sizeof(c->aprs_filter), jget_str(d, "igateFilter", def.aprs_filter));
    c->igate_gps = jget_bool(d, "igateGPS", def.igate_gps);
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
    c->igate_phg_power = (uint16_t)jget_num(d, "igatePHGPower", def.igate_phg_power);
    c->igate_phg_gain = (float)jget_num(d, "igatePHGGain", def.igate_phg_gain);
    c->igate_phg_height = (uint16_t)jget_num(d, "igatePHGHeight", def.igate_phg_height);
    c->igate_phg_dir = (uint8_t)jget_num(d, "igatePHGDir", def.igate_phg_dir);
    c->igate_sts_interval = (uint16_t)jget_num(d, "igateSTSIntv", def.igate_sts_interval);
    set_str(c->igate_status, sizeof(c->igate_status), jget_str(d, "igateStatus", def.igate_status));
    {
        cJSON *a1 = cJSON_GetObjectItemCaseSensitive(d, "igateTlmAvg"), *a2 = cJSON_GetObjectItemCaseSensitive(d, "igateTlmSen");
        cJSON *a3 = cJSON_GetObjectItemCaseSensitive(d, "igateTlmPrec"), *a4 = cJSON_GetObjectItemCaseSensitive(d, "igateTlmOffset");
        cJSON *a5 = cJSON_GetObjectItemCaseSensitive(d, "igateTlmPARM"), *a6 = cJSON_GetObjectItemCaseSensitive(d, "igateTlmUNIT");
        for (int i = 0; i < TLM_CH; i++) {
            cJSON *v;
            if (a1 && (v = cJSON_GetArrayItem(a1, i)))
                c->igate_tlm_avg[i] = cJSON_IsTrue(v);
            if (a2 && (v = cJSON_GetArrayItem(a2, i)))
                c->igate_tlm_sensor[i] = (uint8_t)v->valuedouble;
            if (a3 && (v = cJSON_GetArrayItem(a3, i)))
                c->igate_tlm_precision[i] = (uint8_t)v->valuedouble;
            if (a4 && (v = cJSON_GetArrayItem(a4, i)))
                c->igate_tlm_offset[i] = (float)v->valuedouble;
            if (a5 && (v = cJSON_GetArrayItem(a5, i)) && cJSON_IsString(v))
                set_str(c->igate_tlm_PARM[i], sizeof(c->igate_tlm_PARM[i]), v->valuestring);
            if (a6 && (v = cJSON_GetArrayItem(a6, i)) && cJSON_IsString(v))
                set_str(c->igate_tlm_UNIT[i], sizeof(c->igate_tlm_UNIT[i]), v->valuestring);
        }
    }

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
    c->digi_alt = (float)jget_num(d, "digiAlt", def.digi_alt);
    c->digi_gps = jget_bool(d, "digiGPS", def.digi_gps);
    c->digi_lat = (float)jget_num(d, "digiLAT", def.digi_lat);
    c->digi_lon = (float)jget_num(d, "digiLON", def.digi_lon);
    c->digi_interval = (uint16_t)jget_num(d, "digiINV", def.digi_interval);
    set_str(c->digi_symbol, sizeof(c->digi_symbol), jget_str(d, "digiSymbol", def.digi_symbol));
    set_str(c->digi_phg, sizeof(c->digi_phg), jget_str(d, "digiPHG", def.digi_phg));
    set_str(c->digi_comment, sizeof(c->digi_comment), jget_str(d, "digiComment", def.digi_comment));
    c->digi_sts_interval = (uint16_t)jget_num(d, "digiSTSIntv", def.digi_sts_interval);
    set_str(c->digi_status, sizeof(c->digi_status), jget_str(d, "digiStatus", def.digi_status));
    {
        cJSON *a1 = cJSON_GetObjectItemCaseSensitive(d, "digiTlmAvg"), *a2 = cJSON_GetObjectItemCaseSensitive(d, "digiTlmSen");
        cJSON *a3 = cJSON_GetObjectItemCaseSensitive(d, "digiTlmPrec"), *a4 = cJSON_GetObjectItemCaseSensitive(d, "digiTlmOffset");
        cJSON *a5 = cJSON_GetObjectItemCaseSensitive(d, "digiTlmPARM"), *a6 = cJSON_GetObjectItemCaseSensitive(d, "digiTlmUNIT");
        for (int i = 0; i < TLM_CH; i++) {
            cJSON *v;
            if (a1 && (v = cJSON_GetArrayItem(a1, i)))
                c->digi_tlm_avg[i] = cJSON_IsTrue(v);
            if (a2 && (v = cJSON_GetArrayItem(a2, i)))
                c->digi_tlm_sensor[i] = (uint8_t)v->valuedouble;
            if (a3 && (v = cJSON_GetArrayItem(a3, i)))
                c->digi_tlm_precision[i] = (uint8_t)v->valuedouble;
            if (a4 && (v = cJSON_GetArrayItem(a4, i)))
                c->digi_tlm_offset[i] = (float)v->valuedouble;
            if (a5 && (v = cJSON_GetArrayItem(a5, i)) && cJSON_IsString(v))
                set_str(c->digi_tlm_PARM[i], sizeof(c->digi_tlm_PARM[i]), v->valuestring);
            if (a6 && (v = cJSON_GetArrayItem(a6, i)) && cJSON_IsString(v))
                set_str(c->digi_tlm_UNIT[i], sizeof(c->digi_tlm_UNIT[i]), v->valuestring);
        }
    }

    c->trk_en = jget_bool(d, "trkEn", def.trk_en);
    c->trk_loc2rf = jget_bool(d, "trkPos2rf", def.trk_loc2rf);
    c->trk_loc2inet = jget_bool(d, "trkPos2inet", def.trk_loc2inet);
    c->trk_timestamp = jget_bool(d, "trkTime", def.trk_timestamp);
    c->trk_ssid = (uint8_t)jget_num(d, "trkSSID", def.trk_ssid);
    set_str(c->trk_mycall, sizeof(c->trk_mycall), jget_str(d, "trkMycall", def.trk_mycall));
    c->trk_use_station = jget_bool(d, "trkUseStation", def.trk_use_station);
    c->trk_path = (uint8_t)jget_num(d, "trkPath", def.trk_path);
    c->trk_gps = jget_bool(d, "trkGPS", def.trk_gps);
    c->trk_lat = (float)jget_num(d, "trkLAT", def.trk_lat);
    c->trk_lon = (float)jget_num(d, "trkLON", def.trk_lon);
    c->trk_alt = (float)jget_num(d, "trkALT", def.trk_alt);
    c->trk_interval = (uint16_t)jget_num(d, "trkINV", def.trk_interval);
    c->trk_smartbeacon = jget_bool(d, "trkSmart", def.trk_smartbeacon);
    c->trk_compress = jget_bool(d, "trkCompress", def.trk_compress);
    c->trk_altitude = jget_bool(d, "trkOptAlt", def.trk_altitude);
    c->trk_log = jget_bool(d, "trkLog", def.trk_log);
    c->trk_rssi = jget_bool(d, "trkOptRSSI", def.trk_rssi);
    c->trk_lspeed = (uint8_t)jget_num(d, "trkLSpeed", def.trk_lspeed);
    c->trk_hspeed = (uint16_t)jget_num(d, "trkHSpeed", def.trk_hspeed);
    c->trk_maxinterval = (uint8_t)jget_num(d, "trkMaxInv", def.trk_maxinterval);
    c->trk_mininterval = (uint8_t)jget_num(d, "trkMinInv", def.trk_mininterval);
    c->trk_minangle = (uint8_t)jget_num(d, "trkMinDir", def.trk_minangle);
    c->trk_slowinterval = (uint16_t)jget_num(d, "trkSlowInv", def.trk_slowinterval);
    set_str(c->trk_symbol, sizeof(c->trk_symbol), jget_str(d, "trkSymbol", def.trk_symbol));
    set_str(c->trk_symmove, sizeof(c->trk_symmove), jget_str(d, "trkSymbolMove", def.trk_symmove));
    set_str(c->trk_symstop, sizeof(c->trk_symstop), jget_str(d, "trkSymbolStop", def.trk_symstop));
    set_str(c->trk_item, sizeof(c->trk_item), jget_str(d, "trkItem", def.trk_item));
    set_str(c->trk_comment, sizeof(c->trk_comment), jget_str(d, "trkComment", def.trk_comment));
    c->trk_sts_interval = (uint16_t)jget_num(d, "trkSTSIntv", def.trk_sts_interval);
    set_str(c->trk_status, sizeof(c->trk_status), jget_str(d, "trkStatus", def.trk_status));
    {
        cJSON *a1 = cJSON_GetObjectItemCaseSensitive(d, "trkTlmAvg"), *a2 = cJSON_GetObjectItemCaseSensitive(d, "trkTlmSen");
        cJSON *a3 = cJSON_GetObjectItemCaseSensitive(d, "trkTlmPrec"), *a4 = cJSON_GetObjectItemCaseSensitive(d, "trkTlmOffset");
        cJSON *a5 = cJSON_GetObjectItemCaseSensitive(d, "trkTlmPARM"), *a6 = cJSON_GetObjectItemCaseSensitive(d, "trkTlmUNIT");
        for (int i = 0; i < TLM_CH; i++) {
            cJSON *v;
            if (a1 && (v = cJSON_GetArrayItem(a1, i)))
                c->trk_tlm_avg[i] = cJSON_IsTrue(v);
            if (a2 && (v = cJSON_GetArrayItem(a2, i)))
                c->trk_tlm_sensor[i] = (uint8_t)v->valuedouble;
            if (a3 && (v = cJSON_GetArrayItem(a3, i)))
                c->trk_tlm_precision[i] = (uint8_t)v->valuedouble;
            if (a4 && (v = cJSON_GetArrayItem(a4, i)))
                c->trk_tlm_offset[i] = (float)v->valuedouble;
            if (a5 && (v = cJSON_GetArrayItem(a5, i)) && cJSON_IsString(v))
                set_str(c->trk_tlm_PARM[i], sizeof(c->trk_tlm_PARM[i]), v->valuestring);
            if (a6 && (v = cJSON_GetArrayItem(a6, i)) && cJSON_IsString(v))
                set_str(c->trk_tlm_UNIT[i], sizeof(c->trk_tlm_UNIT[i]), v->valuestring);
        }
    }

    c->wx_en = jget_bool(d, "wxEn", def.wx_en);
    c->wx_2rf = jget_bool(d, "wxTx2rf", def.wx_2rf);
    c->wx_2inet = jget_bool(d, "wxTx2inet", def.wx_2inet);
    c->wx_timestamp = jget_bool(d, "wxTime", def.wx_timestamp);
    c->wx_ssid = (uint8_t)jget_num(d, "wxSSID", def.wx_ssid);
    set_str(c->wx_mycall, sizeof(c->wx_mycall), jget_str(d, "wxMycall", def.wx_mycall));
    c->wx_use_station = jget_bool(d, "wxUseStation", def.wx_use_station);
    c->wx_path = (uint8_t)jget_num(d, "wxPath", def.wx_path);
    c->wx_gps = jget_bool(d, "wxGPS", def.wx_gps);
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
            if (a3 && (v = cJSON_GetArrayItem(a3, i)))
                c->wx_sensor_ch[i] = (uint8_t)v->valuedouble;
        }
    }

    for (int ch = 0; ch < 2; ch++) {
        const char *pfx = ch == 0 ? "tlm0" : "tlm1";
        char key[24];
        bool *en = ch == 0 ? &c->tlm0_en : &c->tlm1_en;
        bool *rf = ch == 0 ? &c->tlm0_2rf : &c->tlm1_2rf;
        bool *inet = ch == 0 ? &c->tlm0_2inet : &c->tlm1_2inet;
        uint8_t *ssid = ch == 0 ? &c->tlm0_ssid : &c->tlm1_ssid;
        char *mycall = ch == 0 ? c->tlm0_mycall : c->tlm1_mycall;
        uint8_t *path = ch == 0 ? &c->tlm0_path : &c->tlm1_path;
        uint16_t *info_iv = ch == 0 ? &c->tlm0_info_interval : &c->tlm1_info_interval;
        uint16_t *data_iv = ch == 0 ? &c->tlm0_data_interval : &c->tlm1_data_interval;
        snprintf(key, sizeof(key), "%sEn", pfx);
        *en = jget_bool(d, key, ch == 0 ? def.tlm0_en : def.tlm1_en);
        snprintf(key, sizeof(key), "%sTx2rf", pfx);
        *rf = jget_bool(d, key, ch == 0 ? def.tlm0_2rf : def.tlm1_2rf);
        snprintf(key, sizeof(key), "%sTx2inet", pfx);
        *inet = jget_bool(d, key, ch == 0 ? def.tlm0_2inet : def.tlm1_2inet);
        snprintf(key, sizeof(key), "%sSSID", pfx);
        *ssid = (uint8_t)jget_num(d, key, ch == 0 ? def.tlm0_ssid : def.tlm1_ssid);
        snprintf(key, sizeof(key), "%sMycall", pfx);
        set_str(mycall, 10, jget_str(d, key, ch == 0 ? def.tlm0_mycall : def.tlm1_mycall));
        snprintf(key, sizeof(key), "%sPath", pfx);
        *path = (uint8_t)jget_num(d, key, ch == 0 ? def.tlm0_path : def.tlm1_path);
        snprintf(key, sizeof(key), "%sInfoInv", pfx);
        *info_iv = (uint16_t)jget_num(d, key, ch == 0 ? def.tlm0_info_interval : def.tlm1_info_interval);
        snprintf(key, sizeof(key), "%sDataInv", pfx);
        *data_iv = (uint16_t)jget_num(d, key, ch == 0 ? def.tlm0_data_interval : def.tlm1_data_interval);
        snprintf(key, sizeof(key), "%sPARM", pfx);
        cJSON *parm = cJSON_GetObjectItemCaseSensitive(d, key);
        char(*P)[10] = ch == 0 ? c->tlm0_PARM : c->tlm1_PARM;
        snprintf(key, sizeof(key), "%sUNIT", pfx);
        cJSON *unit = cJSON_GetObjectItemCaseSensitive(d, key);
        char(*U)[8] = ch == 0 ? c->tlm0_UNIT : c->tlm1_UNIT;
        for (int i = 0; i < TLM_PARM_NUM; i++) {
            cJSON *v;
            if (parm && (v = cJSON_GetArrayItem(parm, i)) && cJSON_IsString(v))
                set_str(P[i], 10, v->valuestring);
            if (unit && (v = cJSON_GetArrayItem(unit, i)) && cJSON_IsString(v))
                set_str(U[i], 8, v->valuestring);
        }
    }

    set_str(c->http_username, sizeof(c->http_username), jget_str(d, "httpUser", def.http_username));
    set_str(c->http_password, sizeof(c->http_password), jget_str(d, "httpPass", def.http_password));
    {
        cJSON *p = cJSON_GetObjectItemCaseSensitive(d, "path");
        for (int i = 0; i < 4; i++) {
            cJSON *v = p ? cJSON_GetArrayItem(p, i) : NULL;
            set_str(c->path[i], sizeof(c->path[i]), (v && cJSON_IsString(v)) ? v->valuestring : def.path[i]);
        }
    }

    c->rf_ptt_gpio = (int8_t)jget_num(d, "rfPTT", def.rf_ptt_gpio);
    c->rf_ptt_active = jget_bool(d, "rfPTTAct", def.rf_ptt_active);

    c->log = (uint16_t)jget_num(d, "logFile", def.log);
    set_str(c->host_name, sizeof(c->host_name), jget_str(d, "hostName", def.host_name));
    c->reset_timeout = (uint16_t)jget_num(d, "resetTimeout", def.reset_timeout);

    if (!cJSON_GetObjectItemCaseSensitive(d, "msgEnable")) {
        // old-version file compatibility -> keep documented defaults
        c->msg_enable = true;
        c->msg_encrypt = false;
        c->msg_rf = true;
        c->msg_inet = true;
        c->msg_retry = 3;
        c->msg_interval = 30;
        c->msg_path = 9;
        set_str(c->msg_key, sizeof(c->msg_key), "8EC8233E91D59B0164C24E771BA66307");
        set_str(c->msg_mycall, sizeof(c->msg_mycall), "NOCALL");
    } else {
        c->msg_enable = jget_bool(d, "msgEnable", def.msg_enable);
        c->msg_path = (uint8_t)jget_num(d, "msgPath", def.msg_path);
        c->msg_rf = jget_bool(d, "msgRf", def.msg_rf);
        c->msg_inet = jget_bool(d, "msgInet", def.msg_inet);
        c->msg_encrypt = jget_bool(d, "msgEncrypt", def.msg_encrypt);
        c->msg_retry = (uint8_t)jget_num(d, "msgRetry", def.msg_retry);
        c->msg_interval = (uint16_t)jget_num(d, "msgInterval", def.msg_interval);
        set_str(c->msg_key, sizeof(c->msg_key), jget_str(d, "msgAESKey", def.msg_key));
        set_str(c->msg_mycall, sizeof(c->msg_mycall), jget_str(d, "msgMycall", def.msg_mycall));
        c->msg_use_station = jget_bool(d, "msgUseStation", def.msg_use_station);
    }
    c->msg_alarm_enable = jget_bool(d, "msgAlarmEn", def.msg_alarm_enable);
    c->msg_alarm_gpio = (int8_t)jget_num(d, "msgAlarmGpio", def.msg_alarm_gpio);
}

bool app_config_save(void) {
    // Stream the configuration straight to the file, one field at a time. We
    // deliberately do NOT build an in-RAM cJSON document of the whole config
    // and then print it into a second full-size string the way the old path
    // did: on this device's small, fragmentable heap that transient
    // double-allocation was the single largest memory event in the firmware's
    // life, and it was what drove the "minimum free heap" watermark down to a
    // few KB after every webconfig save. Writing token-by-token through the
    // FILE keeps the extra RAM needed for a save to essentially just littlefs's
    // own write buffer.
    // Serialize the whole save against any other save/load in flight (see
    // s_config_mutex comment above). Block indefinitely: a save must never
    // be silently dropped, and the critical section below is short.
    xSemaphoreTake(config_mutex(), portMAX_DELAY);

    FILE *f = fopen(CONFIG_TMP_PATH, "w");
    if (!f) {
        ESP_LOGE(TAG, "open tmp for write failed");
        xSemaphoreGive(config_mutex());
        return false;
    }

    // Give the stream a fixed, known buffer BEFORE the first byte is written.
    // If we don't, newlib lazily allocates the stdio buffer on the first
    // fputc(), and it sizes that buffer from st_blksize - which esp_littlefs
    // reports as the 4096-byte flash block size. On this device's small,
    // fragmented heap that transient malloc(4096) is exactly what made saves
    // crash intermittently: when it can't be satisfied cleanly the stream
    // falls back to the unbuffered per-byte path (__swbuf_r) and a starved
    // heap trips a memset with a corrupted length - the double-exception /
    // corrupted-backtrace panic seen in the field. A small static buffer
    // removes that allocation entirely and, as a bonus, coalesces the
    // hundreds of token writes into a handful of block writes. Static (not on
    // the stack) and safe to reuse: every save is fully serialized by
    // config_mutex(), held across this whole function.
    static char s_save_buf[512];
    setvbuf(f, s_save_buf, _IOFBF, sizeof(s_save_buf));

    jw_t w = { .f = f, .obj_comma = false, .arr_comma = false };
    config_write_json(&w, &g_config);

    // Catch any deferred stdio/filesystem write error before committing the
    // temp file over the live one, so a full or failing filesystem can never
    // leave a truncated config.json in place.
    bool ok = (fflush(f) == 0) && (ferror(f) == 0);
    if (fclose(f) != 0)
        ok = false;
    if (!ok) {
        ESP_LOGE(TAG, "write error while saving config (free heap=%u bytes)",
                 (unsigned)esp_get_free_heap_size());
        remove(CONFIG_TMP_PATH);
        xSemaphoreGive(config_mutex());
        return false;
    }

    remove(CONFIG_PATH);
    if (rename(CONFIG_TMP_PATH, CONFIG_PATH) != 0) {
        ESP_LOGE(TAG, "rename tmp->config failed");
        xSemaphoreGive(config_mutex());
        return false;
    }
    ESP_LOGI(TAG, "Configuration saved");
    // log how close the calling task (normally the
    // httpd task) came to overflowing its stack during this save, so the
    // config.stack_size in web_server.c can be sized from real numbers
    // instead of another guess. Remove once a safe margin is confirmed.
    ESP_LOGI(TAG, "Caller stack high-water mark: %u bytes free",
             (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
    xSemaphoreGive(config_mutex());
    return true;
}

bool app_config_load(void) {
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) {
        ESP_LOGW(TAG, "%s not found, writing defaults", CONFIG_PATH);
        app_config_set_defaults(&g_config);
        return app_config_save();
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        ESP_LOGW(TAG, "config.json empty, writing defaults");
        app_config_set_defaults(&g_config);
        return app_config_save();
    }
    char *buf = malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return false;
    }
    size_t rd = fread(buf, 1, sz, f);
    buf[rd] = 0;
    fclose(f);

    cJSON *doc = cJSON_Parse(buf);
    free(buf);
    if (!doc) {
        ESP_LOGW(TAG, "config.json corrupt, resetting to defaults");
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
