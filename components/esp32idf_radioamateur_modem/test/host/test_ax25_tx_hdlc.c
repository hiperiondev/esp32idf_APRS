// @file test_ax25_tx_hdlc.c
//
// @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
// @date 2026
// @copyright GNU General Public License v3
// @see https://github.com/hiperiondev/esp32idf_APRS
//
// @brief Host regression test for the HDLC transmitter in ax25.c.
//
// The real ax25.c, crc_ccit.c and FX.25 sources are compiled for the host
// against the stand-in headers in stubs/, and random AX.25 UI frames are pushed
// through the same path the firmware uses: Ax25WriteTxFrame() queues a frame,
// Ax25TransmitCheck() keys it up and Ax25GetTxBit() is clocked until the modem
// is stopped. Every bit that comes out is checked three ways:
//
//   1. Bit-exact against a reference HDLC encoder: preamble flags, then the
//      frame and its FCS bit-stuffed as one continuous field (a run of five 1s
//      ending on the last FCS bit is followed by its stuffed 0), then flags.
//   2. Through the in-tree receiver, Ax25BitParse() + Ax25ReadNextRxFrame().
//   3. Through a strict receiver that applies the Direwolf hdlc_rec.c rules,
//      which accepts a closing flag only when exactly seven data bits have
//      accumulated since the last byte boundary.
//
// A second pass sends frames as FX.25 blocks and checks them through the
// in-tree receiver, which is the only one of the two that understands FX.25.
// While Ax25GetTxBit() is being clocked the pages holding Fx25ModeList are
// unmapped, the host equivalent of the flash cache being disabled under the
// cache-safe DAC ISR: any read of the table from the transmit bit path ends
// the run with a segmentation fault.
//
// A third pass builds frames from TNC2 text with 0 to 8 digipeaters through
// ax25_encode() and hdlcFrame(), and checks that exactly the last address
// carries the end-of-address bit, that ax25_decode() recovers every
// digipeater, and that the frame survives a key-up and the in-tree receiver.
//
// A fourth pass runs half duplex and measures how long a queued frame waits
// before keying up: with Ax25TimeSlot(0) it must key up at once, frame after
// frame, even after a non-zero time slot was set before; with a non-zero time
// slot it must wait at least that long.
//
// A fifth pass checks the end of a key-up and the key-up gate. With every tail
// length it tries, the frame must be followed by exactly one closing flag plus
// the tail's flags, the tail rounded up to whole flags. And a queued frame must
// not key up while the modulator is still running or while the previous
// key-down's deferred teardown is still owed, then key up once both clear.
//
// The run also counts how many frames ended their FCS on a run of five 1s, so
// a pass cannot come from a seed that simply never exercised that case.
//
// Exit status is 0 when every frame passed every check, 1 otherwise.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ax25.h"
#include "esp32idf_radioamateur_modem.h"
#include "modem.h"

#ifdef ENABLE_FX25
#include "fx25.h"
#endif

// Frames per pass and their size range, in bytes including the header.
#define AX25_FRAMES     20000
#define FX25_FRAMES     2000
#define FRAME_MIN_BYTES 30
#define FRAME_MAX_BYTES 180

// Preamble length used for the run. Short keeps the run fast; the value only
// sets how many flags precede the frame.
#define TEST_TXDELAY_MS 50

// Upper bound on the bits one key-up may produce before the harness gives up,
// far above the longest frame plus preamble.
#define MAX_TX_BITS 8192

// Upper bound on service ticks spent waiting for a queued frame to key up.
#define MAX_KEYUP_TICKS 16

// Time slots used by the quiet-time pass, in milliseconds, and how many frames
// each case sends.
#define QUIET_TEST_SLOT_MS 2000
#define QUIET_TEST_FRAMES  4

// Upper bound, in 1 ms service ticks, on the wait the quiet-time pass allows a
// frame before it counts it as never keyed up.
#define QUIET_TEST_MAX_TICKS 5000

// Longest wait, in milliseconds, still counted as "keyed up at once" when the
// time slot is 0. It covers the service ticks the unkey holdoff always takes.
#define QUIET_TEST_IMMEDIATE_MS 5

// Service ticks the key-up gate pass holds each blocking condition for.
#define GATE_TEST_TICKS 20

