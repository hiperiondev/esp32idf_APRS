/**
 * @file winlink.h
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
 * @brief Winlink radio e-mail over APRSLink. Plain C / ESP-IDF.
 *
 * @details APRSLink is the CMS-hosted gateway between APRS and the Winlink
 * global radio e-mail system. It is reached with ordinary APRS text messages
 * addressed to a service callsign - @c "WLNK-1" by default, configurable as
 * ::app_config_t::wl_service_call - and answers with APRS text messages back.
 * There is no separate protocol, no session layer and no framing of its own:
 * every command this module sends travels through sendAPRSMessage() and every
 * reply arrives through handleIncomingAPRS(), exactly like a conversation with
 * a human operator.
 *
 * The mailbox the service opens is keyed on the @b base callsign of the station
 * that sends the command, with any @c "-SSID" suffix ignored, so the account
 * addressed is always @c <BASECALL>@winlink.org.
 *
 * Two distinct roles are involved in carrying Winlink traffic on an APRS
 * network, and this station performs both:
 *
 * - @b Client. This module: this station runs its own APRSLink session, reads
 *   and writes its own mail, and keeps what the service sends back in the
 *   mailbox this header exposes.
 * - @b Gateway. The IGate: a neighbouring station on the local RF channel
 *   addresses the service itself, its command is gated to APRS-IS by the
 *   ordinary RF-to-Internet path, and the service's answer is put back on the
 *   air by the Internet-to-RF path. The one place that needs to know Winlink
 *   exists is the message-gating rule in main/aprs_service.c, which consults
 *   winlink_is_service_call() and ::app_config_t::wl_gate_exempt; see the
 *   comment there for the reasoning. Nothing else in this module participates.
 *
 * @par Session model
 * A session is opened by sending any command to the service. When the account
 * has secure login switched on, the service answers with a challenge of the
 * form @c "Login [NNN]", where each digit is a 1-based character position in
 * the operator's Winlink password; the answer is those characters plus three
 * arbitrary ones, in any order, so the password itself never travels on the
 * air. When secure login is off the service simply answers the command, and
 * the state machine accepts that just as readily. The session is closed by the
 * @c "B" command, and the service also expires it on its own after about two
 * hours, which ::app_config_t::wl_session_max_min stays below.
 *
 * @par Pacing
 * One command is outstanding at a time and the next one is only sent once the
 * service has acknowledged its predecessor. This is what keeps a session from
 * turning into a burst of frames on a shared channel, and it is also what
 * makes the state machine reliable: every transition is driven by something
 * the service actually said, never by a local timer alone.
 */

#ifndef WINLINK_H
#define WINLINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Number of commands queued for transmission to the service.
 *
 * @details Only one command is ever outstanding, so this is the depth of what
 * is waiting behind it rather than a transmit burst. The longest run an
 * operator produces is the body of a message being composed, entered one line
 * at a time and closed with @c "/EX", and eight slots hold a message longer
 * than the service is willing to relay in either direction.
 */
#define WL_CMD_QUEUE_SIZE 8

/**
 * @brief Number of replies from the service kept in the mailbox.
 *
 * @details The service answers a read or a listing with a handful of APRS
 * messages and caps how many it will send for any one request, so this holds
 * several complete exchanges. Storing the newest entry once the table is full
 * discards the oldest, the same way ::MSG_QUEUE_SIZE governs the chat history.
 */
#define WL_MAIL_MAX 24

/**
 * @brief Longest reply text kept per mailbox entry, terminating NUL included.
 *
 * @details Sized to the standard APRS message text length
 * (::APRS_MSG_TEXT_STD_MAX), which is the most the service can put in one
 * message.
 */
#define WL_MAIL_TEXT_MAX 68

/**
 * @brief Longest reason string winlink_last_error() returns, terminating NUL
 * excluded.
 *
 * @details Published so that a caller rendering the reason - the web admin's
 * status document escapes it into a JSON literal - can size its own buffer
 * from the same constant rather than from a guess that would silently start
 * truncating if the reasons grew.
 */
