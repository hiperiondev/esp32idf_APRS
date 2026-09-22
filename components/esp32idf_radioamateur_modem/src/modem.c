// @file modem.c
//
// @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
// @date 2026
// @copyright GNU General Public License v3
// @see https://github.com/hiperiondev/esp32idf_APRS
//
// @note
// This is based on other projects:
//     VP-Digi: https://github.com/sq8vps/vp-digi
//     ESP32APRS: https://github.com/nakhonthai/ESP32APRS_Audio
//     LibAPRS: https://github.com/markqvist/LibAPRS
//
//     please contact their authors for more information.
//
// @brief AFSK/FSK modulator and demodulator core implementation: tone
// generation, filtering and correlation demodulation, DCD, bit recovery and the
// parallel demodulator instances shared by every supported modem profile.

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"

#include "afsk.h"
#include "ax25.h"
#include "esp32idf_radioamateur_modem_config.h"
#include "modem.h"

static const char *TAG = "modem";

// Configuration for PLL-based data carrier detection.
// 1. MAXPULSE - maximum value of the DCD pulse counter. Higher values give more
//    stability once a correct signal is detected, but delay the DCD release.
// 2. THRES - threshold of the DCD pulse counter. When reached, the input signal
//    is assumed valid. Higher values mean more noise immunity but a slower DCD set.
// 3. The MAXPULSE/THRES difference sets the DCD "inertia".
// 4. INC is added when a symbol change happens near PLL counter zero.
// 5. DEC is subtracted when a symbol change happens too far from zero.
// 6. TUNE is the PLL counter tuning coefficient.
//
// [       DCD OFF    *      |    DCD ON   ]
// 0               COUNTER THRES        MAXPULSE
//        <-DEC INC->
#define DCD1200_MAXPULSE 60
#define DCD1200_THRES    20
#define DCD1200_INC      2
#define DCD1200_DEC      1
#define DCD1200_TUNE     0.74f

#define DCD9600_MAXPULSE 60
#define DCD9600_THRES    40
#define DCD9600_INC      1
#define DCD9600_DEC      1
#define DCD9600_TUNE     0.74f

#define DCD300_MAXPULSE 80
#define DCD300_THRES    20
#define DCD300_INC      4
#define DCD300_DEC      1
#define DCD300_TUNE     0.74f

#define N1200 8  // samples per symbol @ fs=9600
#define N9600 8  // samples per symbol @ fs=76800
#define N300  32 // samples per symbol @ fs=9600
#define NMAX  32 // keep equal to the biggest Nx

// Every Nx is a samples-per-symbol count, so the PLL step must be 2^32 / Nx.
//
// Eight samples per symbol is the resolution this timing-recovery design needs.
// The DPLL's sample instant is quantised to one ADC sample and the majority
// vote in decode() spans three of them, so at four samples per symbol the vote
// window would cover 75 % of a symbol and necessarily reach into a transition.
// Measured on the host against the real modulator, with the analogue path and
// both real clocks modelled and no noise at all, four samples per symbol
// produce hard bit errors at the sampling phases where the ADC instants
// coincide with the DAC's update instants (2/8, 4/8 and 6/8 of a symbol), 1.4 %
// to 2.7 % BER at those phases and 0 % between them; since the two clocks
// differ by ~0.05 %, the alignment walks through the bad phases every ~55 ms
// and a 350 ms transmission crosses them repeatedly.
//
// At eight samples per symbol - what N9600 gives once the ADC runs at 76800 Hz,
// and the same figure the 1200 Bd profile has - the same simulation gives ZERO
// bit errors at every sampling phase and with up to 30 us of TX edge jitter.
//
// The vote window and the DPLL tune constants were both swept on the host:
// (win=3, shift=0) is the best of its family, and the tune surface is chaotic
// at four samples per symbol (some settings collapse to 48 % BER), which is
// itself a sign the loop is bistable there. Resolution, not tuning, is what
// this profile needs.
#define PLL1200_STEP ((int32_t)(uint32_t)(((uint64_t)1 << 32) / N1200))
#define PLL9600_STEP ((int32_t)(uint32_t)(((uint64_t)1 << 32) / N9600))
#define PLL300_STEP  ((int32_t)(uint32_t)(((uint64_t)1 << 32) / N300))

// ADC clock calibration.
//
// PLLxxxx_STEP above assumes samples-per-symbol is exactly Nxxx, which holds
// only if the ADC really runs at MODEM_ADC_SAMPLERATE. It does not: the ADC
// clock is a hardware divider with its own rounding error (see the
// MODEM_ADC_SAMPLERATE comment in esp32idf_radioamateur_modem_config.h), and
// left uncorrected the gap is a steady phase error every DPLL in this file has
// to track for every frame instead of being told about it once.
//
// adcRateRatio is real / nominal ADC rate, which is also (real samples per
// symbol) / (nominal samples per symbol) for a transmitter at its nominal baud
// rate - every station on the air. dacRateRatio is real / nominal DAC alarm
// rate. It matters only to a receiver that hears this node's own G3RUH
// transmitter (full duplex, the wire loopback self-test), because that
// transmitter holds every symbol for a whole number of DAC samples and so
// runs at the DAC error; the AFSK transmitter derives its symbol and tone
// timing from the real alarm rate and is exact. Both ratios are fixed board
// properties (both clocks come from the same crystal) and stay 1.0 until
// ModemCalibrateSampleRate() has run.
static float adcRateRatio = 1.0f;
static float dacRateRatio = 1.0f;

// Real samples-per-symbol over nominal samples-per-symbol for the profile
// ModemInit() is building, derived from the two ratios above.
static float rxRateCorrection = 1.0f;

// @brief Apply the calibrated clock ratio to a nominal PLL step.
//
// nominalStep = 2^32 / N assumes exactly N samples per symbol. The real
// count is N * rxRateCorrection, so the real step is the nominal one divided
// by the same factor.
static int32_t calibratedPllStep(int32_t nominalStep) {
    double step = (double)(uint32_t)nominalStep / (double)rxRateCorrection;

    // Guard the extremes: a step of 0 would never overflow the PLL counter, so
    // decode() would never sample a symbol, and a step above 2^32-1 cannot be
    // represented at all. Both would require the correction to be wildly out
    // of range, which ModemCalibrateSampleRate() rejects, but this keeps the
    // arithmetic itself well-defined regardless.
    if (step < 1.0)
        step = 1.0;
    if (step > 4294967295.0)
        step = 4294967295.0;

    return (int32_t)(uint32_t)(step + 0.5);
}

// @brief Accept a measured/nominal clock ratio only if it is plausible.
//
// Both clocks are quartz derived, so a real error is a few tenths of a
// percent at most; anything past 1 % is a bad measurement (ADC not yet
// running, wrong pin, too short a window - see the quantization note in
// modem_init()). Applying a bogus ratio does more harm than none: a
// miscalibration of well under 1 % is enough to multiply the G3RUH frame loss.
static float plausibleRatio(const char *what, float measured, float nominal) {
    if (!(measured > 0.0f) || !(nominal > 0.0f)) {
        ESP_LOGW(TAG, "ModemCalibrateSampleRate: no usable %s measurement (%.1f Hz), keeping the nominal rate", what, (double)measured);
        return 1.0f;
    }

    float ratio = measured / nominal;
    if (ratio < 0.99f || ratio > 1.01f) {
        ESP_LOGW(TAG, "ModemCalibrateSampleRate: %s ratio %.4f (%.1f Hz for %.1f Hz nominal) out of sane range, ignoring", what, (double)ratio,
                 (double)measured, (double)nominal);
        return 1.0f;
    }
    return ratio;
}

