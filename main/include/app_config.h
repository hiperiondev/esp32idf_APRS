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
 * @brief Global application configuration type (::app_config_t) and the
 * ::g_config instance, plus the compile-time module (@c ENABLE_*) and UI
 * language (@c LANGUAGE) selection macros.
 *
 * Field names and JSON keys are kept 1:1 with the original include/config.h and
 * src/config.cpp so that every value the web admin shows/edits has a home here
 * and persists to LittleFS as /storage/config.json.
 *
 * EXCEPT for the settings whose subsystems this firmware does not implement.
 * Bluetooth, the OLED/display, WireGuard, GNSS, MQTT, the PPP/GSM modem, the
 * I2C/1-Wire/UART/Modbus/pulse-counter/external-TNC/power-management pin sets,
 * the AT-command routing flags, and the RF-module and audio-front-end pin
 * fields are not part of this configuration: keeping only keys the firmware
 * actually reads keeps config.json small, which matters directly because
 * app_config_save() runs against a small, fragmented heap (see the streaming
 * writer there), so every key that changes nothing is pure cost. Unknown keys
 * left in an existing config.json are simply ignored by config_from_json(), so
 * older files still load. rf_ptt_gpio and rf_ptt_active are not stored either
 * (the PTT pin and its active level are fixed compile-time constants,
 * MODEM_PTT_GPIO and MODEM_PTT_ACTIVE_HIGH), and both are simply ignored if
 * present in an existing config.json.
 *
 * Exactly one language is built into the firmware image at a time - there is no
 * runtime language switch and no other language's strings are compiled in. To
 * change the language, change @c LANGUAGE below to one of the @c LANG_* codes (or
 * override it from the build system, e.g. idf.py build -DLANGUAGE=LANG_ES, or
 * `set(LANGUAGE LANG_ES)` / target_compile_definitions in CMakeLists.txt) and
 * rebuild. See translations/translations.h for how the selection works and
 * translations/lang_en.h / translations/lang_es.h / translations/lang_it.h for the string tables.
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "must_check.h" // APRS_MUST_CHECK: the persistence entry points below may not have their result discarded

/**
 * @name Firmware UI language selection
 *
 * Exactly one language is built into the firmware image at a time - there is
 * no runtime language switch and no other language's strings are compiled in.
 * To change the language, change @c LANGUAGE to one of the @c LANG_* codes and
 * rebuild. See translations/translations.h for how the selection works and
 * translations/lang_*.h for the string tables.
 * @{
 */
#define LANG_EN 0 /**< English. */
#define LANG_ES 1 /**< Espanol (Spanish). */
#define LANG_IT 2 /**< Italiano (Italian). */

#ifndef LANGUAGE
#define LANGUAGE LANG_EN /**< Active UI language, one of the @c LANG_* codes. Overridable from the build system. */
#endif
/** @} */

/**
 * @name Compile-time module enables
 *
 * Each of these selects one web admin page / firmware feature into the build.
 * Modules that are not implemented are simply not defined here.
 * @{
 */
#define ENABLE_DASHBOARD      /**< Dashboard page. */
#define ENABLE_MSG_CHAT       /**< "Snd/Rcv Msg" chat page. */
#define ENABLE_BULLETINS      /**< APRS bulletins page. */
#define ENABLE_OBJECTS_ITEMS  /**< APRS Objects/Items page. */
#define ENABLE_STATION        /**< "My Station" page. */
#define ENABLE_RADIO_MODEM    /**< Radiomodem page. */
#define ENABLE_MESSAGE        /**< APRS Message service page. */
#define ENABLE_IGATE          /**< IGate page. */
#define ENABLE_QUERY          /**< Query responder page. */
#define ENABLE_DIGIPEATER     /**< Digipeater page. */
#define ENABLE_TRACKER        /**< Tracker page. */
#define ENABLE_WEATHER        /**< Weather page. */
#define ENABLE_TELEMETRY      /**< Telemetry page. */
#define ENABLE_SYSTEM         /**< System page. */
#define ENABLE_WIRELESS       /**< Wireless page. */
#define ENABLE_FILE_STORAGE   /**< File Storage page. */
#define ENABLE_ABOUT_FIRMWARE /**< About / Firmware page. */
/** @} */

/**
 * @brief Size of the comment buffers (IGate/Digipeater/Tracker/Weather):
 * 128 bytes of comment text + NUL terminator.
 */
#define COMMENT_SIZE 129
/**
 * @brief Size of the status-text buffers.
 */
#define STATUS_SIZE 50

/**
 * @brief Weather station "Sensor Mapping" rows: the canonical on-air list of
 * every quantity an APRS Weather Report can carry, one row per mappable
 * measurement.
 *
 * @details This is the on-air list defined by APRS101 ch.12 plus the APRS 1.2
 * flood proposals. Each row corresponds 1:1 to a field the WX encoder in
 * weather.c emits and is used to index the per-field mapping arrays
 * (@c wx_sensor_* in ::app_config_t).
 *
 * The list is limited to quantities that have a Weather Report token on-air.
 * Extended measurements such as UV, soil moisture, water level or battery
 * voltage have no such token and are carried as Telemetry instead, so they are
 * deliberately absent here.
 */
typedef enum {
    WX_FIELD_WIND_DIRECTION = 0, /**< Wind direction, deg     -> "ddd/" (aprs_wind_t.direction_deg). */
    WX_FIELD_WIND_SPEED,         /**< Sustained wind, mph     -> "/sss" (aprs_wind_t.sustained_mph). */
    WX_FIELD_WIND_GUST,          /**< Peak gust, mph          -> "gXXX" (aprs_wind_t.gust_mph). */
    WX_FIELD_TEMPERATURE,        /**< Air temperature, deg F  -> "tXXX". */
    WX_FIELD_RAIN_1H,            /**< Rain last hour, 1/100 in-> "rXXX". */
    WX_FIELD_RAIN_24H,           /**< Rain last 24h, 1/100 in -> "pXXX". */
    WX_FIELD_RAIN_MIDNIGHT,      /**< Rain since midnight      -> "PXXX". */
    WX_FIELD_SNOW_24H,           /**< Snow last 24h, 1/10 in  -> "sXXX" or "sX.X" below 10 in (see aprs.org/aprs11/spec-wx.txt). */
    WX_FIELD_HUMIDITY,           /**< Relative humidity, %    -> "hXX". */
    WX_FIELD_PRESSURE,           /**< Barometric pressure     -> "bXXXXX" (tenths of mb). */
    WX_FIELD_LUMINOSITY,         /**< Solar luminosity, W/m^2 -> "LXXX"/"lXXX" (APRS 1.2). */
    WX_FIELD_FLOOD_HEIGHT_FT,    /**< Flood/water gauge, feet -> "FXXXX.X" (APRS 1.2). */
    WX_FIELD_FLOOD_HEIGHT_M,     /**< Flood/water gauge, m    -> "fXXXX.X" (APRS 1.2). */
    WX_SENSOR_NUM                /**< Sentinel: number of mappable WX fields. Not a real field. */
} wx_field_id_t;

