/**
 * @file modem.c
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
 * @brief AFSK/FSK modulator and demodulator core implementation: tone
 * generation, filtering and correlation demodulation, DCD, bit recovery and the
 * parallel demodulator instances shared by every supported modem profile.
 */

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"

#include "esp32idf_radioamateur_modem_config.h"
#include "afsk.h"
#include "ax25.h"
#include "modem.h"

static const char *TAG = "modem";

/*
 * Configuration for PLL-based data carrier detection.
 * 1. MAXPULSE - maximum value of the DCD pulse counter. Higher values give more
 *    stability once a correct signal is detected, but delay the DCD release.
 * 2. THRES - threshold of the DCD pulse counter. When reached, the input signal
 *    is assumed valid. Higher values mean more noise immunity but a slower DCD set.
 * 3. The MAXPULSE/THRES difference sets the DCD "inertia".
 * 4. INC is added when a symbol change happens near PLL counter zero.
 * 5. DEC is subtracted when a symbol change happens too far from zero.
 * 6. TUNE is the PLL counter tuning coefficient.
 *
 * [       DCD OFF    *      |    DCD ON   ]
 * 0               COUNTER THRES        MAXPULSE
 *        <-DEC INC->
 */
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

#define N1200 8  /* samples per symbol @ fs=9600 */
#define N9600 8  /* samples per symbol @ fs=76800 */
#define N300  32 /* samples per symbol @ fs=9600 */
#define NMAX  32 /* keep equal to the biggest Nx */

/* Every Nx is a samples-per-symbol count, so the PLL step must be 2^32 / Nx.
 *
 * N9600 was 1 (a zero step: dem->pll never advanced and decode() never sampled,
 * so the profile could not decode a single bit by construction), then 4, which
 * at least ran. 4 was still wrong, and it is the whole reason G3RUH sat at 3/5
 * frames while every AFSK profile did 5/5.
 *
 * 4 samples per symbol does not give this timing-recovery design enough to work
 * with. The DPLL's sample instant is quantised to one ADC sample - 25 % of a
 * symbol at N=4 - and the majority vote in decode() spans three of them, i.e.
 * 75 % of a symbol, so the vote window necessarily reaches into a transition.
 * Measured on the host against the real modulator, with the analogue path and
 * both real clocks modelled and no noise at all: at N=4 the profile produces
 * hard bit errors at the sampling phases where the ADC instants coincide with
 * the DAC's update instants (2/8, 4/8 and 6/8 of a symbol), 1.4 % to 2.7 % BER
 * at those phases and 0 % between them. Since the DAC (38461.5 Hz) and the ADC
 * differ by ~0.05 %, the alignment walks through the bad phases every ~55 ms,
 * and a 350 ms transmission crosses them repeatedly. Hence "random" frame loss
 * of roughly 40 %, immune to every amount of TX jitter reduction.
 *
 * At N=8 - the same 8 samples per symbol the 1200 Bd profile has always had,
 * obtained by running the ADC at 76800 Hz - the same simulation gives ZERO bit
 * errors at every sampling phase and with up to 30 us of TX edge jitter. Nothing
 * else about the demodulator changes; it stops being starved of resolution.
 *
 * The vote window and the DPLL tune constants were both swept on the host first:
 * (win=3, shift=0) is already the best of its family and the tune surface is
 * chaotic (some settings collapse to 48 % BER), which is itself a sign the loop
 * is bistable at N=4. There is no tuning fix, only a resolution fix. */
#define PLL1200_STEP ((int32_t)(uint32_t)(((uint64_t)1 << 32) / N1200))
#define PLL9600_STEP ((int32_t)(uint32_t)(((uint64_t)1 << 32) / N9600))
#define PLL300_STEP  ((int32_t)(uint32_t)(((uint64_t)1 << 32) / N300))

/*
 * ADC/DAC clock-ratio calibration.
 *
 * PLLxxxx_STEP above assumes samples-per-symbol is exactly Nxxx, which is
 * only true if the ADC and DAC really run at the nominal
 * MODEM_ADC_SAMPLERATE / MODEM_DAC_SAMPLERATE ratio. They do not: each is
 * an independent hardware timer with its own rounding error against the rate
 * it was configured for (see the MODEM_ADC_SAMPLERATE comment in
 * modem_config.h and dac_timer_create() in afsk.c), and the gap between the
 * two is a steady-state phase error every DPLL in this file has to track for
 * the rest of a transmission. That is the residual, repeatable loss the
 * G3RUH stress test in afsk_loopback_test.c measures: real samples-per-symbol
 * is off from the nominal 8 by a small, fixed amount, and left uncorrected
 * the DPLL has to fight the same known bias, transmission after
 * transmission, instead of being told about it once.
 *
 * sampleRateCorrection is that bias, expressed as (real samples-per-symbol) /
 * (nominal samples-per-symbol). Both clocks are derived from the same
 * crystal, so this ratio is a fixed board property - see
 * ModemCalibrateSampleRate() for the derivation and how it is measured.
 * Default 1.0 (no correction) until ModemCalibrateSampleRate() has been
 * called at least once.
 */
