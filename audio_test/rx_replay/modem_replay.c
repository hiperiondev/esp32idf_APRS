// @file modem_replay.c
//
// @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
// @date 2026
// @copyright GNU General Public License v3
// @see https://github.com/hiperiondev/esp32idf_APRS
//
// @brief PC replay of the 1200 Bd receive chain of the firmware.
//
// The demodulators, the HDLC receiver, the impulse blanker and the gain
// control are the firmware's own sources (modem.c, ax25.c, fx25.c,
// impulse_blanker.h, rx_agc.h), compiled for the PC against the stand-in
// headers of components/esp32idf_radioamateur_modem/test/host/stubs. The
// rest of the front end of AFSK_Poll() in afsk.c - the running DC average,
// the decimation filter, the high-pass, the tone-band meter and the receive
// gate with its hold ring - is reproduced here step by step; the decimation
// coefficients are taken from afsk.c by build.sh when the tool is built.
//
// Two inputs:
//
//   recording (default)  signed 16-bit mono samples at 76800 Hz on stdin,
//                        turned into ADC codes around mid-scale: --gain ADC
//                        counts per full-scale sample, --noise RMS counts of
//                        converter noise. The whole front end runs.
//   --capture FILE       a POST /radio/capture stream: afsk_capture_block_t
//                        records, i.e. the decimated, high-passed demodulator
//                        input of the ESP32 before the gain, one record per
//                        20 ms block. The recorded gate state and gain are
//                        applied (--no-gate feeds every block, --agc runs the
//                        gain control again instead of using the recorded
//                        gain).
//
// Every frame is written to stdout as a TNC2 line, bytes outside 0x20..0x7e
// of the information field as <0xNN> the way Direwolf writes them. The
// statistics rx_diag.py parses go to stderr.

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "afsk.h"
#include "ax25.h"
#include "esp32idf_radioamateur_modem.h"
#include "impulse_blanker.h"
#include "modem.h"
#include "rx_agc.h"

// Decimation filter of afsk.c for MODEM_RESAMPLE_RATIO == 8, written by
// build.sh as "static const float resample_coeffs[FILTER_TAPS] = {...};".
#define FILTER_TAPS 48
#include "resample_coeffs.inc"

_Static_assert(MODEM_RESAMPLE_RATIO == 8, "the replay reproduces the 76800 -> 9600 Hz front end");

#define BLOCK        MODEM_BLOCK_SIZE
#define DECIM        (MODEM_BLOCK_SIZE / MODEM_RESAMPLE_RATIO)
#define AVG_N        125
#define HOLD_BLOCKS  3
#define ADC_MID      2048
#define RECORD_BYTES 396

_Static_assert(sizeof(afsk_capture_block_t) == RECORD_BYTES, "capture record layout");
_Static_assert(AFSK_CAPTURE_SAMPLES == DECIM, "one capture record per decimated block");

// ------------------------------------------------------------------
// Firmware symbols the modem core calls outside the sources built here
// ------------------------------------------------------------------

static int64_t s_nowUs = 0;
static uint32_t s_rand = 0x12345678u;

int64_t esp_timer_get_time(void) {
    return s_nowUs;
}

uint32_t esp_random(void) {
    s_rand = s_rand * 1664525u + 1013904223u;
    return s_rand;
}

void LED_Status2(uint8_t r, uint8_t g, uint8_t b) {
    (void)r;
    (void)g;
    (void)b;
}

void setPtt(bool state) {
    (void)state;
}

void setTransmit(bool val) {
    (void)val;
}

bool getTransmit(void) {
    return false;
}

bool AFSK_TxTeardownPending(void) {
    return false;
}

void AFSK_ServiceTx(void) {
}

uint32_t afskGetDacSampleRate(void) {
    return MODEM_DAC_SAMPLERATE;
}

float afskGetDacAlarmRate(void) {
    return (float)MODEM_DAC_SAMPLERATE;
}

