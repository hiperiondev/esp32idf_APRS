/**
 * @file esp32idf_radioamateur_modem_config.h
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
 * @brief Compile-time configuration.
 *
 * Every value that must be adapted to a particular board lives here. All
 * values may be overridden from the build system, for example:
 */

#ifndef ESP32IDF_RADIOAMATEUR_MODEM_CONFIG_H_
#define ESP32IDF_RADIOAMATEUR_MODEM_CONFIG_H_

/* SOC_ADC_DIGI_RESULT_BYTES / SOC_ADC_DIGI_DATA_BYTES_PER_CONV are used by the
 * derived-value checks at the bottom of this file. */
#include "soc/soc_caps.h"

/* =========================================================================
 *  MODEM AUDIO OUTPUT  --  DAC
 * ========================================================================= */

/**
 * @brief GPIO used for the modulated audio output (DAC).
 *
 * The ESP32 has two 8-bit DACs, hard-wired to fixed pads and not routable
 * through the GPIO matrix, so only these two pins can ever be used:
 *  - GPIO25 -> DAC channel 1 (DAC_CHAN_0), the default.
 *  - GPIO26 -> DAC channel 2 (DAC_CHAN_1).
 *
 * Any other value produces a compile error further down in this file.
 *
 * @note ESP32-S2 also has DACs (GPIO17/GPIO18). ESP32-S3/C3/C6/H2 have no
 *       DAC at all and would require an external DAC, PWM or sigma-delta
 *       output instead.
 */
#ifndef MODEM_DAC_GPIO
#define MODEM_DAC_GPIO 25
#endif

/* =========================================================================
 *  MODEM AUDIO INPUT  --  ADC
 * ========================================================================= */

/**
 * @brief GPIO used for the received audio input (ADC).
 *
 * The receiver uses ADC1 in continuous (DMA) mode. Allowed pins are the
 * eight ADC1 pads of the ESP32:
 *  - GPIO36 -> ADC1_CH0 (labelled SENSOR_VP / VP)
 *  - GPIO37 -> ADC1_CH1 (not bonded out on most modules)
 *  - GPIO38 -> ADC1_CH2 (not bonded out on most modules)
 *  - GPIO39 -> ADC1_CH3 (labelled SENSOR_VN / VN)
 *  - GPIO32 -> ADC1_CH4
 *  - GPIO33 -> ADC1_CH5
 *  - GPIO34 -> ADC1_CH6 (input only, no internal pull-up/down)
 *  - GPIO35 -> ADC1_CH7 (input only, no internal pull-up/down), the default
 *
 * ADC2 pins (GPIO0/2/4/12..15/25..27) are deliberately NOT supported: ADC2
 * is shared with the Wi-Fi radio and cannot be used by the continuous-mode
 * driver on the ESP32.
 *
 * Any other value produces a compile error further down in this file.
 */
#ifndef MODEM_ADC_GPIO
#define MODEM_ADC_GPIO 33
#endif

/* =========================================================================
 *  OPTIONAL CONTROL PINS   (set to -1 to disable)
 * ========================================================================= */

/**
 * @brief GPIO used to drive PTT (push-to-talk), or -1 to disable it.
 *
 * Any output-capable GPIO may be used (0-19, 21-23, 25-27, 32, 33).
 * GPIO34-39 are input-only and cannot be used. Not needed for the loopback
 * test.
 */
#ifndef MODEM_PTT_GPIO
#define MODEM_PTT_GPIO (-1)
#endif

/**
 * @brief Active level of the PTT output.
 * @details 1 = PTT is active-high, 0 = PTT is active-low.
 */
#ifndef MODEM_PTT_ACTIVE_HIGH
#define MODEM_PTT_ACTIVE_HIGH 1
#endif

/**
 * @brief GPIO used to drive the TX status LED, or -1 to disable it. Any
 *        output-capable GPIO may be used.
 */
#ifndef MODEM_LED_TX_GPIO
#define MODEM_LED_TX_GPIO (-1)
#endif

