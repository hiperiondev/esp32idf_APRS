/**
 * @file aprs_filter.h
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
 * @brief APRS payload classification for the IGate [Filter] bitmasks
 * (g_config.rf2inetFilter / g_config.inet2rfFilter).
 *
 * Both masks use the same IGATE_FILT_* bits declared in app_config.h, and both
 * mean the same thing: "relay a packet only if the bit matching its payload
 * type is set". This is the single place that decides which bit a given packet
 * belongs to, so the two directions can never drift apart.
 *
 * The classifier works on the APRS *information field* (everything after the
 * first ':' of a TNC2 line), i.e. on the APRS data type identifier (DTI) and,
 * where the DTI alone is ambiguous (a position report may be a plain position,
 * a weather station or a buoy), on the symbol the report carries.
 */

#ifndef APRS_FILTER_H
#define APRS_FILTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "app_config.h"

/**
 * @brief Classify one TNC2 text line ("SRC-N>DST,PATH:info") into exactly one
 * IGATE_FILT_* bit.
 *
 * @param line NUL-terminated TNC2 line. The header is only used to find the
 *             start of the information field; classification depends solely on
 *             the payload.
 * @return The single IGATE_FILT_* bit describing the payload, or 0 when the
 *         payload is malformed or is of a kind no filter bit covers (station
 *         capabilities, third-party traffic, user-defined formats, ...).
 *         0 never passes aprs_filter_pass(), i.e. unknown means "do not relay".
 */
uint16_t aprs_filter_classify_tnc2(const char *line);

/**
 * @brief Same as aprs_filter_classify_tnc2() but for an already-isolated
 * information field (no header, starts at the data type identifier).
 *
 * @param info NUL-terminated APRS information field.
 * @return The matching IGATE_FILT_* bit, or 0 if none applies.
 */
uint16_t aprs_filter_classify_info(const char *info);

/**
 * @brief Test one classified packet type against a filter bitmask.
 *
 * @param mask One of g_config.rf2inetFilter / g_config.inet2rfFilter.
 * @param type Bit returned by aprs_filter_classify_*().
 * @return true if the packet may be relayed.
 *
 * @note An all-zero @p type (unclassifiable payload) never passes, and an
 *       all-zero @p mask (every checkbox cleared on the web page's IGATE
 *       Filter form) passes nothing - the mask is a whitelist of the payload
 *       types allowed through, exactly as the checkboxes read.
 */
static inline bool aprs_filter_pass(uint16_t mask, uint16_t type) {
    return type != 0 && (mask & type) != 0;
}

/**
 * @brief Short human-readable name of an IGATE_FILT_* bit, for log lines.
 * @param type Bit returned by aprs_filter_classify_*() (0 allowed).
 * @return Static string, never NULL ("unknown" for 0 / unrecognized).
 */
const char *aprs_filter_type_name(uint16_t type);

// ---------------------------------------------------------------------------
// APRS-IS *server-side* filter string validation.
//
// Unrelated to the classifier above: this checks the grammar of
// g_config.aprs_filter, the free-text string forwarded verbatim to the
// APRS-IS server in the login line's "filter ..." clause (see igate.c). It
// does not touch, and is not touched by, aprs_filter_classify_*() /
// aprs_filter_pass(), which gate already-received traffic against the local
// rf2inetFilter/inet2rfFilter bitmasks instead.
// ---------------------------------------------------------------------------

/**
 * @brief Validate an APRS-IS server-side filter string (space-separated
 * terms, each "<letter>/<args>") against the known filter-term grammar.
 *
 * Checks structure only: that the string parses into terms of the form
 * "<letter>/<arg>[/<arg>...]", that <letter> is one of the APRS-IS filter
 * letters, and that each term has the argument count/shape that letter
 * requires (e.g. "r" needs exactly 3 numeric args, "p" needs at least 1
 * prefix). Does not evaluate whether coordinate/distance values are
 * sensible, only whether the server will be able to parse the term at all.
 *
 * @param filter NUL-terminated filter string (may be empty/NULL; both pass).
 * @param err Optional buffer for a human-readable reason on failure.
 * @param errSize Size of @p err (ignored if @p err is NULL).
 * @return true if every term is structurally valid.
 */
bool aprs_filter_validate_server_string(const char *filter, char *err, size_t errSize);

// ---------------------------------------------------------------------------
// Local callsign whitelist/blacklist ("budlist").
//
// Independent of, and composed with (AND semantics), the payload-type
// bitmasks above: a packet must pass both its type filter and this check.
// Shared list (g_config.budlist[]), one mode per direction
// (g_config.rf2inet_budlist_mode / g_config.inet2rf_budlist_mode).
// ---------------------------------------------------------------------------