void ModemCalibrateSampleRate(float measuredAdcHz, float measuredDacHz) {
    adcRateRatio = plausibleRatio("ADC", measuredAdcHz, (float)MODEM_ADC_SAMPLERATE);
    dacRateRatio = plausibleRatio("DAC", measuredDacHz, (float)afskGetDacSampleRate());

    ESP_LOGI(TAG, "ModemCalibrateSampleRate: ADC %.1f Hz (%+.3f%%), DAC %.1f Hz (%+.3f%%)", (double)measuredAdcHz, (double)((adcRateRatio - 1.0f) * 100.0f),
             (double)measuredDacHz, (double)((dacRateRatio - 1.0f) * 100.0f));

    // Takes effect from the next ModemInit() (i.e. the next afskSetModem()),
    // which is why modem_init() calls this before the first one. Patching an
    // already-running profile in place would race afsk_rx_task the same way
    // ModemInit() itself has to guard against - see the note above
    // afskSetModem() in afsk.c.
}

#define PLL1200_LOCKED_TUNE     0.74f
#define PLL1200_NOT_LOCKED_TUNE 0.50f
// The tune is the fraction of the phase error left in place at each transition,
// so a HIGHER number is a WEAKER pull and a narrower loop. The only thing this
// loop has to track is the DAC/ADC clock offset - about 17 Hz, 0.045 % - and it
// gets ~4800 transitions a second from a scrambled signal to do it in, so a
// narrow loop tracks it with a steady-state error around 1 % of a symbol. What
// a wide loop buys instead is a faster grab onto edge noise, and there is
// little phase margin to give away at 9600 Bd.
//
// Host simulation, sweeping the DAC/ADC phase over 16 offsets x 2 DMA
// alignments x 5 frames: a wide 0.89 loop loses 2.5 % of frames on a clean
// channel and 12.5 % with 12 us of TX edge jitter, against 0.0 % and 8.8 % for
// the 0.97 used here. 0.97 is also better at every preamble length down to
// 20 ms, so the slower acquisition a narrow loop would normally cost does not
// materialise: 20 ms is still 192 symbols at 9600 Bd.
//
// This is a real but secondary effect. The jitter itself is the dominant term
// and is handled in afsk.c; see the dac_write_isr() notes.
#define PLL9600_LOCKED_TUNE     0.97f
#define PLL9600_NOT_LOCKED_TUNE 0.50f
#define PLL300_LOCKED_TUNE      0.74f
#define PLL300_NOT_LOCKED_TUNE  0.50f

#define AMP_TRACKING_ATTACK 0.16f
#define AMP_TRACKING_DECAY  0.00004f

#define PLL_TUNE_BITS 8 // fixed point bits when tuning the PLL

// ------------------------------------------------------------------
// Quarter-wave sine table, 512 points, 8-bit unsigned, midpoint 128.
// Only the first quarter is stored; sinSample() mirrors/inverts it.
// ------------------------------------------------------------------
#define SIN_LEN 512

// DRAM_ATTR: read by sinSample() from the DAC sample ISR. In flash it is an
// XIP fetch inside the ISR, which is jitter on the transmitted edge.
static const DRAM_ATTR uint8_t sin_table[128] = {
    128, 129, 131, 132, 134, 135, 137, 138, 140, 142, 143, 145, 146, 148, 149, 151, 152, 154, 155, 157, 158, 160, 162, 163, 165, 166,
    167, 169, 170, 172, 173, 175, 176, 178, 179, 181, 182, 183, 185, 186, 188, 189, 190, 192, 193, 194, 196, 197, 198, 200, 201, 202,
    203, 205, 206, 207, 208, 210, 211, 212, 213, 214, 215, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231,
    232, 233, 234, 234, 235, 236, 237, 238, 238, 239, 240, 241, 241, 242, 243, 243, 244, 245, 245, 246, 246, 247, 248, 248, 249, 249,
    250, 250, 250, 251, 251, 252, 252, 252, 253, 253, 253, 253, 254, 254, 254, 254, 254, 255, 255, 255, 255, 255, 255, 255,
};

// IRAM_ATTR: called directly from MODEM_BAUDRATE_TIMER_HANDLER(), which runs
// in the DAC GPTimer ISR. "static inline" alone is just a hint - under
// CONFIG_COMPILER_OPTIMIZATION_DEBUG (-Og) GCC often leaves it un-inlined,
// and an un-inlined static function defaults to flash. That is precisely the
// XIP-fetch-in-the-ISR jitter sin_table's DRAM_ATTR comment above warns
// about; the table being in DRAM buys nothing if the code reading it is
// itself a flash round trip.
static inline uint8_t IRAM_ATTR sinSample(uint16_t i) {
    // Full cycle is SIN_LEN samples. Reduce to one cycle first; only after
    // establishing which half of the cycle we are in (for the 255-v
    // inversion) do we fold down into the stored quarter-wave table.
    uint16_t cycle = i % SIN_LEN;
    uint16_t half = cycle % (SIN_LEN / 2);
    uint16_t newI = (half >= (SIN_LEN / 4)) ? (SIN_LEN / 2 - half - 1) : half;
    uint8_t sine = sin_table[newI];
    return (cycle >= (SIN_LEN / 2)) ? (uint8_t)(255 - sine) : sine;
}

// ------------------------------------------------------------------

struct ModemDemodConfig ModemConfig;

// Note on the ModemInit() vs MODEM_DECODE() race.
//
// ModemInit() memsets demodState[] and then reassigns each filter's
// coeffs/taps piecemeal, while MODEM_DECODE() may be reading that same state
// from the RX task on another core.
//
// What serialises this is afskSetModem(), which suspends the RX task
// across the ModemInit() call. Any other caller of ModemInit() must do the
// same. A spinlock would be the wrong tool anyway: portENTER_CRITICAL masks
// interrupts up to level 3 on this core, which starves the DAC sample clock
// and destroys every AX.25 frame in flight - see the ring buffer notes in
// afsk.c.

static uint8_t N;             // samples per symbol
static uint8_t demodCount;    // number of parallel demodulators
static uint8_t currentSymbol; // current symbol for NRZI encoding
static uint8_t scrambledSymbol;

float markFreq;
float spaceFreq;
float baudRate;

static uint32_t markStep;  // Q32 phase increment per DAC sample
static uint32_t spaceStep; // Q32 phase increment per DAC sample
static uint16_t baudRateStep;
static float txRateHz; // real DAC alarm rate every transmit step is built from

// Receive tuning stored by ModemSetRxTuning() and read by ModemInit().
static modem_rx_tuning_t rxTuning = MODEM_RX_TUNING_DEFAULT();
static int16_t coeffHiI[NMAX], coeffLoI[NMAX], coeffHiQ[NMAX], coeffLoQ[NMAX];
// Data Carrier Detect, as a bitmap: bit i is set while demodulator i has its
// PLL locked. Consumers that only need "is the channel busy" test it for
// non-zero, which stays correct whatever the demodulator count is; the loop
// test in main/aprs_service.c reports the bits so an operator can tell which
// of the parallel demodulators locked.
static uint8_t dcd = 0;

// G3RUH scrambler state. TX and RX must have one each.
//
// A single shared register would be enough for a half duplex node, which never
// scrambles and descrambles at the same moment. It is fatal in full duplex -
// the only mode the loopback self test can run in - because the DAC ISR
// advances the register once per transmitted symbol while the RX task advances
// it once per received symbol, from the same variable, and neither side then
// holds the sequence it needs. They are logically two independent shift
// registers, so they are two variables.
static uint32_t txLfsr = 0x1FFFF;
static uint32_t rxLfsr = 0x1FFFF;

