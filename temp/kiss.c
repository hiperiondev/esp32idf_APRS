/**
 * @file kiss.c
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
 * @brief KISS TNC protocol framing implementation: escapes and unescapes raw
 * AX.25 frames for transport over a serial link and handles KISS command
 * frames.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "afsk.h"
#include "ax25.h"
#include "kiss.h"

static const char *TAG = "kiss";

static uint8_t serialBuffer[AX25_MAX_FRAME_LEN];
static size_t frame_len = 0;

static bool IN_FRAME;
static bool ESCAPE;
static uint8_t command = CMD_UNKNOWN;

uint8_t kiss_persistence = 63;

int kiss_wrap(uint8_t *pkg, const uint8_t *buf, size_t len) {
    uint8_t *ptr = pkg;

    *ptr++ = FEND;
    *ptr++ = 0x00;
    for (size_t i = 0; i < len; i++) {
        uint8_t b = buf[i];
        if (b == FEND) {
            *ptr++ = FESC;
            *ptr++ = TFEND;
        } else if (b == FESC) {
            *ptr++ = FESC;
            *ptr++ = TFESC;
        } else {
            *ptr++ = b;
        }
    }
    *ptr++ = FEND;
    return (int)(ptr - pkg);
}

static void kiss_apply_command(uint8_t cmd, uint8_t sbyte) {
    switch (cmd) {
        case CMD_TXDELAY:
            Ax25TxDelay((uint16_t)(sbyte * 10U));
            break;
        case CMD_TXTAIL:
            Ax25Config.txTailLength = (uint16_t)(sbyte * 10);
            break;
        case CMD_SLOTTIME:
            Ax25Config.quietTime = (uint16_t)(sbyte * 10);
            break;
        case CMD_P:
            kiss_persistence = sbyte;
            break;
        case CMD_FULLDUPLEX:
            afskSetFullDuplex(sbyte != 0);
            break;
        default:
            break;
    }
}

void kiss_serial(uint8_t sbyte) {
    if (IN_FRAME && sbyte == FEND && command == CMD_DATA) {
        IN_FRAME = false;
        Ax25WriteTxFrame(serialBuffer, (uint16_t)frame_len);
        Ax25TransmitBuffer();
        ESP_LOGD(TAG, "received a %u byte packet", (unsigned)frame_len);
    } else if (sbyte == FEND) {
        IN_FRAME = true;
        command = CMD_UNKNOWN;
        frame_len = 0;
    } else if (IN_FRAME && frame_len < AX25_MAX_FRAME_LEN) {
        if (frame_len == 0 && command == CMD_UNKNOWN) {
            /* only one HDLC port is supported, strip the port nibble */
            command = sbyte & 0x0F;
        } else if (command == CMD_DATA) {
            if (sbyte == FESC) {
                ESCAPE = true;
            } else {
                if (ESCAPE) {
                    if (sbyte == TFEND)
                        sbyte = FEND;
                    if (sbyte == TFESC)
                        sbyte = FESC;
                    ESCAPE = false;
                }
                serialBuffer[frame_len++] = sbyte;
            }
        } else {
            kiss_apply_command(command, sbyte);
        }
    }
}

size_t kiss_parse(uint8_t *buf, const uint8_t *raw, size_t len) {
    uint8_t sbyte;
    size_t out_len = 0;
    bool inFrame = false;
    bool esc = false;
    uint8_t cmd = CMD_UNKNOWN;

    for (size_t i = 0; i < len; i++) {
        sbyte = raw[i];
        if (inFrame && sbyte == FEND && cmd == CMD_DATA)
            return out_len;

        if (sbyte == FEND) {
            inFrame = true;
            cmd = CMD_UNKNOWN;
            out_len = 0;
        } else if (inFrame && out_len < AX25_MAX_FRAME_LEN) {
            if (out_len == 0 && cmd == CMD_UNKNOWN) {
                cmd = sbyte & 0x0F;
            } else if (cmd == CMD_DATA) {
                if (sbyte == FESC) {
                    esc = true;
                } else {
                    if (esc) {
                        if (sbyte == TFEND)
                            sbyte = FEND;
                        if (sbyte == TFESC)
                            sbyte = FESC;
                        esc = false;
                    }
                    buf[out_len++] = sbyte;
                }
            } else {
                kiss_apply_command(cmd, sbyte);
            }
        }
    }
    return out_len;
}
