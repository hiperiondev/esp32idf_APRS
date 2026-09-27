/**
 * @file FreeRTOS.h
 * @brief Host stand-in for the FreeRTOS base header.
 *
 * The modem public header includes it only for types used by functions the
 * host test never calls, so it provides the handful of base types and nothing
 * else.
 */

#pragma once

#include <stdint.h>

/** @brief FreeRTOS signed base type. */
typedef long BaseType_t;
/** @brief FreeRTOS unsigned base type. */
typedef unsigned long UBaseType_t;
/** @brief FreeRTOS tick count type. */
typedef uint32_t TickType_t;
