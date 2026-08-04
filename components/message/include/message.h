/**
 * @file message.h
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
 * @brief APRS text messaging (send/ack/retry). Plain C / ESP-IDF.
 *
 * Configuration comes from g_config (app_config_t, web admin "Message" page).
 */

#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "query.h" // ::query_source_t, carried through to query_process_directed()

/**
 * @brief Number of message slots in the in-memory queue, shared by received and
 * outbound messages.
 *
 * The queue is the conversation history the "Snd/Rcv Msg" web page shows: every
 * message this station sends and every message it receives takes one slot, and
 * an entry stays there until it is pushed out by newer traffic. Once all
 * ::MSG_QUEUE_SIZE slots hold a message, storing the next one discards the
 * oldest entry in the queue, RX or TX alike - exactly the way a chat window
 * keeps the last few lines of a conversation and lets the older ones go.
 */
#define MSG_QUEUE_SIZE 10

#define MSG_TEXT_MAX 200 /**< In-memory storage limit for a queued message's text, in bytes (NOT the on-air limit; see ::APRS_MSG_TEXT_STD_MAX). */

/**
 * @brief Maximum APRS message TEXT length per the de-facto protocol convention
 * used by UI-View/Xastir/APRSIS32 and the wider APRS ecosystem.
 *
 * With a 9-char fixed-width addressee field and a "{NN" message-number suffix,
 * capping the text itself at 67 chars keeps the whole ":ADDRESSEE:text{id"
 * information field inside the classic 256-byte TNC2 packet budget.
 * ::MSG_TEXT_MAX is the (larger) in-memory storage limit for the RX/TX queue,
 * not the on-air protocol limit - use this constant wherever user-entered
 * message text needs to be validated/truncated before it is transmitted.
 */
#define APRS_MSG_TEXT_STD_MAX 67

/**
 * @brief Maximum number of pending messages message_send_pending_to() puts on
 * the air in answer to a single "?APRSM" directed query.
 *
 * The query asks for the traffic this station is holding, and a handful of
 * frames is what makes that useful. Answering with the whole queue would mean
 * up to ::MSG_QUEUE_SIZE frames built and keyed back to back with no gap
 * between them, from one query that the responder's own rate limiter admits
 * once every few seconds - a long uninterrupted transmission on a shared
 * channel. Whatever the cap leaves behind is still queued and unacknowledged,
 * so sendAPRSMessageRetry() keeps delivering it on its normal schedule, paced
 * one retry interval apart.
 */
#define MSG_QUERY_BURST_MAX 3

/**
 * @brief One entry of the in-memory message queue (an RX or TX message).
 *
 * @p seq orders the conversation and @p time dates it: @p seq is assigned once,
 * when the message enters the queue, and never changes, so the chat history
 * keeps the order in which messages were actually sent and received even when
 * an outbound entry is transmitted again later, and even across a system-clock
 * step (an NTP sync early after boot moves @p time, never @p seq).
 */
typedef struct {
    uint32_t seq;            /**< Position in the conversation: the value of a counter incremented once per stored message. Higher means newer. */
    time_t time;             /**< Wall-clock time the entry was created, shown next to the message. */
    time_t last_tx;          /**< Wall-clock time of this entry's last transmission; the retry timer measures its interval from here. TX entries only. */
    int8_t ack;              /**< TX retry state: >0 = retries remaining, -1 = RX pending, -2 = acked, -3 = rejected by recipient (rej). */
    bool rxtx;               /**< true = received (RX), false = to transmit (TX). */
    uint16_t msgID;          /**< APRS message number. */
    char callsign[11];       /**< The other station's callsign, upper case (NUL-terminated). */
    char text[MSG_TEXT_MAX]; /**< Message text. */
    bool used;               /**< true if this slot holds a valid entry. */
} msg_entry_t;

/**
 * @brief Initialize the in-memory message queue. Call once at startup.
 */
void message_init(void);

/**
 * @brief Returns true if `gpio` is acceptable as the "Message Alarm" pin:
 * an output-capable ESP32 GPIO that is not already used by the audio modem
 * (ADC/DAC/PTT), the RF module GPIOs, or any of the sensors_local
 * peripheral pins configured on the "MOD (GPIO)" page (I2C x2, 1-Wire,
 * UART0/1/2, Modbus DE, pulse counters, power switch, PPP modem, GNSS PPS).
 * gpio == -1 ("disabled") is always accepted.
 */
bool message_alarm_gpio_is_valid(int8_t gpio);

/**
 * @brief (Re)configure the Message Alarm GPIO from g_config.msg_alarm_enable
 * / g_config.msg_alarm_gpio. Releases any previously configured pin first
 * (leaving it as plain input, not driven), then - if enabled and the pin
 * passes message_alarm_gpio_is_valid() - sets up the new pin as an output
 * and drives it low (idle). Safe to call again any time the config changes
 * (e.g. right after a webconfig save), and safe to call with alarm disabled
 * or gpio == -1 (no-op besides releasing the previous pin).
 */
