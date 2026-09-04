/**
 * @file heap_monitor.h
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
 * @brief Standing heap instrumentation: a periodic line describing the
 * allocator on the healthy path, and an optional integrity sweep.
 *
 * @details Every other heap figure this firmware logs - the two lines in the
 * Telegram transport, the diagnosis detail of telegram_app.c, the write-error
 * path of json_store.h - is printed *after* a failure. Those numbers say what
 * the heap looked like once the damage was done; they say nothing about what
 * the device was doing while it got there, so a watermark that fell at three
 * in the morning leaves no trace at all.
 *
 * This module is the missing half: a sample of the healthy path, taken on a
 * fixed period from the APRS service's shared 1 Hz tick, carrying the three
 * figures that describe the allocator between them.
 *
 *  - The free size is how much memory exists in total.
 *  - The largest free block is the biggest single allocation still possible,
 *    and the one that decides whether a TLS handshake can build its record
 *    buffers. Read against the free size it is also the measure of
 *    fragmentation: the two figures drifting apart is a heap breaking up,
 *    which is a different fault from a heap being consumed.
 *  - The minimum free size is the low watermark since boot, which is the only
 *    figure that survives a transient - a dip that recovered before the next
 *    line still shows here.
 *
 * The figures are reported for internal 8-bit memory, the same class the TLS
 * handshakes compete for and the same class the transport prints on failure,
 * so a line from this module and a line from esp_telegram_bot.c can be read
 * side by side. On a board with no PSRAM that is the whole heap anyway.
 *
 * Both halves are compile-time options (see main/Kconfig.projbuild, menu
 * "APRS heap instrumentation"): the periodic line is on by default because it
 * costs three allocator queries per period and no memory; the integrity sweep
 * is off by default because it blocks every other task's allocations while it
 * walks each heap. With both turned off this module compiles to an empty
 * function, so its caller never needs a guard of its own.
 *
 * @details A third, unrelated piece of shared state lives here too: the
 * "heavy network op" lock. A free-heap floor checked immediately before an
 * allocation only rules out one thing - that this task, alone, is about to
 * run the heap dry. It says nothing about a second task passing the same
 * kind of check a moment later and landing on top of it, because two
 * independent point-in-time snapshots do not add up to mutual exclusion -
 * only serialization does. On this firmware the Telegram bot's TLS handshake
 * and the APRS-IS uplink's TCP connect are exactly that pair: both are rare,
 * both peak sharply while they set up, and neither has any reason to run at
 * the same instant as the other. The lock makes that serialization explicit
 * instead of leaving it to chance.
 *
 * Both sides take it with a zero-tick wait and simply defer to their own next
 * retry pass if it is already held, so neither ever blocks the other, and a
 * task that dies while holding it (there is no such path today) would wedge
 * the loser into permanent deferral rather than a crash - a failure mode
 * worth naming even though nothing here currently produces it.
 */

#ifndef HEAP_MONITOR_H
#define HEAP_MONITOR_H

#include <stdbool.h>

/**
 * @brief Advance the heap instrumentation one step. Must be called once per
 * second (from serviceTickTask in aprs_service.c).
 *
 * @details Non-blocking in the configuration that ships: it increments the
 * period counters and, on the pass where one of them comes due, queries the
 * allocator three times and emits a single log line. A pass that is not due
 * does nothing at all.
 *
 * The one case where this call is not cheap is when the integrity sweep is
 * enabled for diagnosis: on the pass where that comes due, the sweep walks
 * every heap while holding its lock, so allocations made by other tasks wait
 * for it. That is a deliberate trade of a few milliseconds of latency for the
 * ability to rule corruption out, and it is why the sweep is a separate,
 * default-off option on a period of its own.
 *
 * Safe to call before any other subsystem is up: it reads allocator state
 * only and keeps no state beyond its own counters.
 */
void heap_monitor_tick_1hz(void);

/**
 * @brief Create the "heavy network op" lock, available for one caller to take.
 *
 * @details Must be called once, before either of the lock's consumers can run
 * - aprs_service_start() does this at the top of its own body, ahead of
 * igate_start() and of the Telegram bring-up path that main.c's app_task()
 * enables further down the boot sequence. A second call is a no-op: the
 * semaphore, once created, is never recreated or destroyed for the life of
 * the firmware.
 *
 * Safe to call before any other subsystem is up: it only creates a FreeRTOS
 * binary semaphore and leaves it given (available).
 */
void heap_monitor_init(void);

/**
 * @brief Try to take the "heavy network op" lock without blocking.
 *
 * @details Backed by a FreeRTOS binary semaphore taken with a zero-tick
 * wait, so a caller that finds the lock already held returns immediately
 * instead of waiting for the holder to finish - the caller's own retry/
 * backoff loop is what tries again, on its own schedule, not this call.
 *
 * @return true if the lock was free and is now held by this caller; false if
 * another caller already holds it.
 */
bool heap_monitor_try_heavy_op(void);

/**
 * @brief Release the "heavy network op" lock.
 *
 * @details Must be called exactly once for every call to
 * heap_monitor_try_heavy_op() that returned true, as soon as the heavy setup
 * work is done - whether it succeeded or failed - so the lock only needs to
 * be held for the handshake/connect window, never for the life of whatever
 * session comes out of it.
 */
void heap_monitor_release_heavy_op(void);

#endif // HEAP_MONITOR_H
