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
 * @note FX.25 sits on top of the Reed-Solomon codec in @c lwfec/ (@c rs.h,
 *       @c rs.c) and is only compiled when the build defines @c ENABLE_FX25,
 *       which this component's own @c CMakeLists.txt does publicly.
 *
 * @par Buffer-size contract
 * Fx25Encode() and Fx25Decode() work in place on a full Reed-Solomon block, so
 * their buffer must hold ::FX25_MAX_BLOCK_SIZE (255) bytes for every mode,
 * including the modes whose payload @c K is as small as 32 bytes. Both take an
 * explicit capacity argument that is checked before the codec is entered.
 */

#ifndef LIB_FX25_H_
#define LIB_FX25_H_

#ifdef ENABLE_FX25

#include <stdbool.h>
#include <stddef.h>
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
 * @param[in,out] buffer In/out buffer: input holds the plain frame data,
 *                output holds the encoded FX.25 block. The whole
 *                ::FX25_MAX_BLOCK_SIZE byte block is rewritten, so it must be
 *                that large even for the small-@c K modes
 * @param[in] bufferCapacity Number of bytes the caller owns at @p buffer. Must
 *            be at least ::FX25_MAX_BLOCK_SIZE; a smaller value makes the call
 *            return without touching @p buffer
 * @param[in] mode FX.25 mode to encode with.
 * @pre @p bufferCapacity >= ::FX25_MAX_BLOCK_SIZE
 */
void Fx25Encode(uint8_t *buffer, size_t bufferCapacity, const struct Fx25Mode *mode);

/**
 * @brief Decode and error-correct an FX.25 block in place.
 * @param[in,out] buffer In/out buffer holding the received FX.25 block;
 *                overwritten with the corrected data on success. The whole
 *                ::FX25_MAX_BLOCK_SIZE byte block is rewritten, so it must be
 *                that large even for the small-@c K modes
 * @param[in] bufferCapacity Number of bytes the caller owns at @p buffer. Must
 *            be at least ::FX25_MAX_BLOCK_SIZE; a smaller value makes the call
 *            fail without touching @p buffer
 * @param[in] mode FX.25 mode the block was encoded with.
 * @param[out] fixed Set to the number of byte errors that were corrected.
 * @return true if the block was successfully decoded (with or without
 *         corrections), false if it was uncorrectable or @p bufferCapacity is
 *         smaller than ::FX25_MAX_BLOCK_SIZE.
 * @pre @p bufferCapacity >= ::FX25_MAX_BLOCK_SIZE
 */
bool Fx25Decode(uint8_t *buffer, size_t bufferCapacity, const struct Fx25Mode *mode, uint8_t *fixed);

/**
 * @brief Initialize internal FX.25 tables and state (Reed-Solomon codec
 *        setup).
 */
void Fx25Init(void);

#endif /* ENABLE_FX25 */

#endif /* LIB_FX25_H_ */