/** @brief Number of stored WiFi station (STA) profiles. */
#define WIFI_STA_NUM 5
/** @brief Number of configurable NTP host slots. */
#define NTP_HOST_NUM 3
/** @brief Minimum NTP resync interval, in seconds. */
#define NTP_RESYNC_MIN_SEC 30

/**
 * @name SoftAP channel range
 * @brief Accepted range and fallback for ::app_config_t::wifi_ap_ch.
 *
 * esp_wifi_set_config() rejects an AP channel outside this range with
 * ESP_ERR_INVALID_ARG, so the stored value is clamped both when a form is
 * saved and when config.json is loaded, and the AP setup in main.c never
 * treats a driver rejection as fatal.
 * @{
 */
#define WIFI_AP_CH_MIN     1  /**< Lowest usable SoftAP channel. */
#define WIFI_AP_CH_MAX     13 /**< Highest usable SoftAP channel. */
#define WIFI_AP_CH_DEFAULT 1  /**< Fallback SoftAP channel, used whenever the stored one is out of range. */
/** @} */

/**
 * @name APRS-IS server port range
 * @brief Accepted range and fallback for ::app_config_t::aprs_port.
 *
 * Port 0 fits in a uint16_t but is not a connectable TCP port: getaddrinfo()
 * accepts the service string "0" and the following connect() then fails, so
 * the IGate would sit in a reconnect loop reporting a destination it could
 * never have reached. The value is therefore clamped both when the form is
 * saved and when config.json is loaded.
 * @{
 */
#define APRS_PORT_MIN     1     /**< Lowest connectable TCP port. */
#define APRS_PORT_MAX     65535 /**< Highest TCP port. */
#define APRS_PORT_DEFAULT 14580 /**< Fallback APRS-IS port (the filtered-feed port), used whenever the stored one is out of range. */
/** @} */

/**
 * @brief Number of stored APRS-IS server slots (see ::app_config_t::aprs_server).
 *
 * The IGate task cycles through the enabled slots in order and wraps back to
 * the first one, so any slot left disabled is simply skipped rather than
 * shortening the rotation to fewer than ::APRS_SERVER_NUM entries.
 */
#define APRS_SERVER_NUM 4

/**
 * @name Activate bit flags
 * @brief Bit flags used by path/object activation selectors.
 * @{
 */
#define ACTIVATE_OFF       0        /**< No services active. */
#define ACTIVATE_TRACKER   (1 << 0) /**< Tracker active. */
#define ACTIVATE_IGATE     (1 << 1) /**< IGate active. */
#define ACTIVATE_DIGI      (1 << 2) /**< Digipeater active. */
#define ACTIVATE_WX        (1 << 3) /**< Weather active. */
#define ACTIVATE_TELEMETRY (1 << 4) /**< Telemetry active. */
#define ACTIVATE_QUERY     (1 << 5) /**< Query responder active. */
#define ACTIVATE_STATUS    (1 << 6) /**< Status beacon active. */
#define ACTIVATE_WIFI      (1 << 7) /**< WiFi active. */
/** @} */

/**
 * @name IGate [Filter] section bit flags
 * @brief Shared payload-type bits for rf2inetFilter / inet2rfFilter (see
 * aprs_filter.h). A set bit means "relay this payload type".
 * @{
 */
#define IGATE_FILT_MESSAGE   (1 << 0) /**< APRS messages. */
#define IGATE_FILT_STATUS    (1 << 1) /**< Status reports. */
#define IGATE_FILT_TELEMETRY (1 << 2) /**< Telemetry reports. */
#define IGATE_FILT_WEATHER   (1 << 3) /**< Weather reports. */
#define IGATE_FILT_OBJECT    (1 << 4) /**< Object reports. */
#define IGATE_FILT_ITEM      (1 << 5) /**< Item reports. */
#define IGATE_FILT_QUERY     (1 << 6) /**< Queries. */
#define IGATE_FILT_BUOY      (1 << 7) /**< Buoy position reports. */
#define IGATE_FILT_POSITION  (1 << 8) /**< Plain position reports. */
/** @} */

/**
 * @name INET->RF message gating window
 * @brief Accepted range and factory default for
 * ::app_config_t::igate_local_window_sec.
 *
 * The window is how far back the IGate looks in the last-heard table when it
 * decides whether a station counts as "heard locally": an APRS message read
 * from APRS-IS is only put on the air when its addressee was heard on RF
 * inside this many seconds. One hour is the upper bound the APRS-IS IGate
 * design notes recommend, and is also the factory default; shortening it makes
 * the gateway more conservative on a busy channel.
 * @{
 */
#define IGATE_LOCAL_WINDOW_SEC_MIN     60   /**< Shortest accepted "heard locally" window, seconds. */
#define IGATE_LOCAL_WINDOW_SEC_MAX     3600 /**< Longest accepted "heard locally" window, seconds. */
#define IGATE_LOCAL_WINDOW_SEC_DEFAULT 3600 /**< Factory default "heard locally" window, seconds. */
/** @} */

/**
 * @brief Number of addressees remembered for the associated-position rule
 * (see the message gate in aprs_service.c's inet2rfHandler()).
 *
 * Every station this gateway puts a message on the air for takes one slot; the
 * next position report seen for that station on the APRS-IS feed is gated once
 * so the local operator has something to plot, and the slot is released. The
 * ring is deliberately small: it is a courtesy follow-up for conversations
 * happening right now, not a station database.
 */
#define IGATE_MSG_ASSOC_MAX 8

/**
 * @brief Maximum number of entries in the local callsign whitelist/blacklist
 * (see g_config.budlist / aprs_filter_budlist_pass()).
 */
#define IGATE_BUDLIST_MAX 8

