// @file sensor_local_telemetry_example.c
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
// @brief Example TELEMETRY ::sensor_local_driver_t.
//
// Advertises only ::SENSOR_LOCAL_DATA_TELEMETRY and, on every call, writes a
// fresh, *simulated but plausible* engineering value into every analog
// (A1..An) and digital (B1..Bn) channel the caller actually allocated in
// ::weather_telemetry_data_t::telemetry_report[0], setting each channel's
// enabled flag. It exists to exercise the telemetry path with no real
// hardware attached, while still being a useful, self-documenting
// reference for what a real ADS1115 / INA219 / GPIO driver should return.
//
// @details
// Unlike a bare random-noise stub, every one of the 5 analog and 8 digital
// channels below models a specific, realistic quantity a solar-powered
// APRS iGate/digipeater station commonly monitors. For each analog
// channel the table in ::s_analog_channels documents:
//   - the physical quantity and its engineering unit (matches what would
//     be typed into the web admin Telemetry page's per-channel
//     "Name"/"Unit" fields, or an "UNIT." metadata message on-air),
//   - the realistic min/max range the simulated value is drawn from, and
//   - the linear APRS101 "EQNS." scaling equation
//     @f$ X = a \cdot v^2 + b \cdot v + c @f$ (APRS101 Ch.13) that
//     recovers that engineering value from the raw 0-255 byte actually
//     placed on-air - i.e. exactly the (a,b,c) triplet to enter in the
//     channel's calibration fields on the web admin Telemetry page (or
//     an "EQNS." message) so a receiver decodes back the same value this
//     driver "measured". Since every channel here uses a purely linear
//     mapping, a is always 0.
//
// Each call re-derives a random-walked engineering value inside its
// documented range, then quantizes it to the raw byte via the *inverse*
// of its equation (raw = (X - c) / b), so the 0-255 value transmitted
// on-air is realistic and reversible with the documented EQNS - both the
// strict APRS101 (0-255) and the extended on-air telemetry encodings stay
// valid. Digital channels B1-B8 model plausible boolean station/telemetry
// flags (see ::s_digital_channels for the meaning/on-state label of each
// bit) and are toggled with a biased random draw so "alarm" style bits
// are mostly false, matching real-world behaviour.
//
// Copy this file as the skeleton for a real ADS1115 / INA219 / GPIO
// driver: replace the simulated-value generator in each entry with an
// actual hardware read, keep the (name/unit/range/eqns) documentation in
// sync with what the hardware really measures, and update
// tlm_example_properties.h's channel labels to match.

#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "esp_log.h"
#include "sensors_local.h"

// Everything below is compiled only when the driver is enabled in menuconfig
// (Component config -> Sensors Local). This mirrors bmp180.c, and it is not
// optional: the component is registered WHOLE_ARCHIVE, so without this guard
// SENSORS_LOCAL_DRIVER_AUTOREGISTER() below is always linked in and the fake
// sensor registers at boot no matter what Kconfig says.
//
// The properties descriptor is included inside the guard on purpose: it is a
// file-scope `static const` object, so leaving it outside would make it an
// unused variable (and a -Wunused-const-variable warning) in a disabled build.
#ifdef CONFIG_SENSORS_LOCAL_TELEMETRY_EXAMPLE_DRIVER

#include "tlm_example_properties.h" // fine-grained Telemetry channel capability descriptor

static const char *TAG = "sensor_tlm_example";

typedef struct {
    uint32_t sample_count;
} tlm_example_ctx_t;

static tlm_example_ctx_t s_ctx;

// @brief Documented linear EQNS scaling (X = b*v + c ; a is always 0 here)
//        plus the realistic engineering-value range each analog channel
//        (A1-A5) is simulated from.
//
// @note  These are the exact values to enter in the web admin Telemetry
//        page's per-channel "Unit" and calibration (a,b,c) fields, or to
//        publish in an on-air "UNIT."/"EQNS." metadata message, so a
//        receiver's decoded reading matches what this example "measures".
typedef struct {
    const char *label; // < Parameter name (PARM.), e.g. "Supply Voltage".
    const char *unit;  // < Engineering unit (UNIT.), e.g. "Volts".
    double eng_min;    // < Lower bound of the simulated realistic value.
    double eng_max;    // < Upper bound of the simulated realistic value.
    double eqns_b;     // < EQNS. linear coefficient b: X = b*raw + c.
    double eqns_c;     // < EQNS. constant offset c.
} tlm_example_analog_channel_t;

// clang-format off
static const tlm_example_analog_channel_t s_analog_channels[APRS_TELEMETRY_ANALOG_CHANNELS] = {
    // label                  unit       eng_min  eng_max   b (=(max-min)/255)   c (=eng_min)
    { "Supply Voltage",       "Volts",     10.50,   14.80,   (14.80-10.50)/255.0,  10.50 }, // A1: battery/solar bus voltage, 12V lead-acid/LiFePO4 range
    { "Enclosure Temp",       "DegC",     -20.00,   60.00,   (60.00-(-20.00))/255.0, -20.00 }, // A2: enclosure ambient temperature
    { "Charge Current",       "Amps",       0.00,    3.20,   (3.20-0.00)/255.0,     0.00 }, // A3: solar charge-controller input current
    { "RF Signal (RSSI)",     "dBm",     -110.00,  -40.00,   (-40.00-(-110.00))/255.0, -110.00 }, // A4: receiver RSSI of the last heard packet
    { "Free Heap",            "Percent",    0.00,  100.00,   (100.00-0.00)/255.0,   0.00 }, // A5: free-heap headroom, percent of total
};
// clang-format on