static float sampleRateCorrection = 1.0f;

/**
 * @brief Apply the calibrated ADC/DAC clock ratio to a nominal PLL step.
 *
 * nominalStep = 2^32 / N assumes exactly N samples per symbol. The real
 * count is N * sampleRateCorrection, so the real step is the nominal one
 * divided by the same factor.
 */
static int32_t calibratedPllStep(int32_t nominalStep) {
    double step = (double)(uint32_t)nominalStep / (double)sampleRateCorrection;

    /* Guard the extremes: a step of 0 would never overflow the PLL counter
     * (decode() would never sample a symbol, exactly the N9600=1 bug the
     * comment above this block documents), and a step above 2^32-1 cannot be
     * represented at all. Both would require sampleRateCorrection to be
     * wildly out of range, which ModemCalibrateSampleRate() already rejects,
     * but this keeps the arithmetic itself well-defined regardless. */
    if (step < 1.0)
        step = 1.0;
    if (step > 4294967295.0)
        step = 4294967295.0;

    return (int32_t)(uint32_t)(step + 0.5);
}

void ModemCalibrateSampleRate(float measuredAdcHz, float measuredDacHz) {
    if (!(measuredAdcHz > 0.0f) || !(measuredDacHz > 0.0f)) {
        ESP_LOGW(TAG, "ModemCalibrateSampleRate: no usable measurement (adc=%.1f Hz, dac=%.1f Hz), keeping nominal rates", (double)measuredAdcHz,
                 (double)measuredDacHz);
        sampleRateCorrection = 1.0f;
        return;
    }

    /* actual samples-per-symbol / nominal samples-per-symbol reduces to this
     * ratio for every profile: the decimation factor (or lack of one, for
     * G3RUH) and the baud-rate divider are common to both the actual and the
     * nominal figure and cancel out. See the derivation in modem.h. */
    float ratio = (measuredAdcHz / (float)MODEM_ADC_SAMPLERATE) / (measuredDacHz / (float)MODEM_DAC_SAMPLERATE);

    /* Sane range is a few tenths of a percent either way - both clocks are
     * quartz-derived, so anything past +/-1% is either a bad measurement
     * (ADC not yet running, wrong pin, too short a window - see the
     * quantization-error note in modem_init()) or a board that is broken
     * in some other way this correction cannot help with. Applying a bogus
     * ratio does more harm than the uncorrected nominal rate ever did - a
     * miscalibration of well under 1% was enough to take G3RUH from 10%
     * loss to 85% - so fall back to nominal rather than trust it. */
    if (ratio < 0.99f || ratio > 1.01f) {
        ESP_LOGW(TAG, "ModemCalibrateSampleRate: ratio %.4f (adc=%.1f Hz, dac=%.1f Hz) out of sane range, ignoring", (double)ratio, (double)measuredAdcHz,
                 (double)measuredDacHz);
        sampleRateCorrection = 1.0f;
        return;
    }

    sampleRateCorrection = ratio;
    ESP_LOGI(TAG, "ModemCalibrateSampleRate: ADC %.1f Hz / DAC %.1f Hz -> correction %.5f (%+.3f%%)", (double)measuredAdcHz, (double)measuredDacHz,
             (double)sampleRateCorrection, (double)((sampleRateCorrection - 1.0f) * 100.0f));

    /* Takes effect from the next ModemInit() (i.e. the next afskSetModem()),
     * which is why modem_init() calls this before the first one. Reaching
     * into demodState[] here to patch an already-running profile in place
     * would race afsk_rx_task the same way ModemInit() itself has to guard
     * against - see the note above afskSetModem() in afsk.c - for a case
     * (mid-run recalibration) nothing in this component actually does. */
}

float ModemGetSampleRateCorrection(void) {
    return sampleRateCorrection;
}

#define PLL1200_LOCKED_TUNE     0.74f
#define PLL1200_NOT_LOCKED_TUNE 0.50f
/*
 * 0.97, not the 0.89 inherited with the profile.
 *
 * The tune is the fraction of the phase error left in place at each transition,
 * so a HIGHER number is a WEAKER pull and a narrower loop. The only thing this
 * loop has to track is the DAC/ADC clock offset - about 17 Hz, 0.045 % - and it
 * gets ~4800 transitions a second from a scrambled signal to do it in, so a
 * narrow loop tracks it with a steady-state error around 1 % of a symbol. What
 * a wide loop buys instead is a faster grab onto edge noise, and at four
 * samples per symbol there is very little phase margin to give away.
 *
 * Host simulation, sweeping the DAC/ADC phase over 16 offsets x 2 DMA
 * alignments x 5 frames: 0.89 loses 2.5 % of frames on a clean channel and
 * 12.5 % with 12 us of TX edge jitter; 0.97 loses 0.0 % and 8.8 %. It is also
 * better, not worse, at every preamble length down to 20 ms, so the slower
 * acquisition a narrow loop would normally cost does not materialise: 20 ms is
 * still 192 symbols at 9600 Bd.
 *
 * This is a real but secondary effect. The jitter itself is the dominant term
 * and is fixed in afsk.c; see the dac_write_isr() notes.
 */
