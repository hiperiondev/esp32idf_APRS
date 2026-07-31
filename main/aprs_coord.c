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
 * @brief Shared decimal-degrees -> APRS position field conversion, both
 * uncompressed and base-91 compressed. See aprs_coord.h.
 */

#include "aprs_coord.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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

// Encodes a non-negative integer as 4 base-91 digits (most significant
// first), each offset by the standard '!' (33) base per APRS101 chapter 9.
// `val` is clamped to the 0..91^4-1 range so a coordinate exactly at a pole
// or the antimeridian never overflows the 4-digit field.
static void base91Encode4(long val, char *out) {
    if (val < 0)
        val = 0;
    const long maxVal = 91L * 91L * 91L * 91L - 1L;
    if (val > maxVal)
        val = maxVal;

    for (int i = 3; i >= 0; i--) {
        long place = 1;
        for (int p = 0; p < i; p++)
            place *= 91;
        long digit = (val / place) % 91;
        out[3 - i] = (char)('!' + digit);
    }
}

void aprs_coord_format_compressed(float lat, float lon, char symTable, char symCode, const char csT[3], char *out, size_t outMax) {
    long latv = lroundf(380926.0f * (90.0f - lat));
    long lonv = lroundf(190463.0f * (180.0f + lon));

    char latDigits[4];
    char lonDigits[4];
    base91Encode4(latv, latDigits);
    base91Encode4(lonv, lonDigits);

    char cs[3] = { ' ', ' ', ' ' };
    if (csT && csT[0] && csT[1] && csT[2]) {
        cs[0] = csT[0];
        cs[1] = csT[1];
        cs[2] = csT[2];
    }

    // Fixed 12-byte field ("!" table + 4 lat + 4 lon + code + 3 cs/T + NUL),
    // assembled byte-by-byte and copied out with strlcpy-style truncation
    // rather than snprintf()'s "%c" chain so a caller-provided outMax
    // smaller than the full field still copies as much as fits and always
    // NUL-terminates.
    char field[12];
    field[0] = symTable;
    field[1] = latDigits[0];
    field[2] = latDigits[1];
    field[3] = latDigits[2];
    field[4] = latDigits[3];
    field[5] = lonDigits[0];
    field[6] = lonDigits[1];
    field[7] = lonDigits[2];
    field[8] = lonDigits[3];
    field[9] = symCode;
    field[10] = cs[0];
    // cs[1] and cs[2] are appended below via memcpy since field[] above only
    // reserves up to the first cs/T byte within its 11 meaningful bytes.
    char full[13];
    memcpy(full, field, 11);
    full[11] = cs[1];
    full[12] = cs[2];

    if (outMax == 0)
        return;
    size_t n = sizeof(full);
    if (n > outMax - 1)
        n = outMax - 1;
    memcpy(out, full, n);
    out[n] = 0;
}

void aprs_compressed_cs_from_course_speed(unsigned course_deg, unsigned speed_knots, char out[3]) {
    unsigned course = course_deg % 360;
    if (course == 0)
        course = 360; // table has no "0 degrees" entry; wraps to 360 per spec

    long cDigit = lroundf((float)course / 4.0f);
    if (cDigit < 0)
        cDigit = 0;
    if (cDigit > 90)
        cDigit = 90;

    long sDigit;
    if (speed_knots == 0) {
        sDigit = 0;
    } else {
        sDigit = lroundf(logf(1.0f + (float)speed_knots / 0.076f) / logf(1.08f));
        if (sDigit < 0)
            sDigit = 0;
        if (sDigit > 90)
            sDigit = 90;
    }

    out[0] = (char)('!' + cDigit);
    out[1] = (char)('!' + sDigit);
    out[2] = 'C'; // compression type: compressed Course/Speed, current GPS fix, no NMEA source, no compression origin flag
}
