/**
 * @file esp_random.h
 * @brief Host stand-in for the ESP-IDF hardware random number generator.
 */

#pragma once

#include <stdint.h>

/**
 * @brief Return a 32-bit random value.
 *
 * Implemented by the test harness so every run is reproducible from its seed.
 *
 * @return Pseudo-random 32-bit value.
 */
uint32_t esp_random(void);
