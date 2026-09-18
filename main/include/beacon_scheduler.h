/**
 * @file beacon_scheduler.h
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
 * @brief Single shared scheduler for all periodic "own-station transmit" work.
 *
 * The tracker, IGate and digipeater position beacons, the APRS weather report,
 * and the APRS bulletins each do the same thing - sleep, wake, build a packet,
 * walk the shared (float-heavy) TNC2/AX.25 TX chain, sleep again - which in a
 * task apiece would mean five large stacks (10-14 KB each) sized for that call
 * tree, even though they almost never run at the same time and the half-duplex
 * modem serialises their transmissions anyway.
 *
 * This scheduler runs all five in one task. On each pass it calls
 * every subsystem's "service" function (::beacon_service, ::weather_beacon_service,
 * ::bulletins_service), each of which transmits whatever is due and reports how
 * many seconds until it next needs servicing; the scheduler then sleeps until
 * the soonest of them. The subsystems keep their independent enable flags and
 * intervals - only the task (and its stack) is shared.
 *
 * Net effect: five stacks (~61 KB total) become one (~14 KB), freeing ~46 KB of
 * internal heap on this no-PSRAM build.
 *
 * The same task also drives the periodic Station Capabilities beacon
 * (::query_capabilities_service) and answers APRS queries (::query_service). A query answer is
 * a beacon: it runs the same builders and the same TNC2/AX.25 encode chain, so
 * it belongs on the stack sized for that tree rather than on the far smaller
 * radio RX and APRS-IS task stacks the queries themselves arrive on. Those
 * tasks queue the request and call ::beacon_scheduler_wake, which cuts this
 * task's sleep short so the answer does not wait for the next beacon to fall
 * due.
 */

#ifndef BEACON_SCHEDULER_H
#define BEACON_SCHEDULER_H

#include <stdint.h>

/**
 * @brief Start the single shared beacon/bulletin/weather scheduler task.
 *
 * Call once from application start-up, AFTER beacon_start(), weather_start()
 * and (if built) bulletins_start() have run, since those set up the state the
 * service functions read. Safe to call once.
 */
void beacon_scheduler_start(void);

/**
 * @brief Cut the scheduler's current sleep short and run a pass now.
 *
 * For work that is handed to the scheduler task because of its stack rather
 * than because it is periodic - answering an APRS query - where waiting up to
 * a full poll period would show up as a late reply. Safe to call from any
 * task, and from before ::beacon_scheduler_start (a no-op then, since the
 * scheduler's first pass is still ahead of it).
 *
 * Requests raised while the task is running rather than sleeping are latched,
 * so the pass after the current one still happens: nothing queued is missed.
 */
void beacon_scheduler_wake(void);

/**
 * @brief Apply a small pseudo-random jitter to a beacon interval.
 *
 * Beacon scheduling is otherwise deterministic, so multiple stations that all
 * pick the same round interval (e.g. WX every 600 s) tend to phase-lock and
 * collide on a shared RF channel - a classic APRS pathology. This spreads a
 * beacon's due time by +/- a few percent (esp_random()-seeded, uniform), so
 * own-station beacons de-correlate both from each other and from neighbouring
 * stations, and simultaneously-due beacons naturally drift apart over time.
 *
 * Apply it to the interval used to compute a beacon's NEXT-DUE timestamp - not
 * merely to the scheduler's sleep, which would leave the underlying due-time
 * grid deterministic and let it re-lock on the next cycle.
 *
 * @param interval_s Nominal interval in seconds.
 * @return The jittered interval, always >= 1. Intervals below 2 s are returned
 *         unchanged (nothing meaningful to spread).
 */
uint32_t beacon_scheduler_jitter(uint32_t interval_s);

#endif // BEACON_SCHEDULER_H
