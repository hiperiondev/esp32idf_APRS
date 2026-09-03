// @file afsk.c
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
// @brief ESP-IDF hardware abstraction layer implementation for the AFSK modem:
// free-running continuous ADC (DMA) receive path, GPTimer-driven one-shot DAC
// transmit path, and PTT / TX / RX GPIO handling.

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "driver/dac_oneshot.h"
// driver/gpio.h for gpio_config()/gpio_set_level()/gpio_num_t, used by the
// MODEM_PTT_GPIO / MODEM_LED_TX_GPIO / MODEM_LED_RX_GPIO blocks below. Those
// three pins default to -1 in esp32idf_radioamateur_modem_config.h, so each
// #if MODEM_*_GPIO >= 0 block that touches the GPIO API is compiled in only
// when its pin is assigned a real value - as this project does for PTT, from
// the top-level CMakeLists.txt.
//
// MODEM_PTT_GPIO in particular is a fixed, compile-time-only board wiring
// choice - an internal radiomodem feature, exactly like MODEM_ADC_GPIO /
// MODEM_DAC_GPIO - and is not exposed as a runtime/web-admin setting.
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "esp_attr.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// hal/gpio_ll.h provides gpio_ll_set_level(): a direct out-register write,
// always-inline and therefore IRAM-safe. gpio_set_level() from driver/gpio.h
// lives in flash, so it must never be called from setPtt()/LED_Status2(),
// which run in the DAC ISR while the flash cache can be disabled by a
// concurrent flash read.
#include "hal/gpio_ll.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"

#include "afsk.h"
#include "ax25.h"
#include "esp32idf_radioamateur_modem_config.h"
#include "modem.h"

#ifdef ENABLE_FX25
#include "fx25.h"
#endif

static const char *TAG = "afsk";

// ------------------------------------------------------------------
// RX sample ring buffer
// ------------------------------------------------------------------

// Reassembly buffer between adc_ingest() and AFSK_Poll(). Both run in
// afsk_rx_task, so this is a plain single-threaded queue; it exists only
// because adc_continuous_read() may hand back a partial frame and AFSK_Poll()
// wants whole MODEM_BLOCK_SIZE blocks.
//
// The sample copy runs entirely at task priority, out of interrupt context:
// the ADC callback does nothing but count samples (see adc_conv_done_cb), and
// afsk_rx_task blocks in adc_continuous_read() and does the work where taking
// as long as it likes costs the modulator nothing. Keeping the copy off the
// DAC clock's interrupt level (level 3) matters: a contiguous blackout of that
// timer longer than about 4-6 sample periods (~100-150 us) freezes the
// modulator mid-symbol at every baud rate, because the phase accumulator and
// the symbol counter both stop and the resulting step in the waveform is not
// something a tracking loop can follow. A uniform rate of missed DAC alarms,
// by contrast, is harmless - the demodulator's DPLL tracks it and every frame
// still decodes.
//
// MODEM_RX_FIFO_SIZE must be a power of two so the index is a mask, not a
// modulo of a wrapping counter.
_Static_assert((MODEM_RX_FIFO_SIZE & (MODEM_RX_FIFO_SIZE - 1)) == 0, "MODEM_RX_FIFO_SIZE must be a power of two");

#define RB_MASK (MODEM_RX_FIFO_SIZE - 1)

typedef struct {
    int16_t buffer[MODEM_RX_FIFO_SIZE];
    volatile uint32_t head; // written by adc_ingest() only
    volatile uint32_t tail; // written by AFSK_Poll() only
} RingBuffer;

static RingBuffer s_fifo;

// Deferred flush. AFSK_FlushFifo() is called from the service task and from
// afskSetModem(), neither of which is the consumer; letting them write tail
// directly would put two writers on it and break the single-consumer invariant.
// They raise a flag instead and AFSK_Poll() does the work between blocks.
static volatile bool s_flushReq = false;

static inline void rb_init(RingBuffer *rb) {
    rb->head = 0;
    rb->tail = 0;
}

static inline uint32_t rb_size(const RingBuffer *rb) {
    return rb->head - rb->tail; // unsigned: correct across the wrap
}

static bool rb_pop(RingBuffer *rb, int16_t *data) {
    uint32_t tail = rb->tail;

    if ((rb->head - tail) == 0)
        return false;

    *data = rb->buffer[tail & RB_MASK];
    __sync_synchronize(); // read the sample before publishing the new tail
    rb->tail = tail + 1;
    return true;
}

void AFSK_FlushFifo(void) {
    s_flushReq = true;
}

// ------------------------------------------------------------------
// State
// ------------------------------------------------------------------

static adc_continuous_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali = NULL;
static dac_oneshot_handle_t s_dac = NULL;
static gptimer_handle_t s_dacTimer = NULL;
static TaskHandle_t s_rxTask = NULL;
static volatile bool s_rxStop = false;   // asks afsk_rx_task to exit
static volatile bool s_rxExited = false; // set by afsk_rx_task on the way out

static volatile bool s_txActive = false;
static volatile bool s_txStopPending = false;
static volatile bool s_fullDuplex = true;
static bool s_dacTimerRunning = false;
static bool s_inited = false;

static volatile uint32_t s_adcSamples = 0; // total samples produced by the ADC
static float s_dacAlarmRateHz = 0.0f;      // rate the alarm really fires at

// Diagnostic capture tap (raw ADC samples, still used by main/aprs_service.c).
//
// The buffer is owned by this file and never handed out: afskDiagCaptureRaw()
// copies out of it once the capture window closes. The producer runs in
// afsk_rx_task (possibly on the other core) and the consumer in the caller's
// task, so a caller-supplied destination pointer could be revoked in the
// instructions between the producer's bounds test and its store. With a
// statically owned buffer there is no pointer to revoke: the only shared state
// is the arm/disarm length, and a store that slips through after disarming
// still lands inside s_capBuf.
#define AFSK_DIAG_CAP_SAMPLES 512

// Blocking timeout of one adc_continuous_read() call, in milliseconds. It is a
// safety net rather than a rate limit: at the configured sample rate a frame is
// always ready far sooner, and the bounded wait is what lets the receive task
// notice a stop request.
#define ADC_READ_TIMEOUT_MS 100

static int16_t s_capBuf[AFSK_DIAG_CAP_SAMPLES];
static volatile int s_capRawLen = 0; // > 0 arms the tap; also the write bound
static volatile int s_capRawIdx = 0;

static float s_audio[MODEM_BLOCK_SIZE];
static float s_agcGain = 1.0f;

static int s_offset = 0; // DC offset of the input, in mV
static int s_mVrms = 0;  // last RMS reading, in mV
static uint8_t s_dcdCnt = 0;

// Running DC average of the raw ADC stream, used to re-centre the samples
// before demodulation.
#define AVG_N 125
static uint16_t s_avgBuf[AVG_N];
static uint8_t s_avgIdx = 0;
static int s_avgSum = 0;
static uint16_t s_avg = 2048;

// DAC idle/centre code and amplitude scaling.
//
// IRAM_ATTR: called directly from dac_timer_isr(), which is itself IRAM_ATTR.
// A plain "static inline" is only a hint - at CONFIG_COMPILER_OPTIMIZATION_DEBUG
// (-Og), GCC frequently declines it, and an un-inlined static function is
// placed in flash by default. That introduces the exact XIP-fetch jitter the
// other ISR-path functions on the transmit side (dac_write_isr() here,
// calculateCRC() in ax25.c) are IRAM_ATTR to avoid.
#define DAC_MID 128
static inline uint8_t IRAM_ATTR dac_scale(uint8_t s) {
    int v = DAC_MID + (((int)s - DAC_MID) * MODEM_DAC_AMPLITUDE_PCT) / 100;
    if (v < 0)
        v = 0;
    if (v > 255)
        v = 255;
    return (uint8_t)v;
}

