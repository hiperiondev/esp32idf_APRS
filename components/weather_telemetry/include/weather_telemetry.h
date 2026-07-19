/**
 * @file weather_telemetry.h
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
 * @details
 * This header is a direct, field-by-field transcription of the on-air data
 * formats defined by:
 *   - APRS Protocol Reference, Version 1.0.1 ("APRS101.PDF"), APRS Working
 *     Group, 29 August 2000 - chapters 5 (Data Type Identifiers), 6 (Time
 *     and Position), 7 (Data Extensions), 12 (Weather Reports) and 13
 *     (Telemetry Data).
 *   - APRS 1.1 Addendum (2004) - http://www.aprs.org/aprs11.html
 *   - APRS 1.2 Proposals - http://www.aprs.org/aprs12.html
 *   - WX.TXT "Using APRS in Weather and Skywarn Applications", Bob
 *     Bruninga WB4APR - http://www.aprs.org/APRS-docs/WX.TXT
 *   - Peet Bros Ultimeter / Davis raw weather station formats referenced
 *     by the above documents.
 *   - The community-extended ("Kenneth's proposed") Telemetry format that
 *     relaxes the strict 3-digit / 0-255 analog value constraint used by
 *     modern telemetry-capable trackers and igates such as aprs.fi.
 *
 * Every field below documents:
 *   - The literal on-air character sequence it corresponds to.
 *   - Its valid range, unit and resolution as transmitted over RF.
 *   - Whether it belongs to the strict APRS101 core specification or to a
 *     later addendum / de-facto industry extension (clearly marked with
 *     @note "Non-standard" / "Extension" where relevant).
 *
 * All values are stored in "engineering units" (already decoded from the
 * on-air ASCII representation) unless the field name explicitly says
 * "raw" or "encoded", to make this header usable directly by application
 * code, not only by a wire-level parser/encoder.
 *
 * @attention
 * APRS on-air units are historically inconsistent (e.g. temperature in
 * Fahrenheit, wind speed in mph, barometric pressure in tenths of a
 * millibar, rainfall in hundredths of an inch). This header preserves the
 * on-air units and documents them explicitly for every field; conversion
 * to SI units is left to the application.
 */

#ifndef WEATHER_TELEMETRY_H
#define WEATHER_TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ======================================================================
 * SECTION 1: GENERIC PROTOCOL LIMITS AND DATA TYPE IDENTIFIERS
 * ====================================================================== */

/**
 * @defgroup aprs_limits Protocol-wide length limits
 * @brief Maximum sizes of the various APRS text fields, as transmitted in
 *        the AX.25 Information field (see APRS101 chapter 5 and 8).
 * @{
 */
#define APRS_MAX_INFO_FIELD_LEN          256 /**< Max AX.25 Information field length (protocol id + data), bytes. */
#define APRS_MAX_COMMENT_LEN             43  /**< Max comment length for a position report without data extension. */
#define APRS_MAX_COMMENT_WITH_EXT_LEN    36  /**< Max comment length when a 7-byte Data Extension is present. */
#define APRS_CALLSIGN_LEN                6   /**< Callsign field length, excluding SSID. */
#define APRS_CALLSIGN_SSID_LEN           9 /**< Callsign left-justified, space-padded to 9 chars, as required in the addressee field of Messages/PARM/UNIT/EQNS/BITS. */
#define APRS_MAX_STATUS_TEXT_LEN         62 /**< Max status text length. */
#define APRS_MAX_OBJECT_NAME_LEN         9  /**< Fixed object/item name length. */
#define APRS_MAX_MESSAGE_TEXT_LEN        67 /**< Max message text length (excluding message number). */
#define APRS_MESSAGE_NUMBER_LEN          5  /**< Message number field length "{mm}" incl. braces, up to 5 chars. */
#define APRS_TELEMETRY_PARAM_NAME_MAXLEN 24 /**< Max length of a single PARM/UNIT/BITS project title text. */
/** @} */

/**
 * @brief APRS Data Type Identifier (DTI): the first byte of the AX.25
 *        Information field, defining the format of everything that
 *        follows (APRS101, Chapter 5, "APRS Data Type Identifiers" table).
 *
 * @note Only the identifier characters relevant to a general-purpose
 *       decoder/encoder are enumerated; identifiers marked [Unused] or
 *       [Do not use] in the specification are intentionally omitted or
 *       marked as reserved.
 */
typedef enum {
    APRS_DTI_MICE_CURRENT_BETA = 0x1C,    /**< Current Mic-E Data (Rev 0 beta). */
    APRS_DTI_MICE_OLD_BETA = 0x1D,        /**< Old Mic-E Data (Rev 0 beta). */
    APRS_DTI_POSITION_NO_TS_NO_MSG = '!', /**< Position without timestamp, no messaging; ALSO used for Ultimeter 2000 raw WX ("!!"). */
    APRS_DTI_RAW_GPS_OR_ULTIMETER = '$',  /**< Raw GPS NMEA sentence, or raw Ultimeter 2000 WX data ("$ULTW..."). */
    APRS_DTI_AGRELO_DFJR = '%',           /**< Agrelo DFJr / MicroFinder. */
    APRS_DTI_MICE_OLD_OR_D700 = '\'',     /**< Old Mic-E Data (current data for the Kenwood TM-D700). */
    APRS_DTI_ITEM = ')',                  /**< Item report. */
    APRS_DTI_PEET_BROS_1 = '*',           /**< Peet Bros U-II Weather Station (complete packet). */
    APRS_DTI_INVALID_TEST_DATA = ',',     /**< Invalid data or test data. */
    APRS_DTI_POSITION_TS_NO_MSG = '/',    /**< Position with timestamp, no messaging. */
    APRS_DTI_MESSAGE = ':',               /**< Message, bulletin, announcement, telemetry meta-message (PARM/UNIT/EQNS/BITS). */
    APRS_DTI_OBJECT = ';',                /**< Object report. */
    APRS_DTI_STATION_CAPABILITIES = '<',  /**< Station Capabilities. */
    APRS_DTI_POSITION_NO_TS_MSG = '=',    /**< Position without timestamp, with messaging. */
    APRS_DTI_STATUS = '>',                /**< Status report. */
    APRS_DTI_QUERY = '?',                 /**< Query. */
    APRS_DTI_POSITION_TS_MSG = '@',       /**< Position with timestamp, with messaging. */
    APRS_DTI_TELEMETRY = 'T',             /**< Telemetry data report. */
    APRS_DTI_MAIDENHEAD_BEACON = '[',     /**< Maidenhead grid locator beacon (obsolete). */
    APRS_DTI_WEATHER_NO_POSITION = '_',   /**< Weather Report, without position. */
    APRS_DTI_MICE_CURRENT = '`',          /**< Current Mic-E Data (not used by the TM-D700). */
    APRS_DTI_USER_DEFINED = '{',          /**< User-Defined APRS packet format. */
    APRS_DTI_THIRD_PARTY = '}'            /**< Third-party traffic (network tunneling). */
} aprs_data_type_identifier_t;

/**
 * @brief APRS symbol table selector, placed immediately before the
 *        latitude in an uncompressed position report, or as the first
 *        byte of a compressed position report.
 */
typedef enum {
    APRS_SYMBOL_TABLE_PRIMARY = '/',   /**< Primary symbol table. */
    APRS_SYMBOL_TABLE_ALTERNATE = '\\' /**< Alternate symbol table (overlay-capable). */
} aprs_symbol_table_id_t;

/**
 * @brief A full APRS display symbol: table selector + symbol code, plus
 *        an optional overlay character (alphanumeric) used with the
 *        Alternate table (APRS101 Chapter 20, APRS 1.1 Addendum).
 */
typedef struct {
    aprs_symbol_table_id_t table; /**< Which of the two 191-entry symbol tables to use. */
    char code;                    /**< Symbol code character, indexes the table (e.g. '_' = Weather Station, '/' = car, etc.). */
    char overlay; /**< Overlay character ('\0' if none); replaces the symbol table id character on-air when the Alternate table with overlay is used. */
} aprs_symbol_t;

/* ======================================================================
 * SECTION 2: TIME FORMATS  (APRS101 Chapter 6)
 * ====================================================================== */

/** @brief Which of the three on-air timestamp encodings is in use. */
typedef enum {
    APRS_TIME_FORMAT_DHM, /**< Day/Hours/Minutes, 7 chars: DDHHMMz or DDHHMM/ (zulu or local). */
    APRS_TIME_FORMAT_HMS, /**< Hours/Minutes/Seconds, 7 chars: HHMMSSh (zulu only). */
    APRS_TIME_FORMAT_MDHM /**< Month/Day/Hours/Minutes, 8 chars: MMDDHHMM (zulu only); used exclusively by positionless weather reports. */
} aprs_time_format_t;

