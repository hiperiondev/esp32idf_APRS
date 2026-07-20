/**
 * @file app_config.h
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
 * @brief Global application configuration type (app_config_t) and the g_config
 * instance, plus the compile-time module (ENABLE_*) and UI language (LANGUAGE)
 * selection macros.
 *
 * Field names and JSON keys are kept 1:1 with the original include/config.h and
 * src/config.cpp so that every value the web admin shows/edits has a home here
 * and persists to LittleFS as /storage/config.json.
 *
 * Exactly one language is built into the firmware image at a time - there is no
 * runtime language switch and no other language's strings are compiled in. To
 * change the language, change LANGUAGE below to one of the LANG_* codes (or
 * override it from the build system, e.g. idf.py build -DLANGUAGE=LANG_ES, or
 * `set(LANGUAGE LANG_ES)` / target_compile_definitions in CMakeLists.txt) and
 * rebuild. See translations/translations.h for how the selection works and
 * translations/lang_en.h / translations/lang_es.h / translations/lang_it.h for the string tables.
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Firmware UI language selection.
//
// Exactly one language is built into the firmware image at a time - there is
// no runtime language switch and no other language's strings are compiled in.
// To change the language, change LANGUAGE below to one of the LANG_* codes
// and rebuild. See translations/translations.h for how the selection works
// and translations/lang_en.h / translations/lang_es.h / translations/lang_it.h for the string tables.
// ---------------------------------------------------------------------------
#define LANG_EN 0 // English
#define LANG_ES 1 // Español (Spanish)
#define LANG_IT 2 // Italiano (Italian)

#ifndef LANGUAGE
#define LANGUAGE LANG_EN
#endif

/////// MODULES //////
// Disabled modules already not implemented
#define ENABLE_DASHBOARD
#define ENABLE_MSG_CHAT
#define ENABLE_BULLETINS
#define ENABLE_STATION
#define ENABLE_RADIO_MODEM
//#define ENABLE_RF_MODULE
//#define ENABLE_VPN
//#define ENABLE_MQTT
#define ENABLE_MESSAGE
//#define ENABLE_MOD_GPIO
#define ENABLE_IGATE
#define ENABLE_DIGIPEATER
#define ENABLE_TRACKER
//#define ENABLE_SMARTBEACONING 
#define ENABLE_WEATHER
//#define ENABLE_TELEMETRY
#define ENABLE_SYSTEM
#define ENABLE_WIRELESS
//#define ENABLE_GNSS
#define ENABLE_FILE_STORAGE
#define ENABLE_ABOUT_FIRMWARE

//////////////////////

#define COMMENT_SIZE       129  // 128 bytes of comment text + NUL terminator (IGate/Digipeater/Tracker/Weather)
#define STATUS_SIZE        50

/*
 * Weather station "Sensor Mapping" rows.
 *
 * This is the canonical, on-air list of every quantity an APRS Weather Report
 * can actually carry (APRS101 ch.12 + the APRS 1.2 flood proposals), one row
 * per mappable measurement. The old 26-entry table mixed in extended sensors
 * that have NO weather-report token on-air (UV, soil, water, battery - those
 * belong on Telemetry, not a WX report) plus nine empty placeholder slots;
 * those are gone. Each row here corresponds 1:1 to a field the WX encoder in
 * weather.c emits, and is indexed by ::wx_field_id_t below.
 */
typedef enum {
    WX_FIELD_WIND_DIRECTION = 0, /**< Wind direction, deg     -> "ddd/" (aprs_wind_t.direction_deg). */
    WX_FIELD_WIND_SPEED,         /**< Sustained wind, mph     -> "/sss" (aprs_wind_t.sustained_mph). */
    WX_FIELD_WIND_GUST,          /**< Peak gust, mph          -> "gXXX" (aprs_wind_t.gust_mph). */
    WX_FIELD_TEMPERATURE,        /**< Air temperature, deg F  -> "tXXX". */
    WX_FIELD_RAIN_1H,            /**< Rain last hour, 1/100 in-> "rXXX". */
    WX_FIELD_RAIN_24H,           /**< Rain last 24h, 1/100 in -> "pXXX". */
    WX_FIELD_RAIN_MIDNIGHT,      /**< Rain since midnight      -> "PXXX". */
    WX_FIELD_SNOW_24H,           /**< Snow last 24h, 1/10 in  -> "sXXX" (APRS 1.2). */
    WX_FIELD_HUMIDITY,           /**< Relative humidity, %    -> "hXX". */
    WX_FIELD_PRESSURE,           /**< Barometric pressure     -> "bXXXXX" (tenths of mb). */
    WX_FIELD_LUMINOSITY,         /**< Solar luminosity, W/m^2 -> "LXXX"/"lXXX" (APRS 1.2). */
    WX_FIELD_FLOOD_HEIGHT_FT,    /**< Flood/water gauge, feet -> "FXXXX.X" (APRS 1.2). */
    WX_FIELD_FLOOD_HEIGHT_M,     /**< Flood/water gauge, m    -> "fXXXX.X" (APRS 1.2). */
    WX_SENSOR_NUM                /**< Sentinel: number of mappable WX fields. Not a real field. */
} wx_field_id_t;

