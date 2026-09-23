/**
 * @file afsk.h
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
 * @brief ESP-IDF hardware abstraction layer for the AFSK modem.
 * @details
 * This module drives the analog front end of the modem in full duplex mode:
 *  - RX path: ADC1 running in continuous (DMA) mode, free running and never
 *    stopped, so incoming audio is always being sampled.
 *  - TX path: the internal DAC running in one-shot mode, fed sample by sample
 *    from a GPTimer interrupt service routine at ::MODEM_DAC_SAMPLERATE.
 *
 * On the ESP32, the continuous ADC driver and the continuous (DMA) DAC driver
 * both require exclusive use of the I2S0 peripheral, so they can never be
 * active at the same time. Driving the DAC from an independent GPTimer instead
 * of the DMA DAC path leaves I2S0 entirely to the ADC, which allows both
 * directions to run simultaneously. This is what makes a simple wire between
 * GPIO25 and GPIO33 usable as a working full-duplex self-test loop.
 */

#ifndef LIB_AFSK_H_
#define LIB_AFSK_H_

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Set the active level (polarity) of the PTT output at runtime.
 *
 * The PTT GPIO itself is a fixed, compile-time-only board wiring choice
 * (::MODEM_PTT_GPIO - an internal radiomodem feature, exactly like
 * ::MODEM_ADC_GPIO / ::MODEM_DAC_GPIO) and cannot be changed from here or
 * from the web admin. Only the active-high/active-low polarity remains
 * runtime-selectable.
 *
 * @param active_high true = PTT output is active-high, false = active-low.
 */
void AFSK_setPttActiveHigh(bool active_high);

/**
 * @brief Check whether a GPIO number is usable as the PTT output pin on
 *        the ESP32.
 *
 * Rejects:
 *  - Input-only pins (GPIO34-39), which cannot drive an output at all.
 *  - The pins already reserved for the ADC/DAC audio path
 *    (::MODEM_ADC_GPIO, ::MODEM_DAC_GPIO).
 *  - Pins outside the 0-39 range.
 *  - The GPIO6-11 pins wired to the module's internal SPI flash/PSRAM,
 *    which are not brought out on any standard module and will crash the
 *    board if driven.
 *  - -1 (PTT disabled) is treated as valid, since it is an accepted
 *    "no PTT pin" sentinel rather than an actual pin number.
 *
 * ::MODEM_PTT_GPIO is validated against these same rules at compile time
 * (see esp32idf_radioamateur_modem_config.h); this function remains
 * available for other features (e.g. the Message Alarm pin picker) that
 * need to check a candidate GPIO against the same collision rules PTT uses.
 *
 * @param gpio GPIO number to validate, or -1 for "disabled".
 * @return true if @p gpio is -1 or a safe, output-capable, non-reserved
 *         GPIO; false otherwise.
 */
bool afsk_ptt_gpio_is_valid(int8_t gpio);

/**
 * @brief Checks whether @p gpio physically exists and can drive an output at
 *        all - not input-only (GPIO34-39), not the internal flash/PSRAM pads
 *        (GPIO6-11). Unlike ::afsk_ptt_gpio_is_valid, this does NOT check
 *        ::MODEM_ADC_GPIO / ::MODEM_DAC_GPIO or any other already-assigned
 *        pin: those collisions are meant to be surfaced by the web admin's
 *        GPIO registry (web_gpio_owner_tag()) as "already used" options
 *        instead of being hidden outright, so a picker's base pin list should
 *        use this function, then grey out registry hits.
 *
 * @param gpio GPIO number to check, or -1 for "disabled".
 * @return true if @p gpio is -1 or a real, output-capable pin; false otherwise.
 */
bool afsk_gpio_is_output_capable(int8_t gpio);