/**
 * @brief Decoded APRS timestamp, valid for all three on-air formats.
 * @note  Zulu (UTC) time is the recommended format for all new
 *        implementations, per APRS101; local time (DHM with trailing
 *        '/' instead of 'z') is legacy but still legal on-air.
 */
typedef struct {
    aprs_time_format_t format; /**< Which format this value was decoded from / will be encoded as. */
    uint8_t month;             /**< 1-12. Only meaningful for MDHM format. */
    uint8_t day;               /**< 1-31. Day of month. */
    uint8_t hour;              /**< 0-23. */
    uint8_t minute;            /**< 0-59. */
    uint8_t second;            /**< 0-59. Only meaningful for HMS format. */
    bool is_zulu;              /**< true = UTC ('z' or 'h' indicator); false = local time ('/' indicator, DHM format only). */
} aprs_timestamp_t;

/* ======================================================================
 * SECTION 3: POSITION / COORDINATE FORMATS (APRS101 Chapter 6, 8, 9)
 * ====================================================================== */

/** @brief Degree of deliberate position imprecision ("Position Ambiguity"). */
typedef enum {
    APRS_AMBIGUITY_NONE = 0,         /**< Full precision: hundredths of a minute. */
    APRS_AMBIGUITY_TENTH_MINUTE = 1, /**< Nearest 1/10 minute. */
    APRS_AMBIGUITY_MINUTE = 2,       /**< Nearest minute. */
    APRS_AMBIGUITY_TEN_MINUTES = 3,  /**< Nearest 10 minutes. */
    APRS_AMBIGUITY_DEGREE = 4        /**< Nearest degree. */
} aprs_position_ambiguity_t;

/**
 * @brief Decoded geographic position (latitude/longitude), plus optional
 *        altitude and the mandatory display symbol.
 *
 * @note Latitude on-air format: ddmm.hhN/S (8 chars).
 * @note Longitude on-air format: dddmm.hhE/W (9 chars).
 * @note Compressed format (13 chars, base-91) is decoded transparently
 *       into the same floating-point representation; see
 *       ::aprs_compressed_position_t for the raw on-air fields.
 */
typedef struct {
    double latitude_deg;                 /**< Decoded latitude, decimal degrees, +north / -south, range [-90, +90]. */
    double longitude_deg;                /**< Decoded longitude, decimal degrees, +east / -west, range [-180, +180]. */
    aprs_position_ambiguity_t ambiguity; /**< Level of ambiguity applied to the on-air lat/long digits. */
    aprs_symbol_t symbol;                /**< Display symbol associated with this position report. */
    bool has_altitude;                   /**< true if altitude_ft is valid. */
    int32_t altitude_ft;                 /**< Altitude in feet. From comment text "/A=aaaaaa" (APRS101 Ch.6) or Mic-E status field. */
    bool is_null_position;               /**< true if this is the "Default Null Position" 0000.00N\00000.00W (unknown position), per APRS101 Ch.6. */
} aprs_position_t;

/**
 * @brief Raw (still base-91 encoded) compressed position fields, as they
 *        appear literally on-air (APRS101 Chapter 9).
 *
 * On-air layout (13 bytes total):
 * `/YYYYXXXX$csT`
 *   - Sym Table ID (1 byte) + Lat (4 bytes, base91) + Long (4 bytes,
 *     base91) + Symbol Code (1 byte) + Course/Speed|Range|Altitude
 *     (2 bytes, base91) + Compression-Type byte T (1 byte).
 */
typedef struct {
    aprs_symbol_table_id_t table;  /**< Leading Symbol Table ID character (indicates compressed format vs. plain digits). */
    char compressed_lat[4];        /**< YYYY: base-91 compressed latitude.  lat = 90 - (val / 380926). */
    char compressed_long[4];       /**< XXXX: base-91 compressed longitude. long = -180 + (val / 190463). */
    char symbol_code;              /**< Symbol Code character. */
    char cs[2];                    /**< Compressed course/speed, OR pre-calculated radio range, OR altitude, depending on value of c and the T byte. */
    uint8_t compression_type_byte; /**< Raw T byte (already -33, i.e. numeric 0-91 value; decode bit fields below). */
} aprs_compressed_position_t;

/**
 * @brief Decoded bit-fields of the Compression Type ("T") byte used in
 *        compressed position reports (APRS101 Chapter 9, "The
 *        Compression Type (T) Byte").
 */
typedef enum {
    APRS_GPS_FIX_OLD = 0,    /**< GPS fix bit = 0: last known / old fix. */
    APRS_GPS_FIX_CURRENT = 1 /**< GPS fix bit = 1: current fix. */
} aprs_gps_fix_t;

typedef enum {
    APRS_NMEA_SOURCE_OTHER = 0,
    /* binary 00 */ /**< Compressed position not sourced from a specific NMEA sentence. */
    APRS_NMEA_SOURCE_GLL = 1,
    /* binary 01 */ /**< Source NMEA sentence: GLL. */
    APRS_NMEA_SOURCE_GGA = 2,
    /* binary 10 */                          /**< Source NMEA sentence: GGA (carries altitude). */
    APRS_NMEA_SOURCE_RMC = 3 /* binary 11 */ /**< Source NMEA sentence: RMC. */
} aprs_nmea_source_t;

typedef enum {
    APRS_COMPRESSION_ORIGIN_COMPRESSED = 0,
    /* binary 000 */ /**< Directly compressed by the originating device. */
    APRS_COMPRESSION_ORIGIN_TNC_BTEXT = 1,
    /* binary 001 */ /**< TNC Beacon Text. */
    APRS_COMPRESSION_ORIGIN_SOFTWARE = 2,
    /* binary 010 */ /**< Software (DOS/Mac/Win/+SA). */
    APRS_COMPRESSION_ORIGIN_RESERVED_011 = 3,
    /* binary 011 */ /**< Reserved / to-be-defined. */
    APRS_COMPRESSION_ORIGIN_KPC3 = 4,
    /* binary 100 */ /**< Kantronics KPC-3. */
    APRS_COMPRESSION_ORIGIN_PICO = 5,
    /* binary 101 */ /**< Pico tracker. */
    APRS_COMPRESSION_ORIGIN_OTHER_TRACKER = 6,
    /* binary 110 */                                        /**< Other tracker (to-be-defined). */
    APRS_COMPRESSION_ORIGIN_DIGIPEATER = 7 /* binary 111 */ /**< Digipeater conversion from raw NMEA. */
} aprs_compression_origin_t;

/* ======================================================================
 * SECTION 4: APRS DATA EXTENSIONS (APRS101 Chapter 7)
 *   Fixed 7-byte fields that may follow a position report.
 * ====================================================================== */

/**
 * @brief Course and Speed data extension: "CSE/SPD" (7 bytes).
 * @note  Also used, with identical layout, for Wind Direction/Speed in
 *        weather reports ("DIR/SPD"); see ::aprs_wind_t for the weather
 *        semantics.
 */
typedef struct {
    uint16_t course_deg; /**< Course over ground, degrees clockwise from true north, range 1-360; 0 or unset = unknown/N/A ("000/000", ".../...", "VVV/VVV"). */
    uint16_t speed_knots; /**< Speed, knots. */
    bool is_unknown;      /**< true when course/speed are not relevant/available (on-air "000/000", ".../..." or "VVV/VVV"). */
} aprs_course_speed_t;

/**
 * @brief Station Power / Effective Antenna Height / Gain / Directivity
 *        data extension: "PHGphgd" (7 bytes). Used by APRS to draw a
 *        computed radio-range circle around a station.
 */
typedef struct {
    uint8_t power_code;       /**< p: 0-9 code. Actual power (W) = code^2 (0,1,4,9,16,25,36,49,64,81 W). */
    uint8_t height_code;      /**< h: 0-9 (or higher ASCII for balloons/aircraft/satellites). Height AAT (ft) = 10 * 2^code. */
    uint8_t gain_code;        /**< g: 0-9 code, antenna gain in dB (numerically equal to the code). */
    uint8_t directivity_code; /**< d: 0=omni, 1=NE(45), 2=E(90), 3=SE(135), 4=S(180), 5=SW(225), 6=W(270), 7=NW(315), 8=N(360). */
} aprs_phg_t;

/**
 * @brief Pre-Calculated Radio Range data extension: "RNGrrrr" (7 bytes).
 */
