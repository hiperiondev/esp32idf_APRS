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
 * @brief Standing memory instrumentation: a periodic line describing the
 * allocator on the healthy path, a bracket around the passages that move it
 * most, a slow report of task stack headroom, and an optional integrity
 * sweep.
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
 *  - The minimum, logged as @c min_sum, is the only figure that survives a
 *    transient: a dip that recovered before the next line still shows there.
 *
 * @note @c min_sum is a sum of minimums, not the minimum of the sum, and the
 * distinction decides how it may be read. The allocator keeps a watermark per
 * registered heap, and this figure adds them up - each one taken at that
 * heap's own worst instant, which is generally not the same instant as any
 * other's. An ESP32 without PSRAM does not have one DRAM heap: ROM and PHY
 * reservations split the internal DRAM into several non-contiguous regions,
 * each registered separately, so the sum is taken over three or four terms.
 *
 * Since the terms are independent, the sum is a *lower bound* on the smallest
 * the total free heap has ever actually been, and the bound loosens as the
 * number of registered heaps grows. A tiny @c min_sum therefore has two
 * readings that this line alone cannot tell apart: every region was near
 * empty at one moment, or each region bottomed out separately at a moment of
 * its own and the total was never in danger. Both matter - a region stuck near
 * zero is a real fragmentation fault, because heap_caps_malloc() will skip it
 * from then on and the allocator behaves as though it were not there - but
 * they call for different work. Select CONFIG_APRS_HEAP_REPORT_PER_HEAP to
 * separate them: the breakdown carries each heap's own minimum, so a single
 * dump says which region ran dry.
 *
 * The figures are reported for HEAP_MONITOR_CAPS, the same class the TLS
 * handshakes compete for and the same class the transport prints on failure,
 * so a line from this module and a line from esp_telegram_bot.c can be read
 * side by side. On a board with no PSRAM that is the whole heap anyway.
 *
 * Every part is a compile-time option (see main/Kconfig.projbuild, menu "APRS
 * heap instrumentation"). The periodic line and the stack report are on by
 * default because they cost a handful of queries per period and no memory;
 * the per-heap breakdown, the brackets and the integrity sweep are diagnostic
 * tools that are off until a question needs them. With all of them turned off
 * the tick below compiles to an empty function, so its caller never needs a
 * guard of its own.
 *
 * @details A fourth, unrelated piece of shared state lives here too: the
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

#include "esp_heap_caps.h"

/**
 * @brief Memory class every figure this module reports describes.
 *
 * @details Internal 8-bit memory: what a TLS handshake's record buffers, the
 * modem's DMA buffers and every ordinary malloc() on this board come out of,
 * and the class esp_telegram_bot.c prints when a handshake fails, so its lines
 * and this module's can be read against each other without converting between
 * two different totals.
 *
 * Shared through this header rather than kept private so that every place that
 * shows the operator a heap figure - the periodic line, the brackets, the
 * dashboard's system-information strip - measures the same thing. On an ESP32
 * without PSRAM this selects the same heaps as MALLOC_CAP_DEFAULT, since the
 * IRAM-only regions carry EXEC|32BIT|INTERNAL and no 8BIT; naming one class
 * explicitly is what keeps that agreement from quietly ending the day the
 * firmware is built for a part with external RAM.
 */
#define HEAP_MONITOR_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

/**
 * @brief Log the heap on one side of a named heavy allocation path.
 *
 * @details Emits the free size and the largest free block for
 * HEAP_MONITOR_CAPS, in the same shape and the same class as the lines
 * esp_telegram_bot.c prints around each API request, so brackets from either
 * side sit on one scale. Called through the HEAP_MONITOR_BRACKET() macro
 * rather than directly, so that the call sites cost nothing when the option is
 * off.
 *
 * Safe from any task and at any point in the boot sequence: it queries the
 * allocator and logs, and keeps no state.
 *
 * @param phase "before" or "after", naming which side of the passage this line
 *              was taken on.
 * @param what  Short label naming the passage, used verbatim as the start of
 *              the line so that a bracket pair can be found by grepping for
 *              it.
 */
void heap_monitor_bracket(const char *phase, const char *what);

#if CONFIG_APRS_HEAP_BRACKET
/**
 * @brief Bracket one side of a heavy allocation path.
 *
 * @details Place one call with @c "before" immediately ahead of the passage
 * and one with @c "after" immediately behind it, on every exit path, using the
 * same label for both so the pair reads as a unit. The two lines are only
 * worth their console bandwidth where the passage is genuinely large or
 * long-lived - a TLS session, a multi-kilobyte buffer, a parse tree - since
 * the point is to attribute a movement the periodic line already showed, not
 * to trace ordinary allocations.
 *
 * Compiled out entirely unless CONFIG_APRS_HEAP_BRACKET is selected, arguments
 * included, so a call site left in place costs nothing in a normal build.
 *
 * @param phase "before" or "after".
 * @param what  Short label naming the passage.
 */
#define HEAP_MONITOR_BRACKET(phase, what) heap_monitor_bracket((phase), (what))
#else
#define HEAP_MONITOR_BRACKET(phase, what) ((void)0)
#endif

/**
 * @brief Advance the memory instrumentation one step. Must be called once per
 * second (from serviceTickTask in aprs_service.c).
 *
 * @details Non-blocking in the configuration that ships: it increments the
 * period counters and, on the pass where one of them comes due, queries the
 * allocator or FreeRTOS a handful of times and emits its lines. A pass that is
 * not due does nothing at all.
 *
 * The one case where this call is not cheap is when the integrity sweep is
 * enabled for diagnosis: on the pass where that comes due, the sweep walks
 * every heap while holding its lock, so allocations made by other tasks wait
 * for it. That is a deliberate trade of a few milliseconds of latency for the
 * ability to rule corruption out, and it is why the sweep is a separate,
 * default-off option on a period of its own.
 *
 * Safe to call before any other subsystem is up: it reads allocator and task
 * state only, and keeps no state beyond its own counters and the fixed array
 * the stack report fills. The stack report lists whichever tasks are alive when
 * it runs, so one the operator has switched off simply has no line that period.
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
