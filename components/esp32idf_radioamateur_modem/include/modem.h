/**
 * @file modem.h
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
 * @brief AFSK/FSK modulator and demodulator core, shared by every supported
 *        modem profile.
 */

#ifndef LIB_MODEM_H_
#define LIB_MODEM_H_

#include <stdbool.h>
#include <stdint.h>

#include "esp32idf_radioamateur_modem.h"

/**
 * @brief Maximum number of demodulators that can run in parallel.
 *
 * Each demodulator instance is configured in ModemInit(). Only the 1200 Bd
 * profiles run more than one. A demodulator is a slicer on the output of a
 * correlator (prefilter plus mark/space correlators): either every
 * demodulator has its own correlator, whose prefilters differ in tilt between
 * the mark and space tones, or several slicers share one correlator and differ
 * in the weight they give the space tone. Either way the set covers a wider
 * range of received tone twist than any single demodulator.
 */
#define MODEM_MAX_DEMODULATOR_COUNT MODEM_RX_MAX_DEMODULATORS

/**
 * @brief Maximum number of correlators (prefilter plus mark/space
 *        correlators) that can run in parallel.
 */
#define MODEM_MAX_CORRELATOR_COUNT MODEM_RX_MAX_PREFILTERS

/**
 * @brief Runtime configuration of the demodulator.
 */
struct ModemDemodConfig {
    modem_mode_t modem;      /**< Active modem/tone profile (::modem_mode_t). */
    uint8_t usePWM : 1;      /**< 0 = R2R resistor ladder output, 1 = PWM/DAC output. */
    uint8_t flatAudioIn : 1; /**< 0 = de-emphasized audio input, 1 = flat (unfiltered) input. */
};

/**
 * @brief Global, live demodulator configuration used by the whole
 *        component.
 */
extern struct ModemDemodConfig ModemConfig;

/**
 * @brief Audio pre-filtering applied ahead of a demodulator's correlators.
 */
enum ModemPrefilter {
    PREFILTER_NONE = 0, /**< No prefilter: the correlators see the demodulator input directly. */
    PREFILTER_BANDPASS, /**< Band-pass prefilter; its tilt between the mark and space tones is reported by ModemGetFilterTiltDb(). */
};

/**
 * @brief Get the peak/valley/level signal indicators for a given
 *        demodulator.
 * @param modem  Index of the demodulator to query, 0 ..
 *               ::MODEM_MAX_DEMODULATOR_COUNT - 1. An index outside that
 *               range sets all three outputs to 0.
 * @param peak   Set to the peak signal level.
 * @param valley Set to the valley (minimum) signal level.
 * @param level  Set to the overall signal level indicator.
 */
void ModemGetSignalLevel(uint8_t modem, int8_t *peak, int8_t *valley, uint8_t *level);

/**
 * @brief Get the baud rate of the currently active modem profile.
 * @return Baud rate, in symbols per second.
 */
float ModemGetBaudrate(void);

/**
 * @brief Get how many demodulators are active for the current modem
 *        profile.
 * @return Number of active demodulators, 1 .. ::MODEM_MAX_DEMODULATOR_COUNT.
 */
uint8_t ModemGetDemodulatorCount(void);

/**
 * @brief Get the pre-filtering strategy appropriate for a given
 *        demodulator's configuration.
 * @param modem Index of the demodulator to query, 0 ..
 *              ::MODEM_MAX_DEMODULATOR_COUNT - 1.
 * @return The pre-filter type that should be applied, or ::PREFILTER_NONE if
 *         the index is outside the valid range.
 */
enum ModemPrefilter ModemGetFilterType(uint8_t modem);

/**
 * @brief Get the tilt of a demodulator's prefilter.
 *
 * Measured on the coefficients actually in use: the prefilter gain at the
 * space tone minus its gain at the mark tone.
 *
 * @param modem Index of the demodulator to query, 0 ..
 *              ::MODEM_MAX_DEMODULATOR_COUNT - 1.
 * @return Tilt, in dB; 0 for a demodulator without prefilter, for an index
 *         outside the valid range and for the G3RUH profile.
 */