typedef struct {
    uint16_t range_miles; /**< rrrr: omnidirectional radio range, statute miles, with leading zeros on-air (0-9999). */
} aprs_range_t;

/**
 * @brief Omni-DF Signal Strength data extension: "DFSshgd" (7 bytes).
 *        Same layout as PHG but with relative signal strength instead of
 *        transmitter power; used to localize jammers/DF targets.
 */
typedef struct {
    uint8_t strength_code;    /**< s: 0-9, relative signal strength in S-points (0 = station does NOT hear the signal - draws exclusion circles). */
    uint8_t height_code;      /**< h: as in ::aprs_phg_t. */
    uint8_t gain_code;        /**< g: as in ::aprs_phg_t. */
    uint8_t directivity_code; /**< d: as in ::aprs_phg_t. */
} aprs_dfs_t;

/**
 * @brief Bearing and Number/Range/Quality data extension: "/BRG/NRQ"
 *        (8 bytes), following CSE/SPD in DF reports.
 */
typedef struct {
    uint16_t bearing_deg; /**< BRG: bearing to the signal, degrees (1-360). */
    uint8_t number;       /**< N: 0 = NRQ meaningless; 1-8 = relative number of hits per sampling period; 9 = manual report. */
    uint8_t range_code;   /**< R: 0-9, range = 2^R miles. */
    uint8_t quality;      /**< Q: 0 (useless) to 9 (<1 degree, best) bearing accuracy code. */
} aprs_bearing_nrq_t;

/**
 * @brief Area Object Descriptor data extension: "Tyy/Cxx" (7 bytes),
 *        used with Object reports to describe circles, lines, ellipses,
 *        triangles, boxes, etc. (APRS101 Chapter 11).
 */
typedef enum {
    APRS_AREA_TYPE_CIRCLE = 1,
    APRS_AREA_TYPE_LINE = 2,
    APRS_AREA_TYPE_TRIANGLE = 6,
    APRS_AREA_TYPE_BOX_FILLED_RECT_TRIANGLE = 3, /**< Filled variants offset by +3/+6 per spec table; application decodes per full table. */
    APRS_AREA_TYPE_RECTANGLE = 5
} aprs_area_object_type_t;

typedef struct {
    aprs_area_object_type_t type; /**< T: shape type code. */
    uint8_t color_code;           /**< Cxx: fill color code (0-15, per APRS101 area color table). */
    uint16_t line_width_miles;    /**< Optional {www} line/box width in miles, embedded in comment text for WATCH/WARNING boxes (WX.TXT). */
} aprs_area_object_t;

/* ======================================================================
 * SECTION 5: WEATHER REPORTS  (APRS101 Chapter 12, WX.TXT)
 * ====================================================================== */

/**
 * @brief Wind Direction and Speed data extension used specifically in
 *        Weather Reports: "DIR/SPDgXXX" (course/speed 7 bytes + gust).
 */
typedef struct {
    uint16_t direction_deg; /**< Wind direction, degrees clockwise from true north (1-360); 0/unset = unknown. */
    uint16_t sustained_mph; /**< Sustained (1-minute average) wind speed, miles per hour. On-air token "SPD". */
    bool has_gust;          /**< true if gust_mph is present (on-air token "g"). */
    uint16_t gust_mph;      /**< Peak wind gust in the last 5 minutes, miles per hour. */
    bool direction_unknown; /**< true when direction/speed are "..." / "VVV" (not available). */
} aprs_wind_t;

/**
 * @brief Identifies which underlying home weather-station hardware/format
 *        produced a raw or reformatted weather report, per WX.TXT
 *        "FORMATS" section and APRS101 "APRS Software Type".
 */
typedef enum {
    APRS_WX_UNIT_UNKNOWN = 0,
    APRS_WX_UNIT_PEET_ULTIMETER_2000,        /**< "U2k": Peet Bros Ultimeter 2000, PC-attached. */
    APRS_WX_UNIT_PEET_ULTIMETER_2000_REMOTE, /**< "U2r" / "UII": remote Ultimeter, no PC, raw serial passthrough. */
    APRS_WX_UNIT_PEET_ULTIMETER_500,         /**< "U5": Peet Bros Ultimeter 500. */
    APRS_WX_UNIT_PEET_ULTIMETER_PKT,         /**< "Upkm": remote Ultimeter in "packet mode" ($ULTW sentence). */
    APRS_WX_UNIT_DAVIS,                      /**< "Dvs": Davis Instruments weather station (Weather Link serial interface). */
    APRS_WX_UNIT_PEET_BROS_UII_RAW,          /**< Peet Bros U-II raw DTI ('#' or '*'). */
    APRS_WX_UNIT_OTHER                       /**< Any other / vendor-specific instrument string. */
} aprs_wx_unit_type_t;

/**
 * @brief Identifies the originating APRS software platform, encoded as
 *        the single lower-case letter preceding the instrument code in
 *        the "d" field (e.g. "dU2k"): d=DOS, m=Mac, w=Win, etc.
 *        (WX.TXT "%type" / "d" field; APRS101 "APRS Software Type").
 */
typedef enum { APRS_SW_TYPE_DOS = 'd', APRS_SW_TYPE_MAC = 'm', APRS_SW_TYPE_WINDOWS = 'w', APRS_SW_TYPE_OTHER = '?' } aprs_software_type_t;

/**
 * @brief The complete, decoded set of every weather sensor field ever
 *        defined by the APRS Weather Report formats, whether "Complete"
 *        (with position and timestamp), "Positionless" (standalone,
 *        MDHM timestamp only) or "Raw" (vendor-native passthrough).
 *
 * @details Field-by-field on-air mapping:
 *   - wind:                "DIR/SPDgXXX" (see ::aprs_wind_t)
 *   - temperature_f:       "tXXX" (degrees Fahrenheit; may be negative,
 *                           e.g. "t-01"; 3 significant digits + optional
 *                           leading minus sign)
 *   - rain_last_hour:      "rXXX" (hundredths of an inch, over the
 *                           preceding 60 minutes)
 *   - rain_last_24h:       "pXXX" (hundredths of an inch, sliding 24 h
 *                           window)
 *   - rain_since_midnight: "PXXX" (hundredths of an inch, since local
 *                           midnight; upper-case P used specifically by
 *                           remote/unattended Ultimeter stations)
 *   - snow_last_24h_in:    "sXXX" (inches, to the nearest tenth, in the
 *                           last 24 hours) - APRS 1.2 proposal
 *   - humidity_percent:    "hXX" (relative humidity, percent; "00" means
 *                           100%)
 *   - barometric_pressure_dmb: "bXXXXX" (barometric pressure, tenths of
 *                           a millibar / tenths of a hPa, 5 digits)
 *   - luminosity_wm2:      "LXXX" (0-999 W/m^2) or "lXXX" (>=1000 W/m^2,
 *                           value stored is offset by 1000) - APRS 1.2
 *                           proposal, replaces one of the rain fields on
 *                           air when present
 *   - raw_rain_counter:    "#XXXX" (raw, un-reset tip-bucket counter used
 *                           by remote/unattended stations with no local
 *                           reference time)
 *   - flood_height_ft:     "FXXXX.X" (flood/water-gauge height, feet, to
 *                           the nearest tenth) - APRS 1.2 proposal
 *   - flood_height_m:      "fXXXX.X" (flood/water-gauge height, meters,
 *                           to the nearest tenth) - APRS 1.2 proposal
 *   - software_indicator:  "%" + software-type letter + instrument code
 *                           string (e.g. "dU2k") identifying both the
 *                           originating APRS software platform and the
 *                           weather instrument model
 *
 * @note Fields marked "APRS 1.2 proposal" are not part of the original
 *       APRS101 core specification but are widely implemented by modern
 *       software (APRSdos, Xastir, aprx, aprs.fi, CWOP gateways, etc.).
 * @note Additional sensor channels that are NOT standardized anywhere in
 *       APRS (UV index, soil moisture/temperature, indoor
 *       temperature/humidity, solar radiation beyond luminosity, water
 *       depth other than flood gauges, air quality, lightning strikes,
 *       etc.) are commonly carried either as free-text in the comment
 *       field, or - more robustly - via the generic APRS Telemetry
 *       channels (see Section 6). This header exposes an explicit
 *       "extended sensor" array (::aprs_weather_extended_sensor_t) for
 *       exactly that purpose, so that no measurable physical quantity is
 *       left unrepresentable.
 */

