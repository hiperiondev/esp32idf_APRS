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
 * GPIO25 and GPIO35 usable as a working full-duplex self-test loop.
 */

#ifndef LIB_AFSK_H_
#define LIB_AFSK_H_

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief HDLC flag byte (0x7E) used to delimit AX.25 frames on the air.
 */
#define AX25_FLAG 0x7e

/**
 * @brief Select the PTT GPIO and its active level at runtime.
 *
 * Overrides the compile-time ::MODEM_PTT_GPIO / ::MODEM_PTT_ACTIVE_HIGH
 * defaults. Must be called before AFSK_init() (i.e. before modem_init())
 * for the pin to actually be configured as an output; calling it again
 * later only updates the active level and the pin used by setPtt(), it
 * does NOT reconfigure GPIO direction on an already-running modem - call
 * AFSK_deinit()/AFSK_init() (or modem_deinit()/modem_init()) to move PTT
 * to a different pin while the modem is running.
 *
 * @param gpio        GPIO number to drive PTT with, or -1 to disable PTT
 *                    output entirely. The caller is responsible for
 *                    ensuring this is a valid output-capable pin that does
 *                    not collide with ::MODEM_ADC_GPIO / ::MODEM_DAC_GPIO
 *                    or any other pin already in use -
 *                    afsk_ptt_gpio_is_valid() can be used to check this.
 * @param active_high true = PTT output is active-high, false = active-low.
 */
void AFSK_setPttGpio(int8_t gpio, bool active_high);

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
 * @param gpio GPIO number to validate, or -1 for "disabled".
 * @return true if @p gpio is -1 or a safe, output-capable, non-reserved
 *         GPIO; false otherwise.
 */
bool afsk_ptt_gpio_is_valid(int8_t gpio);

/**
 * @brief Initialize the AFSK hardware layer.
 *
 * Brings up the ADC (continuous/DMA mode), the DAC and the GPTimer used to
 * clock DAC samples. This function is called internally by modem_init()
 * and does not normally need to be called directly by the application.
 *
 * Call AFSK_setPttGpio() before this function if a runtime PTT pin (other
 * than the ::MODEM_PTT_GPIO compile-time default) is needed.
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
 */
void afskSetModem(uint8_t val, bool flatAudio, uint16_t timeSlot, uint16_t preamble, uint8_t fx25Mode);

/**
 * @brief Enable or disable full-duplex operation.
 *
 * In full duplex mode the modem transmits immediately, without waiting for
 * the channel to be free (no DCD check, no CSMA backoff), which is required
 * for a hardware loopback such as GPIO25 -> GPIO35, where the node always
 * hears its own carrier.
 *
 * @param enable true to enable full duplex, false to use standard
 *               half-duplex CSMA behavior.
 */
void afskSetFullDuplex(bool enable);

/**
 * @brief Query whether full-duplex operation is currently enabled.
 * @return true if full duplex is enabled, false otherwise.
 */
bool afskGetFullDuplex(void);

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
 * @brief Set the internal transmit state flag.
 *
 * Also responsible for keying/unkeying the transmitter hardware (PTT) and
 * starting/stopping the DAC sample timer as needed.
 *
 * @param val true to enter the transmitting state, false to leave it.
 */
void setTransmit(bool val);

/**
 * @brief Check whether the receiver is currently active.
 * @return true if the receiver is active, false otherwise.
 */
bool getReceive(void);

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
 * @return RMS input level, in millivolts.
 */
uint16_t afskGetRms(void);

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
 * @brief Get the current AGC (automatic gain control) gain applied to the
 *        receiver input.
 * @return Current AGC gain, as a linear multiplier.
 */
float afskGetAgcGain(void);

/* ======================================================================== */
/*  Diagnostics - used by the characterization test in main/                 */
/* ======================================================================== */

/**
 * @brief Get the number of times the DAC timer ISR has fired since boot.
 *
 * The DAC sample clock is the one rate that nothing else in the system can
 * verify independently: the timer alarm is programmed as an integer number
 * of timer ticks (for example 10 MHz / 38400 Hz = 260 ticks, giving an actual
 * rate of 38461.5 Hz rather than exactly 38400 Hz). If the ISR ever overruns
 * its available time budget (roughly 26 microseconds), the real sample rate
 * silently collapses, which drags every generated tone and the effective
 * baud rate down with it, even though the output amplitude still looks
 * correct on a scope. Counting ISR invocations and comparing them against
 * elapsed time is the only way to detect this failure mode.
 *
 * @return Number of DAC timer ISR invocations since boot.
 */
uint32_t afskGetDacIsrCount(void);

/**
 * @brief Get the real rate at which the DAC timer alarm fires, in Hz.
 *
 * This is not necessarily equal to ::MODEM_DAC_SAMPLERATE: because the
 * alarm period must be an integer number of timer ticks, the achievable rate
 * is quantized (for example 10 MHz / 38400 Hz = 260.4 ticks, which rounds to
 * 260 ticks, giving an actual rate of 38461.5 Hz). This is the reference
 * value that afskGetDacIsrCount() must be measured against; comparing the
 * ISR count against the nominal configured rate instead would hide missed
 * alarms behind the quantization error.
 *
 * @return Real DAC timer alarm rate, in Hz, or 0 if AFSK_init() has not yet
 *         configured the timer.
 */
float afskGetDacAlarmRate(void);

