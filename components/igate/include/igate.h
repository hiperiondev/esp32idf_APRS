/**
 * @file igate.h
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
 * @brief APRS-IS Internet Gateway component (RF->INET and INET->RF), C /
 * ESP-IDF (LWIP sockets).
 *
 * Configuration comes entirely from g_config (app_config_t, see
 * main/include/app_config.h / web admin "IGate" page).
 */

#ifndef IGATE_H
#define IGATE_H

#include <stdbool.h>
#include <stdint.h>

#include "ax25.h"

// Duplicate-suppression cache size and timeout are runtime-configurable, see
// g_config.dup_cache_size / g_config.dup_cache_timeout_ms and the
// DUP_CACHE_SIZE_*/DUP_CACHE_TIMEOUT_MS_* bounds in app_config.h. The cache
// array itself is still allocated at the fixed compile-time capacity
// DUP_CACHE_SIZE_MAX (see igate.c); dup_cache_size only selects how much of
// that capacity is actually used at runtime.

/**
 * @brief Which consumer a duplicate-suppression lookup belongs to.
 *
 * The IGate (RF->INET) and the digipeater (RF->RF) both need their own 30 s
 * view of "have I already handled this frame", and they see the same frames:
 * a single shared window would let whichever ran first consume the frame and
 * make the other one treat it as a duplicate. Entries therefore carry the
 * scope that inserted them and only ever match a lookup from that same scope,
 * so the two windows are independent while still sharing one table and one
 * expiry sweep.
 */
typedef enum {
    DUP_SCOPE_IGATE = 0, /**< IGate RF->INET gating window. */
    DUP_SCOPE_DIGI,      /**< Digipeater RF->RF repeat window. */
    DUP_SCOPE_COUNT      /**< Number of scopes; sizes the per-scope state, never used as a scope itself. */
} dup_scope_t;

/**
 * @brief IGate traffic counters (snapshot by igate_get_stats()).
 */
/**
 * @brief Reason codes for every way a packet/send can be dropped anywhere in
 * the firmware (IGate, digipeater, and the RX/TX service level alike), so the
 * dashboard's Drop Breakdown table can show "N dropped because X" for every
 * single reason with none left bucketed into a generic/opaque catch-all. See
 * igate_stats_t.dropByReason[] and igate_stats_total_drop().
 *
 * Every reason below stands for a packet that did not reach its destination.
 * Events that merely describe how a packet got there - the CSMA channel-access
 * statistics in aprs_service_stats_t, for instance - do not belong in this
 * table: counting them here would inflate the dashboard's DROP total with
 * traffic that was in fact transmitted.
 */
