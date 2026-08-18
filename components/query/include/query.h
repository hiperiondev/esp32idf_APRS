/**
 * @file query.h
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
 * @brief APRS query responder (APRS101 ch.15): recognizes the general
 * ("?APRS?", "?WX?", "?IGATE?", "?QRU?") and directed ("CALL:?query?")
 * queries in received traffic and transmits the matching response.
 *
 * "?QRU?" answers with the QRU group-membership roll call: the tag/name of
 * each locally-configured Object/Item (objects_items.h) that carries a
 * non-empty QRU tag.
 *
 * The directed set additionally covers "?APRSD" (stations heard direct),
 * "?APRSH" (18-hour heard graph for one station), "?APRSM" (re-send this
 * station's pending messages for the querying operator), "?APRSO"
 * (re-announce the Objects/Items originated here), "?APRSP" (position),
 * "?APRSS" (status) and "?APRST" / "?PING?" (the route the query took).
 *
 * Configuration comes from g_config (app_config_t, web admin "Query" page).
 *
 * Every query carries the ::query_source_t it was received on, and that source
 * decides two things: whether the query is answered at all
 * (@c g_config.query_rf for RF, @c g_config.query_inet for APRS-IS) and where
 * the answer goes - an RF question is answered on RF, an APRS-IS question is
 * answered to APRS-IS. A broadcast query relayed by the APRS-IS backbone
 * therefore cannot key the transmitter of a station whose APRS-IS source
 * switch is off, which is the factory setting.
 *
 * Receiving a query and answering it are split across two tasks. The entry
 * points below only parse, rate-limit and record what to answer; the answer
 * itself is built and transmitted later by ::query_service, which the beacon
 * scheduler task calls on each pass. Building an answer walks the same deep
 * (float-formatting, TNC2/AX.25 encoding) call tree the periodic beacons do,
 * and that task is the one whose stack is budgeted for it - see
 * beacon_scheduler.h. A queued request also wakes the scheduler, so deferring
 * costs a task switch rather than a wait for its next scheduled pass.
 */

#ifndef QUERY_H
#define QUERY_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Where a query reached this station, passed to both entry points
 * below.
 *
 * The source is what the operator's two source switches select on
 * (@c g_config.query_rf / @c g_config.query_inet), and it is also the channel
 * the answer is transmitted on: a question heard off the air is answered on
 * the air, a question read from the APRS-IS feed is answered to APRS-IS.
 * Keeping the two together is what makes internet traffic unable to key the
 * transmitter on its own - a general query such as "?APRS?" is ordinary
 * backbone traffic, and an IGate sees a steady stream of it.
 */
typedef enum {
    QUERY_SRC_RF = 0, /**< Heard off the air, decoded by the local radio. */
    QUERY_SRC_INET,   /**< Read from the APRS-IS feed. */
    QUERY_SRC_COUNT   /**< Number of sources; sizes the per-source rate-limit table, never used as a source itself. */
} query_source_t;

/**
 * @brief Initialize the query responder (rate-limit timers). Call once at
 * startup, after message_init()/igate_start(), before aprs_service_start()
 * finishes wiring the RX dispatch chain.
 */
void query_init(void);

/**
 * @brief Register the TX handler used to actually transmit a built response
 * packet. Signature matches message_set_tx_handler() so aprs_service.c can
 * reuse the same messageTxHandler() RF+INET plumbing (see ::MSG_CHANNEL_RF /
 * ::MSG_CHANNEL_INET in message.h).
 *
 * The bitmask handed to the callback always carries exactly one channel, the
 * one matching the ::query_source_t the query arrived on.
 *
 * @param handler Callback invoked with the built packet, its length and the
 *                channel bitmask.
 */
void query_set_tx_handler(void (*handler)(const char *packet, size_t len, uint8_t channels));

