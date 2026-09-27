/**
 * @file impulse_blanker.h
 * @brief Removal of isolated glitches from the raw ADC sample stream.
 *
 * The ESP32's ADC picks up short glitches while the Wi-Fi radio is active,
 * in bursts that repeat with the beacon interval (102.4 ms). Each glitch
 * lasts one or a few conversions at the ADC rate, but once it has gone
 * through the decimation filter it is spread over the whole filter length
 * and lands in the tone band as a click strong enough to corrupt the bits
 * under it. At the ADC rate the audio is heavily oversampled - a tone below
 * 2.6 kHz moves by at most about a fifth of its amplitude from one conversion
 * to the next, and far less than that away from what its neighbours predict
 * - so a glitch stands out clearly before decimation and can be replaced
 * there without touching the audio.
 *
 * Each sample is compared with the median of the five samples centred on it
 * (two on either side). For a tone below 2.6 kHz at 76.8 kHz that median
 * differs from the sample by at most about two percent of the amplitude,
 * while a glitch of one or two conversions cannot move it, so the median is
 * a reliable detector. A sample whose distance from the median exceeds
 * ::IMPULSE_BLANK_FACTOR times the running mean distance (at least
 * ::IMPULSE_BLANK_MIN_COUNTS), plus ::IMPULSE_BLANK_STEP_FACTOR times the
 * local step between samples, is replaced by an interpolation from the
 * neighbours that are not glitches themselves, which follows a steep tone
 * far more closely than the median would. The judgement always uses the raw samples, never earlier replacements, so a
 * mistake cannot feed into the next decision, and the running mean learns
 * only from samples that are kept, so a burst of glitches cannot raise its
 * own threshold. Glitches of three or more consecutive conversions are
 * beyond a five-sample median and pass.
 *
 * Header-only (static inline), so the firmware and the PC replay in
 * audio_test/rx_replay run exactly the same code.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** @brief Replacement threshold, as a multiple of the running mean distance from the median. */
#define IMPULSE_BLANK_FACTOR 8

/**
 * @brief Smallest distance from the median, in ADC counts, that is ever
 *        treated as a glitch, whatever the running mean. Keeps the blanker
 *        from reacting to the converter's own noise on a quiet input.
 */
#define IMPULSE_BLANK_MIN_COUNTS 24

/**
 * @brief Threshold added per ADC count of local step between consecutive
 *        samples: on a steep stretch of audio a clean sample can lie a couple
 *        of steps from the window median.
 */
#define IMPULSE_BLANK_STEP_FACTOR 3

/**
 * @brief Time constant of the running mean distance, as a power of two in
 *        samples (12: 4096 samples, 53 ms at 76.8 kHz).
 */
#define IMPULSE_BLANK_MEAN_SHIFT 12

/** @brief Fixed-point position of ::impulse_blanker_t::mean_err. */
#define IMPULSE_BLANK_MEAN_BITS 12

/**
 * @brief State of one impulse blanker.
 */
typedef struct {
    int16_t h[5];      /**< Last five raw input samples, oldest first; h[2] is the one being judged. */
    bool primed;       /**< The history holds real samples. */
    int32_t mean_err;  /**< Running mean distance from the median, in counts with ::IMPULSE_BLANK_MEAN_BITS fraction bits. */
    uint32_t repaired; /**< Samples replaced since the last impulse_blanker_reset(). */
} impulse_blanker_t;

/**
 * @brief Clear a blanker, including its count of repaired samples.
 * @param b Blanker to clear.
 */
static inline void impulse_blanker_reset(impulse_blanker_t *b) {
    for (int i = 0; i < 5; i++)
        b->h[i] = 0;
    b->primed = false;
    // Starts where the threshold equals its floor.
    b->mean_err = ((int32_t)IMPULSE_BLANK_MIN_COUNTS << IMPULSE_BLANK_MEAN_BITS) / IMPULSE_BLANK_FACTOR;
    b->repaired = 0;
}

/**
 * @brief Median of five values (seven compare-exchanges).
 */