typedef enum {
    DROP_DUP = 0,            /**< Reserved: duplicates are tracked separately in igate_stats_t.dupCount and do not currently bump this array (kept for
                                future/other-component use). */
    DROP_TOO_SHORT,          /**< RF frame's info field shorter than the minimum usable length (IGate RF->INET). */
    DROP_SRC_PLACEHOLDER,    /**< RF frame's source callsign matches one of the APRS-IS basic RX-IGate blacklist prefixes - NOCALL, N0CALL, WIDE, TRACE, TCP -
                                which never identifies a real originating station and must never be gated onto APRS-IS (IGate RF->INET). */
    DROP_NOT_APRS,           /**< RF frame is not a valid AX.25 UI frame with PID 0xF0 (no layer 3) - IGating.aspx requires exactly that shape of every frame
                                gated onto APRS-IS, checked here independently of the modem's own allowNonAprs RX setting (IGate RF->INET). */
    DROP_PATH_TOKEN,         /**< RF frame's path carries RFONLY/TCPIP/qA/NOGATE (IGate RF->INET). */
    DROP_3RDPARTY_LOOP,      /**< RF frame is third-party ('}') traffic whose inner header already carries TCPIP/TCPXX, i.e. it already reached APRS-IS once
                                (IGate RF->INET). */
    DROP_3RDPARTY_NESTED,    /**< INET->RF selective third-party unwrap: the whitelisted station's payload is itself third-party-wrapped (a second '}'
                                immediately after the unwrapped inner header's ':'), so unwrapping it further would re-gate an already multiply-wrapped frame.
                                Rejected outright rather than unwrapped again or truncated (see aprs_service.c's inet2rfHandler()). */
    DROP_3RDPARTY_NESTED_RF, /**< RF frame is third-party ('}') traffic whose payload, once unwrapped, is itself third-party-wrapped (a second '}'
                                immediately after the unwrapped inner header's ':'). Rejected outright instead of being unwrapped only once and passed on,
                                which would otherwise leave a still-'}'-prefixed payload for the type filter to catch incidentally rather than by design
                                (see igate.c's thirdPartyUnwrap()). */
    DROP_SAT_NOT_USED,       /**< RF frame repeated via a known satellite gate whose call isn't marked used ('*') (IGate RF->INET). */
    DROP_GENERIC_QUERY,      /**< Frame/line is a generic query (info field starts with '?', e.g. "?APRS?", "?WX?") - dropped unconditionally in both
                                directions regardless of rf2inetFilter/inet2rfFilter, since relaying one lets a single RF station trigger a flood of
                                responses across the whole APRS-IS network. A directed query (":CALLSIGN :?APRSD", data type ':') is unaffected. */
    DROP_TYPE_FILTER,        /**< Payload type not allowed by rf2inetFilter (RF->INET) or inet2rfFilter (INET->RF). */
    DROP_RANGE_FILTER,       /**< Blocked by the local RF->INET range gate (g_config.rf2inet_range_en/rf2inet_range_km, see aprs_filter_haversine_km()). */
    DROP_PREFIX_FILTER, /**< Blocked by the local RF->INET callsign-prefix gate (g_config.rf2inet_prefix_en/rf2inet_prefixes, see aprs_filter_prefix_match()).
                         */
    DROP_BUDLIST,       /**< Blocked by the local callsign whitelist/blacklist (see aprs_filter_budlist_pass()). */
    DROP_MSG_NOT_LOCAL, /**< INET->RF message whose addressee has not been heard on the local RF channel inside g_config.igate_local_window_sec, so there is
                           nobody in earshot to transmit it to. */
    DROP_MSG_ADDRESSEE_HOPS,  /**< INET->RF message whose addressee was heard on RF inside the window, but only over more used digipeater addresses than
                                 g_config.igate_msg_max_hops allows: audible from here, yet beyond the reach of a transmission that carries this station's own
                                 IGate path. */
    DROP_INET2RF_RANGE,       /**< INET->RF line whose decoded position lies further from "My Station" than app_config_t::inet2rf_range_km, the mirror of
                                 ::DROP_RANGE_FILTER on the other direction. Also counted, without a distance to report, for a BrandMeister-classified line
                                 (see aprs_bm.h) that carries no decodable position of its own: an ordinary INET->RF line without one is already known local
                                 through the operator's own server-side geographic filter term, but a BrandMeister worldwide-monitor subscription carries no
                                 such term, so a position-less BrandMeister line has no other geographic gate standing between it and the transmitter. Every
                                 other position-less line is left to the gating rules further down and never counted here. */
    DROP_MSG_BROADCAST,       /**< INET->RF message addressed to a broadcast addressee - a general bulletin or announcement ("BLNn", "BLNa", with or without a
                                 group name) or one of the weather service families ("NWS-xxxxx", "SKY...", "CWA...") - which is never gated onto RF. Applied
                                 unconditionally, independently of g_config.igate_msg_gate_en and g_config.inet2rfFilter, on the same terms as
                                 ::DROP_GENERIC_QUERY. */
    DROP_MSG_SENDER_LOCAL,    /**< INET->RF message whose sender was itself heard on RF inside the same window: both ends of the conversation are local, so the
                                 original transmission was already on the air and gating the copy back would echo it. */
    DROP_HEADER_FORBIDS_RF,   /**< INET->RF line whose header carries TCPXX, NOGATE, RFONLY, qAX or qAZ - tokens/q-constructs whose whole purpose is to forbid
                                 this packet reaching RF. Checked for every line inet2rfHandler() considers, not just messages. */
    DROP_MSG_ADDRESSEE_INET,  /**< INET->RF message whose addressee is itself Internet-connected and therefore already has it. */
    DROP_TX_FAIL,             /**< APRS-IS TX attempted but the socket wasn't connected / the write failed (IGate). */
    DROP_HEADER_OVERFLOW,     /**< IGate RF->INET header build overflowed its buffer (excessively long repeater path). */
    DROP_IS_LINE_TOO_LONG,    /**< IGate RF->INET gated frame (header + info field) would exceed the 512-byte APRS-IS line limit and was refused instead of
                                 being sent truncated. */
    DROP_IS_RX_LINE_TOO_LONG, /**< A line received from APRS-IS exceeded the 512-byte APRS-IS line limit; the whole line was discarded up to the next
                                terminator instead of being processed truncated. */
    DROP_PLACEHOLDER_CALL,    /**< RX frame's source callsign is the NOCALL/MYCALL sentinel (radio not configured / digipeater misconfigured), checked
                                 unconditionally at RX regardless of digi_en. */
    DROP_MODEM_NOT_READY,     /**< RF TX attempted before the audio modem finished bring-up. */
    DROP_TX_QUEUE_FULL,       /**< RF TX ring already holds "TX buffers" pending frames; new frame discarded instead of queued. */
    DROP_TX_TOO_LONG,         /**< Outgoing TNC2 packet longer than the modem's frame buffer. */
    DROP_TX_DUTY_CYCLE,       /**< Non-critical RF TX (beacon, object/item, weather, telemetry, or bulk IGate INET->RF relay) held back because the
                                 station's measured transmit airtime over the rolling duty-cycle window has reached the configured ceiling
                                 (g_config.duty_cycle_en/duty_cycle_pct). Message traffic and digipeat repeats are exempt and always transmit; the deferred
                                 frame is simply re-offered on its own next scheduled attempt, so this is a hold-back rather than a permanent loss. */
    ERR_MODEM_SEND_FAIL,      /**< modem_send_tnc2() itself returned an error transmitting an RF frame. */
    ERR_AX25_DECODE,          /**< RX frame too short or with an address field running past the frame end: a malformed/corrupted reception, not a
                                 well-formed non-APRS frame (see ERR_AX25_NOT_APRS for that case). */
    ERR_AX25_NOT_APRS,        /**< RX frame decoded as a well-formed AX.25 frame but is not APRS: Control field not UI, or UI with a PID other than
                                 "no layer 3". Expected, benign traffic on a channel shared with legacy connected-mode packet stations - distinguished
                                 from ERR_AX25_DECODE so the dashboard can tell "channel has non-APRS traffic on it" apart from "my decoder is broken". */
    DROP_DIGI_MALFORMED,      /**< Digipeater: information field is empty, so there is nothing to repeat. */
    DROP_DIGI_PLACEHOLDER_CALL, /**< Digipeater: source callsign is the NOCALL/MYCALL sentinel. */
    DROP_DIGI_ALREADY_USED,     /**< Digipeater: path already carries this digipeater's call marked used ('*'). */
    DROP_DIGI_PATH_FULL,  /**< Digipeater: path already at the AX.25 maximum (8) repeater addresses; inserting our call would overflow rpt_list/rpt_flags. */
    DROP_DIGI_N_TRAPPED,  /**< Digipeater: hop count above the matched alias row's max_n, with the trap set to drop rather than clamp. */
    DROP_DIGI_PATH_TOKEN, /**< Digipeater: path carries qA or TCP (already gated, not for RF repeat). */
    DROP_DIGI_DUPLICATE,  /**< Digipeater: another copy of this frame was already repeated within g_config.dup_cache_timeout_ms (see
                            isDuplicatePacketScoped()). */
    DROP_REASON_COUNT     /**< Number of reasons; sizes ::igate_stats_t::dropByReason, never used as a reason itself. */
} drop_reason_t;

