// @file mice.c
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
// Decodes and encodes the Mic-E position report format (APRS101 Chapter 10):
// the AX.25 destination address field encodes latitude, the 3-bit message
// code and the N/S, longitude-offset and W/E flag bits; the AX.25
// information field completes the report with longitude, course, speed and
// symbol, followed by the TYPE byte, an optional altitude field, an optional
// free text and the Manufacturer/Version pair that identifies the sending
// device (aprs12/mic-e-types.txt, aprs12/mic-e-examples.txt).

#include <ctype.h>
#include <math.h>
#include <string.h>

#include "weather_telemetry.h"

// One decoded byte of a Mic-E destination address field (bytes 1-6 only;
// byte 7, the SSID, is not part of this alphabet). Every one of the six
// bytes uses the same character set and the same digit/flag-bit split;
// which of the three things a byte's flag bit actually means (a message
// bit, N/S, longitude offset or W/E) depends only on the byte's position,
// decided by the caller.
typedef struct {
    int digit;      // 0-9, or -1 if this position is blanked (position ambiguity).
    bool flag_bit;  // The flag bit carried by this byte: 0 for a plain digit or 'L', 1 for a letter.
    bool is_custom; // true if the letter used comes from the Custom alphabet (A-K); only meaningful when flag_bit is true.
} mice_dest_byte_t;

// APRS101 Ch.10 "Destination Address Field Encoding": '0'-'9' are plain
// digits (flag bit 0); 'A'-'J' plus 'K' (space) are the Custom-message
// alphabet (flag bit 1); 'L' is the universal "space, flag bit 0" marker;
// 'P'-'Y' plus 'Z' (space) are the Standard-message alphabet (flag bit 1).
// 'A'-'K' never appear in bytes 4-6, which only ever carry a plain binary
// flag (N/S, longitude offset, W/E), not a message code.
static bool mice_decode_dest_byte(char c, mice_dest_byte_t *out) {
    if (c >= '0' && c <= '9') {
        out->digit = c - '0';
        out->flag_bit = false;
        out->is_custom = false;
        return true;
    }
    if (c >= 'A' && c <= 'J') {
        out->digit = c - 'A';
        out->flag_bit = true;
        out->is_custom = true;
        return true;
    }
    if (c == 'K') {
        out->digit = -1;
        out->flag_bit = true;
        out->is_custom = true;
        return true;
    }
    if (c == 'L') {
        out->digit = -1;
        out->flag_bit = false;
        out->is_custom = false;
        return true;
    }
    if (c >= 'P' && c <= 'Y') {
        out->digit = c - 'P';
        out->flag_bit = true;
        out->is_custom = false;
        return true;
    }
    if (c == 'Z') {
        out->digit = -1;
        out->flag_bit = true;
        out->is_custom = false;
        return true;
    }
    return false;
}

// APRS101 Ch.10 "Mic-E Messages": the 3-bit A/B/C identifier, packed here
// as (A<<2)|(B<<1)|C, maps to a message value in the reverse order of its
// binary value - idx 7 is M0/C0, idx 0 is always Emergency regardless of
// which alphabet produced the (all-zero) bits.
static aprs_mice_message_code_t mice_message_from_bits(bool bitA, bool bitB, bool bitC, bool customA, bool customB, bool customC, bool *out_is_custom) {
    int idx = (bitA ? 4 : 0) | (bitB ? 2 : 0) | (bitC ? 1 : 0);

    if (idx == 0) {
        *out_is_custom = false;
        return APRS_MICE_MSG_EMERGENCY;
    }

    bool any_std = (bitA && !customA) || (bitB && !customB) || (bitC && !customC);
    bool any_custom = (bitA && customA) || (bitB && customB) || (bitC && customC);

    if (any_std && any_custom) {
        *out_is_custom = false;
        return APRS_MICE_MSG_UNKNOWN;
    }

    *out_is_custom = any_custom;
    return (aprs_mice_message_code_t)(7 - idx);
}

// Mic-E TYPE byte (aprs12/mic-e-types.txt): the byte that follows the symbol
// table byte, telling receivers which family of equipment sent the report and
// whether it can accept messages. '`' marks a message-capable unit and '\''
// a one-way tracker; ' ', '>' and ']' are the legacy identifiers of the
// original Mic-E, the Kenwood D7 family and the Kenwood D700 family. A report
// whose text starts with any other byte carries no TYPE byte at all, and the
// text begins immediately.
static bool mice_is_type_byte(char c) {
    return c == '`' || c == '\'' || c == ' ' || c == '>' || c == ']';
}