/**
 * @brief Enumerates, as a single canonical and iterable list, all 13
 *        sensor fields defined by the APRS Weather Report formats
 *        (APRS101 Chapter 12, WX.TXT and the APRS 1.2 proposals
 *        combined). Used to index ::aprs_weather_report_t::enabled, so
 *        that "is sensor N present in this report" can be tested or
 *        iterated generically instead of referencing 13 separately
 *        named boolean fields.
 *
 * @note  A single physical weather station will typically populate only
 *        a subset of these 13 slots, since not every instrument
 *        (Ultimeter, Davis, Peet Bros, etc.) measures every quantity,
 *        and APRS101 defines every weather field as individually
 *        optional on-air.
 */
typedef enum {
    APRS_WX_SENSOR_WIND = 0,            /**< 0: Wind direction / sustained speed / gust ("DIR/SPDgXXX"). */
    APRS_WX_SENSOR_TEMPERATURE,         /**< 1: Ambient temperature ("tXXX"). */
    APRS_WX_SENSOR_RAIN_LAST_HOUR,      /**< 2: Rainfall, last 60 minutes ("rXXX"). */
    APRS_WX_SENSOR_RAIN_LAST_24H,       /**< 3: Rainfall, last 24 hours, sliding window ("pXXX"). */
    APRS_WX_SENSOR_RAIN_SINCE_MIDNIGHT, /**< 4: Rainfall since local midnight ("PXXX"). */
    APRS_WX_SENSOR_SNOW_LAST_24H,       /**< 5: Snowfall, last 24 hours ("sXXX") - APRS 1.2 proposal. */
    APRS_WX_SENSOR_HUMIDITY,            /**< 6: Relative humidity ("hXX"). */
    APRS_WX_SENSOR_BAROMETRIC_PRESSURE, /**< 7: Barometric pressure ("bXXXXX"). */
    APRS_WX_SENSOR_LUMINOSITY,          /**< 8: Solar luminosity ("LXXX"/"lXXX") - APRS 1.2 proposal. */
    APRS_WX_SENSOR_RAW_RAIN_COUNTER,    /**< 9: Raw, un-reset tip-bucket counter ("#XXXX"), remote/unattended stations. */
    APRS_WX_SENSOR_FLOOD_HEIGHT_FT,     /**< 10: Flood/water-gauge height, feet ("FXXXX.X") - APRS 1.2 proposal. */
    APRS_WX_SENSOR_FLOOD_HEIGHT_M,      /**< 11: Flood/water-gauge height, meters ("fXXXX.X") - APRS 1.2 proposal. */
    APRS_WX_SENSOR_SOFTWARE_INDICATOR,  /**< 12: Originating software + instrument-type indicator ("%..."). */
    APRS_WX_SENSOR_COUNT                /**< Sentinel: total number of defined weather sensor slots (13). Not a real sensor. */
} aprs_weather_sensor_id_t;

typedef struct {
    /**
     * @brief Per-sensor "enabled" flag, indexed by ::aprs_weather_sensor_id_t.
     *
     * @details `enabled[APRS_WX_SENSOR_TEMPERATURE]` is `true` if and only
     *          if this report actually carries a temperature reading
     *          (equivalently, if the on-air "tXXX" token was present /
     *          is to be transmitted). Every one of the
     *          ::APRS_WX_SENSOR_COUNT (13) slots is independently
     *          optional, mirroring the fact that every weather field in
     *          APRS101 is optional on-air. Iterate with:
     * @code
     * for (int s = 0; s < APRS_WX_SENSOR_COUNT; s++) {
     *     if (report.enabled[s]) { / * sensor s is present * / }
     * }
     * @endcode
     */
    bool enabled[APRS_WX_SENSOR_COUNT];

    /* --- Decoded values --- */
    aprs_wind_t wind;                           /**< Wind direction / sustained speed / gust. */
    int16_t temperature_f;                      /**< Ambient temperature, degrees Fahrenheit, may be negative. */
    uint16_t rain_last_hour_hundredths_in;      /**< Rainfall in the last 60 minutes, hundredths of an inch. */
    uint16_t rain_last_24h_hundredths_in;       /**< Rainfall in the last 24 hours (sliding window), hundredths of an inch. */
    uint16_t rain_since_midnight_hundredths_in; /**< Rainfall since local midnight, hundredths of an inch. */
    uint16_t snow_last_24h_tenths_in;           /**< Snowfall in the last 24 hours, tenths of an inch. */
    uint8_t humidity_percent;                   /**< Relative humidity, 1-100 percent (on-air "00" decodes to 100). */
    uint32_t barometric_pressure_tenths_mb;     /**< Barometric pressure, tenths of a millibar/hPa (e.g. 10132 = 1013.2 mb). */
    uint16_t luminosity_wm2;                    /**< Solar luminosity, watts per square meter (0-1999+). */
    uint32_t raw_rain_counter;                  /**< Raw, unscaled tip-bucket counter value from a remote/unattended station. */
    float flood_height_ft;                      /**< Flood/water-gauge height, feet (tenth resolution). */
    float flood_height_m;                       /**< Flood/water-gauge height, meters (tenth resolution). */

    aprs_wx_unit_type_t instrument_type;       /**< Decoded weather instrument model. */
    aprs_software_type_t originating_software; /**< Decoded originating software platform letter. */

    aprs_timestamp_t timestamp; /**< Report timestamp; MDHM format for positionless reports, DHM otherwise. */
    bool has_position;          /**< true for "Complete Weather Reports with Timestamp and Position"; false for standalone "Positionless Weather Reports". */
    aprs_position_t position;   /**< Valid only if has_position == true. */

    char raw_comment[APRS_MAX_COMMENT_LEN + 1]; /**< Any free-text remainder of the comment field, verbatim. */
} aprs_weather_report_t;

/**
 * @brief Generic key/value slot for weather-adjacent sensor quantities
 *        that have no reserved on-air token in APRS101/WX.TXT, but which
 *        real-world CWOP (Citizen Weather Observer Program) gateways and
 *        modern IoT weather stations commonly report - either appended
 *        as free text in the comment field, or (recommended) via the
 *        Telemetry channels of Section 6.
 *
 * @note  This structure exists purely as an application-level convenience
 *        container; it has no fixed on-air representation of its own.
 */
typedef enum {
    APRS_EXT_SENSOR_UV_INDEX,            /**< Ultraviolet index (dimensionless, typically 0-11+). Non-standard. */
    APRS_EXT_SENSOR_SOLAR_RADIATION_WM2, /**< Solar radiation, W/m^2, when distinct from the standard "L"/"l" luminosity token. Non-standard. */
    APRS_EXT_SENSOR_INDOOR_TEMP_F,       /**< Indoor temperature, degrees F. Non-standard. */
    APRS_EXT_SENSOR_INDOOR_HUMIDITY_PCT, /**< Indoor relative humidity, percent. Non-standard. */
    APRS_EXT_SENSOR_SOIL_TEMP_F,         /**< Soil temperature, degrees F. Non-standard. */
    APRS_EXT_SENSOR_SOIL_MOISTURE_PCT,   /**< Soil moisture, percent. Non-standard. */
    APRS_EXT_SENSOR_WATER_TEMP_F,        /**< Water temperature (lake/river/sea buoy), degrees F. Non-standard. */
    APRS_EXT_SENSOR_WIND_CHILL_F,        /**< Computed wind chill, degrees F. Non-standard (usually derived, not transmitted). */
    APRS_EXT_SENSOR_HEAT_INDEX_F,        /**< Computed heat index, degrees F. Non-standard (usually derived, not transmitted). */
    APRS_EXT_SENSOR_DEW_POINT_F,         /**< Computed dew point, degrees F. Non-standard (usually derived, not transmitted). */
    APRS_EXT_SENSOR_BATTERY_VOLTAGE,     /**< Station power-supply / battery voltage, volts. Typically sent as a Telemetry analog channel. */
    APRS_EXT_SENSOR_RADIATION_CPM,       /**< Ionizing radiation, counts per minute (Geiger payloads). Non-standard, typically sent via Telemetry. */
    APRS_EXT_SENSOR_CO2_PPM,             /**< Carbon dioxide concentration, ppm. Non-standard, typically sent via Telemetry. */
    APRS_EXT_SENSOR_CUSTOM               /**< Vendor/application-defined; see custom_label. */
} aprs_extended_sensor_kind_t;

typedef struct {
    aprs_extended_sensor_kind_t kind; /**< Which physical quantity this slot represents. */
    char custom_label[APRS_TELEMETRY_PARAM_NAME_MAXLEN +
                      1]; /**< Human-readable label, used verbatim when kind == APRS_EXT_SENSOR_CUSTOM or to override the default label. */
    double value;         /**< Decoded engineering-unit value. */
    char unit[16];        /**< Free-text unit string (e.g. "V", "ppm", "CPM", "degF"). */
} aprs_weather_extended_sensor_t;

