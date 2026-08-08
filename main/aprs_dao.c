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
