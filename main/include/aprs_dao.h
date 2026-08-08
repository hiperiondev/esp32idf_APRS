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
 * Only the human-readable form (uppercase datum byte, one extra decimal
 * digit per axis) is implemented here, against the WGS-84 datum ('W') this
 * firmware's positions are already in.
 */

#ifndef APRS_DAO_H
#define APRS_DAO_H

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

#endif // APRS_DAO_H