/* ---- Raw / vendor-native weather station passthrough formats ---- */

/**
 * @brief Peet Bros Ultimeter II raw serial passthrough packet, as
 *        directly retransmitted on-air by remote (TNC + radio only, no
 *        PC) stations using DTI '#' or '*' (APRS101 DTI table; WX.TXT
 *        "REMOTE ULTIMETER OPERATION" / "DATA LOGGER MODE").
 *        Fixed 14-byte record beginning with '*' or '#'.
 */
typedef struct {
    uint16_t wind_speed_raw;     /**< Raw wind speed counter (peak gust since last transmission), instrument units. */
    uint16_t wind_direction_raw; /**< Raw wind direction, instrument units (0-255 representing 0-360 degrees). */
    int16_t outdoor_temp_raw;    /**< Raw outdoor temperature, tenths of a degree F, instrument units. */
    uint16_t rain_counter_raw;   /**< Raw rain tip-bucket counter, un-reset. */
    uint16_t barometer_raw;      /**< Raw barometric pressure, instrument units. */
    bool units_are_mph;          /**< true if the unit outputs in MPH (marked with '*'); false if KPH (marked with '#'). */
} aprs_peet_ultimeter_ii_raw_t;

/**
 * @brief Peet Bros Ultimeter 2000/500/2000+ "$ULTW" raw hexadecimal
 *        packet-mode sentence (WX.TXT "PACKET MODE"), 44/48/52 bytes of
 *        ASCII-hex fields, transmitted once every 5 minutes.
 *        Every field below is one raw hex sub-field of the $ULTW record,
 *        stored already converted from ASCII-hex to its native integer
 *        instrument units (application must still apply the appropriate
 *        engineering scale factor, which is instrument-firmware
 *        specific and is not standardized by APRS).
 */
typedef struct {
    uint16_t wind_gust_speed_raw;       /**< Peak wind speed since last transmission. */
    uint16_t wind_gust_direction_raw;   /**< Direction of peak wind gust. */
    int16_t outdoor_temp_raw;           /**< Outdoor temperature, tenths of degree F. */
    uint16_t rain_since_midnight_raw;   /**< Total rain since local midnight, instrument units (0.01 in resolution typical). */
    uint16_t barometer_raw;             /**< Barometric pressure, instrument units (0.1 mbar resolution typical). */
    int16_t indoor_temp_raw;            /**< Indoor temperature, tenths of degree F. */
    uint8_t outdoor_humidity_raw;       /**< Outdoor relative humidity, tenths of a percent. */
    uint8_t indoor_humidity_raw;        /**< Indoor relative humidity, tenths of a percent. */
    uint16_t rain_today_raw;            /**< Rain today, alternate accumulator, instrument units. */
    uint16_t one_minute_wind_speed_raw; /**< Average wind speed, once-per-minute sample, instrument units. */
} aprs_ultimeter_ultw_raw_t;

/* ======================================================================
 * SECTION 6: TELEMETRY DATA  (APRS101 Chapter 13, plus the community
 *            "Kenneth's Proposed Telemetry Format" extension used by
 *            aprs.fi and most modern trackers/igates)
 * ====================================================================== */

/**
 * @brief Number of analog telemetry channels defined by APRS101 (5, A1-A5).
 *
 * @note This is the standard/default channel count, used to size a
 *       report/metadata instance's dynamically-allocated analog arrays
 *       (see ::aprs_telemetry_report_t::analog_count and
 *       ::aprs_telemetry_metadata_t::analog_count) for ordinary
 *       stations. It is not a hard ceiling: a given instance's
 *       analog_count may be allocated larger for non-standard extended
 *       links that carry more than 5 analog channels.
 */
#define APRS_TELEMETRY_ANALOG_CHANNELS 5
/**
 * @brief Number of digital (binary) telemetry channels defined by
 *        APRS101 (8, B1-B8).
 *
 * @note Standard/default channel count, analogous to
 *       ::APRS_TELEMETRY_ANALOG_CHANNELS; see that macro's note for the
 *       dynamic-sizing rationale.
 */
#define APRS_TELEMETRY_DIGITAL_CHANNELS 8

/**
 * @brief Canonical, iterable identifiers for the 5 analog telemetry
 *        channels (A1-A5) defined by APRS101 Chapter 13. Used to index
 *        the `analog`/`analog_enabled` arrays of ::aprs_telemetry_report_t
 *        and the per-channel arrays of ::aprs_telemetry_metadata_t,
 *        exactly as ::aprs_weather_sensor_id_t indexes the weather
 *        report's `enabled` array.
 */
typedef enum {
    APRS_TLM_ANALOG_A1 = 0, /**< Analog channel 1. */
    APRS_TLM_ANALOG_A2,     /**< Analog channel 2. */
    APRS_TLM_ANALOG_A3,     /**< Analog channel 3. */
    APRS_TLM_ANALOG_A4,     /**< Analog channel 4. */
    APRS_TLM_ANALOG_A5,     /**< Analog channel 5. */
    APRS_TLM_ANALOG_COUNT   /**< Sentinel: total number of analog channels (5). Not a real channel. Must equal ::APRS_TELEMETRY_ANALOG_CHANNELS. */
} aprs_telemetry_analog_channel_id_t;

/**
 * @brief Canonical, iterable identifiers for the 8 digital (binary)
 *        telemetry channels (B1-B8) defined by APRS101 Chapter 13.
 */
typedef enum {
    APRS_TLM_DIGITAL_B1 = 0, /**< Digital channel 1. */
    APRS_TLM_DIGITAL_B2,     /**< Digital channel 2. */
    APRS_TLM_DIGITAL_B3,     /**< Digital channel 3. */
    APRS_TLM_DIGITAL_B4,     /**< Digital channel 4. */
    APRS_TLM_DIGITAL_B5,     /**< Digital channel 5. */
    APRS_TLM_DIGITAL_B6,     /**< Digital channel 6. */
    APRS_TLM_DIGITAL_B7,     /**< Digital channel 7. */
    APRS_TLM_DIGITAL_B8,     /**< Digital channel 8. */
    APRS_TLM_DIGITAL_COUNT   /**< Sentinel: total number of digital channels (8). Not a real channel. Must equal ::APRS_TELEMETRY_DIGITAL_CHANNELS. */
} aprs_telemetry_digital_channel_id_t;

/**
 * @brief A single Telemetry Report ("T#..." on-air packet), APRS101
 *        Chapter 13 "Telemetry Report Format".
 *
 * On-air strict format: `T#sss,aaa,aaa,aaa,aaa,aaa,bbbbbbbb`
 *   - sss:  3-character sequence number (typically 000-999, or the
 *           literal letters "MIC" for Mic-E-derived telemetry).
 *   - aaa*5: five analog channel values. APRS101 defines these as 8-bit
 *            unsigned integers, 000-255 decimal, three digits with
 *            leading zeros.
 *   - bbbbbbbb: eight single-character '0'/'1' digital channel values.
 *
 * @note The community "Kenneth's Proposed Telemetry Format" (adopted by
 *       aprs.fi and widely deployed since ~2020) relaxes the strict
 *       8-bit/leading-zero analog constraint: analog values may be
 *       negative, larger than 255, or contain a decimal point, and are
 *       parsed/generated as ordinary base-10 numbers of variable length.
 *       Trailing analog and digital fields may also be omitted entirely.
 *       This structure stores values already decoded to double precision
 *       so that both the strict and the extended encodings are
 *       represented uniformly; the @ref is_legacy_8bit_strict flag
 *       records which wire format was actually used/should be used.
 * @note Only TRAILING channels may be legally omitted on-air (both in
 *       the strict and the extended format): e.g. it is valid to send
 *       just A1-A3 and no digital byte at all, but it is not valid to
 *       send A1, skip A2, and then send A3. The `*_enabled` arrays below
 *       are therefore expected to have all `true` entries contiguous
 *       from index 0; the highest enabled index plus one determines how
 *       many channels are actually placed on-air when encoding.
 */
