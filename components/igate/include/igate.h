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
    DROP_MSG_SENDER_LOCAL,   /**< INET->RF message whose sender was itself heard on RF inside the same window: both ends of the conversation are local, so the
                                original transmission was already on the air and gating the copy back would echo it. */
    DROP_HEADER_FORBIDS_RF,  /**< INET->RF line whose header carries TCPXX, NOGATE, RFONLY, qAX or qAZ - tokens/q-constructs whose whole purpose is to forbid
                                this packet reaching RF. Checked for every line inet2rfHandler() considers, not just messages. */
    DROP_MSG_ADDRESSEE_INET, /**< INET->RF message whose addressee is itself Internet-connected and therefore already has it. */
    DROP_TX_FAIL,            /**< APRS-IS TX attempted but the socket wasn't connected / the write failed (IGate). */
    DROP_HEADER_OVERFLOW,    /**< IGate RF->INET header build overflowed its buffer (excessively long repeater path). */
    DROP_PLACEHOLDER_CALL,   /**< RX frame's source callsign is the NOCALL/MYCALL sentinel (radio not configured / digipeater misconfigured), checked
                                unconditionally at RX regardless of digi_en. */
    DROP_MODEM_NOT_READY,    /**< RF TX attempted before the audio modem finished bring-up. */
    DROP_TX_QUEUE_FULL,      /**< RF TX ring already holds "TX buffers" pending frames; new frame discarded instead of queued. */
    DROP_TX_TOO_LONG,        /**< Outgoing TNC2 packet longer than the modem's frame buffer. */
    DROP_TX_DUTY_CYCLE,      /**< Non-critical RF TX (beacon, object/item, weather, telemetry, or bulk IGate INET->RF relay) held back because the
                                station's measured transmit airtime over the rolling duty-cycle window has reached the configured ceiling
                                (g_config.duty_cycle_en/duty_cycle_pct). Message traffic and digipeat repeats are exempt and always transmit; the deferred
                                frame is simply re-offered on its own next scheduled attempt, so this is a hold-back rather than a permanent loss. */
    ERR_MODEM_SEND_FAIL,     /**< modem_send_tnc2() itself returned an error transmitting an RF frame. */
    ERR_AX25_DECODE,         /**< RX frame too short or with an address field running past the frame end: a malformed/corrupted reception, not a
                                well-formed non-APRS frame (see ERR_AX25_NOT_APRS for that case). */
    ERR_AX25_NOT_APRS,       /**< RX frame decoded as a well-formed AX.25 frame but is not APRS: Control field not UI, or UI with a PID other than
                                "no layer 3". Expected, benign traffic on a channel shared with legacy connected-mode packet stations - distinguished
                                from ERR_AX25_DECODE so the dashboard can tell "channel has non-APRS traffic on it" apart from "my decoder is broken". */
    DROP_DIGI_MALFORMED,     /**< Digipeater: frame too short to carry a destination / usable path. */
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
 * servers (g_config.aprs_server). Whenever a connection attempt to the
 * currently selected server fails - DNS lookup, socket connect, or login -
 * the task advances to the next enabled server in the list, wrapping back to
 * the first one after the last, and waits 1 second before the next attempt.
 * This failover keeps cycling through every enabled server indefinitely
 * until one of them accepts the connection.
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

#endif // IGATE_H
