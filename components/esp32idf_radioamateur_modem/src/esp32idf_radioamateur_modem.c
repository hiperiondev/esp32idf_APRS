/**
 * @file esp32idf_radioamateur_modem.c
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
 * @brief Public API implementation: modem lifecycle (init/deinit/reconfigure),
 * TNC2 string transmit path, RX frame callback dispatch and the receive service
 * task.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp32idf_radioamateur_modem.h"
#include "esp32idf_radioamateur_modem_config.h"
#include "ax25.h"
#include "afsk.h"
#include "modem.h"

static const char *TAG = "radiomodem";

/* How often the service task polls the TX state machine and drains RX frames.
 * The effective period is never shorter than one FreeRTOS tick. */
#define MODEM_SVC_PERIOD_MS 5

static ax25_ctx_t s_ctx;
static TaskHandle_t s_svcTask = NULL;
static modem_rx_cb_t s_rxCb = NULL;
static void *s_rxCbCtx = NULL;
static bool s_running = false;

/* ------------------------------------------------------------------ */
/* Service task                                                        */
/* ------------------------------------------------------------------ */

static void modem_service_task(void *arg) {
    (void)arg;
    uint8_t *frame;
    uint16_t size;
    int8_t peak, valley;
    uint8_t level, corrected;
    uint16_t mV;

    //uint32_t lastHeartbeat = 0;

    for (;;) {
        AFSK_ServiceTx();

        /* Throttled proof-of-life: confirms this task loop (and therefore
         * Ax25TransmitCheck() right below) is actually being reached, without
         * flooding the log. Remove once TX is confirmed working end to end. */
        //uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        //if (now - lastHeartbeat >= 2000) {
            //lastHeartbeat = now;
            //ESP_LOGI(TAG, "svc task alive, t=%" PRIu32 " ms", now);
        //}

        Ax25TransmitCheck();

        while (Ax25ReadNextRxFrame(&frame, &size, &peak, &valley, &level, &corrected, &mV)) {
            if (s_rxCb) {
                modem_rx_frame_t f = {
                    .frame = frame,
                    .len = size,
                    .peak = peak,
                    .valley = valley,
                    .level = level,
                    .corrected = corrected,
                    .mVrms = mV,
                };
                s_rxCb(&f, s_rxCbCtx);
            }
        }

        /* MODEM_DELAY_TICKS, not pdMS_TO_TICKS: at CONFIG_FREERTOS_HZ=100
         * this rounds up to one tick (10 ms) instead of collapsing to
         * vTaskDelay(0), which would spin and starve IDLE. */
        vTaskDelay(MODEM_DELAY_TICKS(MODEM_SVC_PERIOD_MS));
    }
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void modem_set_rx_callback(modem_rx_cb_t cb, void *ctx) {
    s_rxCb = cb;
    s_rxCbCtx = ctx;
}

void modem_set_modem(const modem_config_t *cfg) {
    /* Validated defensively here (not just at the config-save boundary in
     * app_config.c/page_mod.c) so any caller of modem_set_modem()/modem_init()
     * can't accidentally drive an ADC/DAC or input-only pin as PTT. An
     * invalid pin falls back to "disabled" rather than being silently
     * applied. */
    int8_t ptt_gpio = afsk_ptt_gpio_is_valid(cfg->ptt_gpio) ? cfg->ptt_gpio : -1;
    AFSK_setPttGpio(ptt_gpio, cfg->ptt_active_high);

    afskSetFullDuplex(cfg->full_duplex);
    afskSetModem((uint8_t)cfg->modem, cfg->flat_audio, cfg->slot_time_ms, cfg->preamble_ms, cfg->fx25_mode);
    Ax25Config.allowNonAprs = cfg->allow_non_aprs ? 1 : 0;
    Ax25Config.fullDuplex = cfg->full_duplex ? 1 : 0;
}

esp_err_t modem_init(const modem_config_t *cfg) {
    if (s_running)
        return ESP_ERR_INVALID_STATE;

    memset(&s_ctx, 0, sizeof(s_ctx));

    /* Set the duplex mode before the hardware comes up so the ADC callback
     * gate and Ax25TransmitCheck() agree from the very first sample. */
    afskSetFullDuplex(cfg->full_duplex);

    /* Must happen before AFSK_init(), since that is what configures the PTT
     * pin's GPIO direction - AFSK_setPttGpio() only updates the pin number
     * afsk.c uses, it does not itself touch gpio_config(). Same defensive
     * fallback-to-disabled as modem_set_modem(). */
    AFSK_setPttGpio(afsk_ptt_gpio_is_valid(cfg->ptt_gpio) ? cfg->ptt_gpio : -1, cfg->ptt_active_high);

    esp_err_t err = AFSK_init();
    if (err != ESP_OK)
        return err;

    /*
     * Calibrate every profile's DPLL against this board's real ADC/DAC clock
     * ratio before the first ModemInit() runs (modem_set_modem() below is
     * what triggers it). See ModemCalibrateSampleRate() for why: nominal
     * MODEM_ADC_SAMPLERATE/MODEM_DAC_SAMPLERATE assumes both clocks hit
     * their configured rates exactly, and they don't - the gap is a steady,
     * repeatable bias, not thermal noise, so one measurement here is enough
     * for the whole run. This is what turns the "residual DAC/ADC clock
     * drift" the G3RUH stress test flags into a solved, calibrated-out
     * quantity instead of something the DPLL has to fight indefinitely.
     *
     * The DAC side needs no live measurement - afskGetDacAlarmRate() already
     * reports the timer's real rate, computed exactly from its configuration.
     *
     * The ADC side does need measuring, and the window matters more than it
     * looks like it should. modem_measure_adc_rate() reads s_adcSamples,
     * which adc_conv_done_cb() only increments once per completed
     * MODEM_ADC_CONV_FRAME (128 samples) - see afsk.c. That means every
     * measurement carries a start/end quantization error of up to one whole
     * frame, and at 76800 Hz that is:
     *
     *      128 samples / (76800 Hz * window_s) = 0.001667 / window_s
     *
     * A 200 ms window - what a first pass at this used - gives ~0.83% of
     * error, which is *larger* than the ~0.3-0.4% real ADC/DAC clock gap
     * this exists to correct for. Applying that as a correction is not
     * "slightly off," it is as likely to have the wrong sign as the right
     * one, and PLL9600_LOCKED_TUNE=0.97 has just enough margin for the real
     * ~0.38% bias and none to spare for an extra, wrong-signed one - which
     * is exactly how a 200 ms window turned a 10% G3RUH loss rate into 85%.
     * 5000 ms brings the quantization error down to ~0.033%, an order of
     * magnitude below the signal being measured, at the cost of 5 extra
     * seconds of boot time paid exactly once.
     */
    ModemCalibrateSampleRate((float)modem_measure_adc_rate(5000), afskGetDacAlarmRate());

    modem_set_modem(cfg);

    /* The RX callback decodes into an AX25Msg and renders a TNC2 string, both
     * of which are a few hundred bytes of stack on top of any printf. */
    /*
     * Pinned, not free-floating. This task is the consumer end of the AX.25 RX
     * ring whose producer (Ax25BitParse(), from afsk_rx_task) is pinned to
     * MODEM_RX_TASK_CORE.
     */
#if MODEM_RX_TASK_CORE >= 0
    if (xTaskCreatePinnedToCore(modem_service_task, "modem_svc", 6144, NULL, 5, &s_svcTask, MODEM_RX_TASK_CORE) != pdPASS) {
#else
    if (xTaskCreate(modem_service_task, "modem_svc", 6144, NULL, 5, &s_svcTask) != pdPASS) {
#endif
        AFSK_deinit();
        return ESP_ERR_NO_MEM;
    }

    s_running = true;
    ESP_LOGI(TAG, "started: modem=%d %s duplex, DAC=GPIO%d ADC=GPIO%d", (int)cfg->modem, cfg->full_duplex ? "full" : "half", MODEM_DAC_GPIO,
             MODEM_ADC_GPIO);
    ESP_LOGI(TAG, "service task: %" PRIu32 " tick(s) per poll at CONFIG_FREERTOS_HZ=%d", (uint32_t)MODEM_DELAY_TICKS(MODEM_SVC_PERIOD_MS),
             CONFIG_FREERTOS_HZ);
    return ESP_OK;
}

uint8_t modem_tx_queue_depth(void) {
    return Ax25TxFramesPending();
}

uint32_t modem_measure_adc_rate(uint32_t ms) {
    uint32_t start = afskGetAdcSampleCount();
    int64_t t0 = esp_timer_get_time();
    vTaskDelay(MODEM_DELAY_TICKS(ms));
    int64_t t1 = esp_timer_get_time();
    uint32_t end = afskGetAdcSampleCount();

    int64_t dt = t1 - t0;
    if (dt <= 0)
        return 0;
    return (uint32_t)(((uint64_t)(end - start) * 1000000ULL) / (uint64_t)dt);
}

/* ------------------------------------------------------------------ */
/* Frame helpers                                                       */
/* ------------------------------------------------------------------ */

int modem_build_frame_tnc2(const char *tnc2, uint8_t *out, size_t out_len) {
    /* ax25_encode() writes into the string it is given (it uses strtok on the
     * digipeater path), so work on a scratch copy. */
    char scratch[AX25_FRAME_MAX_SIZE + 1];
    ax25_frame_t frame;

    size_t len = strlen(tnc2);
    if (len == 0 || len >= sizeof(scratch))
        return 0;

    memcpy(scratch, tnc2, len + 1);

    if (!ax25_encode(&frame, scratch, (int)len))
        return 0;

    return hdlcFrame(out, out_len, &s_ctx, &frame);
}

esp_err_t modem_send_raw(const uint8_t *frame, uint16_t len) {
    if (frame == NULL || len == 0)
        return ESP_ERR_INVALID_ARG;

    if (Ax25WriteTxFrame(frame, len) == NULL) {
        ESP_LOGW(TAG, "TX buffer full, frame dropped");
        return ESP_ERR_NO_MEM;
    }
    Ax25TransmitBuffer();
    return ESP_OK;
}

esp_err_t modem_send_tnc2(const char *tnc2) {
    uint8_t buf[AX25_FRAME_MAX_SIZE];
    int size = modem_build_frame_tnc2(tnc2, buf, sizeof(buf));

    if (size <= 0) {
        ESP_LOGW(TAG, "cannot encode \"%s\"", tnc2);
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "TX: %s", tnc2);
    return modem_send_raw(buf, (uint16_t)size);
}

static int appendCall(char *out, size_t out_len, size_t pos, const ax25_call_t *c) {
    if (c->ssid)
        return snprintf(out + pos, out_len - pos, "%s-%u", c->call, c->ssid);
    return snprintf(out + pos, out_len - pos, "%s", c->call);
}

void modem_format_tnc2(const ax25_msg_t *msg, char *out, size_t out_len) {
    size_t pos = 0;
    int n;

    if (out_len == 0)
        return;
    out[0] = 0;

    n = appendCall(out, out_len, pos, &msg->src);
    if (n < 0 || (size_t)n >= out_len - pos)
        return;
    pos += n;

    n = snprintf(out + pos, out_len - pos, ">");
    pos += n;

    n = appendCall(out, out_len, pos, &msg->dst);
    if (n < 0 || (size_t)n >= out_len - pos)
        return;
    pos += n;

    for (uint8_t i = 0; i < msg->rpt_count; i++) {
        n = snprintf(out + pos, out_len - pos, ",");
        if (n < 0 || (size_t)n >= out_len - pos)
            return;
        pos += n;
        n = appendCall(out, out_len, pos, &msg->rpt_list[i]);
        if (n < 0 || (size_t)n >= out_len - pos)
            return;
        pos += n;
        if (AX25_REPEATED(msg, i)) {
            n = snprintf(out + pos, out_len - pos, "*");
            if (n < 0 || (size_t)n >= out_len - pos)
                return;
            pos += n;
        }
    }

    n = snprintf(out + pos, out_len - pos, ":");
    if (n < 0 || (size_t)n >= out_len - pos)
        return;
    pos += n;

    for (size_t i = 0; i < msg->len && pos < out_len - 1; i++)
        out[pos++] = (char)msg->info[i];
    out[pos] = 0;
}

