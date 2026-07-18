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

#endif // APRS_FILTER_H