/**
 * @brief Test a callsign against the local whitelist/blacklist.
 *
 * @param mode One of g_config.rf2inet_budlist_mode / inet2rf_budlist_mode.
 * @param call Callsign to test. May include a "-SSID" suffix (stripped
 *             internally before comparison) or not - both RF (base call
 *             only) and INET (may carry "-SSID") callers can pass their
 *             callsign straight through unmodified. Case-insensitive.
 * @return true if the packet may be relayed (BUDLIST_OFF always passes;
 *         BUDLIST_WHITELIST passes only listed calls; BUDLIST_BLACKLIST
 *         passes everything except listed calls).
 */
bool aprs_filter_budlist_pass(budlist_mode_t mode, const char *call);

// ---------------------------------------------------------------------------
// Local RF->INET range/prefix gate.
//
// Independent of, and composed with (AND semantics), both the payload-type
// bitmasks and the budlist above: a packet must pass all that apply. Unlike
// g_config.aprs_filter (the *server-side* filter APRS-IS applies to what it
// sends INTO the client), this is a purely local knob for what the client
// chooses to push OUT to APRS-IS from RF.
// ---------------------------------------------------------------------------

/**
 * @brief Decode the latitude/longitude carried by an APRS information field,
 * reusing the same DTI dispatch aprs_filter_classify_info() uses to find
 * where the position data starts. Supports both position report layouts
 * (uncompressed "DDMM.hhN/DDDMM.hhW" and compressed base-91) for the DTIs
 * that carry a position directly in the info field: '!'/'=' (no timestamp),
 * '/'/'@' (with timestamp), ';' (object) and ')' (item); and, via
 * aprs_mice_decode(), Mic-E reports ('`', '\'', 0x1c, 0x1d), whose position
 * is split between @p info and the AX.25 destination address field.
 *
 * @param info NUL-terminated APRS information field.
 * @param dst_call 6-character AX.25 destination address field, exactly as
 *                 decoded off the air (see aprs_mice_decode()). Only
 *                 consulted for Mic-E DTIs; NULL is fine for every other
 *                 payload type (and causes Mic-E packets to fail decoding).
 * @param out_lat Set to the decoded latitude, decimal degrees (+N/-S), on success.
 * @param out_lon Set to the decoded longitude, decimal degrees (+E/-W), on success.
 * @return true if a position was found and decoded.
 */
bool aprs_filter_decode_position(const char *info, const char *dst_call, float *out_lat, float *out_lon);

// ---------------------------------------------------------------------------
// Receive-side field decoding.
//
// Everything a well-formed report carries next to its coordinates: the
// compressed cs/T pair (APRS101 chapter 9), the 7-byte data extension slot
// that follows an uncompressed symbol code (chapter 7), the "/A=" altitude
// token, the report's own timestamp (chapter 6) and the "!DAO!" precision
// extension (aprs12/datum.txt). One pass over the information field fills all
// of them, so a consumer that wants to show what a station is doing - moving,
// how fast, how high, how far it reaches, and when it said so - does not have
// to walk the payload again itself.
// ---------------------------------------------------------------------------

/** @brief Which data extension a received report carried, if any. */
typedef enum {
    APRS_RX_EXT_NONE = 0, /**< No extension: the 7-byte slot is comment text. */
    APRS_RX_EXT_CSE_SPD,  /**< "CSE/SPD" course and speed. */
    APRS_RX_EXT_WIND,     /**< Same layout on a weather symbol: wind direction and speed. */
    APRS_RX_EXT_PHG,      /**< "PHGphgd", optionally with the 1.2 "PHGphgdr/" probe rate. */
    APRS_RX_EXT_RNG,      /**< "RNGrrrr" pre-calculated radio range. */
    APRS_RX_EXT_DFS       /**< "DFSshgd" omni-DF signal strength. */
} aprs_rx_ext_t;

/** @brief Value of ::aprs_rx_report_t::phg_rate_per_hour when the received
 *  PHG extension carried no "probes" rate character. */
#define APRS_RX_PHG_RATE_NONE (-1)

/**
 * @brief Everything aprs_filter_decode_report() reads out of one received
 * information field.
 *
 * Every member is only meaningful when the matching @c has_ flag (or, for the
 * extension sub-fields, @c ext) says so; the whole structure is zeroed on
 * entry, so an untouched field reads as absent rather than as stale.
 */