// ------------------------------------------------------------------
// AGC / resampler
// ------------------------------------------------------------------

#define AGC_TARGET_RMS 0.2f // target RMS level (-10 dBFS)

// Attack = gain coming DOWN because the signal is too loud. It must be fast:
// an overdriven demodulator decodes nothing. Release = gain going UP on a quiet
// signal, and must be slow so noise between frames does not pump the gain.
#define AGC_ATTACK   0.05f
#define AGC_RELEASE  0.002f
#define AGC_MAX_GAIN 8.0f
#define AGC_MIN_GAIN 0.1f

// Below this block RMS there is nothing but ADC noise, so there is no
// meaningful level to track.
#define AGC_SQUELCH_RMS 0.01f

// Most the gain may change in a single 20 ms block. Without this a single
// near-silent block drives error towards infinity and slams the gain to the
// rail in one step.
#define AGC_MAX_STEP 2.0f

// @brief Track the input level.
//
// Only call this when a signal is actually present. Between frames the DAC is
// parked at mid-scale and the DC tracker removes it, so the block RMS is
// essentially zero; adapting on that would compute AGC_TARGET_RMS/~0, peg the
// gain at AGC_MAX_GAIN, and hand the next transmission to the demodulator
// roughly 10x overdriven - well past the 13 bits demodulate() accepts, which
// overflows the int32 accumulator in filterRun() (the 15-tap AFSK300 BPF is the
// first to go) and wraps the int16 cast on the correlator output, inverting the
// symbol decision, so every profile would decode zero frames.
static float update_agc(const float *buf, size_t len) {
    float sum_sq = 0;
    for (size_t i = 0; i < len; i++)
        sum_sq += buf[i] * buf[i];

    float rms = sqrtf(sum_sq / (float)len);
    if (rms < AGC_SQUELCH_RMS)
        return s_agcGain; // noise only - hold the gain where it is

    float error = AGC_TARGET_RMS / rms;
    if (error > AGC_MAX_STEP)
        error = AGC_MAX_STEP;
    else if (error < 1.0f / AGC_MAX_STEP)
        error = 1.0f / AGC_MAX_STEP;

    float rate = (error < 1.0f) ? AGC_ATTACK : AGC_RELEASE;

    s_agcGain += (s_agcGain * error - s_agcGain) * rate;
    s_agcGain = fmaxf(fminf(s_agcGain, AGC_MAX_GAIN), AGC_MIN_GAIN);
    return s_agcGain;
}

// Anti-alias FIR for the MODEM_ADC_SAMPLERATE -> 9600 Hz decimation.
//
// The cutoff is fixed by the job: 4800 Hz, the 9600 Hz Nyquist. The LENGTH is
// not - it is set by the ratio, because the cutoff is what matters in NORMALISED
// terms and that halves when the input rate doubles. The 8-tap set below was cut
// for 38400 -> 9600 (4:1, normalised cutoff 0.125). Reusing it at 76800 -> 9600
// (8:1, normalised 0.0625) does not filter, it just picks every eighth output of
// a filter that is now twice too wide, and the AFSK profiles alias into
// unintelligibility - verified on the host, 48 % BER.
//
// So the taps follow the ratio. Both sets are Hamming-windowed sincs at 4800 Hz
// and both are host-verified to give 0 bit errors on Bell202, V.23 and AFSK300.
#if MODEM_RESAMPLE_RATIO == 8
#define FILTER_TAPS 16
static const float resample_coeffs[FILTER_TAPS] = {
    0.000813f, 0.004002f, 0.013722f, 0.033895f, 0.064417f, 0.100105f, 0.132060f, 0.150986f,
    0.150986f, 0.132060f, 0.100105f, 0.064417f, 0.033895f, 0.013722f, 0.004002f, 0.000813f,
};
#elif MODEM_RESAMPLE_RATIO == 4
#define FILTER_TAPS 8
static const float resample_coeffs[FILTER_TAPS] = {
    0.003560f, 0.038084f, 0.161032f, 0.297324f, 0.297324f, 0.161032f, 0.038084f, 0.003560f,
};
#elif MODEM_RESAMPLE_RATIO == 1
#define FILTER_TAPS 1
static const float resample_coeffs[FILTER_TAPS] = { 1.0f };
#else
#error "No decimation FIR for this MODEM_RESAMPLE_RATIO. Cut one at 4800 Hz for the new ratio; do not reuse a filter designed for a different one."
#endif

#if FILTER_TAPS > 1
// The last FILTER_TAPS - 1 raw samples of the previous block, prepended
// ahead of this one so the filter has real data to draw on instead of
// zeros at the join. A decimator fed block-by-block must carry this
// history between calls.
static float s_resampleTail[FILTER_TAPS - 1];
#endif

// The decimator filters buf[] into the front of buf[] itself: output i lands in
// buf[i] while the taps read the window that ends at buf[i *
// MODEM_RESAMPLE_RATIO]. Almost none of that overlaps, and the proof is worth
// spelling out because it is what keeps a whole block out of .bss.
//
// The samples output i reads from buf[] sit at indices
// [i * MODEM_RESAMPLE_RATIO - (FILTER_TAPS - 1) .. i * MODEM_RESAMPLE_RATIO].
// Take one such index r with r >= FILTER_TAPS - 1 and suppose the loop had
// already overwritten it, that is r < i. The lower bound with
// MODEM_RESAMPLE_RATIO >= 2 gives r >= 2i - (FILTER_TAPS - 1), and r < i gives
// 2i > 2r, so r > 2r - (FILTER_TAPS - 1), i.e. r < FILTER_TAPS - 1 - which
// contradicts the assumption. So every read that CAN land on an already-written
// slot lies below index FILTER_TAPS - 1, and there are at most FILTER_TAPS - 1
// of them: at ratio 8 they are the reads of buf[0] and buf[1] taken while
// computing outputs 1 and 2.
//
// The filter therefore needs two short runs of history and no full-block copy:
// the previous block's tail (s_resampleTail) and this block's own leading
// FILTER_TAPS - 1 samples. They are staged together in hist[] so that the tap
// index k walks the conceptual [ previous tail ][ this block ] sequence with a
// single bound test; everything from index FILTER_TAPS - 1 upwards is read
// straight out of buf[], still raw.
_Static_assert(MODEM_RESAMPLE_RATIO >= 2 || FILTER_TAPS == 1,
               "In-place decimation needs MODEM_RESAMPLE_RATIO >= 2: at ratio 1 a multi-tap filter reads its own output.");
_Static_assert(MODEM_BLOCK_SIZE >= 2 * (FILTER_TAPS - 1), "MODEM_BLOCK_SIZE must hold the leading and trailing history runs without them overlapping.");

