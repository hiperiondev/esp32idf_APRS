/**
 * @file ax25.h
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
 * @brief AX.25 frame handling, HDLC bit-level state machine and public
 *        AX.25 data types used throughout the LibAPRS component.
 */

#ifndef LIB_AX25_H_
#define LIB_AX25_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Sentinel value used in place of an FX.25 correction count to mean
 *        "this frame was not received as FX.25" (i.e. a plain AX.25 frame).
 */
#define AX25_NOT_FX25 255

/**
 * @brief Theoretical maximum size, in bytes, of an AX.25 frame.
 *
 * Computed assuming a 2-byte Control field, a 1-byte PID field, a 256-byte
 * Information field and up to 8 digipeater address fields.
 */
#define AX25_FRAME_MAX_SIZE (329)

/**
 * @brief Usable depth of the internal TX frame ring used by
 *        Ax25WriteTxFrame()/Ax25TxFramesPending().
 *
 * The ring itself holds one more slot than this (see FRAME_MAX_COUNT in
 * ax25.c) so that head == tail unambiguously means "empty"; that spare slot
 * is never usable capacity, so it is already subtracted here. Any caller
 * that lets a config value control "how many frames may be pending before
 * we stop queuing" (e.g. g_config.rf_tx_buffers in aprs_service.c) MUST
 * clamp that value to this constant - not some other guessed number -
 * otherwise the caller's own accounting can claim room is left ("7/10
 * pending") while Ax25WriteTxFrame() independently hits the real ring limit
 * and silently drops the frame ("TX buffer full, frame dropped").
 */
#define AX25_TX_FRAME_RING_MAX (11)

/**
 * @brief AX.25 Control field value for an Unnumbered Information (UI) frame.
 */
#define AX25_CTRL_UI 0x03

/**
 * @brief AX.25 Protocol Identifier value meaning "no layer 3 protocol",
 *        as used by virtually all APRS traffic.
 */
#define AX25_PID_NOLAYER3 0xF0

/**
 * @brief Why ax25_decode() rejected a frame, distinguishing a malformed
 *        reception from a well-formed frame that simply is not APRS.
 *
 * A shared radio channel routinely carries legacy connected-mode AX.25
 * packet traffic (I-frames, S-frames) alongside APRS, and occasionally a
 * UI frame with a PID other than ::AX25_PID_NOLAYER3. Those frames are
 * intact and correctly decoded up to the point where their Control/PID
 * marks them as non-APRS; they are expected, benign channel activity, not
 * a decoder problem. A frame that is simply too short or whose address
 * field runs past the end of the buffer is different: it points at RF
 * corruption, a bit-slip in the HDLC decoder, or another genuine fault.
 * Callers use this distinction to keep those two cases in separate
 * counters/log lines so an operator sharing a channel with legacy packet
 * stations can tell "channel has non-APRS traffic on it" apart from
 * "my decoder is broken" from the dashboard alone.
 */
enum Ax25DecodeReason {
    AX25_DECODE_OK = 0,       /**< Frame decoded successfully; msg is fully populated. */
    AX25_DECODE_MALFORMED,    /**< Frame too short, or its address field ran past the end of the buffer: corrupted or truncated reception. */
    AX25_DECODE_NOT_UI,       /**< Well-formed frame whose Control field is not ::AX25_CTRL_UI: legacy connected-mode AX.25 traffic on the channel. */
    AX25_DECODE_NOT_NOLAYER3, /**< Well-formed UI frame whose PID is not ::AX25_PID_NOLAYER3: non-APRS UI traffic on the channel. */
};

/**
 * @brief Maximum number of digipeater (repeater) address fields supported
 *        in a single AX.25 frame.
 */
#define AX25_MAX_RPT 8

/**
 * @brief Extra bytes reserved in an ::AX25Call callsign buffer beyond the
 *        6 characters of the callsign itself, to simplify string handling.
 */
#define CALL_OVERSPACE 1

/**
 * @brief States of the AX.25 HDLC receiver state machine.
 */
enum Ax25RxStage {
    RX_STAGE_IDLE = 0, /**< Not currently receiving; waiting for a flag byte. */
    RX_STAGE_FLAG,     /**< HDLC flag byte(s) detected; waiting for frame data. */
    RX_STAGE_FRAME,    /**< Currently receiving a plain AX.25 frame. */
#ifdef ENABLE_FX25
    RX_STAGE_FX25_FRAME, /**< Currently receiving an FX.25 (FEC-protected) frame. */
#endif
};

