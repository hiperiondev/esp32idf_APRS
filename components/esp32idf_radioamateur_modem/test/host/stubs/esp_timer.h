/**
 * @file esp_timer.h
 * @brief Host stand-in for the ESP-IDF high-resolution timer.
 */

#pragma once

#include <stdint.h>

/**
 * @brief Return the time since boot.
 *
 * Implemented by the test harness as a simulated clock that the harness
 * advances explicitly.
 *
 * @return Microseconds since the simulated boot.
 */
int64_t esp_timer_get_time(void);