#define WL_ERROR_MAX 80

/**
 * @brief Upper bound, in bytes, on what winlink_mail_next_json() writes for a
 * single mailbox entry, terminating NUL included.
 *
 * @details A caller streaming the mailbox sizes its per-entry buffer with this
 * constant and is then guaranteed that no entry is ever cut short or dropped
 * for want of room, however many of its characters need escaping. The value is
 * derived from ::WL_MAIL_TEXT_MAX and is checked against it by a static
 * assertion in winlink.c, so it cannot fall behind if that changes.
 */
#define WL_MAIL_JSON_ENTRY_MAX 256

/**
 * @brief Where an APRSLink session currently stands.
 *
 * @details The order of the constants follows the order a successful login
 * passes through them, which is what lets the web page render the sequence as
 * progress. Every transition out of a waiting state is driven by a reply from
 * the service or by the per-state timeout in winlink_tick_1hz(); none of them
 * is driven by the operator, who can only queue work and let the session carry
 * it.
 */
typedef enum {
    WL_STATE_DISABLED = 0,   /**< The client is switched off, or has no password to answer a challenge with. */
    WL_STATE_IDLE,           /**< Enabled and not logged in. A queued command opens a session. */
    WL_STATE_LOGIN_SENT,     /**< The command that opens the session has gone out; waiting for the service to acknowledge it. */
    WL_STATE_WAIT_CHALLENGE, /**< Acknowledged; waiting for the @c "Login [NNN]" challenge, or for the reply of an account without secure login. */
    WL_STATE_CHALLENGE_SENT, /**< The challenge answer has gone out; waiting for the service to acknowledge it. */
    WL_STATE_WAIT_VALID,     /**< Answer acknowledged; waiting for the service to confirm the login is valid. */
    WL_STATE_LOGGED_IN,      /**< Command mode: queued commands are sent one at a time and replies land in the mailbox. */
    WL_STATE_COMPOSING,      /**< Inside a message being composed, between the @c "SP" that opened it and the @c "/EX" that sends it. */
    WL_STATE_LOGGING_OFF,    /**< The log-off command has gone out; waiting for the service to confirm the session is closed. */
    WL_STATE_ERROR,          /**< The session was abandoned; winlink_last_error() says why. A new login clears it. */
} winlink_state_t;

/**
 * @brief Initialize the Winlink client. Call once at startup, after storage_init()
 * and after message_init(), and before winlink_tick_1hz() is first called.
 *
 * @details Loads the mailbox from @c /storage/winlink.json, registers the
 * message observer that feeds replies to the state machine, and tells the
 * messaging engine which addressee is the Winlink service so that
 * ::app_config_t::wl_inet_only can keep the session off the air. The session
 * itself always starts closed: a login is a live exchange with the service and
 * there is nothing about one worth carrying across a reboot.
 */
void winlink_init(void);

/**
 * @brief Advance the session by one second. Call once per second from the
 * shared service tick.
 *
 * @details Non-blocking and free of file I/O, like every other function folded
 * into that tick: it retransmits an outstanding command whose acknowledgement
 * is overdue, gives up on one that has exhausted its retries, expires a session
 * that has run past ::app_config_t::wl_session_max_min, sends the next queued
 * command when the channel has been quiet long enough, and queues the periodic
 * listing when ::app_config_t::wl_poll_min asks for one. A no-op while the
 * client is disabled.
 */
void winlink_tick_1hz(void);

/**
 * @brief Re-read the settings that change how the client behaves outside the
 * session itself. Call after every configuration save.
 *
 * @details Today that is ::app_config_t::wl_inet_only, which decides whether
 * traffic addressed to the service is kept off the air; the session state
 * machine reads everything else afresh on each tick. Calling this is what makes
 * the switch take effect without a reboot.
 */
void winlink_apply_config(void);

/**
 * @brief Current session state.
 * @return The state, ::WL_STATE_DISABLED while the client is switched off.
 */
winlink_state_t winlink_state(void);