/**
 * @brief Initialize the AFSK hardware layer.
 *
 * Brings up the ADC (continuous/DMA mode), the DAC and the GPTimer used to
 * clock DAC samples. This function is called internally by modem_init()
 * and does not normally need to be called directly by the application.
 *
 * The PTT pin is configured from the compile-time ::MODEM_PTT_GPIO constant;
 * call AFSK_setPttActiveHigh() before this function if a non-default
 * polarity is needed.
 *
 * @return ESP_OK on success, or an ESP-IDF error code if any peripheral
 *         could not be configured.
 */
esp_err_t AFSK_init(void);

/**
 * @brief Release all hardware resources acquired by AFSK_init().
 *
 * Stops the ADC, the DAC and the GPTimer, and frees any buffers that were
 * allocated. After this call the modem must be reinitialized with
 * AFSK_init() (or modem_init()) before it can be used again.
 */
void AFSK_deinit(void);

/**
 * @brief Select the modem profile and reconfigure the modem and AX.25 layers.
 *
 * @param val       Modem profile to use:
 *                    - 0 = AFSK300 (300 Bd, 1600/1800 Hz)
 *                    - 1 = Bell 202, 1200 Bd (standard APRS, 1200/2200 Hz)
 *                    - 2 = ITU V.23, 1200 Bd (1300/2100 Hz)
 *                    - 3 = G3RUH, 9600 Bd FSK
 * @param flatAudio true if the audio input is flat (unfiltered/discriminator
 *                  output); false if it is de-emphasized audio.
 * @param timeSlot  CSMA quiet/slot time, in milliseconds. Ignored when full
 *                  duplex mode is active.
 * @param preamble  TXDelay (preamble) duration, in milliseconds.
 * @param fx25Mode  FX.25 mode selector:
 *                    - 0 = disabled
 *                    - 1 = RX only
 *                    - 2 = RX and TX (requires the component to be built with
 *                      ENABLE_FX25 defined)
 * @param minUnkeyMs Extra minimum PTT-off (unkeyed) hold time between
 *                  transmissions, in milliseconds, on top of the fixed
 *                  one-service-tick release holdoff that always applies.
 *                  0 disables the extra hold. Forwarded to
 *                  Ax25MinUnkeyTime().
 *
 * The receive front-end settings stored by afskSetRxFrontEnd() and the
 * prefilter tuning stored by ModemSetRxTuning() are applied here as well:
 * the receive task is held for the whole rebuild, so the demodulators never
 * see a half-built configuration.
 */
void afskSetModem(uint8_t val, bool flatAudio, uint16_t timeSlot, uint16_t preamble, uint8_t fx25Mode, uint16_t minUnkeyMs);

/**
 * @brief Select the DAC (transmit) sample rate used by the modulator.
 *
 * Only takes effect while the modem hardware is stopped: the value is read by
 * AFSK_init() to program the DAC sample-clock alarm and by ModemInit() to
 * derive the tone phase steps and the baud-rate divider, so it must be set
 * before modem_init() brings the modem up.
 *
 * The rate must stay an exact integer multiple of every supported baud rate,
 * which restricts it to ::MODEM_DAC_SAMPLERATE and twice that value. A higher
 * rate moves the DAC reconstruction images further away from the audio band,
 * which matters when the interface between the DAC pin and the transceiver
 * has no reconstruction low-pass filter, and costs one DAC interrupt per
 * additional sample.
 *
 * @param rate Requested DAC sample rate, in Hz. A value that is not an exact
 *             multiple of every supported baud rate is rejected and the
 *             current rate is kept.
 * @return ESP_OK if the rate was accepted, ESP_ERR_INVALID_ARG if it was
 *         rejected, or ESP_ERR_INVALID_STATE if the modem hardware is running.
 */
esp_err_t afskSetDacSampleRate(uint32_t rate);

/**
 * @brief Get the DAC (transmit) sample rate the modulator is programmed for.
 * @return DAC sample rate, in Hz.
 */
uint32_t afskGetDacSampleRate(void);