static void resample_audio(float *buf) {
#if FILTER_TAPS > 1
    // [ previous block's tail ][ this block's first FILTER_TAPS - 1 samples ]
    float hist[2 * (FILTER_TAPS - 1)];
    memcpy(hist, s_resampleTail, sizeof(s_resampleTail));
    memcpy(hist + (FILTER_TAPS - 1), buf, (FILTER_TAPS - 1) * sizeof(float));

    // Take this block's own tail now, before the decimation loop runs. The
    // loop only ever writes the first MODEM_BLOCK_SIZE / MODEM_RESAMPLE_RATIO
    // slots and so cannot reach these, but reading them up front means the
    // tail no longer depends on where the write pointer stops.
    for (int t = 0; t < FILTER_TAPS - 1; t++)
        s_resampleTail[t] = buf[MODEM_BLOCK_SIZE - (FILTER_TAPS - 1) + t];
#endif

    for (int i = 0; i < MODEM_BLOCK_SIZE / MODEM_RESAMPLE_RATIO; i++) {
        float sum = 0;
        for (int j = 0; j < FILTER_TAPS; j++) {
#if FILTER_TAPS > 1
            // k indexes the conceptual [ tail ][ block ] sequence; hist[] holds
            // its first 2 * (FILTER_TAPS - 1) entries.
            int k = i * MODEM_RESAMPLE_RATIO + j;
            float in = (k < 2 * (FILTER_TAPS - 1)) ? hist[k] : buf[k - (FILTER_TAPS - 1)];
#else
            float in = buf[i * MODEM_RESAMPLE_RATIO + j]; // MODEM_RESAMPLE_RATIO == 1, reads and writes the same slot
#endif
            sum += in * resample_coeffs[j];
        }
        buf[i] = sum;
    }
}

// ------------------------------------------------------------------
// LEDs / PTT
// ------------------------------------------------------------------

static uint8_t r_old = 0, g_old = 0, b_old = 0;
static int64_t rgbTimeout = 0;

// PTT pin is a fixed compile-time constant (::MODEM_PTT_GPIO, an internal
// radiomodem feature - like ::MODEM_ADC_GPIO / ::MODEM_DAC_GPIO it is not
// runtime/web-admin selectable). Its active level is likewise fixed at
// compile time (::MODEM_PTT_ACTIVE_HIGH) and is not a user-facing polarity
// toggle - AFSK_setPttActiveHigh() is only ever called with that same
// compile-time value, from modem_init()/modem_set_modem(), including once
// live per Radio-page Save (aprs_service_apply_modem_config()) as part of
// re-applying the whole modem config.
//
// s_pttActiveHigh is read from setPtt(), which runs from the DAC timer ISR
// (IRAM_ATTR, fires continuously during TX) - so any change to it from task
// context must be made atomic with respect to that ISR. s_pttMux guards
// exactly that variable; it must never be held around gpio_set_level(), which
// is not guaranteed ISR-safe on this target.
static const int8_t s_pttGpio = MODEM_PTT_GPIO;
static bool s_pttActiveHigh = MODEM_PTT_ACTIVE_HIGH ? true : false;
static portMUX_TYPE s_pttMux = portMUX_INITIALIZER_UNLOCKED;

bool afsk_gpio_is_output_capable(int8_t gpio) {
    if (gpio == -1)
        return true; // "disabled" is always accepted
    if (gpio < 0 || gpio > 39)
        return false;
    if (gpio >= 34 && gpio <= 39)
        return false; // input-only, cannot drive an output
    if (gpio >= 6 && gpio <= 11)
        return false; // internal SPI flash/PSRAM, not brought out
    return true;
}

bool afsk_ptt_gpio_is_valid(int8_t gpio) {
    if (gpio == -1)
        return true; // "disabled" is always accepted
    if (!afsk_gpio_is_output_capable(gpio))
        return false;
    if (gpio == MODEM_ADC_GPIO || gpio == MODEM_DAC_GPIO)
        return false; // already used by the audio front end
    return true;
}

void AFSK_setPttActiveHigh(bool active_high) {
    // setPtt() (IRAM ISR, driven by the DAC timer during TX) reads
    // s_pttActiveHigh on every bit and calls gpio_set_level() on the fixed
    // s_pttGpio pin - it does not know a Save is in progress. The pin itself
    // never changes (it is the compile-time ::MODEM_PTT_GPIO constant), so
    // there is no pin swap to sequence here: only the polarity is updated,
    // under the same spinlock setPtt() uses.
    //
    // The idle level is re-driven here only when nothing is transmitting. The
    // DAC-timer ISR already owns the pin throughout TX and re-asserts the
    // correct level (active or idle, per the current s_pttActiveHigh) on every
    // sample via setPtt(), so while TX is active the write is skipped and any
    // change is applied by setPtt() itself on the very next bit.
    portENTER_CRITICAL(&s_pttMux);
    s_pttActiveHigh = active_high;
    portEXIT_CRITICAL(&s_pttMux);

    if (s_inited && s_pttGpio >= 0 && !getTransmit())
        gpio_set_level((gpio_num_t)s_pttGpio, active_high ? 0 : 1);
}

// IRAM_ATTR: reachable from dac_timer_isr() (IRAM ISR) via setPtt(), which
// runs setPtt(false) directly from Ax25GetTxBit() at key-down. If this ever
// executes while a concurrent flash op (e.g. LittleFS reading bulletins.json)
// has cache disabled, a flash-resident function here panics with
// "Cache disabled but cached memory region accessed".
void IRAM_ATTR LED_Status2(uint8_t r, uint8_t g, uint8_t b) {
    int64_t now = esp_timer_get_time() / 1000;

    if (r == r_old && g == g_old && b == b_old) {
        // Same state as last time: nothing to drive, just keep pushing the
        // "quiet" window out so a burst of identical calls (e.g. once per
        // transmitted bit) does not repeatedly touch the GPIOs. This must
        // NOT gate genuine transitions below - see the note there.
        rgbTimeout = now + 100;
        return;
    }

    // A genuine state change (this is what setPtt() uses to reflect PTT
    // on/off). A real transition is always applied: packets are transmitted
    // one at a time with a real PTT-off in between (see Ax25TransmitCheck()),
    // and back-to-back packets in a burst can toggle PTT on/off/on again well
    // within 100 ms, so this LED (and anything wired the same way, e.g. a PTT
    // relay) must follow the real GPIO, which is driven separately by
    // setPtt(). Only the 100 ms window before the NEXT transition is rate-
    // limited, and only for repeats of this same new state.
    rgbTimeout = now + 100;
    r_old = r;
    g_old = g;
    b_old = b;

#if MODEM_LED_TX_GPIO >= 0
    gpio_ll_set_level(&GPIO, (uint32_t)MODEM_LED_TX_GPIO, r > 0);
#endif
#if MODEM_LED_RX_GPIO >= 0
    gpio_ll_set_level(&GPIO, (uint32_t)MODEM_LED_RX_GPIO, g > 0);
#endif
}

// IRAM_ATTR: called from dac_timer_isr() -> MODEM_BAUDRATE_TIMER_HANDLER() ->
// Ax25GetTxBit() at key-down (setPtt(false)), same ISR context as the rest of
// the TX path. Must stay flash-cache-safe for the same reason as
// LED_Status2() above.
void IRAM_ATTR setPtt(bool state) {
    // s_pttGpio is a compile-time constant (::MODEM_PTT_GPIO), so only the
    // polarity needs to be snapshotted under the spinlock
    // AFSK_setPttActiveHigh() uses - short enough to be fine from IRAM/ISR
    // context, and it can never observe a half-applied change.
    portENTER_CRITICAL_ISR(&s_pttMux);
    bool activeHigh = s_pttActiveHigh;
    portEXIT_CRITICAL_ISR(&s_pttMux);

    if (s_pttGpio >= 0)
        gpio_ll_set_level(&GPIO, (uint32_t)s_pttGpio, activeHigh ? state : !state);
    if (state)
        LED_Status2(255, 0, 0);
    else
        LED_Status2(0, 0, 0);
}

bool getTransmit(void) {
    return s_txActive;
}

bool AFSK_TxTeardownPending(void) {
    return s_txStopPending;
}