void message_alarm_configure(bool enable, int8_t gpio);

/**
 * @brief Send an APRS text message to `toCall`. Transmits on RF and/or INET
 * per g_config.msg_rf / g_config.msg_inet via the TX-queue callback
 * registered with message_set_tx_handler().
 */
void sendAPRSMessage(const char *toCall, const char *text);

/**
 * @brief Send an APRS message ACK ("ackNN") to `toCall`.
 */
void sendAPRSAck(const char *toCall, const char *msgNo);

/**
 * @brief Retry any pending outbound messages whose ack window has elapsed.
 * Call periodically (e.g. once per second) from the application task.
 */
void sendAPRSMessageRetry(void);

/**
 * @brief Re-transmit every still-unacknowledged outbound message addressed to
 * @p toCall, without consuming any of that message's remaining retries or
 * disturbing its retry schedule.
 *
 * This is what an APRS "?APRSM" directed query asks for (APRS101 chapter 15):
 * "send me the messages you are holding for me". Callsigns are matched on
 * their base form, ignoring any "-SSID" suffix on either side, the same rule
 * handleIncomingAPRS() applies to an incoming addressee.
 *
 * At most ::MSG_QUERY_BURST_MAX messages go out per call, oldest queue slot
 * first; any further pending message for @p toCall is left to
 * sendAPRSMessageRetry(), which keeps delivering it at the configured retry
 * interval.
 *
 * @param toCall Callsign of the station asking for its messages.
 * @return Number of messages re-transmitted, 0 to ::MSG_QUERY_BURST_MAX (0 if
 *         none are pending).
 */
int message_send_pending_to(const char *toCall);

/**
 * @brief Parse one incoming TNC2 text line (from RF or APRS-IS) and, if it is
 * an APRS message addressed to g_config.msg_mycall, store it and send an
 * ack. ACK lines update the outbound queue's retry state instead.
 *
 * A stored message takes its own queue slot next to everything else in the
 * conversation and is only ever displaced by newer traffic. The one line that
 * does not take a slot is a retransmission the queue already holds - same
 * sender, same message number and identical text - which is answered with a
 * fresh ack (the sender is asking for one) without appearing twice in the
 * history.
 *
 * A line whose addressed text starts with '?' is a directed query rather than
 * a message and is handed to query_process_directed() together with @p source,
 * which is the only reason this function needs to know where the line came
 * from: the messaging engine itself routes by its own "send via" flags.
 *
 * @param line   Decoded TNC2 text line.
 * @param source Where the line was received (::QUERY_SRC_RF for the radio,
 *               ::QUERY_SRC_INET for the APRS-IS feed).
 */
void handleIncomingAPRS(const char *line, query_source_t source);

/**
 * @brief Find a message queue slot by callsign, message number and direction.
 * @param call  Other station's callsign to match.
 * @param msgID APRS message number to match.
 * @param rxtx  Direction to match (true = RX, false = TX).
 * @return Index of the matching slot, or a negative value if none matches.
 */
int pkgMsg_Find(const char *call, uint16_t msgID, bool rxtx);

/**
 * @brief Dump the in-memory message queue (both RX and TX entries) as a JSON
 * array for the "Snd/Rcv Msg" web admin chat page.
 *
 * Elements come out in conversation order, oldest first (by ::msg_entry_t::seq),
 * so the page can append them top to bottom as they are read, and an outbound
 * message keeps its place in the thread for as long as it is retried. The array
 * holds at most ::MSG_QUEUE_SIZE elements. Each element is
 * {"time":<unix seconds>,"dir":"rx"|"tx","call":"<other station>","text":"<message text>","status":"rx"|"pending"|"sent"}
 * - "status" is only meaningful for "tx" entries ("pending": still awaiting
 * ack/retries remain, "sent": acked or no-retry) and is always "rx" for
 * received entries. `out` is always NUL-terminated; the return value is the
 * number of bytes written not counting that trailing NUL (same convention as
 * lastheard_dump_json()/trafficlog_dump_json()).
 */
size_t message_dump_json(char *out, size_t out_size);

#define MSG_CHANNEL_RF   (1 << 0) /**< TX-handler channel bit: transmit on RF. */
#define MSG_CHANNEL_INET (1 << 1) /**< TX-handler channel bit: transmit to APRS-IS (Internet). */

/**
 * @brief Register the function used to actually transmit a built TNC2 packet.
 *
 * The handler's @c channels argument is a bitmask of ::MSG_CHANNEL_RF /
 * ::MSG_CHANNEL_INET selecting where the packet should go.
 *
 * @param handler Callback invoked with the built packet, its length and the
 *                channel bitmask.
 */
void message_set_tx_handler(void (*handler)(const char *packet, size_t len, uint8_t channels));

#endif // MESSAGE_H