/**
 * @brief Runtime configuration of the AX.25 protocol layer.
 */
struct Ax25ProtoConfig {
    uint16_t txDelayLength; /**< TXDelay (preamble) length, in milliseconds. */
    uint16_t txTailLength;  /**< TXTail (postamble) length, in milliseconds. */
    uint16_t quietTime;     /**< Channel quiet time required before transmitting, in milliseconds. */
    /**
     * Minimum PTT-off (unkeyed) hold time enforced between the end of one
     * transmission and the start of the next, in milliseconds, on top of the
     * fixed one-service-tick release holdoff Ax25TransmitCheck() already
     * always applies (see txReleaseHoldoff in ax25.c). 0 = no extra hold.
     */
    uint16_t minUnkeyTime;
    /**
     * CSMA slot time, in milliseconds: once the channel is heard clear, this
     * is how long Ax25TransmitCheck() waits between each persistence roll
     * (see `persist` below) and between each re-check of a busy channel.
     * Matches the standard AX.25/KISS "SlotTime" parameter.
     */
    uint16_t csmaSlotTime;
    /**
     * CSMA persistence parameter (standard AX.25/KISS "Persist"): once the
     * channel is heard clear, Ax25TransmitCheck() transmits immediately with
     * probability `persist / 256` on every slot and otherwise waits one more
     * `csmaSlotTime` before rolling again. 255 transmits on the first clear
     * slot every time (equivalent to plain non-persistent CSMA); lower
     * values spread contending stations' key-ups further apart.
     */
    uint8_t persist;
    /**
     * 1 = accept frames whose Control/PID do not match plain APRS UI frames.
     * Affects local RX/monitor handling only (what the modem decodes and
     * hands up as a received frame): it never reaches the APRS-IS gateway,
     * which enforces AX25_CTRL_UI/AX25_PID_NOLAYER3 itself, independently of
     * this setting, before gating anything (see igateProcess()).
     */
    uint8_t allowNonAprs : 1;
    uint8_t fx25 : 1;   /**< 1 = FX.25 (FEC) decoding is enabled for reception. */
    uint8_t fx25Tx : 1; /**< 1 = FX.25 (FEC) encoding is enabled for transmission. */
    /**
     * 1 = full duplex operation: transmit immediately without waiting for
     * the channel to go idle (no DCD check, no CSMA backoff). Required for
     * the GPIO ADC -> GPIO DAC hardware loopback test, where the node always
     * hears its own carrier and would otherwise never see a clear channel.
     */
    uint8_t fullDuplex : 1;
};

/**
 * @brief Global, live AX.25 protocol configuration used by the whole
 *        component.
 */
extern struct Ax25ProtoConfig Ax25Config;

/**
 * @brief Bit-level HDLC decoder state, used internally by the AX.25 receive
 *        state machine.
 */
typedef struct Hdlc {
    uint8_t demodulatedBits; /**< Shift register of the most recently demodulated bits. */
    uint8_t bitIndex;        /**< Current bit position within the byte being assembled. */
    uint8_t currentByte;     /**< Byte currently being assembled from incoming bits. */
    bool receiving;          /**< true while a frame reception is in progress. */
} hdlc_t;

/**
 * @brief AX.25 callsign and SSID pair, as used in every address field.
 */
typedef struct AX25Call {
    char call[6 + CALL_OVERSPACE]; /**< Callsign, up to 6 characters, NUL-terminated. */
    uint8_t ssid;                  /**< Secondary Station Identifier (0-15), decoded from bits 4:1 of the address SSID octet. */
    /**
     * Full, raw address SSID octet exactly as received on the air (or as
     * built for transmission): bit 7 is the AX.25 Command/Response (C) bit,
     * bits 6:5 are the reserved bits, bits 4:1 are the SSID already exposed
     * above, and bit 0 is the address-extension bit. APRS UI frames never
     * inspect the C bit or the reserved bits themselves, so `ssid` alone is
     * enough for APRS use; this field keeps the rest available for AX.25
     * v2.0 connected-mode use and for TNCs that expect it to round-trip.
     */
    uint8_t ssidBits;
} ax25_call_t;

/**
 * @brief Fully decoded AX.25 message, produced by ax25_decode().
 */