/**
 * @brief Maximum number of entries in the satellite/ISS digipeater gate-call
 * list (see g_config.satgate / igateProcess()'s satellite-gate check).
 */
#define IGATE_SATGATE_MAX 8

/**
 * @brief Duplicate-suppression cache bounds shared by the IGate and
 * digipeater ::dup_scope_t windows (see igate.h).
 *
 * @c DUP_CACHE_SIZE_MIN/MAX bound @c app_config_t.dup_cache_size (the number
 * of recent frames kept) and @c DUP_CACHE_TIMEOUT_MS_MIN/MAX bound
 * @c app_config_t.dup_cache_timeout_ms (the window, in milliseconds, after
 * which an entry stops counting as a duplicate). The cache array itself is
 * still allocated at the fixed compile-time capacity DUP_CACHE_SIZE_MAX (see
 * igate.c); dup_cache_size only selects how much of that capacity is
 * actually used at runtime.
 * @{
 */
#define DUP_CACHE_SIZE_MAX           40     /**< Compile-time capacity of the duplicate cache array (igate.c). */
#define DUP_CACHE_SIZE_MIN           4      /**< Lowest g_config.dup_cache_size accepted from the web form / config.json. */
#define DUP_CACHE_SIZE_DEFAULT       20     /**< Factory default for g_config.dup_cache_size. */
#define DUP_CACHE_TIMEOUT_MS_MIN     1000   /**< Lowest g_config.dup_cache_timeout_ms accepted from the web form / config.json. */
#define DUP_CACHE_TIMEOUT_MS_MAX     120000 /**< Highest g_config.dup_cache_timeout_ms accepted from the web form / config.json. */
#define DUP_CACHE_TIMEOUT_MS_DEFAULT 30000  /**< Factory default for g_config.dup_cache_timeout_ms, in milliseconds. */
/** @} */

/**
 * @brief Per-direction mode for the local callsign whitelist/blacklist
 * (g_config.rf2inet_budlist_mode / g_config.inet2rf_budlist_mode). Composes
 * with (ANDs against) the existing rf2inetFilter/inet2rfFilter payload-type
 * filters: a packet must pass both.
 */
typedef enum {
    BUDLIST_OFF = 0,   /**< Callsign filter disabled for this direction. */
    BUDLIST_WHITELIST, /**< Only callsigns in g_config.budlist[] are allowed through. */
    BUDLIST_BLACKLIST, /**< Callsigns in g_config.budlist[] are blocked; everyone else passes. */
} budlist_mode_t;

/**
 * @name Digipeater alias table
 * @brief Bounds and text conventions for ::app_config_t::digi_alias.
 *
 * The digipeater recognises no path aliases of its own: every alias it honours
 * is a row of this table, which is what makes the New n-N Paradigm's local
 * conventions (a fill-in ``WIDE1-1``, a two-hop ``WIDE2-2``, a regional
 * ``SSn-N``) an operator setting rather than a firmware constant.
 *
 * An alias is written as the repeater callsign without its SSID - the SSID is
 * the hop count @c N and is handled separately - and may contain
 * ::DIGI_ALIAS_WILDCARD in any position, which matches exactly one decimal
 * digit. That is how one row covers a whole family: @c "WIDE#" matches
 * @c WIDE1 through @c WIDE9 but never @c WIDE, @c WIDEN or @c WIDE12, because
 * the match also requires equal length. Rows are consulted in order and the
 * first one that matches wins, so a specific row placed above a wildcard row
 * gives that one alias its own hop limit.
 * @{
 */
#define DIGI_ALIAS_MAX 4 /**< Rows in the digipeater alias table. */
#define DIGI_ALIAS_LEN 7 /**< Storage for one alias: 6 AX.25 callsign characters + NUL. */
#define DIGI_ALIAS_WILDCARD                                                                                                                                    \
    '#'                    /**< Alias character matching exactly one decimal digit. Not a legal AX.25 callsign character, so it can never be ambiguous.        \
                            */
#define DIGI_ALIAS_MAX_N 7 /**< Highest hop limit selectable for one alias row. */
/** @} */

/**
 * @brief How one ::digi_alias_t row is repeated.
 *
 * The distinction is the one the New n-N Paradigm was introduced to make: a
 * traced hop is identifiable afterwards, a flooded hop is not.
 */
typedef enum {
    DIGI_ALIAS_OFF = 0, /**< Row unused: the alias is ignored, exactly as if it were not listed. */
    DIGI_ALIAS_TRACE, /**< Tracing: this station's callsign is inserted ahead of the remaining alias and marked used, so every hop of the path is identifiable.
                         This is what @c WIDEn-N is required to do. */
    DIGI_ALIAS_FLOOD, /**< Flooding: the hop count is decremented without inserting a callsign, so the route cannot be reconstructed. Only appropriate for a
                         regional @c SSn-N alias an operator deliberately runs untraced. */
} digi_alias_mode_t;

/**
 * @brief One digipeater path alias and the rules for repeating it.
 */
typedef struct {
    char alias[DIGI_ALIAS_LEN]; /**< Repeater callsign without SSID, ::DIGI_ALIAS_WILDCARD allowed. Empty disables the row. */
    uint8_t max_n;              /**< Highest hop count honoured for this alias, 1 to ::DIGI_ALIAS_MAX_N. A larger @c N received on air is trapped. */
    uint8_t mode;               /**< ::digi_alias_mode_t for this row. */
} digi_alias_t;

/**
 * @brief How the digipeater treats an explicit path that names this station
 *        further down than the first unused address.
 *
 * Preemptive digipeating (http://www.aprs.org/aprs12/preemptive-digipeating.txt)
 * scans from the first unused address to the end of the path for one of this
 * station's own identities, and repeats the frame on that match instead of
 * waiting to be reached one address at a time. It is what makes an explicit
 * route such as @c WIDE1-1,CITYA,WIDE2-1,CITYB work: without it a digipeater
 * named in such a path simply drops out of it, and the operator is pushed back
 * onto a @c WIDEn-N flood that loads the channel far more heavily.
 *
 * The two indicator modes differ only in what is left of the addresses that
 * were skipped, and neither applies to generic @c XXXXn-N aliases, which the
 * scan never claims.
 */
