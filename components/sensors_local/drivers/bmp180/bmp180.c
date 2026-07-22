/**
 * @file bmp180.c
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
 * @brief Real WEATHER ::sensor_local_driver_t for the Bosch BMP180 digital
 *        barometric-pressure / temperature sensor over I2C.
 *
 * Advertises only ::SENSOR_LOCAL_DATA_WEATHER and, on every call, reads the
 * BMP180 and writes ambient temperature and barometric pressure straight into
 * ::weather_telemetry_data_t::weather[0], setting the matching
 * ::aprs_weather_sensor_id_t enabled flags. The I2C pins are fixed at build
 * time in BMP180.h (default GPIO21=SDA, GPIO22=SCL) and are excluded from every
 * web-admin GPIO picker via bmp180_gpio_is_reserved().
 *
 * Uses the esp-idf-lib BMP180 driver:
 *     https://components.espressif.com/components/esp-idf-lib/bmp180/
 */

#include <string.h>

#include "esp_log.h"
#include "esp_err.h"

/* Angle brackets on purpose: skip the including file's own directory so the
 * esp-idf-lib "bmp180.h" is picked from the managed component and never clashes
 * with our sibling "BMP180.h" on a case-insensitive host filesystem. */
#include <bmp180.h>   /* esp-idf-lib managed component driver (lower-case) */
#include <i2cdev.h>   /* esp-idf-lib i2cdev, required once before init_desc */

#include "sensors_local.h"
#include "BMP180.h"   /* our compile-time pin/port config (upper-case)      */

#ifdef CONFIG_SENSORS_LOCAL_BMP180_DRIVER

static const char *TAG = "sensor_bmp180";

typedef struct {
    bmp180_dev_t dev; /**< esp-idf-lib device descriptor. */
    bool i2cdev_up;   /**< i2cdev_init() has been done. */
} bmp180_ctx_t;

static bmp180_ctx_t s_ctx;

static esp_err_t bmp180_drv_init(sensor_local_driver_t *self) {
    bmp180_ctx_t *c = (bmp180_ctx_t *)self->ctx;
    memset(&c->dev, 0, sizeof(c->dev));

    // i2cdev keeps a per-port mutex; it must be initialised once before any
    // descriptor is created. Safe to call again on a re-register.
    esp_err_t err = i2cdev_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2cdev_init failed: %s", esp_err_to_name(err));
        return err;
    }
    c->i2cdev_up = true;

    err = bmp180_init_desc(&c->dev, BMP180_I2C_PORT, (gpio_num_t)BMP180_I2C_SDA_GPIO, (gpio_num_t)BMP180_I2C_SCL_GPIO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bmp180_init_desc failed: %s", esp_err_to_name(err));
        return err;
    }

    err = bmp180_init(&c->dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bmp180_init failed (sensor present on SDA=%d SCL=%d?): %s", BMP180_I2C_SDA_GPIO, BMP180_I2C_SCL_GPIO, esp_err_to_name(err));
        bmp180_free_desc(&c->dev);
        return err;
    }

    ESP_LOGI(TAG, "BMP180 brought up on I2C%d (SDA=%d, SCL=%d)", BMP180_I2C_PORT, BMP180_I2C_SDA_GPIO, BMP180_I2C_SCL_GPIO);
    return ESP_OK;
}

/* The one entry the framework calls. kind is already masked to WEATHER. */
static esp_err_t bmp180_drv_save(sensor_local_driver_t *self, weather_telemetry_data_t *data, sensor_local_data_kind_t kind) {
    bmp180_ctx_t *c = (bmp180_ctx_t *)self->ctx;

    if (!(kind & SENSOR_LOCAL_DATA_WEATHER) || data->weather == NULL || data->weather_qty < 1)
        return ESP_OK;

    float temperature_c = 0.0f;   // degrees Celsius
    uint32_t pressure_pa = 0;     // pascals

    esp_err_t err = bmp180_measure(&c->dev, &temperature_c, &pressure_pa, (bmp180_mode_t)BMP180_OVERSAMPLING_MODE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bmp180_measure failed: %s", esp_err_to_name(err));
        return err; // one driver's error is logged and skipped by the caller
    }

    aprs_weather_report_t *wx = &data->weather[0];

    // Temperature: APRS carries whole degrees Fahrenheit. C -> F, rounded.
    float temperature_f = temperature_c * 9.0f / 5.0f + 32.0f;
    wx->temperature_f = (int16_t)(temperature_f >= 0.0f ? temperature_f + 0.5f : temperature_f - 0.5f);
    wx->enabled[APRS_WX_SENSOR_TEMPERATURE] = true;

    // Barometric pressure: APRS stores tenths of a millibar/hPa. 1 mb = 100 Pa,
    // so tenths-of-mb = Pa / 10.
    wx->barometric_pressure_tenths_mb = (uint32_t)((pressure_pa + 5) / 10);
    wx->enabled[APRS_WX_SENSOR_BAROMETRIC_PRESSURE] = true;

    ESP_LOGD(TAG, "BMP180: %.1f C (%d F), %u Pa (%u tenths-mb)", temperature_c, (int)wx->temperature_f, (unsigned)pressure_pa,
             (unsigned)wx->barometric_pressure_tenths_mb);

    return ESP_OK;
}

static void bmp180_drv_deinit(sensor_local_driver_t *self) {
    bmp180_ctx_t *c = (bmp180_ctx_t *)self->ctx;
    bmp180_free_desc(&c->dev);
}

static sensor_local_driver_t bmp180_driver = {
    .name = "bmp180",
    .capabilities = SENSOR_LOCAL_DATA_WEATHER,
    .init = bmp180_drv_init,
    .save = bmp180_drv_save,
    .deinit = bmp180_drv_deinit,
    .ctx = &s_ctx,
};

SENSORS_LOCAL_DRIVER_AUTOREGISTER(bmp180_driver);

#endif /* CONFIG_SENSORS_LOCAL_BMP180_DRIVER */
