/**
 * @file rx_dc_block.h
 *
 * @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
 * @date 2026
 * @copyright GNU General Public License v3
 * @see https://github.com/hiperiondev/esp32idf_APRS
 *
 * @brief DC removal of the raw ADC stream of the receive chain.
 *
 * The ADC delivers the audio riding on the bias of its input pin, about
 * mid-scale. The DC level is tracked by a first-order low-pass on the raw
 * samples and subtracted from each of them, which makes the whole block a
 * first-order high-pass with its corner at about
 * ::MODEM_ADC_SAMPLERATE * 2^-::RX_DC_BLOCK_SHIFT / (2 * pi) - 6 Hz at
 * 76800 Hz. Its gain is flat within 0.1 dB from 60 Hz upwards, so it neither
 * lifts nor cuts the bass, the CTCSS or the tones: shaping the band is left
 * to the high-pass (rx_hpf.h) and the prefilters.
 *
 * The tracked level is kept in fixed point with ::RX_DC_BLOCK_FRAC_BITS
 * fraction bits and updated with an arithmetic shift, so the estimate is exact
 * integer arithmetic and settles on the true mean however small the
 * difference between it and the input becomes.
 *
 * Header-only (static inline), so the firmware and the PC replay in
 * audio_test/rx_replay run exactly the same code.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Time constant of the DC tracker, as a power of two in samples.
 *
 * 11 gives 2048 samples: 27 ms and a corner of about 6 Hz at 76800 Hz, so
 * the tracker follows a change of the input bias within a fraction of a
 * second while staying far below the lowest CTCSS tone.
 */
#define RX_DC_BLOCK_SHIFT 11

/**
 * @brief Fixed-point position of ::rx_dc_block_t::level.
 *
 * With 12-bit ADC codes the scaled level stays below 2^28, inside int32.
 */
#define RX_DC_BLOCK_FRAC_BITS 16

/**
 * @brief State of one DC tracker.
 */
typedef struct {
    int32_t level; /**< Tracked DC level, ADC counts with ::RX_DC_BLOCK_FRAC_BITS fraction bits. */
    bool primed;   /**< The level holds a real estimate. */
} rx_dc_block_t;

/**
 * @brief Clear a DC tracker. The next sample fed to it becomes its starting
 *        level, so the output starts near zero instead of swinging by the
 *        whole bias.
 * @param b Tracker to clear.
 */
static inline void rx_dc_block_reset(rx_dc_block_t *b) {
    b->level = 0;
    b->primed = false;
}

/**
 * @brief Feed one raw ADC sample and get it back with the DC removed.
 * @param b   Tracker state.
 * @param raw Raw ADC code.
 * @return The sample minus the tracked DC level, in ADC counts.
 */
static inline float rx_dc_block_step(rx_dc_block_t *b, int16_t raw) {
    const int32_t x = (int32_t)raw << RX_DC_BLOCK_FRAC_BITS;

    if (!b->primed) {
        b->level = x;
        b->primed = true;
    }
    b->level += (x - b->level) >> RX_DC_BLOCK_SHIFT;
    return (float)(x - b->level) * (1.0f / (float)(1 << RX_DC_BLOCK_FRAC_BITS));
}

/**
 * @brief Tracked DC level, rounded to a whole ADC code.
 * @param b Tracker state.
 * @return DC level, ADC counts.
 */
static inline int rx_dc_block_level(const rx_dc_block_t *b) {
    return (int)((b->level + (1 << (RX_DC_BLOCK_FRAC_BITS - 1))) >> RX_DC_BLOCK_FRAC_BITS);
}