/**
 * @brief Feed a received, already-decoded TNC2 line ("SRC>DST,PATH:INFO") to
 * the query responder. No-ops immediately unless g_config.query_en is set, the
 * switch for @p source is on, and the info field is a recognized broadcast
 * query (info[0] == '?').
 *
 * Cheap enough for the task that decoded the frame: it matches the keyword,
 * applies the per-type rate limit and, if an answer is due, queues it for
 * ::query_service. Nothing is built or transmitted here.
 *
 * "?APRS?" additionally recognizes APRS101 ch.15's area-restricted form,
 * "?APRS?LLLLLL,OOOOOO,RRRR" (signed lat/lon in hundredths of a degree, range
 * in miles): when that suffix is present and this station's own position is
 * configured (g_config.my_lat/my_lon), the query is answered only if this
 * station falls within the named circle. A plain "?APRS?", or an area query
 * received before this station's own position is known, is answered as
 * before.
 *
 * Directed queries ("CALL:?query?") are NOT reached through this entry
 * point: they arrive already split out by message.c's own addressee parsing.
 * See query_process_directed().
 *
 * @param tnc2Line Decoded TNC2 text line, as handed to handleIncomingAPRS().
 * @param source   Where the line was received; gates the answer and selects
 *                 the channel it is transmitted on.
 */
void query_process(const char *tnc2Line, query_source_t source);

/**
 * @brief Handle one directed query ("CALL:?query?"), called by
 * message.c's handleIncomingAPRS() when the addressed text starts with '?'
 * (rather than duplicating its own ":ADDRESSEE:" parsing here).
 *
 * No-ops unless g_config.query_en and g_config.query_directed_en are set, the
 * switch for @p source is on, and @p toCall matches g_config.aprs_mycall (base
 * callsign, SSID-insensitive).
 *
 * @param fromCall Source callsign of the querying station.
 * @param toCall   Addressee field of the directed query (already trimmed).
 * @param text     Query text after the addressee, starting with '?'. Any
 *                 argument (e.g. the callsign a "?APRSH" asks about) follows
 *                 the keyword in this same string.
 * @param tnc2Line The whole received TNC2 line the query arrived on, used to
 *                 report the route back for "?APRST" / "?PING?". Read here,
 *                 while it is still in scope, and only the route is kept. May
 *                 be NULL, in which case a traceroute answers with an unknown
 *                 route.
 * @param source   Where the query was received; gates the answer and selects
 *                 the channel it is transmitted on.
 */
void query_process_directed(const char *fromCall, const char *toCall, const char *text, const char *tnc2Line, query_source_t source);

/**
 * @brief Build and transmit every answer queued by the entry points above,
 * then return.
 *
 * Called by the beacon scheduler task at the start of each pass - and that
 * task only, since this is where the beacon builders, the float formatting and
 * the TNC2/AX.25 encode chain run and its stack is the one sized for them.
 * Returns immediately when nothing is queued, so it is free to call every
 * pass.
 *
 * Requests are answered oldest first, with the queue unlocked across each
 * build and transmission so incoming traffic can keep queuing meanwhile. A
 * question already waiting to be answered is not queued twice: every answer
 * reports live state at the moment it is sent.
 */
/**
 * @brief Transmit the periodic Station Capabilities beacon when it is due, and
 * report how many seconds until it next needs servicing.
 *
 * APRS101 chapter 15 allows a station to send its capabilities line at any
 * time, not only in reply to "?IGATE?", so that neighbours learn a gateway
 * exists without having to ask. The beacon is off by default and gated on
 * @c g_config.query_cap_beacon_en together with @c g_config.igate_en, with its
 * own interval and its own RF/APRS-IS channel selection
 * (@c g_config.query_cap_rf / @c g_config.query_cap_inet).
 *
 * Called by the beacon scheduler task on each pass, alongside the other
 * periodic originators and for the same reason: the line is built and
 * transmitted through the TNC2/AX.25 chain that task's stack is sized for.
 *
 * @return Seconds until this function next needs to be called.
 */
uint32_t query_capabilities_service(void);

void query_service(void);

#endif // QUERY_H
