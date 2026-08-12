/**
 * @file aprs_dao.h
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
 * @brief Builds the APRS !DAO! precision/datum extension (aprs12/datum.txt),
 * the 5-byte field a position comment carries to recover the third decimal
 * minute digit of latitude and longitude - the digit the uncompressed
 * "DDMM.mmN"/"DDDMM.mmW" fields (APRS101 chapter 6) round away - plus an
 * explicit datum identification.
 *
 * The extension is a self-contained token, "!" + one datum byte + one digit
 * each for latitude and longitude + "!", placed anywhere in the position
 * comment (the end is the documented convention). It is purely additive: a
 * receiver that does not recognize it just sees five extra bytes of comment
 * text, so it is always safe to append.
 *
 * Transmission uses the human-readable form only (uppercase datum byte, one
 * extra decimal digit per axis), against the WGS-84 datum ('W') this
 * firmware's positions are already in. Reception accepts both forms: the
 * human-readable one and the base-91 one (lower-case datum byte), which is
 * what most trackers emit and which resolves the same hundredth of a minute
 * into 91 steps instead of 10.
 */

#ifndef APRS_DAO_H
#define APRS_DAO_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Size of the buffer aprs_dao_build() requires: "!Wxy!" (5 bytes) plus
 * its NUL terminator.
 */
#define APRS_DAO_BUF_SIZE 6

/**
 * @brief Build the human-readable WGS-84 "!DAO!" precision/datum extension
 * for a decimal-degrees position.
 *
 * Recovers, as a single extra digit per axis, the third decimal minute digit
 * that aprs_coord_format_ambiguous() rounds away when it prints the minutes
 * field to two decimal places - one order of magnitude more precision than
 * the plain "DDMM.mmN"/"DDDMM.mmW" fields carry, matching this firmware's own
 * float latitude/longitude resolution.
 *
 * The output is always exactly 5 bytes: '!', the datum byte 'W' (uppercase =
 * human-readable form, WGS-84), the latitude's extra digit, the longitude's
 * extra digit, and a closing '!'.
 *
 * @param lat Latitude in decimal degrees (positive = N, negative = S).
 * @param lon Longitude in decimal degrees (positive = E, negative = W).
 * @param out Destination buffer; must be at least ::APRS_DAO_BUF_SIZE bytes.
 *            Left untouched if too small or NULL.
 */
void aprs_dao_build(float lat, float lon, char out[APRS_DAO_BUF_SIZE]);

/**
 * @brief Find and decode a "!DAO!" precision/datum extension inside a
 * position comment.
 *
 * Scans @p text for the first well-formed 5-byte token, "!" + datum byte +
 * two axis bytes + "!", and reports how much extra latitude and longitude
 * the sender encoded in it. Both on-air forms are accepted, told apart by
 * the case of the datum byte exactly as aprs12/datum.txt specifies:
 *
 * - Upper-case datum (human-readable): each axis byte is a decimal digit
 *   giving the third decimal place of the minutes field, i.e. 0.001 minute
 *   per unit.
 * - Lower-case datum (base-91): each axis byte is a base-91 digit ('!'
 *   through '{') giving the position inside the hundredth of a minute the
 *   plain field rounds to, i.e. 0.01/91 minute per unit.
 *
 * A space in an axis byte means "not specified" and yields 0 for that axis.
 * The values are magnitudes, always positive: they refine a coordinate away
 * from zero, so a caller applies them to the absolute value of its latitude
 * and longitude before restoring the hemisphere sign.
 *
 * @param text Text to scan (typically the comment of a position report).
 *             May be NULL, which yields false.
 * @param len Number of bytes of @p text to scan.
 * @param out_lat_extra_min Extra latitude, in minutes, in the range
 *                          [0, 0.01). May be NULL.
 * @param out_lon_extra_min Extra longitude, in minutes, same range. May be
 *                          NULL.
 * @return true if a token was found and decoded, false otherwise (in which
 *         case both outputs are left untouched).
 */
bool aprs_dao_parse(const char *text, size_t len, float *out_lat_extra_min, float *out_lon_extra_min);

#endif // APRS_DAO_H
