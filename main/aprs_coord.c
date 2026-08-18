// @file aprs_coord.c
//
// @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
// @date 2026
// @copyright GNU General Public License v3
// @see https://github.com/hiperiondev/esp32idf_APRS
//
// @note
// This is based on other projects:
//     VP-Digi: https://github.com/sq8vps/vp-digi
//     ESP32APRS: https://github.com/nakhonthai/ESP32APRS_Audio
//     LibAPRS: https://github.com/markqvist/LibAPRS
//
//     please contact their authors for more information.
//
// @brief Shared APRS position and symbol conversion: decimal-degrees ->
// uncompressed (with optional position ambiguity), base-91 compressed and
// Maidenhead grid locator fields on transmit, and raw NMEA-0183 position
// sentences plus destination-address/source-SSID symbols on receive. See
// aprs_coord.h.

#include "aprs_coord.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "aprs_coord";

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

// Translates a configured Symbol Table Identifier into the byte a compressed
// position report is allowed to carry in its first position. APRS 1.2 chapter
// 21 forbids a numeric overlay there because a compressed position field never
// starts with a digit - that byte is what tells a receiver the field is
// compressed at all - so a numeric overlay travels as the matching lower-case
// letter ('0' -> 'a' ... '9' -> 'j') and is mapped back to the digit on
// receive. The primary and alternate tables and the alphabetic overlays are
// already legal here and pass through unchanged. Anything else reaches this
// point from a hand-edited configuration file and falls back to the primary
// table: emitting it would put a byte on air that no receiver can classify.
static char compressedSymbolTable(char symTable) {
    if (symTable >= '0' && symTable <= '9')
        return (char)(APRS_COMPRESSED_OVERLAY_DIGIT_BASE + (symTable - '0'));
    if (symTable == '/' || symTable == '\\' || (symTable >= 'A' && symTable <= 'Z'))
        return symTable;

    ESP_LOGW(TAG, "symbol table identifier 0x%02X is not valid, using '%c'", (unsigned)(unsigned char)symTable, APRS_SYMBOL_TABLE_DEFAULT);
    return APRS_SYMBOL_TABLE_DEFAULT;
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
    field[0] = compressedSymbolTable(symTable);
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
    // Course quantises to the 4 degree step the field is decoded with
    // (course = c * 4), so the digit is the truncated quotient: due north is
    // digit 0 and reads back as 0 degrees. The digit never reaches
    // APRS_COMPRESSED_CS_DIGIT_MAX + 1, the value reserved for the
    // pre-calculated radio range form of the same two bytes.
    long cDigit = (long)((course_deg % 360u) / 4u);
    if (cDigit > APRS_COMPRESSED_CS_DIGIT_MAX)
        cDigit = APRS_COMPRESSED_CS_DIGIT_MAX;

    // Speed is decoded as 1.08^s - 1 knots, so the digit is the inverse of
    // that relation. Speeds past the top of the table saturate at the highest
    // digit, which stands for roughly 940 knots.
    long sDigit;
    if (speed_knots == 0) {
        sDigit = 0;
    } else {
        sDigit = lroundf(logf(1.0f + (float)speed_knots) / logf(1.08f));
        if (sDigit < 0)
            sDigit = 0;
        if (sDigit > APRS_COMPRESSED_CS_DIGIT_MAX)
            sDigit = APRS_COMPRESSED_CS_DIGIT_MAX;
    }

    out[0] = (char)(APRS_COMPRESSED_BASE91_OFFSET + cDigit);
    out[1] = (char)(APRS_COMPRESSED_BASE91_OFFSET + sDigit);
    out[2] = APRS_COMPRESSED_T_BYTE_CS;
}