typedef struct {
    bool has_position; /**< A latitude/longitude was decoded. */
    float lat;         /**< Latitude, decimal degrees, +N/-S. */
    float lon;         /**< Longitude, decimal degrees, +E/-W. */
    bool dao_refined;  /**< The position was refined by a "!DAO!" extension in the comment. */

    bool has_course_speed; /**< Course and speed were decoded (compressed cs pair, data extension or Mic-E). */
    uint16_t course_deg;   /**< Course over ground, degrees clockwise from true north, 0-359. */
    float speed_kt;        /**< Speed over ground, knots. */

    bool has_range;    /**< A pre-calculated radio range was decoded (compressed cs pair or "RNGrrrr"). */
    float range_miles; /**< Omnidirectional radio range, statute miles. */
    bool has_altitude; /**< An altitude was decoded (compressed cs pair, "/A=" token or Mic-E). */
    float altitude_ft; /**< Altitude above mean sea level, feet. */

    aprs_rx_ext_t ext;         /**< Which data extension occupied the 7-byte slot. */
    uint16_t phg_power_w;      /**< PHG transmitter power, watts (::APRS_RX_EXT_PHG). */
    uint32_t phg_height_ft;    /**< PHG/DFS antenna height above average terrain, feet. */
    uint8_t phg_gain_db;       /**< PHG/DFS antenna gain, dB. */
    uint8_t phg_dir;           /**< PHG/DFS directivity: 0 = omni, 1-8 = the eight compass octants. */
    int16_t phg_rate_per_hour; /**< PHGR beacon rate, beacons/hour, or ::APRS_RX_PHG_RATE_NONE. */
    uint8_t dfs_strength;      /**< DFS received signal strength, S-points (::APRS_RX_EXT_DFS). */

    bool has_time;       /**< The report carried its own timestamp. */
    bool time_is_zulu;   /**< true for the 'z'/'h' (UTC) forms, false for the legacy local-time '/' form. */
    time_t time_utc;     /**< The timestamp as absolute UTC, or 0 when it is local time or the clock is unset. */
    uint8_t time_day;    /**< Day of month, 0 when the format carries none (HMS). */
    uint8_t time_hour;   /**< Hour, 0-23. */
    uint8_t time_minute; /**< Minute, 0-59. */
    uint8_t time_second; /**< Second, 0-59; 0 for the formats that carry no seconds. */
} aprs_rx_report_t;

/**
 * @brief Decode one received information field into an ::aprs_rx_report_t.
 *
 * Uses the same data type identifier dispatch as
 * aprs_filter_classify_info() to find where the position data starts, so the
 * two can never disagree about a payload's layout. Handles the position
 * report DTIs '!'/'='/'/'/'@', objects (';'), items (')'), raw NMEA ('$'),
 * Mic-E ('`', '\'', 0x1c, 0x1d) and the positionless weather report ('_',
 * timestamp only). Any other payload yields false.
 *
 * @param info NUL-terminated APRS information field.
 * @param dst_call 6-character AX.25 destination address field, as decoded off
 *                 the air. Only consulted for Mic-E DTIs; NULL is fine for
 *                 every other payload type.
 * @param out Destination, zeroed before anything is written to it.
 * @return true if at least one field was decoded (a position, a timestamp or
 *         a data extension).
 */
bool aprs_filter_decode_report(const char *info, const char *dst_call, aprs_rx_report_t *out);

/**
 * @brief Buffer size aprs_filter_format_report() needs for its longest output.
 */
#define APRS_RX_DECODED_BUF_SIZE 64

/**
 * @brief Render the decoded fields of a report as one short operator-facing
 * line, e.g. "121530Z CSE 088 SPD 36kt ALT 1200ft".
 *
 * Only the fields the report actually carried appear, in a fixed order:
 * timestamp, course/speed (or wind), altitude, range, PHG or DFS, and a
 * "DAO" marker when the position was refined. A report with nothing to show
 * produces an empty string.
 *
 * @param report Decoded report; NULL yields an empty string.
 * @param out Destination buffer, always NUL-terminated when @p out_size > 0.
 * @param out_size Size of @p out; ::APRS_RX_DECODED_BUF_SIZE always fits.
 * @return Number of characters written, excluding the NUL terminator.
 */
size_t aprs_filter_format_report(const aprs_rx_report_t *report, char *out, size_t out_size);

/**
 * @brief Report the Mic-E position comment carried by a received packet
 * (APRS101 Chapter 10, "Mic-E Message Types").
 *
 * The comment lives in the A/B/C bits of the destination address, so it takes
 * both halves of the frame to read, exactly like the position itself. This is
 * the receive-side entry point for surfacing it: it answers whether the
 * packet is Mic-E at all, what the operator-visible name of its comment is,
 * and whether that comment is the all-bits-clear Emergency, which a station
 * sends to ask for help and which therefore has to reach the operator rather
 * than sit unremarked in a packet log.
 *
 * @param dst_call 6-character AX.25 destination address field, exactly as
 *                 decoded off the air (see aprs_mice_decode()). NULL simply
 *                 yields false, since a Mic-E packet cannot be read without
 *                 it.
 * @param info APRS information field, starting at its data type identifier.
 * @param len Length of @p info in bytes.
 * @param out_name Set to the static English name of the comment (see
 *                 aprs_mice_message_name()) on success; untouched otherwise.
 *                 May be NULL.
 * @param out_emergency Set to true on success when the comment is Emergency,
 *                      false for any other value - including the mixed
 *                      standard/custom bit pattern the specification leaves
 *                      undefined, which is reported as unknown and never as
 *                      an emergency. May be NULL.
 * @return true if @p info is a well-formed Mic-E report whose comment was
 *         read.
 */
