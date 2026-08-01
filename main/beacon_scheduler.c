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
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"   // ENABLE_BULLETINS / ENABLE_OBJECTS_ITEMS
#include "aprs_service.h" // aprs_service_set_beacon_context()
#include "beacon.h"
#include "beacon_scheduler.h"
#include "bulletins.h"
#include "objects_items.h"
#include "telemetry.h"
#include "weather.h"

static const char *TAG = "beacon_sched";

// Stack budget for the shared task. This is the single source of truth for the
// beacon/bulletin stack size now that the per-service tasks (and their
// individual *_TASK_STACK_BYTES defines in beacon.c/bulletins.c/weather.c) are
// gone. The five services run sequentially within a pass, so the stack is
// reused between them and only the DEEPEST single call tree matters. That call
// tree is the position/WX/telemetry TX path: buildPositionPacket() /
// build_tlm_data_packet() / the WX token builder each run several snprintf()s
// including newlib's float-capable *printf (a much deeper call tree than
// integer formatting), then lat_lon_to_aprs + aprs_path_build_suffix ->
// aprs_service_send_tnc2 -> modem_send_tnc2 -> modem_build_frame_tnc2 ->
// ax25_encode/hdlcFrame, stacking several ~300-450 byte buffers per level. When
// they had separate tasks these were sized independently (WX 14336, the
// tracker/igate/digi beacons 12288, the bulletin transmitter 10240) after a
// stack overrun on the RF leg was seen to silently corrupt/truncate packets;
// sizing the one shared stack to that proven maximum keeps every path's
// headroom while replacing ~61 KB of separate stacks with a single ~14 KB one.
//
// One consumer of that headroom is invisible from here and worth naming: at
// the very bottom of the same call tree, Ax25WriteTxFrame() can decode the
// frame it just queued back into a TNC2 line for the log, which costs about a
// kilobyte of stack (an ax25_msg_t plus a 256-byte line buffer). That block is
// compiled out unless the build's maximum log level admits ESP_LOGD and is
// only executed when the "ax25" tag is actually raised to debug at run time -
// so raising it is not free here, it eats into this budget.
#define BEACON_SCHED_TASK_STACK_BYTES 14336

// Upper bound on how long the scheduler sleeps between passes. Even when every
// enabled beacon has a long interval, re-evaluating at least this often means
// web-admin edits (enable/interval toggles) take effect without a reboot - the
// same promise the individual tasks made via their 5 s idle re-checks and the
// bulletin task's 60 s poll cap.
#define BEACON_SCHED_POLL_CAP_S 30

// +/- this percent of the interval, uniformly distributed. 8% keeps a 600 s WX
// beacon within +/-48 s of its nominal cadence: enough to break phase-lock with
// a neighbour on the same round interval, small enough not to visibly shift the
// rate the operator configured.
#define BEACON_JITTER_PCT 8

uint32_t beacon_scheduler_jitter(uint32_t interval_s) {
    if (interval_s < 2)
        return interval_s; // nothing meaningful to spread on a sub-2s interval

    uint32_t dev = (interval_s * BEACON_JITTER_PCT) / 100; // magnitude of the +/- window
    if (dev == 0)
        dev = 1;

    // Uniform in [-dev, +dev]. esp_random() is the hardware RNG the loop-test
    // token already uses; here it de-correlates beacon due-times.
    int32_t delta = (int32_t)(esp_random() % (2u * dev + 1u)) - (int32_t)dev;
    int64_t v = (int64_t)interval_s + delta;
    if (v < 1)
        v = 1;
    return (uint32_t)v;
}

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

// Longest we'll hold the very first pass waiting for the RF modem to finish
// bring-up. modem_init() blocks app_task for ~5 s doing ADC sample-rate
// calibration (see the note in main.c's app_task()), and aprs_service_start()
// - which creates this task - runs *before* that call. Without this wait, the
// scheduler's first pass finds every "next due" timestamp still at its
// zero-initialised default, so all enabled beacons (tracker/igate/digi, WX,
// telemetry) fire immediately: before aprs_service_notify_modem_ready() has
// ever been called, so the RF leg logs "modem not ready or busy" for every one
// of them on every boot, and before WiFi/APRS-IS has had any chance to connect,
// so the INET leg logs "not connected" right alongside it. This cap bounds the
// wait for boards where the audio modem is disabled in config (audio_modem_en
// == false), where aprs_service_notify_modem_ready() is never called at all -
// the scheduler still needs to start beaconing eventually.
#define BEACON_SCHED_MODEM_WAIT_CAP_MS 6000
#define BEACON_SCHED_MODEM_POLL_MS     100

static void beacon_scheduler_task(void *arg) {
    (void)arg;

    // Mark this task as the periodic-beacon context. Inside it (and only it),
    // aprs_service_send_tnc2() is allowed to wait briefly for the RF TX ring to
    // drain instead of dropping when it is momentarily full - which is what
    // lets several own-station beacons that fall due in the same pass all reach
    // the air with the factory-default "TX buffers = 1", rather than the 2nd
    // and 3rd being silently discarded. Every periodic transmitter (tracker/
    // igate/digi beacons, WX, telemetry, bulletins, objects) runs in this one
    // task, so this single registration covers them all; the RX/digipeat,
    // INET2RF and message paths run in other tasks and keep their non-blocking
    // drop-if-full behavior.
    aprs_service_set_beacon_context();

    // Hold the first pass until the RF modem reports ready, or until the cap
    // above elapses (audio_modem_en disabled, or modem_init() failed - see
    // main.c's app_task(), which logs and moves on without ever calling
    // aprs_service_notify_modem_ready() in either case). This does not touch
    // the INET leg: igate.c already backs off internally on
    // net_state_is_connected() and reports "not connected" until the STA link
    // is actually up, which is expected/transient, not a bug to fix here.
    uint32_t waited_ms = 0;
    while (!aprs_service_modem_ready() && waited_ms < BEACON_SCHED_MODEM_WAIT_CAP_MS) {
        vTaskDelay(pdMS_TO_TICKS(BEACON_SCHED_MODEM_POLL_MS));
        waited_ms += BEACON_SCHED_MODEM_POLL_MS;
    }
    if (!aprs_service_modem_ready())
        ESP_LOGW(TAG, "Starting beacon schedule without RF modem ready (disabled or failed to init)");

    for (;;) {
        // Each service transmits whatever is due and returns seconds-until-next.
        uint32_t soonest = BEACON_SCHED_POLL_CAP_S;

        soonest = min_u32(soonest, beacon_service());           // tracker + igate + digi
        soonest = min_u32(soonest, weather_beacon_service());   // WX report
        soonest = min_u32(soonest, telemetry_beacon_service()); // Telemetry (Binary B1-B8) report
#ifdef ENABLE_BULLETINS
        soonest = min_u32(soonest, bulletins_service()); // BLN1..BLNn
#endif
#ifdef ENABLE_OBJECTS_ITEMS
        soonest = min_u32(soonest, objitems_service()); // APRS Objects/Items
#endif

        if (soonest < 1)
            soonest = 1;

        ESP_LOGD(TAG, "scheduler stack free: %u bytes; next pass in %us", (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)),
                 (unsigned)soonest);

        vTaskDelay(pdMS_TO_TICKS((uint32_t)soonest * 1000UL));
    }
}

void beacon_scheduler_start(void) {
    xTaskCreate(beacon_scheduler_task, "beacon_sched", BEACON_SCHED_TASK_STACK_BYTES, NULL, 4, NULL);
    ESP_LOGI(TAG, "Beacon scheduler started (one task drives tracker/igate/digi beacons, WX, and bulletins)");
}