void aprs_compressed_cs_from_range(unsigned range_miles, char out[3]) {
    // Range is decoded as 2 * 1.08^s miles, so the digit is the inverse of
    // that relation. The form starts at 2 miles (s = 0): anything below that
    // floor, including 0, encodes as the floor rather than as a negative
    // digit, and anything past the top of the table saturates at the highest
    // digit, which stands for roughly 1890 miles.
    long sDigit;
    if (range_miles <= 2) {
        sDigit = 0;
    } else {
        sDigit = lroundf(logf((float)range_miles / 2.0f) / logf(1.08f));
        if (sDigit < 0)
            sDigit = 0;
        if (sDigit > APRS_COMPRESSED_CS_DIGIT_MAX)
            sDigit = APRS_COMPRESSED_CS_DIGIT_MAX;
    }

    out[0] = (char)(APRS_COMPRESSED_BASE91_OFFSET + APRS_COMPRESSED_CS_RANGE_MARKER);
    out[1] = (char)(APRS_COMPRESSED_BASE91_OFFSET + sDigit);
    out[2] = APRS_COMPRESSED_T_BYTE_CS;
}

void aprs_compressed_cs_from_altitude(unsigned alt_feet, char out[3]) {
    // Altitude is decoded as 1.002^(c * 91 + s) feet, so the combined value of
    // the two bytes is the inverse of that relation. The form starts at 1 foot
    // (cs = 0): anything below that floor, including 0, encodes as the floor
    // rather than as a negative value, and anything past the top of the table
    // saturates at APRS_COMPRESSED_ALT_CS_MAX. The logarithm is taken in
    // double precision because the 0.2 % step divides the result by a very
    // small constant, which magnifies any error in the ratio.
    long cs;
    if (alt_feet <= 1) {
        cs = 0;
    } else {
        cs = lround(log((double)alt_feet) / log(1.002));
        if (cs < 0)
            cs = 0;
        if (cs > APRS_COMPRESSED_ALT_CS_MAX)
            cs = APRS_COMPRESSED_ALT_CS_MAX;
    }

    // Both bytes carry a full base-91 digit here: the altitude form is
    // selected by the type byte, so the value the course/speed form reserves
    // for the radio range marker is an ordinary digit in this one.
    out[0] = (char)(APRS_COMPRESSED_BASE91_OFFSET + cs / 91);
    out[1] = (char)(APRS_COMPRESSED_BASE91_OFFSET + cs % 91);
    out[2] = APRS_COMPRESSED_T_BYTE_ALTITUDE;
}

bool aprs_symbol_table_byte_uncompressed(char c) {
    return c == '/' || c == '\\' || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

bool aprs_symbol_table_byte_compressed(char c) {
    return c == '/' || c == '\\' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'j');
}

char aprs_symbol_table_from_compressed(char c) {
    if (c >= 'a' && c <= 'j')
        return (char)('0' + (c - 'a'));
    return c;
}

// Reads the symbol table byte and symbol code out of a position field that
// starts at offset `posStart` within info, handling both position layouts
// defined by APRS101 chapter 9: the base-91 compressed field (symbol table
// byte immediately followed by 4 compressed-latitude digits, 4
// compressed-longitude digits, then the symbol code) and the uncompressed
// field (8-byte latitude, symbol table byte, 9-byte longitude, symbol code).
// The two are told apart by the byte at posStart, which is the same rule the
// receive-side decoder in main/aprs_filter.c applies, from these same
// predicates: an uncompressed latitude always starts with a decimal digit,
// and a compressed field always starts with its own symbol table byte, which
// chapter 21 keeps clear of the digits by spelling a numeric overlay 'a'-'j'
// there. So any non-digit opens a compressed field, overlays included, and
// the overlay is translated to the digit the uncompressed layout carries for
// the same symbol, so one station reads the same whichever layout it uses.
static bool aprsExtractPositionSymbol(const char *info, size_t infoLen, size_t posStart, char *symTable, char *symCode) {
    if (posStart >= infoLen)
        return false;

    char first = info[posStart];
    if (!isdigit((unsigned char)first)) {
        if (!aprs_symbol_table_byte_compressed(first))
            return false;
        size_t symCodePos = posStart + 9; // symtable[1] + lat[4] + lon[4]
        if (symCodePos >= infoLen)
            return false;
        *symTable = aprs_symbol_table_from_compressed(first);
        *symCode = info[symCodePos];
        return true;
    }

    size_t minLen = posStart + 19; // lat[8] + symtable[1] + lon[9] + symcode[1]
    if (infoLen < minLen)
        return false;

    *symTable = info[posStart + 8];
    *symCode = info[posStart + 18];
    return true;
}

