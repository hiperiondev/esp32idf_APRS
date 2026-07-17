/**
 * @file crc_ccit.h
 *
 * @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
 * @date 2026
 * @copyright GNU General Public License v3
 * @see https://github.com/hiperiondev/esp32idf_radioamateur_modem
 *
 * @note
 * This is based on other projects:
 *     VP-Digi: https://github.com/sq8vps/vp-digi
 *     ESP32APRS: https://github.com/nakhonthai/ESP32APRS_Audio
 *     LibAPRS: https://github.com/markqvist/LibAPRS
 *
 *     please contact their authors for more information.
 *
 * @brief CRC-CCITT computation used for the AX.25 Frame Check Sequence
 *        (FCS). Based on work by Francesco Sacchi.
 */

#ifndef LIB_CRC_CCIT_H_
#define LIB_CRC_CCIT_H_

#include <stdint.h>

/**
 * @brief Initial value of the CRC-CCITT accumulator, as required by the
 *        AX.25 specification.
 */
#define CRC_CCIT_INIT_VAL ((uint16_t)0xFFFF)

/**
 * @brief Precomputed CRC-CCITT lookup table, indexed by byte value.
 */
extern const uint16_t crc_ccit_table[256];

/**
 * @brief Update a running CRC-CCITT value with one additional byte.
 * @param c        Next input byte to fold into the CRC.
 * @param prev_crc Current CRC accumulator value.
 * @return Updated CRC accumulator value.
 */
static inline uint16_t update_crc_ccit(uint8_t c, uint16_t prev_crc) {
    return (uint16_t)((prev_crc >> 8) ^ crc_ccit_table[(prev_crc ^ c) & 0xff]);
}

/**
 * @brief Compute the AX.25 Frame Check Sequence over a complete frame.
 * @param data Frame bytes to checksum.
 * @param len  Number of bytes in @p data.
 * @return Computed FCS value.
 */
uint16_t fcs_calc(const uint8_t *data, int len);

/**
 * @brief Compute a CRC-CCITT value over a buffer, starting from an
 *        arbitrary seed instead of ::CRC_CCIT_INIT_VAL.
 * @param data Bytes to checksum.
 * @param len  Number of bytes in @p data.
 * @param seed Initial CRC accumulator value.
 * @return Computed CRC value.
 */
uint16_t crc16_seed(const uint8_t *data, int len, uint16_t seed);

#endif /* LIB_CRC_CCIT_H_ */
