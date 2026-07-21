/**
 * @file beacon_scheduler.c
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
 * @brief Single task that services all periodic own-station transmissions.
 *        See beacon_scheduler.h for the rationale (five big-stack tasks folded
 *        into one to reclaim internal heap).
 */

#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h" // ENABLE_BULLETINS / ENABLE_OBJECTS_ITEMS
#include "beacon.h"
#include "beacon_scheduler.h"
#include "bulletins.h"
#include "objects_items.h"
#include "weather.h"

static const char *TAG = "beacon_sched";

// Stack budget for the shared task. The five services run sequentially within
// a pass, so the stack is reused between them and only the DEEPEST single call
// tree matters - that is the WX beacon's, which was independently sized at
// 14336 (see the old WX_BEACON_TASK_STACK_BYTES in weather.c; the tracker/igate/
// digi beacons needed 12288 and the bulletin transmitter 10240). Sizing the one
// shared stack to that maximum keeps every path's proven headroom while still
// replacing ~61 KB of separate stacks with a single ~14 KB one.
#define BEACON_SCHED_TASK_STACK_BYTES 14336

// Upper bound on how long the scheduler sleeps between passes. Even when every
// enabled beacon has a long interval, re-evaluating at least this often means
// web-admin edits (enable/interval toggles) take effect without a reboot - the
// same promise the individual tasks made via their 5 s idle re-checks and the
// bulletin task's 60 s poll cap.
#define BEACON_SCHED_POLL_CAP_S 30

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static void beacon_scheduler_task(void *arg) {
    (void)arg;
    for (;;) {
        // Each service transmits whatever is due and returns seconds-until-next.
        uint32_t soonest = BEACON_SCHED_POLL_CAP_S;

        soonest = min_u32(soonest, beacon_service());         // tracker + igate + digi
        soonest = min_u32(soonest, weather_beacon_service()); // WX report
#ifdef ENABLE_BULLETINS
        soonest = min_u32(soonest, bulletins_service());      // BLN1..BLNn
#endif
#ifdef ENABLE_OBJECTS_ITEMS
        soonest = min_u32(soonest, objitems_service());       // APRS Objects/Items
#endif

        if (soonest < 1)
            soonest = 1;

        ESP_LOGD(TAG, "scheduler stack free: %u bytes; next pass in %us",
                 (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)), (unsigned)soonest);

        vTaskDelay(pdMS_TO_TICKS((uint32_t)soonest * 1000UL));
    }
}

void beacon_scheduler_start(void) {
    xTaskCreate(beacon_scheduler_task, "beacon_sched", BEACON_SCHED_TASK_STACK_BYTES, NULL, 4, NULL);
    ESP_LOGI(TAG, "Beacon scheduler started (one task drives tracker/igate/digi beacons, WX, and bulletins)");
}
