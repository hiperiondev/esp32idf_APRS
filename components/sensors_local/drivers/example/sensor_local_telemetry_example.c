/**
 * @file sensor_local_telemetry_example.c
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
 * @brief Example TELEMETRY ::sensor_local_driver_t.
 *
 * Advertises only ::SENSOR_LOCAL_DATA_TELEMETRY and, on every call, writes a
 * fresh set of *random* values into every analog (A1..An) and digital (B1..Bn)
 * channel the caller actually allocated in
 * ::weather_telemetry_data_t::telemetry_report[0], setting each channel's
 * enabled flag. It exists to exercise the telemetry path with no real hardware
 * attached. Copy it as the skeleton for a real ADS1115 / INA219 / GPIO driver.
 *
 */

#include <stdlib.h>
#include <time.h>

#include "esp_log.h"
#include "sensors_local.h"
#include "tlm_example_properties.h" /* fine-grained Telemetry channel capability descriptor */

static const char *TAG = "sensor_tlm_example";

typedef struct {
    uint32_t sample_count;
} tlm_example_ctx_t;

static tlm_example_ctx_t s_ctx;

static esp_err_t tlm_example_init(sensor_local_driver_t *self) {
    tlm_example_ctx_t *c = (tlm_example_ctx_t *)self->ctx;
    c->sample_count = 0;
    srand((unsigned)time(NULL) ^ 0x5a5a5a5au); // seed differently from the WX example
    ESP_LOGI(TAG, "telemetry example sensor brought up");
    return ESP_OK;
}

/* The one entry the framework calls. kind is already masked to TELEMETRY. */
static esp_err_t tlm_example_save(sensor_local_driver_t *self, weather_telemetry_data_t *data, sensor_local_data_kind_t kind) {
    tlm_example_ctx_t *c = (tlm_example_ctx_t *)self->ctx;
    c->sample_count++;

    if (!(kind & SENSOR_LOCAL_DATA_TELEMETRY) || data->telemetry_report == NULL || data->telemetry_report_qty < 1)
        return ESP_OK;

    aprs_telemetry_report_t *tlm = &data->telemetry_report[0];

    // Analog A1..An: only touch channels the caller actually allocated. Values
    // are kept in the classic 0-255 range so both the strict and the extended
    // on-air telemetry encodings are valid.
    if (tlm->analog != NULL && tlm->analog_enabled != NULL) {
        for (size_t i = 0; i < tlm->analog_count; i++) {
            tlm->analog[i] = (double)(rand() % 256);
            tlm->analog_enabled[i] = true;
        }
    }

    // Digital B1..Bn: random 0/1 for each allocated channel.
    if (tlm->digital != NULL && tlm->digital_enabled != NULL) {
        for (size_t i = 0; i < tlm->digital_count; i++) {
            tlm->digital[i] = (rand() & 1) != 0;
            tlm->digital_enabled[i] = true;
        }
    }

    return ESP_OK;
}

static sensor_local_driver_t tlm_example_driver = {
    .name = "tlm-example",
    .capabilities = SENSOR_LOCAL_DATA_TELEMETRY,
    .init = tlm_example_init,
    .save = tlm_example_save,
    .deinit = NULL,
    .properties = &tlm_example_properties,
    .ctx = &s_ctx,
};

SENSORS_LOCAL_DRIVER_AUTOREGISTER(tlm_example_driver);
