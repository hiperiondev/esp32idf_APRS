/**
 * @file kiss.h
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
 * @brief KISS (Keep It Simple, Stupid) TNC protocol framing: wraps and
 *        unwraps raw AX.25 frames for transport over a serial link.
 */

#ifndef LIB_KISS_H_
#define LIB_KISS_H_

#include <stddef.h>
#include <stdint.h>

#include "ax25.h"

/** @brief KISS frame end marker. */
#define FEND 0xC0

/** @brief KISS control escape marker. */
#define FESC 0xDB

/** @brief Transposed FEND, used after an escape byte. */
#define TFEND 0xDC

/** @brief Transposed FESC, used after an escape byte. */
#define TFESC 0xDD

/** @brief KISS command byte: unrecognized/invalid command. */
#define CMD_UNKNOWN 0xFE

/** @brief KISS command byte: data frame (the payload is a raw AX.25 frame). */
#define CMD_DATA 0x00

/** @brief KISS command byte: set TXDelay parameter. */
#define CMD_TXDELAY 0x01

/** @brief KISS command byte: set persistence parameter (p). */
#define CMD_P 0x02

/** @brief KISS command byte: set slot time parameter. */
#define CMD_SLOTTIME 0x03

/** @brief KISS command byte: set TXTail parameter. */
#define CMD_TXTAIL 0x04

/** @brief KISS command byte: set full-duplex mode. */
#define CMD_FULLDUPLEX 0x05

/** @brief KISS command byte: set hardware-specific parameters. */
#define CMD_SETHARDWARE 0x06

/** @brief KISS command byte: exit KISS mode. */
#define CMD_RETURN 0xFF

/**
 * @brief Maximum size, in bytes, of an AX.25 frame carried inside a KISS
 *        data frame. Matches ::AX25_FRAME_MAX_SIZE.
 */
#define AX25_MAX_FRAME_LEN AX25_FRAME_MAX_SIZE

/**
 * @brief Wrap a raw AX.25 frame into a KISS data frame.
 *
 * Applies FEND/FESC byte stuffing and prepends the KISS command byte.
 *
 * @param pkg Output buffer for the KISS-encoded frame; must be large enough
 *            to hold the worst case of 2 * @p len + 3 bytes.
 * @param buf Raw AX.25 frame to wrap.
 * @param len Length, in bytes, of @p buf.
 * @return Number of bytes written to @p pkg.
 */
int kiss_wrap(uint8_t *pkg, const uint8_t *buf, size_t len);

/**
 * @brief Feed one byte of an incoming KISS byte stream to the parser.
 *
 * Once a complete data frame has been received and un-escaped, it is
 * automatically queued for transmission over AX.25.
 *
 * @param sbyte Next byte received from the KISS serial stream.
 */
void kiss_serial(uint8_t sbyte);

/**
 * @brief Parse a complete, already-delimited KISS buffer.
 *
 * Removes FEND/FESC byte stuffing and the leading command byte, returning
 * the raw payload.
 *
 * @param buf Destination buffer for the decoded payload.
 * @param raw Raw KISS-encoded buffer to parse (including command byte and
 *            stuffing, without the surrounding FEND delimiters).
 * @param len Length, in bytes, of @p raw.
 * @return Length, in bytes, of the decoded payload written to @p buf.
 */
size_t kiss_parse(uint8_t *buf, const uint8_t *raw, size_t len);

#endif /* LIB_KISS_H_ */