// Fixed 8-tap prefilters of the MODEM_RX_EQ_LEGACY demodulator set, fs=9600,
// gain 32768. Both carry an asymmetric last tap and are not linear phase.
//
// bpf1200Tilted: -6.5 dB at 1200 Hz, +3.0 dB at 2200 Hz - the space tone gains
// 9.5 dB on the mark tone. Used on de-emphasized (speaker) audio.
static const int16_t bpf1200Tilted[8] = { 728, -13418, -554, 19493, -554, -13418, 728, 2104 };

// bpf1200Level: +4.2 dB at 1200 Hz, +4.4 dB at 2200 Hz, peak +6.1 dB at
// 1700 Hz, -17 dB at 300 Hz and -22 dB at 3500 Hz - a band-pass with equal
// gain at both tones. Used on flat (discriminator) audio.
static const int16_t bpf1200Level[8] = { -10513, -10854, 9589, 23884, 9589, -10854, -10513, -879 };

// fs=9600, rectangular, fc1=1500, fc2=1900, 0 dB @ 1600/1800 Hz, N=15, gain 65536
static const int16_t bpf300[15] = {
    186, 8887, 8184, -1662, -10171, -8509, 386, 5394, 386, -8509, -10171, -1662, 8184, 8887, 186,
};

// Longest prefilter a demodulator can hold: the runtime-designed ones are
// bounded by MODEM_RX_BPF_TAPS_MAX, bpf300 is 15 taps.
#define BPF_MAX_TAPS MODEM_RX_BPF_TAPS_MAX
_Static_assert(BPF_MAX_TAPS >= 15, "BPF_MAX_TAPS must hold bpf300");

// fs=9600 Hz, raised cosine, fc=300 Hz (BR=600 Bd), beta=0.8, N=14, gain=65536
static const int16_t lpf300[14] = {
    4385, 4515, 4627, 4720, 4793, 4846, 4878, 4878, 4846, 4793, 4720, 4627, 4515, 4385,
};

static const int16_t lpf1200[15] = {
    -6128, -5974, -2503, 4125, 12679, 21152, 27364, 29643, 27364, 21152, 12679, 4125, -2503, -5974, -6128,
};

// fs=76800 Hz, Gaussian, fc ~5000 Hz (-3 dB at 0.53 x baud), N=15, gain=65536.
//
// The width of this filter is chosen for the sample rate it actually runs at.
// With 8 samples per symbol a Gaussian whose impulse response is long relative
// to one symbol costs real intersymbol interference, so sigma cannot simply be
// scaled up with the rate. Host sweep of sigma against worst-case BER over 8
// sampling phases:
//
//      sigma 0.8..2.2  ->  0 errors / 7374 bits
//      sigma 2.4       ->  6
//      sigma 2.6       -> 12
//      sigma 2.8..3.0  -> 18
//
// sigma 2.0 is taken: mid-range, so there is margin on the ISI side, while
// -3 dB at ~5 kHz is a defensible noise bandwidth for 9600 Bd on air rather than
// merely whatever passes a noiseless loopback.
static const int16_t lpf9600[15] = {
    29, 145, 574, 1769, 4245, 7930, 11538, 13076, 11538, 7930, 4245, 1769, 574, 145, 29,
};

#define LPF_MAX_TAPS    15
#define FILTER_MAX_TAPS ((LPF_MAX_TAPS > BPF_MAX_TAPS) ? LPF_MAX_TAPS : BPF_MAX_TAPS)

struct Filter {
    const int16_t *coeffs;
    uint8_t taps;
    int32_t samples[FILTER_MAX_TAPS];
    uint8_t idx; // index of the newest sample in the circular samples[] buffer
    uint8_t gainShift;
};

// Front half of a demodulator: prefilter, mark and space quadrature
// correlators, and a post-detection low-pass on the magnitude of each tone.
//
// Everything here is linear up to the magnitudes, and the post-detection
// low-pass is linear too, so low-pass filtering the two magnitudes separately
// and weighting them afterwards is the same as low-pass filtering the weighted
// difference. That is what lets several slicers (struct DemodState) share one
// correlator: each of them only adds a multiply and a subtract per sample.
//
// G3RUH has no tones. Its correlator is a pass-through: the input sample is
// the "mark" magnitude, "space" stays 0, and markLpf is the 9600 Bd receive
// filter, so its single slicer sees exactly the baseband signal.
struct Correlator {
    enum ModemPrefilter prefilter;
    struct Filter bpf;
    int16_t bpfTable[BPF_MAX_TAPS]; // storage for a prefilter designed by ModemInit()
    float bpfTiltDb;                // prefilter gain at the space tone minus gain at the mark tone

    int16_t samples[NMAX]; // prefiltered input, one symbol long, circular
    uint8_t samplesIdx;

    struct Filter markLpf;
    struct Filter spaceLpf;

    // Tone magnitudes of the current sample, before and after the
    // post-detection low-pass.
    int32_t mark;
    int32_t space;
    int32_t markFiltered;
    int32_t spaceFiltered;

    // Magnitude of the mark tone while it is the stronger one, and of the
    // space tone while that one is: first-order averages with a time constant
    // of 64 samples (eight symbols at 1200 Bd), read by ModemGetTwistDb().
    int32_t markLevel;
    int32_t spaceLevel;
};

// Back half of a demodulator: one slicer on one correlator's output, with its
// own carrier detect, clock recovery and, in ax25.c, its own HDLC decoder.
//
// The slicer compares the mark magnitude against the space magnitude scaled
// by spaceWeight. A weight below unity favours the mark tone and so suits a
// signal whose space tone arrives louder than its mark tone, and the other way
// round; a set of slicers with weights spread over the expected range of tone
// twist covers that range from a single correlator.
struct DemodState {
    uint8_t corr;        // index into correlators[]
    int32_t spaceWeight; // weight of the space magnitude, Q(SLICER_WEIGHT_BITS)
    float spaceWeightDb; // the same weight in dB, for the diagnostics
    uint8_t rawSymbols;  // raw, unsynchronized symbols
    uint8_t syncSymbols; // synchronized symbols

    uint8_t dcd;

    int32_t pll;
    int32_t pllStep;
    int32_t pllLockedTune;
    int32_t pllNotLockedTune;

    int32_t dcdPll;
    uint8_t dcdLastSymbol;
    uint16_t dcdCounter;
    uint16_t dcdMax;
    uint16_t dcdThres;
    uint16_t dcdInc;
    uint16_t dcdDec;
    int32_t dcdTune;
};

// Fixed-point position of DemodState::spaceWeight. With the weights bounded to
// +-MODEM_RX_TILT_DB_MAX and the magnitudes and their low-passed values well
// inside 2^16, the product stays far inside int32.
#define SLICER_WEIGHT_BITS 12

static struct Correlator correlators[MODEM_MAX_CORRELATOR_COUNT];
static struct DemodState demodState[MODEM_MAX_DEMODULATOR_COUNT];
static uint8_t corrCount; // correlators in use, 1..MODEM_MAX_CORRELATOR_COUNT

// Amplitude of the demodulator input, common to every demodulator because
// they all receive the same samples.
static int16_t inputPeak;
static int16_t inputValley;

static void decode(uint8_t symbol, uint8_t demod, uint16_t mV);
static void correlate(int16_t sample, struct Correlator *c);
static int32_t slice(struct DemodState *dem, const struct Correlator *c);

