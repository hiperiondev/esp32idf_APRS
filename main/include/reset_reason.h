/**
 * @file reset_reason.h
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
 * @brief Single implementation of "turn the cause of this boot into a short
 * English label an operator can read".
 *
 * Why the station came up is asked in two unrelated places - the web admin's
 * dashboard renders it in its System Info strip, and the Telegram start-up
 * notice carries it to the operator's phone - and the two must not be able to
 * spell the same cause differently, so the wording lives here once.
 *
 * The labels are deliberately English literals rather than translated strings:
 * one of the two consumers is a Telegram message, which is composed on a task
 * that has no web request and therefore no language selection behind it, and a
 * cause reported one way on the phone and another way on the dashboard is
 * harder to act on than a cause reported in one language.
 *
 * This header is deliberately implementation-only (static inline, no .c file)
 * so it can be included from `main/` and from every component that already has
 * `main/include` on its include path, without adding a link dependency.
 */

#ifndef RESET_REASON_H
#define RESET_REASON_H

#include "esp_system.h"

/**
 * @brief Short human-readable label for one reset cause.
 *
 * @param reason Cause as reported by @c esp_reset_reason().
 *
 * @return A static, never-NULL string literal describing @p reason. Causes
 *         this target cannot report, and any cause a future ESP-IDF adds,
 *         answer "Unknown", so a caller may render the result unconditionally.
 */
static inline const char *reset_reason_label(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:
            return "Power-on";
        case ESP_RST_EXT:
            return "External pin";
        case ESP_RST_SW:
            return "Software reset";
        case ESP_RST_PANIC:
            return "Panic/exception";
        case ESP_RST_INT_WDT:
            return "Interrupt watchdog";
        case ESP_RST_TASK_WDT:
            return "Task watchdog";
        case ESP_RST_WDT:
            return "Other watchdog";
        case ESP_RST_DEEPSLEEP:
            return "Deep sleep wake";
        case ESP_RST_BROWNOUT:
            return "Brownout";
        case ESP_RST_SDIO:
            return "SDIO";
        default:
            return "Unknown";
    }
}

#endif // RESET_REASON_H
