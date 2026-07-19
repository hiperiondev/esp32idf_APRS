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
#include <string.h>

#include "app_config.h"
#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "app_config";
#define CONFIG_PATH     "/storage/config.json"
#define CONFIG_TMP_PATH "/storage/config.json.tmp"

app_config_t g_config;

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
    c->title = true;
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

    c->bt_slave = false;
    c->bt_master = false;
    c->bt_mode = 0;
    set_str(c->bt_name, sizeof(c->bt_name), "esp32idf_APRS");
    c->bt_pin = 1234;
    c->bt_power = 3;

    c->rf_en = false;
    c->rf_type = RF_SX1278;
    c->freq_rx = 144.800f;
    c->freq_tx = 144.800f;
    c->sql_level = 40;
    c->volume = 60;
    c->agc_max_gain = 10; // matches AFSK.c's s_agcMaxGain default
    c->mic = 60;

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

    // OLED / display
    c->oled_enable = true;
    c->oled_timeout = 30;
    c->dim = 0;
    c->contrast = 128;
    c->startup = 0;
    c->dispDelay = 3;
    c->filterDistant = 0;
    c->h_up = true;
    c->tx_display = true;
    c->rx_display = true;
    c->dispRF = true;
    c->dispINET = true;
    c->disp_brightness = 255;

    // AFSK / TNC
    c->audio_modem_en = true;
    c->audio_hpf = false;
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

    // VPN
    c->vpn = false;
    c->wg_port = 51820;

    // System / HTTP auth  (README documented default: admin/admin)
    set_str(c->http_username, sizeof(c->http_username), "admin");
    set_str(c->http_password, sizeof(c->http_password), "admin");
    set_str(c->host_name, sizeof(c->host_name), "esp32idf_APRS");
    c->reset_timeout = 0;
    for (int i = 0; i < 4; i++)
        set_str(c->path[i], sizeof(c->path[i]), "");
    set_str(c->path[0], sizeof(c->path[0]), "WIDE1-1,WIDE2-1");

    // GNSS
    c->gnss_enable = true;
    c->gnss_pps_gpio = -1;
    c->gnss_channel = 0;
    c->gnss_tcp_port = 10110;
    set_str(c->gnss_tcp_host, sizeof(c->gnss_tcp_host), "");

    // RF module GPIO defaults (as shipped for ESP32 DevKit / ESP32DR board)
    c->rf_baudrate = 9600;
    c->rf_tx_gpio = 13;
    c->rf_rx_gpio = 14;
    c->rf_sql_gpio = 27; // GPIO33 is the real (hardwired) ADC audio-input pin and GPIO25 is the real
                         // (hardwired) DAC audio-output pin - see adc_pins[]/DAC_CHAN_0 in
                         // the modem component. Neither SQL nor PTT may be assigned to 25/33,
                         // since pinMode() on either pad would fight the ADC/DAC's analog use of it.
    c->rf_pd_gpio = -1; // never wired into the modem - left disabled by default
    c->rf_pwr_gpio = 12;
    c->rf_ptt_gpio = 26;
    c->rf_sql_active = false;
    c->rf_pd_active = true;
    c->rf_pwr_active = false;
    c->rf_ptt_active = false;
    // NOTE: since the esp32_IDF_libAPRS -> esp32idf_radioamateur_modem swap, the
    // audio modem no longer reads ANY of the pin fields above or below. It takes
    // its ADC/DAC/PTT pins as compile-time constants (MODEM_ADC_GPIO /
    // MODEM_DAC_GPIO / MODEM_PTT_GPIO, defined in the top-level CMakeLists.txt)
    // and has no hardware-squelch or RF-power-switch output at all. They are kept
    // here, and still editable on the "Mod" page, purely so existing config.json
    // files load unchanged and the values survive for any future component that
    // can use them. Change the CMakeLists.txt definitions to move the audio pins.
    c->adc_gpio = 1;
    c->dac_gpio = 18;
    c->adc_sel_gpio = -1;
    c->dac_sel_gpio = 17;
    // ADC_ATTEN_DB_0 (0) only linearly measures ~0-1.1V, but the real DAC output
    // (DAC_CHAN_0/GPIO25) swings the full 0-3.3V rail. Wired straight into GPIO33
    // for the ADC/DAC loopback self-test (or into a radio's discriminator/mic
    // input), a 0dB atten clips/saturates most of that swing, distorting the tone
    // enough that the AFSK demodulator can't lock onto it - this is why the LOOP
    // TEST fails with "no packet was received back" even though ADC33/DAC25 are
    // correctly wired together. adc_atten=4 (ADC_ATTEN_DB_12, Vref=3300) matches
    // the DAC's full ~0-3.3V rail-to-rail swing.
    //
    // Also inert now, and for the same reason as the pins above: the modem
    // component hard-codes MODEM_ADC_ATTEN, which already defaults to
    // ADC_ATTEN_DB_12 - i.e. the value this field held anyway. Override
    // MODEM_ADC_ATTEN from the top-level CMakeLists.txt to change it.
    c->adc_atten = 4;

    c->i2c_enable = false;
    c->i2c_sda_pin = -1;
    c->i2c_sck_pin = -1;
    c->i2c_rst_pin = -1;
    c->i2c_freq = 400000;
    c->i2c1_enable = false;
    c->i2c1_sda_pin = -1;
    c->i2c1_sck_pin = -1;
    c->i2c1_freq = 100000;

    c->onewire_enable = false;
    c->onewire_gpio = -1;

    c->uart0_enable = false;
    c->uart0_tx_gpio = -1;
    c->uart0_rx_gpio = -1;
    c->uart0_rts_gpio = -1;
    c->uart1_enable = false;
    c->uart1_tx_gpio = -1;
    c->uart1_rx_gpio = -1;
    c->uart1_rts_gpio = -1;
    c->uart2_enable = false;
    c->uart2_tx_gpio = -1;
    c->uart2_rx_gpio = -1;

    c->modbus_enable = false;
    c->modbus_channel = -1;
    c->modbus_de_gpio = -1;

    c->counter0_gpio = -1;
    c->counter1_gpio = -1;

    c->ext_tnc_enable = false;
    c->ext_tnc_channel = 0;
    c->ext_tnc_mode = 0;

    c->pwr_gpio = -1;
    c->pwr_active = true;

    c->ppp_enable = false;
    c->ppp_rst_gpio = -1;
    c->ppp_tx_gpio = -1;
    c->ppp_rx_gpio = -1;
    c->ppp_rts_gpio = -1;
    c->ppp_cts_gpio = -1;
    c->ppp_dtr_gpio = -1;
    c->ppp_ri_gpio = -1;
    c->ppp_pwr_gpio = -1;
    c->ppp_rst_delay = 1000;
    c->ppp_serial = 1;
    c->ppp_serial_baudrate = 115200;
    c->ppp_napt = true;

    c->en_mqtt = false;
    c->mqtt_port = 1883;

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
}