#define PLL9600_LOCKED_TUNE     0.97f
#define PLL9600_NOT_LOCKED_TUNE 0.50f
#define PLL300_LOCKED_TUNE      0.74f
#define PLL300_NOT_LOCKED_TUNE  0.50f

#define AMP_TRACKING_ATTACK 0.16f
#define AMP_TRACKING_DECAY  0.00004f

#define PLL_TUNE_BITS 8 /* fixed point bits when tuning the PLL */

#define DIV_ROUND(dividend, divisor) (((dividend) + (divisor) / 2) / (divisor))

/* ------------------------------------------------------------------ */
/* Quarter-wave sine table, 512 points, 8-bit unsigned, midpoint 128.  */
/* Only the first quarter is stored; sinSample() mirrors/inverts it.   */
/* ------------------------------------------------------------------ */
#define SIN_LEN 512

/* DRAM_ATTR: read by sinSample() from the DAC sample ISR. In flash it is an
 * XIP fetch inside the ISR, which is jitter on the transmitted edge. */
static const DRAM_ATTR uint8_t sin_table[128] = {
    128, 129, 131, 132, 134, 135, 137, 138, 140, 142, 143, 145, 146, 148, 149, 151, 152, 154, 155, 157, 158, 160, 162, 163, 165, 166,
    167, 169, 170, 172, 173, 175, 176, 178, 179, 181, 182, 183, 185, 186, 188, 189, 190, 192, 193, 194, 196, 197, 198, 200, 201, 202,
    203, 205, 206, 207, 208, 210, 211, 212, 213, 214, 215, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231,
    232, 233, 234, 234, 235, 236, 237, 238, 238, 239, 240, 241, 241, 242, 243, 243, 244, 245, 245, 246, 246, 247, 248, 248, 249, 249,
    250, 250, 250, 251, 251, 252, 252, 252, 253, 253, 253, 253, 254, 254, 254, 254, 254, 255, 255, 255, 255, 255, 255, 255,
};

/* IRAM_ATTR: called directly from MODEM_BAUDRATE_TIMER_HANDLER(), which runs
 * in the DAC GPTimer ISR. "static inline" alone is just a hint - under
 * CONFIG_COMPILER_OPTIMIZATION_DEBUG (-Og) GCC often leaves it un-inlined,
 * and an un-inlined static function defaults to flash. That is precisely the
 * XIP-fetch-in-the-ISR jitter sin_table's DRAM_ATTR comment above warns
 * about; the table being in DRAM buys nothing if the code reading it is
 * itself a flash round trip. */
static inline uint8_t IRAM_ATTR sinSample(uint16_t i) {
    /* Full cycle is SIN_LEN samples. Reduce to one cycle first (this was
     * previously reduced mod SIN_LEN/2, which folded two cycles into one
     * table sweep and doubled every emitted tone's frequency). Only after
     * establishing which half of the cycle we are in (for the 255-v
     * inversion) do we fold down into the stored quarter-wave table. */
    uint16_t cycle = i % SIN_LEN;
    uint16_t half = cycle % (SIN_LEN / 2);
    uint16_t newI = (half >= (SIN_LEN / 4)) ? (SIN_LEN / 2 - half - 1) : half;
    uint8_t sine = sin_table[newI];
    return (cycle >= (SIN_LEN / 2)) ? (uint8_t)(255 - sine) : sine;
}

/* ------------------------------------------------------------------ */

struct ModemDemodConfig ModemConfig;

/* Note on the ModemInit() vs MODEM_DECODE() race.
 *
 * ModemInit() memsets demodState[] and then reassigns each filter's
 * coeffs/taps piecemeal, while MODEM_DECODE() may be reading that same state
 * from the RX task on another core. A portMUX_TYPE used to be declared here to
 * guard it, with a long comment explaining what it protected - and it was never
 * taken anywhere. It has been removed rather than left to imply a guarantee
 * that did not exist.
 *
 * What actually serialises this is afskSetModem(), which suspends the RX task
 * across the ModemInit() call. Any other caller of ModemInit() must do the
 * same. A spinlock would be the wrong tool anyway: portENTER_CRITICAL masks
 * interrupts up to level 3 on this core, which is exactly how the DAC sample
 * clock got starved and every AX.25 frame destroyed - see the ring buffer
 * notes in afsk.c. */

static uint8_t N;                        /* samples per symbol */
static enum ModemTxTestMode txTestState; /* current TX test mode */
static uint8_t demodCount;               /* number of parallel demodulators */
static uint8_t currentSymbol;            /* current symbol for NRZI encoding */
static uint8_t scrambledSymbol;

float markFreq;
float spaceFreq;
float baudRate;

