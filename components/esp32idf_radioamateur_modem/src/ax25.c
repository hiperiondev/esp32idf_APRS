/**
 * @file ax25.c
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
 * @brief AX.25 frame handling implementation: HDLC bit-level receive and
 * transmit state machines, bit stuffing, FCS checking, frame buffering and the
 * CSMA/p-persistence channel access logic.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "ax25.h"
#include "crc_ccit.h"
#include "esp32idf_radioamateur_modem.h" /* modem_format_tnc2() for the readable TX log line */
#include "modem.h"

#ifdef ENABLE_FX25
#include "fx25.h"
#endif

static const char *TAG = "ax25";

struct Ax25ProtoConfig Ax25Config;

/*
 * One slot is deliberately left unused in each handle ring so that
 * head == tail unambiguously means "empty" and head + 1 == tail means "full".
 * That removes the shared rxFrameBufferFull/txFrameBufferFull flags, which
 * were read-modify-written from two different cores with no synchronisation.
 * Hence one more than the usable depth.
 *
 * 8 slots (7 usable) rather than the original 4 (3 usable): during
 * digipeating/igating, several RF frames can arrive back-to-back and each
 * gets re-queued for TX while the AFSK modem is still keying up and sending
 * the previous one (hundreds of ms at 1200 baud). With only 3 usable slots
 * that burst routinely overran the ring and later frames in the burst were
 * silently dropped ("TX buffer full, frame dropped"). The extra headroom
 * costs 4 * AX25_FRAME_MAX_SIZE (~1.3KB) more on each of the tx/rx byte
 * buffers, which is negligible on ESP32.
 */
#define FRAME_MAX_COUNT   (8) /* ring slots; usable depth is FRAME_MAX_COUNT-1 */
#define FRAME_BUFFER_SIZE (FRAME_MAX_COUNT * AX25_FRAME_MAX_SIZE)

#define RING_NEXT(i) (((i) + 1u) % FRAME_MAX_COUNT)

/*
 * How long a just-accepted frame's CRC is remembered so that the second
 * demodulator's copy of the SAME frame is recognised as a duplicate and
 * dropped.
 *
 * This used to be 4 * MODEM_MAX_DEMODULATOR_COUNT, which reads like "4 per
 * demodulator" but is not: the counter is bumped once per Ax25BitParse() call
 * and there is one call per demodulator per bit, so the window was 4 BIT
 * PERIODS. The group delay between the two demodulators' filters is larger
 * than that, so the slower one routinely found lastCrc already cleared and
 * stored the frame a second time (this is what makes decoded > sent in the
 * loopback self test).
 *
 * The window has to exceed the worst-case inter-demodulator skew and stay
 * below the shortest interval at which a station could legitimately repeat a
 * byte-identical frame. 32 bit periods is ~27 ms at 1200 Bd and ~107 ms at
 * 300 Bd; both sit comfortably in that gap.
 */
#define RX_DEDUP_HOLD_BITS  32
#define RX_DEDUP_HOLD_CALLS (RX_DEDUP_HOLD_BITS * MODEM_MAX_DEMODULATOR_COUNT)

#define STATIC_HEADER_FLAG_COUNT 4 /* flags sent before each frame */
#define STATIC_FOOTER_FLAG_COUNT 1 /* flags sent after each frame */

#define MAX_TRANSMIT_RETRY_COUNT 8 /* max retries if the channel is busy */

#define SYNC_BYTE 0x7E /* preamble/postamble octet */