// ---- small cJSON helpers -------------------------------------------------
static void jadd_str(cJSON *o, const char *k, const char *v) {
    cJSON_AddStringToObject(o, k, v ? v : "");
}
static void jadd_num(cJSON *o, const char *k, double v) {
    cJSON_AddNumberToObject(o, k, v);
}
static void jadd_bool(cJSON *o, const char *k, bool v) {
    cJSON_AddBoolToObject(o, k, v);
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
static cJSON *config_to_json(const app_config_t *c) {
    cJSON *d = cJSON_CreateObject();
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
    cJSON *wsta = cJSON_CreateArray();
    for (int i = 0; i < WIFI_STA_NUM; i++) {
        cJSON_AddItemToArray(wsta, cJSON_CreateBool(c->wifi_sta[i].enable));
        cJSON_AddItemToArray(wsta, cJSON_CreateString(c->wifi_sta[i].wifi_ssid));
        cJSON_AddItemToArray(wsta, cJSON_CreateString(c->wifi_sta[i].wifi_pass));
    }
    cJSON_AddItemToObject(d, "WiFiSTA", wsta);

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
    jadd_num(d, "rfSql", c->sql_level);
    jadd_num(d, "rfVolume", c->volume);
    jadd_num(d, "agcMaxGain", c->agc_max_gain);
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
    {
        cJSON *a1 = cJSON_CreateArray(), *a2 = cJSON_CreateArray(), *a3 = cJSON_CreateArray(), *a4 = cJSON_CreateArray(), *a5 = cJSON_CreateArray(),
              *a6 = cJSON_CreateArray(), *a7 = cJSON_CreateArray();
        for (int i = 0; i < TLM_CH; i++) {
            cJSON_AddItemToArray(a1, cJSON_CreateBool(c->igate_tlm_avg[i]));
            cJSON_AddItemToArray(a2, cJSON_CreateNumber(c->igate_tlm_sensor[i]));
            cJSON_AddItemToArray(a3, cJSON_CreateNumber(c->igate_tlm_precision[i]));
            cJSON_AddItemToArray(a4, cJSON_CreateNumber(c->igate_tlm_offset[i]));
            cJSON_AddItemToArray(a5, cJSON_CreateString(c->igate_tlm_PARM[i]));
            cJSON_AddItemToArray(a6, cJSON_CreateString(c->igate_tlm_UNIT[i]));
            cJSON_AddItemToArray(a7, cJSON_CreateNumber(c->igate_tlm_EQNS[i][0]));
            cJSON_AddItemToArray(a7, cJSON_CreateNumber(c->igate_tlm_EQNS[i][1]));
            cJSON_AddItemToArray(a7, cJSON_CreateNumber(c->igate_tlm_EQNS[i][2]));
        }
        cJSON_AddItemToObject(d, "igateTlmAvg", a1);
        cJSON_AddItemToObject(d, "igateTlmSen", a2);
        cJSON_AddItemToObject(d, "igateTlmPrec", a3);
        cJSON_AddItemToObject(d, "igateTlmOffset", a4);
        cJSON_AddItemToObject(d, "igateTlmPARM", a5);
        cJSON_AddItemToObject(d, "igateTlmUNIT", a6);
        cJSON_AddItemToObject(d, "igateTlmEQNS", a7);
    }

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
    {
        cJSON *a1 = cJSON_CreateArray(), *a2 = cJSON_CreateArray(), *a3 = cJSON_CreateArray(), *a4 = cJSON_CreateArray(), *a5 = cJSON_CreateArray(),
              *a6 = cJSON_CreateArray(), *a7 = cJSON_CreateArray();
        for (int i = 0; i < TLM_CH; i++) {
            cJSON_AddItemToArray(a1, cJSON_CreateBool(c->digi_tlm_avg[i]));
            cJSON_AddItemToArray(a2, cJSON_CreateNumber(c->digi_tlm_sensor[i]));
            cJSON_AddItemToArray(a3, cJSON_CreateNumber(c->digi_tlm_precision[i]));
            cJSON_AddItemToArray(a4, cJSON_CreateNumber(c->digi_tlm_offset[i]));
            cJSON_AddItemToArray(a5, cJSON_CreateString(c->digi_tlm_PARM[i]));
            cJSON_AddItemToArray(a6, cJSON_CreateString(c->digi_tlm_UNIT[i]));
            cJSON_AddItemToArray(a7, cJSON_CreateNumber(c->digi_tlm_EQNS[i][0]));
            cJSON_AddItemToArray(a7, cJSON_CreateNumber(c->digi_tlm_EQNS[i][1]));
            cJSON_AddItemToArray(a7, cJSON_CreateNumber(c->digi_tlm_EQNS[i][2]));
        }
        cJSON_AddItemToObject(d, "digiTlmAvg", a1);
        cJSON_AddItemToObject(d, "digiTlmSen", a2);
        cJSON_AddItemToObject(d, "digiTlmPrec", a3);
        cJSON_AddItemToObject(d, "digiTlmOffset", a4);
        cJSON_AddItemToObject(d, "digiTlmPARM", a5);
        cJSON_AddItemToObject(d, "digiTlmUNIT", a6);
        cJSON_AddItemToObject(d, "digiTlmEQNS", a7);
    }

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
    jadd_num(d, "trkMicEType", c->trk_mice_type);
    {
        cJSON *a1 = cJSON_CreateArray(), *a2 = cJSON_CreateArray(), *a3 = cJSON_CreateArray(), *a4 = cJSON_CreateArray(), *a5 = cJSON_CreateArray(),
              *a6 = cJSON_CreateArray(), *a7 = cJSON_CreateArray();
        for (int i = 0; i < TLM_CH; i++) {
            cJSON_AddItemToArray(a1, cJSON_CreateBool(c->trk_tlm_avg[i]));
            cJSON_AddItemToArray(a2, cJSON_CreateNumber(c->trk_tlm_sensor[i]));
            cJSON_AddItemToArray(a3, cJSON_CreateNumber(c->trk_tlm_precision[i]));
            cJSON_AddItemToArray(a4, cJSON_CreateNumber(c->trk_tlm_offset[i]));
            cJSON_AddItemToArray(a5, cJSON_CreateString(c->trk_tlm_PARM[i]));
            cJSON_AddItemToArray(a6, cJSON_CreateString(c->trk_tlm_UNIT[i]));
            cJSON_AddItemToArray(a7, cJSON_CreateNumber(c->trk_tlm_EQNS[i][0]));
            cJSON_AddItemToArray(a7, cJSON_CreateNumber(c->trk_tlm_EQNS[i][1]));
            cJSON_AddItemToArray(a7, cJSON_CreateNumber(c->trk_tlm_EQNS[i][2]));
        }
        cJSON_AddItemToObject(d, "trkTlmAvg", a1);
        cJSON_AddItemToObject(d, "trkTlmSen", a2);
        cJSON_AddItemToObject(d, "trkTlmPrec", a3);
        cJSON_AddItemToObject(d, "trkTlmOffset", a4);
        cJSON_AddItemToObject(d, "trkTlmPARM", a5);
        cJSON_AddItemToObject(d, "trkTlmUNIT", a6);
        cJSON_AddItemToObject(d, "trkTlmEQNS", a7);
    }

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
    jadd_num(d, "wxFlage", c->wx_flage);
    jadd_str(d, "wxObject", c->wx_object);
    jadd_str(d, "wxComment", c->wx_comment);
    jadd_num(d, "wxTlmInv", c->wx_tlm_interval);
    {
        cJSON *a1 = cJSON_CreateArray(), *a2 = cJSON_CreateArray(), *a3 = cJSON_CreateArray();
        for (int i = 0; i < WX_SENSOR_NUM; i++) {
            cJSON_AddItemToArray(a1, cJSON_CreateBool(c->wx_sensor_enable[i]));
            cJSON_AddItemToArray(a2, cJSON_CreateBool(c->wx_sensor_avg[i]));
            cJSON_AddItemToArray(a3, cJSON_CreateNumber(c->wx_sensor_ch[i]));
        }
        cJSON_AddItemToObject(d, "wxSenEn", a1);
        cJSON_AddItemToObject(d, "wxSenAvg", a2);
        cJSON_AddItemToObject(d, "wxSenCH", a3);
    }

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
        uint8_t bits = ch == 0 ? c->tlm0_BITS_Active : c->tlm1_BITS_Active;
        const char *comment = ch == 0 ? c->tlm0_comment : c->tlm1_comment;
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
        snprintf(key, sizeof(key), "%sBIT", pfx);
        jadd_num(d, key, bits);
        snprintf(key, sizeof(key), "%sComment", pfx);
        jadd_str(d, key, comment);
        cJSON *eqns = cJSON_CreateArray();
        cJSON *parm = cJSON_CreateArray();
        cJSON *unit = cJSON_CreateArray();
        cJSON *dch = cJSON_CreateArray();
        for (int i = 0; i < TLM_CH; i++) {
            const float(*E)[3] = ch == 0 ? c->tlm0_EQNS : c->tlm1_EQNS;
            cJSON_AddItemToArray(eqns, cJSON_CreateNumber(E[i][0]));
            cJSON_AddItemToArray(eqns, cJSON_CreateNumber(E[i][1]));
            cJSON_AddItemToArray(eqns, cJSON_CreateNumber(E[i][2]));
        }
        for (int i = 0; i < TLM_PARM_NUM; i++) {
            const char(*P)[10] = ch == 0 ? c->tlm0_PARM : c->tlm1_PARM;
            const char(*U)[8] = ch == 0 ? c->tlm0_UNIT : c->tlm1_UNIT;
            const uint8_t *DC = ch == 0 ? c->tml0_data_channel : c->tml1_data_channel;
            cJSON_AddItemToArray(parm, cJSON_CreateString(P[i]));
            cJSON_AddItemToArray(unit, cJSON_CreateString(U[i]));
            cJSON_AddItemToArray(dch, cJSON_CreateNumber(DC[i]));
        }
        snprintf(key, sizeof(key), "%sEQNS", pfx);
        cJSON_AddItemToObject(d, key, eqns);
        snprintf(key, sizeof(key), "%sPARM", pfx);
        cJSON_AddItemToObject(d, key, parm);
        snprintf(key, sizeof(key), "%sUNIT", pfx);
        cJSON_AddItemToObject(d, key, unit);
        snprintf(key, sizeof(key), "%sDataCH", pfx);
        cJSON_AddItemToObject(d, key, dch);
    }

    jadd_bool(d, "dspEn", c->oled_enable);
    jadd_num(d, "dspTOut", c->oled_timeout);
    jadd_num(d, "dspDim", c->dim);
    jadd_num(d, "dspContrast", c->contrast);
    jadd_num(d, "dspBright", c->disp_brightness);
    jadd_num(d, "dspStartUp", c->startup);
    jadd_num(d, "dspDelay", c->dispDelay);
    jadd_num(d, "dspDxFilter", c->filterDistant);
    jadd_bool(d, "dspHUp", c->h_up);
    jadd_bool(d, "dspTX", c->tx_display);
    jadd_bool(d, "dspRX", c->rx_display);
    jadd_num(d, "dspFilter", c->dispFilter);
    jadd_bool(d, "dspRF", c->dispRF);
    jadd_bool(d, "dspINET", c->dispINET);
    jadd_bool(d, "dspFlip", c->disp_flip);

    jadd_bool(d, "vpnEn", c->vpn);
    jadd_num(d, "vpnPort", c->wg_port);
    jadd_str(d, "vpnPeer", c->wg_peer_address);
    jadd_str(d, "vpnLocal", c->wg_local_address);
    jadd_str(d, "vpnNetmark", c->wg_netmask_address);
    jadd_str(d, "vpnGW", c->wg_gw_address);
    jadd_str(d, "vpnPubKey", c->wg_public_key);
    jadd_str(d, "vpnPriKey", c->wg_private_key);

    jadd_str(d, "httpUser", c->http_username);
    jadd_str(d, "httpPass", c->http_password);
    {
        cJSON *p = cJSON_CreateArray();
        for (int i = 0; i < 4; i++)
            cJSON_AddItemToArray(p, cJSON_CreateString(c->path[i]));
        cJSON_AddItemToObject(d, "path", p);
    }

    jadd_bool(d, "gnssEn", c->gnss_enable);
    jadd_num(d, "gnssCH", c->gnss_channel);
    jadd_num(d, "gnssPPS", c->gnss_pps_gpio);
    jadd_num(d, "gnssTCPPort", c->gnss_tcp_port);
    jadd_str(d, "gnssTCPHost", c->gnss_tcp_host);
    jadd_str(d, "gnssAT", c->gnss_at_command);

    jadd_num(d, "rfTx", c->rf_tx_gpio);
    jadd_num(d, "rfRx", c->rf_rx_gpio);
    jadd_num(d, "rfSQL", c->rf_sql_gpio);
    jadd_num(d, "rfPD", c->rf_pd_gpio);
    jadd_num(d, "rfPWR", c->rf_pwr_gpio);
    jadd_num(d, "rfPTT", c->rf_ptt_gpio);
    jadd_bool(d, "rfSQLAct", c->rf_sql_active);
    jadd_bool(d, "rfPDAct", c->rf_pd_active);
    jadd_bool(d, "rfPWRAct", c->rf_pwr_active);
    jadd_bool(d, "rfPTTAct", c->rf_ptt_active);
    jadd_num(d, "adcAtten", c->adc_atten);
    jadd_num(d, "adcOffset", c->adc_dc_offset);
    jadd_num(d, "rfBaudrate", c->rf_baudrate);

    jadd_bool(d, "i2cEn", c->i2c_enable);
    jadd_num(d, "i2cSDA", c->i2c_sda_pin);
    jadd_num(d, "i2cSCK", c->i2c_sck_pin);
    jadd_num(d, "i2cFreq", c->i2c_freq);
    jadd_bool(d, "i2c1En", c->i2c1_enable);
    jadd_num(d, "i2c1SDA", c->i2c1_sda_pin);
    jadd_num(d, "i2c1SCK", c->i2c1_sck_pin);
    jadd_num(d, "i2c1Freq", c->i2c1_freq);

    jadd_bool(d, "oneWireEn", c->onewire_enable);
    jadd_num(d, "oneWireIO", c->onewire_gpio);

    jadd_bool(d, "uart0En", c->uart0_enable);
    jadd_num(d, "uart0BR", c->uart0_baudrate);
    jadd_num(d, "uart0TX", c->uart0_tx_gpio);
    jadd_num(d, "uart0RX", c->uart0_rx_gpio);
    jadd_num(d, "uart0RTS", c->uart0_rts_gpio);
    jadd_bool(d, "uart1En", c->uart1_enable);
    jadd_num(d, "uart1BR", c->uart1_baudrate);
    jadd_num(d, "uart1TX", c->uart1_tx_gpio);
    jadd_num(d, "uart1RX", c->uart1_rx_gpio);
    jadd_num(d, "uart1RTS", c->uart1_rts_gpio);
    jadd_bool(d, "uart2En", c->uart2_enable);
    jadd_num(d, "uart2BR", c->uart2_baudrate);
    jadd_num(d, "uart2TX", c->uart2_tx_gpio);
    jadd_num(d, "uart2RX", c->uart2_rx_gpio);

    jadd_bool(d, "modbusEn", c->modbus_enable);
    jadd_num(d, "modbusAddr", c->modbus_address);
    jadd_num(d, "modbusCh", c->modbus_channel);
    jadd_num(d, "modbusDE", c->modbus_de_gpio);

    jadd_bool(d, "cnt0En", c->counter0_enable);
    jadd_bool(d, "cnt0Act", c->counter0_active);
    jadd_num(d, "cnt0IO", c->counter0_gpio);
    jadd_bool(d, "cnt1En", c->counter1_enable);
    jadd_bool(d, "cnt1Act", c->counter1_active);
    jadd_num(d, "cnt1IO", c->counter1_gpio);

    jadd_bool(d, "extTNCEn", c->ext_tnc_enable);
    jadd_num(d, "extTNCCh", c->ext_tnc_channel);
    jadd_num(d, "extTNCMode", c->ext_tnc_mode);

    jadd_bool(d, "pwrEn", c->pwr_en);
    jadd_num(d, "pwrMode", c->pwr_mode);
    jadd_num(d, "pwrSleep", c->pwr_sleep_interval);
    jadd_num(d, "pwrStanby", c->pwr_stanby_delay);
    jadd_num(d, "pwrSleepAct", c->pwr_sleep_activate);
    jadd_num(d, "pwrIO", c->pwr_gpio);
    jadd_bool(d, "pwrIOAct", c->pwr_active);

    jadd_num(d, "logFile", c->log);

    jadd_bool(d, "pppEn", c->ppp_enable);
    jadd_str(d, "pppAPN", c->ppp_apn);
    jadd_num(d, "pppRST", c->ppp_rst_gpio);
    jadd_bool(d, "pppRSTAct", c->ppp_rst_active);
    jadd_num(d, "pppTX", c->ppp_tx_gpio);
    jadd_num(d, "pppRX", c->ppp_rx_gpio);
    jadd_num(d, "pppRTS", c->ppp_rts_gpio);
    jadd_num(d, "pppDTR", c->ppp_dtr_gpio);
    jadd_num(d, "pppCTS", c->ppp_cts_gpio);
    jadd_num(d, "pppRI", c->ppp_ri_gpio);
    jadd_num(d, "pppPWR", c->ppp_pwr_gpio);
    jadd_bool(d, "pppPWRAct", c->ppp_pwr_active);
    jadd_num(d, "pppRSTDelay", c->ppp_rst_delay);
    jadd_str(d, "pppPin", c->ppp_pin);
    jadd_num(d, "pppSerial", c->ppp_serial);
    jadd_num(d, "pppSerialBaudrate", c->ppp_serial_baudrate);
    jadd_num(d, "pppModel", c->ppp_model);
    jadd_num(d, "pppFlowCtrl", c->ppp_flow_ctrl);
    jadd_bool(d, "pppGNSS", c->ppp_gnss);
    jadd_bool(d, "pppNAPT", c->ppp_napt);

    jadd_bool(d, "mqttEnable", c->en_mqtt);
    jadd_str(d, "mqttHost", c->mqtt_host);
    jadd_str(d, "mqttTopic", c->mqtt_topic);
    jadd_str(d, "mqttSub", c->mqtt_subscribe);
    jadd_num(d, "mqttTopicFlag", c->mqtt_topic_flag);
    jadd_num(d, "mqttSubFlag", c->mqtt_subscribe_flag);
    jadd_num(d, "mqttPort", c->mqtt_port);
    jadd_str(d, "mqttUser", c->mqtt_user);
    jadd_str(d, "mqttPass", c->mqtt_pass);

    jadd_num(d, "trkTlmInv", c->trk_tlm_interval);
    jadd_num(d, "digiTlmInv", c->digi_tlm_interval);
    jadd_num(d, "igateTlmInv", c->igate_tlm_interval);
    jadd_str(d, "hostName", c->host_name);
    jadd_num(d, "resetTimeout", c->reset_timeout);
    jadd_bool(d, "cmdOnMqtt", c->at_cmd_mqtt);
    jadd_bool(d, "cmdOnMsg", c->at_cmd_msg);
    jadd_bool(d, "cmdOnBluetooth", c->at_cmd_bluetooth);
    jadd_num(d, "cmdOnUart", c->at_cmd_uart);

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

    return d;
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
    c->sql_level = (uint8_t)jget_num(d, "rfSql", def.sql_level);
    c->volume = (uint8_t)jget_num(d, "rfVolume", def.volume);
    c->agc_max_gain = (uint8_t)jget_num(d, "agcMaxGain", def.agc_max_gain);
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
        cJSON *a7 = cJSON_GetObjectItemCaseSensitive(d, "igateTlmEQNS");
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
            if (a7) {
                cJSON *e0 = cJSON_GetArrayItem(a7, i * 3), *e1 = cJSON_GetArrayItem(a7, i * 3 + 1), *e2 = cJSON_GetArrayItem(a7, i * 3 + 2);
                if (e0)
                    c->igate_tlm_EQNS[i][0] = (float)e0->valuedouble;
                if (e1)
                    c->igate_tlm_EQNS[i][1] = (float)e1->valuedouble;
                if (e2)
                    c->igate_tlm_EQNS[i][2] = (float)e2->valuedouble;
            }
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
        cJSON *a7 = cJSON_GetObjectItemCaseSensitive(d, "digiTlmEQNS");
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
            if (a7) {
                cJSON *e0 = cJSON_GetArrayItem(a7, i * 3), *e1 = cJSON_GetArrayItem(a7, i * 3 + 1), *e2 = cJSON_GetArrayItem(a7, i * 3 + 2);
                if (e0)
                    c->digi_tlm_EQNS[i][0] = (float)e0->valuedouble;
                if (e1)
                    c->digi_tlm_EQNS[i][1] = (float)e1->valuedouble;
                if (e2)
                    c->digi_tlm_EQNS[i][2] = (float)e2->valuedouble;
            }
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
    c->trk_mice_type = (uint8_t)jget_num(d, "trkMicEType", def.trk_mice_type);
    {
        cJSON *a1 = cJSON_GetObjectItemCaseSensitive(d, "trkTlmAvg"), *a2 = cJSON_GetObjectItemCaseSensitive(d, "trkTlmSen");
        cJSON *a3 = cJSON_GetObjectItemCaseSensitive(d, "trkTlmPrec"), *a4 = cJSON_GetObjectItemCaseSensitive(d, "trkTlmOffset");
        cJSON *a5 = cJSON_GetObjectItemCaseSensitive(d, "trkTlmPARM"), *a6 = cJSON_GetObjectItemCaseSensitive(d, "trkTlmUNIT");
        cJSON *a7 = cJSON_GetObjectItemCaseSensitive(d, "trkTlmEQNS");
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
            if (a7) {
                cJSON *e0 = cJSON_GetArrayItem(a7, i * 3), *e1 = cJSON_GetArrayItem(a7, i * 3 + 1), *e2 = cJSON_GetArrayItem(a7, i * 3 + 2);
                if (e0)
                    c->trk_tlm_EQNS[i][0] = (float)e0->valuedouble;
                if (e1)
                    c->trk_tlm_EQNS[i][1] = (float)e1->valuedouble;
                if (e2)
                    c->trk_tlm_EQNS[i][2] = (float)e2->valuedouble;
            }
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
    c->wx_flage = (uint32_t)jget_num(d, "wxFlage", def.wx_flage);
    set_str(c->wx_object, sizeof(c->wx_object), jget_str(d, "wxObject", def.wx_object));
    set_str(c->wx_comment, sizeof(c->wx_comment), jget_str(d, "wxComment", def.wx_comment));
    c->wx_tlm_interval = (uint8_t)jget_num(d, "wxTlmInv", def.wx_tlm_interval);
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
        uint8_t *bits = ch == 0 ? &c->tlm0_BITS_Active : &c->tlm1_BITS_Active;
        char *comment = ch == 0 ? c->tlm0_comment : c->tlm1_comment;
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
        snprintf(key, sizeof(key), "%sBIT", pfx);
        *bits = (uint8_t)jget_num(d, key, ch == 0 ? def.tlm0_BITS_Active : def.tlm1_BITS_Active);
        snprintf(key, sizeof(key), "%sComment", pfx);
        set_str(comment, COMMENT_SIZE, jget_str(d, key, ch == 0 ? def.tlm0_comment : def.tlm1_comment));
        snprintf(key, sizeof(key), "%sEQNS", pfx);
        cJSON *eqns = cJSON_GetObjectItemCaseSensitive(d, key);
        float(*E)[3] = ch == 0 ? c->tlm0_EQNS : c->tlm1_EQNS;
        for (int i = 0; i < TLM_CH; i++) {
            if (eqns) {
                cJSON *e0 = cJSON_GetArrayItem(eqns, i * 3), *e1 = cJSON_GetArrayItem(eqns, i * 3 + 1), *e2 = cJSON_GetArrayItem(eqns, i * 3 + 2);
                E[i][0] = e0 ? (float)e0->valuedouble : 0;
                E[i][1] = e1 ? (float)e1->valuedouble : 0;
                E[i][2] = e2 ? (float)e2->valuedouble : 0;
            }
        }
        snprintf(key, sizeof(key), "%sPARM", pfx);
        cJSON *parm = cJSON_GetObjectItemCaseSensitive(d, key);
        char(*P)[10] = ch == 0 ? c->tlm0_PARM : c->tlm1_PARM;
        snprintf(key, sizeof(key), "%sUNIT", pfx);
        cJSON *unit = cJSON_GetObjectItemCaseSensitive(d, key);
        char(*U)[8] = ch == 0 ? c->tlm0_UNIT : c->tlm1_UNIT;
        snprintf(key, sizeof(key), "%sDataCH", pfx);
        cJSON *dch = cJSON_GetObjectItemCaseSensitive(d, key);
        uint8_t *DC = ch == 0 ? c->tml0_data_channel : c->tml1_data_channel;
        for (int i = 0; i < TLM_PARM_NUM; i++) {
            cJSON *v;
            if (parm && (v = cJSON_GetArrayItem(parm, i)) && cJSON_IsString(v))
                set_str(P[i], 10, v->valuestring);
            if (unit && (v = cJSON_GetArrayItem(unit, i)) && cJSON_IsString(v))
                set_str(U[i], 8, v->valuestring);
            if (dch && (v = cJSON_GetArrayItem(dch, i)))
                DC[i] = (uint8_t)v->valuedouble;
        }
    }

    c->oled_enable = jget_bool(d, "dspEn", def.oled_enable);
    c->oled_timeout = (int)jget_num(d, "dspTOut", def.oled_timeout);
    c->dim = (uint8_t)jget_num(d, "dspDim", def.dim);
    c->contrast = (uint8_t)jget_num(d, "dspContrast", def.contrast);
    c->disp_brightness = (uint8_t)jget_num(d, "dspBright", def.disp_brightness);
    c->startup = (uint8_t)jget_num(d, "dspStartUp", def.startup);
    c->dispDelay = (unsigned int)jget_num(d, "dspDelay", def.dispDelay);
    c->filterDistant = (unsigned int)jget_num(d, "dspDxFilter", def.filterDistant);
    c->h_up = jget_bool(d, "dspHUp", def.h_up);
    c->tx_display = jget_bool(d, "dspTX", def.tx_display);
    c->rx_display = jget_bool(d, "dspRX", def.rx_display);
    c->dispFilter = (uint16_t)jget_num(d, "dspFilter", def.dispFilter);
    c->dispRF = jget_bool(d, "dspRF", def.dispRF);
    c->dispINET = jget_bool(d, "dspINET", def.dispINET);
    c->disp_flip = jget_bool(d, "dspFlip", def.disp_flip);

    c->vpn = jget_bool(d, "vpnEn", def.vpn);
    c->wg_port = (uint16_t)jget_num(d, "vpnPort", def.wg_port);
    set_str(c->wg_peer_address, sizeof(c->wg_peer_address), jget_str(d, "vpnPeer", def.wg_peer_address));
    set_str(c->wg_local_address, sizeof(c->wg_local_address), jget_str(d, "vpnLocal", def.wg_local_address));
    set_str(c->wg_netmask_address, sizeof(c->wg_netmask_address), jget_str(d, "vpnNetmark", def.wg_netmask_address));
    set_str(c->wg_gw_address, sizeof(c->wg_gw_address), jget_str(d, "vpnGW", def.wg_gw_address));
    set_str(c->wg_public_key, sizeof(c->wg_public_key), jget_str(d, "vpnPubKey", def.wg_public_key));
    set_str(c->wg_private_key, sizeof(c->wg_private_key), jget_str(d, "vpnPriKey", def.wg_private_key));

    set_str(c->http_username, sizeof(c->http_username), jget_str(d, "httpUser", def.http_username));
    set_str(c->http_password, sizeof(c->http_password), jget_str(d, "httpPass", def.http_password));
    {
        cJSON *p = cJSON_GetObjectItemCaseSensitive(d, "path");
        for (int i = 0; i < 4; i++) {
            cJSON *v = p ? cJSON_GetArrayItem(p, i) : NULL;
            set_str(c->path[i], sizeof(c->path[i]), (v && cJSON_IsString(v)) ? v->valuestring : def.path[i]);
        }
    }

    c->gnss_enable = jget_bool(d, "gnssEn", def.gnss_enable);
    c->gnss_channel = (int8_t)jget_num(d, "gnssCH", def.gnss_channel);
    c->gnss_pps_gpio = (int8_t)jget_num(d, "gnssPPS", def.gnss_pps_gpio);
    c->gnss_tcp_port = (uint16_t)jget_num(d, "gnssTCPPort", def.gnss_tcp_port);
    set_str(c->gnss_tcp_host, sizeof(c->gnss_tcp_host), jget_str(d, "gnssTCPHost", def.gnss_tcp_host));
    set_str(c->gnss_at_command, sizeof(c->gnss_at_command), jget_str(d, "gnssAT", def.gnss_at_command));

    c->rf_tx_gpio = (int8_t)jget_num(d, "rfTx", def.rf_tx_gpio);
    c->rf_rx_gpio = (int8_t)jget_num(d, "rfRx", def.rf_rx_gpio);
    c->rf_sql_gpio = (int8_t)jget_num(d, "rfSQL", def.rf_sql_gpio);
    c->rf_pd_gpio = (int8_t)jget_num(d, "rfPD", def.rf_pd_gpio);
    c->rf_pwr_gpio = (int8_t)jget_num(d, "rfPWR", def.rf_pwr_gpio);
    c->rf_ptt_gpio = (int8_t)jget_num(d, "rfPTT", def.rf_ptt_gpio);
    c->rf_sql_active = jget_bool(d, "rfSQLAct", def.rf_sql_active);
    c->rf_pd_active = jget_bool(d, "rfPDAct", def.rf_pd_active);
    c->rf_pwr_active = jget_bool(d, "rfPWRAct", def.rf_pwr_active);
    c->rf_ptt_active = jget_bool(d, "rfPTTAct", def.rf_ptt_active);
    c->adc_atten = (uint8_t)jget_num(d, "adcAtten", def.adc_atten);
    c->adc_dc_offset = (uint16_t)jget_num(d, "adcOffset", def.adc_dc_offset);
    c->rf_baudrate = (unsigned long)jget_num(d, "rfBaudrate", def.rf_baudrate);

    c->i2c_enable = jget_bool(d, "i2cEn", def.i2c_enable);
    c->i2c_sda_pin = (int8_t)jget_num(d, "i2cSDA", def.i2c_sda_pin);
    c->i2c_sck_pin = (int8_t)jget_num(d, "i2cSCK", def.i2c_sck_pin);
    c->i2c_freq = (uint32_t)jget_num(d, "i2cFreq", def.i2c_freq);
    c->i2c1_enable = jget_bool(d, "i2c1En", def.i2c1_enable);
    c->i2c1_sda_pin = (int8_t)jget_num(d, "i2c1SDA", def.i2c1_sda_pin);
    c->i2c1_sck_pin = (int8_t)jget_num(d, "i2c1SCK", def.i2c1_sck_pin);
    c->i2c1_freq = (uint32_t)jget_num(d, "i2c1Freq", def.i2c1_freq);

    c->onewire_enable = jget_bool(d, "oneWireEn", def.onewire_enable);
    c->onewire_gpio = (int8_t)jget_num(d, "oneWireIO", def.onewire_gpio);

    c->uart0_enable = jget_bool(d, "uart0En", def.uart0_enable);
    c->uart0_baudrate = (unsigned long)jget_num(d, "uart0BR", def.uart0_baudrate);
    c->uart0_tx_gpio = (int8_t)jget_num(d, "uart0TX", def.uart0_tx_gpio);
    c->uart0_rx_gpio = (int8_t)jget_num(d, "uart0RX", def.uart0_rx_gpio);
    c->uart0_rts_gpio = (int8_t)jget_num(d, "uart0RTS", def.uart0_rts_gpio);
    c->uart1_enable = jget_bool(d, "uart1En", def.uart1_enable);
    c->uart1_baudrate = (unsigned long)jget_num(d, "uart1BR", def.uart1_baudrate);
    c->uart1_tx_gpio = (int8_t)jget_num(d, "uart1TX", def.uart1_tx_gpio);
    c->uart1_rx_gpio = (int8_t)jget_num(d, "uart1RX", def.uart1_rx_gpio);
    c->uart1_rts_gpio = (int8_t)jget_num(d, "uart1RTS", def.uart1_rts_gpio);
    c->uart2_enable = jget_bool(d, "uart2En", def.uart2_enable);
    c->uart2_baudrate = (unsigned long)jget_num(d, "uart2BR", def.uart2_baudrate);
    c->uart2_tx_gpio = (int8_t)jget_num(d, "uart2TX", def.uart2_tx_gpio);
    c->uart2_rx_gpio = (int8_t)jget_num(d, "uart2RX", def.uart2_rx_gpio);

    c->modbus_enable = jget_bool(d, "modbusEn", def.modbus_enable);
    c->modbus_address = (uint8_t)jget_num(d, "modbusAddr", def.modbus_address);
    c->modbus_channel = (int8_t)jget_num(d, "modbusCh", def.modbus_channel);
    c->modbus_de_gpio = (int8_t)jget_num(d, "modbusDE", def.modbus_de_gpio);

    c->counter0_enable = jget_bool(d, "cnt0En", def.counter0_enable);
    c->counter0_active = jget_bool(d, "cnt0Act", def.counter0_active);
    c->counter0_gpio = (int8_t)jget_num(d, "cnt0IO", def.counter0_gpio);
    c->counter1_enable = jget_bool(d, "cnt1En", def.counter1_enable);
    c->counter1_active = jget_bool(d, "cnt1Act", def.counter1_active);
    c->counter1_gpio = (int8_t)jget_num(d, "cnt1IO", def.counter1_gpio);

    c->ext_tnc_enable = jget_bool(d, "extTNCEn", def.ext_tnc_enable);
    c->ext_tnc_channel = (int8_t)jget_num(d, "extTNCCh", def.ext_tnc_channel);
    c->ext_tnc_mode = (int8_t)jget_num(d, "extTNCMode", def.ext_tnc_mode);

    c->pwr_en = jget_bool(d, "pwrEn", def.pwr_en);
    c->pwr_mode = (uint8_t)jget_num(d, "pwrMode", def.pwr_mode);
    c->pwr_sleep_interval = (uint16_t)jget_num(d, "pwrSleep", def.pwr_sleep_interval);
    c->pwr_stanby_delay = (uint16_t)jget_num(d, "pwrStanby", def.pwr_stanby_delay);
    c->pwr_sleep_activate = (uint8_t)jget_num(d, "pwrSleepAct", def.pwr_sleep_activate);
    c->pwr_gpio = (int8_t)jget_num(d, "pwrIO", def.pwr_gpio);
    c->pwr_active = jget_bool(d, "pwrIOAct", def.pwr_active);

    c->log = (uint16_t)jget_num(d, "logFile", def.log);

    c->ppp_enable = jget_bool(d, "pppEn", def.ppp_enable);
    set_str(c->ppp_apn, sizeof(c->ppp_apn), jget_str(d, "pppAPN", def.ppp_apn));
    c->ppp_rst_gpio = (int8_t)jget_num(d, "pppRST", def.ppp_rst_gpio);
    c->ppp_rst_active = jget_bool(d, "pppRSTAct", def.ppp_rst_active);
    c->ppp_tx_gpio = (int8_t)jget_num(d, "pppTX", def.ppp_tx_gpio);
    c->ppp_rx_gpio = (int8_t)jget_num(d, "pppRX", def.ppp_rx_gpio);
    c->ppp_rts_gpio = (int8_t)jget_num(d, "pppRTS", def.ppp_rts_gpio);
    c->ppp_dtr_gpio = (int8_t)jget_num(d, "pppDTR", def.ppp_dtr_gpio);
    c->ppp_cts_gpio = (int8_t)jget_num(d, "pppCTS", def.ppp_cts_gpio);
    c->ppp_ri_gpio = (int8_t)jget_num(d, "pppRI", def.ppp_ri_gpio);
    c->ppp_pwr_gpio = (int8_t)jget_num(d, "pppPWR", def.ppp_pwr_gpio);
    c->ppp_pwr_active = jget_bool(d, "pppPWRAct", def.ppp_pwr_active);
    c->ppp_rst_delay = (uint16_t)jget_num(d, "pppRSTDelay", def.ppp_rst_delay);
    set_str(c->ppp_pin, sizeof(c->ppp_pin), jget_str(d, "pppPin", def.ppp_pin));
    c->ppp_serial = (uint8_t)jget_num(d, "pppSerial", def.ppp_serial);
    c->ppp_serial_baudrate = (unsigned long)jget_num(d, "pppSerialBaudrate", def.ppp_serial_baudrate);
    c->ppp_model = (uint8_t)jget_num(d, "pppModel", def.ppp_model);
    c->ppp_flow_ctrl = (uint8_t)jget_num(d, "pppFlowCtrl", def.ppp_flow_ctrl);
    c->ppp_gnss = jget_bool(d, "pppGNSS", def.ppp_gnss);
    c->ppp_napt = jget_bool(d, "pppNAPT", def.ppp_napt);

    c->en_mqtt = jget_bool(d, "mqttEnable", def.en_mqtt);
    set_str(c->mqtt_host, sizeof(c->mqtt_host), jget_str(d, "mqttHost", def.mqtt_host));
    set_str(c->mqtt_topic, sizeof(c->mqtt_topic), jget_str(d, "mqttTopic", def.mqtt_topic));
    set_str(c->mqtt_subscribe, sizeof(c->mqtt_subscribe), jget_str(d, "mqttSub", def.mqtt_subscribe));
    c->mqtt_topic_flag = (uint16_t)jget_num(d, "mqttTopicFlag", def.mqtt_topic_flag);
    c->mqtt_subscribe_flag = (uint16_t)jget_num(d, "mqttSubFlag", def.mqtt_subscribe_flag);
    c->mqtt_port = (uint16_t)jget_num(d, "mqttPort", def.mqtt_port);
    set_str(c->mqtt_user, sizeof(c->mqtt_user), jget_str(d, "mqttUser", def.mqtt_user));
    set_str(c->mqtt_pass, sizeof(c->mqtt_pass), jget_str(d, "mqttPass", def.mqtt_pass));

    c->trk_tlm_interval = (uint8_t)jget_num(d, "trkTlmInv", def.trk_tlm_interval);
    c->digi_tlm_interval = (uint8_t)jget_num(d, "digiTlmInv", def.digi_tlm_interval);
    c->igate_tlm_interval = (uint8_t)jget_num(d, "igateTlmInv", def.igate_tlm_interval);
    set_str(c->host_name, sizeof(c->host_name), jget_str(d, "hostName", def.host_name));
    c->reset_timeout = (uint16_t)jget_num(d, "resetTimeout", def.reset_timeout);
    c->at_cmd_mqtt = jget_bool(d, "cmdOnMqtt", def.at_cmd_mqtt);
    c->at_cmd_msg = jget_bool(d, "cmdOnMsg", def.at_cmd_msg);
    c->at_cmd_bluetooth = jget_bool(d, "cmdOnBluetooth", def.at_cmd_bluetooth);
    c->at_cmd_uart = (uint8_t)jget_num(d, "cmdOnUart", def.at_cmd_uart);

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
}

bool app_config_save(void) {
    cJSON *doc = config_to_json(&g_config);
    char *out = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (!out) {
        ESP_LOGE(TAG, "cJSON_Print failed");
        return false;
    }

    FILE *f = fopen(CONFIG_TMP_PATH, "w");
    if (!f) {
        ESP_LOGE(TAG, "open tmp for write failed");
        free(out);
        return false;
    }
    size_t len = strlen(out);
    size_t written = fwrite(out, 1, len, f);
    fclose(f);
    free(out);
    if (written != len) {
        ESP_LOGE(TAG, "short write on config");
        return false;
    }

    remove(CONFIG_PATH);
    if (rename(CONFIG_TMP_PATH, CONFIG_PATH) != 0) {
        ESP_LOGE(TAG, "rename tmp->config failed");
        return false;
    }
    ESP_LOGI(TAG, "Configuration saved (%d bytes)", (int)len);
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
    app_config_set_defaults(&g_config);
    return app_config_save();
}
