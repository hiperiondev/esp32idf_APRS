/**
 * @file sensor_local_weather_example.c
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
 * @brief Example WEATHER ::sensor_local_driver_t.
 *
 * Advertises only ::SENSOR_LOCAL_DATA_WEATHER and, on every call, writes a
 * fresh set of *random* weather readings straight into
 * ::weather_telemetry_data_t::weather[0], setting the matching
 * ::aprs_weather_sensor_id_t enabled flags. It exists to exercise the whole
 * pipeline (registry -> 1 Hz refresh -> WX encoder/beacon and the Weather
 * page's channel picker) with no real hardware attached. Copy it as the
 * skeleton for a real BME280 / Davis / Ultimeter driver.
 *
 * Enabled with CONFIG_SENSORS_LOCAL_WEATHER_EXAMPLE_DRIVER.
 */

#if defined(CONFIG_SENSORS_LOCAL_WEATHER_EXAMPLE_DRIVER)

#include <stdlib.h>
#include <time.h>

#include "esp_log.h"
#include "sensors_local.h"

static const char *TAG = "sensor_wx_example";

typedef struct {
    uint32_t sample_count;
} wx_example_ctx_t;

static wx_example_ctx_t s_ctx;

// Inclusive random integer in [lo, hi].
static int rnd(int lo, int hi) {
    if (hi <= lo)
        return lo;
    return lo + rand() % (hi - lo + 1);
}

static esp_err_t wx_example_init(sensor_local_driver_t *self) {
    wx_example_ctx_t *c = (wx_example_ctx_t *)self->ctx;
    c->sample_count = 0;
    srand((unsigned)time(NULL)); // seed once so values differ across boots
    ESP_LOGI(TAG, "weather example sensor brought up");
    return ESP_OK;
}

/* The one entry the framework calls. kind is already masked to WEATHER. */
static esp_err_t wx_example_save(sensor_local_driver_t *self, weather_telemetry_data_t *data, sensor_local_data_kind_t kind) {
    wx_example_ctx_t *c = (wx_example_ctx_t *)self->ctx;
    c->sample_count++;

    if (!(kind & SENSOR_LOCAL_DATA_WEATHER) || data->weather == NULL || data->weather_qty < 1)
        return ESP_OK;

    aprs_weather_report_t *wx = &data->weather[0];

    // Wind: direction 0-359 deg, sustained 0-25 mph, gust a bit above.
    wx->wind.direction_deg = (uint16_t)rnd(0, 359);
    wx->wind.sustained_mph = (uint16_t)rnd(0, 25);
    wx->wind.gust_mph = (uint16_t)(wx->wind.sustained_mph + rnd(0, 15));
    wx->wind.has_gust = true;
    wx->wind.direction_unknown = false;
    wx->enabled[APRS_WX_SENSOR_WIND] = true;

    // Temperature -10..+105 F.
    wx->temperature_f = (int16_t)rnd(-10, 105);
    wx->enabled[APRS_WX_SENSOR_TEMPERATURE] = true;

    // Humidity 1-100 %RH.
    wx->humidity_percent = (uint8_t)rnd(1, 100);
    wx->enabled[APRS_WX_SENSOR_HUMIDITY] = true;

    // Barometric pressure ~950.0 - 1050.0 mb, stored in tenths.
    wx->barometric_pressure_tenths_mb = (uint32_t)rnd(9500, 10500);
    wx->enabled[APRS_WX_SENSOR_BAROMETRIC_PRESSURE] = true;

    // Rain last hour 0-50 hundredths of an inch.
    wx->rain_last_hour_hundredths_in = (uint16_t)rnd(0, 50);
    wx->enabled[APRS_WX_SENSOR_RAIN_LAST_HOUR] = true;

    // Solar luminosity 0-1200 W/m^2.
    wx->luminosity_wm2 = (uint16_t)rnd(0, 1200);
    wx->enabled[APRS_WX_SENSOR_LUMINOSITY] = true;

    return ESP_OK;
}

static sensor_local_driver_t wx_example_driver = {
    .name = "wx-example",
    .capabilities = SENSOR_LOCAL_DATA_WEATHER,
    .init = wx_example_init,
    .save = wx_example_save,
    .deinit = NULL,
    .ctx = &s_ctx,
};

SENSORS_LOCAL_DRIVER_AUTOREGISTER(wx_example_driver);

#endif /* CONFIG_SENSORS_LOCAL_WEATHER_EXAMPLE_DRIVER */
