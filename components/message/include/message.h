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
 * @brief Highest APRS message number this station puts on the air, after which
 * numbering wraps back to 1.
 *
 * @details APRS101 chapter 14 gives the message identifier up to five
 * characters, and the Reply-ACK algorithm (APRS 1.1) spends three of them on
 * the @c "}AA" free acknowledgement it appends, so an outgoing number of at
 * most two digits is what keeps a full @c "{MM}AA" identifier inside the
 * protocol's budget. Two digits are ample: only ::MSG_QUEUE_SIZE messages are
 * ever outstanding at once, so numbers are reused long after the message that
 * carried them has left the queue. Zero is never used - many APRS clients read
 * a @c "{0" suffix as "no message number" and would never acknowledge it.
 */
#define MSG_ID_MAX 99

/**
 * @brief Number of correspondents for which the latest acknowledgement owed is
 * remembered, for the Reply-ACK algorithm.
 *
 * @details Rule 5 of the algorithm says that the number of the last message
 * received from a station becomes the free acknowledgement carried by the next
 * message sent to it, so one entry is needed per station this one is holding a
 * conversation with. Beyond that many, the least recently updated entry is
 * reused: the station it belonged to simply loses the free ride and falls back
 * on the ordinary @c "ackNN" that was already sent to it when its message
 * arrived, which is the pre-Reply-ACK behaviour and costs nothing but the
 * redundancy.
 */
#define MSG_REPLY_ACK_STATIONS 5

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
 * @brief Number of operator-defined message group names g_config.msg_group[]
 * holds, on top of the built-in "ALL"/"QST"/"CQ" set every station reads.
 *
 * @details APRS101 chapter 14, "Message Groups", has a receiving station read
 * every message addressed to "ALL", "QST" or "CQ" in addition to its own
 * callsign, plus any locally configured group names - a net or round-table
 * addresses its traffic to a group rather than to each participant by
 * callsign. Three slots mirrors the bulletin-slot UI pattern (::BULLETIN_COUNT
 * territory) and is ample for the handful of nets/groups a single station
 * typically follows.
 */
#define MSG_USER_GROUPS 3

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
    bool group;              /**< RX entries only: true when the message was addressed to a group name ("ALL"/"QST"/"CQ" or a g_config.msg_group[] entry)
                                   rather than to this station's own callsign. A group entry is never acked, retransmitted or auto-replied to, and is kept
                                   in a slot of its own even when a direct message from the same sender carries the same ::msgID. */
    uint16_t msgID;          /**< APRS message number. */
    char callsign[11];       /**< The other station's callsign, upper case (NUL-terminated). RX entries: the sender. TX entries: the addressee. */
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
 *
 * @details The message is numbered @c "{MM}AA" per the Reply-ACK algorithm:
 * @c MM is this station's own number for the message and @c AA the
 * acknowledgement owed to @p toCall, appended at the instant of transmission so
 * that every retry carries the most recent one. With nothing owed the number is
 * @c "{MM}", whose trailing brace is what tells the other end that a Reply-ACK
 * can be sent back. Both forms are read as an ordinary message identifier by
 * software that does not implement the algorithm.
 *
 * @note Safe to call from several tasks at once, as it is: the HTTP server
 * task reaches it from the @c /msgchat POST handler and from the Winlink
 * control pages, the 1 Hz APRS service tick and the packet receive path both
 * reach it through the Winlink session machine. Each caller claims its own
 * @c MM under a spinlock and carries it to both the transmitted frame and the
 * retry queue entry, so overlapping sends never share a number nor file one
 * that differs from the one they put on the air.
 */
void sendAPRSMessage(const char *toCall, const char *text);

