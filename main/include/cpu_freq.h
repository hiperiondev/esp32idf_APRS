/**
 * @file cpu_freq.h
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
 * @brief CPU clock frequency policy.
 *
 * Applies g_config.cpuFreq (80/160/240 MHz) to the running system via
 * esp_pm_configure(), so the "CPU frequency" setting on the System web page
 * actually changes the real clock speed instead of only being stored/shown. Safe
 * to call at boot (after app_config_load()) and again any time the setting is
 * changed and saved (e.g. from page_system_post()) to apply it immediately
 * without requiring a reboot.
 */

#pragma once

// Applies g_config.cpuFreq (80/160/240 MHz) to the running system via
// esp_pm_configure(), so the "CPU frequency" setting on the System web page
// actually changes the real clock speed instead of only being stored/shown.
// Safe to call at boot (after app_config_load()) and again any time the
// setting is changed and saved (e.g. from page_system_post()) to apply it
// immediately without requiring a reboot.
void cpu_freq_apply(void);