bool aprs_filter_mice_message(const char *dst_call, const char *info, size_t len, const char **out_name, bool *out_emergency);

/**
 * @brief Detect a bracketed APRS 1.2 alert code in the comment field of a
 * non-Mic-E position, object or item report (aprs.org/aprs12/EmergencyCode.txt).
 *
 * Mic-E carries its Emergency/Priority/Special/... indication in the A/B/C
 * bits of the destination address (see aprs_filter_mice_message()), which a
 * station not using Mic-E has no way to set. This proposal lets any station
 * raise the same fourteen indications as plain text, bracketed by '!' and
 * placed where the comment field begins: right after the fixed-length
 * position bytes, and after the data extension slot (PHG/DFS/RNG/CSE-SPD)
 * when one is present.
 *
 * @param info APRS information field, starting at its data type identifier.
 *             Only the four position DTIs ('!' '=' '/' '@'), the object DTI
 *             (';') and the item DTI (')') carry a comment field this way;
 *             every other DTI, including the four Mic-E ones, yields false.
 * @param len Length of @p info in bytes.
 * @param out_name Set to the static English name of the alert code found
 *                 (e.g. "Emergency", "Priority", "WX Alarm") on success;
 *                 untouched otherwise. May be NULL.
 * @param out_emergency Set to true on success when the code found is
 *                      ``!EMERGENCY!``, false for any of the other thirteen
 *                      values - including ``!TESTALARM!``, which is
 *                      deliberately excluded so a station testing its own
 *                      alarm chain never raises a real one downstream. May
 *                      be NULL.
 * @return true if @p info carries a position/object/item comment and that
 *         comment starts with one of the recognised bracketed codes.
 */
bool aprs_filter_comment_alert(const char *info, size_t len, const char **out_name, bool *out_emergency);

/**
 * @brief Great-circle distance between two lat/lon points (haversine formula).
 * @return Distance in kilometers.
 */
float aprs_filter_haversine_km(float lat1, float lon1, float lat2, float lon2);

/**
 * @brief Test a callsign against a comma-separated prefix whitelist (e.g.
 * g_config.rf2inet_prefixes, "EA,EB,EC"). Case-insensitive; whitespace around
 * entries is ignored. A callsign matches if it starts with any listed prefix.
 *
 * @param call Callsign to test (SSID, if any, is irrelevant - only the
 *             leading characters are compared).
 * @param prefixes_csv Comma-separated prefix list. NULL/empty never matches.
 * @return true if @p call starts with one of the listed prefixes.
 */
bool aprs_filter_prefix_match(const char *call, const char *prefixes_csv);

// ---------------------------------------------------------------------------
// Selective third-party ('}') unwrap (INET->RF only).
//
// aprs_filter_classify_info()/aprs_filter_classify_tnc2() deliberately return
// 0 (never passes aprs_filter_pass()) for '}'-prefixed payloads - re-gating
// third-party traffic without restriction is the #1 cause of IGate loops, so
// that default never changes here. This helper exists only to let a caller
// that has ALREADY verified the inner packet's source is explicitly trusted
// (on the local budlist, in BUDLIST_WHITELIST mode) evaluate what that inner
// packet's own payload type is, so it can be relayed if desired. See
// g_config.inet2rf_3rdparty_unwrap_en and aprs_service.c's inet2rfHandler().
// ---------------------------------------------------------------------------

/**
 * @brief Classify the packet wrapped inside a third-party ('}') payload.
 *
 * A third-party payload is itself a complete TNC2-style line prefixed with
 * '}': "}SRC>DST,PATH:payload". This finds the inner ':' the same way
 * aprs_filter_classify_tnc2() finds the outer one, then classifies the inner
 * payload exactly as any other packet.
 *
 * @param info NUL-terminated APRS information field starting with '}'.
 * @return The inner payload's IGATE_FILT_* bit, or 0 if @p info doesn't start
 *         with '}', doesn't parse, or the inner payload is itself unclassifiable.
 */
uint16_t aprs_filter_classify_thirdparty_inner(const char *info);

#endif // APRS_FILTER_H