static uint32_t markStep;  /* Q32 phase increment per DAC sample */
static uint32_t spaceStep; /* Q32 phase increment per DAC sample */
static uint16_t baudRateStep;
static int16_t coeffHiI[NMAX], coeffLoI[NMAX], coeffHiQ[NMAX], coeffLoQ[NMAX];
static uint8_t dcd = 0;

/*
 * G3RUH scrambler state. TX and RX must have one each.
 *
 * A single shared `lfsr` was enough for a half duplex node, which is never
 * scrambling and descrambling at the same moment. It is fatal in full duplex -
 * which is the only mode the loopback self test can run in - because the DAC
 * ISR advances the register once per transmitted symbol while the RX task
 * advances it once per received symbol, from the same variable. Neither
 * register then holds the sequence its own side needs and nothing decodes.
 * They are logically two independent shift registers and are now two
 * variables.
 */
static uint32_t txLfsr = 0x1FFFF;
static uint32_t rxLfsr = 0x1FFFF;

/** BPF with 2200 Hz tone 6 dB preemphasis (it attenuates the 1200 Hz tone by 6 dB) */
static const int16_t bpf1200[8] = { 728, -13418, -554, 19493, -554, -13418, 728, 2104 };

/** BPF with 2200 Hz tone 6 dB deemphasis */
static const int16_t bpf1200Inv[8] = { -10513, -10854, 9589, 23884, 9589, -10854, -10513, -879 };

/* fs=9600, rectangular, fc1=1500, fc2=1900, 0 dB @ 1600/1800 Hz, N=15, gain 65536 */
static const int16_t bpf300[15] = {
    186, 8887, 8184, -1662, -10171, -8509, 386, 5394, 386, -8509, -10171, -1662, 8184, 8887, 186,
};

#define BPF_MAX_TAPS 15

/* fs=9600 Hz, raised cosine, fc=300 Hz (BR=600 Bd), beta=0.8, N=14, gain=65536 */
static const int16_t lpf300[14] = {
    4385, 4515, 4627, 4720, 4793, 4846, 4878, 4878, 4846, 4793, 4720, 4627, 4515, 4385,
};

static const int16_t lpf1200[15] = {
    -6128, -5974, -2503, 4125, 12679, 21152, 27364, 29643, 27364, 21152, 12679, 4125, -2503, -5974, -6128,
};

/* fs=76800 Hz, Gaussian, fc ~5000 Hz (-3 dB at 0.53 x baud), N=15, gain=65536.
 *
 * Redesigned with the sample rate, not merely rescaled. The old 9-tap version
 * was specified for fs=38400 and had to go anyway, but the obvious replacement -
 * the same response at twice the rate, which lands on a 15-tap Gaussian with
 * sigma ~3.0 - is NOT the right filter once there are 8 samples per symbol: its
 * impulse response is then long enough relative to a symbol to cost real ISI.
 * Host sweep of sigma against worst-case BER over 8 sampling phases:
 *
 *      sigma 0.8..2.2  ->  0 errors / 7374 bits
 *      sigma 2.4       ->  6
 *      sigma 2.6       -> 12
 *      sigma 2.8..3.0  -> 18   (the naive "same response as before" choice)
 *
 * sigma 2.0 is taken: mid-range, so there is margin on the ISI side, while
 * -3 dB at ~5 kHz is a defensible noise bandwidth for 9600 Bd on air rather than
 * merely whatever passes a noiseless loopback. */
static const int16_t lpf9600[15] = {
    29, 145, 574, 1769, 4245, 7930, 11538, 13076, 11538, 7930, 4245, 1769, 574, 145, 29,
};

#define LPF_MAX_TAPS    15
#define FILTER_MAX_TAPS ((LPF_MAX_TAPS > BPF_MAX_TAPS) ? LPF_MAX_TAPS : BPF_MAX_TAPS)

struct Filter {
    const int16_t *coeffs;
    uint8_t taps;
    int32_t samples[FILTER_MAX_TAPS];
    uint8_t gainShift;
};

struct DemodState {
    uint8_t rawSymbols;  /* raw, unsynchronized symbols */
    uint8_t syncSymbols; /* synchronized symbols */

    enum ModemPrefilter prefilter;
    struct Filter bpf;
    int16_t correlatorSamples[NMAX];
    uint8_t correlatorSamplesIdx;
    struct Filter lpf;

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

    int16_t peak;
    int16_t valley;
};

static struct DemodState demodState[MODEM_MAX_DEMODULATOR_COUNT];

static void decode(uint8_t symbol, uint8_t demod, uint16_t mV);
static int32_t demodulate(int16_t sample, struct DemodState *dem);