typedef struct AX25Msg {
    ax25_call_t src;                    /**< Source station callsign/SSID. */
    ax25_call_t dst;                    /**< Destination callsign/SSID. */
    ax25_call_t rpt_list[AX25_MAX_RPT]; /**< List of digipeater addresses found in the frame. */
    uint8_t rpt_count;                  /**< Number of valid entries in rpt_list. */
    uint8_t rpt_flags;                  /**< Bitmap of "has been repeated" flags, one bit per rpt_list entry. */
    uint16_t ctrl;                      /**< AX.25 Control field. */
    uint8_t pid;                        /**< AX.25 Protocol Identifier field. */
    uint8_t info[AX25_FRAME_MAX_SIZE];  /**< Information (payload) field. */
    size_t len;                         /**< Length, in bytes, of the data stored in info. */
    uint16_t mVrms;                     /**< RMS input level measured while this frame was received, in millivolts. */
} ax25_msg_t;

/**
 * @brief Callback invoked whenever a complete, valid AX.25 message has been
 *        decoded.
 * @param msg Pointer to the decoded message. Valid only for the duration of
 *            the callback.
 */
typedef void (*ax25_callback_t)(ax25_msg_t *msg);

/**
 * @brief Low-level AX.25 codec context: raw frame buffer, CRC accumulators
 *        and HDLC bit-stuffing state.
 *
 * A context carries state across the individual bytes of one frame and has no
 * locking of its own, so a single context must be used by one encoder or
 * decoder at a time; concurrent users need either their own context or a lock
 * shared between them.
 */
typedef struct AX25Ctx {
    uint8_t buf[AX25_FRAME_MAX_SIZE]; /**< Raw frame buffer (no flags, no FCS). */
    size_t frame_len;                 /**< Number of valid bytes currently stored in buf. */
    uint16_t crc_in;                  /**< Running CRC accumulator for frames being received. */
    uint16_t crc_out;                 /**< Running CRC accumulator for frames being transmitted. */
    ax25_callback_t hook;             /**< Callback invoked when a frame is fully decoded. */
    bool sync;                        /**< true once bit synchronization (flag detection) has been achieved. */
    bool escape;                      /**< true when the next bit must be un-stuffed (bit-stuffing state). */
} ax25_ctx_t;

/**
 * @brief Single address field as used by the TNC2 text encoder/decoder
 *        (ax25_encode() / hdlcFrame()).
 */
typedef struct ax25header_struct {
    char addr[8]; /**< Callsign, space-padded to 6 characters plus 2 extra bytes. */
    char ssid;    /**< Secondary Station Identifier. */
} ax25_header_t;

/**
 * @brief Complete frame as used by the TNC2 text encoder/decoder, holding
 *        both the address header list and the information field.
 */
typedef struct ax25frame_struct {
    ax25_header_t header[10];       /**< Destination, source and up to 8 digipeater addresses. */
    char data[AX25_FRAME_MAX_SIZE]; /**< Information (payload) field, as a NUL-terminated string. */
} ax25_frame_t;

/**
 * @brief Test whether digipeater address number @p n in @p msg has already
 *        been marked as repeated.
 * @param msg Pointer to an ::AX25Msg.
 * @param n   Zero-based digipeater address index.
 * @return Non-zero if the address has been repeated, zero otherwise.
 */
#define AX25_REPEATED(msg, n) ((msg)->rpt_flags & (1u << (n)))

/**
 * @brief Write a frame to the internal transmit buffer.
 * @param data Frame content, without HDLC flags and without the FCS
 *             (checksum); both are added automatically by the modulator.
 * @param size Size, in bytes, of @p data.
 * @return Pointer to the internal frame handle on success, or NULL on
 *         failure (for example if the frame is too large or no buffer is
 *         available).
 */
void *Ax25WriteTxFrame(const uint8_t *data, uint16_t size);

/**
 * @brief Count how many frames are currently sitting in the TX ring.
 *
 * A frame is counted from the moment Ax25WriteTxFrame() accepts it until the
 * DAC ISR has fully sent it and retired it, so the count covers both frames
 * still waiting to key up (quiet time / CSMA backoff) and the one on the air
 * right now. Callers use it to allow a bounded backlog rather than pushing
 * more frames into the ring than the RF channel can clear - queuing anyway
 * just moves the drop to Ax25WriteTxFrame() once the ring fills.
 *
 * @return Number of frames still in the ring (waiting to key up or being
 *         transmitted right now), from 0 up to FRAME_MAX_COUNT-1.
 */
uint8_t Ax25TxFramesPending(void);