// Minimum HDLC frame length (addresses + control + PID + FCS) the strict
// receiver accepts, as in Direwolf.
#define STRICT_MIN_FRAME_LEN 17

// Size of the strict receiver's frame buffer.
#define STRICT_FRAME_BUF 512

// ---------------------------------------------------------------------------
// Platform stand-ins used by ax25.c.
// ---------------------------------------------------------------------------

static uint32_t s_rngState = 0x12345678u;
static int64_t s_nowUs = 0;
static bool s_transmitting = false;
// The deferred teardown a key-down leaves for AFSK_ServiceTx(), raised by
// ModemTransmitStop() and cleared by serviceTick(), as on the target.
static bool s_teardownPending = false;
// Makes getTransmit() report a running modulator without a key-up, so the
// gate pass can hold the transmitter busy from outside.
static bool s_forceActive = false;

// xorshift32: reproducible from the seed and independent of the host libc.
static uint32_t rng32(void) {
    uint32_t x = s_rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rngState = x;
    return x;
}

uint32_t esp_random(void) {
    return rng32();
}

int64_t esp_timer_get_time(void) {
    return s_nowUs;
}

void ModemTransmitStart(void) {
    s_transmitting = true;
}

void ModemTransmitStop(void) {
    s_teardownPending = true;
    s_transmitting = false;
}

bool ModemTxTeardownPending(void) {
    return s_teardownPending;
}

bool getTransmit(void) {
    return s_transmitting || s_forceActive;
}

uint8_t ModemDcdState(void) {
    return 0;
}

float ModemGetBaudrate(void) {
    return 1200.f;
}

uint8_t ModemGetDemodulatorCount(void) {
    return 1;
}

int8_t ModemGetTwistDb(uint8_t modem) {
    (void)modem;
    return 0;
}

void ModemGetSignalLevel(uint8_t modem, int8_t *peak, int8_t *valley, uint8_t *level) {
    (void)modem;
    *peak = 0;
    *valley = 0;
    *level = 0;
}

void modem_format_tnc2(const ax25_msg_t *msg, char *out, size_t out_len) {
    (void)msg;
    if (out_len > 0)
        out[0] = '\0';
}

// ---------------------------------------------------------------------------
// Reference encoder.
// ---------------------------------------------------------------------------

// CRC-16/X.25 over a byte buffer, LSB first; returns the FCS as sent on air.
static uint16_t refFcs(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0x8408) : (uint16_t)(crc >> 1);
    }
    return (uint16_t)(crc ^ 0xFFFF);
}

// Bit-stuff a frame and its FCS as one field, LSB first. Returns the bit
// count, and reports through lastRunOwed whether the field ended on a run of
// five 1s (and therefore on a stuffed 0).
static size_t refStuff(const uint8_t *frame, size_t len, uint8_t *bits, size_t cap, bool *lastRunOwed) {
    uint8_t field[STRICT_FRAME_BUF];
    memcpy(field, frame, len);
    uint16_t fcs = refFcs(frame, len);
    field[len] = (uint8_t)(fcs & 0xFF);
    field[len + 1] = (uint8_t)(fcs >> 8);

    size_t n = 0;
    int ones = 0;
    *lastRunOwed = false;
    for (size_t i = 0; i < len + 2; i++) {
        for (int k = 0; k < 8; k++) {
            uint8_t b = (field[i] >> k) & 1;
            if (n < cap)
                bits[n] = b;
            n++;
            ones = b ? ones + 1 : 0;
            if (ones == 5) {
                if (n < cap)
                    bits[n] = 0;
                n++;
                ones = 0;
                *lastRunOwed = (i == len + 1) && (k == 7);
            }
        }
    }
    return n;
}

// ---------------------------------------------------------------------------
// Strict receiver, following the Direwolf hdlc_rec.c rules.
// ---------------------------------------------------------------------------

struct StrictRx {
    uint8_t patDet;
    int olen; // data bits since the last byte boundary; -1 outside a frame
    uint8_t oacc;
    uint8_t frame[STRICT_FRAME_BUF];
    size_t frameLen;
    bool gotFrame;
    uint8_t out[STRICT_FRAME_BUF];
    size_t outLen;
    int closingBits; // olen seen at the last closing flag of a long-enough frame
};

