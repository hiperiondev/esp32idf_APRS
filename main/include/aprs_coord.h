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
 * @brief Shared decimal-degrees -> APRS uncompressed position field
 * conversion, used by every module that builds a position report (station
 * beacons, Objects/Items, weather reports).
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

#endif
