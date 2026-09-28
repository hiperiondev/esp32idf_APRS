/**
 * @file rx_hpf.h
 *
 * @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
 * @date 2026
 * @copyright GNU General Public License v3
 * @see https://github.com/hiperiondev/esp32idf_APRS
 *
 * @brief High-pass filter of the AFSK receive chain.
 *
 * A fourth-order Butterworth high-pass, built as two second-order sections
 * from the bilinear transform, runs on the decimated signal ahead of the gain
 * control and the demodulators (::modem_rx_tuning_t::hpf_hz). It rejects
 * CTCSS tones and hum, and the bass of a de-emphasized speaker output, by
 * 24 dB per octave below its corner: at the 300 Hz default, 100 Hz CTCSS is
 * down by 38 dB and the 1200 Hz mark tone by less than 0.1 dB.
 *
 * Header-only (static inline), so the firmware and the PC replay in
 * audio_test/rx_replay run exactly the same code.
 */

#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/**
 * @brief Number of second-order sections of the high-pass; the filter order
 *        is twice this.
 */
#define RX_HPF_SECTIONS 2

/**
 * @brief One second-order section, direct form I.
 */
typedef struct {
    float b0, b1, b2; /**< Feed-forward coefficients. */
    float a1, a2;     /**< Feedback coefficients, sign as subtracted. */
    float x1, x2;     /**< Last two inputs. */
    float y1, y2;     /**< Last two outputs. */
} rx_hpf_section_t;

/**
 * @brief State of the high-pass.
 */
typedef struct {
    bool on;                                   /**< false when the high-pass is disabled; rx_hpf_run() then leaves the samples unchanged. */
    rx_hpf_section_t section[RX_HPF_SECTIONS]; /**< Cascaded sections. */
} rx_hpf_t;

/**
 * @brief Design the high-pass for a corner frequency and clear its state.
 * @param f  Filter to set up.
 * @param hz Corner (-3 dB) frequency, Hz; 0 disables the filter.
 * @param fs Sample rate of the signal the filter runs on, Hz.
 */
static inline void rx_hpf_setup(rx_hpf_t *f, uint16_t hz, float fs) {
    // Quality factors of the two pole pairs of a fourth-order Butterworth
    // response: 1 / (2 cos(pi/8)) and 1 / (2 cos(3 pi/8)).
    static const float q[RX_HPF_SECTIONS] = { 0.54119610f, 1.30656296f };

    memset(f, 0, sizeof(*f));
    f->on = (hz > 0);
    if (!f->on)
        return;

    const float k = tanf((float)M_PI * (float)hz / fs);
    for (int s = 0; s < RX_HPF_SECTIONS; s++) {
        rx_hpf_section_t *sec = &f->section[s];
        const float norm = 1.0f / (1.0f + k / q[s] + k * k);
        sec->b0 = norm;
        sec->b1 = -2.0f * norm;
        sec->b2 = norm;
        sec->a1 = 2.0f * (k * k - 1.0f) * norm;
        sec->a2 = (1.0f - k / q[s] + k * k) * norm;
    }
}

/**
 * @brief Run the high-pass over a block, in place.
 * @param f   Filter state.
 * @param buf Samples to filter.
 * @param len Number of samples in @p buf.
 */
static inline void rx_hpf_run(rx_hpf_t *f, float *buf, int len) {
    if (!f->on)
        return;

    for (int i = 0; i < len; i++) {
        float v = buf[i];
        for (int s = 0; s < RX_HPF_SECTIONS; s++) {
            rx_hpf_section_t *sec = &f->section[s];
            const float y = sec->b0 * v + sec->b1 * sec->x1 + sec->b2 * sec->x2 - sec->a1 * sec->y1 - sec->a2 * sec->y2;
            sec->x2 = sec->x1;
            sec->x1 = v;
            sec->y2 = sec->y1;
            sec->y1 = y;
            v = y;
        }
        buf[i] = v;
    }
}