/**
 * @brief Untranslated one-word name of a session state, for logs and for the
 * status document the web page polls.
 *
 * @param s State to name.
 * @return A static string, never NULL, @c "unknown" for a value outside the
 *         enumeration.
 */
const char *winlink_state_name(winlink_state_t s);

/**
 * @brief Why the session was abandoned.
 * @return The reason recorded when the state last became ::WL_STATE_ERROR, or
 *         an empty string when no session has failed since the last successful
 *         login. Never NULL.
 */
const char *winlink_last_error(void);

/**
 * @brief How much of the local session lifetime is left.
 *
 * @details Measured against ::app_config_t::wl_session_max_min, which is set
 * below the service's own expiry so that this station gives up on a session
 * fractionally before the service does rather than after.
 *
 * @return Seconds remaining, or 0 when no session is open.
 */
uint32_t winlink_session_remaining_sec(void);

/**
 * @brief Number of commands waiting behind the one currently outstanding.
 * @return Queue depth, 0 to ::WL_CMD_QUEUE_SIZE.
 */
int winlink_queue_depth(void);

/**
 * @brief Open a session with the service.
 *
 * @details Queues the command that starts a login. Accepted only from
 * ::WL_STATE_IDLE or ::WL_STATE_ERROR; a session that is already open or
 * already opening needs nothing done to it.
 *
 * @return true when the login was queued.
 */
bool winlink_login(void);

/**
 * @brief Close the session with the service.
 *
 * @details Queues the log-off command, which the service answers by confirming
 * the session is closed. Anything still queued behind it is discarded: those
 * commands were meant for the session being closed.
 *
 * @return true when the log-off was queued.
 */
bool winlink_logoff(void);

/**
 * @brief Queue one APRSLink command line.
 *
 * @details The text is the command exactly as the service reads it - @c "L",
 * @c "R3", @c "SP someone@example.org Subject", a body line of a message being
 * composed, @c "/EX". It is stripped of anything an APRS message field cannot
 * carry and rejected if what remains is empty or longer than
 * ::APRS_MSG_TEXT_STD_MAX, so nothing that could not reach the service intact
 * is ever queued.
 *
 * When the client is idle and ::app_config_t::wl_auto_login is set, queuing a
 * command opens the session first and the command follows it; without that
 * switch, a command queued while idle is refused, so a session is only ever
 * opened deliberately.
 *
 * @param cmd Command line, NUL-terminated.
 * @return true when the command was queued.
 */
bool winlink_send_command(const char *cmd);

/**
 * @brief Ask the service for the list of pending messages.
 * @return true when the command was queued.
 */
bool winlink_list(void);

/**
 * @brief Ask the service to send the text of one pending message.
 * @param n Message number as it appeared in the listing.
 * @return true when the command was queued.
 */
bool winlink_read(unsigned n);

/**
 * @brief Ask the service to delete one pending message.
 * @param n Message number as it appeared in the listing.
 * @return true when the command was queued.
 */
bool winlink_kill(unsigned n);

/**
 * @brief Ask the service to start a reply to one pending message.
 * @param n Message number as it appeared in the listing.
 * @return true when the command was queued.
 */
bool winlink_reply(unsigned n);

/**
 * @brief Ask the service to forward one pending message to another address.
 * @param n         Message number as it appeared in the listing.
 * @param addressee E-mail address, callsign or alias to forward it to.
 * @return true when the command was queued.
 */
bool winlink_forward(unsigned n, const char *addressee);

/**
 * @brief Begin composing a message.
 *
 * @details Queues the @c "SP" command that names the recipient and the
 * subject. Everything queued after it is body text until winlink_compose_end()
 * closes the message, which is why the state machine tracks composition
 * separately: a body line is ordinary text and must not be mistaken for a
 * command.
 *
 * @param addressee E-mail address, callsign or alias to send to.
 * @param subject   Subject line. May be empty.
 * @return true when the command was queued.
 */
bool winlink_compose_begin(const char *addressee, const char *subject);

