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
 * @brief APRS query responder (APRS101 ch.15): recognizes broadcast
 * ("?APRS?", "?WX?", "?IGATE?") and directed ("CALL:?query?") queries in
 * received traffic and transmits the matching response.
 *
 * Configuration comes from g_config (app_config_t, web admin "Query" page).
 */

#ifndef QUERY_H
#define QUERY_H

#include <stddef.h>
#include <stdint.h>

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
 * @param handler Callback invoked with the built packet, its length and the
 *                channel bitmask.
 */
void query_set_tx_handler(void (*handler)(const char *packet, size_t len, uint8_t channels));

/**
 * @brief Feed a received, already-decoded TNC2 line ("SRC>DST,PATH:INFO") to
 * the query responder. No-ops immediately unless g_config.query_en is set
 * and the info field is a recognized broadcast query (info[0] == '?').
 *
 * Directed queries ("CALL:?query?") are NOT reached through this entry
 * point: they arrive already split out by message.c's own addressee parsing.
 * See query_process_directed().
 *
 * @param tnc2Line Decoded TNC2 text line, as handed to handleIncomingAPRS().
 */
void query_process(const char *tnc2Line);

/**
 * @brief Handle one directed query ("CALL:?query?"), called by
 * message.c's handleIncomingAPRS() when the addressed text starts with '?'
 * (rather than duplicating its own ":ADDRESSEE:" parsing here).
 *
 * No-ops unless g_config.query_en and g_config.query_directed_en are set and
 * @p toCall matches g_config.aprs_mycall (base callsign, SSID-insensitive).
 *
 * @param fromCall Source callsign of the querying station.
 * @param toCall   Addressee field of the directed query (already trimmed).
 * @param text     Query text after the addressee, starting with '?'.
 */
void query_process_directed(const char *fromCall, const char *toCall, const char *text);

#endif // QUERY_H
