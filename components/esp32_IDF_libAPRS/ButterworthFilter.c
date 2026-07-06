#include <string.h>

#include "ButterworthFilter.h"

void ButterworthFilter_Init(ButterworthFilter *f) {
    memset(f, 0, sizeof(*f));
}

void ButterworthFilter_InitWithParams(ButterworthFilter *f, float frequency, int sampleRate, ButterworthPassType passType, float resonance) {
    memset(f, 0, sizeof(*f));
    ButterworthFilter_SetParameters(f, frequency, sampleRate, passType, resonance);
}

void ButterworthFilter_SetParameters(ButterworthFilter *f, float frequency, int sampleRate, ButterworthPassType passType, float resonance) {
    switch (passType) {
        case BW_LOWPASS:
            f->c = 1.0f / (float)tan(M_PI * frequency / sampleRate);
            f->a1 = 1.0f / (1.0f + resonance * f->c + f->c * f->c);
            f->a2 = 2.0f * f->a1;
            f->a3 = f->a1;
            f->b1 = 2.0f * (1.0f - f->c * f->c) * f->a1;
            f->b2 = (1.0f - resonance * f->c + f->c * f->c) * f->a1;
            break;
        case BW_HIGHPASS:
            f->c = (float)tan(M_PI * frequency / sampleRate);
            f->a1 = 1.0f / (1.0f + resonance * f->c + f->c * f->c);
            f->a2 = -2.0f * f->a1;
            f->a3 = f->a1;
            f->b1 = 2.0f * (f->c * f->c - 1.0f) * f->a1;
            f->b2 = (1.0f - resonance * f->c + f->c * f->c) * f->a1;
            break;
    }
}

float ButterworthFilter_Update(ButterworthFilter *f, float newInput) {
    float newOutput = f->a1 * newInput + f->a2 * f->inputHistory[0] + f->a3 * f->inputHistory[1] - f->b1 * f->outputHistory[0] - f->b2 * f->outputHistory[1];

    f->inputHistory[1] = f->inputHistory[0];
    f->inputHistory[0] = newInput;

    f->outputHistory[2] = f->outputHistory[1];
    f->outputHistory[1] = f->outputHistory[0];
    f->outputHistory[0] = newOutput;

    return newOutput;
}
