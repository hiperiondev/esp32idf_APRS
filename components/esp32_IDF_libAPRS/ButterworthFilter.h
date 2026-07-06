#ifndef BUTTERWORTH_FILTER_H_
#define BUTTERWORTH_FILTER_H_

#include <math.h>

typedef enum {
    BW_HIGHPASS,
    BW_LOWPASS,
} ButterworthPassType;

typedef struct {
    float c, a1, a2, a3, b1, b2;

    /* Array of input values, latest are in front */
    float inputHistory[2];

    /* Array of output values, latest are in front */
    float outputHistory[3];
} ButterworthFilter;

/**
 * @brief Zero-initialize a filter (call SetParameters before Update()).
 */
void ButterworthFilter_Init(ButterworthFilter *f);

/**
 * @brief Initialize and configure a filter in one call.
 */
void ButterworthFilter_InitWithParams(ButterworthFilter *f, float frequency, int sampleRate, ButterworthPassType passType, float resonance);

/**
 * @brief (Re)configure filter coefficients.
 */
void ButterworthFilter_SetParameters(ButterworthFilter *f, float frequency, int sampleRate, ButterworthPassType passType, float resonance);

/**
 * @brief Process one input sample, return filtered output.
 */
float ButterworthFilter_Update(ButterworthFilter *f, float newInput);

#endif /* BUTTERWORTH_FILTER_H_ */