#define WIFI_STA_NUM       5
#define NTP_HOST_NUM       3
#define NTP_RESYNC_MIN_SEC 30
#define TLM_CH             5
#define TLM_PARM_NUM       13

// Activate bit flags (path/object selection use)
#define ACTIVATE_OFF       0
#define ACTIVATE_TRACKER   (1 << 0)
#define ACTIVATE_IGATE     (1 << 1)
#define ACTIVATE_DIGI      (1 << 2)
#define ACTIVATE_WX        (1 << 3)
#define ACTIVATE_TELEMETRY (1 << 4)
#define ACTIVATE_QUERY     (1 << 5)
#define ACTIVATE_STATUS    (1 << 6)
#define ACTIVATE_WIFI      (1 << 7)

// IGATE [Filter] section bit flags - shared meaning for rf2inetFilter / inet2rfFilter
#define IGATE_FILT_MESSAGE   (1 << 0)
#define IGATE_FILT_STATUS    (1 << 1)
#define IGATE_FILT_TELEMETRY (1 << 2)
#define IGATE_FILT_WEATHER   (1 << 3)
#define IGATE_FILT_OBJECT    (1 << 4)
#define IGATE_FILT_ITEM      (1 << 5)
#define IGATE_FILT_QUERY     (1 << 6)
#define IGATE_FILT_BUOY      (1 << 7)
#define IGATE_FILT_POSITION  (1 << 8)

// RF module types
#define RF_SX1231 1
#define RF_SX1233 2
#define RF_SX1261 3
#define RF_SX1262 4
#define RF_SX1268 5
#define RF_SX1272 6
#define RF_SX1273 7
#define RF_SX1276 8
#define RF_SX1278 9
#define RF_SX1279 10
#define RF_SX1280 11
#define RF_SX1281 12
#define RF_SX1282 13

#define RF_MODE_OFF   0
#define RF_MODE_LoRa  1
#define RF_MODE_G3RUH 2
#define RF_MODE_AIS   3
#define RF_MODE_GFSK  4
#define RF_MODE_DPRS  5

typedef struct {
    bool enable;
    char wifi_ssid[33]; // 32 chars max (IEEE 802.11 SSID limit) + null terminator
    char wifi_pass[64]; // 63 chars max (WPA/WPA2/WPA3 PSK limit) + null terminator
} wifi_sta_t;