/**
 * @brief Count how many times the CSMA/p-persistent anti-starvation floor
 *        has forced a transmission on a channel that was clear throughout.
 *
 * Cumulative since boot: bumped every time Ax25TransmitCheck() reaches
 * MAX_TRANSMIT_RETRY_COUNT backoff slots in which DCD never asserted once and
 * every persistence roll missed, and transmits anyway. Because the channel was
 * free the whole time, this figure measures only the transmit probability:
 * with the standard `persist` of 63 roughly one key-up in ten ends this way,
 * and a markedly higher share points at `persist` being set too low for the
 * amount of traffic this station originates.
 *
 * Runs that included even one busy slot are reported by
 * Ax25GetChannelBusyCount() instead, so the two never double-count and a
 * congested channel cannot inflate this one.
 *
 * @return Total number of forced transmissions caused by missed persistence
 *         rolls on a clear channel since boot.
 */
uint32_t Ax25GetPersistenceMissedCount(void);

/**
 * @brief Count how many times the CSMA/p-persistent anti-starvation floor
 *        has forced a transmission over a channel that was in use.
 *
 * Cumulative since boot: bumped every time Ax25TransmitCheck() reaches
 * MAX_TRANSMIT_RETRY_COUNT backoff slots of which at least one found DCD
 * asserted, and transmits anyway rather than holding the frame indefinitely.
 * This is a congestion figure - the frame goes out on top of whatever else was
 * on the air - and it climbing means the channel is busy for longer than
 * MAX_TRANSMIT_RETRY_COUNT slot times at a stretch.
 *
 * @return Total number of forced transmissions over a busy channel since boot.
 */
uint32_t Ax25GetChannelBusyCount(void);

/**
 * @brief Retrieve the next pending received frame, if any is available.
 *
 * @param dst       Set to point at the internal buffer holding the raw
 *                   frame bytes.
 * @param size      Set to the length, in bytes, of the received frame.
 * @param peak      Set to the peak signal level measured during reception.
 * @param valley    Set to the valley (minimum) signal level measured during
 *                   reception.
 * @param level     Set to the overall signal level indicator.
 * @param corrected Set to the number of bytes corrected by FX.25 FEC, or
 *                   ::AX25_NOT_FX25 if the frame was plain AX.25.
 * @param mV        Set to the RMS input level measured during reception, in
 *                   millivolts.
 * @return true if a frame was available and has been read, false if no
 *         frame was pending.
 */
bool Ax25ReadNextRxFrame(uint8_t **dst, uint16_t *size, int8_t *peak, int8_t *valley, uint8_t *level, uint8_t *corrected, uint16_t *mV);

/**
 * @brief Get the current HDLC receive state for a given demodulator.
 * @param modemNo Index of the demodulator to query, 0 ..
 *                ::MODEM_MAX_DEMODULATOR_COUNT - 1.
 * @return Current reception stage, or ::RX_STAGE_IDLE if the index is outside
 *         the valid range.
 */
enum Ax25RxStage Ax25GetRxStage(uint8_t modemNo);

/**
 * @brief Feed one demodulated bit into the AX.25 HDLC receive state machine.
 *
 * Intended for internal use by the demodulator only.
 *
 * @param bit   The received bit (0 or 1), not a symbol.
 * @param modem Index of the demodulator that produced this bit.
 * @param mV    RMS input level at the time this bit was produced, in
 *              millivolts.
 */
void Ax25BitParse(uint8_t bit, uint8_t modem, uint16_t mV);

/**
 * @brief Get the next bit to transmit from the pending TX frame.
 *
 * Intended for internal use only, called from the DAC interrupt service
 * routine while a transmission is in progress.
 *
 * @return The next bit to transmit (0 or 1).
 */
uint8_t Ax25GetTxBit(void);

/**
 * @brief Queue the frame currently held in the TX buffer for transmission.
 */
void Ax25TransmitBuffer(void);

/**
 * @brief Attempt to start transmitting a queued frame when the channel
 *        conditions allow it.
 *
 * Must be polled periodically from a task; it is not triggered by an
 * interrupt.
 */
void Ax25TransmitCheck(void);

/**
 * @brief Initialize the AX.25 protocol layer.
 * @param fx25Mode FX.25 mode selector: 0 = disabled, 1 = RX only, 2 = RX+TX.
 */
void Ax25Init(uint8_t fx25Mode);