static int32_t filterRun(struct Filter *f, int32_t input) {
    // An unconfigured filter (taps == 0) has no coefficients to run against.
    // Without this guard, f->taps - 1 wraps to 255 and the code below
    // indexes far past the end of samples[].
    if (f->taps == 0)
        return 0;

    // Circular buffer: the newest sample overwrites the oldest one in place
    // instead of shifting every other sample down by one on every call.
    if (f->idx == 0)
        f->idx = f->taps;
    f->idx--;
    f->samples[f->idx] = input;

    int32_t out = 0;
    uint8_t idx = f->idx;
    for (uint8_t i = 0; i < f->taps; i++) {
        out += (int32_t)f->coeffs[i] * f->samples[idx];
        if (++idx == f->taps)
            idx = 0;
    }

    return out >> f->gainShift;
}

float ModemGetBaudrate(void) {
    return baudRate;
}

uint8_t ModemGetDemodulatorCount(void) {
    return demodCount;
}

uint8_t ModemDcdState(void) {
    return dcd;
}

void ModemGetSignalLevel(uint8_t modem, int8_t *peak, int8_t *valley, uint8_t *level) {
    // Public component API: the index is bounded against the demodState array
    // here rather than trusted, so a caller outside the component cannot read
    // past it. An out-of-range demodulator reports a flat, silent channel.
    if (modem >= MODEM_MAX_DEMODULATOR_COUNT) {
        *peak = 0;
        *valley = 0;
        *level = 0;
        return;
    }

    // Every demodulator sees the same input, so the figures are shared.
    *peak = (int8_t)((100 * (int32_t)inputPeak) >> 12);
    *valley = (int8_t)((100 * (int32_t)inputValley) >> 12);
    *level = (uint8_t)((100 * (int32_t)(inputPeak - inputValley)) >> 13);
}

enum ModemPrefilter ModemGetFilterType(uint8_t modem) {
    // Same bound as ModemGetSignalLevel(): an out-of-range demodulator reports
    // PREFILTER_NONE, the value that makes the pre-filter path a no-op.
    if (modem >= MODEM_MAX_DEMODULATOR_COUNT)
        return PREFILTER_NONE;

    return correlators[demodState[modem].corr].prefilter;
}

float ModemGetFilterTiltDb(uint8_t modem) {
    if (modem >= MODEM_MAX_DEMODULATOR_COUNT)
        return 0.0f;

    const struct Correlator *c = &correlators[demodState[modem].corr];
    return (c->prefilter == PREFILTER_NONE) ? 0.0f : c->bpfTiltDb;
}

float ModemGetSlicerWeightDb(uint8_t modem) {
    if (modem >= MODEM_MAX_DEMODULATOR_COUNT || ModemConfig.modem == MODEM_MODEM_G3RUH)
        return 0.0f;

    return demodState[modem].spaceWeightDb;
}

uint8_t ModemGetCorrelatorIndex(uint8_t modem) {
    if (modem >= MODEM_MAX_DEMODULATOR_COUNT)
        return 0;

    return demodState[modem].corr;
}

int8_t ModemGetTwistDb(uint8_t modem) {
    if (modem >= MODEM_MAX_DEMODULATOR_COUNT || ModemConfig.modem == MODEM_MODEM_G3RUH)
        return 0;

    const struct Correlator *c = &correlators[demodState[modem].corr];
    int32_t mark = c->markLevel;
    int32_t space = c->spaceLevel;
    if (mark <= 0 || space <= 0)
        return 0;

    float db = 20.0f * log10f((float)space / (float)mark) - ModemGetFilterTiltDb(modem);
    if (db > 30.0f)
        db = 30.0f;
    else if (db < -30.0f)
        db = -30.0f;
    return (int8_t)lrintf(db);
}

void ModemSetRxTuning(const modem_rx_tuning_t *t) {
    if (t == NULL)
        return;

    rxTuning = *t;
    modem_rx_tuning_sanitize(&rxTuning);
}

static void setDcd(bool state) {
    if (state)
        LED_Status2(0, 255, 0);
    else
        LED_Status2(0, 0, 0);
}

static inline uint8_t descramble(uint8_t in) {
    // G3RUH descrambling (x^17+x^12+1). Self synchronising: the register only
    // has to see 17 symbols of any signal to be in step, so no init handshake
    // is needed and the initial value below does not matter.
    uint8_t bit = (uint8_t)(((rxLfsr & 0x10000) > 0) ^ ((rxLfsr & 0x800) > 0) ^ (in > 0));

    rxLfsr <<= 1;
    rxLfsr |= in;
    return bit;
}

// IRAM_ATTR: called from MODEM_BAUDRATE_TIMER_HANDLER() (DAC GPTimer ISR)
// whenever ModemConfig.modem == MODEM_MODEM_G3RUH. Same -Og inlining risk as
// dac_scale()/sinSample() above - "static inline" is not a guarantee, and an
// un-inlined static function defaults to flash. This one is gated on exactly
// the G3RUH path, which is why it can look like a 9600-only problem while
// 1200 Bd profiles never call it and never show the jitter. descramble()
// does not need this: it only runs from decode(), which executes in
// afsk_rx_task task context, not at interrupt level.
static inline uint8_t IRAM_ATTR scramble(uint8_t in) {
    // G3RUH scrambling (x^17+x^12+1)
    uint8_t bit = (uint8_t)(((txLfsr & 0x10000) > 0) ^ ((txLfsr & 0x800) > 0) ^ (in > 0));

    txLfsr <<= 1;
    txLfsr |= bit;
    return bit;
}

void MODEM_DECODE(int16_t sample, uint16_t mVrms) {
    uint8_t dcdBits = 0;

    // input signal amplitude tracking
    if (sample >= inputPeak)
        inputPeak += (int16_t)(((int32_t)(AMP_TRACKING_ATTACK * 32768.f) * (int32_t)(sample - inputPeak)) >> 15);
    else
        inputPeak += (int16_t)(((int32_t)(AMP_TRACKING_DECAY * 32768.f) * (int32_t)(sample - inputPeak)) >> 15);

    if (sample <= inputValley)
        inputValley -= (int16_t)(((int32_t)(AMP_TRACKING_ATTACK * 32768.f) * (int32_t)(inputValley - sample)) >> 15);
    else
        inputValley -= (int16_t)(((int32_t)(AMP_TRACKING_DECAY * 32768.f) * (int32_t)(inputValley - sample)) >> 15);

    for (uint8_t c = 0; c < corrCount; c++)
        correlate(sample, &correlators[c]);

    for (uint8_t i = 0; i < demodCount; i++) {
        uint8_t symbol = (slice(&demodState[i], &correlators[demodState[i].corr]) > 0);

        decode(symbol, i, mVrms);
        if (demodState[i].dcd)
            dcdBits |= (uint8_t)(1u << i);
    }

    // The status LED and the busy-channel test both mean "any demodulator
    // locked", so they key off the bitmap being non-zero.
    setDcd(dcdBits != 0);
    dcd = dcdBits;
}

// Modulator state. Runs in the GPTimer ISR at the configured DAC sample rate.
//
// Symbol timing differs between the two kinds of profile.
//
// The AFSK profiles advance a Q32 symbol-phase accumulator by
// baud * 2^32 / (real DAC alarm rate) per DAC sample and start a new symbol
// each time it wraps, so the symbol rate is exact whatever the alarm period
// rounds to. A symbol edge lands on the nearest DAC sample - at 1200 Bd that
// is at most 3 % of a symbol, and the tone keeps a continuous phase across
// it, so the receiver never sees it.
//
// G3RUH holds every symbol for exactly baudRateStep DAC samples instead. At
// four samples per symbol a fractional accumulator would move one edge in
// every few hundred by a quarter of a symbol, which is the kind of edge
// jitter this profile cannot absorb. Whole-sample symbols keep every edge on
// the same grid and turn the alarm rounding into a small, uniform rate offset
// the receiving DPLL tracks.
static uint32_t phaseAcc = 0;      // Q32: the full sine cycle is 2^32
static uint32_t baudPhase = 0;     // Q32 symbol phase, AFSK profiles
static uint32_t baudPhaseStep = 0; // Q32 symbol phase increment per DAC sample
static uint16_t sampleIndex = 0;   // DAC samples left in the current symbol, G3RUH
static bool symbolDue = true;      // the next DAC sample starts a new symbol

