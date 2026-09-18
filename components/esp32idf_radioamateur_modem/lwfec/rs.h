/**
 * @file rs.h
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
 * @brief Reed-Solomon forward-error-correction codec used by FX.25.
 *
 * @par Buffer-size contract
 * RsEncode() and RsDecode() operate in place on a **full Reed-Solomon block**
 * of ::RS_BLOCK_SIZE (255) bytes, no matter how small the @c size (K) argument
 * is: the parity bytes are relocated to the tail of the block and everything
 * between the payload and the parity is zero-filled before the codec runs.
 * A caller that sizes its buffer to @c size bytes therefore gets a buffer
 * overflow, not a shorter block. Both functions take an explicit
 * @c dataCapacity argument for that reason: it declares how many bytes the
 * caller really owns, is asserted against ::RS_BLOCK_SIZE, and makes the
 * function fail safely instead of writing out of bounds when the buffer is
 * too small.
 */

#ifndef RS_H_
#define RS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RS_MAX_REDUNDANCY_BYTES 64 /**< Maximum number of parity (redundancy) bytes. */

#define RS_BLOCK_SIZE    255                                       /**< Natural full Reed-Solomon block size, in bytes (N). */
#define RS_MAX_DATA_SIZE (RS_BLOCK_SIZE - RS_MAX_REDUNDANCY_BYTES) /**< Maximum data payload size, in bytes (K), at maximum parity. */

/**
 * @brief Reed-Solomon module configuration structure
 */
struct LwFecRS {
    uint8_t generator[RS_MAX_REDUNDANCY_BYTES + 1]; /**< Generator polynomial. */
    uint8_t T;                                      /**< Number of redundancy/parity bytes. */
    uint8_t fcr;                                    /**< First consecutive root index. */
};

/**
 * @brief Decode message using Reed-Solomon FEC
 *
 * This function takes input buffer with K data bytes and T parity bytes.
 * Then it moves parity bytes to the end and fills everything inbetween with zeros.
 * Next the in-place decoding is performed.
 *
 * @param[in] rs RS coder/decoder instance
 * @param[in,out] data Input/output buffer, worked on as a whole
 *                ::RS_BLOCK_SIZE (N = 255) byte block: bytes past @p size are
 *                rewritten even when @p size is much smaller than N
 * @param[in] dataCapacity Number of bytes the caller owns at @p data. Must be
 *            at least ::RS_BLOCK_SIZE (255) **regardless of @p size**; a
 *            smaller value makes the call fail without touching @p data
 * @param[in] size Data size = K, in bytes. This is the payload length, not the
 *            size of the @p data buffer
 * @param[out] fixed Output number of bytes corrected
 * @return True on success, false on failure (including a @p dataCapacity
 *         smaller than ::RS_BLOCK_SIZE)
 * @pre @p dataCapacity >= ::RS_BLOCK_SIZE
 * @pre @p size <= ::RS_BLOCK_SIZE - @c rs->T
 */
bool RsDecode(const struct LwFecRS *rs, uint8_t *data, size_t dataCapacity, uint8_t size, uint8_t *fixed);

/**
 * @brief Encode message using Reed-Solomon FEC
 *
 * The K payload bytes at the head of @p data are kept, the rest of the block is
 * zero-filled and the T parity bytes are written starting at @c data[size].
 *
 * @param[in] rs RS coder/decoder instance
 * @param[in,out] data Input/output buffer, worked on as a whole
 *                ::RS_BLOCK_SIZE (N = 255) byte block: bytes past @p size are
 *                rewritten even when @p size is much smaller than N
 * @param[in] dataCapacity Number of bytes the caller owns at @p data. Must be
 *            at least ::RS_BLOCK_SIZE (255) **regardless of @p size**; a
 *            smaller value makes the call return without touching @p data
 * @param[in] size Data size = K, in bytes. This is the payload length, not the
 *            size of the @p data buffer
 * @pre @p dataCapacity >= ::RS_BLOCK_SIZE
 * @pre @p size <= ::RS_BLOCK_SIZE - @c rs->T
 */
void RsEncode(const struct LwFecRS *rs, uint8_t *data, size_t dataCapacity, uint8_t size);

/**
 * @brief Initialize Reed-Solomon coder/decoder
 *
 * This function calculates generator polynomial and stores required constants.
 * @param[out] rs RS coder/decoder instance to be filled
 * @param[in] T Number of parity check bytes, at most ::RS_MAX_REDUNDANCY_BYTES
 * @param[in] fcr First consecutive root index
 */
void RsInit(struct LwFecRS *rs, uint8_t T, uint8_t fcr);

#endif /* RS_H_ */