/**
 * @brief Set the peak-to-peak swing of the DAC output.
 *
 * Expressed as a percentage of the full 0..3.3 V range, centered on the DAC's
 * idle code. Applied to every sample from the next one onwards, so it may be
 * changed while the modem is running.
 *
 * The ESP32 DAC is 8 bits wide, so the percentage also sets how many codes a
 * sine period is drawn with: at 20 % a full period spans roughly 50 codes,
 * and below that quantization distortion grows quickly. The transmit level a
 * microphone input expects is reached with an external attenuator, not by
 * lowering this value.
 *
 * @param pct Peak-to-peak swing, in percent of the full DAC range (1..100).
 *            Values outside that range are clamped.
 */
void afskSetDacAmplitude(uint8_t pct);

/**
 * @brief Get the peak-to-peak swing currently applied to the DAC output.
 * @return Swing, in percent of the full DAC range.
 */
uint8_t afskGetDacAmplitude(void);

/**
 * @brief Enable or disable the ADC pad's internal input bias.
 *
 * Connects the pad's internal pull-up and pull-down at the same time; in
 * series they bias the pin to roughly mid-rail, which is what an AC-coupled
 * input with no external bias network needs. It must stay disabled whenever
 * the interface board provides its own bias divider, since the internal pulls
 * would load it.
 *
 * The bias is only nominally half of the supply: the internal pull resistors
 * are specified between 30 kOhm and 80 kOhm, are not matched to each other
 * and drift with temperature, so the resting level lands anywhere between
 * roughly 1.2 V and 2.0 V. The running DC average in AFSK_Poll() removes
 * whatever it settles at, and afskGetDcOffset() reports it in millivolts.
 *
 * Only GPIO32 and GPIO33 carry RTC pull resistors; on any other ADC pin this
 * reports ESP_ERR_NOT_SUPPORTED.
 *
 * @param enable true to bias the input from the internal pulls, false to
 *               leave the pad unbiased.
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if the configured ADC pin
 *         has no internal pull resistors, or an ESP-IDF error code from the
 *         RTC IO driver.
 */
esp_err_t afskSetAdcSelfBias(bool enable);

/**
 * @brief Check whether the ADC pad's internal input bias is enabled.
 * @return true if the internal pull-up and pull-down are both connected.
 */
bool afskGetAdcSelfBias(void);

/**
 * @brief Enable or disable the receive over-range warning.
 *
 * When enabled, a rate-limited warning is logged whenever a processed block
 * of samples reaches the ends of the ADC's conversion range. On an interface
 * without input clamp diodes that condition also means the pin is being
 * driven beyond the supply rails, so it is worth reporting rather than
 * leaving to show up as decode failures.
 *
 * @param enable true to log over-range blocks, false to stay silent.
 */
void afskSetClipWarn(bool enable);

/**
 * @brief Lowest raw ADC conversion result still counted as in range.
 *
 * Results at or below this code, or at or above ::AFSK_RAW_CLIP_HIGH, are
 * taken as over-range: the last few codes are already outside the
 * converter's usable window, and on an interface without input clamp diodes
 * reaching them also means the pin is being driven past the supply rails.
 */
#define AFSK_RAW_CLIP_LOW 15

/**
 * @brief Highest raw ADC conversion result still counted as in range; see
 *        ::AFSK_RAW_CLIP_LOW.
 */
#define AFSK_RAW_CLIP_HIGH 4080

/**
 * @brief Get the raw ADC extremes of the last processed block.
 *
 * Reports the minimum and maximum conversion results of the most recent
 * complete block, before DC removal, AGC and decimation. The full-scale range
 * is 0..4095; values at either end indicate the input is over-range.
 *
 * @param[out] min Minimum raw conversion result, or NULL if not needed.
 * @param[out] max Maximum raw conversion result, or NULL if not needed.
 */
void afskGetRawMinMax(int16_t *min, int16_t *max);