uint8_t IRAM_ATTR MODEM_BAUDRATE_TIMER_HANDLER(void) {
    uint8_t sinwave = 0;

    if (symbolDue) {
        symbolDue = false;
        if (Ax25GetTxBit() == 0) // next bit is 0 -> change symbol (NRZI)
            currentSymbol ^= 1;

        // Scramble exactly once per symbol, here, and hold the result for the
        // whole symbol below. The receiver's descrambler is clocked once per
        // symbol, so a scrambler clocked on every DAC sample would produce a
        // sequence it could never match. The other profiles are not
        // scrambled.
        if (ModemConfig.modem == MODEM_MODEM_G3RUH)
            scrambledSymbol = scramble(currentSymbol);
    }

    if (ModemConfig.modem == MODEM_MODEM_G3RUH) {
        sinwave = scrambledSymbol ? 240 : 20;

        if (sampleIndex > 1) {
            sampleIndex--;
        } else {
            sampleIndex = baudRateStep;
            symbolDue = true;
        }
    } else {
        // The phase accumulator is 32 bits wide and the table index is the top
        // 9 of them, with the fraction carried in the low 23 bits. Stepping the
        // table index by an integer instead would quantise the tone to the DAC
        // sample rate over SIN_LEN, i.e. 75 Hz at 38400 - more than 1 % off
        // frequency for every profile. Carrying the fraction, with the step
        // built from the real alarm rate, keeps every tone exact.
        //
        // The index is still 9 bit, so the table lookup truncates the phase -
        // that is amplitude distortion around -54 dBc, already well under the
        // 8-bit DAC's own noise floor.
        if (currentSymbol)
            phaseAcc += spaceStep;
        else
            phaseAcc += markStep;

        sinwave = sinSample((uint16_t)((phaseAcc >> 23) & (SIN_LEN - 1)));

        uint32_t previous = baudPhase;
        baudPhase += baudPhaseStep;
        if (baudPhase < previous)
            symbolDue = true;
    }

    return sinwave;
}

// @brief Run one input sample through a correlator.
//
// Updates the tone magnitudes of c, before and after the post-detection
// low-pass. The magnitude of each tone is the true (L2) magnitude of its I/Q
// correlator pair: |I| + |Q| would swing between 1 and 1.41 times it with the
// phase of the tone against the correlator, which is 3 dB of noise on the
// mark/space decision.
//
// @param sample Received sample, no more than 13 bits.
static void correlate(int16_t sample, struct Correlator *c) {
    if (ModemConfig.modem == MODEM_MODEM_G3RUH) {
        c->mark = sample;
        c->markFiltered = filterRun(&c->markLpf, sample);
        return;
    }

    if (c->prefilter != PREFILTER_NONE)
        c->samples[c->samplesIdx] = (int16_t)filterRun(&c->bpf, sample);
    else
        c->samples[c->samplesIdx] = sample;

    // N is a runtime value (N1200 / N300 / N9600), so "% N" cannot be
    // strength-reduced to a mask by the compiler and would otherwise become a
    // real integer division on every tap. Wrap on compare instead.
    if (++c->samplesIdx == N)
        c->samplesIdx = 0;

    int32_t outLoI = 0, outLoQ = 0, outHiI = 0, outHiQ = 0;
    uint8_t idx = c->samplesIdx;

    for (uint8_t i = 0; i < N; i++) {
        int16_t t = c->samples[idx];
        outLoI += t * coeffLoI[i];
        outLoQ += t * coeffLoQ[i];
        outHiI += t * coeffHiI[i];
        outHiQ += t * coeffHiQ[i];

        if (++idx == N)
            idx = 0;
    }

    // The coefficients are Q12 and a symbol holds up to 32 samples, so the
    // sums fit int32 and their squares fit a float; the 2^-14 scale keeps the
    // magnitudes in the same range as the input samples.
    const float scale = 1.0f / 16384.0f;
    int32_t lo = (int32_t)(sqrtf((float)outLoI * (float)outLoI + (float)outLoQ * (float)outLoQ) * scale);
    int32_t hi = (int32_t)(sqrtf((float)outHiI * (float)outHiI + (float)outHiQ * (float)outHiQ) * scale);

    // Tone levels for the twist estimate: each tone is averaged only while
    // it is the stronger one.
    if (lo > hi)
        c->markLevel += (lo - c->markLevel) >> 6;
    else
        c->spaceLevel += (hi - c->spaceLevel) >> 6;

    c->mark = lo;
    c->space = hi;
    c->markFiltered = filterRun(&c->markLpf, lo);
    c->spaceFiltered = filterRun(&c->spaceLpf, hi);
}

// @brief Slice one correlator output and run the demodulator's carrier detect.
// @return Low-passed decision variable: > 0 for mark, <= 0 for space (for
//         G3RUH, the filtered baseband sample).
static int32_t slice(struct DemodState *dem, const struct Correlator *c) {
    int32_t sample = c->mark - ((c->space * dem->spaceWeight) >> SLICER_WEIGHT_BITS);

    // DCD using a "PLL". The PLL runs nominally at the baudrate; its counter
    // counts up and overflows to a minimal negative value, so it crosses zero
    // in the middle. A tone change should happen near this zero crossing.
    // Changes near zero raise the DCD pulse counter and pull the counter phase
    // towards zero; changes far from zero lower it. Above dcdThres we claim the
    // incoming signal is valid. dcdMax keeps the DCD from getting "sticky".
    dem->dcdPll = (int32_t)((uint32_t)(dem->dcdPll) + (uint32_t)(dem->pllStep));

    if ((sample > 0) != dem->dcdLastSymbol) { // tone changed
        if ((uint32_t)abs(dem->dcdPll) < (uint32_t)(dem->pllStep)) {
            dem->dcdCounter += dem->dcdInc;
            if (dem->dcdCounter > dem->dcdMax)
                dem->dcdCounter = dem->dcdMax;
        } else {
            if (dem->dcdCounter >= dem->dcdDec)
                dem->dcdCounter -= dem->dcdDec;
            else
                dem->dcdCounter = 0;
        }

        dem->dcdPll = (int32_t)(((int64_t)dem->dcdPll * (int64_t)dem->dcdTune) >> PLL_TUNE_BITS);
    }

    dem->dcdLastSymbol = (sample > 0);

    dem->dcd = (dem->dcdCounter > dem->dcdThres) ? 1 : 0;

    return c->markFiltered - ((c->spaceFiltered * dem->spaceWeight) >> SLICER_WEIGHT_BITS);
}

