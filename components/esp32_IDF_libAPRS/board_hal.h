/*
 * board_hal.h
 *
 * Minimal, pure ESP-IDF (no Arduino) hardware-abstraction shim.
 *
 * LibAPRS was originally written against the Arduino-ESP32 core. This
 * header implements just enough of that surface (GPIO helpers, timing
 * helpers, a hw_timer_t-style periodic timer, ADC oneshot helpers and a
 * couple of misc helpers) directly on top of ESP-IDF drivers so the rest
 * of the library can be converted to plain C with minimal call-site
 * churn, WITHOUT linking against arduino-esp32 in any way.
 */
#ifndef BOARD_HAL_H_
#define BOARD_HAL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ------------------------------------------------------------------ */
/* GPIO level / mode constants (Arduino-compatible names)              */
/* ------------------------------------------------------------------ */
#define HIGH 1
#define LOW  0

#define INPUT             0x00
#define OUTPUT            0x01
#define INPUT_PULLUP      0x02
#define INPUT_PULLDOWN    0x03
#define OUTPUT_OPEN_DRAIN 0x04

/**
 * @brief Configure a GPIO pin direction/pull mode.
 */
void pinMode(int pin, int mode);

/**
 * @brief Set a GPIO pin output level.
 */
void digitalWrite(int pin, int level);

/**
 * @brief Read a GPIO pin input level.
 */
int digitalRead(int pin);

/* ------------------------------------------------------------------ */
/* Timing helpers                                                       */
/* ------------------------------------------------------------------ */

/** Milliseconds since boot (wraps like Arduino millis(), 32-bit). */
unsigned long millis(void);

/** Microseconds since boot (wraps like Arduino micros(), 32-bit). */
unsigned long micros(void);

/** Blocking delay, milliseconds (yields to the scheduler). */
void delay(unsigned long ms);

/** Blocking delay, microseconds (busy-wait, does not yield). */
void delayMicroseconds(uint32_t us);

/** Return a pseudo-random integer in [min, max) like Arduino's random(). */
long int board_random(long min, long max);

/* ------------------------------------------------------------------ */
/* Simple ADC helpers (legacy adc1 driver, ADC1 channels only)          */
/* These back analogReadMilliVolts()/analogSetPinAttenuation() used by  */
/* the single-shot ISR sampling path (ESP32 classic target only).      */
/* ------------------------------------------------------------------ */

/** Configure ADC1 resolution in bits (9-12). */
void analogReadResolution(int bits);

/** Configure attenuation (ADC_ATTEN_DB_0 .. ADC_ATTEN_DB_12) for a pin mapped to ADC1. */
void analogSetPinAttenuation(int pin, int attenuation);

/** Perform a one-shot ADC1 read on the given pin, returned already scaled to millivolts. */
int analogReadMilliVolts(int pin);

/* ------------------------------------------------------------------ */
/* hw_timer_t-style periodic timer, implemented with ESP-IDF gptimer   */
/* ------------------------------------------------------------------ */

typedef struct hw_timer_s hw_timer_t;
typedef void (*hw_timer_isr_t)(void);

/** Create (but do not start) a timer counting at resolution_hz ticks/second. */
hw_timer_t *timerBegin(uint32_t resolution_hz);

/** Attach the ISR callback to fire on every alarm. Callback runs in ISR context (IRAM). */
void timerAttachInterrupt(hw_timer_t *timer, hw_timer_isr_t fn);

/** Configure the alarm value (in timer ticks) and whether it auto-reloads. */
void timerAlarm(hw_timer_t *timer, uint64_t alarm_value, bool autoreload, uint32_t reload_count);

/** Start the timer counting/alarming. */
void timerStart(hw_timer_t *timer);

/** Stop the timer (counter halted, no more alarms). */
void timerStop(hw_timer_t *timer);

/* ------------------------------------------------------------------ */
/* Misc                                                                 */
/* ------------------------------------------------------------------ */

/** Free heap size, in bytes (replaces Arduino's ESP.getFreeHeap()). */
uint32_t board_getFreeHeap(void);

#endif /* BOARD_HAL_H_ */
