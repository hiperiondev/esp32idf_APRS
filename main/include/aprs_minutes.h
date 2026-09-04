/**
 * @file aprs_minutes.h
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
 * @brief Single degrees/minutes quantisation shared by every transmit-side
 * encoder that puts a coordinate on the air in the degrees-minutes form.
 *
 * Three encoders publish the same coordinate at two different resolutions:
 *
 * - the uncompressed "DDMM.mmN"/"DDDMM.mmW" position fields (APRS101 chapter
 *   6, main/aprs_coord.c) and the Mic-E destination address and longitude
 *   bytes (APRS101 chapter 10, components/weather_telemetry/mice.c) carry
 *   hundredths of a minute;
 * - the "!DAO!" precision extension (aprs12/datum.txt, main/aprs_dao.c)
 *   carries the one further decimal digit, the thousandths, that a receiver
 *   appends to the field above to recover the position more precisely.
 *
 * A receiver reconstructs the coordinate as `base field + DAO digit * 0.001`
 * minutes, so the two halves are only meaningful together: the digit has to
 * be the next digit of the very number the base field emitted. Computing the
 * two independently - even from the same input, even with the same rounding
 * rule - lets the results disagree, because they quantise at different
 * scales and a coordinate at a hundredth-of-a-minute boundary lands on
 * either side of it depending on which scale it was measured at. Where they
 * disagree the DAO digit refines a hundredths value that was never
 * transmitted, and the reconstructed position ends up further from the truth
 * than the base field alone would have been.
 *
 * This header is therefore the one place the split happens. Every encoder
 * calls ::aprs_minutes_split once per axis and reads the field it needs out
 * of the result, so the base field and the DAO digit are by construction two
 * views of a single number.
 *
 * The header is self-contained and defines its function inline, so a
 * component consuming it needs `../../main/include` on its include path but
 * takes on no link-time dependency on main.
 */

#ifndef APRS_MINUTES_H
#define APRS_MINUTES_H

/**
 * @brief Minutes in one degree.
 */
#define APRS_MINUTES_PER_DEGREE 60

/**
 * @brief Thousandths of a minute in one whole minute, i.e. the resolution
 * ::aprs_minutes_split quantises to and the place value of the "!DAO!"
 * digit.
 */
#define APRS_MINUTES_THOUSANDTHS_PER_MINUTE 1000

/**
 * @brief Thousandths of a minute in one degree, i.e. the value at which the
 * split carries into the next whole degree.
 */
#define APRS_MINUTES_THOUSANDTHS_PER_DEGREE (APRS_MINUTES_PER_DEGREE * APRS_MINUTES_THOUSANDTHS_PER_MINUTE)

/**
 * @brief One coordinate axis split into whole degrees and thousandths of a
 * minute, as produced by ::aprs_minutes_split.
 */
typedef struct {
    int deg;         /**< Whole degrees, with the 60.000-minute carry already applied. */
    int thousandths; /**< Minutes as thousandths, 0..::APRS_MINUTES_THOUSANDTHS_PER_DEGREE - 1. */
} aprs_minutes_t;

/**
 * @brief Split one absolute-value decimal-degrees coordinate into whole
 * degrees and thousandths of a minute.
 *
 * The minutes are rounded to the nearest thousandth, which is the finest
 * resolution any transmitted form of the coordinate expresses, and every
 * coarser form is then derived from that single rounded integer rather than
 * measured again from the input. Rounding to nearest, rather than
 * truncating, bounds the error of the fully reconstructed coordinate at half
 * a thousandth of a minute (about 0.9 m) instead of a whole one.
 *
 * A minutes value that reaches a full 60.000 - either because the coordinate
 * really is that close to the top of a degree or because of floating-point
 * error on the way in - carries into the next whole degree, so the minutes
 * handed back are always inside the 00.000-59.999 range the on-air fields
 * accept. The carry can only ever fire once, since the fractional part of
 * the input is below 1 by construction.
 *
 * The argument is a double so that a float coordinate widens to it exactly
 * and every caller quantises the identical value, whatever type it holds the
 * coordinate in. A negative or not-a-number input is treated as zero, so the
 * conversion to int below is always defined.
 *
 * @param abs_deg Absolute value of the coordinate, in decimal degrees.
 * @return The degrees and thousandths-of-a-minute pair.
 */
static inline aprs_minutes_t aprs_minutes_split(double abs_deg) {
    aprs_minutes_t out;

    // Written as a negated >= test so a NaN input takes this branch too.
    if (!(abs_deg >= 0.0))
        abs_deg = 0.0;

    int deg = (int)abs_deg;
    double minutes = (abs_deg - deg) * (double)APRS_MINUTES_PER_DEGREE;

    // minutes is non-negative here, so adding a half unit before truncating
    // is a round-to-nearest without pulling in a libm call.
    long thousandths = (long)(minutes * (double)APRS_MINUTES_THOUSANDTHS_PER_MINUTE + 0.5);

    if (thousandths < 0)
        thousandths = 0;
    if (thousandths >= APRS_MINUTES_THOUSANDTHS_PER_DEGREE) {
        thousandths -= APRS_MINUTES_THOUSANDTHS_PER_DEGREE;
        deg += 1;
    }

    out.deg = deg;
    out.thousandths = (int)thousandths;
    return out;
}

/**
 * @brief Hundredths of a minute of a split coordinate, i.e. the whole
 * "MM.mm" minutes field the uncompressed position layout and the Mic-E
 * position bytes transmit.
 *
 * @param m Split produced by ::aprs_minutes_split.
 * @return Minutes as hundredths, 0..5999.
 */
static inline int aprs_minutes_hundredths(aprs_minutes_t m) {
    return m.thousandths / 10;
}

/**
 * @brief Third decimal minute digit of a split coordinate, i.e. the digit
 * the "!DAO!" extension carries so a receiver can append it to the
 * hundredths the base field already transmitted.
 *
 * @param m Split produced by ::aprs_minutes_split.
 * @return The digit, 0..9.
 */
static inline int aprs_minutes_dao_digit(aprs_minutes_t m) {
    return m.thousandths % 10;
}

#endif // APRS_MINUTES_H