/**
 * @brief Queue one body line of the message being composed.
 * @param line Body text.
 * @return true when the line was queued, false when no message is being
 *         composed or the line does not fit an APRS message.
 */
bool winlink_compose_line(const char *line);

/**
 * @brief Finish the message being composed and hand it to the service.
 * @return true when the terminator was queued.
 */
bool winlink_compose_end(void);

/**
 * @brief Abandon the message being composed.
 *
 * @details Drops the body lines still queued locally and returns the session to
 * command mode. Anything already transmitted has reached the service and is
 * only discarded there once the message is abandoned from its side too, which
 * is what the @c "/EX" terminator's absence eventually achieves.
 *
 * @return true when a composition was in progress and was abandoned.
 */
bool winlink_compose_abort(void);

/**
 * @brief Serialize the oldest mailbox entry past @p after_seq as one JSON
 * object, for a caller streaming the mailbox one entry per chunk.
 *
 * @details The object is
 * {"time":<unix seconds>,"seq":<n>,"text":"<reply text>"}, with no leading or
 * trailing separator - the caller supplies the commas and the enclosing array.
 * @p out is always NUL-terminated; the return value is the number of bytes
 * written not counting that trailing NUL, the same convention
 * message_next_json() follows.
 *
 * Call it in a loop, starting with @p after_seq at 0 and feeding the value
 * returned through @p out_seq back in on the next iteration, until it returns
 * 0. Entries then come out oldest first. Because a returned entry always has a
 * strictly greater sequence number than @p after_seq, the loop is guaranteed to
 * terminate after at most ::WL_MAIL_MAX iterations that produce output.
 *
 * @param after_seq Exclusive lower bound on the entry sequence number. Pass 0
 *                  to start at the oldest entry held.
 * @param out       Destination buffer.
 * @param out_size  Size of @p out, in bytes. Must be at least
 *                  ::WL_MAIL_JSON_ENTRY_MAX; anything smaller writes nothing
 *                  and returns 0, rather than emitting a truncated object.
 * @param out_seq   Receives the sequence number of the entry that was written,
 *                  or @p after_seq unchanged when none was. May be NULL.
 * @return Number of bytes written (excluding the NUL), or 0 when no entry is
 *         left past @p after_seq or the arguments are unusable.
 */
size_t winlink_mail_next_json(uint32_t after_seq, char *out, size_t out_size, uint32_t *out_seq);

/**
 * @brief Number of replies currently held in the mailbox.
 * @return Entry count, 0 to ::WL_MAIL_MAX.
 */
int winlink_mail_count(void);

/**
 * @brief Discard every mailbox entry and remove the file holding them.
 * @return true when the mailbox is empty and nothing is left on the storage
 *         partition, false when the file could not be removed.
 */
bool winlink_mail_clear(void);

/**
 * @brief Whether the beacon comment should carry the Winlink notification
 * marker.
 *
 * @details APRSLink watches the comment of the position reports it sees for the
 * word the marker adds, and uses it to decide which stations to notify
 * unprompted when mail is waiting for them. It is therefore only worth adding
 * while there is a client here to receive such a notification, which is what
 * this answers: the client is enabled and the operator has asked for the marker
 * (::app_config_t::wl_comment_en).
 *
 * @return true when beacon.c should append the marker.
 */
bool winlink_comment_active(void);

/**
 * @brief The word appended to the beacon comment when winlink_comment_active()
 * is true, leading separator included.
 */
#define WL_BEACON_COMMENT_MARKER " winlink"

/**
 * @brief Whether @p call is the configured APRSLink service callsign.
 *
 * @details Compared on the base callsign, ignoring any @c "-SSID" suffix on
 * either side, the same rule handleIncomingAPRS() applies to an addressee. Used
 * by the messaging engine to route Winlink traffic and by the IGate's
 * message-gating rule to recognise a service reply.
 *
 * @param call Callsign to test. NULL or empty is never the service.
 * @return true when @p call is the service callsign.
 */
bool winlink_is_service_call(const char *call);

#endif // WINLINK_H