bool afskGetFullDuplex(void) {
    return false;
}

// The values come from the command line and are clamped there.
void modem_rx_tuning_sanitize(modem_rx_tuning_t *t) {
    (void)t;
}

// Called by ax25.c only to build debug log lines, which the stand-in log
// macros discard.
void modem_format_tnc2(const ax25_msg_t *msg, char *out, size_t out_len) {
    (void)msg;
    if (out_len > 0)
        out[0] = 0;
}

// ------------------------------------------------------------------
// Options
// ------------------------------------------------------------------

typedef struct {
    modem_rx_tuning_t rx;
    bool flat;
    float gain;
    float noise;
    float mvPerCount;
    const char *capture;
    bool noGate;
    bool agcRecompute;
    unsigned seed;
} options_t;

static void usage(void) {
    fprintf(stderr, "usage: modem_replay [--preset N] [--taps N] [--lo HZ] [--hi HZ] [--hpf HZ] [--gate-mv MV] [--blank 0|1]\n"
                    "                    [--agc-fixed DB] [--fix-bits N] [--flat 0|1] [--mv-per-count X]\n"
                    "                    [--gain COUNTS] [--noise COUNTS] [--seed N] < samples.s16\n"
                    "       modem_replay [same demodulator options] --capture FILE [--no-gate] [--agc]\n");
    exit(2);
}

static long clampl(long v, long lo, long hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static void parse(int argc, char **argv, options_t *o) {
    const modem_rx_tuning_t def = MODEM_RX_TUNING_DEFAULT();

    memset(o, 0, sizeof(*o));
    o->rx = def;
    o->gain = 1200.0f;
    o->noise = 1.2f;
    o->mvPerCount = 0.806f;
    o->seed = 1;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;

        if (!strcmp(a, "--no-gate")) {
            o->noGate = true;
            continue;
        }
        if (!strcmp(a, "--agc")) {
            o->agcRecompute = true;
            continue;
        }
        if (v == NULL)
            usage();
        i++;

        if (!strcmp(a, "--preset"))
            o->rx.eq_preset = (modem_rx_eq_preset_t)clampl(atol(v), MODEM_RX_EQ_LEGACY, MODEM_RX_EQ_MULTISLICE2);
        else if (!strcmp(a, "--taps"))
            o->rx.bpf_taps = (uint8_t)(clampl(atol(v), MODEM_RX_BPF_TAPS_MIN, MODEM_RX_BPF_TAPS_MAX) | 1);
        else if (!strcmp(a, "--lo"))
            o->rx.bpf_lo_hz = (uint16_t)clampl(atol(v), MODEM_RX_BPF_LO_HZ_MIN, MODEM_RX_BPF_LO_HZ_MAX);
        else if (!strcmp(a, "--hi"))
            o->rx.bpf_hi_hz = (uint16_t)clampl(atol(v), MODEM_RX_BPF_HI_HZ_MIN, MODEM_RX_BPF_HI_HZ_MAX);
        else if (!strcmp(a, "--hpf"))
            o->rx.hpf_hz = (uint16_t)clampl(atol(v), 0, MODEM_RX_HPF_HZ_MAX);
        else if (!strcmp(a, "--gate-mv"))
            o->rx.gate_mv = (uint16_t)clampl(atol(v), 0, MODEM_RX_GATE_MV_MAX);
        else if (!strcmp(a, "--blank"))
            o->rx.impulse_blank = atol(v) != 0;
        else if (!strcmp(a, "--agc-fixed")) {
            o->rx.agc_mode = MODEM_RX_AGC_FIXED;
            o->rx.agc_fixed_gain_db = (int8_t)clampl(atol(v), MODEM_RX_AGC_GAIN_DB_MIN, MODEM_RX_AGC_GAIN_DB_MAX);
        } else if (!strcmp(a, "--fix-bits"))
            o->rx.fix_bits = (uint8_t)clampl(atol(v), 0, MODEM_RX_FIX_BITS_MAX);
        else if (!strcmp(a, "--flat"))
            o->flat = atol(v) != 0;
        else if (!strcmp(a, "--gain"))
            o->gain = (float)atof(v);
        else if (!strcmp(a, "--noise"))
            o->noise = (float)atof(v);
        else if (!strcmp(a, "--mv-per-count"))
            o->mvPerCount = (float)atof(v);
        else if (!strcmp(a, "--seed"))
            o->seed = (unsigned)atol(v);
        else if (!strcmp(a, "--capture"))
            o->capture = v;
        else
            usage();
    }
}