static void strictReset(struct StrictRx *s) {
    memset(s, 0, sizeof(*s));
    s->olen = -1;
    s->closingBits = -1;
}

static void strictBit(struct StrictRx *s, uint8_t bit) {
    s->patDet >>= 1;
    if (bit)
        s->patDet |= 0x80;

    if (s->patDet == 0x7E) {
        if (s->frameLen >= STRICT_MIN_FRAME_LEN) {
            s->closingBits = s->olen;
            if (s->olen == 7) {
                uint16_t fcs = refFcs(s->frame, s->frameLen - 2);
                if ((s->frame[s->frameLen - 2] == (fcs & 0xFF)) && (s->frame[s->frameLen - 1] == (fcs >> 8))) {
                    memcpy(s->out, s->frame, s->frameLen - 2);
                    s->outLen = s->frameLen - 2;
                    s->gotFrame = true;
                }
            }
        }
        s->olen = 0;
        s->frameLen = 0;
        return;
    }
    if (s->patDet == 0xFE) { // seven 1s: abort
        s->olen = -1;
        s->frameLen = 0;
        return;
    }
    if ((s->patDet & 0xFC) == 0x7C) // stuffed 0 after five 1s
        return;
    if (s->olen < 0)
        return;

    s->oacc >>= 1;
    if (bit)
        s->oacc |= 0x80;
    if (++s->olen == 8) {
        if (s->frameLen < sizeof(s->frame))
            s->frame[s->frameLen++] = s->oacc;
        s->olen = 0;
    }
}

// ---------------------------------------------------------------------------
// Frame generation and transmission.
// ---------------------------------------------------------------------------

static void putCall(uint8_t *p, bool last) {
    for (int i = 0; i < 6; i++)
        p[i] = (uint8_t)(('A' + (int)(rng32() % 26)) << 1);
    p[6] = (uint8_t)(0x60 | ((rng32() % 16) << 1) | (last ? 1 : 0));
}

// Build a UI frame with a random header (destination, source, 0-2
// repeaters) and a random information field covering every byte value.
static size_t makeFrame(uint8_t *f) {
    size_t len = FRAME_MIN_BYTES + (rng32() % (FRAME_MAX_BYTES - FRAME_MIN_BYTES + 1));
    int rpt = (int)(rng32() % 3);
    size_t idx = 0;
    putCall(&f[idx], false);
    idx += 7;
    putCall(&f[idx], rpt == 0);
    idx += 7;
    for (int r = 0; r < rpt; r++) {
        putCall(&f[idx], r == rpt - 1);
        idx += 7;
    }
    f[idx++] = 0x03; // UI
    f[idx++] = 0xF0; // no layer 3
    while (idx < len)
        f[idx++] = (uint8_t)rng32();
    return len;
}

// ---------------------------------------------------------------------------
// Flash-cache simulation.
// ---------------------------------------------------------------------------

// On the target Fx25ModeList is in flash, and Ax25GetTxBit() runs in a DAC ISR
// that keeps running while a flash write has the cache disabled. Here the
// pages that hold the table are made inaccessible for as long as the bit path
// is clocked, so a read of the table from that path faults as it would on the
// target.
static void flashCacheSet(bool enabled) {
#ifdef ENABLE_FX25
    long page = sysconf(_SC_PAGESIZE);
    uintptr_t first = (uintptr_t)&Fx25ModeList[0] & ~(uintptr_t)(page - 1);
    uintptr_t last = ((uintptr_t)&Fx25ModeList[0] + sizeof(Fx25ModeList) - 1) & ~(uintptr_t)(page - 1);
    if (mprotect((void *)first, (size_t)(last - first) + (size_t)page, enabled ? PROT_READ : PROT_NONE) != 0) {
        perror("mprotect");
        exit(2);
    }
#else
    (void)enabled;
#endif
}

// One pass of the modem service loop: the deferred teardown, then the key-up
// state machine.
static void serviceTick(void) {
    s_teardownPending = false;
    Ax25TransmitCheck();
}