float ModemGetFilterTiltDb(uint8_t modem);

/**
 * @brief Estimate the tone twist of the signal a demodulator is receiving.
 *
 * The correlator a demodulator reads tracks the magnitude of the mark tone
 * while it is the stronger one and of the space tone while that one is,
 * averaged over roughly the last eight symbols. Their ratio, with the
 * correlator's prefilter tilt removed, is the twist at the modem input. The
 * slicer weight does not enter the figure. Called by the
 * AX.25 layer when a frame completes, so the figure describes that frame.
 *
 * @param modem Index of the demodulator to query, 0 ..
 *              ::MODEM_MAX_DEMODULATOR_COUNT - 1.
 * @return Space tone level relative to mark tone level, in dB, clamped to
 *         -30..+30; 0 when no estimate is available.
 */
int8_t ModemGetTwistDb(uint8_t modem);

/**
 * @brief Get the weight a demodulator's slicer gives the space tone.
 *
 * The slicer decides "mark" while the mark tone magnitude exceeds the space
 * tone magnitude multiplied by this weight. 0 dB is a plain comparison;
 * a negative weight favours the mark tone and suits a signal whose space tone
 * arrives louder, which is the same compensation a prefilter tilt of the same
 * value gives.
 *
 * @param modem Index of the demodulator to query, 0 ..
 *              ::MODEM_MAX_DEMODULATOR_COUNT - 1.
 * @return Weight, in dB; 0 for an index outside the valid range and for the
 *         G3RUH profile.
 */
float ModemGetSlicerWeightDb(uint8_t modem);

/**
 * @brief Get the correlator a demodulator reads.
 *
 * Demodulators with the same index share one prefilter and one pair of
 * mark/space correlators, so they report the same prefilter tilt and twist.
 *
 * @param modem Index of the demodulator to query, 0 ..
 *              ::MODEM_MAX_DEMODULATOR_COUNT - 1.
 * @return Correlator index, 0 .. ::MODEM_MAX_CORRELATOR_COUNT - 1; 0 for an
 *         index outside the valid range.
 */
uint8_t ModemGetCorrelatorIndex(uint8_t modem);

/**
 * @brief Store the receive tuning the next ModemInit() builds the
 *        demodulators from.
 *
 * Only the prefilter fields of @p t are used here (preset, custom set, band
 * edges and length); the front-end fields are applied by afsk.c. The values
 * are copied and take effect at the next ModemInit(), which afskSetModem()
 * runs with the receive task held.
 *
 * @param t Tuning to store. Ignored if NULL.
 */
void ModemSetRxTuning(const modem_rx_tuning_t *t);

/**
 * @brief Get the current Data Carrier Detect (DCD) state.
 *
 * The return value is a bitmap with one bit per parallel demodulator: bit 0
 * is demodulator 0, bit 1 is demodulator 1, and so on up to
 * ::MODEM_MAX_DEMODULATOR_COUNT. Callers that only need to know whether the
 * channel is busy can simply test the result for non-zero.
 *
 * @return Bitmap of the demodulators that currently have carrier lock, 0 if
 *         the channel is free.
 */
uint8_t ModemDcdState(void);

/**
 * @brief Configure and start a transmission.
 *
 * Used internally by the AX.25 protocol layer when a frame is ready to be
 * sent; not normally called directly by application code.
 */
void ModemTransmitStart(void);

/**
 * @brief Stop transmitting and return to the receiving state.
 *
 * Called from the DAC interrupt service routine, so this function and
 * everything it calls must remain ISR-safe.
 */
void ModemTransmitStop(void);

/**
 * @brief Check whether the deferred teardown from the previous transmission
 *        (releasing PTT, parking the DAC) has finished yet.
 *
 * Ax25TransmitCheck() must confirm this is false before starting a new
 * transmission: the teardown for a just-finished key-up is only performed by
 * AFSK_ServiceTx() in task context, one modem-service-loop tick after the DAC
 * ISR requests it, so a new transmission started in between would - via
 * setTransmit(true)'s side effect of clearing the pending-teardown flag -
 * silently cancel that PTT release, leaving PTT continuously asserted across
 * what should be two separate keyups.
 *
 * @return true if a teardown is still pending and it is not yet safe to key
 *         up again; false once PTT has actually been released.
 */