// ------------------------------------------------------------------
// Front end of AFSK_Poll()
// ------------------------------------------------------------------

static float s_tail[FILTER_TAPS - 1];

// Block decimator with the previous block's tail carried over; the output
// lands in the first DECIM slots of buf.
static void decimate(float *buf) {
    static float in[FILTER_TAPS - 1 + BLOCK];

    memcpy(in, s_tail, sizeof(s_tail));
    memcpy(in + FILTER_TAPS - 1, buf, BLOCK * sizeof(float));
    memcpy(s_tail, in + BLOCK, sizeof(s_tail));

    for (int i = 0; i < DECIM; i++) {
        float sum = 0.0f;
        const float *w = &in[i * MODEM_RESAMPLE_RATIO];
        for (int j = 0; j < FILTER_TAPS; j++)
            sum += w[j] * resample_coeffs[j];
        buf[i] = sum;
    }
}

typedef struct {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
} biquad_t;

static void biquad_hpf(biquad_t *f, unsigned hz) {
    memset(f, 0, sizeof(*f));
    const float k = tanf((float)M_PI * (float)hz / (float)MODEM_DEMOD_SAMPLERATE);
    const float q = 1.41421356f;
    const float norm = 1.0f / (1.0f + q * k + k * k);
    f->b0 = norm;
    f->b1 = -2.0f * norm;
    f->b2 = norm;
    f->a1 = 2.0f * (k * k - 1.0f) * norm;
    f->a2 = (1.0f - q * k + k * k) * norm;
}

static void biquad_band(biquad_t *f) {
    memset(f, 0, sizeof(*f));
    const float f0 = sqrtf(900.0f * 2600.0f);
    const float w0 = 2.0f * (float)M_PI * f0 / (float)MODEM_DEMOD_SAMPLERATE;
    const float alpha = sinf(w0) / (2.0f * 0.9f);
    const float a0 = 1.0f + alpha;
    f->b0 = alpha / a0;
    f->b1 = 0.0f;
    f->b2 = -alpha / a0;
    f->a1 = -2.0f * cosf(w0) / a0;
    f->a2 = (1.0f - alpha) / a0;
}

static inline float biquad_step(biquad_t *f, float x) {
    float y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1;
    f->x1 = x;
    f->y2 = f->y1;
    f->y1 = y;
    return y;
}

// RMS of a decimated block through the fourth-order tone-band meter, in ADC
// counts; buf is left unchanged.
static float band_rms(biquad_t band[2], const float *buf, int len) {
    float sumSq = 0.0f;
    for (int i = 0; i < len; i++) {
        float v = biquad_step(&band[1], biquad_step(&band[0], buf[i]));
        sumSq += v * v;
    }
    return sqrtf(sumSq / (float)len) * 2048.0f;
}

// ------------------------------------------------------------------
// Demodulation and output
// ------------------------------------------------------------------

static unsigned long s_frames = 0;

static void print_call(const ax25_call_t *c) {
    if (c->ssid)
        printf("%s-%u", c->call, c->ssid);
    else
        printf("%s", c->call);
}