// @brief Key up / key down.
//
// setTransmit(true) is only ever reached from ModemTransmitStart(), i.e. task
// context, so the timer may be started here. setTransmit(false) is reached from
// the DAC ISR via Ax25GetTxBit() -> ModemTransmitStop(), so it must do nothing
// more than lower a flag; AFSK_ServiceTx() finishes the job from a task.
void IRAM_ATTR setTransmit(bool val) {
    if (val) {
        if (!s_txActive) {
            s_txStopPending = false;
            s_txActive = true;
            if (s_dacTimer && !s_dacTimerRunning) {
                gptimer_start(s_dacTimer);
                s_dacTimerRunning = true;
            }
        }
    } else {
        s_txActive = false;
        // Drop the PTT GPIO right here, at key-down. This is the only teardown
        // step that is safe to run from the DAC ISR: setPtt() is IRAM_ATTR and
        // does nothing heavier than a spinlock-guarded gpio_set_level() (the
        // status LED it also touches is compiled out when MODEM_LED_*_GPIO are
        // -1). The rest of the teardown - stopping the DAC sample timer,
        // parking the DAC, flushing the RX FIFO - is NOT ISR-safe and stays
        // deferred to AFSK_ServiceTx() via s_txStopPending below.
        //
        // The pin drop must NOT be deferred with it: AFSK_ServiceTx() runs at
        // most once per service-task period (>= 1 tick) and behind an unbounded
        // RX-drain loop, so deferring the pin would leave the PTT line
        // physically asserted for up to a whole tick after the transmitter had
        // already stopped modulating (s_txActive == false), letting "PTT OFF
        // (unkeyed)" be reached while the pin was still keyed. Releasing it
        // here keeps the physical PTT in lock-step with key-down. The
        // idempotent setPtt(false) in AFSK_ServiceTx() re-asserts the idle
        // level as a belt-and-suspenders measure.
        setPtt(false);
        s_txStopPending = true;
    }
}

void afskSetFullDuplex(bool enable) {
    s_fullDuplex = enable;
    Ax25Config.fullDuplex = enable ? 1 : 0;
}

uint16_t afskGetRms(void) {
    return (uint16_t)s_mVrms;
}

uint32_t afskGetAdcSampleCount(void) {
    return s_adcSamples;
}

int afskGetDcOffset(void) {
    return s_offset;
}

float afskGetAgcGain(void) {
    return s_agcGain;
}

float afskGetDacAlarmRate(void) {
    return s_dacAlarmRateHz;
}

int afskDiagCaptureRaw(int16_t *dst, int n, uint32_t timeout_ms) {
    if ((dst == NULL) || (n <= 0))
        return 0;
    if (n > AFSK_DIAG_CAP_SAMPLES)
        n = AFSK_DIAG_CAP_SAMPLES;

    s_capRawIdx = 0;
    s_capRawLen = n; // arms the tap

    uint32_t waited = 0;
    while ((s_capRawIdx < n) && (waited < timeout_ms)) {
        vTaskDelay(pdMS_TO_TICKS(5) ? pdMS_TO_TICKS(5) : 1);
        waited += 5;
    }

    // Disarm first, then let one FIFO drain pass go by before reading the
    // sample count, so a producer already past its bounds test has finished
    // its store and the copy below sees a settled buffer.
    s_capRawLen = 0;
    vTaskDelay(pdMS_TO_TICKS(5) ? pdMS_TO_TICKS(5) : 1);

    int got = s_capRawIdx;
    if (got > n)
        got = n;
    if (got > 0)
        memcpy(dst, s_capBuf, (size_t)got * sizeof(s_capBuf[0]));
    return got;
}

// ------------------------------------------------------------------
// DAC timer ISR
// ------------------------------------------------------------------

// Why the sample ISR writes the DAC register directly.
//
// dac_oneshot_output_voltage() is ISR safe in the sense that matters for
// correctness - a NULL check and one register write inside
// DAC_RTC_ENTER_CRITICAL_SAFE(), no logging - but it lives in FLASH. Every call
// from this ISR is a potential instruction-cache miss, and an XIP fetch off the
// SPI flash costs tens of microseconds. That is not a rate error: the GPTimer
// alarm is scheduled on an absolute count, so a late ISR emits one sample late
// and the next is back on schedule. It is pure jitter on the transmitted edge,
// which is why stage 2 reports "missed alarms +0.00 %" while also reporting a
// 38 us worst-case gap against a 26.0 us period - about 12 us of excess.
//
// 12 us is 1.4 % of a Bell202 symbol and the AFSK profiles never notice it. It
// is 12 % of a G3RUH symbol (104 us, four DAC samples), and a symbol edge that
// moves by 12 % of a symbol is a bit error waiting for the descrambler to
// triple it. Host simulation puts the cost at roughly one frame in eight at 12
// us, against zero for every AFSK profile.
//
// dac_ll_update_output_value() is a static inline in a HAL header, so it
// compiles into this IRAM_ATTR ISR and the flash round trip disappears. Only
// this ISR touches the DAC while the timer is running - AFSK_ServiceTx() writes
// the idle code only after TX has stopped - so dropping the driver's critical
// section here does not race anything.
#if CONFIG_IDF_TARGET_ESP32
#include "hal/dac_ll.h"
static inline void IRAM_ATTR dac_write_isr(uint8_t code) {
    dac_ll_update_output_value(MODEM_DAC_CHANNEL, code);
}
#else
static inline void dac_write_isr(uint8_t code) {
    dac_oneshot_output_voltage(s_dac, code);
}
#endif

static bool IRAM_ATTR dac_timer_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
    (void)timer;
    (void)edata;
    (void)user_ctx;

    if (s_txActive) {
        uint8_t sinwave = MODEM_BAUDRATE_TIMER_HANDLER();
        dac_write_isr(dac_scale(sinwave));
    }
    return false;
}

// @brief Deferred TX teardown. Must run in task context.
void AFSK_ServiceTx(void) {
    if (!s_txStopPending)
        return;

    // s_txStopPending is cleared only after the teardown work below (and
    // setPtt(false) in particular) has run. Every reader of "is teardown
    // done" - ModemTxTeardownPending() / AFSK_TxTeardownPending(), and
    // therefore pollTxEvents()'s "PTT OFF (unkeyed)" log gate in ax25.c, plus
    // any getTransmit()-style guard - takes s_txStopPending == false as proof
    // the PTT GPIO has already been released. Clearing the flag last means
    // nothing can observe "teardown done" before the pin is genuinely idle.

    if (s_dacTimer && s_dacTimerRunning) {
        gptimer_stop(s_dacTimer);
        s_dacTimerRunning = false;
    }
    if (s_dac)
        dac_oneshot_output_voltage(s_dac, DAC_MID); // park the output at mid-scale

    // Half duplex only: the samples captured while we were keyed up are our own
    // transmission leaking back in, so throw them away. In full duplex the tail
    // of our own frame is still in the FIFO and must be demodulated - that is
    // exactly what the GPIO ADC -> GPIO DAC loopback test relies on.
    if (!s_fullDuplex)
        AFSK_FlushFifo();

    setPtt(false);
    s_txStopPending = false; // only now is teardown actually complete
    ESP_LOGD(TAG, "TX stopped");
}

// ------------------------------------------------------------------
// ADC
// ------------------------------------------------------------------

// @brief ADC conversion-done callback.
//
// This runs in interrupt context and is therefore allowed to do exactly one
// thing: count. Every microsecond spent here is a microsecond the DAC timer may
// not get, and the modulator cannot survive a blackout longer than about 100 us
// (see the ring buffer notes at the top of this file). Copying the samples out
// is left to afsk_rx_task, which runs at task level.
//
// The count is kept here rather than in adc_ingest() on purpose: it must report
// the true hardware rate to modem_measure_adc_rate() even if the DSP task
// falls behind and the driver drops a frame from the pool.
static bool IRAM_ATTR adc_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data) {
    (void)handle;
    (void)user_data;
    s_adcSamples += edata->size / SOC_ADC_DIGI_RESULT_BYTES;
    return false;
}

