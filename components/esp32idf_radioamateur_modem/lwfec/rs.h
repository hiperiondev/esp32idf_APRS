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
 */

#include <stdbool.h>
#include <stdint.h>

#define RS_MAX_REDUNDANCY_BYTES 64 // maximum parity bytes

#define RS_BLOCK_SIZE    255 // natural full RS block size
#define RS_MAX_DATA_SIZE (RS_BLOCK_SIZE - RS_MAX_REDUNDANCY_BYTES)

/**
 * @brief Reed-Solomon module configuration structure
 */
struct LwFecRS {
    uint8_t generator[RS_MAX_REDUNDANCY_BYTES + 1]; // generator polynomial
    uint8_t T;                                      // number of redundancy/parity bytes
    uint8_t fcr;                                    // first consecutive root index
};

/**
 * @brief Decode message using Reed-Solomon FEC
 *
 * This function takes input buffer with K data bytes and T parity bytes
 * Then it moves parity bytes to the end and fills everything inbetween with zeros.
 * Next the in-place decoding is performed.
 * @param *rs RS coder/decoder instance
 * @param *data Input/output buffer. Must be of size N = 255
 * @param size Data size = K
 * @param *fixed Output number of bytes corrected
 * @return True on success, false on failure
 */
bool RsDecode(struct LwFecRS *rs, uint8_t *data, uint8_t size, uint8_t *fixed);

/**
 * @brief Encode message using Reed-Solomon FEC
 * @param *rs RS coder/decoder instance
 * @param *data Input/output buffer. Must be of size N = 255
 * @param size Data size = K
 */
void RsEncode(struct LwFecRS *rs, uint8_t *data, uint8_t size);

/**
 * @brief Initialize Reed-Solomon coder/decoder
 *
 * This function calculates generator polynomial and stores required constants.
 * @param *rs RS coder/decoder instance to be filled
 * @param T Number of parity check bytes
 * @param fcr First consecutive root index
 */
void RsInit(struct LwFecRS *rs, uint8_t T, uint8_t fcr);