typedef struct {
    uint32_t rxCount;   /**< Frames considered for gatewaying (RF->INET direction). */
    uint32_t txCount;   /**< Frames actually sent to APRS-IS as a result of gatewaying (RF->INET). */
    uint32_t msgCount;  /**< APRS message packets (':' data type identifier) gated by this station, RF->INET and INET->RF alike. This is the figure the
                           "?IGATE?" Station Capabilities answer reports as MSG_CNT, so it counts messages only and not the rest of the gated traffic. */
    uint32_t dupCount;  /**< Duplicate frames suppressed. */
    uint32_t isRxCount; /**< ALL packets received from APRS-IS (every non-keepalive line read off the socket), regardless of inet2rf being enabled or the line
                           being relayed. Superset of what reaches the inet2rf handler. */
    uint32_t isTxCount; /**< ALL packets sent to APRS-IS over sendToAprsIs(): gatewayed RF frames (also in @c txCount), outbound messages (igate_send_raw()) and
                           digi "beacon to internet" sends alike. */
    uint32_t dropByReason[DROP_REASON_COUNT]; /**< Per-reason drop counters, indexed by ::drop_reason_t. igate_stats_total_drop() sums the DROP half and
                                                 igate_stats_total_err() the ERR half. */
} igate_stats_t;