typedef struct {
    float timeZone;
    bool synctime;
    bool title;
    uint8_t cpuFreq;

    // MY STATION - shared station identity/position, entered once on the
    // "Station" page and reused (read-only) by every other page's
    // "Use My Station Data" checkbox instead of retyping the same callsign
    // and coordinates on every service.
    char my_callsign[10];
    float my_lat;
    float my_lon;
    float my_alt;

    // WiFi / BT / RF
    uint8_t wifi_mode; // 0=off,1=STA,2=AP,3=AP_STA (see WIFI_MODE_* below)
    int8_t wifi_power;
    wifi_sta_t wifi_sta[WIFI_STA_NUM];
    uint8_t wifi_ap_ch;
    char wifi_ap_ssid[33]; // 32 chars max (IEEE 802.11 SSID limit) + null terminator
    char wifi_ap_pass[64]; // 63 chars max (WPA/WPA2/WPA3 PSK limit) + null terminator

    // Bluetooth
    bool bt_slave;
    bool bt_master;
    uint8_t bt_mode;
    char bt_uuid[37];
    char bt_uuid_rx[37];
    char bt_uuid_tx[37];
    char bt_name[20];
    uint32_t bt_pin;
    uint8_t bt_power;

    // RF Module
    bool rf_en;
    uint8_t rf_type;
    float freq_rx;
    float freq_tx;
    int offset_rx;
    int offset_tx;
    int tone_rx;
    int tone_tx;
    uint8_t sql_level;
    uint8_t volume;
    uint8_t agc_max_gain; // software AGC gain ceiling (1-100x). Unused since the modem component swap:
                          // esp32idf_radioamateur_modem's AGC is self-limiting and exposes no ceiling.
                          // Kept so existing config.json files round-trip unchanged.
    uint8_t mic;

    // IGATE
    bool igate_en;
    bool rf2inet;
    bool inet2rf;
    bool igate_loc2rf;
    bool igate_loc2inet;
    uint16_t rf2inetFilter;
    uint16_t inet2rfFilter;
    uint8_t aprs_ssid;
    uint16_t aprs_port;
    char aprs_mycall[10];
    bool igate_use_station; // "Use My Station Data": mirror g_config.my_callsign/my_lat/my_lon/my_alt here (aprs_mycall/igate_lat/igate_lon/igate_alt) and lock those fields for editing
    char aprs_host[20];
    char aprs_passcode[6];
    char aprs_moniCall[10];
    char aprs_filter[30];
    bool igate_bcn;
    bool igate_gps;
    bool igate_timestamp;
    float igate_lat;
    float igate_lon;
    float igate_alt;
    uint16_t igate_interval;
    char igate_symbol[3];
    char igate_object[10];
    char igate_phg[8];
    uint8_t igate_path;
    char igate_comment[COMMENT_SIZE];
    uint16_t igate_sts_interval;
    char igate_status[STATUS_SIZE];
    // PHG sub-fields (Radio TX Power/Antenna Gain/Height/Direction) used to
    // compute igate_phg client-side; persisted so the web form can redisplay
    // the same selections/PHG text after a reload.
    uint16_t igate_phg_power;  // Watts
    float igate_phg_gain;      // dBi
    uint16_t igate_phg_height; // Feet
    uint8_t igate_phg_dir;     // 0=Omni, 1-8 = N,NE,E,SE,S,SW,W,NW

    // DIGI REPEATER
    bool digi_en;
    bool digi_auto;
    bool digi_loc2rf;
    bool digi_loc2inet;
    bool digi_timestamp;
    uint8_t digi_ssid;
    char digi_mycall[10];
    bool digi_use_station; // "Use My Station Data": mirror g_config.my_callsign/my_lat/my_lon/my_alt here and lock those fields for editing
    uint8_t digi_path;
    uint16_t digi_delay;
    uint16_t digiFilter;
    bool digi_bcn;
    bool digi_gps;
    float digi_lat;
    float digi_lon;
    float digi_alt;
    uint16_t digi_interval;
    char digi_symbol[3];
    char digi_phg[8];
    char digi_comment[COMMENT_SIZE];
    uint16_t digi_sts_interval;
    char digi_status[STATUS_SIZE];

    // TRACKER
    bool trk_en;
    bool trk_loc2rf;
    bool trk_loc2inet;
    bool trk_timestamp;
    uint8_t trk_ssid;
    char trk_mycall[10];
    bool trk_use_station; // "Use My Station Data": mirror g_config.my_callsign/my_lat/my_lon/my_alt here and lock those fields for editing
    uint8_t trk_path;
    bool trk_gps;
    float trk_lat;
    float trk_lon;
    float trk_alt;
    uint16_t trk_interval;
    bool trk_smartbeacon;
    bool trk_compress;
    bool trk_altitude;
    bool trk_log;
    bool trk_rssi;
    bool trk_sat;
    bool trk_dx;
    uint16_t trk_hspeed;
    uint8_t trk_lspeed;
    uint8_t trk_maxinterval;
    uint8_t trk_mininterval;
    uint8_t trk_minangle;
    uint16_t trk_slowinterval;
    char trk_symbol[3];
    char trk_symmove[3];
    char trk_symstop[3];
    char trk_comment[COMMENT_SIZE];
    char trk_item[10];
    uint16_t trk_sts_interval;
    char trk_status[STATUS_SIZE];
    uint8_t trk_mice_type;

    // WX
    bool wx_en;
    bool wx_2rf;
    bool wx_2inet;
    bool wx_timestamp;
    uint8_t wx_ssid;
    char wx_mycall[10];
    bool wx_use_station; // "Use My Station Data": mirror g_config.my_callsign/my_lat/my_lon/my_alt here and lock those fields for editing
    uint8_t wx_path;
    bool wx_gps;
    float wx_lat;
    float wx_lon;
    float wx_alt;
    uint16_t wx_interval;
    uint32_t wx_flage;
    char wx_object[10];
    char wx_comment[COMMENT_SIZE];
    bool wx_sensor_enable[WX_SENSOR_NUM];
    bool wx_sensor_avg[WX_SENSOR_NUM];
    uint8_t wx_sensor_ch[WX_SENSOR_NUM];
    uint8_t wx_tlm_interval;

    // Telemetry channel 0 & 1
    bool tlm0_en, tlm1_en;
    bool tlm0_2rf, tlm1_2rf;
    bool tlm0_2inet, tlm1_2inet;
    uint8_t tlm0_ssid, tlm1_ssid;
    char tlm0_mycall[10], tlm1_mycall[10];
    uint8_t tlm0_path, tlm1_path;
    uint16_t tlm0_data_interval, tlm1_data_interval;
    uint16_t tlm0_info_interval, tlm1_info_interval;
    char tlm0_PARM[TLM_PARM_NUM][10], tlm1_PARM[TLM_PARM_NUM][10];
    char tlm0_UNIT[TLM_PARM_NUM][8], tlm1_UNIT[TLM_PARM_NUM][8];
    float tlm0_EQNS[TLM_CH][3], tlm1_EQNS[TLM_CH][3];
    uint8_t tlm0_BITS_Active, tlm1_BITS_Active;
    char tlm0_comment[COMMENT_SIZE], tlm1_comment[COMMENT_SIZE];
    uint8_t tml0_data_channel[TLM_PARM_NUM], tml1_data_channel[TLM_PARM_NUM];

    // Per-service telemetry (5ch) used on trk/digi/igate beacons
    bool trk_tlm_avg[TLM_CH];
    uint8_t trk_tlm_sensor[TLM_CH];
    uint8_t trk_tlm_precision[TLM_CH];
    float trk_tlm_offset[TLM_CH];
    char trk_tlm_PARM[TLM_CH][10];
    char trk_tlm_UNIT[TLM_CH][8];
    float trk_tlm_EQNS[TLM_CH][3];
    bool digi_tlm_avg[TLM_CH];
    uint8_t digi_tlm_sensor[TLM_CH];
    uint8_t digi_tlm_precision[TLM_CH];
    float digi_tlm_offset[TLM_CH];
    char digi_tlm_PARM[TLM_CH][10];
    char digi_tlm_UNIT[TLM_CH][8];
    float digi_tlm_EQNS[TLM_CH][3];
    bool igate_tlm_avg[TLM_CH];
    uint8_t igate_tlm_sensor[TLM_CH];
    uint8_t igate_tlm_precision[TLM_CH];
    float igate_tlm_offset[TLM_CH];
    char igate_tlm_PARM[TLM_CH][10];
    char igate_tlm_UNIT[TLM_CH][8];
    float igate_tlm_EQNS[TLM_CH][3];
    uint8_t digi_tlm_interval, igate_tlm_interval, trk_tlm_interval;

    // OLED / Display
    bool oled_enable;
    int oled_timeout;
    uint8_t dim;
    uint8_t contrast;
    uint8_t startup;
    unsigned int dispDelay;
    unsigned int filterDistant;
    bool h_up;
    bool tx_display;
    bool rx_display;
    uint16_t dispFilter;
    bool dispRF;
    bool dispINET;
    bool disp_flip;
    uint8_t disp_brightness;

    // AFSK / TNC
    bool audio_modem_en; // Enable the audio ADC/DAC AFSK modem
    bool audio_hpf;
    bool audio_lpf;
    uint16_t preamble;
    uint8_t modem_type;      // RF module modem mode (RF_MODE_OFF/LoRa/G3RUH/GFSK/DPRS) - only used when ENABLE_RF_MODULE
    uint8_t afsk_modem_type; // Audio ADC/DAC AFSK modulation (see enum ModemType in modem.h: 0=300Bd,1=1200Bd,2=1200Bd V.23,3=9600Bd), used for both RX and TX on the audio modem
    uint8_t fx25_mode;
    uint16_t tx_timeslot;
    char ntp_host[NTP_HOST_NUM][20];
    uint16_t ntp_resync_sec;

    // VPN WireGuard
    bool vpn;
    bool modem;
    uint16_t wg_port;
    char wg_peer_address[32];
    char wg_local_address[16];
    char wg_netmask_address[16];
    char wg_gw_address[16];
    char wg_public_key[45];
    char wg_private_key[45];

    // System / HTTP auth
    char http_username[32];
    char http_password[64];
    char path[4][72];
    char host_name[32];
    uint16_t reset_timeout;
    bool at_cmd_mqtt;
    bool at_cmd_msg;
    bool at_cmd_bluetooth;
    uint8_t at_cmd_uart;
    uint16_t log;

    // GNSS
    bool gnss_enable;
    int8_t gnss_pps_gpio;
    int8_t gnss_channel;
    uint16_t gnss_tcp_port;
    char gnss_tcp_host[20];
    char gnss_at_command[30];

    // RF Module GPIO ("MOD" page)
    unsigned long rf_baudrate;
    int8_t rf_tx_gpio, rf_rx_gpio, rf_sql_gpio, rf_pd_gpio, rf_pwr_gpio, rf_ptt_gpio;
    bool rf_sql_active, rf_pd_active, rf_pwr_active, rf_ptt_active;
    int8_t adc_gpio, dac_gpio, adc_sel_gpio, dac_sel_gpio;
    uint8_t adc_atten;
    uint16_t adc_dc_offset;

    bool i2c_enable;
    int8_t i2c_sda_pin, i2c_sck_pin, i2c_rst_pin;
    uint32_t i2c_freq;
    bool i2c1_enable;
    int8_t i2c1_sda_pin, i2c1_sck_pin;
    uint32_t i2c1_freq;

    bool onewire_enable;
    int8_t onewire_gpio;

    bool uart0_enable;
    unsigned long uart0_baudrate;
    int8_t uart0_tx_gpio, uart0_rx_gpio, uart0_rts_gpio;
    bool uart1_enable;
    unsigned long uart1_baudrate;
    int8_t uart1_tx_gpio, uart1_rx_gpio, uart1_rts_gpio;
    bool uart2_enable;
    unsigned long uart2_baudrate;
    int8_t uart2_tx_gpio, uart2_rx_gpio;

    bool modbus_enable;
    uint8_t modbus_address;
    int8_t modbus_channel;
    int8_t modbus_de_gpio;

    bool counter0_enable;
    bool counter0_active;
    int8_t counter0_gpio;
    bool counter1_enable;
    bool counter1_active;
    int8_t counter1_gpio;

    bool ext_tnc_enable;
    int8_t ext_tnc_channel;
    int8_t ext_tnc_mode;

    // Power
    bool pwr_en;
    uint8_t pwr_mode;
    uint16_t pwr_sleep_interval;
    uint16_t pwr_stanby_delay;
    uint8_t pwr_sleep_activate;
    int8_t pwr_gpio;
    bool pwr_active;

    // PPP (GSM modem)
    bool ppp_enable;
    char ppp_apn[32];
    char ppp_pin[8];
    int8_t ppp_rst_gpio, ppp_tx_gpio, ppp_rx_gpio, ppp_rts_gpio, ppp_cts_gpio, ppp_dtr_gpio, ppp_ri_gpio, ppp_pwr_gpio;
    bool ppp_rst_active;
    uint16_t ppp_rst_delay;
    bool ppp_pwr_active;
    uint8_t ppp_serial;
    unsigned long ppp_serial_baudrate;
    uint8_t ppp_model;
    uint8_t ppp_flow_ctrl;
    bool ppp_gnss;
    bool ppp_napt;

    // MQTT
    bool en_mqtt;
    char mqtt_host[63];
    char mqtt_topic[63];
    char mqtt_subscribe[63];
    char mqtt_user[32];
    char mqtt_pass[63];
    uint16_t mqtt_port;
    uint16_t mqtt_topic_flag;
    uint16_t mqtt_subscribe_flag;

    // Message
    bool msg_enable;
    char msg_mycall[10];
    bool msg_use_station; // "Use My Station Data": mirror g_config.my_callsign into msg_mycall and lock that field for editing
    uint8_t msg_path;
    bool msg_rf;
    bool msg_inet;
    bool msg_encrypt;
    char msg_key[33];
    uint8_t msg_retry;
    uint16_t msg_interval;
    bool msg_alarm_enable;  // "Message Alarm": disabled by default
    int8_t msg_alarm_gpio;  // -1 = disabled/unset; see message_alarm_gpio_is_valid()

} app_config_t;

// Global live configuration instance (loaded at boot, edited by web handlers, saved to flash).
extern app_config_t g_config;

// Fill g_config with the same defaults esp32idf_APRS ships with.
void app_config_set_defaults(app_config_t *c);

// Load /storage/config.json into g_config. If missing/corrupt, defaults are applied
// and immediately saved so the file always exists and is consistent (fulfils the
// "must exist with default data" requirement).
bool app_config_load(void);

// Serialize g_config to /storage/config.json (atomic: write tmp then rename).
bool app_config_save(void);

// Wipe config back to factory defaults and persist.
bool app_config_factory_reset(void);

#endif // APP_CONFIG_H