// Queue one frame, key it up and clock it out. Returns the bit count, or 0 if
// the frame never keyed up or never finished.
static size_t transmitFrame(const uint8_t *frame, size_t len, uint8_t *bits, size_t cap) {
    if (Ax25WriteTxFrame(frame, (uint16_t)len) == NULL)
        return 0;

    for (int t = 0; (t < MAX_KEYUP_TICKS) && !s_transmitting; t++) {
        s_nowUs += 10000;
        serviceTick();
    }
    if (!s_transmitting)
        return 0;

    // Everything from here to the key-down is ISR context on the target.
    flashCacheSet(false);
    size_t n = 0;
    while (s_transmitting && (n < cap)) {
        uint8_t b = Ax25GetTxBit();
        if (!s_transmitting) // the call that stopped the modem carries no bit
            break;
        bits[n++] = b;
    }
    flashCacheSet(true);
    if (s_transmitting)
        return 0;

    // Let the service side observe the key-down before the next frame.
    s_nowUs += 10000;
    serviceTick();
    return n;
}

// Strip whole leading and trailing flags from an emitted key-up. A key-up
// opens with one idle octet of 0 bits before the first preamble flag, which is
// skipped too. The number of trailing flags is stored in *trailOut when it is
// not NULL. Returns false unless at least one flag sits on each side.
static bool stripFlags(const uint8_t *bits, size_t n, size_t *start, size_t *end, int *trailOut) {
    static const uint8_t flag[8] = { 0, 1, 1, 1, 1, 1, 1, 0 };
    size_t s = 0;
    size_t e = n;
    int lead = 0;
    int trail = 0;
    while ((s < 8) && (s < n) && (bits[s] == 0) && ((n - s < 8) || (memcmp(&bits[s], flag, 8) != 0)))
        s++;
    while ((e - s >= 8) && (memcmp(&bits[s], flag, 8) == 0)) {
        s += 8;
        lead++;
    }
    while ((e - s >= 8) && (memcmp(&bits[e - 8], flag, 8) == 0)) {
        e -= 8;
        trail++;
    }
    *start = s;
    *end = e;
    if (trailOut != NULL)
        *trailOut = trail;
    return (lead > 0) && (trail > 0);
}

static bool readInTree(const uint8_t *frame, size_t len) {
    uint8_t *rx = NULL;
    uint16_t rxLen = 0;
    struct Ax25RxMeta meta;
    bool ok = Ax25ReadNextRxFrame(&rx, &rxLen, &meta) && (rxLen == len) && (memcmp(rx, frame, len) == 0);
    while (Ax25ReadNextRxFrame(&rx, &rxLen, &meta)) // never leave a stale frame for the next check
        ok = false;
    return ok;
}

static int runAx25Pass(void) {
    static uint8_t frame[STRICT_FRAME_BUF];
    static uint8_t bits[MAX_TX_BITS];
    static uint8_t ref[MAX_TX_BITS];
    static struct StrictRx strict;

    Ax25Config.fullDuplex = 1;
    Ax25Init(0);
    Ax25TxDelay(TEST_TXDELAY_MS);

    unsigned txFail = 0;
    unsigned bitMismatch = 0;
    unsigned inTreeLost = 0;
    unsigned strictLost = 0;
    unsigned owedCases = 0;

    for (unsigned i = 0; i < AX25_FRAMES; i++) {
        size_t len = makeFrame(frame);
        size_t n = transmitFrame(frame, len, bits, sizeof(bits));
        if (n == 0) {
            txFail++;
            continue;
        }

        bool owed = false;
        size_t refLen = refStuff(frame, len, ref, sizeof(ref), &owed);
        if (owed)
            owedCases++;

        size_t s = 0;
        size_t e = 0;
        if (!stripFlags(bits, n, &s, &e, NULL) || (e - s != refLen) || (memcmp(&bits[s], ref, refLen) != 0)) {
            if (bitMismatch < 5)
                fprintf(stderr, "AX.25 frame %u: emitted field is not the reference HDLC encoding (owed=%d)\n", i, owed);
            bitMismatch++;
        }

        for (size_t k = 0; k < n; k++)
            Ax25BitParse(bits[k], 0, 0);
        if (!readInTree(frame, len))
            inTreeLost++;

        strictReset(&strict);
        for (size_t k = 0; k < n; k++)
            strictBit(&strict, bits[k]);
        if (!strict.gotFrame || (strict.outLen != len) || (memcmp(strict.out, frame, len) != 0)) {
            if (strictLost < 5)
                fprintf(stderr, "AX.25 frame %u: strict receiver rejected it (%d data bits at the closing flag)\n", i, strict.closingBits);
            strictLost++;
        }
    }

    printf("AX.25: %u frames, %u ended the FCS on five 1s\n", AX25_FRAMES, owedCases);
    printf("  key-up failures          : %u\n", txFail);
    printf("  bitstream mismatches     : %u\n", bitMismatch);
    printf("  in-tree receiver losses  : %u\n", inTreeLost);
    printf("  strict receiver losses   : %u\n", strictLost);

    int failed = (txFail + bitMismatch + inTreeLost + strictLost) != 0;
    if (owedCases == 0) {
        fprintf(stderr, "AX.25: no frame exercised a run of five 1s at the end of the FCS\n");
        failed = 1;
    }
    return failed;
}

