/**
 * @file sensor_local_example.c
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
 * @brief Reference implementation of a ::sensor_local_driver_t.
 *
 * It shows the whole contract in one place: a single common entry (@ref
 * example_save) that writes *directly* into the shared
 * ::weather_telemetry_data_t, honouring the requested
 * ::sensor_local_data_kind_t, and self-registration into the dynamic registry
 * via ::SENSORS_LOCAL_DRIVER_AUTOREGISTER. Copy this file as the skeleton for a
 * real BME280 / DS18B20 / ADS1115 / ... driver.
 *
 * Disabled by default; enable with CONFIG_SENSORS_LOCAL_EXAMPLE_DRIVER
 * ("Component config -> Local sensors").
 */

#if defined(CONFIG_SENSORS_LOCAL_EXAMPLE_DRIVER)

#include "sensors_local.h"

#include "esp_log.h"

static const char *TAG = "sensor_example";

/* Driver-private state. A real driver would keep its bus handle, I2C address,
 * calibration coefficients, last-good readings, etc. here. */
typedef struct {
    uint32_t sample_count;
} example_ctx_t;

static example_ctx_t s_ctx;

/* ------------------------------------------------------------------ init */
static esp_err_t example_init(sensor_local_driver_t *self) {
    example_ctx_t *c = (example_ctx_t *)self->ctx;
    c->sample_count = 0;
    ESP_LOGI(TAG, "example sensor brought up");
    return ESP_OK;
}

/* ------------------------------------------------------- common entry: save */
/*
 * The ONE entry the framework calls. It modifies `data` in place. `kind` has
 * already been masked to what this driver advertised, so we can just test the
 * bits and fill whichever family is asked for.
 */
static esp_err_t example_save(sensor_local_driver_t *self, weather_telemetry_data_t *data, sensor_local_data_kind_t kind) {
    example_ctx_t *c = (example_ctx_t *)self->ctx;
    c->sample_count++;

    /* Pretend we just read a chip. Replace with a real bus transaction. */
    const int16_t temperature_f = 68;          /* 20 C ~= 68 F */
    const uint8_t humidity_pct = 55;           /* %RH */
    const uint32_t pressure_tenths_mb = 10132; /* 1013.2 mb */
    const double battery_volts = 3.97;         /* telemetry analog channel */

    /* --- Weather family: write straight into weather[0] --- */
    if ((kind & SENSOR_LOCAL_DATA_WEATHER) && data->weather != NULL && data->weather_qty >= 1) {
        aprs_weather_report_t *wx = &data->weather[0];

        wx->temperature_f = temperature_f;
        wx->enabled[APRS_WX_SENSOR_TEMPERATURE] = true;

        wx->humidity_percent = humidity_pct;
        wx->enabled[APRS_WX_SENSOR_HUMIDITY] = true;

        wx->barometric_pressure_tenths_mb = pressure_tenths_mb;
        wx->enabled[APRS_WX_SENSOR_BAROMETRIC_PRESSURE] = true;
    }

    /* --- Telemetry family: write straight into telemetry_report[0], A1 --- */
    if ((kind & SENSOR_LOCAL_DATA_TELEMETRY) && data->telemetry_report != NULL && data->telemetry_report_qty >= 1) {
        aprs_telemetry_report_t *tlm = &data->telemetry_report[0];

        /* Only touch a channel that the caller actually allocated. */
        if (tlm->analog != NULL && tlm->analog_enabled != NULL && tlm->analog_count >= 1) {
            tlm->analog[APRS_TLM_ANALOG_A1] = battery_volts;
            tlm->analog_enabled[APRS_TLM_ANALOG_A1] = true;
        }
    }

    return ESP_OK;
}

/* ----------------------------------------------------------- descriptor */
static sensor_local_driver_t example_driver = {
    .name = "example",
    .capabilities = SENSOR_LOCAL_DATA_WEATHER | SENSOR_LOCAL_DATA_TELEMETRY,
    .init = example_init,
    .save = example_save,
    .deinit = NULL,
    .ctx = &s_ctx,
};

/* Loads itself into the dynamic registry before app_main. */
SENSORS_LOCAL_DRIVER_AUTOREGISTER(example_driver);

#endif /* CONFIG_SENSORS_LOCAL_EXAMPLE_DRIVER */