static void drain_frames(void) {
    uint8_t *data;
    uint16_t size;
    struct Ax25RxMeta meta;
    static ax25_msg_t msg;

    while (Ax25ReadNextRxFrame(&data, &size, &meta)) {
        if (!ax25_decode(data, size, meta.mVrms, &msg, NULL))
            continue;
        s_frames++;
        print_call(&msg.src);
        printf(">");
        print_call(&msg.dst);
        for (uint8_t i = 0; i < msg.rpt_count; i++) {
            printf(",");
            print_call(&msg.rpt_list[i]);
            if (AX25_REPEATED(&msg, i))
                printf("*");
        }
        printf(":");
        for (size_t i = 0; i < msg.len; i++) {
            uint8_t b = msg.info[i];
            if ((b < 0x20) || (b > 0x7e))
                printf("<0x%02x>", b);
            else
                putchar(b);
        }
        printf("\n");
    }
}

static void feed(const float *buf, int len, float gain) {
    const float scale = gain * 2048.0f;
    for (int i = 0; i < len; i++) {
        float v = buf[i] * scale;
        if (v > 2047.0f)
            v = 2047.0f;
        else if (v < -2047.0f)
            v = -2047.0f;
        MODEM_DECODE((int16_t)v, 0);
    }
    drain_frames();
}