// The three TYPE bytes that mark a station able to receive APRS messages
// (aprs12/mic-e-types.txt): the message-capable '`' and the two Kenwood
// families, both of which are full messaging radios. The one-way-tracker
// '\'' and the original Mic-E ' ' are not.
static bool mice_type_is_msg_capable(char c) {
    return c == '`' || c == '>' || c == ']';
}

// Altitude field of the Mic-E text (APRS101 Ch.10, aprs12/mic-e-examples.txt):
// exactly 4 bytes, 3 base-91 digits ('!'..'{') followed by a literal '}',
// encoding altitude in meters as
// (c0-33)*91*91 + (c1-33)*91 + (c2-33) - 10000. `text` must have at least 4
// readable bytes.
//
// The on-air unit is the meter, so the value handed back is the whole foot
// nearest the transmitted altitude - the same rounding mice_encode_altitude()
// applies in the other direction. One meter is 3.28 ft, so a value that made
// the round trip through this format lands within 2 ft of where it started,
// above or below it with equal likelihood.
static bool mice_parse_altitude_ft(const char *text, int32_t *out_ft) {
    if (text[3] != '}')
        return false;

    for (int i = 0; i < 3; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c < '!' || c > '{')
            return false;
    }

    long meters = (text[0] - 33) * 91L * 91L + (text[1] - 33) * 91L + (text[2] - 33) - 10000L;
    *out_ft = (int32_t)lround((double)meters * 3.28084);
    return true;
}