// @brief Bit/clock recovery, NRZI decoding, and hand-off to the protocol layer.
static void decode(uint8_t symbol, uint8_t demod, uint16_t mV) {
    struct DemodState *dem = &demodState[demod];

    int32_t previous = dem->pll;

    dem->pll = (int32_t)((uint32_t)(dem->pll) + (uint32_t)(dem->pllStep));

    dem->rawSymbols <<= 1;
    dem->rawSymbols |= (symbol & 1);

    if ((dem->pll < 0) && (previous > 0)) { // PLL overflow: sample the symbol
        dem->syncSymbols <<= 1;

        // take the last three symbols; one is not enough, three work well
        uint8_t sym = dem->rawSymbols & 0x07;
        if (sym == 0x07 || sym == 0x06 || sym == 0x05 || sym == 0x03)
            sym = 1;
        else
            sym = 0;

        if (ModemConfig.modem == MODEM_MODEM_G3RUH)
            sym = descramble(sym);

        dem->syncSymbols |= sym;

        // NRZI decoding: no transition -> 1, transition -> 0
        if (((dem->syncSymbols & 0x03) == 0x03) || ((dem->syncSymbols & 0x03) == 0x00))
            Ax25BitParse(1, demod, mV);
        else
            Ax25BitParse(0, demod, mV);
    }

    if (((dem->rawSymbols & 0x03) == 0x02) || ((dem->rawSymbols & 0x03) == 0x01)) {
        if (!dem->dcd) // PLL not locked - adjust faster
            dem->pll = (int32_t)(((int64_t)dem->pll * (int64_t)dem->pllNotLockedTune) >> PLL_TUNE_BITS);
        else // PLL locked - adjust slower
            dem->pll = (int32_t)(((int64_t)dem->pll * (int64_t)dem->pllLockedTune) >> PLL_TUNE_BITS);
    }
}

void ModemTransmitStart(void) {
    setPtt(true);
    setTransmit(true);
    ESP_LOGD(TAG, "ModemTransmitStart");
}

// @brief Stop TX and go back to RX.
//
// Called from the DAC timer ISR via Ax25GetTxBit(), so it must be ISR safe:
// it only lowers a flag. AFSK_ServiceTx(), running in the service task, does
// the actual teardown (stop the timer, park the DAC, release PTT).
void IRAM_ATTR ModemTransmitStop(void) {
    setTransmit(false);
}

bool ModemTxTeardownPending(void) {
    return AFSK_TxTeardownPending();
}

void ModemGetStepTones(float *mark, float *space) {
    // Derived from the steps themselves, so this reports what the modulator is
    // really doing rather than what it was asked to do.
    if (mark)
        *mark = (float)(((double)markStep * (double)txRateHz) / 4294967296.0);
    if (space)
        *space = (float)(((double)spaceStep * (double)txRateHz) / 4294967296.0);
}

// Prefilter tilt tables of the designed presets, dB (space gain minus mark
// gain), for flat (discriminator) and de-emphasized (speaker) input. See
// modem_rx_eq_preset_t in esp32idf_radioamateur_modem.h.
static const int8_t tiltSingleFlat[1] = { 0 };
static const int8_t tiltSingleSpeaker[1] = { 3 };
static const int8_t tiltDiv2Flat[2] = { 0, -5 };
static const int8_t tiltDiv2Speaker[2] = { 0, 5 };
static const int8_t tiltDiv3Flat[3] = { 4, 0, -5 };
static const int8_t tiltDiv3Speaker[3] = { 0, 3, 6 };

// MODEM_RX_EQ_MULTISLICE: MODEM_RX_SLICE_PREFILTERS prefilters, each feeding
// MODEM_RX_SLICE_WEIGHTS slicers. The prefilter tables hold tilts and the
// weight tables hold the weight every slicer gives the space magnitude, both
// in dB and with the same sign convention (negative favours the mark tone).
//
// Twist compensation is split between the two on purpose. A slicer is nearly
// free and reaches any weight exactly, where a 21-tap prefilter realizes only
// part of a large tilt, so the slicers carry most of the range. What they
// cannot do is keep a loud space tone out of the mark correlator: the
// correlator is one symbol long, so its response is broad enough for the space
// tone to leak into the mark arm, and with the space tone 10 dB up that
// leakage buries a weak mark tone no matter how the two magnitudes are
// weighted afterwards. Only a filter ahead of the correlators removes it,
// which is what the tilted second prefilter is for.
//
// Flat (discriminator) audio carries no twist from a flat transmitter and up
// to about +12 dB from a pre-emphasizing one, so the set runs from +3 to
// -12 dB. De-emphasized (speaker) audio shifts the whole range up by the
// receiver's de-emphasis, about 6 dB.
#define MODEM_RX_SLICE_PREFILTERS 2
#define MODEM_RX_SLICE_WEIGHTS    (MODEM_RX_SLICER_COUNT / MODEM_RX_SLICE_PREFILTERS)
_Static_assert(MODEM_RX_SLICER_COUNT == MODEM_RX_SLICE_PREFILTERS * MODEM_RX_SLICE_WEIGHTS,
               "MODEM_RX_SLICER_COUNT must be a whole number of slicers per prefilter");
_Static_assert(MODEM_RX_SLICE_PREFILTERS <= MODEM_MAX_CORRELATOR_COUNT, "the multi-slicer preset needs one correlator per prefilter");

static const int8_t sliceTiltFlat[MODEM_RX_SLICE_PREFILTERS] = { 0, -5 };
static const int8_t sliceTiltSpeaker[MODEM_RX_SLICE_PREFILTERS] = { 5, 0 };
static const int8_t sliceWeightFlat[MODEM_RX_SLICE_PREFILTERS][MODEM_RX_SLICE_WEIGHTS] = {
    { 3, 0, -3 },
    { -3, -6, -9 },
};
static const int8_t sliceWeightSpeaker[MODEM_RX_SLICE_PREFILTERS][MODEM_RX_SLICE_WEIGHTS] = {
    { 6, 3, 0 },
    { 0, -3, -6 },
};

// Frequency grid of the prefilter designer, points from 0 Hz to fs/2.
#define BPF_DESIGN_GRID 128

// @brief Gain of an FIR, Q15 coefficients, at one frequency.
static float firGain(const int16_t *c, uint8_t taps, float f, float fs) {
    float re = 0.0f, im = 0.0f;
    for (uint8_t n = 0; n < taps; n++) {
        float w = 2.0f * (float)M_PI * f * (float)n / fs;
        re += (float)c[n] * cosf(w);
        im -= (float)c[n] * sinf(w);
    }
    return sqrtf(re * re + im * im) / 32768.0f;
}

// @brief Design a linear-phase band-pass prefilter with a tilt.
//
// Frequency-sampling design on a grid of BPF_DESIGN_GRID + 1 points with a
// Hamming window. The target gain is zero outside [lo, hi] and, inside it,
// varies linearly in dB: 0 dB at markHz, tiltDb at spaceHz. The result is
// scaled so the passband maximum is at most unity, which keeps the int16 cast
// after filterRun() and the post-detection filter's int32 accumulator within
// range for the +-2047 input demodulate() is fed. Runs only from ModemInit(),
// with the receive task held, so the float work here costs nothing in the
// signal path.
static void designPrefilter(int16_t *out, uint8_t taps, float fs, float lo, float hi, float markHz, float spaceHz, float tiltDb) {
    static float target[BPF_DESIGN_GRID + 1];
    float h[BPF_MAX_TAPS];
    const int m = (taps - 1) / 2;

    // Target gain on the grid, trapezoid-rule weights folded in.
    for (int k = 0; k <= BPF_DESIGN_GRID; k++) {
        float f = (fs * 0.5f) * (float)k / (float)BPF_DESIGN_GRID;
        target[k] = 0.0f;
        if (f >= lo && f <= hi) {
            target[k] = powf(10.0f, (tiltDb * (f - markHz) / (spaceHz - markHz)) / 20.0f);
            if (k == 0 || k == BPF_DESIGN_GRID)
                target[k] *= 0.5f;
        }
    }

    for (int n = 0; n < taps; n++) {
        float acc = 0.0f;
        for (int k = 0; k <= BPF_DESIGN_GRID; k++) {
            if (target[k] == 0.0f)
                continue;
            float f = (fs * 0.5f) * (float)k / (float)BPF_DESIGN_GRID;
            acc += target[k] * cosf(2.0f * (float)M_PI * f * (float)(n - m) / fs);
        }
        float window = 0.54f - 0.46f * cosf(2.0f * (float)M_PI * (float)n / (float)(taps - 1));
        h[n] = acc / (float)BPF_DESIGN_GRID * window;
    }

    float peak = 0.0f;
    for (int k = 0; k <= BPF_DESIGN_GRID; k++) {
        float f = (fs * 0.5f) * (float)k / (float)BPF_DESIGN_GRID;
        float re = 0.0f, im = 0.0f;
        for (int n = 0; n < taps; n++) {
            float w = 2.0f * (float)M_PI * f * (float)n / fs;
            re += h[n] * cosf(w);
            im -= h[n] * sinf(w);
        }
        float g = sqrtf(re * re + im * im);
        if (g > peak)
            peak = g;
    }
    if (!(peak > 0.0f))
        peak = 1.0f;

    for (int n = 0; n < taps; n++)
        out[n] = (int16_t)lrintf(h[n] / peak * 32767.0f);
}