typedef struct {
    char sequence[4]; /**< 3-char sequence number, e.g. "001", or "MIC" for Mic-E telemetry; NUL-terminated. */

    /**
     * @brief Number of analog channel slots actually allocated in
     *        @ref analog_enabled / @ref analog for this report.
     *
     * @details Defaults to ::APRS_TELEMETRY_ANALOG_CHANNELS (5, the
     *          APRS101 standard count) but is not a hard ceiling: the
     *          community/extended telemetry format allows more than 5
     *          analog channels on non-standard links, so this field lets
     *          @ref analog_enabled and @ref analog be allocated (e.g. via
     *          malloc/realloc) to whatever size a given station actually
     *          needs. Every index below this count is a real, addressable
     *          slot - unlike the fixed-array design this replaces, no
     *          slot can silently overlap another payload's memory.
     */
    size_t analog_count;

    /**
     * @brief Per-channel "enabled" flag, dynamically sized to
     *        @ref analog_count and indexed the same way
     *        ::aprs_telemetry_analog_channel_id_t indexes the standard 5
     *        channels. `analog_enabled[APRS_TLM_ANALOG_A3]` is `true` iff
     *        this report actually carries a value for A3.
     */
    bool *analog_enabled;

    /**
     * @brief Raw transmitted analog values, dynamically sized to
     *        @ref analog_count (index 0 = A1 ... index 4 = A5 for a
     *        standard station, more for extended-channel stations),
     *        BEFORE applying EQNS scaling. Strict format: integers
     *        0-255. Extended format: any signed decimal value.
     *        Meaningful only where the corresponding analog_enabled[]
     *        entry is true.
     */
    double *analog;

    /**
     * @brief Number of digital channel slots actually allocated in
     *        @ref digital_enabled / @ref digital for this report.
     *        Defaults to ::APRS_TELEMETRY_DIGITAL_CHANNELS (8, the
     *        APRS101 standard count); see @ref analog_count for the
     *        rationale of making this dynamic rather than fixed.
     */
    size_t digital_count;

    /**
     * @brief Per-channel "enabled" flag, dynamically sized to
     *        @ref digital_count and indexed the same way
     *        ::aprs_telemetry_digital_channel_id_t indexes the standard 8
     *        channels. `digital_enabled[APRS_TLM_DIGITAL_B1]` is `true`
     *        iff this report actually carries a value for B1.
     */
    bool *digital_enabled;

    /**
     * @brief Raw transmitted binary values, dynamically sized to
     *        @ref digital_count (index 0 = B1 ... index 7 = B8 for a
     *        standard station). Meaningful only where the corresponding
     *        digital_enabled[] entry is true.
     */
    bool *digital;

    bool is_legacy_8bit_strict; /**< true = encode/expect classic 3-digit 000-255 analog values and exactly 8 digital bits (APRS101 strict); false = use the
                                   extended/community variable-length numeric format. */
    bool via_base91_compressed_position; /**< true if this telemetry was carried inline with a Base-91 compressed position report rather than as a standalone
                                            'T' packet (common on trackers/balloons). */
} aprs_telemetry_report_t;

/**
 * @brief Quadratic scaling equation coefficients for one analog
 *        telemetry channel, as defined by an "EQNS." metadata message
 *        (APRS101 Chapter 13, "Equation Coefficients Message").
 *
 * Engineering value X is recovered from the raw transmitted value v as:
 * @f[ X = a \cdot v^2 + b \cdot v + c @f]
 *
 * @note Default coefficients, used whenever no EQNS message has been
 *       received for a channel, are {a=0, b=1, c=0} (i.e. identity: the
 *       raw transmitted value is used unscaled).
 */
typedef struct {
    double a; /**< Quadratic coefficient. */
    double b; /**< Linear coefficient. */
    double c; /**< Constant offset. */
} aprs_telemetry_eqns_t;

/**
 * @brief Full telemetry channel metadata for one station: human-readable
 *        parameter names, engineering units/bit labels, scaling
 *        equations and digital bit-sense, exactly mirroring the four
 *        APRS101 telemetry metadata Message sub-types:
 *          - "PARM." Parameter Name Message
 *          - "UNIT." Unit/Label Message
 *          - "EQNS." Equation Coefficients Message
 *          - "BITS." Bit Sense / Project Name Message
 *
 * @note Per APRS101 and common implementation practice, the addressee
 *       callsign field of these Message packets must be left-justified
 *       and space-padded to exactly 9 characters
 *       (see ::APRS_CALLSIGN_SSID_LEN).
 * @note Field-length guidance from the original telemetry documentation
 *       (screen-width driven, not enforced on-air, but useful for UI
 *       layout): analog channel name/unit lengths are historically
 *       limited to 7,6,5,5,4 characters and digital bit label lengths to
 *       5,4,3,3,3,2,2,2 characters respectively; modern software is not
 *       required to honor this, and the on-air PARM message as a whole
 *       may be up to 197 characters.
 */
typedef struct {
    char station_callsign[APRS_CALLSIGN_SSID_LEN + 1]; /**< Callsign this metadata applies to, space-padded to 9 chars on-air. */

    /**
     * @brief Number of analog channel slots allocated in the
     *        analog_defined/analog_name/analog_unit/eqns arrays below.
     *        Defaults to ::APRS_TELEMETRY_ANALOG_CHANNELS (5) but, like
     *        ::aprs_telemetry_report_t::analog_count, is not a hard
     *        ceiling - it may be allocated larger for stations that
     *        publish metadata for more than the 5 standard channels.
     */
    size_t analog_count;

    /**
     * @brief Number of digital channel slots allocated in the
     *        digital_defined/digital_name/digital_unit_label/
     *        bit_true_when_transmitted_one arrays below. Defaults to
     *        ::APRS_TELEMETRY_DIGITAL_CHANNELS (8); see @ref analog_count
     *        for the rationale of making this dynamic.
     */
    size_t digital_count;

    /**
     * @brief Per-channel "defined" flag, dynamically sized to
     *        @ref analog_count and indexed the same way
     *        ::aprs_telemetry_analog_channel_id_t indexes the standard 5
     *        channels. `true` means this station has actually published
     *        PARM/UNIT/EQNS metadata for that channel; `false` means the
     *        channel is unused/reserved (on-air, an empty field between
     *        commas in the PARM./UNIT./EQNS. messages) and receivers
     *        should not display it.
     */
    bool *analog_defined;

    /**
     * @brief Per-channel "defined" flag, dynamically sized to
     *        @ref digital_count and indexed the same way
     *        ::aprs_telemetry_digital_channel_id_t indexes the standard 8
     *        channels, with the same "published metadata vs. unused"
     *        semantics as @ref analog_defined.
     */
    bool *digital_defined;

    /* --- PARM: parameter (channel) names --- */
    char (*analog_name)[APRS_TELEMETRY_PARAM_NAME_MAXLEN +
                        1]; /**< Names for A1..A5 (or more), dynamically sized to @ref analog_count. Meaningful only where analog_defined[] is true. */
    char (*digital_name)[APRS_TELEMETRY_PARAM_NAME_MAXLEN +
                         1]; /**< Names for B1..B8 (or more), dynamically sized to @ref digital_count. Meaningful only where digital_defined[] is true. */

    /* --- UNIT: engineering units for analog channels, state labels for digital --- */
    char (*analog_unit)[APRS_TELEMETRY_PARAM_NAME_MAXLEN + 1]; /**< Units for A1..A5 (e.g. "Volts", "Deg.", "NUM"), dynamically sized to @ref analog_count.
                                                                  Meaningful only where analog_defined[] is true. */
    char (*digital_unit_label)[APRS_TELEMETRY_PARAM_NAME_MAXLEN + 1]; /**< On-state labels for B1..B8 (e.g. "ON", "OPEN"), dynamically sized to
                                                                         @ref digital_count. Meaningful only where digital_defined[] is true. */

    /* --- EQNS: quadratic scaling coefficients, one triplet per analog channel --- */
    aprs_telemetry_eqns_t *eqns; /**< Scaling equations for A1..A5 (or more), dynamically sized to @ref analog_count. Defaults to {a=0,b=1,c=0} (identity) even
                                   where analog_defined[] is false. */

    /* --- BITS: digital bit "true" polarity + project/title text --- */
    bool *bit_true_when_transmitted_one; /**< true if a transmitted '1' means the labeled condition is TRUE for that bit; false if the polarity is inverted.
                                            Dynamically sized to @ref digital_count. Meaningful only where digital_defined[] is true. */
    char project_title[APRS_TELEMETRY_PARAM_NAME_MAXLEN + 1]; /**< Free-text project/title name, up to 24 characters, used to title the telemetry display. */
} aprs_telemetry_metadata_t;

/* ======================================================================
 * SECTION 7: STORM DATA  (APRS101 Chapter 12 "Storm Data"; WX.TXT
 *            "TROPICAL STORM WINDS")
 * ====================================================================== */

