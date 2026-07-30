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

#define DUP_PACKET_CACHE_SIZE 10    /**< Number of recent frames kept for duplicate suppression. */
#define DUP_PACKET_TIMEOUT_MS 30000 /**< Duplicate-suppression window, in milliseconds (30 s). */

/**
 * @brief IGate traffic counters (snapshot by igate_get_stats()).
 */
/**
 * @brief Reason codes for every way a packet/send can be dropped anywhere in
 * the firmware (IGate, digipeater, and the RX/TX service level alike), so the
 * dashboard's Drop Breakdown table can show "N dropped because X" for every
 * single reason with none left bucketed into a generic/opaque catch-all. See
 * igate_stats_t.dropByReason[] and igate_stats_total_drop().
 */
typedef enum {
    DROP_DUP = 0,         /**< Reserved: duplicates are tracked separately in igate_stats_t.dupCount and do not currently bump this array (kept for
                             future/other-component use). */
    DROP_TOO_SHORT,       /**< RF frame's info field shorter than the minimum usable length (IGate RF->INET). */
    DROP_PATH_TOKEN,      /**< RF frame's path carries RFONLY/TCPIP/qA/NOGATE (IGate RF->INET). */
    DROP_SAT_NOT_USED,    /**< RF frame repeated via a known satellite gate whose call isn't marked used ('*') (IGate RF->INET). */
    DROP_TYPE_FILTER,     /**< Payload type not allowed by rf2inetFilter (RF->INET) or inet2rfFilter (INET->RF). */
    DROP_RANGE_FILTER,    /**< Blocked by the local RF->INET range gate (g_config.rf2inet_range_en/rf2inet_range_km, see aprs_filter_haversine_km()). */
    DROP_PREFIX_FILTER,   /**< Blocked by the local RF->INET callsign-prefix gate (g_config.rf2inet_prefix_en/rf2inet_prefixes, see aprs_filter_prefix_match()).
                           */
    DROP_BUDLIST,         /**< Blocked by the local callsign whitelist/blacklist (see aprs_filter_budlist_pass()). */
    DROP_TX_FAIL,         /**< APRS-IS TX attempted but the socket wasn't connected / the write failed (IGate). */
    DROP_HEADER_OVERFLOW, /**< IGate RF->INET header build overflowed its buffer (excessively long repeater path). */
    DROP_PLACEHOLDER_CALL,      /**< RX frame's source callsign is the NOCALL/MYCALL sentinel (radio not configured / digipeater misconfigured), checked
                                   unconditionally at RX regardless of digi_en. */
    DROP_MODEM_NOT_READY,       /**< RF TX attempted before the audio modem finished bring-up. */
    DROP_TX_QUEUE_FULL,         /**< RF TX ring already holds "TX buffers" pending frames; new frame discarded instead of queued. */
    DROP_TX_TOO_LONG,           /**< Outgoing TNC2 packet longer than the modem's frame buffer. */
    ERR_MODEM_SEND_FAIL,        /**< modem_send_tnc2() itself returned an error transmitting an RF frame. */
    ERR_AX25_DECODE,            /**< RX frame failed to decode as a valid APRS (UI, no-layer-3) AX.25 frame. */
    DROP_DIGI_MALFORMED,        /**< Digipeater: frame too short to carry a destination / usable path. */
    DROP_DIGI_PLACEHOLDER_CALL, /**< Digipeater: source callsign is the NOCALL/MYCALL sentinel. */
    DROP_DIGI_ALREADY_USED,     /**< Digipeater: path already carries this digipeater's call marked used ('*'). */
    DROP_DIGI_PATH_FULL,  /**< Digipeater: path already at the AX.25 maximum (8) repeater addresses; inserting our call would overflow rpt_list/rpt_flags. */
    DROP_DIGI_NO_PATH,    /**< Digipeater: destination-SSID trace decoded to no usable WIDEn-N path. */
    DROP_DIGI_PATH_TOKEN, /**< Digipeater: path carries qA or TCP (already gated, not for RF repeat). */
    DROP_REASON_COUNT
} drop_reason_t;