// @brief Meaning and on-state label for each digital channel (B1-B8),
//        matching what would be entered in the web admin Telemetry
//        page's per-bit "Label"/"ON label" fields or an on-air "BITS."
//        metadata message. @c alarm_style channels are simulated with a
//        low duty cycle (mostly false) since they model fault/alert
//        conditions rather than routine state.
typedef struct {
    const char *label;    // < Bit label (BITS.), e.g. "PTT".
    const char *on_label; // < UNIT. on-state text, e.g. "TX".
    bool alarm_style;     // < true = simulate mostly-false with rare true (alert-like).
} tlm_example_digital_channel_t;

static const tlm_example_digital_channel_t s_digital_channels[APRS_TELEMETRY_DIGITAL_CHANNELS] = {
    { "PTT Active", "TX", false },          // B1: transmitter keyed
    { "GPS Fix Valid", "FIX", false },      // B2: GPS has a current fix
    { "SD Card Present", "OK", false },     // B3: SD/LittleFS log storage present and mounted
    { "Charge Ctrl Active", "CHG", false }, // B4: solar charge controller is actively charging
    { "Enclosure Door", "OPEN", true },     // B5: enclosure tamper/door switch (alert-like)
    { "Low Battery Alarm", "LOW", true },   // B6: supply voltage below low-battery threshold
    { "Over Temp Alarm", "HOT", true },     // B7: enclosure temperature above safe threshold
    { "Watchdog Reset", "RST", true },      // B8: last boot was caused by a watchdog reset
};

static esp_err_t tlm_example_init(sensor_local_driver_t *self) {
    tlm_example_ctx_t *c = (tlm_example_ctx_t *)self->ctx;
    c->sample_count = 0;
    srand((unsigned)time(NULL) ^ 0x5a5a5a5au); // seed differently from the WX example
    ESP_LOGI(TAG, "telemetry example sensor brought up");
    return ESP_OK;
}

// @brief Uniform random double in [lo, hi].
static double tlm_example_rand_range(double lo, double hi) {
    double frac = (double)rand() / (double)RAND_MAX;
    return lo + frac * (hi - lo);
}

// @brief Quantize an engineering-unit value to the raw 0-255 byte that,
//        run through this channel's documented EQNS (X = b*raw + c),
//        decodes back to (approximately) @p eng_value.
static uint8_t tlm_example_encode_raw(const tlm_example_analog_channel_t *ch, double eng_value) {
    if (ch->eqns_b == 0.0)
        return 0;
    double raw = (eng_value - ch->eqns_c) / ch->eqns_b;
    if (raw < 0.0)
        raw = 0.0;
    if (raw > 255.0)
        raw = 255.0;
    return (uint8_t)lround(raw);
}

// The one entry the framework calls. kind is already masked to TELEMETRY.
static esp_err_t tlm_example_save(sensor_local_driver_t *self, weather_telemetry_data_t *data, sensor_local_data_kind_t kind) {
    tlm_example_ctx_t *c = (tlm_example_ctx_t *)self->ctx;
    c->sample_count++;

    if (!(kind & SENSOR_LOCAL_DATA_TELEMETRY) || data->telemetry_report == NULL || data->telemetry_report_qty < 1)
        return ESP_OK;

    aprs_telemetry_report_t *tlm = &data->telemetry_report[0];

    // Analog A1..An: only touch channels the caller actually allocated. Each
    // channel draws a realistic engineering value from s_analog_channels[]
    // and re-encodes it to the raw 0-255 byte via that channel's documented
    // EQNS, so both the strict and the extended on-air telemetry encodings
    // stay valid and the transmitted value is meaningful once decoded with
    // the matching (a=0,b,c) calibration on the web admin Telemetry page.
    if (tlm->analog != NULL && tlm->analog_enabled != NULL) {
        for (size_t i = 0; i < tlm->analog_count; i++) {
            if (i < APRS_TELEMETRY_ANALOG_CHANNELS) {
                const tlm_example_analog_channel_t *ch = &s_analog_channels[i];
                double eng_value = tlm_example_rand_range(ch->eng_min, ch->eng_max);
                tlm->analog[i] = (double)tlm_example_encode_raw(ch, eng_value);
            } else {
                // Non-standard extended channel beyond A5: no documented
                // quantity, fall back to a plain 0-255 raw value.
                tlm->analog[i] = (double)(rand() % 256);
            }
            tlm->analog_enabled[i] = true;
        }
    }

    // Digital B1..Bn: each allocated channel is toggled per s_digital_channels[]
    // - "alarm"-style bits (door/low-battery/over-temp/watchdog) are true only
    // ~10% of the time, routine status bits (PTT/GPS/SD/charge) are true ~50%
    // of the time, mirroring how these conditions actually behave in the field.
    if (tlm->digital != NULL && tlm->digital_enabled != NULL) {
        for (size_t i = 0; i < tlm->digital_count; i++) {
            bool alarm_style = (i < APRS_TELEMETRY_DIGITAL_CHANNELS) ? s_digital_channels[i].alarm_style : false;
            int threshold = alarm_style ? 10 : 50; // percent chance of being true
            tlm->digital[i] = ((rand() % 100) < threshold);
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
    .properties = &tlm_example_properties,
    .ctx = &s_ctx,
};

SENSORS_LOCAL_DRIVER_AUTOREGISTER(tlm_example_driver);

#endif // CONFIG_SENSORS_LOCAL_TELEMETRY_EXAMPLE_DRIVER