static int runFx25Pass(void) {
#ifdef ENABLE_FX25
    static uint8_t frame[STRICT_FRAME_BUF];
    static uint8_t bits[MAX_TX_BITS];

    Ax25Config.fullDuplex = 1;
    Ax25Init(2);
    Ax25TxDelay(TEST_TXDELAY_MS);

    unsigned txFail = 0;
    unsigned inTreeLost = 0;

    for (unsigned i = 0; i < FX25_FRAMES; i++) {
        size_t len = makeFrame(frame);
        size_t n = transmitFrame(frame, len, bits, sizeof(bits));
        if (n == 0) {
            txFail++;
            continue;
        }
        for (size_t k = 0; k < n; k++)
            Ax25BitParse(bits[k], 0, 0);
        // Flush the receiver with a few flags so the block is fully handled.
        for (int f = 0; f < 4; f++)
            for (int k = 0; k < 8; k++)
                Ax25BitParse((uint8_t)((0x7E >> k) & 1), 0, 0);
        if (!readInTree(frame, len))
            inTreeLost++;
    }

    printf("FX.25: %u frames\n", FX25_FRAMES);
    printf("  key-up failures          : %u\n", txFail);
    printf("  in-tree receiver losses  : %u\n", inTreeLost);
    return (txFail + inTreeLost) != 0;
#else
    printf("FX.25: not built (ENABLE_FX25 undefined)\n");
    return 0;
#endif
}

// Check the address field of a raw frame built for @p digis digipeaters:
// exactly the last address carries the end-of-address bit, and the control
// and PID bytes follow it.
static bool addressFieldTerminated(const uint8_t *raw, size_t len, int digis) {
    size_t addrs = (size_t)digis + 2;
    if (len < (addrs * 7) + 2)
        return false;
    for (size_t a = 0; a < addrs; a++) {
        bool ext = (raw[(a * 7) + 6] & 0x01) != 0;
        if (ext != (a == addrs - 1))
            return false;
    }
    return (raw[addrs * 7] == 0x03) && (raw[(addrs * 7) + 1] == 0xF0);
}

static int runAddressPass(void) {
    static uint8_t bits[MAX_TX_BITS];

    Ax25Config.fullDuplex = 1;
    Ax25Init(0);
    Ax25TxDelay(TEST_TXDELAY_MS);

    unsigned encodeFail = 0;
    unsigned badTerminator = 0;
    unsigned decodeFail = 0;
    unsigned txFail = 0;
    unsigned inTreeLost = 0;

    for (int digis = 0; digis <= 8; digis++) {
        char line[128];
        size_t used = (size_t)snprintf(line, sizeof(line), "N0CALL>APRS");
        for (int d = 1; d <= digis; d++)
            used += (size_t)snprintf(&line[used], sizeof(line) - used, ",A%d", d);
        snprintf(&line[used], sizeof(line) - used, ":>x");

        ax25_frame_t frame;
        ax25_ctx_t ctx;
        uint8_t raw[AX25_FRAME_MAX_SIZE];
        memset(&ctx, 0, sizeof(ctx));
        int len = 0;
        if (ax25_encode(&frame, line, (int)strlen(line)))
            len = hdlcFrame(raw, sizeof(raw), &ctx, &frame);
        if (len <= 0) {
            encodeFail++;
            continue;
        }

        if (!addressFieldTerminated(raw, (size_t)len, digis)) {
            fprintf(stderr, "address pass: %d digipeaters, end-of-address bit not on the last address only\n", digis);
            badTerminator++;
        }

        ax25_msg_t msg;
        if (!ax25_decode(raw, (size_t)len, 0, &msg, NULL) || (msg.rpt_count != digis) || (msg.len != 2) || (memcmp(msg.info, ">x", 2) != 0)) {
            fprintf(stderr, "address pass: %d digipeaters, ax25_decode() did not recover the frame\n", digis);
            decodeFail++;
        }

        size_t n = transmitFrame(raw, (size_t)len, bits, sizeof(bits));
        if (n == 0) {
            txFail++;
            continue;
        }
        for (size_t k = 0; k < n; k++)
            Ax25BitParse(bits[k], 0, 0);
        if (!readInTree(raw, (size_t)len)) {
            fprintf(stderr, "address pass: %d digipeaters, in-tree receiver dropped the frame\n", digis);
            inTreeLost++;
        }
    }

    printf("Address field: 0-8 digipeaters\n");
    printf("  encode failures          : %u\n", encodeFail);
    printf("  terminator errors        : %u\n", badTerminator);
    printf("  decode failures          : %u\n", decodeFail);
    printf("  key-up failures          : %u\n", txFail);
    printf("  in-tree receiver losses  : %u\n", inTreeLost);
    return (encodeFail + badTerminator + decodeFail + txFail + inTreeLost) != 0;
}