/**
 * @brief Send an APRS message ACK ("ackNN") to `toCall`.
 *
 * @param toCall Callsign to acknowledge.
 * @param msgNo  Message number exactly as it was received, Reply-ACK suffix
 *               included: a message numbered @c "MM}AA" is acknowledged with
 *               @c "ackMM}AA", because the sender matches the whole identifier
 *               it issued rather than any part of it.
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
 * an APRS message addressed to g_config.msg_mycall or to a message group this
 * station reads ("ALL", "QST", "CQ" or one of g_config.msg_group[]), store it.
 * A message addressed to this station's own callsign is also acked; a group
 * message never is. ACK lines update the outbound queue's retry state
 * instead.
 *
 * The message is recognised from the line's information field - everything
 * after the first ':', since no address in a TNC2 header can hold one - which
 * must carry the @c ":ADDRESSEE:" framing of APRS101 chapter 14 in full: data
 * type identifier ':', a nine-character addressee and a closing ':'. This is
 * the same test aprs_filter_classify_info() applies, so free text that merely
 * reads like a message - inside a status report, a position comment or an
 * object - is not treated as one, and neither the Message Alarm nor an ack
 * transmission can be raised by a frame that was never addressed here.
 *
 * Per APRS101 chapter 14, "Message Groups", a receiving station reads every
 * message sent to "ALL", "QST" or "CQ" plus any user-defined group name, but
 * acknowledges only messages addressed to itself - a group has no single
 * owner to send an "ackNN" back, and every member reading the group would
 * otherwise answer at once. A group message is therefore never acked, never
 * retransmitted and never auto-replied to (no Reply-ACK bookkeeping, no
 * Message Alarm pulse), regardless of whether it happens to carry a "{id"
 * suffix.
 *
 * A stored message takes its own queue slot next to everything else in the
 * conversation and is only ever displaced by newer traffic. A group message
 * and a direct message that carry the same sender and the same message number
 * still take separate slots, since one is addressed to this station and the
 * other to a group. The one line that does not take a slot of its own is a
 * retransmission the queue already holds - same sender, same message number,
 * identical text and the same direct/group status - which is answered with a
 * fresh ack when it was a direct message (the sender is asking for one)
 * without appearing twice in the history.
 *
 * A message number written @c "MM}AA" is a Reply-ACK (APRS 1.1): @c MM is the
 * sender's own number and @c AA a free acknowledgement of one of this station's
 * outbound messages, which is matched against the queue and marks that message
 * acknowledged without any @c "ackNN" of its own ever having to arrive. @c MM
 * is remembered as the acknowledgement now owed to that station, to ride out on
 * the next message sent to it, and is what identifies the received message for
 * duplicate detection - so the same message arriving twice with two different
 * free acknowledgements is still one line of the conversation. The ordinary
 * @c "ackMM}AA" reply goes back regardless, quoting the number exactly as it
 * arrived.
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
 * @brief Upper bound, in bytes, on what message_next_json() writes for a single
 * queue entry, terminating NUL included.
 *
 * @details A caller streaming the thread sizes its per-entry buffer with this
 * constant and is then guaranteed that no message is ever cut short or dropped
 * for want of room, however long its text is and however many of its characters
 * need escaping. The value is derived from ::MSG_TEXT_MAX and the callsign field
 * width and is checked against them by a static assertion in message.c, so it
 * cannot fall behind if either changes.
 */
#define MSG_JSON_ENTRY_MAX 512

