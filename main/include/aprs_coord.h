/**
 * @file aprs_coord.h
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
 * @brief Shared decimal-degrees -> APRS position field conversion, used by
 * every module that builds a position report (station beacons, Objects/Items,
 * weather reports). Covers both the uncompressed "DDMM.mmN/DDDMM.mmW" field
 * pair and the base-91 compressed position format (APRS101 chapter 9).
 */

#ifndef APRS_COORD_H
#define APRS_COORD_H

#include <stddef.h>

/**
 * @brief Format a decimal-degrees latitude/longitude pair as the APRS
 * uncompressed position fields "DDMM.mmN"/"S" and "DDDMM.mmE"/"W".
 *
 * The minutes value is rounded to two decimal places before the
 * degrees/minutes split is finalized, so a minutes value that rounds up to
 * 60.00 carries into the degrees field instead of being emitted as-is.
 * This keeps the output within the valid MM.mm range of 00.00-59.99
 * required by the APRS spec.
 *
 * @param lat Latitude in decimal degrees (positive = N, negative = S).
 * @param lon Longitude in decimal degrees (positive = E, negative = W).
 * @param latOut Destination buffer for the latitude field.
 * @param latMax Size of latOut.
 * @param lonOut Destination buffer for the longitude field.
 * @param lonMax Size of lonOut.
 */
void aprs_coord_format(float lat, float lon, char *latOut, size_t latMax, char *lonOut, size_t lonMax);

/**
 * @brief Format a decimal-degrees latitude/longitude pair as the APRS
 * base-91 compressed position field, per APRS101 chapter 9: symbol-table
 * byte, 4 compressed-latitude digits, 4 compressed-longitude digits, symbol
 * code, and the 3-byte compression type/course-speed token.
 *
 * @param lat Latitude in decimal degrees (positive = N, negative = S).
 * @param lon Longitude in decimal degrees (positive = E, negative = W).
 * @param symTable Symbol table byte ('/' primary or '\\' alternate, or an
 *        overlay character).
 * @param symCode Symbol code byte.
 * @param csT The 3-byte course/speed-or-PHG-or-radio-range token described
 *        by aprs_compressed_cs_from_course_speed(), or "   " (3 spaces) to
 *        emit "no cs/T data" per spec.
 * @param out Destination buffer for the compressed position field (symbol
 *        table byte + 4 lat digits + 4 lon digits + symbol code + 3-byte csT).
 * @param outMax Size of out; must be >= 14 to hold the full 13-byte field plus
 *        its NUL. A smaller buffer is not an error - the field is copied as
 *        far as it fits and always terminated - but a truncated compressed
 *        position still decodes, as a *different* coordinate, so any caller
 *        that cannot guarantee 14 bytes here puts wrong positions on the air.
 */
void aprs_coord_format_compressed(float lat, float lon, char symTable, char symCode, const char csT[3], char *out, size_t outMax);

/**
 * @brief Build the 2-byte compressed course/speed token described by
 * APRS101 chapter 9, encoding a course of 1-360 degrees as base-91 digit
 * `round(course / 4)` and a speed of 0+ knots as base-91 digit
 * `round(log(1 + speed/0.076) / log(1.08))`, each offset by the standard
 * '!' base and followed by the fixed compression-type byte for "compressed
 * Course/Speed, current GPS fix".
 *
 * Per spec, course 0 has no valid encoding (the table starts at 1 degree),
 * so a course of exactly 0 is emitted as 360 (i.e. due north wraps to the
 * top of the table) rather than being silently dropped.
 *
 * @param course_deg Course over ground, degrees (0-359; 0 is treated as 360).
 * @param speed_knots Speed over ground, knots.
 * @param out 3-byte destination for the course/speed-and-type token.
 */
void aprs_compressed_cs_from_course_speed(unsigned course_deg, unsigned speed_knots, char out[3]);

#endif