// Queue one frame in half duplex and return how many milliseconds passed
// before it keyed up, advancing the clock one millisecond per service tick.
// The frame is then clocked out completely. Returns -1 if it never keyed up.
static long quietWaitMs(void) {
    static uint8_t frame[STRICT_FRAME_BUF];
    size_t len = makeFrame(frame);
    if (Ax25WriteTxFrame(frame, (uint16_t)len) == NULL)
        return -1;

    int64_t queuedUs = s_nowUs;
    serviceTick();
    for (int t = 0; (t < QUIET_TEST_MAX_TICKS) && !s_transmitting; t++) {
        s_nowUs += 1000;
        serviceTick();
    }
    if (!s_transmitting)
        return -1;
    long waited = (long)((s_nowUs - queuedUs) / 1000);

    for (size_t n = 0; s_transmitting && (n < MAX_TX_BITS); n++)
        (void)Ax25GetTxBit();
    if (s_transmitting)
        return -1;
    s_nowUs += 1000;
    serviceTick(); // observe the key-down
    return waited;
}

static int runQuietPass(void) {
    // Half duplex, a clear channel and a persistence roll that practically
    // always wins, with no slot to wait on a lost roll: the time slot is the
    // only thing that can hold a frame back.
    Ax25Config.fullDuplex = 0;
    Ax25Init(0);
    Ax25TxDelay(TEST_TXDELAY_MS);
    Ax25Config.persist = 255;
    Ax25Config.csmaSlotTime = 0;

    unsigned zeroLate = 0;
    unsigned slotEarly = 0;
    unsigned txFail = 0;
    long zeroWorst = 0;
    long slotShortest = -1;

    // A non-zero slot set first, then 0: nothing of the earlier value may
    // survive.
    Ax25TimeSlot(QUIET_TEST_SLOT_MS);
    Ax25TimeSlot(0);
    for (int i = 0; i < QUIET_TEST_FRAMES; i++) {
        long w = quietWaitMs();
        if (w < 0) {
            txFail++;
            continue;
        }
        if (w > zeroWorst)
            zeroWorst = w;
        if (w > QUIET_TEST_IMMEDIATE_MS)
            zeroLate++;
    }

    Ax25TimeSlot(QUIET_TEST_SLOT_MS);
    for (int i = 0; i < QUIET_TEST_FRAMES; i++) {
        long w = quietWaitMs();
        if (w < 0) {
            txFail++;
            continue;
        }
        if ((slotShortest < 0) || (w < slotShortest))
            slotShortest = w;
        if (w < QUIET_TEST_SLOT_MS)
            slotEarly++;
    }

    printf("Quiet time: half duplex, %d frames per case\n", QUIET_TEST_FRAMES);
    printf("  time slot 0, longest wait    : %ld ms\n", zeroWorst);
    printf("  time slot %d, shortest wait : %ld ms\n", QUIET_TEST_SLOT_MS, slotShortest);
    printf("  key-up failures          : %u\n", txFail);
    printf("  late with time slot 0    : %u\n", zeroLate);
    printf("  early with time slot %d : %u\n", QUIET_TEST_SLOT_MS, slotEarly);
    return (txFail + zeroLate + slotEarly) != 0;
}