// The ESP32 hands back the two results in each 32-bit DMA word in the wrong
// order, and the FIFO must undo it.
//
// On this chip the ADC is clocked through I2S0, which packs two 16-bit results
// into one 32-bit DMA word and writes them out low half last. The sample stream
// therefore arrives with every adjacent PAIR exchanged: t1 t0 t3 t2 t5 t4. It is
// a fixed, known reordering, not a rate error.
//
// The three AFSK profiles are immune to it. They are demodulated at 9600 Hz, so
// the MODEM_ADC_SAMPLERATE -> 9600 decimation FIR averages each swapped pair
// back together before the correlator ever sees it: exchanging two samples
// inside a filter window does not change the window's output by anything the
// correlator can measure.
//
// G3RUH is not decimated - it cannot be, its own bandwidth IS the anti-alias
// cutoff - so it is handed the raw stream, MODEM_ADC_SAMPLERATE / 9600 samples
// per symbol, with nothing in between to average the swap away. Whether that is survivable
// depends on where the DMA word boundary happens to fall relative to the symbol
// boundary:
//
//   pairs inside a symbol   [t0 t1][t2 t3]  -> harmless, both samples belong
//                                              to the same symbol
//   pairs across a symbol   t0[t1 t2][t3 t4] -> fatal: the last sample of one
//                                              symbol and the first of the next
//                                              trade places, so every symbol
//                                              edge moves by +/-1 sample = +/-90
//                                              degrees of symbol clock, and the
//                                              DPLL is chasing an edge that
//                                              zig-zags either side of where the
//                                              symbol actually changed
//
// Nothing aligns those two boundaries. The DMA frame start bears no relation to
// the transmitter's symbol phase, and the ADC and DAC timers are independent
// and differ by a few hundredths of a percent, so the alignment slips one
// sample every few tens of milliseconds and walks through the fatal phase
// several times during a single frame.
// One burst of bad bits is enough to fail the FCS, so without the un-swap the
// G3RUH profile decodes nothing while the AFSK profiles are unaffected.
//
// Un-swap here, at ingest, so everything downstream - both demodulators, the
// AGC, the DC tracker and afskDiagCaptureRaw() - sees samples in true time
// order. Doing it here rather than in the G3RUH branch keeps stage 3 of the
// diagnostics honest: it measures this FIFO's output, so if the un-swap is ever
// wrong or is skipped on a future target, stage 3 reports "pair-swapped"
// instead of leaving G3RUH to fail silently.
//
// ESP32 only. The S2/S3 ADC does not go through I2S and returns 4-byte results
// in order.
#if CONFIG_IDF_TARGET_ESP32
#define MODEM_ADC_DMA_PAIR_SWAP 1
#else
#define MODEM_ADC_DMA_PAIR_SWAP 0
#endif

// @brief Parse one conversion frame into the sample FIFO. Task context.
#if MODEM_ADC_DMA_PAIR_SWAP
// adc_continuous_read() may hand back a partial frame (see the reassembly
// note above afsk_rx_task), which can make `entries` below odd. A leftover
// last entry is not passed through on its own: its real word partner is the
// FIRST entry of the NEXT call, which has not arrived yet. It is held in
// s_pendingSwap and paired with that entry when the next read comes in, so
// the un-swap always follows the true DMA word boundary across reads instead
// of slipping by one sample on an odd-length read.
static bool s_havePendingSwap = false;
static adc_digi_output_data_t s_pendingSwap;
#endif

// Feed one raw sample to the diagnostic tap. The index is read once into a
// local so the bounds test and the store use the same value, and the bound is
// s_capRawLen, which never exceeds AFSK_DIAG_CAP_SAMPLES: the store is in range
// even if the consumer disarms the tap in between.
static inline void diag_capture_push(int16_t raw) {
    int idx = s_capRawIdx;

    if (idx < s_capRawLen) {
        s_capBuf[idx] = raw;
        s_capRawIdx = idx + 1;
    }
}

static void adc_ingest(const uint8_t *buf, uint32_t size) {
    // Half duplex: do not fill the FIFO while transmitting, otherwise the
    // garbage captured during TX corrupts the demodulator state when drained.
    if (!s_fullDuplex && s_txActive)
        return;

    uint32_t head = s_fifo.head;
    const uint32_t entries = size / SOC_ADC_DIGI_RESULT_BYTES;
    uint32_t start = 0;

#if MODEM_ADC_DMA_PAIR_SWAP
    if (s_havePendingSwap) {
        // s_pendingSwap is the LATER sample of a word whose EARLIER sample
        // is the first entry of this call (if any arrived at all). Emit
        // them in true chronological order: earlier, then later.
        if (entries > 0) {
            adc_digi_output_data_t *earlier = (adc_digi_output_data_t *)&buf[0];

            if (earlier->type1.channel == MODEM_ADC_CHANNEL && (head - s_fifo.tail) < MODEM_RX_FIFO_SIZE) {
                int16_t raw = (int16_t)earlier->type1.data;
                s_fifo.buffer[head & RB_MASK] = raw;
                s_fifo.head = ++head;
                diag_capture_push(raw);
            }
            if (s_pendingSwap.type1.channel == MODEM_ADC_CHANNEL && (head - s_fifo.tail) < MODEM_RX_FIFO_SIZE) {
                int16_t raw = (int16_t)s_pendingSwap.type1.data;
                s_fifo.buffer[head & RB_MASK] = raw;
                s_fifo.head = ++head;
                diag_capture_push(raw);
            }
            start = 1; // buf[0] already consumed above
        }
        // If entries == 0 the pending entry simply waits for the next call;
        // it is not dropped.
        s_havePendingSwap = false;
    }
#endif

    for (uint32_t i = start; i < entries; i++) {
        uint32_t n = i;

#if MODEM_ADC_DMA_PAIR_SWAP
        // rel is this entry's position measured from the last confirmed
        // word boundary (start), not from the start of the raw buffer, so
        // a partial previous call can never desync the pairing.
        uint32_t rel = i - start;

        if ((rel & 1u) == 0u) {
            if (i + 1 < entries) {
                n = i + 1;
            } else {
                // Genuinely unpaired tail: hold it for the next call rather
                // than emitting it unswapped.
                s_pendingSwap = *(const adc_digi_output_data_t *)&buf[i * SOC_ADC_DIGI_RESULT_BYTES];
                s_havePendingSwap = true;
                continue;
            }
        } else {
            n = i - 1;
        }
#endif

        adc_digi_output_data_t *p = (adc_digi_output_data_t *)&buf[n * SOC_ADC_DIGI_RESULT_BYTES];

        if (p->type1.channel != MODEM_ADC_CHANNEL)
            continue;

        if ((head - s_fifo.tail) >= MODEM_RX_FIFO_SIZE)
            continue; // overrun: drop rather than corrupt

        int16_t raw = (int16_t)p->type1.data;
        s_fifo.buffer[head & RB_MASK] = raw;
        s_fifo.head = ++head;

        diag_capture_push(raw);
    }
}