/**
 * @brief Set the TXDelay (preamble) duration used before transmitting.
 * @param delay_ms Preamble duration, in milliseconds.
 */
void Ax25TxDelay(uint16_t delay_ms);

/**
 * @brief Set the CSMA time slot (quiet time) duration.
 * @param ts Time slot duration, in milliseconds.
 */
void Ax25TimeSlot(uint16_t ts);

/**
 * @brief Set the extra minimum PTT-off (unkeyed) hold time between
 *        transmissions, in milliseconds, on top of the fixed one-service-
 *        tick release holdoff that always applies. Takes effect on the
 *        next key-down; 0 disables the extra hold.
 * @param ms Minimum unkey time in milliseconds.
 */
void Ax25MinUnkeyTime(uint16_t ms);

/**
 * @brief Decode a raw AX.25 frame (without FCS) into a structured message.
 *
 * Reads nothing outside the @p len bytes it is given. The address field is
 * walked one address at a time and each step is checked against the end of
 * the frame first, so a header whose address-extension bits claim more
 * repeaters than the frame can hold - a truncated or corrupted reception -
 * is rejected instead of decoding bytes that lie past the frame in the
 * caller's buffer. The same applies to the control and PID fields: they are
 * only read once the frame is known to still hold them.
 *
 * @param buf    Raw frame bytes (address field onwards, no FCS).
 * @param len    Length, in bytes, of @p buf. Only these bytes are read.
 * @param mVrms  RMS input level measured while this frame was received, in
 *               millivolts.
 * @param msg    Destination structure to fill with the decoded fields.
 * @param reason Optional (may be NULL). Set to the specific ::Ax25DecodeReason
 *               for the result: ::AX25_DECODE_OK on success, or the precise
 *               rejection cause on failure - letting the caller separate a
 *               malformed/corrupted reception from a well-formed frame that
 *               is simply not APRS (legacy connected-mode traffic or a
 *               non-APRS PID sharing the channel).
 * @return true if the frame was a well-formed UI frame with a "no layer 3"
 *         PID (i.e. a decodable APRS frame - @p msg is fully populated,
 *         including @c info / @c len). false if the frame is shorter than a
 *         minimal UI header, if its address field runs past @p len, or if
 *         decoding stopped early because the control field wasn't UI or the
 *         PID wasn't AX25_PID_NOLAYER3 (corrupted frame, or legitimate
 *         non-APRS AX.25 traffic) - in that case only @c dst / @c src /
 *         @c rpt_list / @c rpt_count / @c ctrl (and @c pid, if reached) are
 *         valid; @c info / @c len are not touched by this call and must not
 *         be relied upon by the caller.
 */
bool ax25_decode(uint8_t *buf, size_t len, uint16_t mVrms, ax25_msg_t *msg, enum Ax25DecodeReason *reason);

/**
 * @brief Parse a TNC2-style monitor string into an ::ax25frame structure.
 *
 * @p txt is rewritten in place: the digipeater path is compacted to the front
 * of the buffer and tokenized there, so the caller must pass a scratch copy it
 * owns and must not expect the string to survive the call. Beyond that the
 * function keeps no state of its own and may be called from several tasks at
 * once, each with its own @p frame and @p txt.
 *
 * @param frame Destination structure to fill.
 * @param txt   TNC2 monitor string, in the form
 *              "SRC>DST,PATH:payload". Must be NUL-terminated.
 * @param size  Number of bytes of @p txt to parse, i.e. its string length.
 * @return Non-zero on success, zero if the string could not be parsed: an
 *         empty argument, no information field, or a header whose '>' does
 *         not precede the ':' that opens that field.
 */
char ax25_encode(ax25_frame_t *frame, char *txt, int size);

/**
 * @brief Serialize an ::ax25frame structure into a raw AX.25 frame.
 * @param outbuf     Destination buffer for the serialized frame.
 * @param outbuf_len Size, in bytes, of @p outbuf.
 * @param ctx        AX.25 codec context to use/update while serializing.
 * @param pkg        Frame to serialize.
 * @return Number of bytes written to @p outbuf (no HDLC flags, no FCS), or 0
 *         if the address field or the control/PID pair does not fit the
 *         internal frame buffer, or if the built frame is longer than
 *         @p outbuf_len.
 */
int hdlcFrame(uint8_t *outbuf, size_t outbuf_len, ax25_ctx_t *ctx, ax25_frame_t *pkg);

#endif /* LIB_AX25_H_ */
