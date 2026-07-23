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
 */

#include <stdlib.h>
#include <time.h>

#include "esp_log.h"
#include "sensors_local.h"

/* Everything below is compiled only when the driver is enabled in menuconfig
 * (Component config -> Sensors Local). This mirrors bmp180.c, and it is not
 * optional: the component is registered WHOLE_ARCHIVE, so without this guard
 * SENSORS_LOCAL_DRIVER_AUTOREGISTER() below is always linked in and the fake
 * sensor registers at boot no matter what Kconfig says.
 *
 * The properties descriptor is included inside the guard on purpose: it is a
 * file-scope `static const` object, so leaving it outside would make it an
 * unused variable (and a -Wunused-const-variable warning) in a disabled build. */
#ifdef CONFIG_SENSORS_LOCAL_WEATHER_EXAMPLE_DRIVER

#include "wx_example_properties.h" /* fine-grained Weather field capability descriptor */

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

/* The one entry the framework calls. kind is already masked to WEATHER.
 *
 * SI-first policy: every quantity is "measured" here in International
 * System (SI) units, exactly as a real sensor would report it internally
 * (m/s, degrees Celsius, hPa, millimeters). Conversion to the legacy
 * imperial units required by the fixed APRS101/WX.TXT on-air format
 * (mph, degrees Fahrenheit, tenths of a millibar, hundredths of an inch)
 * happens only right here, at the boundary where the APRS weather report
 * struct is filled - never earlier. This mirrors the pattern used by the
 * real bmp180 driver. Luminosity (W/m^2) is already SI on both sides, so
 * no conversion is needed there. */
static esp_err_t wx_example_save(sensor_local_driver_t *self, weather_telemetry_data_t *data, sensor_local_data_kind_t kind) {
    wx_example_ctx_t *c = (wx_example_ctx_t *)self->ctx;
    c->sample_count++;

    if (!(kind & SENSOR_LOCAL_DATA_WEATHER) || data->weather == NULL || data->weather_qty < 1)
        return ESP_OK;

    aprs_weather_report_t *wx = &data->weather[0];

    // --- "Measured" values in SI units ---
    uint16_t wind_direction_deg = (uint16_t)rnd(0, 359);        // degrees, SI-compatible angular unit
    float wind_sustained_ms = (float)rnd(0, 112) / 10.0f;       // 0.0-11.2 m/s (~0-25 mph)
    float wind_gust_ms = wind_sustained_ms + (float)rnd(0, 67) / 10.0f; // + up to 6.7 m/s gust
    float temperature_c = (float)rnd(-230, 405) / 10.0f;        // -23.0..+40.5 degC (~-10..+105 degF)
    uint8_t humidity_pct = (uint8_t)rnd(1, 100);                // percent, dimensionless, no conversion needed
    float pressure_hpa = (float)rnd(9500, 10500) / 10.0f;       // 950.0-1050.0 hPa (hPa numerically == mb)
    float rain_last_hour_mm = (float)rnd(0, 127) / 10.0f;       // 0.0-12.7 mm (~0-50 hundredths of an inch)
    uint16_t luminosity_wm2 = (uint16_t)rnd(0, 1200);           // W/m^2 - already SI, on-air unit matches

    // --- Convert to APRS101/WX.TXT on-air units at the boundary ---

    // Wind: APRS wind fields are fixed by spec to whole mph. m/s -> mph.
    wx->wind.direction_deg = wind_direction_deg;
    wx->wind.sustained_mph = (uint16_t)(wind_sustained_ms * 2.23694f + 0.5f);
    wx->wind.gust_mph = (uint16_t)(wind_gust_ms * 2.23694f + 0.5f);
    wx->wind.has_gust = true;
    wx->wind.direction_unknown = false;
    wx->enabled[APRS_WX_SENSOR_WIND] = true;

    // Temperature: APRS carries whole degrees Fahrenheit. C -> F, rounded.
    float temperature_f = temperature_c * 9.0f / 5.0f + 32.0f;
    wx->temperature_f = (int16_t)(temperature_f >= 0.0f ? temperature_f + 0.5f : temperature_f - 0.5f);
    wx->enabled[APRS_WX_SENSOR_TEMPERATURE] = true;

    // Humidity: already a dimensionless percent on both sides, no conversion.
    wx->humidity_percent = humidity_pct;
    wx->enabled[APRS_WX_SENSOR_HUMIDITY] = true;

    // Barometric pressure: APRS stores tenths of a millibar/hPa. 1 hPa == 1 mb.
    wx->barometric_pressure_tenths_mb = (uint32_t)(pressure_hpa * 10.0f + 0.5f);
    wx->enabled[APRS_WX_SENSOR_BAROMETRIC_PRESSURE] = true;

    // Rain last hour: APRS stores hundredths of an inch. mm -> in (1 in = 25.4 mm).
    wx->rain_last_hour_hundredths_in = (uint16_t)((rain_last_hour_mm / 25.4f) * 100.0f + 0.5f);
    wx->enabled[APRS_WX_SENSOR_RAIN_LAST_HOUR] = true;

    // Solar luminosity: APRS "LXXX"/"lXXX" is already W/m^2 - SI on both sides.
    wx->luminosity_wm2 = luminosity_wm2;
    wx->enabled[APRS_WX_SENSOR_LUMINOSITY] = true;

    ESP_LOGD(TAG, "wx-example (SI): wind %u deg %.1f m/s (gust %.1f m/s), %.1f degC, %u%%RH, %.1f hPa, %.1f mm/1h, %u W/m^2",
             wind_direction_deg, wind_sustained_ms, wind_gust_ms, temperature_c, humidity_pct, pressure_hpa, rain_last_hour_mm, luminosity_wm2);

    return ESP_OK;
}

static sensor_local_driver_t wx_example_driver = {
    .name = "wx-example",
    .capabilities = SENSOR_LOCAL_DATA_WEATHER,
    .init = wx_example_init,
    .save = wx_example_save,
    .deinit = NULL,
    .properties = &wx_example_properties,
    .ctx = &s_ctx,
};

SENSORS_LOCAL_DRIVER_AUTOREGISTER(wx_example_driver);

#endif /* CONFIG_SENSORS_LOCAL_WEATHER_EXAMPLE_DRIVER */