bool aprs_extract_symbol(const char *info, size_t infoLen, char *symTable, char *symCode) {
    if (!info || !symTable || !symCode || infoLen == 0)
        return false;

    char dti = info[0];

    // Position reports start with one of !=/@; '/' and '@' additionally
    // carry a fixed 7-byte DHM/HMS timestamp between the DTI and the
    // position field, shifting the position's start offset by 7 bytes
    // relative to the no-timestamp forms. See APRS101 chapters 5, 8 and 9.
    if (dti == '!' || dti == '=' || dti == '/' || dti == '@') {
        bool hasTimestamp = (dti == '/' || dti == '@');
        size_t posStart = hasTimestamp ? 8 : 1;
        return aprsExtractPositionSymbol(info, infoLen, posStart, symTable, symCode);
    }

    // Object report (APRS101 chapter 11): ';' + 9-byte name + 1-byte
    // live('*')/killed('_') flag + 7-byte DHM timestamp + position field.
    // The name and flag are fixed width, so the position always starts at a
    // fixed offset regardless of the object's actual name.
    if (dti == ';') {
        size_t posStart = 1 + 9 + 1 + 7;
        return aprsExtractPositionSymbol(info, infoLen, posStart, symTable, symCode);
    }

    // Item report (APRS101 chapter 11): ')' + a 3-9 byte name + 1-byte
    // live('!')/killed('_') flag + position field. The name has no fixed
    // width, so its end is found by scanning for the flag byte instead.
    if (dti == ')') {
        for (size_t nameLen = 3; nameLen <= 9; nameLen++) {
            size_t flagPos = 1 + nameLen;
            if (flagPos >= infoLen)
                return false;
            char flag = info[flagPos];
            if (flag == '!' || flag == '_')
                return aprsExtractPositionSymbol(info, infoLen, flagPos + 1, symTable, symCode);
        }
        return false;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Raw NMEA-0183 position sentences (APRS101 chapter 6, "Raw NMEA Position
// Reports", and Appendix 1). A tracker that has no room to reformat its GPS
// receiver's output puts the sentence on the air verbatim behind the '$' data
// type identifier, so the sentence identifier is the first field and the '$'
// is both the DTI and the NMEA start delimiter.
//
// The whole decode is integer arithmetic. Coordinates arrive as
// "DDMM.mmmm"/"DDDMM.mmmm" and are accumulated in units of 1e-7 degrees, so
// the value handed back to the caller is converted to float exactly once, at
// the end. That keeps the parse independent of any floating-point rounding on
// the way in, which matters because this feeds the IGate range gate.
// ---------------------------------------------------------------------------

// Latitude and longitude are carried in units of 1e-7 degrees while being
// assembled, which resolves about 1 cm and leaves the widest intermediate
// (minutes scaled by 1e-6) well inside a signed 32-bit range.
#define NMEA_DEG_SCALE 10000000

// Number of fraction digits kept from the minutes field. NMEA receivers emit
// between one and five; anything beyond the fifth is below the resolution of
// any GPS receiver and is discarded.
#define NMEA_MIN_FRAC_DIGITS 5

// Value of one whole minute once the minutes field has been scaled by
// 10^NMEA_MIN_FRAC_DIGITS.
#define NMEA_MIN_SCALE 100000

// Length of an NMEA sentence identifier without its '$': two talker
// characters plus a three-character sentence type ("GPRMC").
#define NMEA_ID_LEN 5

// Convert one hexadecimal digit to its value, or -1 if it is not one.
static int nmeaHexDigit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

// Establish how much of `sentence` is sentence body and, when the optional
// "*HH" trailer is present, verify it. The NMEA checksum is the XOR of every
// byte between the '$' and the '*', printed as two hexadecimal digits. A
// sentence without the trailer is accepted (older receivers and several APRS
// trackers omit it); a sentence whose trailer is present but malformed or
// wrong is rejected outright, since a corrupted position is worse than no
// position at all for the range gate this feeds.
//
// On success *bodyLen receives the number of bytes of field data that follow
// the '$', excluding the trailer and any trailing CR/LF.
static bool nmeaChecksumOk(const char *sentence, size_t len, size_t *bodyLen) {
    size_t end = 0;
    while (end < len && sentence[end] != '\r' && sentence[end] != '\n')
        end++;

    size_t star = 1;
    while (star < end && sentence[star] != '*')
        star++;

    if (star >= end) {
        *bodyLen = end - 1;
        return true;
    }

    if (star + 2 >= end)
        return false;

    int hi = nmeaHexDigit(sentence[star + 1]);
    int lo = nmeaHexDigit(sentence[star + 2]);
    if (hi < 0 || lo < 0)
        return false;

    unsigned char sum = 0;
    for (size_t i = 1; i < star; i++)
        sum ^= (unsigned char)sentence[i];

    if (sum != (unsigned char)((hi << 4) | lo))
        return false;

    *bodyLen = star - 1;
    return true;
}

// Locate one comma-separated field of an NMEA sentence body. Field 0 is the
// sentence identifier itself ("GPRMC"), field 1 the one after the first
// comma, and so on. Empty fields are legal in NMEA and are reported as a
// zero length rather than as an error, because whether an empty field is
// acceptable depends on which field it is.
static bool nmeaField(const char *body, size_t bodyLen, int index, const char **out, size_t *outLen) {
    size_t start = 0;
    int field = 0;

    while (field < index) {
        while (start < bodyLen && body[start] != ',')
            start++;
        if (start >= bodyLen)
            return false;
        start++;
        field++;
    }

    size_t end = start;
    while (end < bodyLen && body[end] != ',')
        end++;

    *out = &body[start];
    *outLen = end - start;
    return true;
}

// Parse an NMEA "DDMM.mmmm" latitude or "DDDMM.mmmm" longitude field into
// units of 1e-7 degrees. `degDigits` is 2 for a latitude and 3 for a
// longitude; the two whole-minute digits that follow are always present, and
// the fractional part is optional. Digits are accumulated one at a time, so
// the field never passes through a floating-point conversion.
static bool nmeaDegrees(const char *field, size_t len, int degDigits, int32_t *out) {
    size_t need = (size_t)degDigits + 2;
    if (len < need)
        return false;

    int32_t deg = 0;
    for (int i = 0; i < degDigits; i++) {
        if (field[i] < '0' || field[i] > '9')
            return false;
        deg = deg * 10 + (field[i] - '0');
    }

    int32_t minutes = 0;
    for (size_t i = (size_t)degDigits; i < need; i++) {
        if (field[i] < '0' || field[i] > '9')
            return false;
        minutes = minutes * 10 + (field[i] - '0');
    }
    if (minutes >= 60)
        return false;
    minutes *= NMEA_MIN_SCALE;

    size_t i = need;
    if (i < len) {
        if (field[i] != '.')
            return false;
        i++;
        int32_t place = NMEA_MIN_SCALE / 10;
        int kept = 0;
        for (; i < len; i++) {
            if (field[i] < '0' || field[i] > '9')
                return false;
            if (kept < NMEA_MIN_FRAC_DIGITS) {
                minutes += (int32_t)(field[i] - '0') * place;
                place /= 10;
                kept++;
            }
        }
        if (kept == 0)
            return false;
    }

    // One minute is 1/60 degree, so the scaled minutes are multiplied by
    // NMEA_DEG_SCALE and divided by 60 * NMEA_MIN_SCALE, which reduces to
    // times ten over six. The extra half divisor rounds to nearest instead of
    // truncating, keeping the error below the 1e-7 degree quantum.
    *out = deg * NMEA_DEG_SCALE + (minutes * 10 + 3) / 6;
    return true;
}

// Apply an NMEA hemisphere field ('N'/'S' for a latitude, 'E'/'W' for a
// longitude) to an already parsed magnitude and range-check the result.
static bool nmeaHemisphere(const char *field, size_t len, bool isLatitude, int32_t *value) {
    if (len != 1)
        return false;

    char h = field[0];
    if (h >= 'a' && h <= 'z')
        h -= 32;

    int32_t limit = (isLatitude ? 90 : 180) * NMEA_DEG_SCALE;
    if (*value > limit)
        return false;

    if (isLatitude) {
        if (h == 'S')
            *value = -*value;
        else if (h != 'N')
            return false;
    } else {
        if (h == 'W')
            *value = -*value;
        else if (h != 'E')
            return false;
    }
    return true;
}

// Read the latitude/hemisphere and longitude/hemisphere pair that every
// supported sentence carries, given the index of the latitude field. The
// three remaining fields always follow it directly.
static bool nmeaLatLon(const char *body, size_t bodyLen, int latIndex, int32_t *lat, int32_t *lon) {
    const char *f;
    size_t flen;

    if (!nmeaField(body, bodyLen, latIndex, &f, &flen) || !nmeaDegrees(f, flen, 2, lat))
        return false;
    if (!nmeaField(body, bodyLen, latIndex + 1, &f, &flen) || !nmeaHemisphere(f, flen, true, lat))
        return false;
    if (!nmeaField(body, bodyLen, latIndex + 2, &f, &flen) || !nmeaDegrees(f, flen, 3, lon))
        return false;
    if (!nmeaField(body, bodyLen, latIndex + 3, &f, &flen) || !nmeaHemisphere(f, flen, false, lon))
        return false;

    return true;
}

bool aprs_nmea_decode_position(const char *sentence, size_t len, float *outLat, float *outLon) {
    if (!sentence || !outLat || !outLon || len < 1 + NMEA_ID_LEN || sentence[0] != '$')
        return false;

    size_t bodyLen;
    if (!nmeaChecksumOk(sentence, len, &bodyLen))
        return false;

    const char *body = &sentence[1];

    const char *id;
    size_t idLen;
    if (!nmeaField(body, bodyLen, 0, &id, &idLen) || idLen != NMEA_ID_LEN)
        return false;

    // Any two-letter talker is accepted. APRS 1.2c dropped the assumption
    // that the receiver is GPS-only, and multi-constellation receivers emit
    // GN (combined fix), GL (GLONASS), GA (Galileo) and GB (BeiDou) where a
    // 1990s receiver emitted GP.
    for (int i = 0; i < 2; i++)
        if (id[i] < 'A' || id[i] > 'Z')
            return false;

    const char *type = &id[2];
    const char *f;
    size_t flen;
    int32_t lat, lon;

    if (!memcmp(type, "RMC", 3)) {
        // Recommended minimum: field 2 is the data validity flag, 'A' for a
        // valid fix and 'V' for a navigation warning. A warning means the
        // coordinates are stale or meaningless, so the sentence is refused.
        if (!nmeaField(body, bodyLen, 2, &f, &flen) || flen != 1 || (f[0] != 'A' && f[0] != 'a'))
            return false;
        if (!nmeaLatLon(body, bodyLen, 3, &lat, &lon))
            return false;
    } else if (!memcmp(type, "GGA", 3)) {
        // Fix data: field 6 is the fix quality, where 0 means "fix not
        // available" and every other value is some kind of usable fix
        // (autonomous, differential, RTK, estimated).
        if (!nmeaField(body, bodyLen, 6, &f, &flen) || flen < 1)
            return false;
        {
            bool zero = true;
            for (size_t i = 0; i < flen; i++) {
                if (f[i] < '0' || f[i] > '9')
                    return false;
                if (f[i] != '0')
                    zero = false;
            }
            if (zero)
                return false;
        }
        if (!nmeaLatLon(body, bodyLen, 2, &lat, &lon))
            return false;
    } else if (!memcmp(type, "GLL", 3)) {
        // Geographic position: the coordinates lead, and the status flag in
        // field 6 was added later, so it is only checked when present.
        if (!nmeaLatLon(body, bodyLen, 1, &lat, &lon))
            return false;
        if (nmeaField(body, bodyLen, 6, &f, &flen) && flen == 1 && (f[0] == 'V' || f[0] == 'v'))
            return false;
    } else {
        return false;
    }

    *outLat = (float)lat / (float)NMEA_DEG_SCALE;
    *outLon = (float)lon / (float)NMEA_DEG_SCALE;
    return true;
}

// ---------------------------------------------------------------------------
// Symbols carried outside the information field (APRS101 chapters 20 and 21).
//
// A station that cannot put a symbol in its information field - a tracker
// beaconing raw NMEA behind the '$' DTI is the classic case - encodes it in
// the AX.25 destination address instead, as "GPSxyz", "SPCxyz" or "SYMxyz".
// The three prefixes are interchangeable: they were meant to separate general
// trackers, special-event trackers and TNC-only stations, but they select the
// same symbol.
//
// The "xy" pair indexes the APRS symbol tables. Both tables are laid out as
// seven runs, each with its own group letter, and within a run the second
// character advances with the symbol code, so the pair maps onto a symbol
// code arithmetically rather than through a 188-entry lookup table:
//
//   symbol code    primary group   alternate group   second character
//   '!'..'/'       B               O                 'B'..'P'
//   '0'..'9'       P               A                 '0'..'9'
//   ':'..'@'       M               N                 'R'..'X'
//   'A'..'Z'       P               A                 'A'..'Z'
//   '['..'`'       H               D                 'S'..'X'
//   'a'..'z'       L               S                 'A'..'Z'
//   '{'..'~'       J               Q                 '1'..'4'
//
// The optional "z" is an overlay character, which the spec allows only over
// an alternate-table symbol, and only as a digit or an upper-case letter -
// the AX.25 address field cannot carry anything else.
//
// The numeric forms "GPSCnn" and "GPSEnn" address the primary and alternate
// tables directly by symbol index 1..94, for symbols whose group letters are
// awkward to remember.
// ---------------------------------------------------------------------------

// Lowest and highest symbol index addressable by the "GPSCnn"/"GPSEnn" forms
// and by an "xy" pair; index n selects symbol code ' ' + n.
#define APRS_DEST_SYM_MIN 1
#define APRS_DEST_SYM_MAX 94

// Symbol code that each source-address SSID stands for in the pre-1997
// convention, all on the primary table. SSID 0 has no symbol.
static const char DEST_SSID_SYMBOL[16] = {
    0,    // 0  - no symbol
    'a',  // 1  - ambulance
    'U',  // 2  - bus
    'f',  // 3  - fire truck
    'b',  // 4  - bicycle
    'Y',  // 5  - yacht
    'X',  // 6  - helicopter
    '\'', // 7 - small aircraft
    's',  // 8  - ship
    '>',  // 9  - car
    '<',  // 10 - motorcycle
    'O',  // 11 - balloon
    'j',  // 12 - jeep
    'R',  // 13 - recreational vehicle
    'k',  // 14 - truck
    'v'   // 15 - van
};

// Resolve one "xy" group/selector pair against one of the two symbol tables,
// yielding the symbol code it names. Returns false when the pair names no
// symbol in that table, which is how the caller learns to try the other one.
static bool destPairToCode(char x, char y, bool alternate, char *code) {
    char gPunct = alternate ? 'O' : 'B';
    char gAlnum = alternate ? 'A' : 'P';
    char gColon = alternate ? 'N' : 'M';
    char gBracket = alternate ? 'D' : 'H';
    char gLower = alternate ? 'S' : 'L';
    char gBrace = alternate ? 'Q' : 'J';

    int nn = -1;

    if (x == gPunct && y >= 'B' && y <= 'P')
        nn = y - 'A';
    else if (x == gAlnum && y >= '0' && y <= '9')
        nn = 16 + (y - '0');
    else if (x == gAlnum && y >= 'A' && y <= 'Z')
        nn = 33 + (y - 'A');
    else if (x == gColon && y >= 'R' && y <= 'X')
        nn = 26 + (y - 'R');
    else if (x == gBracket && y >= 'S' && y <= 'X')
        nn = 59 + (y - 'S');
    else if (x == gLower && y >= 'A' && y <= 'Z')
        nn = 65 + (y - 'A');
    else if (x == gBrace && y >= '1' && y <= '4')
        nn = 91 + (y - '1');

    if (nn < APRS_DEST_SYM_MIN || nn > APRS_DEST_SYM_MAX)
        return false;

    *code = (char)(' ' + nn);
    return true;
}

// Parse the two decimal digits of a "GPSCnn"/"GPSEnn" destination into a
// symbol index. Exactly two digits are required: a shorter or non-numeric
// tail is one of the "xy" forms instead, which the caller tries next.
static bool destIndexDigits(const char *nn, char *code) {
    if (nn[0] < '0' || nn[0] > '9' || nn[1] < '0' || nn[1] > '9')
        return false;

    int value = (nn[0] - '0') * 10 + (nn[1] - '0');
    if (value < APRS_DEST_SYM_MIN || value > APRS_DEST_SYM_MAX)
        return false;

    *code = (char)(' ' + value);
    return true;
}

bool aprs_symbol_from_dest(char dti, const char *destCall, int srcSsid, char *symTable, char *symCode) {
    if (!destCall || !symTable || !symCode)
        return false;

    // Mic-E packs latitude, message code and ambiguity into the destination
    // address, so its bytes are position data and must never be read as a
    // symbol. The Mic-E symbol lives in the information field, where the
    // caller has already looked for it.
    bool micE = (dti == '`' || dti == '\'' || dti == 0x1c || dti == 0x1d);

    if (!micE) {
        // The SSID of the destination address carries no symbol information,
        // so only the callsign part is examined.
        char dest[7] = { 0 };
        size_t n = 0;
        while (n < sizeof(dest) - 1 && destCall[n] && destCall[n] != '-') {
            dest[n] = destCall[n];
            n++;
        }

        bool prefixed = (n >= 5 && (!memcmp(dest, "GPS", 3) || !memcmp(dest, "SPC", 3) || !memcmp(dest, "SYM", 3)));

        if (prefixed) {
            // Numeric forms first: they are only defined behind "GPS", and a
            // 'C' or 'E' group letter exists in neither symbol table, so
            // testing them ahead of the "xy" forms cannot shadow one.
            if (n == 6 && !memcmp(dest, "GPS", 3) && (dest[3] == 'C' || dest[3] == 'E') && destIndexDigits(&dest[4], symCode)) {
                *symTable = (dest[3] == 'C') ? '/' : '\\';
                return true;
            }

            if (destPairToCode(dest[3], dest[4], false, symCode)) {
                *symTable = '/';
                return true;
            }

            if (destPairToCode(dest[3], dest[4], true, symCode)) {
                // An overlay replaces the table byte on the air, which is
                // what makes the alternate table's overlayable symbols
                // distinguishable. Anything that is not a digit or an
                // upper-case letter is not a valid overlay and leaves the
                // plain alternate table in place.
                char z = (n >= 6) ? dest[5] : 0;
                *symTable = ((z >= '0' && z <= '9') || (z >= 'A' && z <= 'Z')) ? z : '\\';
                return true;
            }
        }
    }

    // Last resort, and deliberately narrow: before the destination-address
    // convention existed, a raw NMEA tracker could only pick among fifteen
    // symbols by choosing its source SSID. Restricting it to the '$' DTI is
    // what keeps an ordinary station's "-9" from turning its messages and
    // status reports into cars.
    if (dti == '$' && srcSsid >= 1 && srcSsid <= 15 && DEST_SSID_SYMBOL[srcSsid] != 0) {
        *symTable = '/';
        *symCode = DEST_SSID_SYMBOL[srcSsid];
        return true;
    }

    return false;
}

bool aprs_symbol_from_tnc2_header(const char *line, char dti, char *symTable, char *symCode) {
    if (!line)
        return false;

    // "SRC-N>DEST-N,PATH,...:info" - the source SSID is whatever digits sit
    // between the last '-' of the source callsign and the '>', and the
    // destination runs from the '>' to the next ',' or ':'.
    const char *gt = strchr(line, '>');
    if (!gt)
        return false;

    int srcSsid = 0;
    for (const char *p = line; p < gt; p++) {
        if (*p == '-') {
            srcSsid = 0;
            for (const char *d = p + 1; d < gt; d++) {
                if (*d < '0' || *d > '9') {
                    srcSsid = 0;
                    break;
                }
                srcSsid = srcSsid * 10 + (*d - '0');
            }
            break;
        }
    }

    char dest[10] = { 0 };
    if (!aprs_tnc2_dest_call(line, dest, sizeof(dest)))
        return false;

    return aprs_symbol_from_dest(dti, dest, srcSsid, symTable, symCode);
}

bool aprs_tnc2_dest_call(const char *line, char *out, size_t out_max) {
    if (line == NULL || out == NULL || out_max == 0)
        return false;

    const char *gt = strchr(line, '>');
    if (gt == NULL)
        return false;

    size_t n = 0;
    for (const char *p = gt + 1; *p && *p != ',' && *p != ':'; p++) {
        if (n + 1 >= out_max)
            return false;
        out[n++] = *p;
    }

    out[n] = 0;
    return n > 0;
}
