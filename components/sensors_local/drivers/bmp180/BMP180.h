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
 *        (temperature + barometric pressure) and the single source of truth for
 *        which GPIO pins that sensor's I2C bus occupies.
 *
 * The BMP180's SDA/SCL pins are fixed at build time here (NOT run-time
 * configurable from the web UI), because they must be excluded from every GPIO
 * <select> the web admin renders so the user can never assign a peripheral to a
 * pin the I2C bus is already using. That exclusion is driven by
 * ::bmp180_gpio_is_reserved, which every web page's GPIO picker calls: change a
 * pin here and both the driver and the web UI follow automatically.
 *
 * @see esp-idf-lib BMP180 driver:
 *      https://components.espressif.com/components/esp-idf-lib/bmp180/
 */

#ifndef BMP180_APP_CONFIG_H_
#define BMP180_APP_CONFIG_H_

#include <stdbool.h>

/* --------------------------------------------------------------------------
 * I2C pin / port assignment for the BMP180 (compile-time constants).
 *
 * These are the ONLY place the BMP180 bus pins are defined. They are applied
 * by the driver (bmp180.c -> bmp180_init_desc) and are simultaneously removed
 * from every GPIO dropdown in the web admin via bmp180_gpio_is_reserved()
 * below, so a colliding assignment is impossible to make from the UI.
 *
 * Defaults per request: GPIO21 = SDA, GPIO22 = SCL.
 * -------------------------------------------------------------------------- */
#ifndef BMP180_I2C_SDA_GPIO
#define BMP180_I2C_SDA_GPIO 21 /**< I2C SDA pin for the BMP180 bus. */
#endif

#ifndef BMP180_I2C_SCL_GPIO
#define BMP180_I2C_SCL_GPIO 22 /**< I2C SCL pin for the BMP180 bus. */
#endif

#ifndef BMP180_I2C_PORT
#define BMP180_I2C_PORT 0 /**< I2C peripheral port number used for the BMP180. */
#endif

/**
 * @brief Hardware oversampling / accuracy mode passed to bmp180_measure().
 *        Maps to esp-idf-lib's ::bmp180_mode_t. Default: standard (2 samples).
 *        0 = ultra low power, 1 = standard, 2 = high res, 3 = ultra high res.
 */
#ifndef BMP180_OVERSAMPLING_MODE
#define BMP180_OVERSAMPLING_MODE 1
#endif

/**
 * @brief True if @p gpio is one of the pins the BMP180 I2C bus permanently
 *        occupies (SDA or SCL).
 *
 * Every GPIO <select> the web admin renders (PTT pin, message-alarm pin, ...)
 * calls this and skips any pin for which it returns true, so those pins can
 * never be handed to another peripheral from the web UI. Kept as a header-only
 * inline so the web pages can use it without linking against the driver object
 * (the sensor driver may be compiled out via Kconfig while the exclusion must
 * still apply).
 *
 * @param gpio  GPIO number to test (any int; out-of-range values return false).
 * @return true if the pin belongs to the BMP180 I2C bus, false otherwise.
 */
static inline bool bmp180_gpio_is_reserved(int gpio) {
    return (gpio == BMP180_I2C_SDA_GPIO) || (gpio == BMP180_I2C_SCL_GPIO);
}

#endif /* BMP180_APP_CONFIG_H_ */