/**
 * @brief GPIO used to drive the RX/DCD status LED, or -1 to disable it. Any
 *        output-capable GPIO may be used.
 */
#ifndef MODEM_LED_RX_GPIO
#define MODEM_LED_RX_GPIO (-1)
#endif

/* =========================================================================
 *  SAMPLE RATES / DSP
 * ========================================================================= */

/**
 * @brief DAC (transmit) sample rate, in Hz.
 *
 * The modulator's sine lookup table and the baud-rate divider are both
 * derived from this value. 38400 = 32 * 1200, which keeps it an exact
 * multiple of every supported baud rate; keep this property if the value is
 * ever changed.
 */
#ifndef MODEM_DAC_SAMPLERATE
#define MODEM_DAC_SAMPLERATE 38400
#endif

/**
 * @brief ADC (receive) sample rate, in Hz.
 *
 * 76800 = 8 x 9600, and the factor of 8 is the point.
 *
 * This was 38400 and that is what held G3RUH to 3/5 frames. 38400 gives the
 * 9600 Bd profile exactly FOUR ADC samples per symbol, and the demodulator's
 * timing recovery cannot work with four: its sample instant is quantised to one
 * ADC period (25 % of a symbol) and decode()'s three-sample majority vote spans
 * 75 % of a symbol, so the vote window always reaches into a transition. Host
 * simulation of the real modem.c, with both real clocks and the measured
 * analogue path and NO noise, produces hard bit errors at every sampling phase
 * where the ADC instants line up with the DAC's update instants - and since the
 * two clocks differ by ~0.05 %, the alignment walks through those phases every
 * ~55 ms, several times per transmission. At 76800 the same simulation gives
 * zero bit errors at every phase and with up to 30 us of TX edge jitter.
 *
 * The AFSK profiles never cared: they are demodulated at 9600 Hz through a
 * correlator after decimation, so they see no baseband edge to mis-sample, and
 * they measure identically at either rate (verified on the host).
 *
 * Constraints: at least 20 kHz and at most 2 MHz (SOC_ADC_SAMPLE_FREQ_THRES_LOW
 * / _HIGH on this chip), and an exact integer multiple of 9600. The cost of the
 * change is that the RX DSP runs on twice as many samples and
 * ::MODEM_RESAMPLE_RATIO becomes 8, which needs the longer decimation FIR in
 * afsk.c - an 8-tap filter cut for a 4:1 ratio does not anti-alias a 8:1 one.
 *
 * The DAC rate is deliberately NOT changed with it. The transmitter puts symbol
 * edges exactly on DAC samples whatever the rate; it is the receiver that needed
 * the resolution.
 */
#ifndef MODEM_ADC_SAMPLERATE
#define MODEM_ADC_SAMPLERATE 76800
#endif

#ifndef MODEM_ADC_RATE_NUM
#define MODEM_ADC_RATE_NUM 1
#endif

/**
 * @brief Denominator of the fudge factor applied to the requested ADC rate.
 *        See ::MODEM_ADC_RATE_NUM for details.
 */
#ifndef MODEM_ADC_RATE_DEN
#define MODEM_ADC_RATE_DEN 1
#endif

/**
 * @brief Peak-to-peak swing of the DAC output, as a percentage of the full
 *        0..3.3 V range.
 *
 * 100% would clip against the ADC full scale even at 12 dB attenuation
 * (approximately 3.1 V), so some headroom must be kept when wiring GPIO25
 * directly to GPIO35 for the loopback test.
 */
#ifndef MODEM_DAC_AMPLITUDE_PCT
#define MODEM_DAC_AMPLITUDE_PCT 60
#endif

/**
 * @brief ADC input attenuation setting.
 *
 * For a direct GPIO25 -> GPIO35 loopback, the DAC idles at approximately
 * 1.65 V, which lies far outside the 0 dB attenuation window (0..950 mV),
 * so 12 dB attenuation is required.
 */
#ifndef MODEM_ADC_ATTEN
#define MODEM_ADC_ATTEN ADC_ATTEN_DB_12
#endif