/**
 * @brief Sum of every "drop" (not "err") dropByReason[] bucket - the DROP
 * half of the dashboard's DROP/ERR tile. Excludes ERR_MODEM_SEND_FAIL,
 * ERR_AX25_DECODE and ERR_AX25_NOT_APRS, which igate_stats_total_err()
 * covers instead, so the two numbers the tile shows add up to exactly the
 * Drop Breakdown table's sum with nothing counted twice.
 * @param s Stats snapshot (e.g. from igate_get_stats()).
 * @return Total number of drops across all non-err reasons.
 */
static inline uint32_t igate_stats_total_drop(const igate_stats_t *s) {
    uint32_t total = 0;
    for (int i = 0; i < DROP_REASON_COUNT; i++) {
        if (i == ERR_MODEM_SEND_FAIL || i == ERR_AX25_DECODE || i == ERR_AX25_NOT_APRS)
            continue;
        total += s->dropByReason[i];
    }
    return total;
}

/**
 * @brief Sum of every "err" dropByReason[] bucket (ERR_MODEM_SEND_FAIL,
 * ERR_AX25_DECODE, ERR_AX25_NOT_APRS) - the ERR half of the dashboard's
 * DROP/ERR tile.
 * @param s Stats snapshot (e.g. from igate_get_stats()).
 * @return Total number of errors across all err reasons.
 */
static inline uint32_t igate_stats_total_err(const igate_stats_t *s) {
    return s->dropByReason[ERR_MODEM_SEND_FAIL] + s->dropByReason[ERR_AX25_DECODE] + s->dropByReason[ERR_AX25_NOT_APRS];
}

/**
 * @brief Short human-readable name of a drop_reason_t, for dashboard labels
 * and log lines.
 * @param reason Reason code.
 * @return Static string, never NULL.
 */
const char *igate_drop_reason_name(drop_reason_t reason);

/**
 * @brief Record one drop against a given reason. Exposed so other
 * components sharing the same filtering concepts (currently
 * aprs_service.c's INET->RF handler, for its type-filter and budlist
 * checks) can contribute to the same per-reason breakdown igateProcess()
 * updates directly.
 * @param reason Reason code (out-of-range values are ignored).
 */
void igate_note_drop(drop_reason_t reason);

/**
 * @brief Record one APRS message packet gated toward RF, bumping the MSG_CNT
 * figure the "?IGATE?" answer reports. Exposed for the same reason
 * igate_note_drop() is: the INET->RF half of the gateway lives in
 * aprs_service.c, and both halves feed one set of counters. The RF->INET half
 * is counted by igateProcess() itself.
 */
void igate_note_message_gated(void);

