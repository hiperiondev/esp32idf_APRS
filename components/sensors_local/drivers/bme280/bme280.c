// @file bme280.c
//
// @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
// @date 2026
// @copyright GNU General Public License v3
// @see https://github.com/hiperiondev/esp32idf_APRS
//
// @note
// This is based on other projects:
//     VP-Digi: https://github.com/sq8vps/vp-digi
//     ESP32APRS: https://github.com/nakhonthai/ESP32APRS_Audio
//     LibAPRS: https://github.com/markqvist/LibAPRS
//
//     please contact their authors for more information.
//
// @brief Real WEATHER ::sensor_local_driver_t for the Bosch BME280 digital
//        humidity / barometric-pressure / temperature sensor over I2C.
//
// Advertises only ::SENSOR_LOCAL_DATA_WEATHER and, on every call, reads the
// sensor and writes the decoded values straight into
// ::weather_telemetry_data_t::weather[0], setting the matching
// ::aprs_weather_sensor_id_t enabled flags. The chip runs free in NORMAL mode,
// so each call returns the newest finished conversion without blocking on one.
//
// Two chip variants share this driver, because they share a register map and
// differ only in their chip ID: the BME280 carries temperature, humidity and
// pressure elements, and the humidity-less BMP280 carries the other two.
// Bring-up reads the ID off the real device and points
// ::sensor_local_driver_t::properties at the descriptor for that variant, so
// the Weather page's per-field "Channel" picker offers a Humidity source only
// on a board that can actually measure it, and read time asks for humidity
// only when the element is there.
//
// The bus is the shared sensor I2C bus of sensors_local_i2c.h (default
// GPIO21=SDA, GPIO22=SCL), whose pins are excluded from every web-admin GPIO
// picker; only the chip-specific settings come from BME280.h.
//
// Uses the esp-idf-lib BMP280 driver, which is named after the BMP280 and
// covers both parts - hence the BMP280 spelling on every symbol reached
// through it:
//     https://components.espressif.com/components/esp-idf-lib/bmp280/

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include <bmp280.h> // esp-idf-lib managed component driver, drives the BME280 too
#include <i2cdev.h> // esp-idf-lib i2cdev, required once before init_desc

#include "BME280.h"            // our compile-time chip config
#include "bme280_properties.h" // fine-grained Weather field capability descriptor
#include "sensors_local.h"
#include "sensors_local_i2c.h" // shared bus pins / port

#ifdef CONFIG_SENSORS_LOCAL_BME280_DRIVER

static const char *TAG = "sensor_bme280";

typedef struct {
    bmp280_t dev;      // < esp-idf-lib device descriptor.
    bool i2cdev_up;    // < i2cdev_init() has been done.
    bool has_humidity; // < Fitted chip is a BME280, so it carries a humidity element.
} bme280_ctx_t;

static bme280_ctx_t s_ctx;

static esp_err_t bme280_drv_init(sensor_local_driver_t *self) {
    bme280_ctx_t *c = (bme280_ctx_t *)self->ctx;
    memset(&c->dev, 0, sizeof(c->dev));

    // i2cdev keeps a per-port mutex; it must be initialised once before any
    // descriptor is created. Safe to call again on a re-register, and safe
    // when another driver already brought the shared bus up.
    esp_err_t err = i2cdev_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2cdev_init failed: %s", esp_err_to_name(err));
        return err;
    }
    c->i2cdev_up = true;

    err = bmp280_init_desc(&c->dev, BME280_I2C_ADDR, SENSORS_LOCAL_I2C_PORT, (gpio_num_t)SENSORS_LOCAL_I2C_SDA_GPIO, (gpio_num_t)SENSORS_LOCAL_I2C_SCL_GPIO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bmp280_init_desc failed: %s", esp_err_to_name(err));
        return err;
    }

    // Continuous conversion: the reader never has to trigger and wait.
    bmp280_params_t params;
    err = bmp280_init_default_params(&params);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bmp280_init_default_params failed: %s", esp_err_to_name(err));
        bmp280_free_desc(&c->dev);
        return err;
    }
    params.mode = BMP280_MODE_NORMAL;
    params.filter = (BMP280_Filter)BME280_IIR_FILTER;
    params.oversampling_pressure = (BMP280_Oversampling)BME280_PRESSURE_OVERSAMPLING;
    params.oversampling_temperature = (BMP280_Oversampling)BME280_TEMPERATURE_OVERSAMPLING;
    params.oversampling_humidity = (BMP280_Oversampling)BME280_HUMIDITY_OVERSAMPLING;
    params.standby = (BMP280_StandbyTime)BME280_STANDBY_TIME;

    err = bmp280_init(&c->dev, &params);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bmp280_init failed (sensor present at 0x%02X on SDA=%d SCL=%d?): %s", (unsigned)BME280_I2C_ADDR, SENSORS_LOCAL_I2C_SDA_GPIO,
                 SENSORS_LOCAL_I2C_SCL_GPIO, esp_err_to_name(err));
        bmp280_free_desc(&c->dev);
        return err;
    }

    // Publish the descriptor for the variant actually found, replacing the
    // placeholder in the driver struct, so the Weather page offers Humidity as
    // a mappable source only on a board that carries the element. A single
    // aligned pointer store, and both descriptors are static const and valid
    // for the life of the image, so a page rendering concurrently sees one
    // whole descriptor or the other and never a half-built one.
    c->has_humidity = (c->dev.id == BME280_CHIP_ID);
    self->properties = c->has_humidity ? &bme280_properties : &bmp280_properties;

    ESP_LOGI(TAG, "%s brought up on I2C%d (SDA=%d, SCL=%d, addr 0x%02X, chip id 0x%02X)", c->has_humidity ? "BME280" : "BMP280", SENSORS_LOCAL_I2C_PORT,
             SENSORS_LOCAL_I2C_SDA_GPIO, SENSORS_LOCAL_I2C_SCL_GPIO, (unsigned)BME280_I2C_ADDR, (unsigned)c->dev.id);
    return ESP_OK;
}

