/**
 * @file fx25.h
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
 * @brief FX.25 forward-error-correction (FEC) framing on top of AX.25.
 *
 * @note FX.25 requires the Reed-Solomon codec (rs.h / rs.c from VP-Digi),
 *       which was NOT included in the archive this component was ported
 *       from. The declarations and logic below are ported and ready to use,
 *       but remain disabled until rs.h/rs.c are added to this component and
 *       the build is compiled with -DENABLE_FX25.
 */

#ifndef LIB_FX25_H_
#define LIB_FX25_H_

#ifdef ENABLE_FX25

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Largest FX.25 block size supported, in bytes.
 */
#define FX25_MAX_BLOCK_SIZE 255

/**
 * @brief Description of one FX.25 coding mode: correlation tag, payload
 *        size and parity size.
 */
struct Fx25Mode {
    uint64_t tag; /**< 64-bit correlation tag identifying this mode on the air. */
    uint16_t K;   /**< Data (payload) size, in bytes. */
    uint8_t T;    /**< Parity (Reed-Solomon check symbol) size, in bytes. */
};

/**
 * @brief Table of all supported FX.25 coding modes.
 */
extern const struct Fx25Mode Fx25ModeList[11];

/**
 * @brief Look up an FX.25 mode by its correlation tag.
 * @param tag Correlation tag read from the air.
 * @return Pointer to the matching mode, or NULL if no mode matches.
 */
const struct Fx25Mode *Fx25GetModeForTag(uint64_t tag);

/**
 * @brief Choose the smallest FX.25 mode able to carry a frame of the given
 *        size.
 * @param size Size, in bytes, of the AX.25 frame to protect.
 * @return Pointer to the selected mode, or NULL if no mode is large enough.
 */
const struct Fx25Mode *Fx25GetModeForSize(uint16_t size);

/**
 * @brief Encode a buffer in place using FX.25 (add Reed-Solomon parity).
 * @param buffer In/out buffer: input holds the plain frame data, output
 *               holds the encoded FX.25 block; must be large enough to hold
 *               @p mode's total block size.
 * @param mode   FX.25 mode to encode with.
 */
void Fx25Encode(uint8_t *buffer, const struct Fx25Mode *mode);

/**
 * @brief Decode and error-correct an FX.25 block in place.
 * @param buffer  In/out buffer holding the received FX.25 block; overwritten
 *                with the corrected data on success.
 * @param mode    FX.25 mode the block was encoded with.
 * @param fixed   Set to the number of byte errors that were corrected.
 * @return true if the block was successfully decoded (with or without
 *         corrections), false if it was uncorrectable.
 */
bool Fx25Decode(uint8_t *buffer, const struct Fx25Mode *mode, uint8_t *fixed);

/**
 * @brief Initialize internal FX.25 tables and state (Reed-Solomon codec
 *        setup).
 */
void Fx25Init(void);

#endif /* ENABLE_FX25 */

#endif /* LIB_FX25_H_ */