bool aprs_mice_decode(const char *dst_call, const char *info, size_t info_len, aprs_mice_report_t *out) {
    if (dst_call == NULL || info == NULL || out == NULL)
        return false;

    if (strlen(dst_call) < 6)
        return false;

    if (info_len < 9)
        return false;

    switch ((unsigned char)info[0]) {
        case '`':
        case '\'':
        case 0x1c:
        case 0x1d:
            break;
        default:
            return false;
    }

    mice_dest_byte_t b[6];
    for (int i = 0; i < 6; i++) {
        if (!mice_decode_dest_byte(dst_call[i], &b[i]))
            return false;
    }

    memset(out, 0, sizeof(*out));

    // Latitude: degrees (bytes 1-2), minutes (bytes 3-4), hundredths of a
    // minute (bytes 5-6). A blanked (ambiguous) digit contributes 0 to the
    // numeric value, same convention the rest of this codebase uses for
    // position ambiguity.
    int lat_deg = (b[0].digit < 0 ? 0 : b[0].digit) * 10 + (b[1].digit < 0 ? 0 : b[1].digit);
    int lat_min_tens = (b[2].digit < 0 ? 0 : b[2].digit);
    int lat_min_units = (b[3].digit < 0 ? 0 : b[3].digit);
    int lat_hun_tens = (b[4].digit < 0 ? 0 : b[4].digit);
    int lat_hun_units = (b[5].digit < 0 ? 0 : b[5].digit);
    double lat_min = lat_min_tens * 10 + lat_min_units + (lat_hun_tens * 10 + lat_hun_units) / 100.0;

    // Position ambiguity (APRS101 Ch.10 "Mic-E Position Ambiguity"): counted
    // as contiguous blanked digits from the hundredths-of-a-minute byte
    // (byte 6) backward; degrees (bytes 1-2) are never blanked, mirroring
    // the plain-text APRS ambiguity rule of Chapter 6.
    aprs_position_ambiguity_t ambiguity = APRS_AMBIGUITY_NONE;
    if (b[5].digit < 0) {
        ambiguity = APRS_AMBIGUITY_TENTH_MINUTE;
        if (b[4].digit < 0) {
            ambiguity = APRS_AMBIGUITY_MINUTE;
            if (b[3].digit < 0) {
                ambiguity = APRS_AMBIGUITY_TEN_MINUTES;
                if (b[2].digit < 0)
                    ambiguity = APRS_AMBIGUITY_DEGREE;
            }
        }
    }

    bool north = b[3].flag_bit;           // Byte 4: N/S indicator.
    bool long_offset_100 = b[4].flag_bit; // Byte 5: longitude offset (+0 or +100 degrees).
    bool west = b[5].flag_bit;            // Byte 6: W/E indicator.

    double lat = lat_deg + lat_min / 60.0;
    if (!north)
        lat = -lat;

    out->position.latitude_deg = lat;
    out->position.ambiguity = ambiguity;
    out->position.is_null_position = (lat_deg == 0 && lat_min == 0.0);

    out->message_code =
        mice_message_from_bits(b[0].flag_bit, b[1].flag_bit, b[2].flag_bit, b[0].is_custom, b[1].is_custom, b[2].is_custom, &out->is_custom_message);

    // Longitude degrees/minutes/hundredths (APRS101 Ch.10 "Longitude Degrees/
    // Minutes/Hundredths of Minutes Encoding"): each byte is offset by 28.
    int d = (unsigned char)info[1] - 28;
    if (long_offset_100)
        d += 100;
    if (d >= 180 && d <= 189)
        d -= 80;
    else if (d >= 190 && d <= 199)
        d -= 190;

    int m = (unsigned char)info[2] - 28;
    if (m >= 60)
        m -= 60;

    int h = (unsigned char)info[3] - 28;
    if (h < 0)
        h = 0;
    if (h > 99)
        h = 99;

    double lon = d + (m + h / 100.0) / 60.0;
    if (west)
        lon = -lon;
    out->position.longitude_deg = lon;

    // Course and speed (APRS101 Ch.10 "Decoding the Speed and Course").
    int sp = (unsigned char)info[4] - 28;
    int dc = (unsigned char)info[5] - 28;
    int se = (unsigned char)info[6] - 28;

    int speed = sp * 10 + dc / 10;
    int course = (dc % 10) * 100 + se;
    if (speed >= 800)
        speed -= 800;
    if (course >= 400)
        course -= 400;
    if (speed < 0)
        speed = 0;
    if (course < 0)
        course = 0;

    out->course_speed.speed_knots = (uint16_t)speed;
    out->course_speed.course_deg = (uint16_t)course;
    out->course_speed.is_unknown = (speed == 0 && course == 0);

    // Symbol: raw (unoffset) bytes 8-9 of the information field.
    out->position.symbol.code = info[7];
    out->position.symbol.table = (aprs_symbol_table_id_t)info[8];
    out->position.symbol.overlay = 0;

    // Everything past the fixed 9 bytes, peeled apart in the order
    // aprs12/mic-e-examples.txt lays it out: TYPE byte, altitude, free text,
    // Mv pair. What survives all four steps is the operator-visible status
    // text (APRS101 Ch.10 "Mic-E Status Text"), which may still embed a
    // frequency block, a Maidenhead locator ("AA99AA/x") or a trailing
    // "!DAO!" for the caller to inspect further.
    if (info_len > 9) {
        const char *tail = &info[9];
        size_t tail_len = info_len - 9;

        // TYPE byte, when the report carries one. This is the only statement
        // a Mic-E report makes about messaging capability, so it is read even
        // though the byte itself is not part of the text.
        if (mice_is_type_byte(tail[0])) {
            out->msg_capable = mice_type_is_msg_capable(tail[0]);
            tail++;
            tail_len--;
        }

        // Mv pair, taken only in the space-delimited form the spec
        // recommends. Two bytes at the end of a text that has no separator
        // are indistinguishable from the last two characters of the text
        // itself, and cutting them off unconditionally would eat a letter and
        // a half from every station that sends no Mv pair at all.
        if (tail_len >= 3 && tail[tail_len - 3] == ' ' && tail[tail_len - 2] != ' ' && tail[tail_len - 1] != ' ') {
            out->device_id[0] = tail[tail_len - 2];
            out->device_id[1] = tail[tail_len - 1];
            tail_len -= 3;
        }

        // Altitude field. mic-e-examples.txt puts it first, right after the
        // TYPE byte; encoders that place it at the very end instead are read
        // as well, so the text handed to the caller is free of it either way.
        int32_t alt_ft;
        if (tail_len >= 4 && mice_parse_altitude_ft(tail, &alt_ft)) {
            out->position.has_altitude = true;
            out->position.altitude_ft = alt_ft;
            tail += 4;
            tail_len -= 4;
        } else if (tail_len >= 4 && mice_parse_altitude_ft(tail + tail_len - 4, &alt_ft)) {
            out->position.has_altitude = true;
            out->position.altitude_ft = alt_ft;
            tail_len -= 4;
        }

        if (tail_len > 0) {
            size_t copy_len = tail_len < APRS_MAX_STATUS_TEXT_LEN ? tail_len : APRS_MAX_STATUS_TEXT_LEN;

            memcpy(out->status_text, tail, copy_len);
            out->status_text[copy_len] = 0;
            out->has_status_text = true;
        }
    }

    return true;
}