typedef enum {
    DIGI_PREEMPT_OFF = 0, /**< No scan: only the first unused address can route the frame. */
    DIGI_PREEMPT_DROP, /**< The matched address is moved to the head of the path and everything before it is discarded, so the frame goes out with the shortest
                          path that still describes what is left to do. */
    DIGI_PREEMPT_MARK, /**< The skipped addresses are kept and marked as used, so the path still shows the route that was requested. */
} digi_preempt_mode_t;

/**
 * @brief One stored WiFi station (STA) profile.
 */
typedef struct {
    bool enable;        /**< Whether this STA profile is used. */
    char wifi_ssid[33]; /**< SSID: 32 chars max (IEEE 802.11 SSID limit) + NUL. */
    char wifi_pass[64]; /**< PSK: 63 chars max (WPA/WPA2/WPA3 limit) + NUL. */
} wifi_sta_t;

/**
 * @brief One stored APRS-IS server slot for IGate failover (see
 * ::app_config_t::aprs_server and ::APRS_SERVER_NUM).
 *
 * All slots share the login identity (aprs_mycall/aprs_ssid/aprs_passcode/
 * aprs_filter) - only the destination host/port differs between them, since
 * they represent the same station connecting to alternative APRS-IS servers.
 */
typedef struct {
    bool enable;   /**< Whether this server slot takes part in the failover rotation. A disabled slot is skipped. */
    char host[20]; /**< APRS-IS server hostname or IP address for this slot. */
    uint16_t port; /**< APRS-IS server TCP port for this slot, clamped to ::APRS_PORT_MIN .. ::APRS_PORT_MAX on save and on load. */
} aprs_server_t;

/**
 * @brief The resident application configuration, loaded at boot and edited by
 * the web POST handlers.
 *
 * @details The single ::g_config instance is the live copy every subsystem
 * reads. It persists to /storage/config.json (see app_config_save()). Fields
 * are grouped by web admin page: system/time, "My Station" identity, WiFi,
 * IGate, Digipeater, Tracker, Weather, the AFSK/TNC modem, System/HTTP auth,
 * the audio-modem PTT timing, and Message. Access to string/array fields must
 * be serialized with app_config_lock()/app_config_unlock() (see below).
 *
 * @note Telemetry configuration is deliberately NOT here: it lives in its own
 * /storage/telemetry.json (see telemetry.h). The audio-modem PTT GPIO and its
 * active level are also not here - they are fixed compile-time constants
 * (MODEM_PTT_GPIO / MODEM_PTT_ACTIVE_HIGH); only @c ptt_min_unkey_ms is
 * user-configurable.
 */
/**
 * @brief Which 7-byte APRS Data Extension a fixed-position beacon carries in
 * the slot immediately after the symbol code (APRS101 chapter 7).
 *
 * The three are mutually exclusive on air - they all occupy the same slot, the
 * one a moving station uses for CSE/SPD - so the beacon carries at most one of
 * them, selected here and gated by the beacon's own "enable data extension"
 * flag (@c app_config_t.igate_phg_enable).
 */
typedef enum {
    APRS_EXT_PHG = 0, /**< "PHGphgd": transmitter power, antenna height/gain and directivity. */
    APRS_EXT_RNG = 1, /**< "RNGrrrr": pre-calculated omnidirectional radio range, statute miles. */
    APRS_EXT_DFS = 2, /**< "DFSshgd": Omni-DF signal strength, with the same height/gain/directivity codes as PHG. */
} aprs_ext_type_t;

#define APRS_EXT_RANGE_MILES_MIN  0    /**< Lowest "RNGrrrr" pre-calculated radio range, statute miles. */
#define APRS_EXT_RANGE_MILES_MAX  9999 /**< Highest "RNGrrrr" pre-calculated radio range: the field is 4 digits wide. */
#define APRS_EXT_DFS_STRENGTH_MIN 0    /**< Lowest "DFSshgd" signal-strength code (0 = this station does NOT hear the signal). */
#define APRS_EXT_DFS_STRENGTH_MAX 9    /**< Highest "DFSshgd" signal-strength code, in S-points. */

/**
 * @brief Highest position-ambiguity level selectable on the Station page
 * (0 = full precision, 4 = nearest degree). Mirrors
 * ::APRS_COORD_AMBIGUITY_MAX, kept here so the web page and the JSON clamp
 * do not have to pull in aprs_coord.h.
 */
#define POS_AMBIGUITY_MAX 4

