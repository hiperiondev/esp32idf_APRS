/**
 * @file BME280.h
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
 * @brief Compile-time configuration for the BME280 I2C WEATHER sensor driver
 *        (temperature + relative humidity + barometric pressure), which also
 *        drives the humidity-less BMP280.
 *
 * @details
 * Only the chip-specific knobs live here: slave address, oversampling, IIR
 * filter and standby time. The bus itself - SDA/SCL pins and I2C port -
 * belongs to sensors_local_i2c.h, which every I2C sensor driver in this
 * component shares, so the BME280 sits on the same two wires as any other
 * local sensor and the web admin's reserved-pin rule needs no per-chip entry.
 *
 * The numeric values below intentionally mirror the esp-idf-lib enumerations
 * (::BMP280_Oversampling, ::BMP280_Filter, ::BMP280_StandbyTime) rather than
 * naming them, so this header stays dependency-free and can be read without
 * the managed component present. That component is named after the BMP280 and
 * covers both parts, which is why its symbols keep the BMP280 spelling while
 * everything belonging to this driver uses BME280.
 *
 * @see esp-idf-lib BMP280 driver (drives the BME280 too):
 *      https://components.espressif.com/components/esp-idf-lib/bmp280/
 */

#ifndef BME280_APP_CONFIG_H_
#define BME280_APP_CONFIG_H_

#include "sensors_local_i2c.h"

/**
 * @brief I2C slave address of the sensor. 0x76 when the SDO pin is tied low
 *        (the wiring of most breakout boards), 0x77 when it is tied high.
 */
#ifndef BME280_I2C_ADDR
#define BME280_I2C_ADDR 0x76
#endif

/**
 * @name Measurement settings applied at bring-up
 *
 * Passed to @c bmp280_init() as a ::bmp280_params_t. The defaults are the
 * esp-idf-lib defaults, which suit a station polled once per second: the chip
 * free-runs in NORMAL mode so every read returns the newest finished
 * conversion instead of waiting for one.
 * @{
 */
/** @brief Pressure oversampling: 0 = skip, 1 = x1, 2 = x2, 3 = x4, 4 = x8, 5 = x16. */
#ifndef BME280_PRESSURE_OVERSAMPLING
#define BME280_PRESSURE_OVERSAMPLING 3
#endif

/** @brief Temperature oversampling, same scale as ::BME280_PRESSURE_OVERSAMPLING. */
#ifndef BME280_TEMPERATURE_OVERSAMPLING
#define BME280_TEMPERATURE_OVERSAMPLING 3
#endif

/** @brief Humidity oversampling, same scale as ::BME280_PRESSURE_OVERSAMPLING. Ignored by a BMP280, which has no humidity element. */
#ifndef BME280_HUMIDITY_OVERSAMPLING
#define BME280_HUMIDITY_OVERSAMPLING 3
#endif

/** @brief Internal IIR filter coefficient: 0 = off, 1 = 2, 2 = 4, 3 = 8, 4 = 16. */
#ifndef BME280_IIR_FILTER
#define BME280_IIR_FILTER 0
#endif

/** @brief Standby time between conversions in NORMAL mode: 0 = 0.5 ms, 1 = 62.5 ms, 2 = 125 ms, 3 = 250 ms, 4 = 500 ms, 5 = 1 s, 6 = 2 s, 7 = 4 s. */
#ifndef BME280_STANDBY_TIME
#define BME280_STANDBY_TIME 3
#endif
/** @} */

#endif /* BME280_APP_CONFIG_H_ */