// Encodes one digit (0-9) or a blanked/space position (digit < 0) of a
// Mic-E destination address byte, using the Standard alphabet ('P'-'Y',
// 'Z' for space) when flag_bit is set, or the plain digit / 'L' (space,
// flag bit 0) otherwise. This is the exact inverse of
// mice_decode_dest_byte() restricted to the Standard alphabet, which is
// what every current Mic-E encoder (Kenwood D7/D700/D710, Yaesu VX-8/
// FTM-350/400D) uses for its own beacons; the Custom alphabet only ever
// appears on receive, decoded for compatibility, and is never generated
// here.
static char mice_encode_dest_byte(int digit, bool flag_bit) {
    if (digit < 0)
        return flag_bit ? 'Z' : 'L';
    return flag_bit ? (char)('P' + digit) : (char)('0' + digit);
}

// APRS101 Ch.10 "Mic-E Messages": inverse of mice_message_from_bits() for
// the Standard alphabet. idx = 7 - message_code for the six Standard/
// Custom codes (0-6); Emergency (7) always encodes as idx = 0 (all three
// bits clear). aprs_mice_message_code_t's APRS_MICE_MSG_UNKNOWN has no
// on-air representation of its own, so a caller passing it gets Off Duty
// (idx = 7, the all-flag-bits-set pattern) instead of emitting an
// undefined bit combination.
static void mice_message_to_bits(aprs_mice_message_code_t code, bool *out_bitA, bool *out_bitB, bool *out_bitC) {
    int idx;
    if (code == APRS_MICE_MSG_EMERGENCY)
        idx = 0;
    else if (code >= APRS_MICE_MSG_OFF_DUTY && code <= APRS_MICE_MSG_PRIORITY)
        idx = 7 - (int)code;
    else
        idx = 7; // APRS_MICE_MSG_UNKNOWN or any out-of-range value: fall back to Off Duty.

    *out_bitA = (idx & 4) != 0;
    *out_bitB = (idx & 2) != 0;
    *out_bitC = (idx & 1) != 0;
}

// Appends up to `len` bytes of `text` to the NUL-terminated string in `out`,
// writing only what fits in `outMax` bytes including the terminator, and
// returns the number of bytes actually appended. Every optional block of the
// Mic-E information field goes through here, so a buffer that runs out simply
// stops growing instead of overrunning.
static size_t mice_append(char *out, size_t outMax, const char *text, size_t len) {
    size_t used = strlen(out);
    size_t room = (outMax > used + 1) ? outMax - used - 1 : 0;
    if (len > room)
        len = room;

    memcpy(out + used, text, len);
    out[used + len] = 0;
    return len;
}

// Altitude field of the Mic-E text (APRS101 Ch.10): inverse of
// mice_parse_altitude_ft(). Appends 3 base-91 digits plus a literal '}' to
// the caller's information-field buffer, encoding altitude in meters as
// (c0*91*91 + c1*91 + c2) + 10000, each digit offset by '!' (33). alt_ft is
// clamped to the representable range so an out-of-range altitude never
// overflows the 3-digit field. The field is written before the free text,
// which is what lets a receiver tell the two apart.
static void mice_encode_altitude(int32_t alt_ft, char *out, size_t outMax) {
    double meters = (double)alt_ft / 3.28084;
    long val = (long)lround(meters) + 10000L;
    if (val < 0)
        val = 0;
    const long maxVal = 91L * 91L * 91L - 1L;
    if (val > maxVal)
        val = maxVal;

    char digits[4];
    digits[0] = (char)(33 + (val / (91 * 91)) % 91);
    digits[1] = (char)(33 + (val / 91) % 91);
    digits[2] = (char)(33 + val % 91);
    digits[3] = '}';

    size_t len = strlen(out);
    if (len + 4 < outMax)
        mice_append(out, outMax, digits, 4);
}