typedef struct {
    bool synctime;        /**< Enable SNTP time sync. */
    uint8_t cpuFreq;      /**< CPU clock frequency selection (80/160/240 MHz); see cpu_freq.h. */
    uint8_t timezone_idx; /**< Selected entry in the built-in timezone table (see ::time_sync_tz_t / time_sync_tz_count() in time_sync.h), used only to
                             render local date/time in the web admin (System page select, dashboard). The system clock itself (time(NULL)/gmtime_r())
                             stays UTC everywhere else in the firmware; see time_sync.h. */

    char my_callsign[10]; /**< "My Station" callsign, entered once on the Station page and reused by every page's "Use My Station Data". */
    float my_lat;         /**< "My Station" latitude, decimal degrees. */
    float my_lon;         /**< "My Station" longitude, decimal degrees. */
    float my_alt;         /**< "My Station" altitude. */

    uint16_t my_phg_power;  /**< "My Station" PHG sub-field: radio TX power, Watts (persisted so the form redisplays the selections). */
    float my_phg_gain;      /**< "My Station" PHG sub-field: antenna gain, dBi. */
    uint16_t my_phg_height; /**< "My Station" PHG sub-field: antenna height. Stored in feet (the unit the APRS PHG code table itself is defined in); the Station
                               page displays/edits this in meters and converts. */
    uint8_t my_phg_dir;     /**< "My Station" PHG sub-field: directivity, 0=Omni, 1-8 = N,NE,E,SE,S,SW,W,NW. */

    uint8_t pos_ambiguity;    /**< Position ambiguity applied to every uncompressed own-station position report, 0 (full precision) to ::POS_AMBIGUITY_MAX
                                 (nearest degree). See aprs_coord_format_ambiguous(). */
    bool status_grid_en;      /**< Prefix every own-station status report with the Maidenhead grid locator of the beacon's position (APRS101 chapter 16). */
    bool status_timestamp_en; /**< Prefix every own-station status report with the optional "DDHHMMz" zulu timestamp (APRS101 chapter 16), immediately
                                 after the '>' data type identifier and before any Maidenhead locator block. */
    bool pos_dao_en; /**< Append the WGS-84 human-readable "!DAO!" precision/datum extension (aprs12/datum.txt) to every uncompressed own-station position
                        report, recovering the third decimal minute digit of latitude/longitude that the plain "DDMM.mmN"/"DDDMM.mmW" fields round away. See
                        aprs_dao_build(). Only applied when pos_ambiguity is 0: a station deliberately obscuring its position must not have that precision
                        handed back via the extension. Never applied to the compressed layout (already full resolution) or to Mic-E. */

    uint8_t wifi_mode;                 /**< WiFi mode: 0=off, 1=STA, 2=AP, 3=AP_STA. */
    int8_t wifi_power;                 /**< WiFi TX power setting. */
    wifi_sta_t wifi_sta[WIFI_STA_NUM]; /**< The ::WIFI_STA_NUM stored STA profiles. */
    uint8_t wifi_ap_ch;                /**< SoftAP channel, clamped to ::WIFI_AP_CH_MIN .. ::WIFI_AP_CH_MAX on save and on load. */
    char wifi_ap_ssid[33];             /**< SoftAP SSID: 32 chars max + NUL. */
    char wifi_ap_pass[64];             /**< SoftAP PSK: 63 chars max + NUL. */

    bool igate_en;                       /**< IGate service enabled. */
    bool rf2inet;                        /**< Gateway RF -> APRS-IS. */
    bool inet2rf;                        /**< Gateway APRS-IS -> RF. */
    bool igate_loc2rf;                   /**< Beacon the IGate's own position on RF. */
    bool igate_loc2inet;                 /**< Beacon the IGate's own position to APRS-IS. */
    uint16_t rf2inetFilter;              /**< RF->INET payload-type filter (IGATE_FILT_* bitmask). */
    uint16_t inet2rfFilter;              /**< INET->RF payload-type filter (IGATE_FILT_* bitmask). */
    budlist_mode_t rf2inet_budlist_mode; /**< RF->INET local callsign whitelist/blacklist mode. */
    budlist_mode_t inet2rf_budlist_mode; /**< INET->RF local callsign whitelist/blacklist mode. */
    char budlist[IGATE_BUDLIST_MAX][10]; /**< Shared callsign list (base call, no SSID) used by both directions' whitelist/blacklist. */

    char satgate[IGATE_SATGATE_MAX][10]; /**< Satellite/ISS digipeater gate-call list (base call, no SSID) checked against the repeater path in igateProcess();
                                            an empty slot is simply skipped. Web-configurable (IGate page, parallel to budlist); the factory default fills
                                            the first six slots with the common amateur satellite digipeater calls. */
    uint8_t dup_cache_size;              /**< Number of recent frames kept for duplicate suppression (shared by every ::dup_scope_t). Clamped to
                                            DUP_CACHE_SIZE_MIN..DUP_CACHE_SIZE_MAX; see igate.c. */
    uint32_t dup_cache_timeout_ms;       /**< Duplicate-suppression window, in milliseconds. Clamped to DUP_CACHE_TIMEOUT_MS_MIN..DUP_CACHE_TIMEOUT_MS_MAX;
                                            see igate.c. */

    bool rf2inet_range_en;  /**< Enable the local RF->INET range gate (see aprs_filter_haversine_km()). Independent of, and composed with (AND semantics),
                               rf2inetFilter/the budlist. */
    float rf2inet_range_km; /**< Max allowed distance from "My Station" (my_lat/my_lon), km. 0 = unlimited (gate has no effect even if enabled). Packets whose
                               position can't be decoded are not evaluated (pass this check). */
    bool rf2inet_prefix_en; /**< Enable the local RF->INET callsign-prefix gate. */
    char rf2inet_prefixes[40]; /**< Comma-separated callsign-prefix whitelist for rf2inet_prefix_en, e.g. "EA,EB,EC". Case-insensitive. */

    bool igate_msg_gate_en;          /**< On by default. Apply the APRS-IS message-gating criteria before putting a message read from APRS-IS on the air: the
                                        addressee must have been heard on RF inside @c igate_local_window_sec, the sender must not have been, the sender's
                                        header must carry no TCPXX/NOGATE/RFONLY, and the addressee must not be Internet-connected. */
    uint16_t igate_local_window_sec; /**< How far back the message gate looks in the last-heard table, seconds. Clamped to ::IGATE_LOCAL_WINDOW_SEC_MIN ..
                                        ::IGATE_LOCAL_WINDOW_SEC_MAX on save and on load. */
    bool inet2rf_3rdparty_unwrap_en; /**< Off by default. Selective INET->RF opt-in: unwrap one level of third-party ('}') traffic and re-classify/relay the
                                        inner packet, but ONLY when inet2rf_budlist_mode is BUDLIST_WHITELIST and the inner packet's source callsign is itself
                                        on budlist[] - see aprs_filter_classify_thirdparty_inner(). Never a general "relay all third-party" switch; misuse (or
                                        use without a whitelist) risks re-introducing an IGate loop. */
    uint8_t aprs_ssid;               /**< SSID for the IGate callsign. */
    char aprs_mycall[10];            /**< IGate callsign. */
    bool igate_use_station;          /**< "Use My Station Data": mirror My Station identity/position into the IGate fields and lock them. */
    aprs_server_t aprs_server[APRS_SERVER_NUM]; /**< The ::APRS_SERVER_NUM stored APRS-IS server slots the IGate task fails over between; see igate.c. */
    char aprs_passcode[6];                      /**< APRS-IS login passcode. */
    char aprs_filter[30];                       /**< APRS-IS server-side filter string. */
    bool igate_bcn;                             /**< Enable the IGate position beacon. */
    bool igate_timestamp;                       /**< Include a timestamp in the IGate beacon. */
    float igate_lat;                            /**< IGate beacon latitude. */
    float igate_lon;                            /**< IGate beacon longitude. */
    float igate_alt;                            /**< IGate beacon altitude. */
    uint16_t igate_interval;                    /**< IGate beacon interval, seconds. */
    char igate_symbol[3];                       /**< IGate APRS symbol ("<table><code>" + NUL). */
    uint8_t igate_path;                         /**< IGate digipeat-path selection (bitmask over g_config.path[0..3]). */
    char igate_comment[COMMENT_SIZE];           /**< IGate beacon comment. */
    uint16_t igate_sts_interval;                /**< IGate status-beacon interval, seconds. */
    char igate_status[STATUS_SIZE];             /**< IGate status text. */
    bool igate_compress;                        /**< Use APRS compressed position format for the IGate position beacon. */
    bool igate_phg_enable;                      /**< Enable transmitting the PHG data extension in the IGate position beacon. */
    bool igate_phg_use_station; /**< "Use My Station Data": mirror the shared "My Station" PHG sub-fields into the IGate PHG fields and lock them. */
    uint16_t igate_phg_power;   /**< PHG sub-field: radio TX power, Watts (persisted so the form redisplays the selections). */
    float igate_phg_gain;       /**< PHG sub-field: antenna gain, dBi. */
    uint16_t igate_phg_height;  /**< PHG sub-field: antenna height, feet. */
    uint8_t igate_phg_dir;      /**< PHG sub-field: directivity, 0=Omni, 1-8 = N,NE,E,SE,S,SW,W,NW. */
    uint8_t igate_ext_type;     /**< Which ::aprs_ext_type_t the IGate position beacon carries when @c igate_phg_enable is set. */
    uint16_t igate_range_miles; /**< "RNGrrrr" pre-calculated radio range, statute miles (::APRS_EXT_RNG only). */
    uint8_t igate_dfs_strength; /**< "DFSshgd" signal-strength code 0-9 (::APRS_EXT_DFS only; height/gain/directivity come from the PHG sub-fields). */

    float igate_freq_mhz;       /**< Recommended travelers' voice repeater frequency this digipeater advertises, MHz; 0 => no frequency block emitted. Built
                                   with objitem_build_freq_block() and prepended as the first 10 bytes of the IGate beacon comment (freqspec.txt); the same
                                   block is also carried in the IGate status report, the spec's documented fallback for radios that decode neither the
                                   frequency Object form nor a position comment. */
    uint16_t igate_tone_tenths; /**< Repeater CTCSS subaudible tone, tenths of Hz (e.g. 1000 = 100.0 Hz); 0 => "Toff". */
    int8_t igate_duplex;        /**< Repeater duplex direction: 0 = simplex (offset omitted), +1 = "+", -1 = "-". */
    uint16_t igate_offset_khz;  /**< Repeater duplex shift magnitude, kHz (e.g. 600); used only when igate_duplex != 0. */

    bool digi_en;                            /**< Digipeater service enabled. */
    bool digi_loc2rf;                        /**< Beacon the digipeater's own position on RF. */
    bool digi_loc2inet;                      /**< Beacon the digipeater's own position to APRS-IS. */
    bool digi_timestamp;                     /**< Include a timestamp in the digipeater beacon. */
    uint8_t digi_ssid;                       /**< SSID for the digipeater callsign. */
    char digi_mycall[10];                    /**< Digipeater callsign. */
    bool digi_use_station;                   /**< "Use My Station Data": mirror My Station into the digipeater fields and lock them. */
    uint8_t digi_path;                       /**< Digipeater beacon digipeat-path selection (bitmask over g_config.path[0..3]). */
    digi_alias_t digi_alias[DIGI_ALIAS_MAX]; /**< The only path aliases this digipeater honours; see ::digi_alias_t. */
    bool digi_fillin_only;                   /**< Fill-in (home) digipeater role: honour only single-hop rows, so the station serves stations that cannot reach
                                                the backbone directly and leaves multi-hop traffic to the wide digipeaters. */
    bool digi_trap_n_clamp;                  /**< What to do with a hop count above the matched row's @c max_n: true clamps it down to @c max_n and repeats,
                                                false drops the frame (@c DROP_DIGI_N_TRAPPED). */
    bool digi_dest_ssid_en;                  /**< Honour the pre-New-N convention that carries the hop count in the AX.25 destination SSID (1-7) instead of in
                                                the path. Off by default: it repeats on the strength of that SSID alone, ahead of and instead of the operator's
                                                alias table, so it is only appropriate where a legacy neighbour still needs it. */
    uint8_t digi_preempt;                    /**< ::digi_preempt_mode_t: whether the path is scanned past its first unused address for one of this station's own
                                                identities, and what is left of the addresses skipped when it is. ::DIGI_PREEMPT_OFF by default. */
    bool digi_bcn;                           /**< Enable the digipeater position beacon. */
    bool digi_compress;                      /**< Use APRS compressed position format for the digipeater position beacon. */
    float digi_lat;                          /**< Digipeater beacon latitude. */
    float digi_lon;                          /**< Digipeater beacon longitude. */
    float digi_alt;                          /**< Digipeater beacon altitude. */
    uint16_t digi_interval;                  /**< Digipeater beacon interval, seconds. */
    char digi_symbol[3];                     /**< Digipeater APRS symbol. */
    char digi_comment[COMMENT_SIZE];         /**< Digipeater beacon comment. */
    uint16_t digi_sts_interval;              /**< Digipeater status-beacon interval, seconds. */
    char digi_status[STATUS_SIZE];           /**< Digipeater status text. */

    float digi_freq_mhz;       /**< Recommended travelers' voice repeater frequency this digipeater advertises, MHz; 0 => no frequency block emitted. See
                                  igate_freq_mhz; freqspec.txt calls this out as specifically the digipeater's responsibility. */
    uint16_t digi_tone_tenths; /**< Repeater CTCSS subaudible tone, tenths of Hz; 0 => "Toff". */
    int8_t digi_duplex;        /**< Repeater duplex direction: 0 = simplex, +1 = "+", -1 = "-". */
    uint16_t digi_offset_khz;  /**< Repeater duplex shift magnitude, kHz; used only when digi_duplex != 0. */

    bool trk_en;                    /**< Tracker service enabled. */
    bool trk_loc2rf;                /**< Beacon the tracker position on RF. */
    bool trk_loc2inet;              /**< Beacon the tracker position to APRS-IS. */
    bool trk_timestamp;             /**< Include a timestamp in the tracker beacon. */
    uint8_t trk_ssid;               /**< SSID for the tracker callsign. */
    char trk_mycall[10];            /**< Tracker callsign. */
    bool trk_use_station;           /**< "Use My Station Data": mirror My Station into the tracker fields and lock them. */
    uint8_t trk_path;               /**< Tracker digipeat-path selection (bitmask over g_config.path[0..3]). */
    float trk_lat;                  /**< Tracker beacon latitude. */
    float trk_lon;                  /**< Tracker beacon longitude. */
    float trk_alt;                  /**< Tracker beacon altitude. */
    uint16_t trk_interval;          /**< Fixed tracker beacon period in seconds (see beacon.c). */
    bool trk_compress;              /**< Use APRS compressed position format. */
    bool trk_altitude;              /**< Include altitude in the beacon. */
    bool trk_mice;                  /**< Use Mic-E position encoding (APRS101 ch.10) instead of uncompressed/compressed; excludes trk_timestamp. */
    char trk_symbol[3];             /**< Tracker APRS symbol. */
    char trk_comment[COMMENT_SIZE]; /**< Tracker beacon comment. */
    uint16_t trk_sts_interval;      /**< Tracker status-beacon interval, seconds. */
    char trk_status[STATUS_SIZE];   /**< Tracker status text. */

    float trk_freq_mhz;       /**< Recommended travelers' voice repeater frequency this station advertises, MHz; 0 => no frequency block emitted. See
                                 igate_freq_mhz. */
    uint16_t trk_tone_tenths; /**< Repeater CTCSS subaudible tone, tenths of Hz; 0 => "Toff". */
    int8_t trk_duplex;        /**< Repeater duplex direction: 0 = simplex, +1 = "+", -1 = "-". */
    uint16_t trk_offset_khz;  /**< Repeater duplex shift magnitude, kHz; used only when trk_duplex != 0. */

    bool wx_en;                           /**< Weather service enabled. */
    bool wx_2rf;                          /**< Transmit the WX report on RF. */
    bool wx_2inet;                        /**< Transmit the WX report to APRS-IS. */
    bool wx_timestamp;                    /**< Include a timestamp in the WX report. */
    uint8_t wx_ssid;                      /**< SSID for the WX callsign. */
    char wx_mycall[10];                   /**< WX callsign. */
    bool wx_use_station;                  /**< "Use My Station Data": mirror My Station into the WX fields and lock them. */
    uint8_t wx_path;                      /**< WX digipeat-path selection (bitmask over g_config.path[0..3]). */
    float wx_lat;                         /**< WX report latitude. */
    float wx_lon;                         /**< WX report longitude. */
    uint16_t wx_interval;                 /**< WX report interval, seconds. */
    char wx_object[10];                   /**< WX object name (if beaconing as an object). */
    char wx_comment[COMMENT_SIZE];        /**< WX report comment. */
    bool wx_sensor_enable[WX_SENSOR_NUM]; /**< Per-field enable, indexed by ::wx_field_id_t. */
    bool wx_sensor_avg[WX_SENSOR_NUM];    /**< Per-field averaging enable, indexed by ::wx_field_id_t. */
    uint8_t wx_sensor_ch[WX_SENSOR_NUM];  /**< Per-field source sensor channel, indexed by ::wx_field_id_t (::SENSOR_LOCAL_CH_NONE = "(none)"); persisted by
                                             driver name. */

    bool audio_modem_en;     /**< Enable the audio ADC/DAC AFSK modem. */
    bool audio_lpf;          /**< Enable the audio low-pass filter. */
    uint16_t preamble;       /**< TXDelay (preamble) length, ms. */
    uint8_t afsk_modem_type; /**< Audio AFSK modulation (::modem_mode_t: 0=AFSK300, 1=Bell202, 2=V.23, 3=G3RUH); used for both RX and TX. */
    uint8_t fx25_mode;       /**< FX.25 mode: 0=off, 1=RX only, 2=RX+TX. */
    uint16_t tx_timeslot;    /**< CSMA quiet time, ms: how long a queued frame waits before channel access begins at all. The interval between the individual
                                persistence rolls that follow is the fixed AX.25 "SlotTime" the modem keeps internally, not this value. */
    uint8_t csma_persist;    /**< CSMA/p-persistent channel-access probability (standard AX.25/KISS "Persist"): once the quiet time has elapsed and the
                                channel is heard clear, the modem transmits immediately with probability csma_persist/256 on every slot and otherwise waits
                                one more slot time before rolling again. 255 transmits on the first clear slot every time (equivalent to plain
                                non-persistent CSMA); lower values spread contending stations' key-ups further apart. A run of eight missed rolls transmits
                                anyway so a frame is never held indefinitely, which at the default of 63 happens on roughly one key-up in ten and is
                                reported as the second CSMA figure on the dashboard. Web-configurable (Radiomodem page, Audio/AFSK section), applied live via
                                aprs_service_apply_modem_config(). Range 1..255. */

    uint8_t rf_tx_buffers; /**< Max frames allowed to sit in the RF TX ring before aprs_service_send_tnc2() starts discarding new packets. Web-configurable and
                              applied live (read on every transmit). Range RF_TX_BUFFERS_MIN..RF_TX_BUFFERS_MAX (aprs_service.h), default 1. */

    bool duty_cycle_en;     /**< Long-term TX duty-cycle limiter enabled. Off by default. When on, aprs_service.c holds back non-critical RF TX (own-station
                                beacons, objects/items, weather, telemetry, bulletins, and the bulk IGate INET->RF relay) once this station's own measured
                                transmit airtime over its rolling window reaches duty_cycle_pct; APRS messages/acks and digipeat repeats are always exempt.
                                Independent of, and in addition to, the CSMA channel-access checks (tx_timeslot/csma_persist above), which only prevent
                                collisions and have no memory of this station's own past transmissions. */
    uint8_t duty_cycle_pct; /**< Duty-cycle ceiling, as a percentage of the rolling window aprs_service.c measures it over, enforced only while
                                duty_cycle_en is on. Web-configurable and applied live (read on every non-critical transmit, no reboot needed). Range
                                DUTY_CYCLE_PCT_MIN..DUTY_CYCLE_PCT_MAX (aprs_service.h), default 25. */

    char ntp_host[NTP_HOST_NUM][20]; /**< Up to ::NTP_HOST_NUM NTP server hostnames. */
    uint16_t ntp_resync_sec;         /**< NTP resync interval, seconds (floored at ::NTP_RESYNC_MIN_SEC). */

    char http_username[32]; /**< Web admin HTTP Basic auth username. */
    char http_password[64]; /**< Web admin HTTP Basic auth password. */
    char path[4][72];       /**< The four shared digipeat-path presets selected by the per-service path bitmasks. */

    uint16_t ptt_min_unkey_ms; /**< Extra minimum PTT-off (unkeyed) hold time between transmissions, ms, on top of the fixed one-service-tick (~10 ms) release
                                  the modem always applies. 0 disables the extra hold. Web-configurable and applied live via aprs_service_apply_modem_config().
                                  Range 0..5000 ms. */

    bool msg_enable;       /**< APRS Message service enabled. */
    char msg_mycall[10];   /**< Message service callsign. */
    bool msg_use_station;  /**< "Use My Station Data": mirror My Station callsign into @c msg_mycall and lock it. */
    uint8_t msg_path;      /**< Message digipeat-path selection (bitmask over g_config.path[0..3]). */
    bool msg_rf;           /**< Send messages on RF. */
    bool msg_inet;         /**< Send messages to APRS-IS. */
    uint8_t msg_retry;     /**< Number of message retries before giving up. */
    uint16_t msg_interval; /**< Message retry interval, seconds. */
    bool msg_alarm_enable; /**< "Message Alarm": drive a GPIO on incoming message (disabled by default). */
    int8_t msg_alarm_gpio; /**< Message-alarm GPIO; -1 = disabled/unset (see message_alarm_gpio_is_valid()). */

    bool query_en;                   /**< Query responder master enable. */
    bool query_rf;                   /**< Answer queries heard on RF; the answer goes back out on RF. */
    bool query_inet;                 /**< Answer queries read from the APRS-IS feed; the answer goes back to APRS-IS. */
    bool query_aprs_en;              /**< Enable "?APRS?" responses. */
    bool query_wx_en;                /**< Enable "?WX?" responses. */
    bool query_igate_en;             /**< Enable "?IGATE?" responses. */
    bool query_directed_en;          /**< Enable directed "CALL:?query?" responses. */
    bool query_ext_en;               /**< Enable the extended directed query set (?APRSD/?APRSH/?APRSM/?APRSO/?APRSP/?APRSS/?APRST/?PING?). */
    uint16_t query_min_interval_sec; /**< Per-type broadcast query rate limit, seconds (floored at 5). */

} app_config_t;

