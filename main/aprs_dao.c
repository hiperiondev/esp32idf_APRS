// @file aprs_dao.c
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
// @brief WGS-84 human-readable "!DAO!" precision/datum extension builder. See
// aprs_dao.h.

#include "aprs_dao.h"

#include <math.h>

// Returns the third decimal digit of the minutes part of an absolute-value
// decimal-degrees coordinate, i.e. the thousandths-of-a-minute digit that
// sits one place past the hundredths already carried by the plain
// "DDMM.mmN"/"DDDMM.mmW" fields. The minutes value is rounded to three
// decimal places first so a value that lands exactly on a digit boundary
// (e.g. .9175) is not truncated one digit short, and a rounding carry all
// the way to a whole minute (999 thousandths) is folded back to 9 rather
// than producing a digit outside 0-9.
static char extraMinuteDigit(float absVal) {
    int deg = (int)absVal;
    float minutes = (absVal - deg) * 60.0f;

    long thousandths = lroundf(minutes * 1000.0f);
    if (thousandths < 0)
        thousandths = 0;
    if (thousandths > 59999)
        thousandths = 59999;

    return (char)('0' + (thousandths % 10));
}

void aprs_dao_build(float lat, float lon, char out[APRS_DAO_BUF_SIZE]) {
    if (out == NULL)
        return;

    out[0] = '!';
    out[1] = 'W'; // datum: WGS-84, uppercase = human-readable one-digit form
    out[2] = extraMinuteDigit(fabsf(lat));
    out[3] = extraMinuteDigit(fabsf(lon));
    out[4] = '!';
    out[5] = 0;
}

// Minutes contributed by one unit of each axis byte. The human-readable form
// spends its digit on the third decimal place of the minutes field; the
// base-91 form spreads its 91 steps across the hundredth of a minute the
// plain field rounds to.
#define DAO_HUMAN_STEP_MIN  0.001f
#define DAO_BASE91_STEP_MIN (0.01f / 91.0f)

// Lowest and highest base-91 digit bytes, '!' and '{'.
#define DAO_BASE91_MIN_CHAR '!'
#define DAO_BASE91_MAX_CHAR '{'

// Decodes one axis byte of a human-readable token. A space means the sender
// left that axis unrefined.
static bool humanAxis(char c, float *out_min) {
    if (c == ' ') {
        *out_min = 0.0f;
        return true;
    }
    if (c < '0' || c > '9')
        return false;

    *out_min = (float)(c - '0') * DAO_HUMAN_STEP_MIN;
    return true;
}

// Decodes one axis byte of a base-91 token, with the same space convention.
static bool base91Axis(char c, float *out_min) {
    if (c == ' ') {
        *out_min = 0.0f;
        return true;
    }
    if (c < DAO_BASE91_MIN_CHAR || c > DAO_BASE91_MAX_CHAR)
        return false;

    *out_min = (float)(c - DAO_BASE91_MIN_CHAR) * DAO_BASE91_STEP_MIN;
    return true;
}

bool aprs_dao_parse(const char *text, size_t len, float *out_lat_extra_min, float *out_lon_extra_min) {
    if (text == NULL || len < 5)
        return false;

    // The token may sit anywhere in the comment, so every '!' with four more
    // bytes behind it is a candidate; the datum byte and the two axis bytes
    // decide whether it really is one.
    for (size_t i = 0; i + 4 < len; i++) {
        if (text[i] != '!' || text[i + 4] != '!')
            continue;

        char datum = text[i + 1];
        float latExtra, lonExtra;

        if (datum >= 'A' && datum <= 'Z') {
            if (!humanAxis(text[i + 2], &latExtra) || !humanAxis(text[i + 3], &lonExtra))
                continue;
        } else if (datum >= 'a' && datum <= 'z') {
            if (!base91Axis(text[i + 2], &latExtra) || !base91Axis(text[i + 3], &lonExtra))
                continue;
        } else {
            continue;
        }

        if (out_lat_extra_min != NULL)
            *out_lat_extra_min = latExtra;
        if (out_lon_extra_min != NULL)
            *out_lon_extra_min = lonExtra;
        return true;
    }

    return false;
}
