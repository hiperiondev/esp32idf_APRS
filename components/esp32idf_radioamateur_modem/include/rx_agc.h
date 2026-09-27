/**
 * @file rx_agc.h
 *
 * @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
 * @date 2026
 * @copyright GNU General Public License v3
 * @see https://github.com/hiperiondev/esp32idf_APRS
 *
 * @brief Automatic gain control of the AFSK receive chain.
 *
 * The gain control runs on the decimated signal of the AFSK profiles, one
 * 20 ms block at a time, just ahead of the demodulators. Its job is to hand
 * them an input large enough for their fixed-point arithmetic: the designed
 * prefilters are normalized to a unity passband peak, so with a strong tilt
 * the weaker tone reaches the correlators well below the input level, and on
 * an input only a few ADC counts high it would be carried by one or two
 * integer counts.
 *
 * The level is measured on the whole decimated band rather than on the tone
 * band. Bass and hum that the high-pass leaves in the signal therefore count
 * towards the level and keep the gain low enough that they do not reach the
 * +-2047 clamp of the demodulator input; a gain set by the tones alone would
 * drive a bass-heavy speaker output into that clamp.
 *
 * A block quieter than ::AGC_SQUELCH_RMS, measured before the gain, holds
 * the gain where it is. The test is on the input itself, so any signal above
 * the converter's own noise is brought up to the target whatever the gain
 * currently is; only a block that carries nothing but converter noise is
 * ignored. On a channel fed continuously (receive gate off) the gain rises
 * slowly while the channel is idle and comes back down within a few blocks
 * when a transmission starts, during which the demodulator input is clamped:
 * a clamped AFSK signal keeps its tone frequencies, so the transmission is
 * still demodulated.
 *
 * Header-only (static inline), so the firmware and the PC replay in
 * audio_test/rx_replay run exactly the same code.
 */

#pragma once

#include <math.h>
#include <stddef.h>

/**
 * @brief Level the gain control brings the decimated signal to, RMS as a
 *        fraction of the ADC half-range (0.2 is -14 dBFS, 410 ADC counts).
 */
#define AGC_TARGET_RMS 0.2f

/**
 * @brief Fraction of the gain error corrected per block when the gain comes
 *        down (the signal is too loud).
 *
 * With ::AGC_MAX_STEP bounding the error to 0.5 per block, 0.25 lowers the
 * gain by an eighth per 20 ms block and halves it in about 100 ms, so an
 * overdriven input is clamped only briefly.
 */
#define AGC_ATTACK 0.25f

/**
 * @brief Fraction of the gain error corrected per block when the gain goes
 *        up (the signal is too quiet).
 *
 * Slow, so the noise between frames does not pump the gain: at most 0.2 %
 * per 20 ms block.
 */
#define AGC_RELEASE 0.002f

/** @brief Highest gain the control applies (+18 dB). */
#define AGC_MAX_GAIN 8.0f

/** @brief Lowest gain the control applies (-20 dB). */
#define AGC_MIN_GAIN 0.1f

/**
 * @brief Input level, RMS as a fraction of the ADC half-range and measured
 *        before the gain, below which a block holds the gain.
 *
 * 0.002 is about 4 ADC counts RMS on the decimated signal (about 3.3 mV RMS
 * at the pin), above the converter's own noise once it has been through the
 * decimation filter and below the quietest input the demodulators can still
 * decode.
 */
#define AGC_SQUELCH_RMS 0.002f

/**
 * @brief Largest factor by which the gain error is counted in one block.
 *
 * Bounds the correction a single block can ask for, so one near-silent or one
 * very loud block cannot slam the gain to a rail.
 */
#define AGC_MAX_STEP 2.0f

/**
 * @brief Advance the gain control by one block.
 *
 * @param gain Gain applied to the previous block.
 * @param buf  Decimated block, samples as a fraction of the ADC half-range,
 *             before the gain.
 * @param len  Number of samples in @p buf.
 * @return Gain to apply to this block, within ::AGC_MIN_GAIN ..
 *         ::AGC_MAX_GAIN. Equal to @p gain when the block is quieter than
 *         ::AGC_SQUELCH_RMS or @p len is 0.
 */
static inline float rx_agc_update(float gain, const float *buf, size_t len) {
    if (len == 0)
        return gain;

    float sum_sq = 0.0f;
    for (size_t i = 0; i < len; i++)
        sum_sq += buf[i] * buf[i];

    const float input = sqrtf(sum_sq / (float)len);
    if (input < AGC_SQUELCH_RMS)
        return gain;

    float error = AGC_TARGET_RMS / (input * gain);
    if (error > AGC_MAX_STEP)
        error = AGC_MAX_STEP;
    else if (error < 1.0f / AGC_MAX_STEP)
        error = 1.0f / AGC_MAX_STEP;

    const float rate = (error < 1.0f) ? AGC_ATTACK : AGC_RELEASE;

    gain += (gain * error - gain) * rate;
    return fmaxf(fminf(gain, AGC_MAX_GAIN), AGC_MIN_GAIN);
}
