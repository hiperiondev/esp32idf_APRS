/**
 * @file net_state.h
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
 * @brief Tiny cross-task "do we have internet yet?" flag.
 *
 * wifi_init()/the WiFi+IP event handlers in main.c are the only writers (true on
 * IP_EVENT_STA_GOT_IP, false on WIFI_EVENT_STA_DISCONNECTED or when the
 * configured WiFi mode has no STA interface at all). Any component that needs an
 * actual route to the internet before it does anything (currently: the APRS-IS
 * IGate TCP client in igate.c) should poll net_state_is_connected() and back off
 * while it is false instead of assuming WiFi == internet.
 */

#ifndef NET_STATE_H
#define NET_STATE_H

#include <stdbool.h>

/**
 * @brief Reset the tracked connectivity state to "not connected". Call once
 * from app startup, before wifi_init().
 */
void net_state_init(void);

/**
 * @brief Update the tracked connectivity state. Safe to call from an
 * esp_event handler (ISR-free, event task context).
 */
void net_state_set_connected(bool connected);

/**
 * @brief True once the STA interface has obtained an IP address and stayed
 * up since; false before the first IP is obtained, after a disconnect, or
 * whenever the configured WiFi mode has no STA interface (AP-only).
 */
bool net_state_is_connected(void);

#endif // NET_STATE_H
