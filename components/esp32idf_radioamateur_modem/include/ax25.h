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
 * @brief AX.25 Control field value for an Unnumbered Information (UI) frame.
 */
#define AX25_CTRL_UI 0x03

/**
 * @brief AX.25 Protocol Identifier value meaning "no layer 3 protocol",
 *        as used by virtually all APRS traffic.
 */
#define AX25_PID_NOLAYER3 0xF0

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
    uint16_t txDelayLength;   /**< TXDelay (preamble) length, in milliseconds. */
    uint16_t txTailLength;    /**< TXTail (postamble) length, in milliseconds. */
    uint16_t quietTime;       /**< Channel quiet time required before transmitting, in milliseconds. */
    uint8_t allowNonAprs : 1; /**< 1 = accept frames whose Control/PID do not match plain APRS UI frames. */
    uint8_t fx25 : 1;         /**< 1 = FX.25 (FEC) decoding is enabled for reception. */
    uint8_t fx25Tx : 1;       /**< 1 = FX.25 (FEC) encoding is enabled for transmission. */
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
    uint8_t ssid;                  /**< Secondary Station Identifier (0-15). */
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
 * @brief Check whether the TX frame ring currently holds a frame.
 *
 * True while a previously queued frame is still waiting to key up (quiet
 * time / CSMA backoff) or is actively being transmitted; false once it has
 * been fully sent and the DAC ISR has retired it. Intended for callers that
 * want to avoid piling more frames into the ring than the RF channel can
 * actually clear - queuing anyway just delays the drop from here to
 * Ax25WriteTxFrame() once the ring fills.
 *
 * @return true if the ring is non-empty (busy or pending), false if idle.
 */
bool Ax25TxBufferPending(void);

/**
 * @brief Count how many frames are currently sitting in the TX ring.
 *
 * Same ring as Ax25TxBufferPending(), but as a count rather than a yes/no,
 * for callers that want to allow a small backlog (e.g. accept up to N
 * queued frames) rather than treating "one frame in flight" as full.
 *
 * @return Number of frames still in the ring (waiting to key up or being
 *         transmitted right now), from 0 up to FRAME_MAX_COUNT-1.
 */
uint8_t Ax25TxFramesPending(void);

/**
 * @brief Get a bitmap indicating which demodulators currently have a
 *        received frame pending.
 * @return Bitmap, one bit per demodulator, set when that demodulator has an
 *         unread received frame.
 */
uint8_t Ax25GetReceivedFrameBitmap(void);

/**
 * @brief Clear the "frame received" bitmap returned by
 *        Ax25GetReceivedFrameBitmap().
 */
void Ax25ClearReceivedFrameBitmap(void);

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
 * @param modemNo Index of the demodulator to query.
 * @return Current reception stage.
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
 * @brief Check whether any new frames have been received since the last
 *        check.
 * @return true if at least one new frame is available to read.
 */
bool Ax25NewRxFrames(void);

/**
 * @brief Decode a raw AX.25 frame (without FCS) into a structured message.
 * @param buf   Raw frame bytes (address field onwards, no FCS).
 * @param len   Length, in bytes, of @p buf.
 * @param mVrms RMS input level measured while this frame was received, in
 *              millivolts.
 * @param msg   Destination structure to fill with the decoded fields.
 * @return true if the frame was a well-formed UI frame with a "no layer 3"
 *         PID (i.e. a decodable APRS frame - @p msg is fully populated,
 *         including @c info / @c len). false if decoding stopped early
 *         because the control field wasn't UI or the PID wasn't
 *         AX25_PID_NOLAYER3 (corrupted frame, or legitimate non-APRS AX.25
 *         traffic) - in that case only @c dst / @c src / @c rpt_list /
 *         @c ctrl (and @c pid, if reached) are valid; @c info / @c len are
 *         not touched by this call and must not be relied upon by the
 *         caller.
 */
bool ax25_decode(uint8_t *buf, size_t len, uint16_t mVrms, ax25_msg_t *msg);

/**
 * @brief Parse a TNC2-style monitor string into an ::ax25frame structure.
 * @param frame Destination structure to fill.
 * @param txt   TNC2 monitor string, in the form
 *              "SRC>DST,PATH:payload".
 * @param size  Size, in bytes, of the buffer available for @p txt.
 * @return Non-zero on success, zero if the string could not be parsed.
 */
char ax25_encode(ax25_frame_t *frame, char *txt, int size);

/**
 * @brief Serialize an ::ax25frame structure into a raw AX.25 frame.
 * @param outbuf     Destination buffer for the serialized frame.
 * @param outbuf_len Size, in bytes, of @p outbuf.
 * @param ctx        AX.25 codec context to use/update while serializing.
 * @param pkg        Frame to serialize.
 * @return Number of bytes written to @p outbuf (no HDLC flags, no FCS), or a
 *         negative value on error.
 */
int hdlcFrame(uint8_t *outbuf, size_t outbuf_len, ax25_ctx_t *ctx, ax25_frame_t *pkg);

/**
 * @brief Number of bytes actually clocked onto the air for a frame of
 *        @p frameSize bytes, under the CURRENT configuration.
 *
 * This is emphatically not @p frameSize. Plain AX.25 adds flags, the FCS and
 * bit stuffing; FX.25 replaces all of that with a fixed-size Reed-Solomon
 * block chosen by Fx25GetModeForSize(), so a 60-byte frame goes out as a
 * K=128/T=32 block plus an 8-byte correlation tag - 168 bytes, nearly three
 * times the payload. Anything sizing a timeout, a duty cycle or a channel
 * occupancy estimate must use this rather than the frame length.
 *
 * Excludes TXDelay and TXTail, which are set independently of frame size.
 *
 * @param frameSize Size of the plain AX.25 frame, without flags or FCS.
 * @return On-air size in bytes.
 */
uint16_t Ax25GetOnAirSize(uint16_t frameSize);

#endif /* LIB_AX25_H_ */