/** @brief Classification of a tropical cyclone, per WX.TXT storm data format. */
typedef enum {
    APRS_STORM_TYPE_TROPICAL_DEPRESSION, /**< "TD": sustained winds < 34 kt. Plotted blue. */
    APRS_STORM_TYPE_TROPICAL_STORM,      /**< "TS": sustained winds 34-63 kt. Plotted yellow. */
    APRS_STORM_TYPE_HURRICANE            /**< "HC": sustained winds >= 64 kt. Plotted red. */
} aprs_storm_type_t;

/**
 * @brief Decoded Storm Data, appended in the comment field of a Position
 *        or Object report to plot a tropical cyclone's predicted track
 *        and wind-radius circles.
 *
 * On-air layout: `DDHHMM/LAT/LONG@CSE/SPD/TS/www^GGG/ppp>RRR&rrr`
 */
typedef struct {
    aprs_timestamp_t forecast_time;         /**< DDHHMM: time of this forecast/observation point. */
    aprs_position_t predicted_position;     /**< LAT/LONG @ symbol: predicted position at forecast_time. */
    uint16_t movement_course_deg;           /**< CSE: direction of movement, degrees. */
    uint16_t movement_speed_kt;             /**< SPD: speed of movement, knots. */
    aprs_storm_type_t storm_type;           /**< TS/HC/TD classification. */
    uint16_t sustained_wind_kt;             /**< www: sustained wind speed, knots. */
    uint16_t peak_gust_kt;                  /**< GGG: peak wind gusts, knots. */
    uint16_t central_pressure_mb;           /**< ppp: central barometric pressure, millibars. */
    uint16_t hurricane_wind_radius_nm;      /**< RRR: radius of hurricane-force winds, nautical miles. */
    uint16_t tropical_storm_wind_radius_nm; /**< rrr: radius of tropical-storm-force winds, nautical miles. */
} aprs_storm_data_t;

/**
 * @brief National Weather Service bulletin / county warning message,
 *        addressed to "NWS-xxxxx" (APRS101 Ch.14; WX.TXT "COUNTY
 *        WARNINGS").
 *
 * On-air layout: `NWS-WARN:exptime,type,c1,c2,c3,c4,c5,` (or WATCH,
 * ADVIS, TEST, CANCL variants).
 */
typedef enum {
    APRS_NWS_BULLETIN_WARN,  /**< Warning: imminent hazardous condition. */
    APRS_NWS_BULLETIN_WATCH, /**< Watch: conditions favorable for a hazard. */
    APRS_NWS_BULLETIN_ADVIS, /**< Advisory: less severe than a warning. */
    APRS_NWS_BULLETIN_TEST,  /**< Test message. */
    APRS_NWS_BULLETIN_CANCL  /**< Cancellation of a previously issued bulletin. */
} aprs_nws_bulletin_kind_t;

#define APRS_NWS_MAX_COUNTIES 5

typedef struct {
    aprs_nws_bulletin_kind_t kind;                          /**< WARN/WATCH/ADVIS/TEST/CANCL. */
    aprs_timestamp_t expiration;                            /**< exptime: DDHHMM expiration time (absent for CANCL). */
    char hazard_type[APRS_TELEMETRY_PARAM_NAME_MAXLEN + 1]; /**< Free-text hazard type (e.g. "TORNADO", "FLASH FLOOD"). */
    char county[APRS_NWS_MAX_COUNTIES][10];                 /**< Up to 5 county codes, each <= 9 bytes (state prefix "SS_" + 6-char county abbreviation). */
    uint8_t county_count;                                   /**< Number of counties actually populated (0-5). */
} aprs_nws_bulletin_t;

/* ======================================================================
 * SECTION 8: MESSAGES, BULLETINS AND ANNOUNCEMENTS (APRS101 Chapter 14)
 * ====================================================================== */

/** @brief Which sub-type of ':'-DTI packet this is. */
typedef enum {
    APRS_MSG_KIND_MESSAGE,        /**< Point-to-point message to a specific addressee, with optional ACK. */
    APRS_MSG_KIND_ACK,            /**< Message acknowledgement ("ackNNNNN"). */
    APRS_MSG_KIND_REJECT,         /**< Message rejection ("rejNNNNN"). */
    APRS_MSG_KIND_BULLETIN,       /**< General bulletin, addressee "BLNn" (n = 0-9). */
    APRS_MSG_KIND_GROUP_BULLETIN, /**< Group bulletin, addressee "BLNnid" (id = up to 5-char group name). */
    APRS_MSG_KIND_ANNOUNCEMENT,   /**< Announcement, addressee "BLNn" reserved usage per convention. */
    APRS_MSG_KIND_NWS_BULLETIN,   /**< Special-cased NWS bulletin, addressee "NWS-xxxxx". */
    APRS_MSG_KIND_NTS_RADIOGRAM,  /**< National Traffic System formatted radiogram, addressee "NTSstn". */
    APRS_MSG_KIND_TELEMETRY_PARM, /**< ":addressee:PARM.…" telemetry parameter-name metadata message. */
    APRS_MSG_KIND_TELEMETRY_UNIT, /**< ":addressee:UNIT.…" telemetry unit/label metadata message. */
    APRS_MSG_KIND_TELEMETRY_EQNS, /**< ":addressee:EQNS.…" telemetry equation-coefficient metadata message. */
    APRS_MSG_KIND_TELEMETRY_BITS  /**< ":addressee:BITS.…" telemetry bit-sense/project-name metadata message. */
} aprs_message_kind_t;

/**
 * @brief Fully decoded Message / Bulletin / Announcement packet.
 *
 * On-air layout: `:addressee:text{msg-id` (addressee always exactly 9
 * bytes, space-padded).
 */
typedef struct {
    aprs_message_kind_t kind;
    char addressee[APRS_CALLSIGN_SSID_LEN + 1];   /**< 9-byte space-padded addressee (callsign, BLNn, BLNnID, NWS-xxxxx, NTSstn, etc.). */
    char text[APRS_MAX_MESSAGE_TEXT_LEN + 1];     /**< Message/bulletin/announcement free text. */
    bool has_message_id;                          /**< true if a "{mm}" message number is present (enables ACK/REJ handshake). */
    char message_id[APRS_MESSAGE_NUMBER_LEN + 1]; /**< Message number, 1-5 alphanumeric characters. */
} aprs_message_t;

/* ======================================================================
 * SECTION 9: STATUS REPORTS  (APRS101 Chapter 16)
 * ====================================================================== */

/**
 * @brief Decoded Status Report, DTI '>' .
 * On-air layout: `>DDHHMMz status-text` (timestamp optional) or, in the
 * special Maidenhead-locator form: `>IO91SX/G status-text`.
 */
typedef struct {
    bool has_timestamp;                             /**< true if a DDHHMMz timestamp precedes the status text (zulu format only, per APRS101). */
    aprs_timestamp_t timestamp;                     /**< Valid only if has_timestamp == true. */
    char status_text[APRS_MAX_STATUS_TEXT_LEN + 1]; /**< Free-text status message. */

    bool has_maidenhead; /**< true if a Maidenhead grid locator is embedded in the status text. */
    char maidenhead[7];  /**< 4 or 6-character Maidenhead grid square, NUL-terminated. */

    bool has_beam_heading_power;   /**< true for the special Meteor-Scatter "beam heading/power" status form. */
    uint16_t beam_heading_deg;     /**< Antenna beam heading, degrees. */
    uint8_t beam_power_watts_code; /**< Transmitter power code, watts (same 0-9 code table as PHG power). */
} aprs_status_report_t;

/* ======================================================================
 * SECTION 10: QUERIES (APRS101 Chapter 15)
 * ====================================================================== */

/** @brief Standard APRS query types (DTI '?'). */
typedef enum {
    APRS_QUERY_APRS,    /**< "?APRS?"      General "are you an APRS station" query. */
    APRS_QUERY_APRSD,   /**< "?APRSD"      Request a list of all directly heard stations. */
    APRS_QUERY_APRSH,   /**< "?APRSH"      Request station heard-list with statistics. */
    APRS_QUERY_APRSS,   /**< "?APRSS"      Request general station status. */
    APRS_QUERY_APRST,   /**< "?APRST"      Request a traceroute-style digipeater path report. */
    APRS_QUERY_IGATE,   /**< "?IGATE?"     Query for an Internet Gateway station. */
    APRS_QUERY_WX,      /**< "?WX?"        Request the nearest weather station's report. */
    APRS_QUERY_GPS,     /**< "?GPS?"       Request current GPS position. */
    APRS_QUERY_PARM,    /**< "?PARM?"      Request telemetry parameter definitions. */
    APRS_QUERY_UNIT,    /**< "?UNIT?"      Request telemetry unit definitions. */
    APRS_QUERY_DIRECTED /**< Directed station query: "callsign:?query?" addressed to a specific station. */
} aprs_query_type_t;