static esp_err_t adc_start_continuous(void) {
    // conv_frame_size is NOT MODEM_BLOCK_SIZE, on purpose. It is the length
    // of the memcpy the IDF's ADC ISR performs inside portENTER_CRITICAL_ISR(),
    // and therefore the length of time the DAC sample clock is masked on that
    // core - see the ring buffer notes at the top of this file. Small frames,
    // deep pool; AFSK_Poll() reassembles MODEM_BLOCK_SIZE blocks from the
    // FIFO downstream.
    uint32_t conv_frame_size = (uint32_t)MODEM_ADC_CONV_FRAME_BYTES;

    adc_continuous_handle_cfg_t handleCfg = {
        .max_store_buf_size = conv_frame_size * MODEM_ADC_POOL_FRAMES,
        .conv_frame_size = conv_frame_size,
    };
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
    handleCfg.flags.flush_pool = true;
#endif

    esp_err_t err = adc_continuous_new_handle(&handleCfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_continuous_new_handle: %s", esp_err_to_name(err));
        return err;
    }

    adc_digi_pattern_config_t pattern = {
        .atten = MODEM_ADC_ATTEN,
        .channel = MODEM_ADC_CHANNEL,
        .unit = ADC_UNIT_1,
        .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
    };

    adc_continuous_config_t cfg = {
        .sample_freq_hz = (uint32_t)MODEM_ADC_SAMPLERATE * MODEM_ADC_RATE_NUM / MODEM_ADC_RATE_DEN,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
        .pattern_num = 1,
        .adc_pattern = &pattern,
    };

    err = adc_continuous_config(s_adc, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_continuous_config: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "ADC1 CH%d (GPIO%d) requested %" PRIu32 " Hz -> nominal %d Hz", (int)MODEM_ADC_CHANNEL, MODEM_ADC_GPIO, cfg.sample_freq_hz,
             MODEM_ADC_SAMPLERATE);
    // Print the frame size in the unit that matters. The driver's ISR copies
    // this many bytes with interrupts masked to level 3; if the DAC clock ever
    // shares this core again, that is its worst-case freeze.
    ESP_LOGI(TAG, "ADC DMA: %d samples/frame (%d B, %.2f ms), pool %d frames (%.1f ms), ISR on core %d", MODEM_ADC_CONV_FRAME, MODEM_ADC_CONV_FRAME_BYTES,
             1000.0 * MODEM_ADC_CONV_FRAME / (double)MODEM_ADC_SAMPLERATE, MODEM_ADC_POOL_FRAMES,
             1000.0 * MODEM_ADC_CONV_FRAME * MODEM_ADC_POOL_FRAMES / (double)MODEM_ADC_SAMPLERATE, MODEM_ADC_ISR_CORE);

    adc_continuous_evt_cbs_t cbs = { .on_conv_done = adc_conv_done_cb };
    err = adc_continuous_register_event_callbacks(s_adc, &cbs, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_continuous_register_event_callbacks: %s", esp_err_to_name(err));
        return err;
    }

    adc_cali_line_fitting_config_t caliCfg = {
        .unit_id = ADC_UNIT_1,
        .atten = MODEM_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    err = adc_cali_create_scheme_line_fitting(&caliCfg, &s_cali);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_cali_create_scheme_line_fitting: %s", esp_err_to_name(err));
        return err;
    }

    return adc_continuous_start(s_adc);
}

// ------------------------------------------------------------------
// RX DSP
// ------------------------------------------------------------------

void AFSK_Poll(void) {
    int16_t adc = 0;
    int mV = 0;
    long mVsum = 0;
    int mVsumCount = 0;

    for (;;) {
        // Only the consumer may move tail, so a flush requested by another task
        // is executed here, between blocks.
        if (s_flushReq) {
            s_flushReq = false;
            s_fifo.tail = s_fifo.head;
        }

        if (rb_size(&s_fifo) < (uint32_t)MODEM_BLOCK_SIZE)
            break;

        mVsum = 0;
        mVsumCount = 0;

        // The RMS/level measurement only needs a fraction of the samples.
        int m = ((MODEM_RESAMPLE_RATIO > 1) || (ModemConfig.modem == MODEM_MODEM_G3RUH)) ? 4 : 1;

        for (int x = 0; x < MODEM_BLOCK_SIZE; x++) {
            if (!rb_pop(&s_fifo, &adc))
                break;

            // running DC average
            s_avgSum += adc - (int)s_avgBuf[s_avgIdx];
            s_avgBuf[s_avgIdx++] = (uint16_t)adc;
            if (s_avgIdx >= AVG_N)
                s_avgIdx -= AVG_N;
            s_avg = (uint16_t)(s_avgSum / AVG_N);

            int adcVal = (int)adc - (int)s_avg;

            if (x % m == 0) {
                adc_cali_raw_to_voltage(s_cali, adc, &mV);
                mV -= s_offset;
                mVsum += (long)mV * mV; // Vrms = sqrt(1/n * sum(Vi^2))
                mVsumCount++;
            }

            s_audio[x] = (float)adcVal / 2048.0f * s_agcGain;
        }

        adc_cali_raw_to_voltage(s_cali, s_avg, &s_offset);

        if (mVsumCount > 0) {
            s_mVrms = (int)sqrtf((float)(mVsum / mVsumCount));
            if (s_mVrms > 10) { // > -40 dBm
                if (s_dcdCnt < 100)
                    s_dcdCnt++;
            } else if (s_mVrms < 5) { // < -46 dBm
                if (s_dcdCnt > 0)
                    s_dcdCnt--;
            }
        }

        bool signalPresent = (s_dcdCnt > 3) || (ModemConfig.modem == MODEM_MODEM_G3RUH);

        // Track the level only while there is something to track. Adapting on
        // an idle channel would pin the gain at maximum and overdrive every
        // incoming frame.
        if (signalPresent)
            update_agc(s_audio, MODEM_BLOCK_SIZE);

        if (signalPresent) {
            // G3RUH is the one profile that is NOT decimated.
            //
            // The AFSK profiles are demodulated at MODEM_DEMOD_SAMPLERATE
            // (9600 Hz), so the MODEM_ADC_SAMPLERATE stream is low-pass
            // filtered and decimated by MODEM_RESAMPLE_RATIO first. G3RUH runs
            // at 9600 Bd: decimating to 9600 Hz would leave exactly one sample
            // per symbol, which gives the DPLL nothing to recover a clock from
            // and puts the symbol rate at Nyquist. Its demodulator is built for
            // the raw MODEM_ADC_SAMPLERATE stream instead - MODEM_RESAMPLE_RATIO
            // samples per symbol, which is what N9600 and the lpf9600
            // coefficients in modem.c are cut for. Feed it the undecimated
            // block.
            //
            // The anti-alias filter is skipped with it, deliberately: its 4800
            // Hz cutoff is G3RUH's own bandwidth, so it would take the signal
            // with it. lpf9600 does the receive filtering for this profile.
            const bool decimate = (ModemConfig.modem != MODEM_MODEM_G3RUH) && (MODEM_RESAMPLE_RATIO > 1);
            const int count = decimate ? (MODEM_BLOCK_SIZE / MODEM_RESAMPLE_RATIO) : MODEM_BLOCK_SIZE;

            if (decimate)
                resample_audio(s_audio);

            for (int i = 0; i < count; i++) {
                // demodulate() is documented for <= 13 bit input, and
                // filterRun() accumulates coeff*sample in an int32 across up to
                // 15 taps. Clamping here keeps both safe no matter what the AGC
                // or a hot input does.
                float v = s_audio[i] * 2048.0f;
                if (v > 2047.0f)
                    v = 2047.0f;
                else if (v < -2047.0f)
                    v = -2047.0f;
                MODEM_DECODE((int16_t)v, (uint16_t)s_mVrms);
            }
        }
    }
}