/**
 * @brief Global live configuration instance (loaded at boot, edited by web
 * handlers, saved to flash).
 */
extern app_config_t g_config;

/**
 * @brief Fill @p c with the same defaults esp32idf_APRS ships with.
 * @param c Destination configuration (must be non-NULL).
 */
void app_config_set_defaults(app_config_t *c);

/**
 * @brief Load /storage/config.json into ::g_config.
 *
 * If the file is missing/corrupt, defaults are applied and immediately saved
 * so the file always exists and is consistent.
 *
 * @return true if an existing file was loaded, false if defaults were written.
 */
bool app_config_load(void);

/**
 * @brief Serialize ::g_config to /storage/config.json (atomic: write tmp then
 * rename).
 * @return true on success.
 *
 * @note Declared ::APRS_MUST_CHECK: a call site that discards the result
 * reports success to the user for a write that may never have reached
 * flash, so ignoring it fails the build.
 */
bool app_config_save(void) APRS_MUST_CHECK;

/**
 * @brief Wipe the configuration back to factory defaults and persist.
 * @return true on success.
 */
bool app_config_factory_reset(void);

/**
 * @brief Acquire the ::g_config concurrency lock.
 *
 * @details ::g_config is written field-by-field by the web POST handlers (a
 * single settings save rewrites many fields, several of them strings/arrays,
 * one at a time) while long-running tasks (beacon builders, IGate login,
 * digipeater, message, weather) read those same fields. A reader that samples
 * a string mid-strcpy can see a torn or transiently non-NUL-terminated value
 * and walk off the end of the buffer. This lock serializes those two sides.
 *
 * It is DISTINCT from the internal save mutex (held across the whole flash
 * serialization): this one is a strict LEAF lock, held only long enough to
 * copy the needed fields into locals - never across a blocking call, I/O,
 * transmit, or another lock. Writers hold it around the block that mutates
 * ::g_config (releasing it before app_config_save()/restarts); readers of
 * string/array fields hold it just long enough to memcpy a local snapshot.
 * Scalar (single-word) fields are word-atomic on this MCU and may be read
 * lock-free. The lock is created lazily on first use, so there is no
 * init-order dependency.
 */