/**
 * @brief Store the receive front-end settings applied by the next
 *        afskSetModem().
 *
 * The values are only recorded here; afskSetModem() applies them while the
 * receive task is held, so no block is ever processed with a partly updated
 * front end.
 *
 * @param gateMv      Receive gate threshold, mV RMS. Blocks are handed to the
 *                    demodulators while the input RMS has been above this
 *                    level for a few blocks, and until it has fallen below
 *                    half of it. The blocks received while the gate was
 *                    closed are held and demodulated first when it opens.
 *                    0 disables the gate: every block is demodulated.
 * @param hpfHz       Corner of the second-order Butterworth high-pass
 *                    applied to the decimated signal of the AFSK profiles,
 *                    Hz, or 0 for none.
 * @param agcFixed    true for a fixed receive gain, false for automatic gain
 *                    control.
 * @param fixedGainDb Receive gain used when @p agcFixed is true, dB.
 */
void afskSetRxFrontEnd(uint16_t gateMv, uint16_t hpfHz, bool agcFixed, int8_t fixedGainDb);

/**
 * @brief Enable or disable full-duplex operation.
 *
 * In full duplex mode the modem transmits immediately, without waiting for
 * the channel to be free (no DCD check, no CSMA backoff), which is required
 * for a hardware loopback such as GPIO25 -> GPIO33, where the node always
 * hears its own carrier.
 *
 * @param enable true to enable full duplex, false to use standard
 *               half-duplex CSMA behavior.
 */
void afskSetFullDuplex(bool enable);

/**
 * @brief Check whether full-duplex operation is enabled.
 * @return true in full duplex, false in half duplex.
 */
bool afskGetFullDuplex(void);

/**
 * @brief Get the number of samples dropped because the receive FIFO was
 *        full.
 * @return Dropped samples since boot or since afskResetRxCounters().
 */
uint32_t afskGetFifoDrops(void);

/**
 * @brief Get the number of times the ADC driver's conversion pool
 *        overflowed.
 *
 * Each overflow means the receive task did not read conversion frames fast
 * enough and the driver discarded samples.
 *
 * @return Overflow events since boot or since afskResetRxCounters().
 */
uint32_t afskGetPoolOverflows(void);

/**
 * @brief Clear the counters reported by afskGetFifoDrops() and
 *        afskGetPoolOverflows().
 */
void afskResetRxCounters(void);

/**
 * @brief Drain the RX FIFO and run the demodulator on the buffered samples.
 *
 * Must be called periodically (typically from the internal DSP task) so
 * that samples captured by the ADC ISR are processed in a timely manner.
 */
void AFSK_Poll(void);

/**
 * @brief Discard every sample currently buffered in the RX FIFO.
 *
 * Useful to resynchronize the receiver, for example right before or after
 * a local transmission.
 */
void AFSK_FlushFifo(void);

/**
 * @brief Perform deferred transmitter teardown.
 *
 * Must be called from a task context, never from an interrupt service
 * routine, because it may perform operations that are not ISR-safe (such as
 * releasing PTT or switching the DAC back to idle).
 */
void AFSK_ServiceTx(void);

/**
 * @brief Check whether the modem is currently transmitting.
 * @return true if a transmission is in progress, false otherwise.
 */
bool getTransmit(void);

/**
 * @brief Check whether a deferred TX teardown (releasing PTT, parking the
 *        DAC) is still waiting to run in AFSK_ServiceTx().
 *
 * setTransmit(false), called from the DAC ISR at the end of a key-up, only
 * raises this flag - it cannot touch PTT itself from ISR context. The real
 * gpio_set_level() that drops PTT happens later, in AFSK_ServiceTx(), from
 * task context. Ax25TransmitCheck() must not start a new transmission while
 * this is still true: setTransmit(true) (called from ModemTransmitStart() at
 * the start of the next key-up) clears the pending-teardown flag as a side
 * effect, which would silently cancel the PTT-off for the frame that just
 * finished - keeping PTT continuously asserted across what should be two
 * separate keyups.
 *
 * @return true if AFSK_ServiceTx() still needs to run before it is safe to
 *         key up again.
 */
bool AFSK_TxTeardownPending(void);

