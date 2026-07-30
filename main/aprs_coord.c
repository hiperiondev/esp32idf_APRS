/**
 * @file aprs_coord.c
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
 * conversion. See aprs_coord.h.
 */

#include "aprs_coord.h"

#include <math.h>
#include <stdio.h>

// Splits an absolute-value decimal-degrees coordinate into whole degrees and
// minutes, rounding the minutes to two decimal places first and carrying
// into the degrees when that rounds up to 60.00. This keeps the minutes
// field within the valid 00.00-59.99 range in every case, including
// fractional degrees that split to a minutes value of e.g. 59.997.
static void splitDegMin(float absVal, int *deg, float *min) {
    int d = (int)absVal;
    float m = (absVal - d) * 60.0f;
    if (m >= 59.995f) {
        m = 0.0f;
        d += 1;
    }
    *deg = d;
    *min = m;
}

void aprs_coord_format(float lat, float lon, char *latOut, size_t latMax, char *lonOut, size_t lonMax) {
    int dLat;
    float mLat;
    splitDegMin(fabsf(lat), &dLat, &mLat);
    snprintf(latOut, latMax, "%02d%05.2f%c", dLat, mLat, lat >= 0 ? 'N' : 'S');

    int dLon;
    float mLon;
    splitDegMin(fabsf(lon), &dLon, &mLon);
    snprintf(lonOut, lonMax, "%03d%05.2f%c", dLon, mLon, lon >= 0 ? 'E' : 'W');
}
