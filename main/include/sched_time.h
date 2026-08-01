/**
 * @file sched_time.h
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
 * @brief Time base and interval policy shared by every periodic transmitter
 * driven by the beacon scheduler (position/status beacons, bulletins,
 * objects/items, weather, telemetry).
 *
 * Two rules that all of them have to follow identically, so they live in one
 * place: cadence is measured on the monotonic clock rather than the wall clock,
 * and a configured interval is bounded by that service's own floor with 0
 * meaning "use the default".
 *
 * This header is deliberately implementation-only (static inline, no .c file)
 * so it can be included from `main/` and from every component that already has
 * `main/include` on its include path, without adding a link dependency.
 */

#ifndef SCHED_TIME_H
#define SCHED_TIME_H

#include <stdint.h>

#include "esp_timer.h"

/**
 * @brief Monotonic seconds since boot.
 *
 * Scheduling deadlines are computed on this clock, never on time(), so an NTP
 * step of the wall clock never disturbs the transmit cadence. Services that
 * also need wall-clock time (bulletin expiry, object timestamps) keep using
 * time() for that and this for "when is the next transmission due".
 *
 * @return Seconds elapsed since boot.
 */
static inline int64_t sched_mono_seconds(void) {
    return (int64_t)(esp_timer_get_time() / 1000000);
}

/**
 * @brief Bound a configured transmit interval into [@p min_s, ...].
 *
 * A value of 0 means "not configured" everywhere in the web forms, so it
 * selects @p default_s rather than a zero-second interval; anything else below
 * @p min_s is raised to the floor, so a mis-typed or imported value cannot turn
 * a service into a continuous transmitter.
 *
 * @param interval_s Configured interval in seconds (0 = use the default).
 * @param min_s      Sanity floor for this service, in seconds.
 * @param default_s  Interval to use when @p interval_s is 0, in seconds.
 * @return The interval to schedule with, in seconds.
 */
static inline uint32_t sched_clamp_interval(uint32_t interval_s, uint32_t min_s, uint32_t default_s) {
    if (interval_s == 0)
        return default_s;
    if (interval_s < min_s)
        return min_s;
    return interval_s;
}

#endif /* SCHED_TIME_H */
