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

#include <stdint.h>

/**
 * @brief Maximum number of demodulators that can run in parallel.
 *
 * Each demodulator instance must be explicitly configured in ModemInit();
 * currently this is only used by the 1200 Bd modem, which runs two
 * demodulators tuned slightly differently to improve decode probability.
 */
#define MODEM_MAX_DEMODULATOR_COUNT 2

/**
 * @brief Supported modem/tone profiles.
 */
enum ModemType {
    MODEM_1200 = 0, /**< Bell 202, 1200 Bd, 1200/2200 Hz (standard APRS). */
    MODEM_1200_V23, /**< ITU V.23, 1200 Bd, 1300/2100 Hz. */
    MODEM_300,      /**< AFSK300, 300 Bd, 1600/1800 Hz. */
    MODEM_9600,     /**< G3RUH FSK, 9600 Bd. */
};

/**
 * @brief Runtime configuration of the demodulator.
 */
struct ModemDemodConfig {
    enum ModemType modem;   /**< Active modem/tone profile. */
    uint8_t usePWM : 1;      /**< 0 = R2R resistor ladder output, 1 = PWM/DAC output. */
    uint8_t flatAudioIn : 1; /**< 0 = de-emphasized audio input, 1 = flat (unfiltered) input. */
};

/**
 * @brief Global, live demodulator configuration used by the whole
 *        component.
 */
extern struct ModemDemodConfig ModemConfig;

/**
 * @brief Audio pre-filtering applied ahead of the demodulator, depending on
 *        the modem profile and the nature of the input signal.
 */
enum ModemPrefilter {
    PREFILTER_NONE = 0,   /**< No pre-filtering applied. */
    PREFILTER_PREEMPHASIS, /**< Pre-emphasis filter applied. */
    PREFILTER_DEEMPHASIS,  /**< De-emphasis filter applied. */
    PREFILTER_FLAT,        /**< Input treated as already flat/unfiltered. */
};

/**
 * @brief Get the peak/valley/level signal indicators for a given
 *        demodulator.
 * @param modem  Index of the demodulator to query.
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
 * @return Number of active demodulators (1 or ::MODEM_MAX_DEMODULATOR_COUNT).
 */
uint8_t ModemGetDemodulatorCount(void);

/**
 * @brief Get the pre-filtering strategy appropriate for a given
 *        demodulator's configuration.
 * @param modem Index of the demodulator to query.
 * @return The pre-filter type that should be applied.
 */
enum ModemPrefilter ModemGetFilterType(uint8_t modem);

/**
 * @brief Get the current Data Carrier Detect (DCD) state.
 * @return 1 if the channel is currently busy (carrier detected), 0 if it is
 *         free.
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
 * @brief Initialize the modem core: demodulator state, tone tables and
 *        default configuration.
 */
void ModemInit(void);

/**
 * @brief Calibrate every demodulator's DPLL against the real ADC/DAC clock
 *        ratio, rather than the nominal ::MODEM_ADC_SAMPLERATE /
 *        ::MODEM_DAC_SAMPLERATE this component is compiled for.
 *
 * The DAC (transmit) and ADC (receive) sample clocks are two independent
 * timers, each with its own rounding error against the rate it was asked
 * for - see the ::MODEM_ADC_SAMPLERATE and dac_timer_create() comments.
 * Every profile's PLL step is computed from the *nominal* ratio of those two
 * rates (see PLL1200_STEP / PLL9600_STEP / PLL300_STEP in modem.c), so any
 * gap between nominal and real is a steady-state error the DPLL has to
 * track for the rest of a transmission instead of being told about up
 * front. For G3RUH at 9600 Bd and only 8 ADC samples per symbol, that gap
 * is the dominant remaining source of the frame loss characterised in
 * afsk_loopback_test.c's stress test.
 *
 * This does not require live measurement of both clocks: the DAC alarm rate
 * (::afskGetDacAlarmRate()) is already known exactly from the timer's
 * configuration, and only the ADC side needs to be measured (see
 * ::modem_measure_adc_rate()). Call this once, after both the ADC and the
 * DAC timer are running but before the first ::ModemInit() (i.e. before the
 * first ::afskSetModem()) - modem_init() does this automatically. The
 * result is stored and reapplied on every subsequent profile switch, so it
 * only needs to be measured once per boot: both clocks are derived from the
 * same crystal, so their ratio is a fixed board property, not something
 * that drifts run to run.
 *
 * @param measuredAdcHz Real ADC sample rate, in Hz, e.g. from
 *                       ::modem_measure_adc_rate().
 * @param measuredDacHz Real DAC alarm rate, in Hz, from ::afskGetDacAlarmRate().
 */
void ModemCalibrateSampleRate(float measuredAdcHz, float measuredDacHz);

/* ---- diagnostics ------------------------------------------------------- */

/**
 * @brief Number of entries in the modulator's sine lookup table.
 */
#define MODEM_SIN_LEN 512

/**
 * @brief Read one sample of the 512-entry, 8-bit unsigned sine table used
 *        by the modulator (midpoint value 128).
 * @param i Index into the sine table, in the range [0, ::MODEM_SIN_LEN).
 * @return Sine table value at index @p i.
 */
uint8_t ModemSinSample(uint16_t i);

/**
 * @brief Get the mark and space tone frequencies the modulator can
 *        actually emit, in Hz.
 *
 * Deliberately derived from the modulator's own phase-accumulator steps
 * rather than from the nominal markFreq/spaceFreq constants the profile was
 * configured with, so that any future change to the modulator cannot
 * silently drift away from the tone values it claims to implement. Anything
 * measuring the transmitter should compare its readings against this
 * function.
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
 *               ::MODEM_9600).
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