// @brief Clock-recovery and DCD settings of a 1200 Bd demodulator, reading
//        correlator corr through a slicer of the given space weight.
static void setup1200Demod(struct DemodState *dem, uint8_t corr, float weightDb) {
    dem->corr = corr;
    dem->spaceWeightDb = weightDb;
    dem->spaceWeight = (int32_t)lrintf(powf(10.0f, weightDb / 20.0f) * (float)(1 << SLICER_WEIGHT_BITS));
    dem->pllStep = calibratedPllStep(PLL1200_STEP);
    dem->pllLockedTune = (int32_t)(PLL1200_LOCKED_TUNE * (float)((uint32_t)1 << PLL_TUNE_BITS));
    dem->pllNotLockedTune = (int32_t)(PLL1200_NOT_LOCKED_TUNE * (float)((uint32_t)1 << PLL_TUNE_BITS));
    dem->dcdMax = DCD1200_MAXPULSE;
    dem->dcdThres = DCD1200_THRES;
    dem->dcdInc = DCD1200_INC;
    dem->dcdDec = DCD1200_DEC;
    dem->dcdTune = (int32_t)(DCD1200_TUNE * (float)((uint32_t)1 << PLL_TUNE_BITS));
}

// @brief Give a correlator its post-detection low-pass filters, no prefilter.
static void setupCorrelator(struct Correlator *c, const int16_t *lpf, uint8_t taps, uint8_t gainShift) {
    c->prefilter = PREFILTER_NONE;
    c->markLpf.coeffs = lpf;
    c->markLpf.taps = taps;
    c->markLpf.gainShift = gainShift;
    c->spaceLpf.coeffs = lpf;
    c->spaceLpf.taps = taps;
    c->spaceLpf.gainShift = gainShift;
}

// @brief Attach a prefilter to a correlator and record its tilt.
static void setPrefilter(struct Correlator *c, const int16_t *coeffs, uint8_t taps, uint8_t gainShift) {
    c->prefilter = PREFILTER_BANDPASS;
    c->bpf.coeffs = coeffs;
    c->bpf.taps = taps;
    c->bpf.gainShift = gainShift;

    float gMark = firGain(coeffs, taps, markFreq, (float)MODEM_DEMOD_SAMPLERATE);
    float gSpace = firGain(coeffs, taps, spaceFreq, (float)MODEM_DEMOD_SAMPLERATE);
    c->bpfTiltDb = (gMark > 0.0f && gSpace > 0.0f) ? 20.0f * log10f(gSpace / gMark) : 0.0f;
}

// @brief Design a band-pass prefilter from rxTuning into a correlator.
static void designCorrelatorPrefilter(uint8_t index, int8_t tiltDb) {
    struct Correlator *c = &correlators[index];

    designPrefilter(c->bpfTable, rxTuning.bpf_taps, (float)MODEM_DEMOD_SAMPLERATE, (float)rxTuning.bpf_lo_hz, (float)rxTuning.bpf_hi_hz, markFreq, spaceFreq,
                    (float)tiltDb);
    setPrefilter(c, c->bpfTable, rxTuning.bpf_taps, 15);
    ESP_LOGI(TAG, "prefilter %u: band-pass %u-%u Hz, %u taps, tilt %+d dB requested, %+.1f dB realized", (unsigned)index, (unsigned)rxTuning.bpf_lo_hz,
             (unsigned)rxTuning.bpf_hi_hz, (unsigned)rxTuning.bpf_taps, (int)tiltDb, (double)c->bpfTiltDb);
}

// @brief Build the 1200 Bd demodulator set selected by rxTuning.
static void setup1200DemodSet(void) {
    const int8_t *tilts = NULL;

    switch (rxTuning.eq_preset) {
        case MODEM_RX_EQ_LEGACY:
            corrCount = 2;
            break;
        case MODEM_RX_EQ_SINGLE:
            corrCount = 1;
            tilts = ModemConfig.flatAudioIn ? tiltSingleFlat : tiltSingleSpeaker;
            break;
        case MODEM_RX_EQ_DIVERSITY2:
            corrCount = 2;
            tilts = ModemConfig.flatAudioIn ? tiltDiv2Flat : tiltDiv2Speaker;
            break;
        case MODEM_RX_EQ_DIVERSITY3:
            corrCount = 3;
            tilts = ModemConfig.flatAudioIn ? tiltDiv3Flat : tiltDiv3Speaker;
            break;
        case MODEM_RX_EQ_CUSTOM:
            corrCount = rxTuning.custom_count;
            tilts = rxTuning.custom_tilt_db;
            break;
        case MODEM_RX_EQ_MULTISLICE:
        default: {
            const int8_t *tilts = ModemConfig.flatAudioIn ? sliceTiltFlat : sliceTiltSpeaker;
            const int8_t (*weights)[MODEM_RX_SLICE_WEIGHTS] = ModemConfig.flatAudioIn ? sliceWeightFlat : sliceWeightSpeaker;

            corrCount = MODEM_RX_SLICE_PREFILTERS;
            demodCount = MODEM_RX_SLICER_COUNT;
            for (uint8_t c = 0; c < corrCount; c++) {
                setupCorrelator(&correlators[c], lpf1200, sizeof(lpf1200) / sizeof(*lpf1200), 15);
                designCorrelatorPrefilter(c, tilts[c]);
                for (uint8_t k = 0; k < MODEM_RX_SLICE_WEIGHTS; k++) {
                    uint8_t i = (uint8_t)(c * MODEM_RX_SLICE_WEIGHTS + k);
                    setup1200Demod(&demodState[i], c, (float)weights[c][k]);
                    ESP_LOGI(TAG, "demod %u: prefilter %u, space weight %+d dB", (unsigned)i, (unsigned)c, (int)weights[c][k]);
                }
            }
            return;
        }
    }

    // Every other set: one slicer, at unit weight, per correlator.
    demodCount = corrCount;
    for (uint8_t i = 0; i < corrCount; i++) {
        setupCorrelator(&correlators[i], lpf1200, sizeof(lpf1200) / sizeof(*lpf1200), 15);
        setup1200Demod(&demodState[i], i, 0.0f);
    }

    if (tilts == NULL) {
        // Legacy set: correlator 0 runs a fixed band-pass matched to the kind
        // of input, correlator 1 sees the input unfiltered.
        if (ModemConfig.flatAudioIn)
            setPrefilter(&correlators[0], bpf1200Level, sizeof(bpf1200Level) / sizeof(*bpf1200Level), 15);
        else
            setPrefilter(&correlators[0], bpf1200Tilted, sizeof(bpf1200Tilted) / sizeof(*bpf1200Tilted), 15);
        return;
    }

    for (uint8_t i = 0; i < corrCount; i++)
        designCorrelatorPrefilter(i, tilts[i]);
}

