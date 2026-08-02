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
 * @brief Shared decimal-degrees -> APRS position field conversion:
 * uncompressed (with optional position ambiguity), base-91 compressed, and
 * Maidenhead grid locator. See aprs_coord.h.
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
    aprs_coord_format_ambiguous(lat, lon, 0, latOut, latMax, lonOut, lonMax);
}

// Overwrites the `count` least significant minute digits of an already
// formatted position field with spaces, skipping the decimal point that sits
// between the whole minutes and their fraction. `digits` lists the field's
// digit offsets from least to most significant (hundredths, tenths, units of
// minutes, tens of minutes), which differ between the 8-byte latitude field
// and the 9-byte longitude field. The field is written in place and its width
// never changes - blanking a digit is exactly how APRS101 chapter 6 signals
// reduced precision.
static void blankMinuteDigits(char *field, size_t fieldLen, const uint8_t *digits, int count) {
    for (int i = 0; i < count; i++) {
        size_t pos = digits[i];
        if (pos < fieldLen)
            field[pos] = ' ';
    }
}

void aprs_coord_format_ambiguous(float lat, float lon, uint8_t ambiguity, char *latOut, size_t latMax, char *lonOut, size_t lonMax) {
    if (ambiguity > APRS_COORD_AMBIGUITY_MAX)
        ambiguity = APRS_COORD_AMBIGUITY_MAX;

    // Build both fields at full precision first, then blank digits. The
    // rounding carry in splitDegMin() therefore still applies at every
    // ambiguity level, so a coordinate that rounds up to the next degree is
    // reported in that degree rather than in the one below it.
    int dLat;
    float mLat;
    splitDegMin(fabsf(lat), &dLat, &mLat);

    int dLon;
    float mLon;
    splitDegMin(fabsf(lon), &dLon, &mLon);

    // "DDMM.mmN" and "DDDMM.mmE": assembled in local buffers of the exact
    // on-air width so the digit offsets below are fixed regardless of what
    // the caller's buffers can hold.
    char latField[9];
    char lonField[10];
    snprintf(latField, sizeof(latField), "%02d%05.2f%c", dLat, mLat, lat >= 0 ? 'N' : 'S');
    snprintf(lonField, sizeof(lonField), "%03d%05.2f%c", dLon, mLon, lon >= 0 ? 'E' : 'W');

    if (ambiguity > 0) {
        static const uint8_t latDigits[APRS_COORD_AMBIGUITY_MAX] = { 6, 5, 3, 2 };
        static const uint8_t lonDigits[APRS_COORD_AMBIGUITY_MAX] = { 7, 6, 4, 3 };
        blankMinuteDigits(latField, sizeof(latField) - 1, latDigits, ambiguity);
        blankMinuteDigits(lonField, sizeof(lonField) - 1, lonDigits, ambiguity);
    }

    snprintf(latOut, latMax, "%s", latField);
    snprintf(lonOut, lonMax, "%s", lonField);
}

void aprs_maidenhead_locator(float lat, float lon, char *out, size_t outMax) {
    if (out == NULL || outMax == 0)
        return;

    // Shift into the all-positive Maidenhead domain: 0..180 degrees of
    // latitude from the south pole, 0..360 degrees of longitude from the
    // antimeridian. Clamping just inside the upper edge keeps a position
    // exactly at the north pole or the antimeridian inside the last field
    // ('R') instead of stepping one letter past the end of the alphabet.
    double la = (double)lat + 90.0;
    double lo = (double)lon + 180.0;
    if (la < 0.0)
        la = 0.0;
    if (la > 179.999999)
        la = 179.999999;
    if (lo < 0.0)
        lo = 0.0;
    if (lo > 359.999999)
        lo = 359.999999;

    char field[APRS_MAIDENHEAD_BUF_SIZE];
    // Field: 20 degrees of longitude / 10 degrees of latitude per letter.
    field[0] = (char)('A' + (int)(lo / 20.0));
    field[1] = (char)('A' + (int)(la / 10.0));
    // Square: one tenth of a field, i.e. 2 degrees of longitude / 1 degree of
    // latitude per digit.
    lo -= 20.0 * (int)(lo / 20.0);
    la -= 10.0 * (int)(la / 10.0);
    field[2] = (char)('0' + (int)(lo / 2.0));
    field[3] = (char)('0' + (int)la);
    // Subsquare: one twenty-fourth of a square, i.e. 5 minutes of longitude /
    // 2.5 minutes of latitude per letter.
    lo -= 2.0 * (int)(lo / 2.0);
    la -= (double)(int)la;
    field[4] = (char)('A' + (int)(lo * 12.0));
    field[5] = (char)('A' + (int)(la * 24.0));
    field[6] = 0;

    size_t n = APRS_MAIDENHEAD_LEN;
    if (n > outMax - 1)
        n = outMax - 1;
    memcpy(out, field, n);
    out[n] = 0;
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

    // The field is a fixed 13 bytes: symbol table + 4 compressed-latitude
    // digits + 4 compressed-longitude digits + symbol code + the 3-byte cs/T
    // token, so a caller needs 14 bytes to hold it with its NUL (see the
    // header). It is assembled byte-by-byte and copied out with strlcpy-style
    // truncation rather than through an snprintf() "%c" chain, so a
    // caller-provided outMax smaller than the full field still copies as much
    // as fits and always terminates.
    char field[13];
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
    field[11] = cs[1];
    field[12] = cs[2];

    if (outMax == 0)
        return;
    size_t n = sizeof(field);
    if (n > outMax - 1)
        n = outMax - 1;
    memcpy(out, field, n);
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

bool aprs_extract_symbol(const char *info, size_t infoLen, char *symTable, char *symCode) {
    if (!info || !symTable || !symCode || infoLen == 0)
        return false;

    // Position reports start with one of !=/@; '/' and '@' additionally
    // carry a fixed 7-byte DHM/HMS timestamp between the DTI and the
    // 8-byte latitude field, shifting every following offset by 7 bytes
    // relative to the no-timestamp forms. See APRS101 chapters 5 and 8.
    bool hasTimestamp = (info[0] == '/' || info[0] == '@');
    bool isPosition = (info[0] == '!' || info[0] == '=' || hasTimestamp);
    if (!isPosition)
        return false;

    size_t tsShift = hasTimestamp ? 7 : 0;
    size_t minLen = 20 + tsShift; // DTI[1] + [timestamp[7]] + lat[8] + symtable[1] + lon[9] + symcode[1]
    if (infoLen < minLen)
        return false;

    *symTable = info[9 + tsShift];
    *symCode = info[19 + tsShift];
    return true;
}