static inline uint32_t millis(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static inline uint32_t randomRange(uint32_t lo, uint32_t hi) {
    return lo + (esp_random() % (hi - lo));
}

struct FrameHandle {
    uint16_t start;
    uint16_t size;
    int8_t peak;
    int8_t valley;
    uint8_t level;
    uint8_t corrected;
    uint16_t mVrms;
#ifdef ENABLE_FX25
    struct Fx25Mode *fx25Mode;
#endif
};

/*
 * The RX rings are single-producer / single-consumer, and the two ends live on
 * DIFFERENT CORES:
 *
 *   producer: Ax25BitParse()        <- afsk_rx_task, pinned to MODEM_RX_TASK_CORE
 *   consumer: Ax25ReadNextRxFrame() <- modem_service_task
 *
 * CONFIG_FREERTOS_UNICORE is not set, so those two genuinely run in parallel.
 * Every index below is therefore owned by exactly one side and published to
 * the other with an explicit release/acquire pair; nothing is read-modify-
 * written by both. Plain loads and stores of these would be reordered both by
 * the compiler and by the store buffer, which is what let the consumer observe
 * a frame handle whose payload had not been written yet.
 */
static uint8_t rxBuffer[FRAME_BUFFER_SIZE];
static uint16_t rxBufferHead = 0; /* producer owns; published to consumer */
static uint16_t rxBufferTail = 0; /* consumer owns; published to producer */
static struct FrameHandle rxFrame[FRAME_MAX_COUNT];
static uint8_t rxFrameHead = 0; /* producer owns; published to consumer */
static uint8_t rxFrameTail = 0; /* consumer owns; published to producer */

/*
 * TX is the mirror image: Ax25WriteTxFrame() runs in the caller's task while
 * Ax25GetTxBit() runs in the DAC sample-clock ISR on another core.
 */
static uint8_t txBuffer[FRAME_BUFFER_SIZE];
static uint16_t txBufferHead = 0;
static uint16_t txBufferTail = 0;
static struct FrameHandle txFrame[FRAME_MAX_COUNT];
static uint8_t txFrameHead = 0; /* producer owns; published to the DAC ISR */
static uint8_t txFrameTail = 0; /* DAC ISR owns; published to the producer */

/* Publish everything written before this point, then make `idx` visible. */
#define RING_PUBLISH(var, idx) __atomic_store_n(&(var), (idx), __ATOMIC_RELEASE)
/* Read an index published by the other side, then see everything it wrote. */
#define RING_OBSERVE(var) __atomic_load_n(&(var), __ATOMIC_ACQUIRE)

/**
 * @brief Bytes free in a frame byte-buffer, keeping one spare so that
 *        head == tail always means empty and never "completely full".
 */
static inline uint16_t ringByteSpace(uint16_t head, uint16_t tail) {
    uint16_t used = (head >= tail) ? (uint16_t)(head - tail) : (uint16_t)(FRAME_BUFFER_SIZE - tail + head);
    return (uint16_t)(FRAME_BUFFER_SIZE - used - 1u);
}

/**
 * @brief Producer-side check: is there room for one more RX frame of @p size
 *        bytes, both in the handle ring and in the byte buffer?
 *
 * The byte-buffer half of this did not exist before: rxBufferHead simply wrapped
 * modulo FRAME_BUFFER_SIZE and silently overwrote frames the service task had
 * not read yet. Only txBuffer was ever checked (GET_FREE_SIZE, in
 * Ax25WriteTxFrame).
 */
static bool rxRingHasRoom(uint16_t size) {
    if (RING_NEXT(rxFrameHead) == RING_OBSERVE(rxFrameTail))
        return false;
    return ringByteSpace(rxBufferHead, RING_OBSERVE(rxBufferTail)) >= size;
}

#ifdef ENABLE_FX25
static uint8_t txFx25Buffer[FX25_MAX_BLOCK_SIZE];
static uint8_t txTagByteIdx = 0;
#endif

enum TxStage {
    TX_STAGE_IDLE = 0,
    TX_STAGE_PREAMBLE,
    TX_STAGE_HEADER_FLAGS,
    TX_STAGE_DATA,
    TX_STAGE_CRC,
    TX_STAGE_FOOTER_FLAGS,
    TX_STAGE_TAIL,
#ifdef ENABLE_FX25
    TX_STAGE_CORRELATION_TAG,
#endif
};

enum TxInitStage {
    TX_INIT_OFF,
    TX_INIT_WAITING,
    TX_INIT_TRANSMITTING,
};

static uint8_t txByte = 0;
static uint16_t txByteIdx = 0;
static int8_t txBitIdx = 0;
static uint16_t txDelayElapsed = 0;
static uint8_t txFlagsElapsed = 0;
static uint8_t txCrcByteIdx = 0;
static uint8_t txBitstuff = 0;
static uint16_t txTailElapsed;
static uint16_t txCrc = 0xFFFF;
static uint32_t txQuiet = 0;
static uint8_t txRetries = 0;
static volatile enum TxInitStage txInitStage;
static enum TxStage txStage;

/*
 * Ax25GetTxBit() runs in the DAC sample-clock ISR (IRAM_ATTR), so it cannot
 * call ESP_LOGx directly - logging from an ISR risks blowing the ISR's stack
 * and stalling the very timer that clocks the waveform out. Instead the ISR
 * only sets these flags/counters (plain stores, single writer), and
 * Ax25TransmitCheck() - which already runs once per service-task tick in
 * normal task context - polls them and does the actual logging.
 */
static volatile uint32_t txFramesSentCount = 0; /* bumped once per frame fully clocked out */
static volatile bool txJustKeyedUp = false;      /* set true when PTT/DAC is (re)started */
static volatile bool txJustKeyedDown = false;    /* set true when TX tail finishes and PTT drops */

struct RxState {
    uint16_t crc;
    uint8_t frame[AX25_FRAME_MAX_SIZE];
    uint16_t frameIdx;
    uint8_t receivedByte;
    uint8_t receivedBitIdx;
    uint8_t rawData;
    enum Ax25RxStage rx;
#ifdef ENABLE_FX25
    struct Fx25Mode *fx25Mode;
    uint64_t tag;
#endif
};

static struct RxState rxState[MODEM_MAX_DEMODULATOR_COUNT];

static uint16_t lastCrc = 0;          /* CRC of the last received frame */
static uint16_t rxMultiplexDelay = 0; /* avoids receiving the same frame twice */

static uint16_t txDelay;
static uint16_t txTail;

static uint8_t outputFrameBuffer[AX25_FRAME_MAX_SIZE];

/* Scratch buffer used only to linearize the about-to-transmit frame (which
 * lives in the circular txBuffer) so it can be handed to ax25_decode() for
 * the "PTT ON" log line. Separate from outputFrameBuffer since that one is
 * used for RX frames and could otherwise be clobbered if both paths were
 * ever serviced close together. */
static uint8_t txLogBuffer[AX25_FRAME_MAX_SIZE];

/**
 * @brief Recalculate the CRC for one bit
 */
/* Called once per transmitted bit from Ax25GetTxBit(), which is IRAM_ATTR.
 * -O2 will almost certainly inline it, but "almost certainly" is not a
 * guarantee, and if it does not, every bit of every frame becomes a potential
 * flash fetch from inside the sample ISR. Pin it. */
static void IRAM_ATTR calculateCRC(uint8_t bit, uint16_t *crc) {
    uint16_t xor_result = *crc ^ bit;
    *crc >>= 1;
    if (xor_result & 0x0001)
        *crc ^= 0x8408;
}

#define countof(a) (sizeof(a) / sizeof(a[0]))

#define DECODE_CALL(buf, addr)                                                                                                                                 \
    for (unsigned i = 0; i < sizeof((addr)) - CALL_OVERSPACE; i++) {                                                                                           \
        char c = (char)(*(buf)++ >> 1);                                                                                                                        \
        (addr)[i] = (c == ' ') ? '\x0' : c;                                                                                                                    \
    }

#define AX25_SET_REPEATED(msg, idx, val)                                                                                                                       \
    do {                                                                                                                                                       \
        if (val)                                                                                                                                               \
            (msg)->rpt_flags |= (1u << (idx));                                                                                                                 \
        else                                                                                                                                                   \
            (msg)->rpt_flags &= ~(1u << (idx));                                                                                                                \
    } while (0)

bool ax25_decode(uint8_t *buf, size_t len, uint16_t mVrms, ax25_msg_t *msg) {
    uint8_t *buf_start = buf;

    DECODE_CALL(buf, msg->dst.call);
    msg->dst.ssid = (*buf++ >> 1) & 0x0F;
    msg->dst.call[6] = 0;

    DECODE_CALL(buf, msg->src.call);
    msg->src.ssid = (*buf >> 1) & 0x0F;
    msg->src.call[6] = 0;

    for (msg->rpt_count = 0; !(*buf++ & 0x01) && (msg->rpt_count < countof(msg->rpt_list)); msg->rpt_count++) {
        DECODE_CALL(buf, msg->rpt_list[msg->rpt_count].call);
        msg->rpt_list[msg->rpt_count].ssid = (*buf >> 1) & 0x0F;
        AX25_SET_REPEATED(msg, msg->rpt_count, (*buf & 0x80));
        msg->rpt_list[msg->rpt_count].call[6] = 0;
    }

    msg->ctrl = *buf++;
    if (msg->ctrl != AX25_CTRL_UI)
        return false; // not a UI frame (corrupted, or legitimate non-APRS AX.25 traffic)

    msg->pid = *buf++;
    if (msg->pid != AX25_PID_NOLAYER3)
        return false; // UI frame but not "no layer 3" - not an APRS frame

    memset(msg->info, 0, sizeof(msg->info));
    int rest = (int)((buf_start + len) - buf);
    if (rest > 0) {
        if (rest > (int)sizeof(msg->info) - 1)
            rest = (int)sizeof(msg->info) - 1;
        msg->len = (size_t)rest;
        /* memcpy, not strncpy: the info field may legally contain any byte */
        memcpy(msg->info, buf, (size_t)rest);
    } else {
        msg->len = 0;
    }
    msg->mVrms = mVrms;
    return true;
}

#ifdef ENABLE_FX25
/**
 * @brief Roll rxBufferHead back after a failed FX.25 parse.
 *
 * Takes the start position as an argument rather than reading it back out of
 * rxFrame[rxFrameHead].start. The handle for a frame in progress is no longer
 * written until the frame has been validated, so there is nothing to read back;
 * relying on it also meant the rollback silently depended on rxFrameHead not
 * having moved.
 */
static void removeLastFrameFromRxBuffer(uint16_t start) {
    rxBufferHead = start;
}

static void *writeFx25Frame(const uint8_t *data, uint16_t size) {
    /* Worst case: 2 flags, 2 CRC bytes and all the bits added by bitstuffing.
     * Bitstuffing occurs after 5 consecutive ones, so it can occupy up to
     * frame size / 5 additional bytes; +1 for the division remainder. */
    const struct Fx25Mode *fx25Mode = Fx25GetModeForSize(size + 4 + (size / 5) + 1);
    uint16_t requiredSize;
    if (NULL != fx25Mode)
        requiredSize = fx25Mode->K + fx25Mode->T;
    else
        return NULL; /* frame will not fit in FX.25 */

    if (ringByteSpace(txBufferHead, RING_OBSERVE(txBufferTail)) < requiredSize)
        return NULL; /* it may still fit in standard AX.25 */

    txFrame[txFrameHead].size = requiredSize;
    txFrame[txFrameHead].start = txBufferHead;
    txFrame[txFrameHead].fx25Mode = (struct Fx25Mode *)fx25Mode;

    memset(txFx25Buffer, 0, sizeof(txFx25Buffer));

    uint16_t index = 0;
    txFx25Buffer[index++] = 0x7E; /* header flag */

    uint16_t crc = 0xFFFF;
    uint8_t bits = 0;
    uint8_t bitstuff = 0;

    for (uint16_t i = 0; i < size + 2; i++) {
        for (uint8_t k = 0; k < 8; k++) {
            txFx25Buffer[index] >>= 1;
            bits++;
            if (i < size) { /* frame data */
                if ((data[i] >> k) & 1) {
                    calculateCRC(1, &crc);
                    bitstuff++;
                    txFx25Buffer[index] |= 0x80;
                } else {
                    calculateCRC(0, &crc);
                    bitstuff = 0;
                }
            } else { /* crc */
                uint8_t c;
                if (i == size)
                    c = (crc & 0xFF) ^ 0xFF;
                else
                    c = (crc >> 8) ^ 0xFF;

                if ((c >> k) & 1) {
                    bitstuff++;
                    txFx25Buffer[index] |= 0x80;
                } else {
                    bitstuff = 0;
                }
            }

            if (bits == 8) {
                bits = 0;
                index++;
            }
            if (bitstuff == 5) {
                bits++;
                bitstuff = 0;
                txFx25Buffer[index] >>= 1;
                if (bits == 8) {
                    bits = 0;
                    index++;
                }
            }
        }
    }

    /* pad with flags */
    while (index < fx25Mode->K) {
        for (uint8_t k = 0; k < 8; k++) {
            txFx25Buffer[index] >>= 1;
            bits++;

            if ((0x7E >> k) & 1)
                txFx25Buffer[index] |= 0x80;

            if (bits == 8) {
                bits = 0;
                index++;
            }
        }
    }

    Fx25Encode(txFx25Buffer, fx25Mode);

    for (uint16_t i = 0; i < (fx25Mode->K + fx25Mode->T); i++) {
        txBuffer[txBufferHead++] = txFx25Buffer[i];
        txBufferHead %= FRAME_BUFFER_SIZE;
    }

    void *ret = &txFrame[txFrameHead];
    RING_PUBLISH(txFrameHead, RING_NEXT(txFrameHead)); /* payload is in place */
    return ret;
}

static struct FrameHandle *parseFx25Frame(uint8_t *frame, uint16_t size, uint16_t *crc) {
    struct FrameHandle *h = &rxFrame[rxFrameHead];
    uint16_t initialRxBufferHead = rxBufferHead;
    uint8_t tempRxFrameHead = rxFrameHead;

    /* Destuffing writes straight into rxBuffer before the output length is
     * known, so reserve the worst case (`size` bytes, i.e. no stuffing removed)
     * up front. Checking after the fact would be too late. */
    if (!rxRingHasRoom(size))
        return NULL;

    uint16_t i = 0;
    uint16_t k = 0;
    while (frame[i] == 0x7E)
        i++;

    uint8_t bitstuff = 0;
    uint8_t outBit = 0;
    for (; i < size; i++) {
        for (uint8_t b = 0; b < 8; b++) {
            if (frame[i] & (1 << b)) {
                rxBuffer[rxBufferHead] >>= 1;
                rxBuffer[rxBufferHead] |= 0x80;
                bitstuff++;
            } else {
                if (bitstuff == 5) { /* zero after 5 ones: normal bitstuffing */
                    bitstuff = 0;
                    continue;
                } else if (bitstuff == 6) { /* zero after 6 ones: this is a flag */
                    goto endParseFx25Frame;
                } else if (bitstuff >= 7) { /* zero after 7 ones: illegal byte */
                    removeLastFrameFromRxBuffer(initialRxBufferHead);
                    return NULL;
                }
                bitstuff = 0;
                rxBuffer[rxBufferHead] >>= 1;
            }
            outBit++;
            if (outBit == 8) {
                k++;
                rxBufferHead++;
                rxBufferHead %= FRAME_BUFFER_SIZE;
                outBit = 0;
            }
        }
    }

endParseFx25Frame:
    /* k is unsigned: without this, a runt block makes (k - 2) wrap to ~65534
     * and the CRC loop below walks the whole rxBuffer. A valid frame is at
     * least 17 bytes (addresses + control + FCS), same floor the plain HDLC
     * path applies. */
    if (k < 17) {
        removeLastFrameFromRxBuffer(initialRxBufferHead);
        return NULL;
    }

    *crc = 0xFFFF;
    i = initialRxBufferHead;

    for (uint16_t j = 0; j < (k - 2); j++) {
        for (uint8_t b = 0; b < 8; b++)
            calculateCRC((rxBuffer[i] >> b) & 1, crc);

        i++;
        i %= FRAME_BUFFER_SIZE;
    }

    *crc ^= 0xFFFF;
    if ((rxBuffer[i] == (*crc & 0xFF)) && (rxBuffer[(i + 1) % FRAME_BUFFER_SIZE] == ((*crc >> 8) & 0xFF))) {
        uint16_t pathEnd = initialRxBufferHead;
        for (uint16_t j = 0; j < (k - 2); j++) {
            if (rxBuffer[pathEnd] & 1)
                break;
            pathEnd++;
            pathEnd %= FRAME_BUFFER_SIZE;
        }

        if (Ax25Config.allowNonAprs || ((rxBuffer[(pathEnd + 1) % FRAME_BUFFER_SIZE] == 0x03) && (rxBuffer[(pathEnd + 2) % FRAME_BUFFER_SIZE] == 0xF0))) {
            /* The payload is already in rxBuffer at this point. Fill the handle,
             * and only then make it visible - the release store below is what
             * orders both against the consumer on the other core. The caller
             * still writes h->peak/level/corrected/fx25Mode after we return, so
             * publication is deferred to Ax25PublishRxFrame(). */
            h->start = initialRxBufferHead;
            h->size = k - 2;
            (void)tempRxFrameHead; /* rxFrameHead is advanced by publishRxFrame() */
            return h;
        }
    }

    removeLastFrameFromRxBuffer(initialRxBufferHead);
    return NULL;
}

/**
 * @brief Make the handle parseFx25Frame() just filled visible to the consumer.
 *
 * Split out because the FX.25 caller decorates the handle (signal level, FEC
 * byte count, mode) after the parse returns. Advancing rxFrameHead inside the
 * parser, as the old code did, published the slot while those fields were still
 * being written.
 */
static void publishRxFrame(void) {
    RING_PUBLISH(rxFrameHead, RING_NEXT(rxFrameHead));
}
#endif /* ENABLE_FX25 */

void *Ax25WriteTxFrame(const uint8_t *data, uint16_t size) {
    /* Consumed by Ax25GetTxBit() in the DAC sample-clock ISR, on another core. */
    if (RING_NEXT(txFrameHead) == RING_OBSERVE(txFrameTail)) {
        ESP_LOGW(TAG, "TX buffer full, frame dropped (%u bytes)", size);
        return NULL;
    }

#ifdef ENABLE_FX25
    if (Ax25Config.fx25 && Ax25Config.fx25Tx) {
        void *ret = writeFx25Frame(data, size);
        if (ret)
            return ret;
    }
#endif

    if (ringByteSpace(txBufferHead, RING_OBSERVE(txBufferTail)) < size) {
        ESP_LOGW(TAG, "TX buffer full, frame dropped (%u bytes, %u available)",
                 size, ringByteSpace(txBufferHead, RING_OBSERVE(txBufferTail)));
        return NULL;
    }

    ESP_LOGI(TAG, "TX frame queued: %u bytes", size);

    /* Decode the frame we just queued back into a readable TNC2 line
     * ("SRC>DST,PATH:info") for the serial log. ax25_decode() only reads the
     * buffer (so the const cast is safe) and returns false for non-UI/non-APRS
     * frames, which we simply don't print. This runs in the caller's task
     * (beacon / message / service tick / weather), all of which have generous
     * stacks, and is skipped entirely when INFO logging is off for this tag so
     * it costs nothing in that case. */
    if (esp_log_level_get(TAG) >= ESP_LOG_INFO) {
        ax25_msg_t decoded;
        if (ax25_decode((uint8_t *)data, size, 0, &decoded)) {
            char tnc2[256];
            modem_format_tnc2(&decoded, tnc2, sizeof(tnc2));
            ESP_LOGI(TAG, "TX frame: %s", tnc2);
        }
    }

    txFrame[txFrameHead].size = size;
    txFrame[txFrameHead].start = txBufferHead;

#ifdef ENABLE_FX25
    txFrame[txFrameHead].fx25Mode = NULL;
#endif

    for (uint16_t i = 0; i < size; i++) {
        txBuffer[txBufferHead++] = data[i];
        txBufferHead %= FRAME_BUFFER_SIZE;
    }

    void *ret = &txFrame[txFrameHead];
    RING_PUBLISH(txFrameHead, RING_NEXT(txFrameHead)); /* payload is in place */
    return ret;
}

uint8_t Ax25TxFramesPending(void) {
    /* Producer-side read of the same ring Ax25WriteTxFrame() checks: the tail
     * is only advanced by the DAC ISR's txRetireFrame(), once a queued
     * frame's transmission (including any quiet-time wait before it started)
     * is fully done. The distance from tail to head is therefore how many
     * frames are still "in the ring" - waiting to key up or on the air right
     * now - out of FRAME_MAX_COUNT-1 usable slots. */
    uint8_t head = txFrameHead;
    uint8_t tail = RING_OBSERVE(txFrameTail);
    return (uint8_t)((head + FRAME_MAX_COUNT - tail) % FRAME_MAX_COUNT);
}

bool Ax25TxBufferPending(void) {
    return Ax25TxFramesPending() != 0;
}

bool Ax25ReadNextRxFrame(uint8_t **dst, uint16_t *size, int8_t *peak, int8_t *valley, uint8_t *level, uint8_t *corrected, uint16_t *mV) {
    uint8_t tail = rxFrameTail; /* we own this one */

    /* Acquire: everything the producer wrote before its release store - the
     * handle AND the rxBuffer payload it points at - is visible from here on. */
    if (tail == RING_OBSERVE(rxFrameHead))
        return false;

    const struct FrameHandle *h = &rxFrame[tail];
    uint16_t start = h->start;
    uint16_t len = h->size;

    if (len > AX25_FRAME_MAX_SIZE) /* cannot happen; do not overrun if it does */
        len = AX25_FRAME_MAX_SIZE;

    *dst = outputFrameBuffer;
    for (uint16_t i = 0; i < len; i++)
        (*dst)[i] = rxBuffer[(start + i) % FRAME_BUFFER_SIZE];

    *peak = h->peak;
    *valley = h->valley;
    *level = h->level;
    *size = len;
    *corrected = h->corrected;
    *mV = h->mVrms;

    /* Release the bytes first, then the slot: the producer must never conclude
     * the payload is reusable before we have finished copying it out. */
    RING_PUBLISH(rxBufferTail, (uint16_t)((start + h->size) % FRAME_BUFFER_SIZE));
    RING_PUBLISH(rxFrameTail, RING_NEXT(tail));
    return true;
}

enum Ax25RxStage Ax25GetRxStage(uint8_t modem) {
    return rxState[modem].rx;
}

void Ax25BitParse(uint8_t bit, uint8_t modem, uint16_t mV) {
    if (lastCrc != 0) { /* a frame was received */
        rxMultiplexDelay++;
        if (rxMultiplexDelay > RX_DEDUP_HOLD_CALLS) {
            /* hold it for a while and wait for the other decoders to receive it */
            lastCrc = 0;
            rxMultiplexDelay = 0;
        }
    }

    struct RxState *rx = &rxState[modem];

    rx->rawData <<= 1;
    rx->rawData |= (bit > 0);

#ifdef ENABLE_FX25
    rx->tag >>= 1;
    if (bit)
        rx->tag |= 0x8000000000000000ULL;

    if (Ax25Config.fx25 && (rx->rx != RX_STAGE_FX25_FRAME) && (NULL != (rx->fx25Mode = (struct Fx25Mode *)Fx25GetModeForTag(rx->tag)))) {
        rx->rx = RX_STAGE_FX25_FRAME;
        rx->receivedByte = 0;
        rx->receivedBitIdx = 0;
        rx->frameIdx = 0;
        return;
    }

    if (rx->rx != RX_STAGE_FX25_FRAME) {
#endif

        if (rx->rawData == 0x7E) { /* HDLC flag received */
            if (rx->rx == RX_STAGE_FRAME) {
                /* a correct frame is at least 17 bytes (source+destination+control+CRC) */
                if (rx->frameIdx >= 17) {
                    rx->crc ^= 0xFFFF;
                    if ((rx->frame[rx->frameIdx - 2] == (rx->crc & 0xFF)) && (rx->frame[rx->frameIdx - 1] == ((rx->crc >> 8) & 0xFF))) {
                        uint16_t i = 13; /* start at the SSID of the source */
                        for (; i < (rx->frameIdx - 2); i++) {
                            if (rx->frame[i] & 1) /* path end bit */
                                break;
                        }

                        /* if non-APRS frames are not allowed, require control=0x03 and PID=0xF0 */
                        if (Ax25Config.allowNonAprs || ((rx->frame[i + 1] == 0x03) && (rx->frame[i + 2] == 0xF0))) {
                            rx->frameIdx -= 2; /* remove CRC */
                            if (rx->crc != lastCrc) {
                                /* the other decoder has not received this frame yet */
                                lastCrc = rx->crc;
                                rxMultiplexDelay = 0; /* restart the dedup hold for THIS frame */

                                if (rxRingHasRoom(rx->frameIdx)) {
                                    /*
                                     * Order matters, and it used to be wrong. The old code
                                     * filled the handle, advanced rxFrameHead, and only THEN
                                     * copied the payload into rxBuffer - so the service task
                                     * (a different task, on a different core, with no lock and
                                     * no barrier between them) could see the frame appear and
                                     * copy it out of rxBuffer before, or while, it was written.
                                     * The frame it handed up was a mix of the new frame and
                                     * whatever the ring still held from an earlier one, which
                                     * is how a byte-shifted frame could reach the application
                                     * having legitimately passed the FCS check right here.
                                     *
                                     * Payload first, then the handle, then one release store
                                     * to publish the slot. Nothing before the release can be
                                     * reordered past it, and the consumer's matching acquire
                                     * load guarantees it sees all of it.
                                     */
                                    uint8_t slot = rxFrameHead;
                                    uint16_t start = rxBufferHead;

                                    for (uint16_t j = 0; j < rx->frameIdx; j++) {
                                        rxBuffer[rxBufferHead++] = rx->frame[j];
                                        rxBufferHead %= FRAME_BUFFER_SIZE;
                                    }

                                    rxFrame[slot].start = start;
                                    rxFrame[slot].size = rx->frameIdx;
                                    rxFrame[slot].mVrms = mV;
                                    ModemGetSignalLevel(modem, &rxFrame[slot].peak, &rxFrame[slot].valley, &rxFrame[slot].level);
#ifdef ENABLE_FX25
                                    rxFrame[slot].fx25Mode = NULL;
#endif
                                    rxFrame[slot].corrected = AX25_NOT_FX25;

                                    RING_PUBLISH(rxFrameHead, RING_NEXT(slot));
                                } else {
                                    ESP_LOGW(TAG, "RX frame buffer full, frame dropped");
                                }
                            }
                        }
                    }
                }
            }
            rx->rx = RX_STAGE_FLAG;
            rx->receivedByte = 0;
            rx->receivedBitIdx = 0;
            rx->frameIdx = 0;
            rx->crc = 0xFFFF;
            return;
        } else {
            rx->rx = RX_STAGE_FRAME;
        }

        /* we're inside "rx->rx != RX_STAGE_FX25_FRAME", so we're not in the middle
         * of an actual FX.25 parity/tag block - safe to check for abort here */
        if ((rx->rawData & 0x7F) == 0x7F) { /* 7 consecutive ones: error */
            rx->rx = RX_STAGE_IDLE;
            rx->receivedByte = 0;
            rx->receivedBitIdx = 0;
            rx->frameIdx = 0;
            rx->crc = 0xFFFF;
            return;
        }
        if ((rx->rawData & 0x3F) == 0x3E) /* dismiss the 0 added by bit stuffing */
            return;
#ifdef ENABLE_FX25
    }
#endif

    if (rx->rawData & 0x01) /* received a 1 */
        rx->receivedByte |= 0x80;

    if (++rx->receivedBitIdx >= 8) { /* received a full byte */
        if (rx->frameIdx >= 2) {
            for (uint8_t k = 0; k < 8; k++)
                calculateCRC((rx->frame[rx->frameIdx - 2] >> k) & 1, &(rx->crc));
        }

#ifdef ENABLE_FX25
        /* end of FX.25 reception, that is, a full block received */
        if ((rx->fx25Mode != NULL) && (rx->frameIdx == (rx->fx25Mode->K + rx->fx25Mode->T))) {
            uint8_t fixed = 0;
            bool fecSuccess = Fx25Decode(rx->frame, rx->fx25Mode, &fixed);
            uint16_t crc;
            struct FrameHandle *h = parseFx25Frame(rx->frame, rx->frameIdx, &crc);
            if (h != NULL) {
                ModemGetSignalLevel(modem, &h->peak, &h->valley, &h->level);
                if (fecSuccess) {
                    h->corrected = fixed;
                    h->fx25Mode = rx->fx25Mode;
                } else {
                    h->corrected = AX25_NOT_FX25;
                }
                h->mVrms = mV; /* was never set on the FX.25 path: the service
                                * task reported whatever the slot held last */
                lastCrc = crc;
                rxMultiplexDelay = 0;
                publishRxFrame(); /* handle complete - now make the slot visible */
            }
            /* on failure parseFx25Frame() already cleaned up the buffer state */
            rx->rx = RX_STAGE_FLAG;
            rx->receivedByte = 0;
            rx->receivedBitIdx = 0;
            rx->frameIdx = 0;
            /* Drop the mode: it is the only thing arming the block-complete test
             * above, and it was left dangling once the block was consumed. A
             * stale pointer here re-arms that test against a plain HDLC frame
             * that happens to reach K+T bytes. */
            rx->fx25Mode = NULL;
            rx->crc = 0xFFFF;
            return;
        }
#else
        rx->rx = RX_STAGE_FRAME;
#endif
        if (rx->frameIdx >= AX25_FRAME_MAX_SIZE) { /* frame too long */
            rx->rx = RX_STAGE_IDLE;
            rx->receivedByte = 0;
            rx->receivedBitIdx = 0;
            rx->frameIdx = 0;
            rx->crc = 0xFFFF;
            return;
        }
        rx->frame[rx->frameIdx++] = rx->receivedByte;
        rx->receivedByte = 0;
        rx->receivedBitIdx = 0;
    } else {
        rx->receivedByte >>= 1;
    }
}

/**
 * @brief DAC-ISR side: retire the frame at txFrameTail and hand its bytes back.
 *
 * The byte tail used to be set once, at the very end of the whole
 * transmission, with `txBufferTail = txBufferHead` - which reads an index the
 * producer owns and can therefore free a frame that Ax25WriteTxFrame() has
 * only just written and that has not been sent yet. Releasing exactly the
 * bytes of the frame that actually finished has no such window.
 */
static inline void IRAM_ATTR txRetireFrame(void) {
    const struct FrameHandle *h = &txFrame[txFrameTail];
    RING_PUBLISH(txBufferTail, (uint16_t)((h->start + h->size) % FRAME_BUFFER_SIZE));
    RING_PUBLISH(txFrameTail, RING_NEXT(txFrameTail));
    txFramesSentCount++; /* plain store; single writer (this ISR), polled elsewhere */
}

uint8_t IRAM_ATTR Ax25GetTxBit(void) {
    if (txBitIdx == 8) {
        txBitIdx = 0;
        if (txStage == TX_STAGE_PREAMBLE) { /* transmitting the preamble (TXDelay) */
            if (txDelayElapsed < txDelay) {
                txByte = SYNC_BYTE;
                txDelayElapsed++;
            } else {
                txDelayElapsed = 0;
#ifdef ENABLE_FX25
                if (NULL != txFrame[txFrameTail].fx25Mode) {
                    txStage = TX_STAGE_CORRELATION_TAG;
                    txTagByteIdx = 0;
                } else
#endif
                    txStage = TX_STAGE_HEADER_FLAGS;
            }
        }
#ifdef ENABLE_FX25
    transmitTag:
        if (txStage == TX_STAGE_CORRELATION_TAG) { /* FX.25 correlation tag */
            if (txTagByteIdx < 8)
                txByte = (txFrame[txFrameTail].fx25Mode->tag >> (8 * txTagByteIdx)) & 0xFF;
            else
                txStage = TX_STAGE_DATA;

            txTagByteIdx++;
        }
#endif
        if (txStage == TX_STAGE_HEADER_FLAGS) {
            if (txFlagsElapsed < STATIC_HEADER_FLAG_COUNT) {
                txByte = 0x7E;
                txFlagsElapsed++;
            } else {
                txFlagsElapsed = 0;
                txStage = TX_STAGE_DATA;
            }
        }
        if (txStage == TX_STAGE_DATA) {
        transmitNormalData:
            if (txFrameTail != RING_OBSERVE(txFrameHead)) {
                if (txByteIdx < txFrame[txFrameTail].size) {
                    txByte = txBuffer[(txFrame[txFrameTail].start + txByteIdx) % FRAME_BUFFER_SIZE];
                    txByteIdx++;
                }
#ifdef ENABLE_FX25
                else if (txFrame[txFrameTail].fx25Mode != NULL) {
                    txRetireFrame();
                    txByteIdx = 0;
                    if (txFrameTail != RING_OBSERVE(txFrameHead)) {
                        if (txFrame[txFrameTail].fx25Mode != NULL) {
                            txStage = TX_STAGE_CORRELATION_TAG;
                            txTagByteIdx = 0;
                            goto transmitTag;
                        } else {
                            goto transmitNormalData;
                        }
                    } else {
                        goto transmitTail;
                    }
                }
#endif
                else {
                    txStage = TX_STAGE_CRC;
                    txCrcByteIdx = 0;
                }
            } else { /* no more frames */
#ifdef ENABLE_FX25
            transmitTail:
#endif
                txByteIdx = 0;
                txBitIdx = 0;
                txStage = TX_STAGE_TAIL;
            }
        }
        if (txStage == TX_STAGE_CRC) {
            if (txCrcByteIdx <= 1) {
                txByte = (txCrc & 0xFF) ^ 0xFF;
                txCrc >>= 8;
                txCrcByteIdx++;
            } else {
                txCrc = 0xFFFF;
                txStage = TX_STAGE_FOOTER_FLAGS;
                txFlagsElapsed = 0;
            }
        }
        if (txStage == TX_STAGE_FOOTER_FLAGS) {
            if (txFlagsElapsed < STATIC_FOOTER_FLAG_COUNT) {
                txByte = 0x7E;
                txFlagsElapsed++;
            } else {
                txFlagsElapsed = 0;
                txRetireFrame();
                txByteIdx = 0;
#ifdef ENABLE_FX25
                if ((txFrameTail != RING_OBSERVE(txFrameHead)) && (txFrame[txFrameTail].fx25Mode != NULL)) {
                    txStage = TX_STAGE_CORRELATION_TAG;
                    txTagByteIdx = 0;
                    goto transmitTag;
                }
#endif
                /* back to normal data - there might be another frame to send */
                txStage = TX_STAGE_DATA;
                goto transmitNormalData;
            }
        }
        if (txStage == TX_STAGE_TAIL) {
            if (txTailElapsed < txTail) {
                txByte = SYNC_BYTE;
                txTailElapsed++;
            } else { /* tail transmitted, stop */
                txTailElapsed = 0;
                txStage = TX_STAGE_IDLE;
                txCrc = 0xFFFF;
                txBitstuff = 0;
                txByte = 0;
                txInitStage = TX_INIT_OFF;
                /* txBufferTail is released per frame by txRetireFrame(); there
                 * is deliberately no bulk reset here any more. */
                ModemTransmitStop();
                txJustKeyedDown = true; /* plain store; polled by Ax25TransmitCheck() */
                return 0;
            }
        }
    }

    uint8_t txBit = 0;
    /* normal data or CRC in AX.25 mode */
    if (
#ifdef ENABLE_FX25
        (NULL == txFrame[txFrameTail].fx25Mode) &&
#endif
        ((txStage == TX_STAGE_DATA) || (txStage == TX_STAGE_CRC))) {
        if (txBitstuff == 5) { /* 5 consecutive ones transmitted */
            txBit = 0;         /* transmit a bit-stuffed 0 */
            txBitstuff = 0;
        } else {
            if (txByte & 1) {
                txBitstuff++;
                txBit = 1;
            } else {
                txBit = 0;
                txBitstuff = 0;
            }
            if (txStage == TX_STAGE_DATA) /* CRC only over normal data */
                calculateCRC(txByte & 1, &txCrc);

            txByte >>= 1;
            txBitIdx++;
        }
    } else {
        /* FX.25 mode, or AX.25 preamble/flags: no CRC, no bit stuffing */
        txBit = txByte & 1;
        txByte >>= 1;
        txBitIdx++;
    }
    return txBit;
}

void Ax25TransmitBuffer(void) {
    if (txInitStage == TX_INIT_WAITING)
        return;
    if (txInitStage == TX_INIT_TRANSMITTING)
        return;

    if (RING_OBSERVE(txFrameHead) != txFrameTail) {
        /* In full duplex there is no reason to wait at all. */
        txQuiet = millis() + (Ax25Config.fullDuplex ? 0 : Ax25Config.quietTime);
        txInitStage = TX_INIT_WAITING;
    }
}

/**
 * @brief Log "PTT ON" together with the actual content of the frame that is
 * about to key up, decoded back into a readable TNC2 line
 * ("SRC>DST,PATH:info"), instead of a fixed placeholder string.
 *
 * txFrameTail still points at that frame here: transmitStart() runs before
 * the DAC ISR has clocked out a single bit of it, so txRetireFrame() (which
 * advances txFrameTail) has not run yet. Skipped entirely when INFO logging
 * is off for this tag, same as the TX-queued log, so it costs nothing then.
 */
static void logPttOn(void) {
    if (esp_log_level_get(TAG) < ESP_LOG_INFO) {
        return;
    }

    const struct FrameHandle *h = &txFrame[txFrameTail];

#ifdef ENABLE_FX25
    if (NULL != h->fx25Mode) {
        /* FX.25-encoded bytes (Reed-Solomon block) are not a plain AX.25
         * frame, so there is nothing meaningful for ax25_decode() to read. */
        ESP_LOGI(TAG, "PTT ON (keyed up)");
        return;
    }
#endif

    uint16_t len = h->size;
    if (len > AX25_FRAME_MAX_SIZE) /* cannot happen; do not overrun if it does */
        len = AX25_FRAME_MAX_SIZE;

    for (uint16_t i = 0; i < len; i++)
        txLogBuffer[i] = txBuffer[(h->start + i) % FRAME_BUFFER_SIZE];

    ax25_msg_t decoded;
    if (ax25_decode(txLogBuffer, len, 0, &decoded)) {
        char tnc2[256];
        modem_format_tnc2(&decoded, tnc2, sizeof(tnc2));
        ESP_LOGI(TAG, "PTT ON (keyed up) - %s", tnc2);
    } else {
        ESP_LOGI(TAG, "PTT ON (keyed up)");
    }
}

/**
 * @brief Start transmission immediately.
 * @warning Transmission must be initialized via Ax25TransmitBuffer().
 */
static void transmitStart(void) {
    txCrc = 0xFFFF;
    txStage = TX_STAGE_PREAMBLE;
    txByte = 0;
    txBitIdx = 0;
    txFlagsElapsed = 0;
    txDelayElapsed = 0;
    ModemTransmitStart();
    logPttOn();
}

/**
 * @brief Poll for TX events raised by the DAC ISR and log them.
 *
 * The ISR (Ax25GetTxBit(), via txRetireFrame() and the TX_STAGE_TAIL
 * completion) only sets plain flags/counters - it must not call ESP_LOGx
 * itself, since that runs far too much code (and can block) to be safe at
 * DAC-sample-clock priority. This runs once per Ax25TransmitCheck() call,
 * which happens every service-task tick in ordinary task context, so it is
 * safe to log from here instead.
 */
static void pollTxEvents(void) {
    static uint32_t lastReportedSentCount = 0;
    uint32_t sent = txFramesSentCount; /* single writer (ISR); safe plain read here */
    while (lastReportedSentCount != sent) {
        lastReportedSentCount++;
        ESP_LOGI(TAG, "Packet sent (frame #%u)", (unsigned)lastReportedSentCount);
    }

    if (txJustKeyedDown) {
        txJustKeyedDown = false;
        ESP_LOGI(TAG, "PTT OFF (key down)");
    }
}

void Ax25TransmitCheck(void) {
    pollTxEvents();

    if (txInitStage == TX_INIT_OFF) /* nothing to transmit */
        return;
    if (txInitStage == TX_INIT_TRANSMITTING) /* already transmitting */
        return;

    if (txQuiet > millis()) /* quiet time has not elapsed yet */
        return;

    /* Full duplex: the channel state is irrelevant, key up right away.
     * This is also what makes the GPIO ADC -> GPIO DAC loop work: the DCD is
     * permanently asserted by our own carrier, so a CSMA node would never
     * transmit a second frame. */
    if (Ax25Config.fullDuplex || !ModemDcdState()) {
        txInitStage = TX_INIT_TRANSMITTING;
        txRetries = 0;
        transmitStart();
    } else { /* channel is busy */
        if (txRetries >= MAX_TRANSMIT_RETRY_COUNT) {
            ESP_LOGI(TAG, "Channel busy after %u retries, transmitting anyway", txRetries);
            txInitStage = TX_INIT_TRANSMITTING;
            txRetries = 0;
            transmitStart();
        } else {
            txQuiet = millis() + randomRange(100, 1000);
            txRetries++;
        }
    }
}

void Ax25Init(uint8_t fx25Mode) {
    txCrc = 0xFFFF;
    uint8_t fullDuplex = Ax25Config.fullDuplex; /* preserve across re-init */
    memset(&Ax25Config, 0, sizeof(Ax25Config));
    Ax25Config.fullDuplex = fullDuplex;
    Ax25Config.quietTime = 2000;
    Ax25Config.txDelayLength = 300;
    Ax25Config.txTailLength = 1;

    if (fx25Mode == 0) {
        Ax25Config.fx25 = 0;
        Ax25Config.fx25Tx = 0;
    } else if (fx25Mode == 1) {
        Ax25Config.fx25 = 1;
        Ax25Config.fx25Tx = 0;
    } else {
        Ax25Config.fx25 = 1;
        Ax25Config.fx25Tx = 1;
    }

    memset(rxState, 0, sizeof(rxState));
    for (uint8_t i = 0; i < countof(rxState); i++)
        rxState[i].crc = 0xFFFF;

    rxBufferHead = 0;
    rxBufferTail = 0;
    rxFrameHead = 0;
    rxFrameTail = 0;
    rxMultiplexDelay = 0;
    txBufferHead = 0;
    txBufferTail = 0;
    txFrameHead = 0;
    txFrameTail = 0;
    txBitIdx = 8; /* force a stage evaluation on the very first Ax25GetTxBit() */
    txStage = TX_STAGE_IDLE;
    lastCrc = 0;

    /* milliseconds -> byte count */
    txDelay = (uint16_t)((float)Ax25Config.txDelayLength / (8.f * 1000.f / ModemGetBaudrate()));
    txTail = (uint16_t)((float)Ax25Config.txTailLength / (8.f * 1000.f / ModemGetBaudrate()));
    txInitStage = TX_INIT_OFF;
    txQuiet = millis() + Ax25Config.quietTime + randomRange(10, 200);
}

void Ax25TxDelay(uint16_t delay_ms) {
    Ax25Config.txDelayLength = delay_ms;
    txDelay = (uint16_t)((float)Ax25Config.txDelayLength / (8.f * 1000.f / ModemGetBaudrate()));
}

void Ax25TimeSlot(uint16_t ts) {
    if (ts > 0) {
        Ax25Config.quietTime = ts;
        txQuiet = millis() + Ax25Config.quietTime + randomRange(100, 1000);
    } else {
        txQuiet = 0;
    }
}

/* ====================================================================== */
/*  TNC2 <-> AX.25 helpers                                                */
/* ====================================================================== */

static unsigned int strpos(const char *txt, char chk) {
    const char *pch = strchr(txt, chk);
    if (pch == NULL)
        return 0;
    return (unsigned int)(pch - txt);
}

static void convPath(ax25_header_t *hdr, const char *txt, unsigned int size) {
    unsigned int i, p, j;
    char num[5];

    hdr->ssid = 0;
    memset(hdr->addr, 0, sizeof(hdr->addr));
    memset(num, 0, sizeof(num));

    p = strpos(txt, '-');
    if (p > 0 && p < size) {
        for (i = 0; i < p && i < 6; i++) /* callsign/path */
            hdr->addr[i] = txt[i];

        j = 0;
        for (i = p + 1; i < size && j < sizeof(num) - 1; i++) { /* SSID */
            if (txt[i] < 0x30)
                break;
            if (txt[i] > 0x39)
                break;
            num[j++] = txt[i];
        }
        if (j > 0)
            hdr->ssid = (char)atoi(num);

        hdr->ssid <<= 1;
    } else {
        for (i = 0; i < size && i < 6; i++) {
            if (txt[i] == '*')
                break;
            if (txt[i] == ',')
                break;
            if (txt[i] == ':')
                break;
            hdr->addr[i] = txt[i];
        }
        hdr->ssid = 0;
    }
    p = strpos(txt, '*');
    if (p > 0 && p < size)
        hdr->ssid |= 0x80;
    hdr->ssid |= 0x60;
}

char ax25_encode(ax25_frame_t *frame, char *txt, int size) {
    char *token, *ptr;
    int i;
    unsigned int p, p2, p3;
    char j;

    memset(frame, 0, sizeof(ax25_frame_t));

    p = strpos(txt, ':');
    if (p > 0 && p < (unsigned int)size) {
        /* payload */
        memset(frame->data, 0, sizeof(frame->data));
        for (i = 0; i < (size - (int)p) - 1 && i < (int)sizeof(frame->data) - 1; i++)
            frame->data[i] = txt[p + i + 1];

        p2 = strpos(txt, '>');
        if (p2 > 0 && p2 < (unsigned int)size) {
            convPath(&frame->header[1], &txt[0], p2); /* source callsign */
            j = (char)strpos(txt, ',');
            if ((j < 1) || ((unsigned int)j > p))
                j = (char)p;
            convPath(&frame->header[0], &txt[p2 + 1], (unsigned int)j - p2 - 1); /* destination */

            p3 = 0;
            if (((unsigned int)j > p2) && ((unsigned int)j < p)) { /* digipeater path present */
                for (i = j; i < size; i++) {
                    if (txt[i] == ':') {
                        for (; i < size; i++)
                            txt[p3++] = 0x00;
                        break;
                    }
                    txt[p3++] = txt[i];
                }
                token = strtok(txt, ",");
                j = 0;
                while (token != NULL) {
                    ptr = token;
                    convPath(&frame->header[j + 2], ptr, (unsigned int)strlen(ptr));
                    token = strtok(NULL, ",");
                    j++;
                    if (j > 7)
                        break;
                }
            }

            for (i = 0; i < 10; i++)
                frame->header[i].ssid &= 0xFE; /* clear all end-of-path bits */
            /* fix the end-of-path bit */
            for (i = 2; i < 10; i++) {
                if (frame->header[i].addr[0] == 0x00) {
                    frame->header[i - 1].ssid |= 0x01;
                    break;
                }
            }
            return 1;
        }
    }
    return 0;
}

static void ax25_putRaw(uint8_t *raw, ax25_ctx_t *ctx, uint8_t c) {
    /* No byte-level escaping here. The real HDLC bit-stuffing (insert a 0 bit
     * after five consecutive 1 bits) is done later, per bit, in Ax25GetTxBit().
     * Escaping bytes here as well used to inject a spurious 0x1B whenever a
     * header byte equalled 0x7E/0x7F/0x1B (e.g. SSID 15 encodes to 0x7E),
     * corrupting the transmitted frame. */
    ctx->crc_out = update_crc_ccit(c, ctx->crc_out);
    *raw = c;
}

int hdlcFrame(uint8_t *outbuf, size_t outbuf_len, ax25_ctx_t *ctx, ax25_frame_t *pkg) {
    int i, j;
    int idx = 0;
    uint8_t data = 0;
    uint8_t info[AX25_FRAME_MAX_SIZE];

    ctx->crc_out = CRC_CCIT_INIT_VAL;

    for (i = 0; i < 10; i++)
        pkg->header[i].ssid &= 0xFE; /* clear all end-of-path bits */
    for (i = 1; i < 10; i++) {
        if (pkg->header[i].addr[0] == 0x00) {
            pkg->header[i - 1].ssid |= 0x01;
            break;
        }
    }

    for (i = 0; i < 10; i++) {
        if (pkg->header[i].addr[0] == 0)
            break;
        for (j = 0; j < 6; j++) {
            data = (uint8_t)pkg->header[i].addr[j];
            if (data == 0)
                data = 0x20;
            data <<= 1;
            if (idx >= (int)sizeof(info))
                return 0;
            ax25_putRaw(&info[idx++], ctx, data);
        }
        if (idx >= (int)sizeof(info))
            return 0;
        ax25_putRaw(&info[idx++], ctx, (uint8_t)pkg->header[i].ssid);
        if (pkg->header[i].ssid & 0x01)
            break;
    }

    ax25_putRaw(&info[idx++], ctx, AX25_CTRL_UI);      /* 0x03 - APRS UI frame */
    ax25_putRaw(&info[idx++], ctx, AX25_PID_NOLAYER3); /* 0xF0 - no layer 3 */

    for (i = 0; i < (int)strlen(pkg->data); i++) {
        if (idx >= (int)sizeof(info))
            break;
        ax25_putRaw(&info[idx++], ctx, (uint8_t)pkg->data[i]);
    }

    if ((size_t)idx > outbuf_len)
        return 0;

    memcpy(outbuf, info, (size_t)idx);
    return idx;
}