/**
 * @brief Start the IGate service task (APRS-IS TCP client with multiserver
 * failover, login, RX line pump). Call once from app startup: the task then
 * runs for the lifetime of the firmware and has no stop entry point. It
 * re-reads g_config on every pass, so turning any of the settings that need
 * APRS-IS on or off simply makes the task open or close the uplink, with no
 * reboot and no restart of the task itself. A second call while the task
 * exists is a no-op.
 *
 * The task connects to one of the ::APRS_SERVER_NUM configured APRS-IS
 * servers (g_config.aprs_server). It advances to the next enabled server in
 * the list, wrapping back to the first one after the last and waiting 1
 * second before the next attempt, whenever the selected server stops
 * carrying the station: when a connection attempt fails outright (DNS
 * lookup, socket connect, or login), and equally when a session the server
 * did accept ends on its side - the peer closing the link, a receive error,
 * or the link falling silent for longer than the dead-link timeout. A server
 * that accepts a session and then fails to sustain it therefore never holds
 * the station on itself. Sessions the station ends on its own - the uplink
 * no longer being needed, the network going away, or a settings change
 * asking for a reconnect - leave the selection where it is. This failover
 * keeps cycling through every enabled server indefinitely until one of them
 * accepts and sustains the connection.
 */
void igate_start(void);

/**
 * @brief Feed one RF-decoded AX.25 frame to the gateway (RF -> INET direction).
 * Applies the APRS-IS basic RX-IGate source-callsign blacklist (NOCALL,
 * N0CALL, WIDE, TRACE, TCP prefixes), the AX25_CTRL_UI/AX25_PID_NOLAYER3
 * check IGating.aspx requires of every gated frame (independent of the
 * modem's own allowNonAprs RX setting), the RFONLY/TCPIP/qA/NOGATE/
 * satellite-gate path filters and the g_config.rf2inetFilter payload-type
 * whitelist (see aprs_filter.h), builds the TNC2 text line with qAR/qAO
 * path, de-duplicates, and forwards it to APRS-IS if connected.
 * @return 1 if forwarded, 0 if dropped/duplicate/not connected.
 */
int igateProcess(ax25_msg_t *packet);

/**
 * @brief Should this RF frame appear in the web traffic log?
 *
 * Implements the "Log after filters" switch of the IGate page
 * (app_config_t::igate_log_after_filters) for the RF side. With the switch
 * off - the default - every decoded frame is logged and this returns true
 * unconditionally. With it on, the frame is shown only if it passes this
 * station's own RF->INET filters: the Satellite Gate List, the payload-type
 * whitelist g_config.rf2inetFilter, the local range and prefix gates of the
 * same fieldset, and the Callsign Filter. Third-party ('}') traffic is
 * evaluated on its unwrapped inner packet, exactly as igateProcess() gates it.
 *
 * The verdict is a display choice only: it governs the traffic-log entry and
 * the matching serial console line together, and changes nothing about what is
 * gated, digipeated, transmitted or counted. The filters are evaluated
 * whatever the state of the IGate enable and the RF->INET direction switch, so
 * a receive-only station's log is narrowed rather than emptied.
 *
 * @param packet Decoded frame, as handed to igateProcess().
 * @return true if the frame belongs in the traffic log.
 */
bool igate_log_accepts_frame(const ax25_msg_t *packet);

/**
 * @brief Should this APRS-IS line appear in the web traffic log?
 *
 * The INET->RF counterpart of igate_log_accepts_frame(), with the same
 * semantics and the same switch behind it. The filters applied are the
 * payload-type whitelist g_config.inet2rfFilter (including the selective
 * third-party unwrap exception), the local INET->RF range gate, and the
 * Callsign Filter - the settings the operator makes on the IGate page, and not
 * the unconditional safety rules of aprs_service.c's INET->RF handler, whose
 * traffic (this station's own reports echoed back by the server above all)
 * stays visible. It gates the console line and the traffic-log entry together,
 * exactly as its RF counterpart does.
 *
 * @param line Raw TNC2 text line read from APRS-IS, NUL-terminated.
 * @return true if the line belongs in the traffic log.
 */
bool igate_log_accepts_line(const char *line);

/**
 * @brief Duplicate-packet check within one ::dup_scope_t window.
 *
 * Hashes @p packet (source callsign+SSID, payload length and a bidirectional
 * CRC-CCITT of the whole info field) and looks that hash up among the entries
 * inserted by the same scope inside the last g_config.dup_cache_timeout_ms. A miss
 * inserts the hash, so the next copy of the same frame reaching the same scope
 * is reported as a duplicate.
 *
 * The lookup only reads the source address and the info field, both of which
 * are untouched by digipeat path rewriting, so a frame stays recognizable as
 * the same frame no matter which route the copies took to get here.
 *
 * @param packet Decoded frame to test.
 * @param scope  Which consumer's window to test and insert into.
 * @return true if @p packet matches a frame that scope has recently seen.
 */