/**
 * @brief Size, in samples, of the RX sample ring buffer (FIFO) between
 *        adc_ingest() and AFSK_Poll(). Must be a power of two.
 *
 * Sized in SAMPLES, so it shrank in TIME when ::MODEM_ADC_SAMPLERATE doubled:
 * 2048 samples was 53 ms at 38400 Hz but only 26.7 ms at 76800, i.e. barely one
 * 20 ms block. 4096 restores the old margin. The check below enforces the part
 * that actually matters - AFSK_Poll() drains in whole blocks, so a FIFO that
 * cannot hold one has nothing to give it.
 */
#ifndef MODEM_RX_FIFO_SIZE
#define MODEM_RX_FIFO_SIZE 4096
#endif

/**
 * @brief Size, in samples, of one ADC DMA conversion frame.
 *
 * This is deliberately NOT ::MODEM_BLOCK_SIZE, and the difference is worth
 * a paragraph because getting it wrong costs G3RUH every other frame.
 *
 * The IDF's own ADC ISR (adc_dma_intr() in esp_adc/adc_continuous.c) hands
 * each finished frame to a FreeRTOS ring buffer with:
 *
 *      xRingbufferSendFromISR(ringbuf_hdl, finished_buffer, finished_size, ...)
 *
 * and xRingbufferSendFromISR() does its memcpy() - all `finished_size` bytes
 * of it - plus a task unblock inside portENTER_CRITICAL_ISR(). On Xtensa that
 * raises PS.INTLEVEL to XCHAL_EXCM_LEVEL, which is 3. The DAC sample clock is
 * a level 3 interrupt (see AFSK_init()), so it is MASKED for the whole copy.
 * No amount of IRAM_ATTR on our side changes that: the code doing the blocking
 * is the driver's, it is already in IRAM, and it is simply long.
 *
 * The freeze length is therefore the frame size, near enough linearly:
 *
 *      768 samples = 1536 B  ->  ~11 us   (10 % of a 9600 Bd symbol: fatal)
 *      128 samples =  256 B  ->  ~2 us    (2 % of a symbol: inside budget)
 *
 * At 1200 Bd an 11 us displacement is 1.3 % of a symbol and invisible, which
 * is exactly why every AFSK profile passed while G3RUH dropped frames and
 * stage 2 of the diagnostics blamed a flash fetch that was not there.
 *
 * Making this smaller costs only ISR entries (128 samples at 38400 Hz is one
 * every 3.3 ms instead of every 20 ms) and buys back the symbol edge. The DSP
 * is unaffected: AFSK_Poll() still consumes whole ::MODEM_BLOCK_SIZE blocks
 * out of the FIFO, which is what the FIFO is for.
 *
 * Constraints: must be EVEN (the ESP32 DMA pair-swap un-doing in adc_ingest()
 * works within a frame), must make conv_frame_size a multiple of
 * SOC_ADC_DIGI_DATA_BYTES_PER_CONV, and must divide ::MODEM_BLOCK_SIZE so a
 * whole number of frames makes a block.
 */
#ifndef MODEM_ADC_CONV_FRAME
#define MODEM_ADC_CONV_FRAME 128
#endif

/**
 * @brief Depth of the ADC driver's internal pool, in conversion frames.
 *
 * This is the driver-side buffering between its ISR and adc_continuous_read().
 * It must cover the longest the RX task can be kept off the CPU. Like the FIFO
 * above, it is counted in frames and therefore shrinks in TIME as the sample
 * rate rises: 32 frames of 128 samples is 4096 samples = 53 ms at 76800 Hz,
 * matching the slack the original 4 x 20 ms pool had, at a fraction of its ISR
 * critical section.
 */
#ifndef MODEM_ADC_POOL_FRAMES
#define MODEM_ADC_POOL_FRAMES 32
#endif

/**
 * @brief FreeRTOS priority of the internal RX DSP task.
 */