void app_config_lock(void);

/**
 * @brief Release the ::g_config concurrency lock acquired with
 * app_config_lock().
 */
void app_config_unlock(void);

/**
 * @brief Count the AX.25 path hops that @p pathBitmask would produce from
 * @p pathPreset[0..3].
 *
 * Each selected preset slot may itself hold several comma-separated hops
 * (e.g. "WIDE1-1,WIDE2-1" counts as 2, not 1), so simply counting set bits
 * undercounts the real on-air path length. Empty/unselected slots contribute
 * 0. This is the single source of truth for "how many hops does this
 * bitmask produce", shared by the webconfig POST handlers (which enforce it
 * at save time via app_config_path_mask_clamp()) and
 * aprs_path_build_suffix() (which enforces it again at transmit time, for
 * every service that originates traffic), so the two can never drift out of
 * sync.
 *
 * @param pathBitmask Bitmask over @p pathPreset (bit N selects pathPreset[N]).
 * @param pathPreset  The four shared path presets, i.e. a copy of/reference
 *                    to ::g_config.path[0..3] taken while holding
 *                    app_config_lock().
 * @return Total number of comma-separated hops the selection would emit.
 */
uint8_t app_config_path_hop_count(uint8_t pathBitmask, const char pathPreset[4][72]);

/**
 * @brief Clamp @p pathBitmask so the path it produces never exceeds AX.25's
 * 8-via limit.
 *
 * Bits are kept low-to-high (preset 1, then 2, then 3, then 4) only as long
 * as adding the next preset's hops would not push the running total past 8;
 * any bit that would exceed the budget, and every bit after it, is dropped.
 * If @p pathBitmask already produces 8 or fewer hops it is returned
 * unchanged. A dropped bit is logged as a warning so an over-long save is
 * visible in the device log even though the web form has no
 * validation-error plumbing to surface it to the browser.
 *
 * @param pathBitmask Raw bitmask read back from the web form (or NVS/restore).
 * @param pathPreset  The four shared path presets (see
 *                    app_config_path_hop_count()).
 * @return @p pathBitmask, with the minimum number of high bits cleared to
 *         bring the total hop count to 8 or fewer.
 */
uint8_t app_config_path_mask_clamp(uint8_t pathBitmask, const char pathPreset[4][72]);

#endif // APP_CONFIG_H
