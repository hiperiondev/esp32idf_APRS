/**
 * @file esp_err.h
 * @brief Host stand-in for the ESP-IDF error type.
 *
 * Provides only the type and the two values the modem headers reference.
 */

#pragma once

#include <stdint.h>

/** @brief ESP-IDF status code. */
typedef int esp_err_t;

/** @brief Success. */
#define ESP_OK 0
/** @brief Generic failure. */
#define ESP_FAIL -1
