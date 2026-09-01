// @file heap_monitor.c
//
// @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
// @date 2026
// @copyright GNU General Public License v3
// @see https://github.com/hiperiondev/esp32idf_APRS
//
// @note
// This is based on other projects:
//     VP-Digi: https://github.com/sq8vps/vp-digi
//     ESP32APRS: https://github.com/nakhonthai/ESP32APRS_Audio
//     LibAPRS: https://github.com/markqvist/LibAPRS
//
//     please contact their authors for more information.
//
// @brief Periodic heap sampling and optional integrity sweep, driven from the
// APRS service's shared 1 Hz tick. See heap_monitor.h for what the figures
// mean and why they are sampled on the healthy path.

#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"

#include "heap_monitor.h"

#if CONFIG_APRS_HEAP_REPORT_TASKS
#include "esp_heap_task_info.h"
#endif

// Both halves of this module are compile-time options, so the tag and the
// period counters only exist when at least one of them is selected. With both
// off the tick below is an empty function and its call site needs no guard.
#if CONFIG_APRS_HEAP_REPORT || CONFIG_APRS_HEAP_INTEGRITY_CHECK
static const char *TAG = "heap_monitor";
#endif

#if CONFIG_APRS_HEAP_REPORT
// Memory class the figures describe: internal 8-bit memory, which is what the
// TLS handshakes compete for and what esp_telegram_bot.c prints when one of
// them fails, so its lines and these can be read against each other. On a
// board with no PSRAM this is the whole heap.
#define HEAP_MONITOR_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

static uint32_t s_report_elapsed_s;
#endif

#if CONFIG_APRS_HEAP_INTEGRITY_CHECK
static uint32_t s_integrity_elapsed_s;
#endif

void heap_monitor_tick_1hz(void) {
#if CONFIG_APRS_HEAP_REPORT
    if (++s_report_elapsed_s >= CONFIG_APRS_HEAP_REPORT_PERIOD_S) {
        s_report_elapsed_s = 0;

        // Three numbers on one line: what exists, what the largest single
        // allocation can still be, and how low the total has ever gone. The
        // minimum is the whole-heap watermark the ESP-IDF keeps for every
        // heap registered at start-up, which is why it is read through
        // esp_get_minimum_free_heap_size() rather than per capability - a dip
        // that recovered before the next line only survives here.
        ESP_LOGI(TAG, "free=%u largest=%u minimum=%u", (unsigned)heap_caps_get_free_size(HEAP_MONITOR_CAPS),
                 (unsigned)heap_caps_get_largest_free_block(HEAP_MONITOR_CAPS), (unsigned)esp_get_minimum_free_heap_size());

#if CONFIG_APRS_HEAP_REPORT_TASKS
        // Per-task attribution of every live block, printed by the heap
        // component itself. It writes to stdout rather than through esp_log,
        // so it arrives as a table under the line above instead of carrying
        // its own timestamp and tag.
        heap_caps_print_all_task_stat_overview(NULL);
#endif
    }
#endif

#if CONFIG_APRS_HEAP_INTEGRITY_CHECK
    if (++s_integrity_elapsed_s >= CONFIG_APRS_HEAP_INTEGRITY_PERIOD_S) {
        s_integrity_elapsed_s = 0;

        // The argument asks the checker to print the address of anything it
        // finds, so the failure below is a marker in the log rather than the
        // whole diagnosis: the addresses precede it.
        if (!heap_caps_check_integrity_all(true)) {
            ESP_LOGE(TAG, "heap integrity check failed, see the corrupt addresses printed above");
        }
    }
#endif
}
