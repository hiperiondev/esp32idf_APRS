/**
 * @file aprs_df.h
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
 * @brief Encoder for the APRS101 chapter 8 DF report data extension:
 * "CSE/SPD/BRG/NRQ", the course/speed token extended with the bearing to a
 * signal and the NRQ triplet describing that bearing.
 *
 * Header-only (@c static @c inline, no translation unit of its own and no
 * build-system change), following the pattern of str_append.h, aprs_path.h and
 * aprs_free_text.h. Both originators of the extension include it - the
 * Objects/Items encoder in main/objects_items.c, which reports a fix taken on
 * someone else, and the own-station position beacon in main/beacon.c, which is
 * the form chapter 16 describes for a direction-finding station - so the two
 * cannot drift apart on the wire.
 *
 * The token occupies the same 7-byte data-extension slot every other extension
 * uses, but is longer than it: the slot holds the leading "CSE/SPD" pair and
 * the "/BRG/NRQ" bytes follow. This is the layout the specification defines,
 * and it is why the compressed position format - which has no extension slot
 * at all - cannot carry a DF report.
 */

#ifndef APRS_DF_H
#define APRS_DF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "weather_telemetry.h" // aprs_bearing_nrq_t

/**
 * @brief Symbol Table ID a station must transmit for its DF report to be read
 *        as one.
 *
 * Chapter 8 states, in the note that precedes both DF Report format diagrams,
 * that the BRG/NRQ parameters are only meaningful when the report carries the
 * DF symbol, i.e. when the Symbol Table ID is @c '/' and the Symbol Code is
 * @c '\\'. The 1.2 errata list records the same rule as an explicit addition
 * to the chapter.
 *
 * The rule is what decides the width of the data-extension slot on the wire:
 * the token is 15 bytes where the slot is 7, so a receiver that does not see
 * the DF symbol reads the leading "CSE/SPD" pair as an ordinary course/speed
 * extension and the trailing "/BRG/NRQ" bytes as the first eight characters
 * of the comment field. Both originators of the token and the receive-side
 * parser therefore share this one definition.
 */
#define APRS_DF_SYMBOL_TABLE '/'

/** @brief Symbol Code that goes with ::APRS_DF_SYMBOL_TABLE to form the DF
 *         symbol pair. @see APRS_DF_SYMBOL_TABLE */
#define APRS_DF_SYMBOL_CODE '\\'

/** @brief On-air width of a "CSE/SPD/BRG/NRQ" token, excluding the NUL. */
#define APRS_DF_EXT_LEN 15

/** @brief On-air width of the "/BRG/NRQ" bytes that follow the "CSE/SPD" pair,
 *         i.e. how far past the 7-byte data-extension slot the token reaches. */
#define APRS_DF_EXT_TAIL_LEN 8

/** @brief Buffer size a caller must provide to hold one built token. */
#define APRS_DF_EXT_BUF_SIZE (APRS_DF_EXT_LEN + 1)

/** @brief Highest bearing accepted; the field is three decimal digits and the
 *         builder reduces any larger value modulo 360. */
#define APRS_DF_BEARING_MAX 359

/** @brief Highest value each of the three NRQ digits can carry: N (hits per
 *         sampling period), R (range code) and Q (bearing accuracy) are one
 *         decimal digit each. */
#define APRS_DF_NRQ_DIGIT_MAX 9

/** @brief Widest course or speed the three-digit CSE and SPD fields hold. */
#define APRS_DF_CSE_SPD_MAX 999

/**
 * @brief True when a symbol pair is the DF symbol, and therefore when a
 *        "CSE/SPD/BRG/NRQ" token may be transmitted with it or read out of a
 *        received report.
 *
 * @param sym_table Symbol Table ID byte of the report.
 * @param sym_code  Symbol Code byte of the report.
 * @return true when the pair is ::APRS_DF_SYMBOL_TABLE / ::APRS_DF_SYMBOL_CODE.
 * @see APRS_DF_SYMBOL_TABLE
 */
static inline bool aprs_df_symbol_matches(char sym_table, char sym_code) {
    return sym_table == APRS_DF_SYMBOL_TABLE && sym_code == APRS_DF_SYMBOL_CODE;
}

/**
 * @brief Build the "CSE/SPD/BRG/NRQ" DF report data extension.
 *
 * A course/speed pair of "000/000" is what the specification uses to say that
 * no course/speed data accompanies the bearing, which is the case for a
 * fixed station reporting a fix.
 *
 * @param course_deg Course in degrees; reduced modulo 360.
 * @param speed_kt   Speed; clamped to ::APRS_DF_CSE_SPD_MAX (the field is
 *                   three digits wide).
 * @param nrq        Bearing and NRQ triplet. The bearing is reduced modulo
 *                   360 and each NRQ digit is clamped to
 *                   ::APRS_DF_NRQ_DIGIT_MAX. Must be non-NULL.
 * @param out        Destination buffer, at least ::APRS_DF_EXT_BUF_SIZE bytes.
 * @param out_size   Size of @p out.
 * @return Number of bytes written, excluding the NUL, or 0 when an argument is
 *         NULL or @p out is too small to hold the whole token (a truncated
 *         extension would decode as a different bearing rather than as a
 *         missing one).
 */
static inline size_t aprs_df_build_extension(uint16_t course_deg, uint16_t speed_kt, const aprs_bearing_nrq_t *nrq, char *out, size_t out_size) {
    if (out == NULL || out_size == 0)
        return 0;

    out[0] = 0;
    if (nrq == NULL || out_size < APRS_DF_EXT_BUF_SIZE)
        return 0;

    unsigned crs = (unsigned)(course_deg % 360);
    unsigned spd = speed_kt > APRS_DF_CSE_SPD_MAX ? (unsigned)APRS_DF_CSE_SPD_MAX : (unsigned)speed_kt;
    unsigned brg = (unsigned)(nrq->bearing_deg % 360);
    unsigned n = nrq->number > APRS_DF_NRQ_DIGIT_MAX ? (unsigned)APRS_DF_NRQ_DIGIT_MAX : (unsigned)nrq->number;
    unsigned r = nrq->range_code > APRS_DF_NRQ_DIGIT_MAX ? (unsigned)APRS_DF_NRQ_DIGIT_MAX : (unsigned)nrq->range_code;
    unsigned q = nrq->quality > APRS_DF_NRQ_DIGIT_MAX ? (unsigned)APRS_DF_NRQ_DIGIT_MAX : (unsigned)nrq->quality;

    int written = snprintf(out, out_size, "%03u/%03u/%03u/%u%u%u", crs, spd, brg, n, r, q);
    if (written != APRS_DF_EXT_LEN) {
        out[0] = 0;
        return 0;
    }

    return (size_t)written;
}

#endif // APRS_DF_H