void ModemInit(void) {
    memset(correlators, 0, sizeof(correlators));
    memset(demodState, 0, sizeof(demodState));
    inputPeak = 0;
    inputValley = 0;

    if (ModemConfig.modem > MODEM_MODEM_G3RUH)
        ModemConfig.modem = MODEM_MODEM_BELL202;

    // Receive clock correction for this profile: the ADC error for every
    // station on the air, plus the DAC error when the receiver hears this
    // node's own G3RUH transmitter (see ModemCalibrateSampleRate()).
    rxRateCorrection = adcRateRatio;
    if ((ModemConfig.modem == MODEM_MODEM_G3RUH) && afskGetFullDuplex())
        rxRateCorrection = adcRateRatio / dacRateRatio;

    if ((ModemConfig.modem == MODEM_MODEM_BELL202) || (ModemConfig.modem == MODEM_MODEM_V23)) {
        N = N1200;
        baudRate = 1200.f;

        if (ModemConfig.modem == MODEM_MODEM_BELL202) { // Bell 202
            markFreq = 1200.f;
            spaceFreq = 2200.f;
        } else { // V.23
            markFreq = 1300.f;
            spaceFreq = 2100.f;
        }

        setup1200DemodSet();
    } else if (ModemConfig.modem == MODEM_MODEM_AFSK300) {
        corrCount = 1;
        demodCount = 1;
        N = N300;
        baudRate = 300.f;
        markFreq = 1600.f;
        spaceFreq = 1800.f;

        demodState[0].pllStep = calibratedPllStep(PLL300_STEP);
        demodState[0].pllLockedTune = (int32_t)(PLL300_LOCKED_TUNE * (float)((uint32_t)1 << PLL_TUNE_BITS));
        demodState[0].pllNotLockedTune = (int32_t)(PLL300_NOT_LOCKED_TUNE * (float)((uint32_t)1 << PLL_TUNE_BITS));
        demodState[0].dcdMax = DCD300_MAXPULSE;
        demodState[0].dcdThres = DCD300_THRES;
        demodState[0].dcdInc = DCD300_INC;
        demodState[0].dcdDec = DCD300_DEC;
        demodState[0].dcdTune = (int32_t)(DCD300_TUNE * (float)((uint32_t)1 << PLL_TUNE_BITS));
        demodState[0].spaceWeight = 1 << SLICER_WEIGHT_BITS;

        setupCorrelator(&correlators[0], lpf300, sizeof(lpf300) / sizeof(*lpf300), 15);
        setPrefilter(&correlators[0], bpf300, sizeof(bpf300) / sizeof(*bpf300), 16);
    } else if (ModemConfig.modem == MODEM_MODEM_G3RUH) {
        corrCount = 1;
        demodCount = 1;
        N = N9600;
        baudRate = 9600.f;

        // G3RUH is baseband NRZ, not AFSK: there are no mark and space tones,
        // so both are reported as 0. The tone-reporting helpers must not
        // claim a mark or space frequency for a profile that emits neither.
        markFreq = 0.f;
        spaceFreq = 0.f;

        demodState[0].pllStep = calibratedPllStep(PLL9600_STEP);
        demodState[0].pllLockedTune = (int32_t)(PLL9600_LOCKED_TUNE * (float)((uint32_t)1 << PLL_TUNE_BITS));
        demodState[0].pllNotLockedTune = (int32_t)(PLL9600_NOT_LOCKED_TUNE * (float)((uint32_t)1 << PLL_TUNE_BITS));
        demodState[0].dcdMax = DCD9600_MAXPULSE;
        demodState[0].dcdThres = DCD9600_THRES;
        demodState[0].dcdInc = DCD9600_INC;
        demodState[0].dcdDec = DCD9600_DEC;
        demodState[0].dcdTune = (int32_t)(DCD9600_TUNE * (float)((uint32_t)1 << PLL_TUNE_BITS));
        demodState[0].spaceWeight = 1 << SLICER_WEIGHT_BITS;

        // Receive only. Nothing on the TX path runs it -
        // MODEM_BAUDRATE_TIMER_HANDLER() emits a raw 240/20 square - and it
        // should stay that way: shaping the transmitter with this same filter
        // was measured on the host to roughly double the BER, because the TX+RX
        // cascade closes the eye. If transmit shaping is ever wanted it needs
        // its own, wider filter and its own state (the receiver is using this
        // instance concurrently in full duplex).
        correlators[0].prefilter = PREFILTER_NONE;
        correlators[0].markLpf.coeffs = lpf9600;
        correlators[0].markLpf.taps = sizeof(lpf9600) / sizeof(*lpf9600);
        correlators[0].markLpf.gainShift = 16;
    }

    // Transmit steps. The tones and the AFSK symbol phase are built from the
    // real DAC alarm rate, so what goes on the air is exact; before the DAC
    // timer exists the nominal rate stands in. G3RUH's whole-sample symbol
    // length comes from the nominal rate, which is an exact multiple of its
    // baud rate.
    uint32_t dacRate = afskGetDacSampleRate();
    txRateHz = afskGetDacAlarmRate();
    if (!(txRateHz > 0.0f))
        txRateHz = (float)dacRate;

    markStep = (uint32_t)(((double)markFreq * 4294967296.0) / (double)txRateHz + 0.5);
    spaceStep = (uint32_t)(((double)spaceFreq * 4294967296.0) / (double)txRateHz + 0.5);
    baudPhaseStep = (uint32_t)(((double)baudRate * 4294967296.0) / (double)txRateHz + 0.5);
    baudRateStep = (uint16_t)(dacRate / (uint32_t)baudRate);

    {
        float txMark = 0, txSpace = 0;
        ModemGetStepTones(&txMark, &txSpace);
        ESP_LOGI(TAG, "mark %.1f Hz -> emits %.2f, space %.1f Hz -> emits %.2f, DAC %.1f Hz, %u demodulator(s), RX clock correction %+.3f%%", markFreq, txMark,
                 spaceFreq, txSpace, (double)txRateHz, (unsigned)demodCount, (double)((rxRateCorrection - 1.0f) * 100.0f));
    }

    for (uint8_t i = 0; i < N; i++) { // correlator coefficients
        coeffLoI[i] = (int16_t)(4095.f * cosf(2.f * (float)M_PI * (float)i / (float)N * markFreq / baudRate));
        coeffLoQ[i] = (int16_t)(4095.f * sinf(2.f * (float)M_PI * (float)i / (float)N * markFreq / baudRate));
        coeffHiI[i] = (int16_t)(4095.f * cosf(2.f * (float)M_PI * (float)i / (float)N * spaceFreq / baudRate));
        coeffHiQ[i] = (int16_t)(4095.f * sinf(2.f * (float)M_PI * (float)i / (float)N * spaceFreq / baudRate));
    }

    // reset the modulator state
    phaseAcc = 0;
    baudPhase = 0;
    sampleIndex = baudRateStep;
    symbolDue = true;
    currentSymbol = 0;
    scrambledSymbol = 0;
    txLfsr = 0x1FFFF;
    rxLfsr = 0x1FFFF;
    dcd = 0;
}
