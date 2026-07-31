// mice.c
//
// Author: Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
// Date: 2026
// Copyright: GNU General Public License v3
// See: https://github.com/hiperiondev/esp32idf_APRS
//
// This is based on other projects:
//     VP-Digi: https://github.com/sq8vps/vp-digi
//     ESP32APRS: https://github.com/nakhonthai/ESP32APRS_Audio
//     LibAPRS: https://github.com/markqvist/LibAPRS
//
//     please contact their authors for more information.
//
// Decodes the Mic-E position report format (APRS101 Chapter 10): the AX.25
// destination address field encodes latitude, the 3-bit message code and
// the N/S, longitude-offset and W/E flag bits; the AX.25 information field
// completes the report with longitude, course, speed, symbol and an
// optional status text.

#include <ctype.h>
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

// Altitude in the Mic-E Status Text Field (APRS101 Ch.10): the last 4 bytes
// of the status text, if present, are 3 base-91 digits ('!'..'{') followed
// by a literal '}', encoding altitude in meters as
// (c0-33)*91*91 + (c1-33)*91 + (c2-33) - 10000.
static bool mice_status_altitude_ft(const char *text, size_t len, int32_t *out_ft) {
    if (len < 4 || text[len - 1] != '}')
        return false;

    for (int i = 0; i < 3; i++) {
        unsigned char c = (unsigned char)text[len - 4 + i];
        if (c < '!' || c > '{')
            return false;
    }

    long meters = (text[len - 4] - 33) * 91L * 91L + (text[len - 3] - 33) * 91L + (text[len - 2] - 33) - 10000L;
    *out_ft = (int32_t)((double)meters * 3.28084);
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

    // Optional trailing status text (APRS101 Ch.10 "Mic-E Status Text");
    // may itself embed an altitude ("xxx}", base-91 - see
    // mice_status_altitude_ft()) or a Maidenhead locator ("AA99AA/x"),
    // captured here verbatim for the caller to inspect further.
    if (info_len > 9) {
        const char *tail = &info[9];
        size_t tail_len = info_len - 9;
        size_t copy_len = tail_len < APRS_MAX_STATUS_TEXT_LEN ? tail_len : APRS_MAX_STATUS_TEXT_LEN;

        memcpy(out->status_text, tail, copy_len);
        out->status_text[copy_len] = 0;
        out->has_status_text = true;

        int32_t alt_ft;
        if (mice_status_altitude_ft(tail, tail_len, &alt_ft)) {
            out->position.has_altitude = true;
            out->position.altitude_ft = alt_ft;
        }
    }

    return true;
}