typedef struct {
    uint32_t rxCount;   /**< Frames considered for gatewaying (RF->INET direction). */
    uint32_t txCount;   /**< Frames actually sent to APRS-IS as a result of gatewaying (RF->INET). */
    uint32_t dupCount;  /**< Duplicate frames suppressed. */
    uint32_t isRxCount; /**< ALL packets received from APRS-IS (every non-keepalive line read off the socket), regardless of inet2rf being enabled or the line
                           being relayed. Superset of what reaches the inet2rf handler. */
    uint32_t isTxCount; /**< ALL packets sent to APRS-IS over sendToAprsIs(): gatewayed RF frames (also in @c txCount), outbound messages (igate_send_raw()) and
                           digi "beacon to internet" sends alike. */
    uint32_t dropByReason[DROP_REASON_COUNT]; /**< Per-reason drop counters. Replaces the old single aggregate dropCount field - see igate_stats_total_drop()
                                                 for the equivalent total. */
} igate_stats_t;

/**
 * @brief Sum of every "drop" (not "err") dropByReason[] bucket - the DROP
 * half of the dashboard's DROP/ERR tile. Excludes ERR_MODEM_SEND_FAIL and
 * ERR_AX25_DECODE, which igate_stats_total_err() covers instead, so the two
 * numbers the tile shows add up to exactly the Drop Breakdown table's sum
 * with nothing counted twice.
 * @param s Stats snapshot (e.g. from igate_get_stats()).
 * @return Total number of drops across all non-err reasons.
 */
static inline uint32_t igate_stats_total_drop(const igate_stats_t *s) {
    uint32_t total = 0;
    for (int i = 0; i < DROP_REASON_COUNT; i++) {
        if (i == ERR_MODEM_SEND_FAIL || i == ERR_AX25_DECODE)
            continue;
        total += s->dropByReason[i];
    }
    return total;
}

/**
 * @brief Sum of every "err" dropByReason[] bucket (ERR_MODEM_SEND_FAIL,
 * ERR_AX25_DECODE) - the ERR half of the dashboard's DROP/ERR tile.
 * @param s Stats snapshot (e.g. from igate_get_stats()).
 * @return Total number of errors across both err reasons.
 */
static inline uint32_t igate_stats_total_err(const igate_stats_t *s) {
    return s->dropByReason[ERR_MODEM_SEND_FAIL] + s->dropByReason[ERR_AX25_DECODE];
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
 * @brief Start the IGate service task (APRS-IS TCP client with auto-reconnect,
 * login, RX line pump). No-op if g_config.igate_en is false. Safe to call once
 * from app startup; re-reads g_config each reconnect so web-admin changes take
 * effect after the next reconnect cycle.
 */
void igate_start(void);

/**
 * @brief Stop the IGate service task and close the APRS-IS connection.
 */
void igate_stop(void);

/**
 * @brief Feed one RF-decoded AX.25 frame to the gateway (RF -> INET direction).
 * Applies the RFONLY/TCPIP/qA/NOGATE/satellite-gate filters and the
 * g_config.rf2inetFilter payload-type whitelist (see aprs_filter.h), builds
 * the TNC2 text line with qAR/qAO path, de-duplicates, and forwards it to
 * APRS-IS if connected.
 * @return 1 if forwarded, 0 if dropped/duplicate/not connected.
 */
int igateProcess(ax25_msg_t *packet);

/**
 * @brief Duplicate-packet check only (exposed for reuse by other components,
 * e.g. digipeater wanting to avoid re-announcing the same frame).
 * @param packet Decoded frame to test.
 * @return true if @p packet matches a recently seen frame (a duplicate).
 */
bool isDuplicatePacket(ax25_msg_t *packet);

/**
 * @brief Drop entries older than ::DUP_PACKET_TIMEOUT_MS from the
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

#endif // IGATE_H