/**
 * @brief Get the DAC sample rate that the component was actually
 *        compiled with, in Hz.
 *
 * A diagnostic running in a different translation unit cannot safely trust
 * its own copy of ::MODEM_DAC_SAMPLERATE: if the macro were defined
 * per-target in a CMakeLists file rather than centrally in
 * config, the main application and the modem component could
 * end up compiled against different values, silently invalidating every
 * derived figure (tone step size, DSP block size, decimation ratio). This
 * function asks the modem directly what rate it is running at, instead of
 * assuming the two translation units agree.
 *
 * @return Compiled-in DAC sample rate, in Hz.
 */
uint32_t afskGetDacSampleRate(void);

/**
 * @brief Get the ADC sample rate that the component was actually
 *        compiled with, in Hz. See afskGetDacSampleRate() for the rationale.
 * @return Compiled-in ADC sample rate, in Hz.
 */
uint32_t afskGetAdcSampleRate(void);

/**
 * @brief Get the sample rate at which the demodulator processes samples, in
 *        Hz. See afskGetDacSampleRate() for the rationale.
 *
 * This depends on the active modem profile. The AFSK profiles run at
 * ::MODEM_DEMOD_SAMPLERATE (the decimated rate); ::MODEM_9600 (G3RUH) is fed
 * the undecimated stream and so runs at ::MODEM_ADC_SAMPLERATE.
 *
 * @return Demodulator sample rate for the active profile, in Hz.
 */
uint32_t afskGetDemodSampleRate(void);

/**
 * @brief Force the DAC output to a fixed, arbitrary code.
 *
 * Intended for hardware diagnostics such as measuring the DAC transfer
 * curve. The call is ignored while a transmission is in progress.
 *
 * @param code Raw 8-bit DAC code to output.
 */
/**
 * @brief Start measuring the longest gap between DAC timer ISR invocations.
 *
 * The average miss rate is a poor predictor of whether AX.25 will work: a
 * uniform 1.7 % of missed alarms decodes every frame, while the same 1.7 %
 * arriving as one contiguous blackout per ADC frame decodes none. What matters
 * is the longest time the modulator is frozen. Measurement is off by default
 * because it costs an esp_timer read per sample.
 */
void afskDiagDacGapStart(void);

/**
 * @brief Stop measuring and return the longest ISR gap seen, in microseconds.
 *
 * Healthy is one alarm period (~26 us at 38400 Hz). The modulator survives
 * about 100 us and fails completely by 150 us, at every baud rate.
 */
uint32_t afskDiagDacGapStop(void);

void afskDiagDacWrite(uint8_t code);

/**
 * @brief Start emitting a continuous sine wave from the DAC.
 *
 * The phase step used to generate the sine is derived from the NOMINAL
 * (configured) sample rate rather than the measured one, so that any
 * mismatch between the two shows up as a frequency error in the emitted
 * tone, which can then be measured and compared against afskGetDacAlarmRate().
 *
 * @param freq_hz Desired tone frequency, in Hz.
 */
void afskDiagToneStart(uint32_t freq_hz);

/**
 * @brief Stop the tone previously started by afskDiagToneStart() and return
 *        the DAC to its idle state.
 */
void afskDiagToneStop(void);

/**
 * @brief Suspend/resume the RX task so a diagnostic can drive demodState[]
 *        directly (e.g. via ModemDiagDemodulate()) without racing the live
 *        RX task, which reads and mutates the same per-demodulator filter
 *        state on every ADC block via AFSK_Poll() -> MODEM_DECODE().
 *
 * Unlike afskSetModem(), which only suspends across ModemInit(), a
 * diagnostic that keeps calling ModemDiagDemodulate() in a loop after
 * ModemInit() has returned needs the RX task held off for the whole loop,
 * not just the reinit. Safe to call even if the RX task does not exist yet
 * (no-op). Callers are expected to pair every Pause() with exactly one
 * Resume().
 */
void afskDiagRxTaskPause(void);
void afskDiagRxTaskResume(void);

/**
 * @brief Capture raw ADC samples straight out of the conversion ISR.
 *
 * Captured samples have not been processed by the DC tracker, AGC or
 * decimation stage; they represent the analog input as seen directly by the
 * ADC hardware.
 *
 * @param dst        Destination buffer for the captured samples.
 * @param n          Maximum number of samples to capture.
 * @param timeout_ms Maximum time to wait for the requested number of
 *                   samples, in milliseconds.
 * @return Number of samples actually captured (may be less than @p n if the
 *         timeout elapses first).
 */
int afskDiagCaptureRaw(int16_t *dst, int n, uint32_t timeout_ms);

/**
 * @brief Capture samples exactly as the demodulator sees them.
 *
 * Unlike afskDiagCaptureRaw(), these samples have already passed through the
 * DC tracker, the AGC stage, the amplitude clamp, and the decimation down to
 * the 9600 Hz demodulator sample rate.
 *
 * @param dst        Destination buffer for the captured samples.
 * @param n          Maximum number of samples to capture.
 * @param timeout_ms Maximum time to wait for the requested number of
 *                   samples, in milliseconds.
 * @return Number of samples actually captured (may be less than @p n if the
 *         timeout elapses first).
 */
int afskDiagCaptureDemodInput(int16_t *dst, int n, uint32_t timeout_ms);

#endif /* LIB_AFSK_H_ */
