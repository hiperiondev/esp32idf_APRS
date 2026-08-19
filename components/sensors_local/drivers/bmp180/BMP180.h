/**
 * @file BMP180.h
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
 * @brief Compile-time configuration for the BMP180 I2C WEATHER sensor driver
 *        (temperature + barometric pressure).
 *
 * @details
 * Only the chip-specific knob lives here: the hardware oversampling mode. The
 * bus itself - SDA/SCL pins and I2C port - belongs to sensors_local_i2c.h,
 * which every I2C sensor driver in this component shares, so the BMP180 sits on
 * the same two wires as any other local sensor and the web admin's reserved-pin
 * rule needs no per-chip entry.
 *
 * @see esp-idf-lib BMP180 driver:
 *      https://components.espressif.com/components/esp-idf-lib/bmp180/
 */

#ifndef BMP180_APP_CONFIG_H_
#define BMP180_APP_CONFIG_H_

#include "sensors_local_i2c.h"

/**
 * @brief Hardware oversampling / accuracy mode passed to bmp180_measure().
 *        Maps to esp-idf-lib's ::bmp180_mode_t. Default: standard (2 samples).
 *        0 = ultra low power, 1 = standard, 2 = high res, 3 = ultra high res.
 */
#ifndef BMP180_OVERSAMPLING_MODE
#define BMP180_OVERSAMPLING_MODE 1
#endif

#endif /* BMP180_APP_CONFIG_H_ */
