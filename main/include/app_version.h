/**
 * @file app_version.h
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
 * @brief Firmware version numbers (MAYOR.MINOR.PATCH) and the derived
 * human-readable version/project strings shown on the web admin "About" page.
 */

#ifndef APP_VERSION_H
#define APP_VERSION_H

/**
 * @def FIRMWARE_VERSION_MAYOR
 * @brief Indicate a really big change that can cause a incompatibilities with previous versions.
 */
#define FIRMWARE_VERSION_MAYOR 1

/**
 * @def FIRMWARE_VERSION_MINOR
 * @brief Indicate some change on API or opcode or very important correction in functionality
 */
#define FIRMWARE_VERSION_MINOR 0

/**
 * @def FIRMWARE_VERSION_PATCH
 * @brief Indicate some minor change or correction
 */
#define FIRMWARE_VERSION_PATCH 0

/**
 * @def FIRMWARE_PROJECT
 * @brief Firmware project information
 */
#define FIRMWARE_PROJECT "esp32idf_APRS [https://github.com/hiperiondev/esp32idf_APRS]"

/**
 * @def FIRMWARE_INFO
 * @brief Human-readable "MAYOR.MINOR.PATCH" firmware version string, built
 * from FIRMWARE_VERSION_MAYOR / FIRMWARE_VERSION_MINOR / FIRMWARE_VERSION_PATCH.
 * Used by the webconfig "About" page to show the running firmware version.
 */
/**
 * @def APP_VERSION_STR_HELPER
 * @brief Stringizing helper: turns its argument into a string literal.
 */
#define APP_VERSION_STR_HELPER(x) #x
/**
 * @def APP_VERSION_STR
 * @brief Expand @p x (so a macro value, not its name, is stringized) and then
 * stringize it. Used to build ::FIRMWARE_INFO from the numeric version macros.
 */
#define APP_VERSION_STR(x) APP_VERSION_STR_HELPER(x)
/**
 * @def FIRMWARE_INFO
 * @brief Full firmware version as a compile-time string literal,
 * "MAYOR.MINOR.PATCH", assembled from the three numeric version macros.
 *
 * Being a literal rather than a runtime concatenation, it can be embedded
 * directly in log lines, HTTP responses and APRS status text.
 */
#define FIRMWARE_INFO APP_VERSION_STR(FIRMWARE_VERSION_MAYOR) "." APP_VERSION_STR(FIRMWARE_VERSION_MINOR) "." APP_VERSION_STR(FIRMWARE_VERSION_PATCH)

#endif // APP_VERSION_H
