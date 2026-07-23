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
 * EXCEPT for the settings whose subsystems this firmware does not implement.
 * Bluetooth, the OLED/display, WireGuard, GNSS, MQTT, the PPP/GSM modem, the
 * I2C/1-Wire/UART/Modbus/pulse-counter/external-TNC/power-management pin sets,
 * the AT-command routing flags, and the RF-module and audio-front-end pin
 * fields orphaned by the esp32_IDF_libAPRS -> esp32idf_radioamateur_modem swap
 * were all carried over verbatim, defaulted, serialized on every save and
 * parsed on every boot - and then read by nothing. They have been removed.
 * config.json shrinks accordingly, which matters directly: app_config_save()
 * runs against a small, fragmented heap (see the streaming writer there), so
 * every key that changes nothing is pure cost. Unknown keys left in an existing
 * config.json are simply ignored by config_from_json(), so older files still
 * load. rf_ptt_gpio / rf_ptt_active survive because the modem really does take
 * them at runtime.
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
#define ENABLE_OBJECTS_ITEMS
#define ENABLE_STATION
#define ENABLE_RADIO_MODEM
//#define ENABLE_RF_MODULE
#define ENABLE_MESSAGE
#define ENABLE_IGATE
#define ENABLE_DIGIPEATER
#define ENABLE_TRACKER
//#define ENABLE_SMARTBEACONING 
#define ENABLE_WEATHER
//#define ENABLE_TELEMETRY
#define ENABLE_SYSTEM
#define ENABLE_WIRELESS
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

    // RF Module
    bool rf_en;
    uint8_t rf_type;
    float freq_rx;
    float freq_tx;
    int tone_rx;
    int tone_tx;

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
    char wx_object[10];
    char wx_comment[COMMENT_SIZE];
    bool wx_sensor_enable[WX_SENSOR_NUM];
    bool wx_sensor_avg[WX_SENSOR_NUM];
    uint8_t wx_sensor_ch[WX_SENSOR_NUM];

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

    // Per-service telemetry (5ch) used on trk/digi/igate beacons
    bool trk_tlm_avg[TLM_CH];
    uint8_t trk_tlm_sensor[TLM_CH];
    uint8_t trk_tlm_precision[TLM_CH];
    float trk_tlm_offset[TLM_CH];
    char trk_tlm_PARM[TLM_CH][10];
    char trk_tlm_UNIT[TLM_CH][8];
    bool digi_tlm_avg[TLM_CH];
    uint8_t digi_tlm_sensor[TLM_CH];
    uint8_t digi_tlm_precision[TLM_CH];
    float digi_tlm_offset[TLM_CH];
    char digi_tlm_PARM[TLM_CH][10];
    char digi_tlm_UNIT[TLM_CH][8];
    bool igate_tlm_avg[TLM_CH];
    uint8_t igate_tlm_sensor[TLM_CH];
    uint8_t igate_tlm_precision[TLM_CH];
    float igate_tlm_offset[TLM_CH];
    char igate_tlm_PARM[TLM_CH][10];
    char igate_tlm_UNIT[TLM_CH][8];

    // AFSK / TNC
    bool audio_modem_en; // Enable the audio ADC/DAC AFSK modem
    bool audio_lpf;
    uint16_t preamble;
    uint8_t modem_type;      // RF module modem mode (RF_MODE_OFF/LoRa/G3RUH/GFSK/DPRS) - only used when ENABLE_RF_MODULE
    uint8_t afsk_modem_type; // Audio ADC/DAC AFSK modulation (see enum ModemType in modem.h: 0=300Bd,1=1200Bd,2=1200Bd V.23,3=9600Bd), used for both RX and TX on the audio modem
    uint8_t fx25_mode;
    uint16_t tx_timeslot;
    char ntp_host[NTP_HOST_NUM][20];
    uint16_t ntp_resync_sec;

    // System / HTTP auth
    char http_username[32];
    char http_password[64];
    char path[4][72];
    char host_name[32];
    uint16_t reset_timeout;
    uint16_t log;

    // Audio modem PTT.
    //
    // These two are the ONLY hardware-pin fields left in this struct. Every
    // other pin the audio modem uses (ADC in, DAC out, optional TX/RX LEDs) is
    // a compile-time constant supplied by the top-level CMakeLists.txt, and the
    // long tail of RF-module / I2C / 1-Wire / UART / Modbus / counter / power /
    // GSM / GNSS pin fields that used to live here was removed: nothing read
    // them, they only inflated /storage/config.json.
    //
    // PTT stays runtime-selectable because aprs_service_build_modem_config()
    // really does push it into modem_config_t.ptt_gpio/.ptt_active_high on
    // every boot and on every Radio-page Save. The factory default below is
    // derived from the same MODEM_PTT_GPIO / MODEM_PTT_ACTIVE_HIGH macros the
    // component itself defaults to, so the build system stays the single
    // source of truth for the board wiring (see app_config.c).
    int8_t rf_ptt_gpio;
    bool rf_ptt_active;

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

// ---------------------------------------------------------------------------
// g_config concurrency lock
//
// g_config is written field-by-field by the web POST handlers (a single
// settings save rewrites many fields, several of them strings/arrays, one at a
// time) while long-running tasks (beacon builders, IGate login, digipeater,
// message, weather) read those same fields. A reader that samples a string
// mid-strcpy can see a torn or transiently non-NUL-terminated value and walk
// off the end of the buffer. This lock serializes those two sides.
//
// It is DISTINCT from the internal save mutex (which is held across the whole
// flash serialization and would stall readers): this one is only ever held
// long enough to copy the needed fields into locals, so it is a strict LEAF
// lock - never hold it across a blocking call, I/O, transmit, or another lock.
//
// Usage:
//   - Writers (web handlers, factory reset): hold it around the block that
//     mutates g_config. Release it before app_config_save() / restarts.
//   - Readers of STRING/ARRAY fields: hold it just long enough to memcpy the
//     fields into a local snapshot, then release and work from the snapshot.
//   - Scalar (single-word) fields are word-atomic on this MCU and may be read
//     lock-free; only strings/arrays and multi-field-consistency need the lock.
//
// The lock is created lazily on first use (same one-time-init guard the save
// mutex uses), so there is no init-order dependency.
void app_config_lock(void);
void app_config_unlock(void);

#endif // APP_CONFIG_H