/**
 * @brief Set the internal transmit state flag.
 *
 * Also responsible for keying/unkeying the transmitter hardware (PTT) and
 * starting/stopping the DAC sample timer as needed.
 *
 * @param val true to enter the transmitting state, false to leave it.
 */
void setTransmit(bool val);

/**
 * @brief Directly drive the PTT (push-to-talk) output.
 * @param state true to key the transmitter, false to unkey it.
 */
void setPtt(bool state);

/**
 * @brief Drive the RX/DCD status indicator LED.
 *
 * @param red   Red channel intensity.
 * @param green Green channel intensity.
 * @param blue  Blue channel intensity.
 */
void LED_Status2(uint8_t red, uint8_t green, uint8_t blue);

/**
 * @brief Get the last measured RMS level of the receiver input.
 *
 * Wideband: everything the ADC converts, DC removed, over the last block.
 *
 * @return RMS input level, in millivolts.
 */
uint16_t afskGetRms(void);

/**
 * @brief Get the last measured RMS level of the tone band of the receiver
 *        input.
 *
 * Measured on the decimated signal of the AFSK profiles, after the optional
 * high-pass and before the gain control, through a fourth-order band-pass
 * centred between 900 and 2600 Hz (about -1.6 dB at 1200 Hz, -3.2 dB at
 * 2200 Hz and -26 dB at 300 Hz), and scaled to millivolts at the ADC pin. It
 * reads the level of the tones themselves, where the wideband figure of
 * afskGetRms() can be dominated by hum, CTCSS, the bass of a de-emphasized
 * speaker output or noise above the tones. The receive gate of the AFSK
 * profiles compares against this value. G3RUH, which is not decimated, reports
 * the wideband level here as well.
 *
 * @return RMS level of the tone band over the last block, in millivolts.
 */
uint16_t afskGetBandRms(void);

/**
 * @brief Get the total number of samples delivered by the ADC since boot.
 * @return Cumulative ADC sample count.
 */
uint32_t afskGetAdcSampleCount(void);

/**
 * @brief Get the current DC offset of the receiver input.
 * @return DC offset, in millivolts, computed as a running average.
 */
int afskGetDcOffset(void);

/**
 * @brief Get the receive gain currently applied ahead of the demodulators.
 *
 * With automatic gain control this is the AGC gain; with a fixed gain it is
 * that gain.
 *
 * @return Current receive gain, as a linear multiplier.
 */
float afskGetAgcGain(void);

/**
 * @name Diagnostics
 * @brief Rate-measurement helpers used by the characterization test in main/.
 * @{
 */

/**
 * @brief Get the real rate at which the DAC timer alarm fires, in Hz.
 *
 * This is not necessarily equal to ::MODEM_DAC_SAMPLERATE: because the
 * alarm period must be an integer number of timer ticks, the achievable rate
 * is quantized (for example 10 MHz / 38400 Hz = 260.4 ticks, which rounds to
 * 260 ticks, giving an actual rate of 38461.5 Hz).
 *
 * @return Real DAC timer alarm rate, in Hz, or 0 if AFSK_init() has not yet
 *         configured the timer.
 */
float afskGetDacAlarmRate(void);

/**
 * @brief Capture raw ADC samples straight out of the conversion ISR.
 *
 * Captured samples have not been processed by the DC tracker, AGC or
 * decimation stage; they represent the analog input as seen directly by the
 * ADC hardware.
 *
 * Samples are collected into a buffer owned by the modem component and copied
 * into @p dst once the capture window closes, so @p dst is only touched by the
 * calling task. Requests larger than the internal buffer (512 samples) are
 * capped to it.
 *
 * @param dst        Destination buffer for the captured samples.
 * @param n          Maximum number of samples to capture, capped at 512.
 * @param timeout_ms Maximum time to wait for the requested number of
 *                   samples, in milliseconds.
 * @return Number of samples actually captured (may be less than @p n if the
 *         timeout elapses first).
 */
int afskDiagCaptureRaw(int16_t *dst, int n, uint32_t timeout_ms);

/** @} */

#endif /* LIB_AFSK_H_ */
