/**
 * @file esp32idf_radioamateur_modem.h
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
 * @brief Public API.
 */

#ifndef ESP32IDF_RADIOAMATEUR_MODEM_H_
#define ESP32IDF_RADIOAMATEUR_MODEM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ax25.h"
#include "esp32idf_radioamateur_modem_config.h" /* MODEM_PTT_GPIO / MODEM_PTT_ACTIVE_HIGH defaults used by MODEM_DEFAULT_CONFIG() */

/**
 * @brief Convert a delay in milliseconds to FreeRTOS ticks, guaranteeing at
 *        least 1 tick of actual delay.
 *
 * pdMS_TO_TICKS() truncates towards zero: at the default
 * CONFIG_FREERTOS_HZ=100 a tick is 10 ms, so any period below that would
 * become 0 ticks. vTaskDelay(0) does NOT block; it only yields to tasks of
 * equal or higher priority, so a polling task using it directly would spin
 * at its own priority, starve the idle task and eventually trip the task
 * watchdog. This macro ensures a polling delay never rounds down to
 * nothing.
 *
 * @param ms Desired delay, in milliseconds.
 * @return Equivalent delay in FreeRTOS ticks, never less than 1.
 */
#define MODEM_DELAY_TICKS(ms) ((pdMS_TO_TICKS(ms) > 0) ? pdMS_TO_TICKS(ms) : 1)

/**
 * @brief Selectable modem profiles.
 */
typedef enum {
    MODEM_MODEM_AFSK300 = 0, /**< 300 Bd, 1600/1800 Hz. */
    MODEM_MODEM_BELL202 = 1, /**< 1200 Bd, 1200/2200 Hz - standard APRS. */
    MODEM_MODEM_V23 = 2,     /**< 1200 Bd, 1300/2100 Hz. */
    MODEM_MODEM_G3RUH = 3,   /**< 9600 Bd FSK. */
} modem_mode_t;

/**
 * @brief Runtime configuration passed to modem_init() and
 *        modem_set_modem().
 */
typedef struct {
    modem_mode_t modem; /**< Modem profile to use. */
    bool flat_audio;       /**< true when the audio input is flat/discriminator output, false for de-emphasized audio. */
    bool full_duplex;      /**< true: key up immediately and keep receiving while transmitting. */
    bool allow_non_aprs;   /**< true: accept frames whose Control/PID fields are not 0x03/0xF0. */
    uint16_t preamble_ms;  /**< TXDelay (preamble) duration, in milliseconds. */
    uint16_t slot_time_ms; /**< CSMA quiet/slot time, in milliseconds; ignored in full duplex mode. */
    uint8_t fx25_mode;     /**< FX.25 mode: 0 = off, 1 = RX only, 2 = RX+TX (requires -DENABLE_FX25). */
    int8_t ptt_gpio;       /**< GPIO to drive PTT with, or -1 to disable it. Must be an output-capable
                                pin distinct from ::MODEM_ADC_GPIO / ::MODEM_DAC_GPIO -
                                see afsk_ptt_gpio_is_valid(). Applied by modem_init()/modem_set_modem(). */
    bool ptt_active_high; /**< true = PTT output is active-high, false = active-low. */
} modem_config_t;

/**
 * @brief Build a ::modem_config_t initializer with sensible default
 *        values: Bell 202 modem, standard (de-emphasized) audio, full
 *        duplex enabled, strict APRS frame filtering, 300 ms preamble, no
 *        CSMA slot time and FX.25 disabled.
 */
#define MODEM_DEFAULT_CONFIG()                                                                                                                               \
    {                                                                                                                                                          \
        .modem = MODEM_MODEM_BELL202,                                                                                                                        \
        .flat_audio = false,                                                                                                                                   \
        .full_duplex = true,                                                                                                                                   \
        .allow_non_aprs = false,                                                                                                                               \
        .preamble_ms = 300,                                                                                                                                    \
        .slot_time_ms = 0,                                                                                                                                     \
        .fx25_mode = 0,                                                                                                                                        \
        .ptt_gpio = MODEM_PTT_GPIO,                                                                                                                            \
        .ptt_active_high = MODEM_PTT_ACTIVE_HIGH ? true : false,                                                                                              \
    }

/**
 * @brief Description of one received frame, passed to the RX callback.
 */