static void afsk_rx_task(void *arg) {
    (void)arg;

    // One conversion frame - MODEM_ADC_CONV_FRAME samples, not a whole DSP
    // block. adc_continuous_read() hands back at most one frame per call, and
    // AFSK_Poll() below only acts once MODEM_BLOCK_SIZE samples have
    // accumulated in the FIFO, so a short read costs nothing but a loop.
    // Static rather than automatic so that a conversion frame never has to be
    // carried on this task's stack.
    static uint8_t convBuf[MODEM_ADC_CONV_FRAME_BYTES];

    while (!s_rxStop) {
        uint32_t got = 0;

        // Blocks on the driver's own pool. Its ISR fills that pool with an
        // xRingbufferSendFromISR() whose critical section is bounded by
        // MODEM_ADC_CONV_FRAME rather than by the DSP block size, and runs on
        // MODEM_ADC_ISR_CORE, which is not where the DAC clock lives. Nothing
        // of ours runs at interrupt level, so however long the DSP below takes,
        // the DAC timer keeps its cadence. The 100 ms timeout is both a safety
        // net and what lets s_rxStop be noticed within a bounded time.
        esp_err_t err = adc_continuous_read(s_adc, convBuf, sizeof(convBuf), &got, ADC_READ_TIMEOUT_MS);
        if (err != ESP_OK || got == 0)
            continue;

        adc_ingest(convBuf, got);
        AFSK_Poll();
    }

    // Exit under our own power. This task blocks inside the ADC driver's ring
    // buffer, and vTaskDelete()ing a task that is blocked on a ring buffer
    // leaves that ring buffer's internal lists pointing at freed TCB memory.
    // AFSK_deinit() therefore asks this task to stop and waits, rather than
    // deleting it from the outside.
    s_rxExited = true; // AFSK_deinit() clears s_rxTask; do not race it here
    vTaskDelete(NULL);
}

// ------------------------------------------------------------------
// Modem profile
// ------------------------------------------------------------------

void afskSetModem(uint8_t val, bool flatAudio, uint16_t timeSlot, uint16_t preamble, uint8_t fx25Mode, uint16_t minUnkeyMs) {
    // afsk_rx_task runs continuously on its own (possibly pinned) core,
    // calling AFSK_Poll() -> MODEM_DECODE() -> demodulate() -> filterRun()
    // on whatever is currently in demodState[]. ModemInit() below does a
    // memset() of demodState[] followed by piecemeal reassignment of each
    // filter's coeffs/taps. If the RX task runs concurrently with that, it
    // can observe a torn/inconsistent DemodState (e.g. a stale nonzero
    // `taps` paired with a just-cleared NULL `coeffs`), which crashes
    // filterRun() with a NULL-pointer read. Suspend the RX task for the
    // duration of the reinit so no profile switch can race it.
    if (s_rxTask)
        vTaskSuspend(s_rxTask);

    ModemConfig.flatAudioIn = flatAudio ? 1 : 0;
    ModemConfig.usePWM = 1;

    // val is the modem_mode_t selector (0=AFSK300, 1=Bell202, 2=V.23, 3=G3RUH);
    // out-of-range values fall back to standard-APRS Bell202.
    ModemConfig.modem = (val <= MODEM_MODEM_G3RUH) ? (modem_mode_t)val : MODEM_MODEM_BELL202;

    ESP_LOGI(TAG, "modem=%d adcRate=%d blockSize=%d resample=%d demodRate=%d", (int)ModemConfig.modem, MODEM_ADC_SAMPLERATE, MODEM_BLOCK_SIZE,
             MODEM_RESAMPLE_RATIO, MODEM_DEMOD_SAMPLERATE);

    ModemInit();
    Ax25Init(fx25Mode);
    Ax25Config.fullDuplex = s_fullDuplex ? 1 : 0;
#ifdef ENABLE_FX25
    if (fx25Mode > 0)
        Fx25Init();
#endif
    Ax25TimeSlot(timeSlot);
    Ax25TxDelay(preamble);
    Ax25MinUnkeyTime(minUnkeyMs);

    // Reset the RX front-end so a profile change cannot leak old state.
    memset(s_avgBuf, 0, sizeof(s_avgBuf));
    s_avgIdx = 0;
    s_avgSum = 0;
    s_avg = 2048;
    s_agcGain = 1.0f;
    s_dcdCnt = 0;
    AFSK_FlushFifo();

    if (s_rxTask)
        vTaskResume(s_rxTask);
}

// ------------------------------------------------------------------
// Core-pinned bring-up
// ------------------------------------------------------------------

// esp_intr_alloc() binds an interrupt to whichever core calls it. Both the ADC
// DMA interrupt and the DAC GPTimer interrupt are allocated inside driver calls
// (adc_continuous_new_handle() and gptimer_register_event_callbacks()
// respectively), which put both of them on core 0.
//
// run_on_core() removes the accident. esp_ipc_call_blocking() would be the
// obvious tool, but CONFIG_ESP_IPC_TASK_STACK_SIZE defaults to 1024 bytes and
// these callees allocate, log and touch the interrupt tables; a throwaway
// pinned task with a real stack is both safer and easier to reason about.
//
// Teardown does not need this: esp_intr_free() detects a cross-core call and
// IPCs itself to the right core, and esp_intr_enable()/disable() go through the
// interrupt matrix for peripheral sources, which works from either core.
typedef struct {
    esp_err_t (*fn)(void);
    esp_err_t err;
    TaskHandle_t caller;
} core_call_t;

static void core_call_task(void *arg) {
    core_call_t *c = (core_call_t *)arg;

    c->err = c->fn();
    xTaskNotifyGive(c->caller);
    vTaskDelete(NULL);
}

static esp_err_t run_on_core(int core, esp_err_t (*fn)(void)) {
#if CONFIG_FREERTOS_UNICORE
    (void)core;
    return fn();
#else
    if ((core < 0) || (core >= CONFIG_FREERTOS_NUMBER_OF_CORES) || (core == xPortGetCoreID()))
        return fn(); // already there, or no preference expressed

    core_call_t call = { .fn = fn, .err = ESP_FAIL, .caller = xTaskGetCurrentTaskHandle() };

    if (xTaskCreatePinnedToCore(core_call_task, "modem_init", 4096, &call, configMAX_PRIORITIES - 2, NULL, core) != pdPASS) {
        ESP_LOGE(TAG, "cannot create the bring-up task for core %d", core);
        return ESP_ERR_NO_MEM;
    }

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    return call.err;
#endif
}

// ------------------------------------------------------------------
// Init / deinit
// ------------------------------------------------------------------

