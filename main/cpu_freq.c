// @file cpu_freq.c
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
// @brief Applies the configured CPU clock frequency (80/160/240 MHz) to the
// running system through esp_pm_configure(), pinning min == max so the clock is
// not dynamically scaled.

#include "esp_err.h"
#include "esp_log.h"
#include "esp_pm.h"

#include "app_config.h"
#include "cpu_freq.h"

static const char *TAG = "cpu_freq";

void cpu_freq_apply(void) {
    uint8_t freq = g_config.cpuFreq; // single-word read, benign if stale
    // Only 80/160/240 MHz are valid on ESP32 (and the only options the System
    // page offers) - fall back to the max if config ever holds anything else.
    if (freq != 80 && freq != 160 && freq != 240) {
        freq = 240;
    }

    // Lock min == max so the clock runs at exactly the selected speed
    // instead of being dynamically scaled between two bounds; light sleep
    // stays off since this project needs the radio/timing paths responsive.
    esp_pm_config_t pm_config = {
        .max_freq_mhz = freq,
        .min_freq_mhz = freq,
        .light_sleep_enable = false,
    };

    esp_err_t err = esp_pm_configure(&pm_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_pm_configure(%d MHz) failed: %s - CPU frequency unchanged", freq, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "CPU frequency set to %d MHz", freq);
    }

    // The audio modem's receive chain - a 76.8 kHz converter feeding a
    // decimating filter and up to three demodulators - is dimensioned for the full
    // clock, and below it the samples arrive faster than they are processed.
    // The choice is still the operator's: this reports what it costs rather
    // than overriding it, because a node with the modem disabled has no reason
    // to run the clock at full speed.
    if (freq < 240 && g_config.audio_modem_en)
        ESP_LOGW(TAG, "the audio ADC/DAC modem needs 240 MHz to keep up with its receive chain; at %d MHz reception will be unreliable", freq);
}