bool isDuplicatePacketScoped(ax25_msg_t *packet, dup_scope_t scope);

/**
 * @brief Duplicate-packet check in the ::DUP_SCOPE_IGATE window.
 * @param packet Decoded frame to test.
 * @return true if @p packet matches a recently seen frame (a duplicate).
 */
bool isDuplicatePacket(ax25_msg_t *packet);

/**
 * @brief Drop entries older than g_config.dup_cache_timeout_ms from the
 * duplicate-suppression cache. Call periodically.
 */
void clearExpiredDuplicates(void);

/**
 * @brief Snapshot of the IGate traffic counters (for the web dashboard).
 * @return Current counter values.
 */
igate_stats_t igate_get_stats(void);

/**
 * @brief True while the APRS-IS TCP socket is currently open (logged in and
 * pumping the RX line reader). Used by the web dashboard's "Network Status"
 * panel (APRS-IS pill), mirroring aprsClient.connected() on the reference
 * esp32idf_APRS dashboard.
 * @return true while the APRS-IS socket is open and logged in, false
 *         otherwise.
 */
bool igate_is_connected(void);

/**
 * @brief Register the handler invoked for every raw TNC2 text line received
 * from APRS-IS (INET -> RF direction), e.g. to hand it to the message
 * component and/or re-transmit it on RF via aprs_service_send_tnc2(). Only called
 * when g_config.inet2rf is true.
 */
void igate_set_inet2rf_handler(void (*handler)(const char *line));

/**
 * @brief Send a raw already-built TNC2 text line to APRS-IS (used by the
 * message component to gate outbound APRS messages/acks over INET). No-op
 * (returns false) if not currently connected.
 */
bool igate_send_raw(const char *line, size_t len);

/**
 * @brief Snapshot of the APRS-IS server slot the IGate task is currently
 * connected to or about to attempt, for the web dashboard's "APRS-IS Server"
 * panel. Reflects the failover rotation driven by g_config.aprs_server: it
 * changes as igate_start()'s task advances to the next enabled server after
 * a connection failure.
 * @param host    Buffer to receive the hostname (NUL-terminated). The copy is
 *                bounded by both the internal field size and @p hostLen: a
 *                buffer shorter than the stored hostname truncates it, and a
 *                buffer longer than the stored hostname is safely padded and
 *                terminated without reading past the source field.
 * @param hostLen Size of @p host in bytes. Passing 0 is a no-op.
 * @param port    Receives the TCP port.
 */
void igate_get_current_server(char *host, size_t hostLen, uint16_t *port);

/**
 * @brief Ask the IGate uplink task to drop and re-establish its APRS-IS
 * session on its next loop iteration, so a changed login identity
 * (aprs_mycall/aprs_ssid/aprs_passcode) or a changed/newly-enabled/disabled
 * server slot (g_config.aprs_server) takes effect immediately instead of
 * waiting for the link to drop on its own. Safe to call from any task
 * (typically the web-admin POST handler right after g_config is updated);
 * only sets a flag for the uplink task to act on, so it never blocks. A call
 * while no session is open, or while one is already pending closure, is a
 * harmless no-op. For a filter-only change, prefer
 * igate_request_filter_update() instead: it updates the running session
 * without dropping it.
 */
void igate_request_reconnect(void);

/**
 * @brief Ask the IGate uplink task to push the current g_config.aprs_filter
 * to APRS-IS on its next loop iteration without dropping the session, using
 * the live filter-update mechanism aprs-is.net/javAPRSFilter.aspx documents:
 * a "#filter <spec>" comment line sent on the already-open socket. Safe to
 * call from any task; only sets a flag, so it never blocks. If no session is
 * currently open, the new filter is simply picked up by the next
 * connectAprsIs() login line, so this call is harmless in that case too. A
 * pending igate_request_reconnect() takes priority: if both are requested,
 * the session is dropped and reopened with the new filter already in the
 * login line, rather than sending it twice.
 */
void igate_request_filter_update(void);

#endif // IGATE_H