static inline int16_t impulse_blanker_median5(int16_t a, int16_t b, int16_t c, int16_t d, int16_t e) {
    int16_t t;
#define IB_SORT(x, y)                                                                                                                                          \
    if ((x) > (y)) {                                                                                                                                           \
        t = (x);                                                                                                                                               \
        (x) = (y);                                                                                                                                             \
        (y) = t;                                                                                                                                               \
    }
    IB_SORT(a, b);
    IB_SORT(d, e);
    IB_SORT(a, c);
    IB_SORT(b, c);
    IB_SORT(a, d);
    IB_SORT(c, d);
    IB_SORT(b, e);
    IB_SORT(b, c);
    IB_SORT(c, d);
#undef IB_SORT
    return c;
}

/**
 * @brief Feed one raw sample and get back the sample two conversions older,
 *        replaced if it was a glitch.
 *
 * The output always lags the input by two samples, with the blanker enabled
 * or not, so switching it does not shift the timing of the stream.
 *
 * @param b       Blanker state.
 * @param in      New raw ADC sample.
 * @param enabled When false the samples only pass through the delay.
 * @return The sample two conversions before @p in, possibly repaired.
 */
static inline int16_t impulse_blanker_step(impulse_blanker_t *b, int16_t in, bool enabled) {
    if (!b->primed) {
        for (int i = 0; i < 5; i++)
            b->h[i] = in;
        b->primed = true;
    }
    b->h[0] = b->h[1];
    b->h[1] = b->h[2];
    b->h[2] = b->h[3];
    b->h[3] = b->h[4];
    b->h[4] = in;

    const int16_t c = b->h[2];
    if (!enabled)
        return c;

    const int16_t med = impulse_blanker_median5(b->h[0], b->h[1], b->h[2], b->h[3], b->h[4]);
    int32_t err = (int32_t)c - (int32_t)med;
    if (err < 0)
        err = -err;

    int32_t thr = (int32_t)(((int64_t)b->mean_err * IMPULSE_BLANK_FACTOR) >> IMPULSE_BLANK_MEAN_BITS);
    if (thr < IMPULSE_BLANK_MIN_COUNTS)
        thr = IMPULSE_BLANK_MIN_COUNTS;

    // On a steep stretch of audio a clean sample can sit a couple of steps
    // from the window median; the local step, taken as the median of the four
    // differences so a glitch cannot inflate it, widens the threshold there.
    int16_t dv[4] = { (int16_t)(b->h[1] - b->h[0]), (int16_t)(b->h[2] - b->h[1]), (int16_t)(b->h[3] - b->h[2]), (int16_t)(b->h[4] - b->h[3]) };
    for (int i = 1; i < 4; i++) {
        int16_t t = dv[i];
        int j = i - 1;
        while ((j >= 0) && (dv[j] > t)) {
            dv[j + 1] = dv[j];
            j--;
        }
        dv[j + 1] = t;
    }
    int32_t step = ((int32_t)dv[1] + (int32_t)dv[2]) / 2;
    thr += IMPULSE_BLANK_STEP_FACTOR * ((step < 0) ? -step : step);

    if (err > thr) {
        // Interpolate across the glitch from the neighbours that are not
        // glitches themselves (judged against the same median): cubic
        // through both immediate neighbours when they are clean, linear
        // across a neighbour that is part of the same glitch.
        int32_t d1 = (int32_t)b->h[1] - med, d3 = (int32_t)b->h[3] - med;
        bool o1 = ((d1 < 0) ? -d1 : d1) > thr;
        bool o3 = ((d3 < 0) ? -d3 : d3) > thr;
        int32_t rep;
        if (!o1 && !o3)
            rep = (-(int32_t)b->h[0] + 4 * (int32_t)b->h[1] + 4 * (int32_t)b->h[3] - (int32_t)b->h[4]) / 6;
        else if (!o1)
            rep = (int32_t)b->h[1] + ((int32_t)b->h[4] - (int32_t)b->h[1]) / 3;
        else if (!o3)
            rep = (int32_t)b->h[0] + (((int32_t)b->h[3] - (int32_t)b->h[0]) * 2) / 3;
        else
            rep = ((int32_t)b->h[0] + (int32_t)b->h[4]) / 2;
        b->repaired++;
        return (int16_t)((rep < 0) ? 0 : ((rep > 4095) ? 4095 : rep));
    }
    b->mean_err += (((err << IMPULSE_BLANK_MEAN_BITS) - b->mean_err) >> IMPULSE_BLANK_MEAN_SHIFT);
    return c;
}