static int32_t filterRun(struct Filter *f, int32_t input) {
    int32_t out = 0;

    for (uint8_t i = f->taps - 1; i > 0; i--)
        f->samples[i] = f->samples[i - 1]; /* shift old samples */

    f->samples[0] = input;
    for (uint8_t i = 0; i < f->taps; i++)
        out += (int32_t)f->coeffs[i] * f->samples[i];

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

uint8_t ModemIsTxTestOngoing(void) {
    return (txTestState != TEST_DISABLED) ? 1 : 0;
}

void ModemGetSignalLevel(uint8_t modem, int8_t *peak, int8_t *valley, uint8_t *level) {
    *peak = (int8_t)((100 * (int32_t)demodState[modem].peak) >> 12);
    *valley = (int8_t)((100 * (int32_t)demodState[modem].valley) >> 12);
    *level = (uint8_t)((100 * (int32_t)(demodState[modem].peak - demodState[modem].valley)) >> 13);
}

enum ModemPrefilter ModemGetFilterType(uint8_t modem) {
    return demodState[modem].prefilter;
}

static void setDcd(bool state) {
    if (state)
        LED_Status2(0, 255, 0);
    else
        LED_Status2(0, 0, 0);
}

static inline uint8_t descramble(uint8_t in) {
    /* G3RUH descrambling (x^17+x^12+1). Self synchronising: the register only
     * has to see 17 symbols of any signal to be in step, so no init handshake
     * is needed and the initial value below does not matter. */
    uint8_t bit = (uint8_t)(((rxLfsr & 0x10000) > 0) ^ ((rxLfsr & 0x800) > 0) ^ (in > 0));

    rxLfsr <<= 1;
    rxLfsr |= in;
    return bit;
}

/* IRAM_ATTR: called from MODEM_BAUDRATE_TIMER_HANDLER() (DAC GPTimer ISR)
 * whenever ModemConfig.modem == MODEM_9600. Same -Og inlining risk as
 * dac_scale()/sinSample() above - "static inline" is not a guarantee, and an
 * un-inlined static function defaults to flash. This one is gated on exactly
 * the G3RUH path, which is why it can look like a 9600-only problem while
 * 1200 Bd profiles never call it and never show the jitter. descramble()
 * does not need this: it only runs from decode(), which executes in
 * afsk_rx_task task context, not at interrupt level. */
static inline uint8_t IRAM_ATTR scramble(uint8_t in) {
    /* G3RUH scrambling (x^17+x^12+1) */
    uint8_t bit = (uint8_t)(((txLfsr & 0x10000) > 0) ^ ((txLfsr & 0x800) > 0) ^ (in > 0));

    txLfsr <<= 1;
    txLfsr |= bit;
    return bit;
}

void MODEM_DECODE(int16_t sample, uint16_t mVrms) {
    bool partialDcd = false;

    for (uint8_t i = 0; i < demodCount; i++) {
        uint8_t symbol = (demodulate(sample, &demodState[i]) > 0);

        decode(symbol, i, mVrms);
        if (demodState[i].dcd)
            partialDcd = true;
    }

    if (partialDcd) {
        setDcd(true);
        dcd = 1;
    } else {
        setDcd(false);
        dcd = 0;
    }
}

/**
 * @brief Baudrate/DAC handler. NRZI encoding happens here.
 *        Runs in the GPTimer ISR at MODEM_DAC_SAMPLERATE.
 */
static uint32_t phaseAcc = 0; /* Q32: the full sine cycle is 2^32 */
static uint16_t sampleIndex = 0;

uint8_t IRAM_ATTR MODEM_BAUDRATE_TIMER_HANDLER(void) {
    uint8_t sinwave = 0;

    if (sampleIndex == 0) {
        if (Ax25GetTxBit() == 0) /* next bit is 0 -> change symbol (NRZI) */
            currentSymbol ^= 1;
        sampleIndex = baudRateStep;

        /*
         * Scramble exactly once per symbol, here, and hold the result for the
         * whole symbol below.
         *
         * This used to live in the MODEM_9600 branch further down, which runs
         * on every DAC sample. At 38400 Hz and 9600 Bd that clocked the
         * scrambler four times per symbol instead of once, so the transmitted
         * sequence was not the G3RUH sequence at all and the receiver's
         * descrambler - correctly clocked once per symbol - could never match
         * it. The other profiles are unaffected: they are not scrambled.
         */
        if (ModemConfig.modem == MODEM_9600)
            scrambledSymbol = scramble(currentSymbol);
    }

    if (ModemConfig.modem == MODEM_9600) {
        sinwave = scrambledSymbol ? 240 : 20;
    } else {
        /*
         * The phase accumulator is 32 bits wide and the table index is the top
         * 9 of them. Stepping the TABLE INDEX by an integer instead - which is
         * what this used to do - quantises the tone to MODEM_DAC_SAMPLERATE /
         * SIN_LEN, i.e. 75 Hz at 38400. Bell 202's 2200 Hz space landed on step
         * 29 and came out at 2175 (-1.14 %); V.23's mark at 1275 (-1.92 %);
         * AFSK300's mark at 1575 (-1.56 %). The correlators tolerated it, but
         * the transmitter was simply off frequency, on air as well as in the
         * loopback. With the fraction carried in the low 23 bits every tone is
         * exact to seven decimal places and the only residual is the timer's
         * own +0.16 %.
         *
         * The index is still 9 bit, so the table lookup still truncates the
         * phase - that is amplitude distortion around -54 dBc, already well
         * under the 8-bit DAC's own noise floor. Frequency is what matters here
         * and frequency is now right.
         */
        if (currentSymbol)
            phaseAcc += spaceStep;
        else
            phaseAcc += markStep;

        sinwave = sinSample((uint16_t)((phaseAcc >> 23) & (SIN_LEN - 1)));
    }

    if (sampleIndex > 0)
        sampleIndex--;

    return sinwave;
}

/**
 * @brief Demodulate a received sample.
 * @param sample Received sample, no more than 13 bits
 * @return Current tone (0 or 1)
 */
static int32_t demodulate(int16_t sample, struct DemodState *dem) {
    /* input signal amplitude tracking */
    if (sample >= dem->peak)
        dem->peak += (int16_t)(((int32_t)(AMP_TRACKING_ATTACK * 32768.f) * (int32_t)(sample - dem->peak)) >> 15);
    else
        dem->peak += (int16_t)(((int32_t)(AMP_TRACKING_DECAY * 32768.f) * (int32_t)(sample - dem->peak)) >> 15);

    if (sample <= dem->valley)
        dem->valley -= (int16_t)(((int32_t)(AMP_TRACKING_ATTACK * 32768.f) * (int32_t)(dem->valley - sample)) >> 15);
    else
        dem->valley -= (int16_t)(((int32_t)(AMP_TRACKING_DECAY * 32768.f) * (int32_t)(dem->valley - sample)) >> 15);

    if (ModemConfig.modem != MODEM_9600) {
        if (dem->prefilter != PREFILTER_NONE)
            dem->correlatorSamples[dem->correlatorSamplesIdx++] = (int16_t)filterRun(&dem->bpf, sample);
        else
            dem->correlatorSamples[dem->correlatorSamplesIdx++] = sample;

        dem->correlatorSamplesIdx %= N;

        int32_t outLoI = 0, outLoQ = 0, outHiI = 0, outHiQ = 0;

        for (uint8_t i = 0; i < N; i++) {
            int16_t t = dem->correlatorSamples[(dem->correlatorSamplesIdx + i) % N];
            outLoI += t * coeffLoI[i];
            outLoQ += t * coeffLoQ[i];
            outHiI += t * coeffHiI[i];
            outHiQ += t * coeffHiQ[i];
        }

        outHiI >>= 14;
        outHiQ >>= 14;
        outLoI >>= 14;
        outLoQ >>= 14;

        sample = (int16_t)((abs(outLoI) + abs(outLoQ)) - (abs(outHiI) + abs(outHiQ)));
    }

    /*
     * DCD using a "PLL". The PLL runs nominally at the baudrate; its counter
     * counts up and overflows to a minimal negative value, so it crosses zero
     * in the middle. A tone change should happen near this zero crossing.
     * Changes near zero raise the DCD pulse counter and pull the counter phase
     * towards zero; changes far from zero lower it. Above dcdThres we claim the
     * incoming signal is valid. dcdMax keeps the DCD from getting "sticky".
     */
    dem->dcdPll = (int32_t)((uint32_t)(dem->dcdPll) + (uint32_t)(dem->pllStep));

    if ((sample > 0) != dem->dcdLastSymbol) { /* tone changed */
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

    /* Return the raw value, not just its sign: callers compare against 0
     * anyway, and the magnitude is what the diagnostics need. */
    return filterRun(&dem->lpf, sample);
}

/**
 * @brief Bit/clock recovery, NRZI decoding, and hand-off to the protocol layer.
 */
static void decode(uint8_t symbol, uint8_t demod, uint16_t mV) {
    struct DemodState *dem = &demodState[demod];

    int32_t previous = dem->pll;

    dem->pll = (int32_t)((uint32_t)(dem->pll) + (uint32_t)(dem->pllStep));

    dem->rawSymbols <<= 1;
    dem->rawSymbols |= (symbol & 1);

    if ((dem->pll < 0) && (previous > 0)) { /* PLL overflow: sample the symbol */
        dem->syncSymbols <<= 1;

        /* take the last three symbols; one is not enough, three work well */
        uint8_t sym = dem->rawSymbols & 0x07;
        if (sym == 0x07 || sym == 0x06 || sym == 0x05 || sym == 0x03)
            sym = 1;
        else
            sym = 0;

        if (ModemConfig.modem == MODEM_9600)
            sym = descramble(sym);

        dem->syncSymbols |= sym;

        /* NRZI decoding: no transition -> 1, transition -> 0 */
        if (((dem->syncSymbols & 0x03) == 0x03) || ((dem->syncSymbols & 0x03) == 0x00))
            Ax25BitParse(1, demod, mV);
        else
            Ax25BitParse(0, demod, mV);
    }

    if (((dem->rawSymbols & 0x03) == 0x02) || ((dem->rawSymbols & 0x03) == 0x01)) {
        if (!dem->dcd) /* PLL not locked - adjust faster */
            dem->pll = (int32_t)(((int64_t)dem->pll * (int64_t)dem->pllNotLockedTune) >> PLL_TUNE_BITS);
        else /* PLL locked - adjust slower */
            dem->pll = (int32_t)(((int64_t)dem->pll * (int64_t)dem->pllLockedTune) >> PLL_TUNE_BITS);
    }
}

void ModemTxTestStart(enum ModemTxTestMode type) {
    if (txTestState != TEST_DISABLED)
        ModemTxTestStop();

    setPtt(true);
    txTestState = type;
}

void ModemTxTestStop(void) {
    txTestState = TEST_DISABLED;
    setTransmit(false);
    setPtt(false);
}

void ModemTransmitStart(void) {
    txTestState = TEST_DISABLED;
    setPtt(true);
    setTransmit(true);
    ESP_LOGD(TAG, "ModemTransmitStart");
}

/**
 * @brief Stop TX and go back to RX.
 *
 * Called from the DAC timer ISR via Ax25GetTxBit(), so it must be ISR safe:
 * it only lowers a flag. AFSK_ServiceTx(), running in the service task, does
 * the actual teardown (stop the timer, park the DAC, release PTT).
 */
void IRAM_ATTR ModemTransmitStop(void) {
    setTransmit(false);
}

uint8_t IRAM_ATTR ModemSinSample(uint16_t i) {
    return sinSample(i);
}

void ModemGetTones(float *mark, float *space) {
    if (mark)
        *mark = markFreq;
    if (space)
        *space = spaceFreq;
}

void ModemGetStepTones(float *mark, float *space) {
    /* Derived from the steps themselves, so this reports what the modulator is
     * really doing rather than what it was asked to do. */
    if (mark)
        *mark = (float)(((double)markStep * (double)MODEM_DAC_SAMPLERATE) / 4294967296.0);
    if (space)
        *space = (float)(((double)spaceStep * (double)MODEM_DAC_SAMPLERATE) / 4294967296.0);
}

int32_t ModemDiagDemodulate(uint8_t demod, int16_t sample) {
    if (demod >= demodCount)
        return 0;
    return demodulate(sample, &demodState[demod]);
}

void ModemInit(void) {
    memset(demodState, 0, sizeof(demodState));

    if (ModemConfig.modem > MODEM_9600)
        ModemConfig.modem = MODEM_1200;

    if ((ModemConfig.modem == MODEM_1200) || (ModemConfig.modem == MODEM_1200_V23)) {
        demodCount = 2;
        N = N1200;
        baudRate = 1200.f;

        demodState[0].pllStep = calibratedPllStep(PLL1200_STEP);
        demodState[0].pllLockedTune = (int32_t)(PLL1200_LOCKED_TUNE * (float)((uint32_t)1 << PLL_TUNE_BITS));
        demodState[0].pllNotLockedTune = (int32_t)(PLL1200_NOT_LOCKED_TUNE * (float)((uint32_t)1 << PLL_TUNE_BITS));
        demodState[0].dcdMax = DCD1200_MAXPULSE;
        demodState[0].dcdThres = DCD1200_THRES;
        demodState[0].dcdInc = DCD1200_INC;
        demodState[0].dcdDec = DCD1200_DEC;
        demodState[0].dcdTune = (int32_t)(DCD1200_TUNE * (float)((uint32_t)1 << PLL_TUNE_BITS));

        demodState[1].pllStep = calibratedPllStep(PLL1200_STEP);
        demodState[1].pllLockedTune = (int32_t)(PLL1200_LOCKED_TUNE * (float)((uint32_t)1 << PLL_TUNE_BITS));
        demodState[1].pllNotLockedTune = (int32_t)(PLL1200_NOT_LOCKED_TUNE * (float)((uint32_t)1 << PLL_TUNE_BITS));
        demodState[1].dcdMax = DCD1200_MAXPULSE;
        demodState[1].dcdThres = DCD1200_THRES;
        demodState[1].dcdInc = DCD1200_INC;
        demodState[1].dcdDec = DCD1200_DEC;
        demodState[1].dcdTune = (int32_t)(DCD1200_TUNE * (float)((uint32_t)1 << PLL_TUNE_BITS));

        demodState[1].prefilter = PREFILTER_NONE;
        demodState[1].lpf.coeffs = lpf1200;
        demodState[1].lpf.taps = sizeof(lpf1200) / sizeof(*lpf1200);
        demodState[1].lpf.gainShift = 15;

        demodState[0].lpf.coeffs = lpf1200;
        demodState[0].lpf.taps = sizeof(lpf1200) / sizeof(*lpf1200);
        demodState[0].lpf.gainShift = 15;
        demodState[0].prefilter = PREFILTER_NONE;

        if (ModemConfig.flatAudioIn) {
            /* flat audio input: use deemphasis + flat modems */
#ifdef ENABLE_FX25
            if (Ax25Config.fx25)
                demodState[0].prefilter = PREFILTER_NONE;
            else
#endif
                demodState[0].prefilter = PREFILTER_DEEMPHASIS;
            demodState[0].bpf.coeffs = bpf1200Inv;
            demodState[0].bpf.taps = sizeof(bpf1200Inv) / sizeof(*bpf1200Inv);
            demodState[0].bpf.gainShift = 15;
        } else {
            /* normal (filtered) audio input: use flat + preemphasis modems */
            demodState[0].prefilter = PREFILTER_PREEMPHASIS;
            demodState[0].bpf.coeffs = bpf1200;
            demodState[0].bpf.taps = sizeof(bpf1200) / sizeof(*bpf1200);
            demodState[0].bpf.gainShift = 15;
        }

        if (ModemConfig.modem == MODEM_1200) { /* Bell 202 */
            markFreq = 1200.f;
            spaceFreq = 2200.f;
        } else { /* V.23 */
            markFreq = 1300.f;
            spaceFreq = 2100.f;
        }
    } else if (ModemConfig.modem == MODEM_300) {
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

        demodState[0].prefilter = PREFILTER_FLAT;
        demodState[0].bpf.coeffs = bpf300;
        demodState[0].bpf.taps = sizeof(bpf300) / sizeof(*bpf300);
        demodState[0].bpf.gainShift = 16;
        demodState[0].lpf.coeffs = lpf300;
        demodState[0].lpf.taps = sizeof(lpf300) / sizeof(*lpf300);
        demodState[0].lpf.gainShift = 15;
    } else if (ModemConfig.modem == MODEM_9600) {
        demodCount = 1;
        N = N9600;
        baudRate = 9600.f;

        /*
         * G3RUH is baseband NRZ, not AFSK: there are no mark and space tones.
         * markFreq used to be set to 38400/128 = 300 Hz here (via a DAC_SINE_SIZE
         * macro whose only remaining use this was, now gone with it), under a
         * comment claiming it was "used as DAC sample rate". Nothing reads it
         * that way - MODEM_DAC_SAMPLERATE is what configures the timer - and
         * the only effect was that ModemGetTones() reported a 300 Hz mark and a
         * 0 Hz space for a profile that emits neither, which any diagnostic
         * driving those tones would then dutifully test. Report the truth.
         */
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

        demodState[0].prefilter = PREFILTER_NONE;
        /* Receive only, despite what this comment said for a long time. Nothing
         * on the TX path runs it - MODEM_BAUDRATE_TIMER_HANDLER() emits a raw
         * 240/20 square - and it should stay that way: shaping the transmitter
         * with this same filter was tried on the host and roughly doubles the
         * BER, because the TX+RX cascade closes the eye. If transmit shaping is
         * ever wanted it needs its own, wider filter and its own state (the
         * receiver is using this instance concurrently in full duplex). */
        demodState[0].lpf.coeffs = lpf9600;
        demodState[0].lpf.taps = sizeof(lpf9600) / sizeof(*lpf9600);
        demodState[0].lpf.gainShift = 16;
    }

    /* Q32 phase increment: step = f * 2^32 / Fs, rounded. Exact to ~1e-7 %. */
    markStep = (uint32_t)(((double)markFreq * 4294967296.0) / (double)MODEM_DAC_SAMPLERATE + 0.5);
    spaceStep = (uint32_t)(((double)spaceFreq * 4294967296.0) / (double)MODEM_DAC_SAMPLERATE + 0.5);
    baudRateStep = (uint16_t)(MODEM_DAC_SAMPLERATE / (uint32_t)baudRate);

    {
        float txMark = 0, txSpace = 0;
        ModemGetStepTones(&txMark, &txSpace);
        ESP_LOGI(TAG, "mark %.1f Hz -> emits %.2f, space %.1f Hz -> emits %.2f, baudRateStep %u, rate correction %+.3f%%", markFreq, txMark, spaceFreq, txSpace,
                 baudRateStep, (double)((sampleRateCorrection - 1.0f) * 100.0f));
    }

    for (uint8_t i = 0; i < N; i++) { /* correlator coefficients */
        coeffLoI[i] = (int16_t)(4095.f * cosf(2.f * 3.1416f * (float)i / (float)N * markFreq / baudRate));
        coeffLoQ[i] = (int16_t)(4095.f * sinf(2.f * 3.1416f * (float)i / (float)N * markFreq / baudRate));
        coeffHiI[i] = (int16_t)(4095.f * cosf(2.f * 3.1416f * (float)i / (float)N * spaceFreq / baudRate));
        coeffHiQ[i] = (int16_t)(4095.f * sinf(2.f * 3.1416f * (float)i / (float)N * spaceFreq / baudRate));
    }

    /* reset the modulator state */
    phaseAcc = 0;
    sampleIndex = 0;
    currentSymbol = 0;
    scrambledSymbol = 0;
    txLfsr = 0x1FFFF;
    rxLfsr = 0x1FFFF;
    dcd = 0;
}