bool aprs_mice_encode(const aprs_mice_report_t *report, char *dst_call_out, char *info_out, size_t info_out_max) {
    if (report == NULL || dst_call_out == NULL || info_out == NULL)
        return false;

    // Fixed 9-byte report plus the TYPE byte plus the NUL terminator; every
    // field past that is optional and simply omitted when it does not fit.
    if (info_out_max < 11)
        return false;

    double lat = report->position.latitude_deg;
    if (lat < -90.0 || lat > 90.0)
        return false;
    double lon = report->position.longitude_deg;
    if (lon < -180.0 || lon > 180.0)
        return false;
    // Exactly +/-180 degrees (the antimeridian) has no valid destination-
    // address byte under the Mic-E longitude encoding (APRS101 Ch.10): the
    // 180-189 and 190-199 correction ranges only ever map back to 0-179
    // degrees. Rejected explicitly rather than silently emitting a byte
    // that decodes to a different longitude.
    if (lon == 180.0 || lon == -180.0)
        return false;

    bool north = lat >= 0.0;
    double lat_abs = north ? lat : -lat;
    int lat_deg = (int)lat_abs;
    double lat_min = (lat_abs - lat_deg) * 60.0;

    // Round to hundredths of a minute first, same resolution as the on-air
    // field, then split into the four destination-address digit pairs
    // (minutes tens/units, hundredths tens/units) decoded by
    // aprs_mice_decode(). A minutes value that rounds up to 60.00 carries
    // into the next whole degree, keeping the encoded position valid.
    int lat_hun_total = (int)lround(lat_min * 100.0);
    if (lat_hun_total >= 6000) {
        lat_hun_total -= 6000;
        lat_deg += 1;
    }
    if (lat_deg > 90)
        lat_deg = 90;

    int lat_min_int = lat_hun_total / 100;
    int lat_hun = lat_hun_total % 100;

    int dig[6];
    dig[0] = (lat_deg / 10) % 10;
    dig[1] = lat_deg % 10;
    dig[2] = (lat_min_int / 10) % 10;
    dig[3] = lat_min_int % 10;
    dig[4] = (lat_hun / 10) % 10;
    dig[5] = lat_hun % 10;

    // Position ambiguity (APRS101 Ch.10 "Mic-E Position Ambiguity"): blank
    // out digits from the hundredths-of-a-minute byte (byte 6) backward,
    // the same convention aprs_mice_decode() reads them with.
    int blank_from = 6; // no blanking by default
    switch (report->position.ambiguity) {
        case APRS_AMBIGUITY_TENTH_MINUTE:
            blank_from = 5;
            break;
        case APRS_AMBIGUITY_MINUTE:
            blank_from = 4;
            break;
        case APRS_AMBIGUITY_TEN_MINUTES:
            blank_from = 3;
            break;
        case APRS_AMBIGUITY_DEGREE:
            blank_from = 2;
            break;
        case APRS_AMBIGUITY_NONE:
        default:
            blank_from = 6;
            break;
    }
    for (int i = blank_from; i < 6; i++)
        dig[i] = -1;

    bool msgA, msgB, msgC;
    mice_message_to_bits(report->message_code, &msgA, &msgB, &msgC);

    bool west = lon < 0.0;
    double lon_abs = west ? -lon : lon;
    int lon_deg_full = (int)lon_abs;
    double lon_min = (lon_abs - lon_deg_full) * 60.0;
    int lon_hun_total = (int)lround(lon_min * 100.0);
    if (lon_hun_total >= 6000) {
        lon_hun_total -= 6000;
        lon_deg_full += 1;
    }
    int lon_min_int = lon_hun_total / 100;
    int lon_hun = lon_hun_total % 100;

    if (lon_deg_full > 179)
        return false; // rounding carried into the unencodable +/-180 antimeridian

    // Longitude offset flag (byte 5 of the destination address): set
    // whenever the (unsigned) longitude is 100 degrees or more, matching
    // the offset range aprs_mice_decode() expects for the d1_val encoding
    // just below.
    bool long_offset_100 = lon_deg_full >= 100;

    char d[6];
    d[0] = mice_encode_dest_byte(dig[0], msgA);
    d[1] = mice_encode_dest_byte(dig[1], msgB);
    d[2] = mice_encode_dest_byte(dig[2], msgC);
    d[3] = mice_encode_dest_byte(dig[3], north);
    d[4] = mice_encode_dest_byte(dig[4], long_offset_100);
    d[5] = mice_encode_dest_byte(dig[5], west);
    memcpy(dst_call_out, d, 6);
    dst_call_out[6] = 0;

    // Longitude degrees/minutes/hundredths (APRS101 Ch.10 "Longitude Degrees/
    // Minutes/Hundredths of Minutes Encoding"): inverse of the decode in
    // aprs_mice_decode(). Below 100 degrees the offset flag is clear and
    // byte = deg + 28 covers the range directly; at or above 100 degrees
    // the offset flag is set and the decoder's own 180-189 correction range
    // makes byte = deg - 72 (i.e. deg - 100 + 28) land back on the same
    // value once decoded. Verified by brute-force round-trip against
    // aprs_mice_decode() for the full 0-180 degree range.
    int d1_val = long_offset_100 ? lon_deg_full - 72 : lon_deg_full + 28;

    int d2_val = lon_min_int + 28;
    int d3_val = lon_hun + 28;

    // Course and speed (APRS101 Ch.10 "Decoding the Speed and Course"),
    // inverse of the split used in aprs_mice_decode(). An "unknown"
    // course/speed report (report->course_speed.is_unknown) is sent as
    // 0/0, the same convention aprs_mice_decode() recognizes on receive.
    int speed = report->course_speed.is_unknown ? 0 : (int)report->course_speed.speed_knots;
    int course = report->course_speed.is_unknown ? 0 : (int)report->course_speed.course_deg;
    if (speed < 0)
        speed = 0;
    if (speed > 799)
        speed = 799;
    if (course < 0)
        course = 0;
    if (course > 399)
        course = 399;

    int sp_val = (speed / 10) + 28;
    int dc_val = ((speed % 10) * 10 + (course / 100)) + 28;
    int se_val = (course % 100) + 28;

    // The Data Type Identifier states how fresh the position is, not what
    // follows it: '`' is "current Mic-E data", the only thing a report built
    // from this station's own live position can be. ('\'' means old/stale
    // data and is what makes APRSdos and the Kenwood radios label a station
    // "OLD FIX".) Messaging capability is stated separately, by the TYPE
    // byte at info[9].
    char info[11];
    info[0] = '`';
    info[1] = (char)d1_val;
    info[2] = (char)d2_val;
    info[3] = (char)d3_val;
    info[4] = (char)sp_val;
    info[5] = (char)dc_val;
    info[6] = (char)se_val;
    info[7] = report->position.symbol.code ? report->position.symbol.code : '>';
    info[8] = (char)(report->position.symbol.table == APRS_SYMBOL_TABLE_ALTERNATE ? APRS_SYMBOL_TABLE_ALTERNATE : APRS_SYMBOL_TABLE_PRIMARY);

    // TYPE byte (aprs12/mic-e-types.txt), always emitted: '`' for a
    // message-capable station, '\'' for a one-way tracker. Mic-E puts the
    // position in the destination address, so this byte and the Mv pair below
    // are the only identification the format has room for.
    info[9] = report->msg_capable ? '`' : '\'';

    memcpy(info_out, info, 10);
    info_out[10] = 0;

    // Optional altitude field, then the free text (APRS101 Ch.10 "Mic-E
    // Status Text"). Both are emitted when both are present: the altitude is
    // a fixed 4-byte prefix that shifts the text along, so a receiver reads
    // the altitude and still sees the whole comment.
    if (report->position.has_altitude)
        mice_encode_altitude(report->position.altitude_ft, info_out, info_out_max);

    if (report->has_status_text && report->status_text[0])
        mice_append(info_out, info_out_max, report->status_text, strlen(report->status_text));

    // Manufacturer and Version bytes, closing the information field after the
    // text and after any "!DAO!" the caller put at its end. The spec's
    // recommended single-space delimiter is included so the pair cannot be
    // mistaken for the last two characters of the text. All three bytes are
    // written together or not at all, so a full buffer never leaves a
    // dangling separator or half an identifier on the air.
    if (report->device_id[0] && report->device_id[1]) {
        char mv[3];
        mv[0] = ' ';
        mv[1] = report->device_id[0];
        mv[2] = report->device_id[1];
        if (strlen(info_out) + 3 < info_out_max)
            mice_append(info_out, info_out_max, mv, 3);
    }

    return true;
}