bool ModemTxTeardownPending(void);

/**
 * @brief Initialize the modem core: demodulator state, tone tables and
 *        default configuration.
 */
void ModemInit(void);

/**
 * @brief Log the demodulator set built by the last ModemInit().
 *
 * Writes one line per designed prefilter (band edges, length, requested and
 * realized tilt), one line per demodulator when several slicers share a
 * correlator (space weight and the effective twist compensation, i.e. weight
 * plus realized prefilter tilt), and a summary line with the transmitted tone
 * frequencies, the DAC rate, the number of demodulators and the receive clock
 * correction.
 *
 * Only reads state ModemInit() left behind, so it is meant to be called once
 * the receive task runs again: writing to the console takes several
 * milliseconds per line, which must not be spent while that task is held.
 */
void ModemLogConfig(void);

/**
 * @brief Calibrate every demodulator's DPLL against the real ADC sample
 *        rate, and record the real DAC rate for the G3RUH self-test.
 *
 * Every profile's PLL step assumes the ADC runs exactly at
 * ::MODEM_ADC_SAMPLERATE. The ADC clock is a hardware divider with its own
 * rounding error, so the real number of samples per symbol differs from the
 * nominal one by a small, fixed ratio - a steady phase error the DPLL would
 * otherwise have to track for every frame. The ratio is a board property
 * (both clocks come from the same crystal), so one measurement per boot is
 * enough; it is reapplied on every profile switch.
 *
 * Stations on the air transmit at their own nominal baud rate, so the
 * receive calibration uses the ADC error alone. The node's own AFSK
 * transmitter is exact as well: its tone and symbol phase accumulators are
 * built from the real DAC alarm rate. The G3RUH transmitter is the one
 * exception - it holds every symbol for a whole number of DAC samples, so
 * it runs at the DAC error - and only a receiver that hears the node's own
 * transmitter needs to know about that: in full duplex (the wire loopback
 * self-test) the G3RUH DPLL step also includes the DAC ratio.
 *
 * Call once, after both the ADC and the DAC timer are running and before the
 * first ModemInit() - modem_init() does this. A ratio further than 1 % from
 * unity is treated as a bad measurement and ignored.
 *
 * @param measuredAdcHz Real ADC sample rate, in Hz, e.g. from
 *                       ::modem_measure_adc_rate().
 * @param measuredDacHz Real DAC alarm rate, in Hz, from ::afskGetDacAlarmRate().
 */
void ModemCalibrateSampleRate(float measuredAdcHz, float measuredDacHz);

/**
 * @brief Get the mark and space tone frequencies the modulator can
 *        actually emit, in Hz.
 *
 * Derived from the modulator's own phase-accumulator steps and the real DAC
 * alarm rate (::afskGetDacAlarmRate()) rather than from the nominal
 * markFreq/spaceFreq constants the profile was configured with, so the
 * figures describe what is on the air. Anything measuring the transmitter
 * should compare its readings against this function.
 *
 * @param mark  Set to the actual mark tone frequency the modulator emits,
 *              in Hz.
 * @param space Set to the actual space tone frequency the modulator emits,
 *              in Hz.
 */
void ModemGetStepTones(float *mark, float *space);

/**
 * @brief Feed one sample to the demodulator during normal operation.
 * @param sample Input audio sample, at 9600 Hz (or 38400 Hz when using
 *               ::MODEM_MODEM_G3RUH).
 * @param mVrms  RMS input level associated with this sample, in millivolts.
 */
void MODEM_DECODE(int16_t sample, uint16_t mVrms);

/**
 * @brief Produce the next DAC output sample.
 *
 * Called from the DAC interrupt service routine at
 * ::MODEM_DAC_SAMPLERATE while a transmission is active.
 *
 * @return Next raw DAC sample to output.
 */
uint8_t MODEM_BAUDRATE_TIMER_HANDLER(void);

#endif /* LIB_MODEM_H_ */