typedef struct {
    const uint8_t *frame; /**< Raw AX.25 frame, address field onwards, without FCS. */
    uint16_t len;         /**< Length, in bytes, of frame. */
    int8_t peak;          /**< Peak signal level measured during reception. */
    int8_t valley;        /**< Valley (minimum) signal level measured during reception. */
    uint8_t level;        /**< Overall signal level indicator. */
    uint8_t corrected;    /**< Bytes corrected by FX.25 FEC, or ::AX25_NOT_FX25 if not FX.25. */
    uint16_t mVrms;       /**< RMS input level measured during reception, in millivolts. */
} modem_rx_frame_t;

/**
 * @brief Callback signature invoked whenever a frame is received.
 * @param f   Description of the received frame. Valid only for the
 *            duration of the callback.
 * @param ctx User-provided context pointer passed to
 *            modem_set_rx_callback().
 */
typedef void (*modem_rx_cb_t)(const modem_rx_frame_t *f, void *ctx);

/**
 * @brief Bring up the modem hardware and start the internal service tasks.
 * @param cfg Configuration to apply. See ::MODEM_DEFAULT_CONFIG for a
 *            reasonable starting point.
 * @return ESP_OK on success, or an ESP-IDF error code on failure.
 */
esp_err_t modem_init(const modem_config_t *cfg);

/**
 * @brief Stop every internal task and release all hardware resources.
 */
void modem_deinit(void);

/**
 * @brief Change the active modem profile and related settings at runtime.
 * @param cfg New configuration to apply.
 */
void modem_set_modem(const modem_config_t *cfg);

/**
 * @brief Install the callback invoked whenever a frame is received.
 *
 * The callback runs in the context of the internal service task.
 *
 * @param cb  Callback to install, or NULL to remove any existing callback.
 * @param ctx Opaque context pointer passed back to @p cb on every call.
 */
void modem_set_rx_callback(modem_rx_cb_t cb, void *ctx);

/**
 * @brief Queue a raw AX.25 frame for transmission.
 * @param frame Frame content, address field onwards: no HDLC flags, no bit
 *              stuffing and no FCS; all three are added automatically by
 *              the modulator.
 * @param len   Length, in bytes, of @p frame.
 * @return ESP_OK if the frame was queued successfully, or an ESP-IDF error
 *         code otherwise.
 */
esp_err_t modem_send_raw(const uint8_t *frame, uint16_t len);

/**
 * @brief Build a raw AX.25 frame from a TNC2-style monitor string.
 * @param tnc2    Monitor string, e.g. "NOCALL-1>APE32I,WIDE1-1:>hello".
 * @param out     Destination buffer for the built raw AX.25 frame.
 * @param out_len Size, in bytes, of @p out.
 * @return Length, in bytes, of the built frame, or 0 if @p tnc2 is
 *         malformed.
 */
int modem_build_frame_tnc2(const char *tnc2, uint8_t *out, size_t out_len);

/**
 * @brief Build a frame from a TNC2-style monitor string and queue it for
 *        transmission in a single call.
 * @param tnc2 Monitor string, e.g. "NOCALL-1>APE32I,WIDE1-1:>hello".
 * @return ESP_OK on success, or an ESP-IDF error code otherwise.
 */
esp_err_t modem_send_tnc2(const char *tnc2);

/**
 * @brief Render an already-decoded frame back into a TNC2-style monitor
 *        string.
 * @param msg     Decoded message to render.
 * @param out     Destination buffer for the resulting string.
 * @param out_len Size, in bytes, of @p out.
 */
void modem_format_tnc2(const ax25_msg_t *msg, char *out, size_t out_len);

/**
 * @brief Check whether a frame transmission is in progress or pending.
 * @return true while a frame is being modulated or is queued waiting to be.
 */
bool modem_tx_busy(void);

/**
 * @brief Count how many frames are currently queued/in flight on RF TX.
 * @return Number of frames still pending transmission (0 = idle).
 */
uint8_t modem_tx_queue_depth(void);

/**
 * @brief Measure the real ADC sampling rate achieved by the hardware.
 *
 * The ESP32 SAR-ADC in DMA mode does not necessarily run at exactly the
 * requested rate; see ::MODEM_ADC_RATE_NUM / ::MODEM_ADC_RATE_DEN for the correction factor applied.
 * This function blocks for the requested duration while it counts samples.
 *
 * @param ms Measurement window duration, in milliseconds.
 * @return Measured ADC sample rate, in samples per second.
 */
uint32_t modem_measure_adc_rate(uint32_t ms);

#endif /* ESP32IDF_RADIOAMATEUR_MODEM_H_ */