/**
 * @brief Serialize the oldest queue entry (RX or TX) whose conversation
 * position is past @p after_seq as one JSON object, for a caller streaming the
 * thread one entry per chunk.
 *
 * @details The object is
 * {"time":<unix seconds>,"dir":"rx"|"tx","call":"<other station>","text":"<message text>","status":"rx"|"pending"|"sent"},
 * with no leading or trailing separator - the caller supplies the commas and the
 * enclosing array. "status" is only meaningful for "tx" entries ("pending":
 * still awaiting ack/retries remain, "sent": acked or no-retry) and is always
 * "rx" for received entries. @p out is always NUL-terminated; the return value
 * is the number of bytes written not counting that trailing NUL (same
 * convention as lastheard_dump_json()).
 *
 * Call it in a loop, starting with @p after_seq at 0 and feeding the value
 * returned through @p out_seq back in on the next iteration, until it returns 0.
 * Entries then come out in conversation order, oldest first (by
 * ::msg_entry_t::seq), so the page can append them top to bottom as they
 * arrive, and an outbound message keeps its place in the thread for as long as
 * it is retried. Because a returned entry always has a strictly greater
 * sequence number than @p after_seq, the loop is guaranteed to terminate after
 * at most ::MSG_QUEUE_SIZE iterations that produce output.
 *
 * @param after_seq Exclusive lower bound on ::msg_entry_t::seq. Pass 0 to start
 *                  at the oldest message in the queue.
 * @param out       Destination buffer.
 * @param out_size  Size of @p out, in bytes. Must be at least
 *                  ::MSG_JSON_ENTRY_MAX; anything smaller writes nothing and
 *                  returns 0, rather than emitting a truncated object.
 * @param out_seq   Receives the sequence number of the entry that was written,
 *                  or @p after_seq unchanged when none was. May be NULL.
 * @return Number of bytes written (excluding the NUL), or 0 when no entry is
 *         left past @p after_seq or the arguments are unusable.
 */
size_t message_next_json(uint32_t after_seq, char *out, size_t out_size, uint32_t *out_seq);

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

/**
 * @brief Observer invoked for every APRS text message this station accepts and
 * for every acknowledgement or rejection that resolves one of its own outbound
 * messages.
 *
 * @details The observer exists so that a subsystem built on top of APRS
 * messaging - a conversation with a service that answers in messages, rather
 * than with a human - can take its own traffic out of the conversation history
 * without this component having to know that subsystem exists. Only one
 * observer is registered at a time, and the messaging engine calls it after the
 * frame has been recognised as a message addressed here and split into its
 * fields, but before the message is stored, so a consumed message leaves no
 * trace in the chat thread.
 *
 * Acknowledgement of the message is @b not the observer's to suppress. An
 * @c "ackNN" is what tells the other station its message arrived, it is
 * required of every direct message that asked for one, and a service waiting
 * for it will otherwise retransmit until it gives up. It is therefore sent
 * whatever the observer returns, and an acknowledgement of this station's own
 * outbound message is likewise always matched against the queue: consuming one
 * only keeps it out of the conversation history.
 *
 * @param sender Other station's callsign, upper case, SSID included as heard.
 * @param text   Message text with any @c "{id" suffix already removed, or the
 *               literal @c "ackNN" / @c "rejNN" when @p is_ack is true.
 * @param msgID  APRS message number, 0 when the message carried none.
 * @param is_ack true when this is an acknowledgement or rejection of one of
 *               this station's outbound messages rather than a new message.
 * @return true when the observer has taken the message: it is then neither
 *         stored in the queue nor allowed to raise the Message Alarm.
 */
typedef bool (*message_rx_observer_t)(const char *sender, const char *text, uint16_t msgID, bool is_ack);

/**
 * @brief Register the observer described by ::message_rx_observer_t.
 *
 * @param observer Callback to invoke, or NULL to remove the one registered.
 */
void message_set_rx_observer(message_rx_observer_t observer);

/**
 * @brief Name one addressee whose messages are sent over APRS-IS only, never on
 * RF, while this station has an APRS-IS uplink to send them over.
 *
 * @details This station is its own IGate, so a message it addresses to a
 * service that lives on the Internet has no reason to be put on the air: the
 * transmission would occupy the shared channel for traffic no station on it
 * needs to hear, and the answer arrives over the same Internet link either way.
 * The suppression only ever applies to the RF leg - with @c g_config.msg_inet
 * off there is no Internet leg to fall back on, so the message follows the
 * ordinary flags and goes out on RF as before.
 *
 * @param call Callsign of the addressee, matched on its base form with any
 *             @c "-SSID" suffix ignored, or NULL to remove the one registered.
 *             The string is copied.
 */
void message_set_inet_only_peer(const char *call);

#endif // MESSAGE_H