#ifndef MODEM_RX_TASK_PRIO
#define MODEM_RX_TASK_PRIO 10
#endif

/**
 * @brief Stack size, in bytes, allocated to the internal RX DSP task.
 */
#ifndef MODEM_RX_TASK_STACK
#define MODEM_RX_TASK_STACK 4096
#endif

/**
 * @brief CPU core the RX DSP task is pinned to, or -1 for no affinity.
 *
 * Keep this on ::MODEM_ADC_ISR_CORE. The RX task and the ADC ISR share a
 * ring buffer and therefore share its spinlock; putting them on the same core
 * means that spinlock is only ever contended locally, and - far more
 * importantly - it keeps both of them off the core that carries the DAC
 * sample clock.
 */
#ifndef MODEM_RX_TASK_CORE
#define MODEM_RX_TASK_CORE 0
#endif

/**
 * @brief CPU core the ADC DMA interrupt is allocated on.
 *
 * esp_intr_alloc() binds an interrupt to whichever core calls it, so this is
 * enforced by doing the ADC bring-up from a task pinned here rather than by
 * hoping modem_init() happens to be called from the right place.
 */
#ifndef MODEM_ADC_ISR_CORE
#define MODEM_ADC_ISR_CORE 0
#endif

/**
 * @brief CPU core the DAC sample-clock (GPTimer) interrupt is allocated on.
 *
 * This MUST differ from ::MODEM_ADC_ISR_CORE, and that is the whole point.
 *
 * portENTER_CRITICAL_ISR() masks interrupts up to level 3 on the LOCAL core
 * only. The ADC driver's ISR holds such a critical section across a memcpy of
 * a whole conversion frame (see ::MODEM_ADC_CONV_FRAME); while it does, a
 * level 3 GPTimer alarm on the same core cannot run and the modulator freezes
 * mid-symbol. On the other core it is untouched: the ADC ISR spins for the
 * lock, it does not mask us.
 *
 * Shrinking the frame size and separating the cores are independent fixes and
 * both are applied here - the frame size bounds the damage if anything else
 * ever takes a long critical section, the core split removes this particular
 * one entirely.
 *
 * Set to -1 to allocate on whichever core calls AFSK_init() (single-core
 * targets, or if you have a reason). The compile-time check below is then
 * skipped and you are on your own.
 */
#ifndef MODEM_DAC_TIMER_CORE
#define MODEM_DAC_TIMER_CORE 1
#endif

/**
 * @brief Priority of the DAC sample clock interrupt (1..3).
 *
 * 3 is the highest an ESP-IDF C handler can be given. Note that this does not
 * make it immune to portENTER_CRITICAL on its own core - level 3 IS the level
 * critical sections mask to. See ::MODEM_DAC_TIMER_CORE.
 */
#ifndef MODEM_DAC_TIMER_INTR_PRIO
#define MODEM_DAC_TIMER_INTR_PRIO 3
#endif

/* =========================================================================
 *  Derived values - do not edit below this line
 * ========================================================================= */

#if MODEM_DAC_GPIO == 25
/** @brief DAC channel corresponding to ::MODEM_DAC_GPIO, derived automatically. */
#define MODEM_DAC_CHANNEL DAC_CHAN_0
#elif MODEM_DAC_GPIO == 26
#define MODEM_DAC_CHANNEL DAC_CHAN_1
#else
#error "MODEM_DAC_GPIO must be 25 (DAC1) or 26 (DAC2): the ESP32 DAC is not routable."
#endif

