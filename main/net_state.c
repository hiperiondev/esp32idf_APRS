/**
 * @file net_state.c
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
 * @brief Atomic cross-task connectivity flag: tracks whether the station
 * interface currently holds an IP address, so tasks needing real internet access
 * can gate on it.
 */

#include <stdatomic.h>

#include "net_state.h"

static atomic_bool s_connected = false;

void net_state_init(void) {
    atomic_store(&s_connected, false);
}

void net_state_set_connected(bool connected) {
    atomic_store(&s_connected, connected);
}

bool net_state_is_connected(void) {
    return atomic_load(&s_connected);
}