// The one entry the framework calls. kind is already masked to WEATHER.
static esp_err_t bme280_drv_save(sensor_local_driver_t *self, weather_telemetry_data_t *data, sensor_local_data_kind_t kind) {
    bme280_ctx_t *c = (bme280_ctx_t *)self->ctx;

    if (!(kind & SENSOR_LOCAL_DATA_WEATHER) || data->weather == NULL || data->weather_qty < 1)
        return ESP_OK;

    float temperature_c = 0.0f; // degrees Celsius
    float pressure_pa = 0.0f;   // pascals
    float humidity_rh = 0.0f;   // percent relative humidity, BME280 only

    esp_err_t err = bmp280_read_float(&c->dev, &temperature_c, &pressure_pa, c->has_humidity ? &humidity_rh : NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bmp280_read_float failed: %s", esp_err_to_name(err));
        return err; // one driver's error is logged and skipped by the caller
    }

    aprs_weather_report_t *wx = &data->weather[0];

    // Temperature: APRS carries whole degrees Fahrenheit. C -> F, rounded.
    float temperature_f = temperature_c * 9.0f / 5.0f + 32.0f;
    wx->temperature_f = (int16_t)(temperature_f >= 0.0f ? temperature_f + 0.5f : temperature_f - 0.5f);
    wx->enabled[APRS_WX_SENSOR_TEMPERATURE] = true;

    // Barometric pressure: APRS stores tenths of a millibar/hPa. 1 mb = 100 Pa,
    // so tenths-of-mb = Pa / 10.
    wx->barometric_pressure_tenths_mb = (uint32_t)(pressure_pa / 10.0f + 0.5f);
    wx->enabled[APRS_WX_SENSOR_BAROMETRIC_PRESSURE] = true;

    // Humidity: dimensionless percent on both sides, but the APRS field holds
    // 1-100 with the on-air "00" reserved for 100, so a reading is rounded and
    // pinned inside that range.
    if (c->has_humidity) {
        int rh = (int)(humidity_rh + 0.5f);
        if (rh < 1)
            rh = 1;
        if (rh > 100)
            rh = 100;
        wx->humidity_percent = (uint8_t)rh;
        wx->enabled[APRS_WX_SENSOR_HUMIDITY] = true;
    }

    if (c->has_humidity)
        ESP_LOGD(TAG, "BME280: %.1f C (%d F), %.0f Pa (%u tenths-mb), %.1f %%RH (%u %%)", temperature_c, (int)wx->temperature_f, pressure_pa,
                 (unsigned)wx->barometric_pressure_tenths_mb, humidity_rh, (unsigned)wx->humidity_percent);
    else
        ESP_LOGD(TAG, "BMP280: %.1f C (%d F), %.0f Pa (%u tenths-mb)", temperature_c, (int)wx->temperature_f, pressure_pa,
                 (unsigned)wx->barometric_pressure_tenths_mb);

    return ESP_OK;
}

static sensor_local_driver_t bme280_driver = {
    .name = "bme280",
    .capabilities = SENSOR_LOCAL_DATA_WEATHER,
    .init = bme280_drv_init,
    .save = bme280_drv_save,
    // Pre-bring-up placeholder: the part this driver is named for. init reads
    // the real chip ID and narrows this to bmp280_properties if the board
    // turns out to carry the humidity-less sibling.
    .properties = &bme280_properties,
    .ctx = &s_ctx,
};

SENSORS_LOCAL_DRIVER_AUTOREGISTER(bme280_driver);

#endif // CONFIG_SENSORS_LOCAL_BME280_DRIVER