#if MODEM_ADC_GPIO == 36
/** @brief ADC1 channel corresponding to ::MODEM_ADC_GPIO, derived automatically. */
#define MODEM_ADC_CHANNEL ADC_CHANNEL_0
#elif MODEM_ADC_GPIO == 37
#define MODEM_ADC_CHANNEL ADC_CHANNEL_1
#elif MODEM_ADC_GPIO == 38
#define MODEM_ADC_CHANNEL ADC_CHANNEL_2
#elif MODEM_ADC_GPIO == 39
#define MODEM_ADC_CHANNEL ADC_CHANNEL_3
#elif MODEM_ADC_GPIO == 32
#define MODEM_ADC_CHANNEL ADC_CHANNEL_4
#elif MODEM_ADC_GPIO == 33
#define MODEM_ADC_CHANNEL ADC_CHANNEL_5
#elif MODEM_ADC_GPIO == 34
#define MODEM_ADC_CHANNEL ADC_CHANNEL_6
#elif MODEM_ADC_GPIO == 35
#define MODEM_ADC_CHANNEL ADC_CHANNEL_7
#else
#error "MODEM_ADC_GPIO must be one of 32..39: only ADC1 works in continuous mode."
#endif

#if (MODEM_ADC_SAMPLERATE % 9600) != 0
#error "MODEM_ADC_SAMPLERATE must be an integer multiple of 9600."
#endif

/**
 * @brief Sample rate, in Hz, at which the demodulator processes audio.
 *        Fixed at 9600 Hz regardless of the ADC's raw sample rate.
 */
#define MODEM_DEMOD_SAMPLERATE 9600

/**
 * @brief Integer decimation ratio applied between the raw ADC stream and
 *        the demodulator input.
 */
#define MODEM_RESAMPLE_RATIO (MODEM_ADC_SAMPLERATE / MODEM_DEMOD_SAMPLERATE)

/**
 * @brief Number of samples in one DSP processing block, corresponding to
 *        20 ms of audio at ::MODEM_ADC_SAMPLERATE.
 */
#define MODEM_BLOCK_SIZE (MODEM_ADC_SAMPLERATE / 50)

/**
 * @brief Size, in bytes, of one ADC DMA conversion frame.
 */
#define MODEM_ADC_CONV_FRAME_BYTES (MODEM_ADC_CONV_FRAME * SOC_ADC_DIGI_RESULT_BYTES)

#if MODEM_RX_FIFO_SIZE < (2 * MODEM_BLOCK_SIZE)
#error "MODEM_RX_FIFO_SIZE must hold at least two MODEM_BLOCK_SIZE blocks: AFSK_Poll() only consumes whole blocks."
#endif

#if (MODEM_ADC_CONV_FRAME % 2) != 0
#error "MODEM_ADC_CONV_FRAME must be even: adc_ingest() un-swaps DMA sample pairs within a frame."
#endif

#if (MODEM_BLOCK_SIZE % MODEM_ADC_CONV_FRAME) != 0
#error "MODEM_ADC_CONV_FRAME must divide MODEM_BLOCK_SIZE."
#endif

#if (MODEM_ADC_CONV_FRAME * SOC_ADC_DIGI_RESULT_BYTES % SOC_ADC_DIGI_DATA_BYTES_PER_CONV) != 0
#error "MODEM_ADC_CONV_FRAME * SOC_ADC_DIGI_RESULT_BYTES must be a multiple of SOC_ADC_DIGI_DATA_BYTES_PER_CONV."
#endif

/*
 * The two interrupts must not share a core. See MODEM_DAC_TIMER_CORE: this
 * is not a performance preference, it is the difference between the DAC ISR
 * being masked for the length of an ADC frame memcpy and not being masked at
 * all.
 */
#if (MODEM_DAC_TIMER_CORE >= 0) && (MODEM_DAC_TIMER_CORE == MODEM_ADC_ISR_CORE)
#error                                                                                                                                                         \
    "MODEM_DAC_TIMER_CORE must differ from MODEM_ADC_ISR_CORE: the ADC ISR's ring-buffer critical section masks level 3 on its own core, which is where the DAC sample clock lives."
#endif

#if (MODEM_DAC_TIMER_INTR_PRIO < 1) || (MODEM_DAC_TIMER_INTR_PRIO > 3)
#error "MODEM_DAC_TIMER_INTR_PRIO must be 1..3 (an ESP-IDF C interrupt handler cannot be given a higher level)."
#endif

#endif /* ESP32IDF_RADIOAMATEUR_MODEM_CONFIG_H_ */
