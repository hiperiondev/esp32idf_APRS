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
 * @brief Public API of the ESP-IDF radioamateur AFSK/FSK modem component:
 * runtime configuration, the RX callback interface and the frame TX helpers
 * used by the rest of the firmware (aprs_service.c).
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
#include "esp32idf_radioamateur_modem_config.h"

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
 * @name Receive tuning valid ranges
 *
 * Inclusive limits of every ::modem_rx_tuning_t field. modem_rx_tuning_sanitize()
 * clamps a structure into these ranges; the web admin and the configuration
 * loader use the same limits for their input fields and stored values.
 * @{
 */
#define MODEM_RX_TILT_DB_MIN     (-9)  /**< Most negative prefilter tilt, dB (space tone attenuated relative to mark). */
#define MODEM_RX_TILT_DB_MAX     9     /**< Most positive prefilter tilt, dB (space tone boosted relative to mark). */
#define MODEM_RX_BPF_LO_HZ_MIN   600   /**< Lowest band-pass lower edge, Hz. */
#define MODEM_RX_BPF_LO_HZ_MAX   1100  /**< Highest band-pass lower edge, Hz. */
#define MODEM_RX_BPF_HI_HZ_MIN   2300  /**< Lowest band-pass upper edge, Hz. */
#define MODEM_RX_BPF_HI_HZ_MAX   3000  /**< Highest band-pass upper edge, Hz. */
#define MODEM_RX_BPF_TAPS_MIN    9     /**< Shortest band-pass prefilter, taps (always odd). */
#define MODEM_RX_BPF_TAPS_MAX    31    /**< Longest band-pass prefilter, taps (always odd). */
#define MODEM_RX_GATE_MV_MAX     50    /**< Highest receive gate threshold, mV RMS (0 disables the gate). */
#define MODEM_RX_HPF_HZ_MAX      400   /**< Highest high-pass corner, Hz (0 disables the high-pass). */
#define MODEM_RX_AGC_GAIN_DB_MIN (-12) /**< Lowest fixed receive gain, dB. */
#define MODEM_RX_AGC_GAIN_DB_MAX 18    /**< Highest fixed receive gain, dB. */
#define MODEM_RX_FIX_BITS_MAX    2     /**< Highest bit-repair level (see ::modem_rx_tuning_t::fix_bits). */
/** @} */

/**
 * @brief Demodulator set used by the 1200 Bd profiles (Bell 202 and V.23).
 *
 * Every entry except ::MODEM_RX_EQ_LEGACY designs its band-pass prefilters at
 * run time from ::modem_rx_tuning_t::bpf_lo_hz, ::modem_rx_tuning_t::bpf_hi_hz
 * and ::modem_rx_tuning_t::bpf_taps. The tilt of each prefilter is the gain
 * difference between the space and the mark tone, and the preset tilt tables
 * depend on ::modem_config_t::flat_audio:
 *
 * | Preset        | Flat / discriminator input | De-emphasized (speaker) input |
 * |---------------|----------------------------|-------------------------------|
 * | SINGLE        | 0 dB                       | +3 dB                         |
 * | DIVERSITY2    | 0, -5 dB                   | 0, +5 dB                      |
 * | DIVERSITY3    | +4, 0, -5 dB               | 0, +3, +6 dB                  |
 *
 * Tone twist on the air ranges well beyond what one prefilter can absorb:
 * a transmitter that pre-emphasizes its audio arrives on a discriminator
 * output with the space tone 5 to 12 dB louder than the mark tone, a flat
 * data-port transmitter arrives with no twist at all, and a speaker output
 * shifts both by the receiver's de-emphasis. Several prefilters with
 * different tilts cover that range together, which one prefilter cannot.
 */
typedef enum {
    MODEM_RX_EQ_LEGACY = 0, /**< Two demodulators with the fixed 8-tap tables: a tilted or flat band-pass, depending on flat_audio, and an unfiltered one. */
    MODEM_RX_EQ_SINGLE = 1, /**< One demodulator, one band-pass prefilter. */
    MODEM_RX_EQ_DIVERSITY2 = 2, /**< Two demodulators with different prefilter tilts. */
    MODEM_RX_EQ_DIVERSITY3 = 3, /**< Three demodulators with different prefilter tilts. */
    MODEM_RX_EQ_CUSTOM = 4,     /**< custom_count demodulators with the tilts in custom_tilt_db[]. */
} modem_rx_eq_preset_t;

/**
 * @brief Receive gain control mode.
 */
typedef enum {
    MODEM_RX_AGC_AUTO = 0,  /**< Automatic gain control on the in-band (decimated) signal. */
    MODEM_RX_AGC_FIXED = 1, /**< Fixed gain of ::modem_rx_tuning_t::agc_fixed_gain_db. Suits a data or discriminator port, whose level is set by deviation. */
} modem_rx_agc_mode_t;

/**
 * @brief Receive-chain tuning, part of ::modem_config_t.
 *
 * Every field is applied by modem_set_modem() without restarting the modem
 * hardware: the demodulators are rebuilt while the receive task is held.
 */
typedef struct {
    modem_rx_eq_preset_t eq_preset;                   /**< Demodulator set for the 1200 Bd profiles. */
    uint8_t custom_count;                             /**< Number of demodulators used by ::MODEM_RX_EQ_CUSTOM, 1..::MODEM_RX_MAX_DEMODULATORS. */
    int8_t custom_tilt_db[MODEM_RX_MAX_DEMODULATORS]; /**< Prefilter tilt of each ::MODEM_RX_EQ_CUSTOM demodulator, dB (space gain minus mark gain). */
    uint16_t bpf_lo_hz;                               /**< Lower band edge of the designed prefilters, Hz. */
    uint16_t bpf_hi_hz;                               /**< Upper band edge of the designed prefilters, Hz. */
    uint8_t bpf_taps; /**< Length of the designed prefilters, taps; forced odd so the filters stay linear phase. Short filters reach only part of the
                           requested tilt; ModemInit() logs the tilt each prefilter actually has. */
    uint16_t gate_mv; /**< Receive gate, mV RMS: the demodulators are fed only while the input exceeds this level (it closes again below half of it).
                         The blocks received while the gate is closed are held and demodulated when it opens, so the start of a transmission is
                         not lost. 0 feeds the demodulators continuously, which suits a squelch-independent data or discriminator port. */
    uint16_t hpf_hz;  /**< Corner of a second-order high-pass applied to the demodulator input of the AFSK profiles, Hz, or 0 for none. Removes
                         CTCSS tones and hum that a discriminator output carries at full level. */
    modem_rx_agc_mode_t agc_mode; /**< Automatic or fixed receive gain. */
    int8_t agc_fixed_gain_db;     /**< Receive gain used by ::MODEM_RX_AGC_FIXED, dB. */
    uint8_t fix_bits;             /**< Repair of frames whose FCS does not match: 0 = off, 1 = one corrupted symbol (two adjacent bits after NRZI decoding),
                                     2 = one corrupted symbol or one single bit. A repaired frame is delivered only when it also passes a strict APRS sanity
                                     check (valid callsign characters, UI control field, no-layer-3 PID, no control characters in the information field).
                                     Every repair accepts a small share of wrongly corrected frames, so this stays off unless the extra decodes are wanted. */
} modem_rx_tuning_t;

/**
 * @brief Build a ::modem_rx_tuning_t initializer with the default receive
 *        tuning: three demodulators, a 900-2600 Hz band, 21-tap prefilters,
 *        a 10 mV receive gate, no high-pass, automatic gain and no bit
 *        repair.
 */
#define MODEM_RX_TUNING_DEFAULT()                                                                                                                              \
    {                                                                                                                                                          \
        .eq_preset = MODEM_RX_EQ_DIVERSITY3,                                                                                                                   \
        .custom_count = 3,                                                                                                                                     \
        .custom_tilt_db = { 4, 0, -5 },                                                                                                                        \
        .bpf_lo_hz = 900,                                                                                                                                      \
        .bpf_hi_hz = 2600,                                                                                                                                     \
        .bpf_taps = 21,                                                                                                                                        \
        .gate_mv = 10,                                                                                                                                         \
        .hpf_hz = 0,                                                                                                                                           \
        .agc_mode = MODEM_RX_AGC_AUTO,                                                                                                                         \
        .agc_fixed_gain_db = 0,                                                                                                                                \
        .fix_bits = 0,                                                                                                                                         \
    }

/**
 * @brief Clamp every field of a ::modem_rx_tuning_t into its valid range.
 *
 * Out-of-range values are moved to the nearest limit, an unknown preset or
 * gain mode falls back to the default one, and an even tap count is rounded
 * up to the next odd one. Safe to call on any structure, including one read
 * from an untrusted source.
 *
 * @param t Structure to sanitize in place. Ignored if NULL.
 */
void modem_rx_tuning_sanitize(modem_rx_tuning_t *t);

/**
 * @brief Receive statistics accumulated since boot or since the last
 *        modem_reset_rx_stats().
 */
typedef struct {
    uint32_t decoded[MODEM_RX_MAX_DEMODULATORS]; /**< Frames with a valid FCS produced by each demodulator, duplicates of the same frame included. */
    uint32_t unique[MODEM_RX_MAX_DEMODULATORS];  /**< Frames that only this demodulator produced: the measure of what each prefilter adds to the set. */
    uint32_t delivered;                          /**< Frames handed to the RX callback after duplicate suppression. */
    uint32_t repaired;                           /**< Delivered frames that needed bit repair (see ::modem_rx_tuning_t::fix_bits). */
    uint32_t fifo_drops;                         /**< Samples dropped because the receive FIFO was full. */
    uint32_t adc_pool_overflows;                 /**< Times the ADC driver's conversion pool overflowed and discarded samples. */
    uint8_t demod_count;                         /**< Number of demodulators active for the current profile. */
} modem_rx_stats_t;

/**
 * @brief Runtime configuration passed to modem_init() and
 *        modem_set_modem().
 */
typedef struct {
    modem_mode_t modem;    /**< Modem profile to use. */
    bool flat_audio;       /**< true when the audio input is flat/discriminator output, false for de-emphasized (speaker) audio. Selects the prefilter
                              tilt table of the ::modem_rx_eq_preset_t presets. */
    bool full_duplex;      /**< true: key up immediately and keep receiving while transmitting. */
    bool allow_non_aprs;   /**< true: accept frames whose Control/PID fields are not 0x03/0xF0. */
    uint16_t preamble_ms;  /**< TXDelay (preamble) duration, in milliseconds. */
    uint16_t slot_time_ms; /**< CSMA quiet time, in milliseconds: how long a queued frame waits before channel access begins. The interval between the
                              persistence rolls that follow is the fixed AX.25 "SlotTime" held in ::Ax25ProtoConfig::csmaSlotTime, not this value. Ignored in
                              full duplex mode. */
    uint8_t persist;       /**< CSMA/p-persistent channel-access probability (standard AX.25/KISS "Persist"): once the quiet time has elapsed and the
                              channel is heard clear, the modem transmits immediately with probability persist/256 on every slot and otherwise waits one
                              more slot time before rolling again. 255 transmits on the first clear slot every time (equivalent to plain non-persistent
                              CSMA); lower values spread contending stations' key-ups further apart. See modem_persistence_missed_count() for the
                              anti-starvation floor that bounds how long a frame can be held this way. Ignored in full duplex mode. */
    uint8_t fx25_mode;     /**< FX.25 mode: 0 = off, 1 = RX only, 2 = RX+TX (requires -DENABLE_FX25). */
    bool ptt_active_high;  /**< true = PTT output active-high, false = active-low. @note There is deliberately no ptt_gpio field: the PTT pin is a fixed
                              compile-time board wiring choice (::MODEM_PTT_GPIO, like ::MODEM_ADC_GPIO / ::MODEM_DAC_GPIO), not runtime/web-admin selectable;
                              only the active level is configurable here. */
    uint16_t min_unkey_ms; /**< Extra minimum PTT-off (unkeyed) time between transmissions, in milliseconds, ON TOP OF the fixed one-modem-service-tick (~10 ms)
                              release Ax25TransmitCheck() always applies. 0 = no extra hold. For radios/repeaters that need a longer guaranteed unkey gap
                              between frames. */
    uint32_t dac_samplerate;   /**< DAC (transmit) sample rate, in Hz. Must be an exact multiple of every supported baud rate, which restricts it to
                                  ::MODEM_DAC_SAMPLERATE and twice that value. Read while the modem hardware is stopped, so modem_init() applies it and
                                  modem_set_modem() does not: a change takes effect at the next start. A higher rate moves the DAC reconstruction images
                                  further from the audio band, which matters when the interface to the transceiver has no reconstruction low-pass filter. */
    uint8_t dac_amplitude_pct; /**< Peak-to-peak swing of the DAC output, in percent of the full 0..3.3 V range. Applied per sample, so it may be changed
                                  while the modem runs. The ESP32 DAC is 8 bits wide: below roughly 20 % a sine period is drawn with so few codes that
                                  quantization distortion dominates, so the level a microphone input expects is reached with an external attenuator and
                                  not by lowering this value. */
    bool adc_self_bias;        /**< true to bias the ADC input from the pad's own pull-up and pull-down in series, for an AC-coupled input with no external
                                  bias network. Must stay false whenever the interface board provides its own bias divider. Only GPIO32/33 carry internal
                                  pull resistors. */
    bool rx_clip_warn;         /**< true to log a rate-limited warning whenever a processed block of samples reaches the ends of the ADC's conversion range.
                                  On an interface without input clamp diodes that condition also means the pin is being driven past the supply rails. */
    uint32_t tx_max_keyed_ms;  /**< Transmitter time-out, in milliseconds, or 0 to disable it. When a key-up lasts longer than this, the modem service task
                                  releases PTT, stops the modulator and discards the transmission, so a stalled transmit path cannot hold the channel
                                  indefinitely. */
    modem_rx_tuning_t rx;      /**< Receive-chain tuning: demodulator set, prefilter band, receive gate, high-pass, gain control and bit repair. */
} modem_config_t;

/**
 * @brief Build a ::modem_config_t initializer with sensible default
 *        values: Bell 202 modem, standard (de-emphasized) audio, full
 *        duplex enabled, strict APRS frame filtering, 300 ms preamble, no
 *        CSMA slot time, the standard AX.25/KISS Persist default (63, ~25%
 *        transmit chance per clear slot), FX.25 disabled, the PTT polarity
 *        taken from ::MODEM_PTT_ACTIVE_HIGH and no extra unkey hold.
 *
 * The audio interface fields take the values that suit an interface board
 * carrying its own bias network, attenuators and reconstruction filter: the
 * compile-time DAC sample rate and output swing, no ADC input self-bias, no
 * over-range warning and no transmitter time-out. The receive chain takes
 * ::MODEM_RX_TUNING_DEFAULT.
 */
#define MODEM_DEFAULT_CONFIG()                                                                                                                                 \
    {                                                                                                                                                          \
        .modem = MODEM_MODEM_BELL202,                                                                                                                          \
        .flat_audio = false,                                                                                                                                   \
        .full_duplex = true,                                                                                                                                   \
        .allow_non_aprs = false,                                                                                                                               \
        .preamble_ms = 300,                                                                                                                                    \
        .slot_time_ms = 0,                                                                                                                                     \
        .persist = 63,                                                                                                                                         \
        .fx25_mode = 0,                                                                                                                                        \
        .ptt_active_high = MODEM_PTT_ACTIVE_HIGH ? true : false,                                                                                               \
        .min_unkey_ms = 0,                                                                                                                                     \
        .dac_samplerate = MODEM_DAC_SAMPLERATE,                                                                                                                \
        .dac_amplitude_pct = MODEM_DAC_AMPLITUDE_PCT,                                                                                                          \
        .adc_self_bias = false,                                                                                                                                \
        .rx_clip_warn = false,                                                                                                                                 \
        .tx_max_keyed_ms = 0,                                                                                                                                  \
        .rx = MODEM_RX_TUNING_DEFAULT(),                                                                                                                       \
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
    uint8_t demod;        /**< Index of the demodulator that produced the frame, 0 .. ::MODEM_RX_MAX_DEMODULATORS - 1. */
    int8_t twist_db;      /**< Estimated tone twist of the received signal at the modem input, dB: level of the space tone relative to the mark
                             tone, with the demodulator's own prefilter tilt removed. 0 for the G3RUH profile, which has no tones. */
    uint8_t repaired;     /**< Number of bits flipped by bit repair to make the FCS match, 0 for a frame received intact. */
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
 *
 * Safe to call from any task: the transmit path is serialized internally, so
 * concurrent callers are queued one after another rather than interleaved.
 *
 * @param frame Frame content, address field onwards: no HDLC flags, no bit
 *              stuffing and no FCS; all three are added automatically by
 *              the modulator.
 * @param len   Length, in bytes, of @p frame.
 * @return ESP_OK if the frame was queued successfully.
 *         ESP_ERR_INVALID_ARG if @p frame is NULL or @p len is zero.
 *         ESP_ERR_INVALID_STATE if modem_init() has not run yet.
 *         ESP_ERR_TIMEOUT if the transmit path could not be acquired.
 *         ESP_ERR_NO_MEM if the transmit ring is full (frame dropped).
 */
esp_err_t modem_send_raw(const uint8_t *frame, uint16_t len);

/**
 * @brief Build a raw AX.25 frame from a TNC2-style monitor string.
 *
 * Safe to call from any task: the encoder state this shares with the rest of
 * the transmit path is held under the same internal lock.
 *
 * @param tnc2    Monitor string, e.g. "NOCALL-1>APE32I,WIDE1-1:>hello".
 * @param out     Destination buffer for the built raw AX.25 frame.
 * @param out_len Size, in bytes, of @p out.
 * @return Length, in bytes, of the built frame, or 0 if any argument is
 *         unusable, if @p tnc2 is malformed or does not fit, if modem_init()
 *         has not run yet, or if the transmit path could not be acquired.
 */
int modem_build_frame_tnc2(const char *tnc2, uint8_t *out, size_t out_len);

/**
 * @brief Build a frame from a TNC2-style monitor string and queue it for
 *        transmission in a single call.
 *
 * Build and queue happen under one acquisition of the internal transmit lock,
 * so the frame reaches the transmit ring carrying the checksum accumulated for
 * it. This is the entry point every beacon, IGate relay and message transmit
 * uses, and they may call it concurrently.
 *
 * @param tnc2 Monitor string, e.g. "NOCALL-1>APE32I,WIDE1-1:>hello".
 * @return ESP_OK if the frame was queued successfully.
 *         ESP_ERR_INVALID_ARG if @p tnc2 is NULL or could not be encoded.
 *         ESP_ERR_INVALID_STATE if modem_init() has not run yet.
 *         ESP_ERR_TIMEOUT if the transmit path could not be acquired.
 *         ESP_ERR_NO_MEM if the transmit ring is full (frame dropped).
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
 * @brief Count how many frames are currently queued/in flight on RF TX.
 * @return Number of frames still pending transmission (0 = idle).
 */
uint8_t modem_tx_queue_depth(void);

/**
 * @brief Count how many times the CSMA/p-persistent anti-starvation floor
 *        has forced a transmission on a clear channel since boot.
 *
 * Counts only the runs in which the channel stayed free for every backoff
 * slot and the persistence roll missed each time; the modem then transmits
 * rather than holding the frame indefinitely. Nothing is discarded, so this
 * is a channel-access statistic and not a drop: it reflects the configured
 * `persist` probability, and with the standard value of 63 roughly one
 * key-up in ten is expected to end this way.
 * @return Total number of forced transmissions caused by missed persistence
 *         rolls on a clear channel since boot.
 */
uint32_t modem_persistence_missed_count(void);

/**
 * @brief Count how many times the CSMA/p-persistent anti-starvation floor
 *        has forced a transmission over a busy channel since boot.
 *
 * Counts the runs in which at least one backoff slot found the carrier
 * detect asserted and the channel still had not cleared by the end of the
 * run; the frame is then transmitted on top of the traffic already there.
 * A climbing figure here is a congestion report about the frequency, not a
 * fault in this station.
 * @return Total number of forced transmissions over a busy channel since boot.
 */
uint32_t modem_channel_busy_count(void);

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

/**
 * @brief Read the receive statistics.
 * @param out Destination structure. Ignored if NULL.
 */
void modem_get_rx_stats(modem_rx_stats_t *out);

/**
 * @brief Clear every counter reported by modem_get_rx_stats().
 */
void modem_reset_rx_stats(void);

#endif /* ESP32IDF_RADIOAMATEUR_MODEM_H_ */
