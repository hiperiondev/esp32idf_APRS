/**
 * @file sensors_local_i2c.h
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
 * @brief Single source of truth for the ONE I2C bus every local sensor driver
 *        shares, and for the GPIO pins that bus permanently occupies.
 *
 * @details
 * All I2C sensor drivers under @c components/sensors_local/drivers (BME280,
 * BMP180, ...) sit on the same physical two-wire bus, so the pins and the port
 * number are declared here once instead of once per driver. Enabling a
 * different driver in Kconfig therefore never moves the bus, and two drivers
 * enabled at the same time address the same wires - which is exactly what an
 * I2C bus is for, since each chip answers on its own slave address.
 *
 * The pins are fixed at build time (NOT run-time configurable from the web UI)
 * because they must be excluded from every GPIO @c \<select\> the web admin
 * renders, so an operator can never assign a peripheral to a pin the bus is
 * already using. That exclusion is driven by
 * ::sensors_local_i2c_gpio_is_reserved, which the web pages call: change a pin
 * here and both the drivers and the web UI follow automatically.
 *
 * This header is deliberately dependency-free (no ESP-IDF, no driver headers)
 * so consumers that only need the reserved-pin rule - the webconfig GPIO
 * registry and the message-alarm pin validator - can include it without
 * pulling in any I2C machinery, and so the rule still applies when every I2C
 * driver has been compiled out through Kconfig.
 */

#ifndef SENSORS_LOCAL_I2C_H_
#define SENSORS_LOCAL_I2C_H_

#include <stdbool.h>

/**
 * @name Shared I2C bus pin / port assignment (compile-time constants)
 *
 * The ONLY place the sensor bus is defined. Applied by every I2C driver's
 * @c init (through its @c *_init_desc call) and simultaneously removed from
 * every GPIO dropdown in the web admin via ::sensors_local_i2c_gpio_is_reserved
 * below, so a colliding assignment is impossible to make from the UI.
 * Defaults: GPIO21 = SDA, GPIO22 = SCL, I2C port 0.
 * @{
 */
#ifndef SENSORS_LOCAL_I2C_SDA_GPIO
#define SENSORS_LOCAL_I2C_SDA_GPIO 21 /**< I2C SDA pin shared by every local sensor driver. */
#endif

#ifndef SENSORS_LOCAL_I2C_SCL_GPIO
#define SENSORS_LOCAL_I2C_SCL_GPIO 22 /**< I2C SCL pin shared by every local sensor driver. */
#endif

#ifndef SENSORS_LOCAL_I2C_PORT
#define SENSORS_LOCAL_I2C_PORT 0 /**< I2C peripheral port number the sensor bus lives on. */
#endif
/** @} */

/**
 * @brief True if @p gpio is one of the pins the shared sensor I2C bus
 *        permanently occupies (SDA or SCL).
 *
 * Every GPIO @c \<select\> the web admin renders (PTT pin, message-alarm pin,
 * ...) consults this - directly, or through the GPIO registry in
 * web_gpio_collect_used() - and skips any pin for which it returns true, so
 * those pins can never be handed to another peripheral from the web UI. Kept
 * as a header-only inline so consumers can use it without linking against any
 * driver object: the sensor drivers may all be compiled out via Kconfig while
 * the exclusion must still apply, because the bus is a property of the board's
 * wiring rather than of any one chip on it.
 *
 * @param gpio  GPIO number to test (any int; values that match neither pin
 *              return false).
 * @return true if the pin belongs to the shared sensor I2C bus, false otherwise.
 */
static inline bool sensors_local_i2c_gpio_is_reserved(int gpio) {
    return (gpio == SENSORS_LOCAL_I2C_SDA_GPIO) || (gpio == SENSORS_LOCAL_I2C_SCL_GPIO);
}

#endif /* SENSORS_LOCAL_I2C_H_ */