static double gaussian(void) {
    double u1 = ((double)rand() + 1.0) / ((double)RAND_MAX + 2.0);
    double u2 = ((double)rand() + 1.0) / ((double)RAND_MAX + 2.0);
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

static int cmp_float(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static float percentile(float *v, size_t n, float p) {
    if (n == 0)
        return 0.0f;
    qsort(v, n, sizeof(float), cmp_float);
    return v[(size_t)(p * (float)(n - 1))];
}

static void print_demods(void) {
    struct Ax25RxStats st;
    Ax25GetRxStats(&st);
    fprintf(stderr, "frames per demodulator:");
    for (uint8_t i = 0; i < ModemGetDemodulatorCount(); i++)
        fprintf(stderr, " %lu", (unsigned long)st.decoded[i]);
    fprintf(stderr, "\n");
}

// ------------------------------------------------------------------
// Recording input
// ------------------------------------------------------------------

static int run_recording(const options_t *o) {
    static int16_t in[BLOCK];
    static float audio[BLOCK];
    static float hold[HOLD_BLOCKS][DECIM];
    static uint16_t avgBuf[AVG_N];
    impulse_blanker_t blanker;
    biquad_t hpf, band[2];

    impulse_blanker_reset(&blanker);
    biquad_hpf(&hpf, o->rx.hpf_hz);
    biquad_band(&band[0]);
    biquad_band(&band[1]);
    for (int i = 0; i < AVG_N; i++)
        avgBuf[i] = ADC_MID;
    int avgSum = ADC_MID * AVG_N, avgIdx = 0, avg = ADC_MID;

    const bool agcFixed = (o->rx.agc_mode == MODEM_RX_AGC_FIXED);
    float gain = agcFixed ? powf(10.0f, (float)o->rx.agc_fixed_gain_db / 20.0f) : 1.0f;
    const unsigned gateOn = o->rx.gate_mv, gateOff = o->rx.gate_mv / 2;
    unsigned dcdCnt = 0, holdHead = 0, holdCount = 0;
    unsigned long samples = 0, rails = 0;

    srand(o->seed);
    const clock_t cpu0 = clock();

    while (fread(in, sizeof(int16_t), BLOCK, stdin) == BLOCK) {
        for (int x = 0; x < BLOCK; x++) {
            double code = ADC_MID + (double)in[x] / 32768.0 * (double)o->gain + (double)o->noise * gaussian();
            int16_t raw = (int16_t)((code < 0.0) ? 0 : ((code > 4095.0) ? 4095 : lrint(code)));
            if ((raw <= AFSK_RAW_CLIP_LOW) || (raw >= AFSK_RAW_CLIP_HIGH))
                rails++;
            samples++;

            int16_t adc = impulse_blanker_step(&blanker, raw, o->rx.impulse_blank);
            avgSum += adc - (int)avgBuf[avgIdx];
            avgBuf[avgIdx++] = (uint16_t)adc;
            if (avgIdx >= AVG_N)
                avgIdx = 0;
            avg = avgSum / AVG_N;
            audio[x] = (float)(adc - avg) / 2048.0f;
        }

        decimate(audio);
        if (o->rx.hpf_hz > 0) {
            for (int i = 0; i < DECIM; i++)
                audio[i] = biquad_step(&hpf, audio[i]);
        }
        const int bandMv = (int)(band_rms(band, audio, DECIM) * o->mvPerCount + 0.5f);

        if (gateOn > 0) {
            if (bandMv > (int)gateOn) {
                if (dcdCnt < 100)
                    dcdCnt++;
            } else if (bandMv < (int)gateOff) {
                if (dcdCnt > 0)
                    dcdCnt--;
            }
        }
        const bool open = (gateOn == 0) || (dcdCnt > 3);

        if (open) {
            if (!agcFixed)
                gain = rx_agc_update(gain, audio, DECIM);
            for (unsigned n = 0; n < holdCount; n++)
                feed(hold[(holdHead + HOLD_BLOCKS - holdCount + n) % HOLD_BLOCKS], DECIM, gain);
            holdCount = 0;
            feed(audio, DECIM, gain);
        } else {
            memcpy(hold[holdHead], audio, sizeof(hold[0]));
            holdHead = (holdHead + 1) % HOLD_BLOCKS;
            if (holdCount < HOLD_BLOCKS)
                holdCount++;
        }
        s_nowUs += 20000;
    }

    const double cpu = (double)(clock() - cpu0) / CLOCKS_PER_SEC;
    const double audioS = (double)samples / (double)MODEM_ADC_SAMPLERATE;
    fprintf(stderr, "ADC rails: %lu of %lu samples (%.3f %%)\n", rails, samples, samples ? 100.0 * (double)rails / (double)samples : 0.0);
    fprintf(stderr, "frames: %lu in %.1f s of audio\n", s_frames, audioS);
    fprintf(stderr, "CPU on this PC: %.4f s per s of audio\n", (audioS > 0.0) ? cpu / audioS : 0.0);
    print_demods();
    return 0;
}

// ------------------------------------------------------------------
// Capture input
// ------------------------------------------------------------------

static int run_capture(const options_t *o) {
    FILE *f = fopen(o->capture, "rb");
    if (f == NULL) {
        perror(o->capture);
        return 1;
    }

    static afsk_capture_block_t rec;
    static float audio[DECIM];
    static float hold[HOLD_BLOCKS][DECIM];
    unsigned holdHead = 0, holdCount = 0;
    float agcGain = 1.0f;

    size_t cap = 4096, n = 0;
    float *rmsAll = malloc(cap * sizeof(float));
    float *rmsClosed = malloc(cap * sizeof(float));
    size_t nClosed = 0;
    if ((rmsAll == NULL) || (rmsClosed == NULL)) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    unsigned long clip = 0, closed = 0, impulsive = 0, blanked = 0, missing = 0;
    unsigned long busySum = 0, busyMax = 0;
    uint32_t firstSeq = 0, lastSeq = 0;

    while (fread(&rec, RECORD_BYTES, 1, f) == 1) {
        if (n == 0)
            firstSeq = rec.seq;
        else if (rec.seq > lastSeq + 1)
            missing += rec.seq - lastSeq - 1;
        lastSeq = rec.seq;

        float sumSq = 0.0f, peak = 0.0f;
        for (int i = 0; i < DECIM; i++) {
            audio[i] = (float)rec.samples[i] / 16384.0f;
            sumSq += audio[i] * audio[i];
            if (fabsf(audio[i]) > peak)
                peak = fabsf(audio[i]);
        }
        const float rms = sqrtf(sumSq / (float)DECIM);
        const float rmsMv = rms * 2048.0f * o->mvPerCount;

        if (n == cap) {
            cap *= 2;
            float *a = realloc(rmsAll, cap * sizeof(float));
            float *b = realloc(rmsClosed, cap * sizeof(float));
            if ((a == NULL) || (b == NULL)) {
                fprintf(stderr, "out of memory\n");
                return 1;
            }
            rmsAll = a;
            rmsClosed = b;
        }
        rmsAll[n++] = rmsMv;

        const bool open = o->noGate || ((rec.flags & AFSK_CAPTURE_FLAG_GATE_OPEN) != 0);
        if (rec.flags & AFSK_CAPTURE_FLAG_CLIP)
            clip++;
        if (rec.flags & AFSK_CAPTURE_FLAG_BLANKED)
            blanked++;
        if ((rec.flags & AFSK_CAPTURE_FLAG_GATE_OPEN) == 0) {
            closed++;
            rmsClosed[nClosed++] = rmsMv;
        }
        if ((rms > 0.0f) && (peak > 6.0f * rms))
            impulsive++;
        busySum += rec.busy_us;
        if (rec.busy_us > busyMax)
            busyMax = rec.busy_us;

        float gain;
        if (o->rx.agc_mode == MODEM_RX_AGC_FIXED)
            gain = powf(10.0f, (float)o->rx.agc_fixed_gain_db / 20.0f);
        else if (o->agcRecompute)
            gain = (open ? (agcGain = rx_agc_update(agcGain, audio, DECIM)) : agcGain);
        else
            gain = (float)rec.agc_x16 / 16.0f;

        if (open) {
            for (unsigned k = 0; k < holdCount; k++)
                feed(hold[(holdHead + HOLD_BLOCKS - holdCount + k) % HOLD_BLOCKS], DECIM, gain);
            holdCount = 0;
            feed(audio, DECIM, gain);
        } else {
            memcpy(hold[holdHead], audio, sizeof(hold[0]));
            holdHead = (holdHead + 1) % HOLD_BLOCKS;
            if (holdCount < HOLD_BLOCKS)
                holdCount++;
        }
        s_nowUs += 20000;
    }
    fclose(f);

    const double pct = n ? 100.0 / (double)n : 0.0;
    fprintf(stderr, "capture: %lu records, seq %lu..%lu, with %lu record(s) missing\n", (unsigned long)n, (unsigned long)firstSeq, (unsigned long)lastSeq,
            missing);
    fprintf(stderr, "ADC rails: %lu block(s)\n", clip);
    fprintf(stderr, "gate closed: %lu (%.1f %%)\n", closed, (double)closed * pct);
    fprintf(stderr, "impulsive blocks (peak > 6x RMS): %lu (%.1f %%)\n", impulsive, (double)impulsive * pct);
    fprintf(stderr, "impulse blanker: %lu (%.1f %%)\n", blanked, (double)blanked * pct);
    fprintf(stderr, "receive task time per block: mean %.1f us, max %lu us\n", n ? (double)busySum / (double)n : 0.0, busyMax);
    fprintf(stderr, "input RMS at the pin, mV: p50 %.2f p90 %.2f\n", percentile(rmsAll, n, 0.5f), percentile(rmsAll, n, 0.9f));
    fprintf(stderr, "gate-closed blocks p50 %.2f p90 %.2f\n", percentile(rmsClosed, nClosed, 0.5f), percentile(rmsClosed, nClosed, 0.9f));
    fprintf(stderr, "frames: %lu\n", s_frames);
    print_demods();

    free(rmsAll);
    free(rmsClosed);
    return 0;
}

int main(int argc, char **argv) {
    options_t o;
    parse(argc, argv, &o);

    ModemConfig.modem = MODEM_MODEM_BELL202;
    ModemConfig.flatAudioIn = o.flat ? 1 : 0;
    ModemSetRxTuning(&o.rx);
    ModemInit();
    Ax25Init(0);
    Ax25SetFixBits(o.rx.fix_bits);

    return o.capture ? run_capture(&o) : run_recording(&o);
}