typedef struct {
    aprs_query_type_t type;
    char footprint[8];                                /**< Optional query target footprint (Maidenhead-based area restriction), if present. */
    bool is_directed;                                 /**< true if this query was addressed to a specific station rather than broadcast. */
    char target_callsign[APRS_CALLSIGN_SSID_LEN + 1]; /**< Target station callsign, valid only if is_directed == true. */
} aprs_query_t;

/* ======================================================================
 * SECTION 11: OBJECT AND ITEM REPORTS (APRS101 Chapter 11)
 *   Frequently used to plot fixed/movable weather-related map features
 *   (Skywarn spotters, warning boxes, NWS sites, storm tracks).
 * ====================================================================== */

typedef enum {
    APRS_OBJECT_LIVE = '*',  /**< Object is live/active. */
    APRS_OBJECT_KILLED = '_' /**< Object has been killed (removed from other stations' maps). */
} aprs_object_state_t;

typedef struct {
    char name[APRS_MAX_OBJECT_NAME_LEN + 1]; /**< Fixed 9-character object name. */
    aprs_object_state_t state;               /**< Live ('*') or Killed ('_'). */
    aprs_timestamp_t timestamp;              /**< Object report timestamp (DHM or HMS format). */
    aprs_position_t position;                /**< Object's geographic position and symbol. */
    bool has_area;                           /**< true if this is an Area Object (circle/line/triangle/etc). */
    aprs_area_object_t area;                 /**< Valid only if has_area == true. */
    bool has_weather;                        /**< true if the object carries an embedded weather report (raw weather data extension). */
    aprs_weather_report_t weather;           /**< Valid only if has_weather == true. */
    char comment[APRS_MAX_COMMENT_LEN + 1];  /**< Free-text comment. */
} aprs_object_report_t;

typedef struct {
    char name[APRS_MAX_OBJECT_NAME_LEN + 1]; /**< Variable-length item name (3-9 characters). */
    aprs_object_state_t state;               /**< Live ('*') or Killed ('!' for items per spec nuance). */
    aprs_position_t position;                /**< Item's geographic position and symbol (no timestamp field for Items). */
    char comment[APRS_MAX_COMMENT_LEN + 1];  /**< Free-text comment. */
} aprs_item_report_t;

/* ======================================================================
 * SECTION 12: MIC-E DATA FORMAT (APRS101 Chapter 10)
 *   Included because Mic-E packets may carry an embedded Telemetry
 *   payload or Status text (which may itself carry Maidenhead/altitude).
 * ====================================================================== */

/** @brief The 15 standard/custom/emergency Mic-E message codes. */
typedef enum {
    APRS_MICE_MSG_OFF_DUTY = 0,   /**< M0 / C0 */
    APRS_MICE_MSG_EN_ROUTE = 1,   /**< M1 / C1 */
    APRS_MICE_MSG_IN_SERVICE = 2, /**< M2 / C2 */
    APRS_MICE_MSG_RETURNING = 3,  /**< M3 / C3 */
    APRS_MICE_MSG_COMMITTED = 4,  /**< M4 / C4 */
    APRS_MICE_MSG_SPECIAL = 5,    /**< M5 / C5 */
    APRS_MICE_MSG_PRIORITY = 6,   /**< M6 / C6 */
    APRS_MICE_MSG_EMERGENCY = 7,  /**< All A/B/C bits = 0. */
    APRS_MICE_MSG_UNKNOWN = 8     /**< Mixed standard/custom bit pattern: undefined per spec. */
} aprs_mice_message_code_t;

typedef struct {
    aprs_position_t position;              /**< Decoded lat/long/symbol. */
    aprs_course_speed_t course_speed;      /**< Decoded course and speed. */
    bool is_custom_message;                /**< true if this is one of the 7 Custom messages rather than Standard. */
    aprs_mice_message_code_t message_code; /**< Standard/Custom/Emergency message code. */

    bool has_telemetry;                /**< true if a Mic-E Telemetry payload (2 or 5 analog bytes + optional digital byte) follows. */
    aprs_telemetry_report_t telemetry; /**< Valid only if has_telemetry == true; sequence field unused (Mic-E telemetry has no sequence number). */

    bool has_status_text; /**< true if a Mic-E status text string follows instead of telemetry. */
    char
        status_text[APRS_MAX_STATUS_TEXT_LEN + 1]; /**< Free-text Mic-E status; may itself embed a Maidenhead locator ("IO91SX/G") or altitude ("/A=001234"). */
} aprs_mice_report_t;

/* ======================================================================
 * SECTION 13: TOP-LEVEL PACKET CONTAINER
 * ====================================================================== */

/**
 * @brief Discriminator for the top-level decoded-packet union, mirroring
 *        the Data Type Identifier of the underlying AX.25 Information
 *        field, restricted to the packet kinds modeled by this header.
 */
typedef enum {
    APRS_PACKET_WEATHER,
    APRS_PACKET_TELEMETRY_REPORT,
    APRS_PACKET_TELEMETRY_METADATA,
    APRS_PACKET_STORM_DATA,
    APRS_PACKET_NWS_BULLETIN,
    APRS_PACKET_MESSAGE,
    APRS_PACKET_STATUS,
    APRS_PACKET_QUERY,
    APRS_PACKET_OBJECT,
    APRS_PACKET_ITEM,
    APRS_PACKET_MICE,
    APRS_PACKET_POSITION_ONLY /**< Plain position report carrying none of the above payloads. */
} aprs_packet_kind_t;

/**
 * @brief Generic decoded APRS packet envelope. Application code inspects
 *        @c kind and reads the corresponding member of the union.
 *
 * @note  Source callsign/SSID, destination address, digipeater path and
 *        the AX.25 framing itself are intentionally out of scope for
 *        this header, which focuses exclusively on Weather and
 *        Telemetry payload modeling; a full AX.25/KISS frame layer
 *        should wrap this structure in a production implementation.
 */
typedef struct {
    aprs_packet_kind_t kind;
    aprs_data_type_identifier_t dti; /**< Raw Data Type Identifier byte actually observed/to be transmitted. */

    /**
     * @brief Weather and Telemetry payloads are intentionally NOT part of
     *        @ref payload below.
     */

    /** @brief Array of decoded Weather Reports carried by/associated with
     *         this packet. NULL if @ref weather_qty is 0. */
    aprs_weather_report_t *weather;
    /** @brief Number of entries populated in @ref weather. 0 if this
     *         packet carries no weather payload. */
    size_t weather_qty;

    /** @brief Array of decoded Telemetry Reports ("T#..." data packets)
     *         carried by/associated with this packet. NULL if
     *         @ref telemetry_report_qty is 0. */
    aprs_telemetry_report_t *telemetry_report;
    /** @brief Number of entries populated in @ref telemetry_report. 0 if
     *         this packet carries no telemetry data report. */
    size_t telemetry_report_qty;

    /** @brief Array of decoded Telemetry Metadata messages (PARM/UNIT/
     *         EQNS/BITS) carried by/associated with this packet. NULL if
     *         @ref telemetry_metadata_qty is 0. */
    aprs_telemetry_metadata_t *telemetry_metadata;
    /** @brief Number of entries populated in @ref telemetry_metadata. 0
     *         if this packet carries no telemetry metadata message. */
    size_t telemetry_metadata_qty;

    union {
        aprs_storm_data_t storm;
        aprs_nws_bulletin_t nws_bulletin;
        aprs_message_t message;
        aprs_status_report_t status;
        aprs_query_t query;
        aprs_object_report_t object;
        aprs_item_report_t item;
        aprs_mice_report_t mice;
        aprs_position_t position;
    } payload; /**< Active member selected by @c kind, for the remaining packet kinds that are genuinely mutually exclusive (a frame cannot simultaneously be,
                 e.g., both a Status report and an Object report). Not used for APRS_PACKET_WEATHER, APRS_PACKET_TELEMETRY_REPORT or
                 APRS_PACKET_TELEMETRY_METADATA - see @ref weather, @ref telemetry_report and @ref telemetry_metadata instead. */

    /** @brief Optional array of application-level "extended" sensor
     *         readings that have no reserved APRS on-air token (see
     *         ::aprs_weather_extended_sensor_t). Only meaningful when
     *         @ref weather_qty is nonzero. NULL/0 if unused. */
    aprs_weather_extended_sensor_t *extended_sensors;
    size_t extended_sensor_count;
} weather_telemetry_data_t;

#endif /* WEATHER_TELEMETRY_H */