// Trailing flags expected after a frame sent with a tail of @p ms at the
// harness's 1200 Bd: the closing flag, then the tail rounded up to whole flags.
static int expectedTrailFlags(uint16_t ms) {
    return 1 + (int)(((uint32_t)ms * 1200u + 7999u) / 8000u);
}

static int runTailGatePass(void) {
    static const uint16_t tails[] = { 0, 7, 20, 21, 100 };
    static uint8_t frame[STRICT_FRAME_BUF];
    static uint8_t bits[MAX_TX_BITS];

    Ax25Config.fullDuplex = 1;
    Ax25Init(0);
    Ax25TxDelay(TEST_TXDELAY_MS);

    unsigned tailWrong = 0;
    unsigned txFail = 0;

    // Ax25Init() alone must already give the default tail.
    int want = expectedTrailFlags(AX25_TX_TAIL_DEFAULT_MS);
    for (int i = -1; i < (int)(sizeof(tails) / sizeof(tails[0])); i++) {
        if (i >= 0) {
            Ax25TxTail(tails[i]);
            want = expectedTrailFlags(tails[i]);
        }
        size_t len = makeFrame(frame);
        size_t n = transmitFrame(frame, len, bits, sizeof(bits));
        size_t s = 0;
        size_t e = 0;
        int trail = 0;
        if (n == 0) {
            txFail++;
            continue;
        }
        if (!stripFlags(bits, n, &s, &e, &trail) || (trail != want)) {
            fprintf(stderr, "tail pass: %u ms tail gave %d trailing flags, expected %d\n", (i >= 0) ? (unsigned)tails[i] : AX25_TX_TAIL_DEFAULT_MS, trail,
                    want);
            tailWrong++;
        }
    }

    // Key-up gate: a queued frame waits while the modulator runs, then while
    // the teardown is owed, and keys up once both have cleared.
    unsigned gateLeak = 0;
    unsigned gateStuck = 0;
    size_t len = makeFrame(frame);
    if (Ax25WriteTxFrame(frame, (uint16_t)len) == NULL) {
        txFail++;
    } else {
        s_forceActive = true;
        for (int t = 0; t < GATE_TEST_TICKS; t++) {
            s_nowUs += 10000;
            serviceTick();
        }
        s_forceActive = false;
        if (s_transmitting)
            gateLeak++;

        s_teardownPending = true;
        for (int t = 0; (t < GATE_TEST_TICKS) && !s_transmitting; t++) {
            s_nowUs += 10000;
            Ax25TransmitCheck(); // no AFSK_ServiceTx(): the teardown stays owed
        }
        if (s_transmitting)
            gateLeak++;

        for (int t = 0; (t < MAX_KEYUP_TICKS) && !s_transmitting; t++) {
            s_nowUs += 10000;
            serviceTick();
        }
        if (!s_transmitting)
            gateStuck++;
        for (size_t k = 0; s_transmitting && (k < MAX_TX_BITS); k++)
            (void)Ax25GetTxBit();
        s_nowUs += 10000;
        serviceTick();
    }

    printf("Tail and key-up gate: default + %u tail lengths\n", (unsigned)(sizeof(tails) / sizeof(tails[0])));
    printf("  key-up failures          : %u\n", txFail);
    printf("  wrong trailing flags     : %u\n", tailWrong);
    printf("  keyed through the gate   : %u\n", gateLeak);
    printf("  never keyed after gate   : %u\n", gateStuck);
    return (txFail + tailWrong + gateLeak + gateStuck) != 0;
}

int main(int argc, char **argv) {
    if (argc > 1)
        s_rngState = (uint32_t)strtoul(argv[1], NULL, 0);
    if (s_rngState == 0)
        s_rngState = 1;
    printf("seed 0x%08x\n", (unsigned)s_rngState);

    int failed = runAx25Pass();
    failed |= runFx25Pass();
    failed |= runAddressPass();
    failed |= runQuietPass();
    failed |= runTailGatePass();

    printf("%s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
