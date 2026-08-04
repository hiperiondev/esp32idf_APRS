/**
 * @file digirepeater.h
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
 * @brief APRS digipeater (WIDEn-N / TRACEn-N / RELAY / ECHO / GATE) component.
 *
 * Pure C, ESP-IDF. Reads its callsign/SSID from g_config
 * (app_config_t, see main/include/app_config.h) so the web admin ("Digi"
 * page) is the single source of truth for configuration.
 */

#ifndef DIGIREPEATER_H
#define DIGIREPEATER_H

#include "ax25.h"

/**
 * @brief Process one received AX.25 frame through the digipeater path logic.
 *
 * @param packet Decoded frame (as produced by ax25_decode()). Modified in place
 *               when the path needs to be rewritten (new-N decrement, callsign
 *               insertion, etc).
 * A frame whose source address and information field match one this
 * digipeater already repeated inside the duplicate-suppression window
 * (g_config.dup_cache_timeout_ms, see isDuplicatePacketScoped()) is dropped before
 * any path work is done, so two digipeaters within earshot of each other do
 * not keep re-repeating each other's copies of the same frame.
 *
 * @return 0  - do not repeat (drop / duplicate / not for us / already relayed)
 *         1  - repeat as-is (path already carries our used call, e.g. bypass "*")
 *         2  - repeat with modified path (packet.info + rewritten header must be
 *              re-encoded and transmitted on RF)
 */
int digiProcess(ax25_msg_t *packet);

#endif // DIGIREPEATER_H