// @brief Create, configure and enable the DAC sample clock.
//
// MUST run on MODEM_DAC_TIMER_CORE: gptimer_register_event_callbacks() is
// where the interrupt is actually allocated, and esp_intr_alloc() binds to the
// calling core.
//
// Why the core matters at all, given intr_priority 3 is the highest an IDF C
// handler can have: because portENTER_CRITICAL_ISR() masks to level 3. Level 3
// is not "above" critical sections, it is exactly what they mask. Any driver
// ISR on this core that takes a spinlock silences the modulator for as long as
// it holds it, and the ADC's holds one across a whole conversion frame. A
// spinlock only raises INTLEVEL on its own core, so the cure is separation, not
// priority. Everything else about this handler - IRAM_ATTR, DRAM_ATTR tables,
// the direct dac_ll write - remains necessary; it just was not the last thing
// blocking it.
static esp_err_t dac_timer_create(void) {
    esp_err_t err;

    // The resolution stays at 10 MHz. 10000000 / 38400 = 260.4 truncates to 260
    // ticks, so the timer really runs at 38461.5 Hz, +0.16 % above the rate
    // modem.c derives its steps from. That is deliberate: 38400 Hz cannot be
    // hit exactly from the 80 MHz APB at any divider (80e6 / 38400 = 2083.33),
    // and a uniform rate error of that size is nothing - the demodulator's DPLL
    // tracks a full 1.7 % of it without dropping a frame. It is bursty stalls,
    // not rate error, that this modem dies of.
    gptimer_config_t timerCfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 10 * 1000 * 1000, // 10 MHz, 1 tick = 100 ns
        .intr_priority = MODEM_DAC_TIMER_INTR_PRIO,
    };
    err = gptimer_new_timer(&timerCfg, &s_dacTimer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gptimer_new_timer: %s", esp_err_to_name(err));
        return err;
    }

    // This call allocates the interrupt. Whatever core we are on now is the
    // core the DAC sample clock will live on for the rest of the run.
    gptimer_event_callbacks_t timerCbs = { .on_alarm = dac_timer_isr };
    err = gptimer_register_event_callbacks(s_dacTimer, &timerCbs, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gptimer_register_event_callbacks: %s", esp_err_to_name(err));
        return err;
    }

    err = gptimer_enable(s_dacTimer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gptimer_enable: %s", esp_err_to_name(err));
        return err;
    }

    gptimer_alarm_config_t alarmCfg = {
        .alarm_count = 10000000ULL / MODEM_DAC_SAMPLERATE,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    err = gptimer_set_alarm_action(s_dacTimer, &alarmCfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gptimer_set_alarm_action: %s", esp_err_to_name(err));
        return err;
    }

    s_dacTimerRunning = false;
    s_dacAlarmRateHz = 10000000.0f / (float)alarmCfg.alarm_count;

    // Print what the timer will REALLY do, not what was asked for. Stage 2 of
    // the characterisation counts ISR invocations and compares them against the
    // nominal rate, so without this line a 2 % shortfall caused by missed alarms
    // is indistinguishable from a 2 % clock error. Any gap between the measured
    // ISR rate and the alarm rate below is the ISR failing to keep up.
    ESP_LOGI(TAG, "DAC timer: %d Hz nominal -> alarm every %" PRIu64 " ticks = %.1f Hz actual, ISR prio %d on core %d", MODEM_DAC_SAMPLERATE,
             alarmCfg.alarm_count, 10000000.0 / (double)alarmCfg.alarm_count, MODEM_DAC_TIMER_INTR_PRIO, xPortGetCoreID());
    return ESP_OK;
}

esp_err_t AFSK_init(void) {
    esp_err_t err;

    if (s_inited)
        return ESP_OK;

    rb_init(&s_fifo);

    // s_pttGpio is the fixed ::MODEM_PTT_GPIO compile-time constant.
    //
    // GPIO_MODE_INPUT_OUTPUT, not GPIO_MODE_OUTPUT: on ESP32 the input path
    // (pad input buffer / FUN_IE) is a separate enable from the output
    // driver, and gpio_config(GPIO_MODE_OUTPUT) does not turn it on. With
    // only GPIO_MODE_OUTPUT the pad reads back as 0 no matter what level is
    // actually being driven, so the keying line cannot be observed on-chip at
    // all. INPUT_OUTPUT keeps the pin driven by setPtt() while leaving the
    // real pad voltage readable, which is what makes a keying fault
    // diagnosable from a debugger without a scope on the PTT line.
    if (s_pttGpio >= 0) {
        gpio_config_t pttCfg = {
            .pin_bit_mask = 1ULL << s_pttGpio,
            .mode = GPIO_MODE_INPUT_OUTPUT,
        };
        gpio_config(&pttCfg);
        gpio_set_level((gpio_num_t)s_pttGpio, s_pttActiveHigh ? 0 : 1);
    }
#if MODEM_LED_TX_GPIO >= 0
    gpio_config_t ledTxCfg = { .pin_bit_mask = 1ULL << MODEM_LED_TX_GPIO, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&ledTxCfg);
#endif
#if MODEM_LED_RX_GPIO >= 0
    gpio_config_t ledRxCfg = { .pin_bit_mask = 1ULL << MODEM_LED_RX_GPIO, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&ledRxCfg);
#endif

    // ---- DAC ----
    dac_oneshot_config_t dacCfg = { .chan_id = MODEM_DAC_CHANNEL };
    err = dac_oneshot_new_channel(&dacCfg, &s_dac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "dac_oneshot_new_channel: %s", esp_err_to_name(err));
        return err;
    }
    dac_oneshot_output_voltage(s_dac, DAC_MID);
    ESP_LOGI(TAG, "DAC on GPIO%d (channel %d)", MODEM_DAC_GPIO, (int)MODEM_DAC_CHANNEL);

    // ---- ADC ----
    // Pinned: adc_continuous_new_handle() allocates the DMA interrupt, and
    // esp_intr_alloc() binds it to the calling core. This is the core whose
    // level 3 gets masked across every conversion-frame memcpy, so it must be
    // a core the modulator is not on.
    err = run_on_core(MODEM_ADC_ISR_CORE, adc_start_continuous);
    if (err != ESP_OK)
        return err;

    // ---- DAC sample clock ----
    // Pinned to the other core. See dac_timer_create().
    err = run_on_core(MODEM_DAC_TIMER_CORE, dac_timer_create);
    if (err != ESP_OK)
        return err;

    // ---- DSP task ----
    BaseType_t ok;
    s_rxStop = false;
    s_rxExited = false;
#if MODEM_RX_TASK_CORE >= 0
    ok = xTaskCreatePinnedToCore(afsk_rx_task, "afsk_rx", MODEM_RX_TASK_STACK, NULL, MODEM_RX_TASK_PRIO, &s_rxTask, MODEM_RX_TASK_CORE);
#else
    ok = xTaskCreate(afsk_rx_task, "afsk_rx", MODEM_RX_TASK_STACK, NULL, MODEM_RX_TASK_PRIO, &s_rxTask);
#endif
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "cannot create the RX task");
        return ESP_ERR_NO_MEM;
    }

    setTransmit(false);
    s_txStopPending = false;
    s_inited = true;
    ESP_LOGI(TAG, "AFSK hardware ready (%s duplex)", s_fullDuplex ? "full" : "half");
    return ESP_OK;
}

void AFSK_deinit(void) {
    if (!s_inited)
        return;

    if (s_rxTask) {
        TaskHandle_t rx = s_rxTask;

        // Resume first: afskSetModem() may have left it suspended, and a
        // suspended task will never see s_rxStop.
        vTaskResume(rx);
        s_rxStop = true;
        for (int i = 0; (i < 40) && !s_rxExited; i++) // the read timeout is 100 ms
            vTaskDelay(pdMS_TO_TICKS(10));
        if (!s_rxExited) {
            ESP_LOGW(TAG, "RX task did not exit in 400 ms, deleting it");
            vTaskDelete(rx);
        }
        s_rxTask = NULL;
    }
    if (s_adc) {
        adc_continuous_stop(s_adc);
        adc_continuous_deinit(s_adc);
        s_adc = NULL;
    }
    if (s_cali) {
        adc_cali_delete_scheme_line_fitting(s_cali);
        s_cali = NULL;
    }
    if (s_dacTimer) {
        // No core juggling needed on the way out, unlike AFSK_init(): the
        // timer's interrupt lives on MODEM_DAC_TIMER_CORE, but esp_intr_free()
        // (reached via gptimer_del_timer) detects a cross-core call and IPCs
        // itself over, and esp_intr_disable() for a peripheral source goes
        // through the interrupt matrix, which either core may write.
        if (s_dacTimerRunning) {
            gptimer_stop(s_dacTimer);
            s_dacTimerRunning = false;
        }
        gptimer_disable(s_dacTimer);
        gptimer_del_timer(s_dacTimer);
        s_dacTimer = NULL;
    }
    if (s_dac) {
        dac_oneshot_del_channel(s_dac);
        s_dac = NULL;
    }
    s_inited = false;
    ESP_LOGI(TAG, "AFSK hardware released");
}
