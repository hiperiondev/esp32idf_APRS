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
 * '/'/'@' (with timestamp), ';' (object) and ')' (item).
 *
 * @note Mic-E position reports carry their position in the AX.25 destination
 *       field, not the info field, and are not decodable from @p info alone;
 *       this returns false for them (and for any other non-position DTI).
 *
 * @param info NUL-terminated APRS information field.
 * @param out_lat Set to the decoded latitude, decimal degrees (+N/-S), on success.
 * @param out_lon Set to the decoded longitude, decimal degrees (+E/-W), on success.
 * @return true if a position was found and decoded.
 */
bool aprs_filter_decode_position(const char *info, float *out_lat, float *out_lon);

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
