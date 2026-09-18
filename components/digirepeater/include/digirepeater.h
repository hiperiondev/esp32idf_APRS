/**
 * @file digirepeater.h
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
 * @brief APRS digipeater component, implementing the New n-N Paradigm.
 *
 * Pure C, ESP-IDF. Reads its callsign/SSID, its alias table
 * (::app_config_t::digi_alias) and its policy settings - fill-in-only, what to
 * do with a trapped hop count, whether the legacy destination-SSID convention
 * is honoured and whether explicit routes are served preemptively - from
 * g_config (app_config_t, see
 * main/include/app_config.h), so the web admin's "Digi" page is the single
 * source of truth for configuration.
 *
 * The component recognises no aliases of its own. Every alias it honours is a
 * row of the operator's table, and a row says how that alias is repeated:
 * ::DIGI_ALIAS_TRACE inserts this station's callsign so the hop is
 * identifiable afterwards, ::DIGI_ALIAS_FLOOD decrements the hop count without
 * saying who did it. The factory table traces @c WIDE1-1, @c WIDE2-2 and the
 * rest of the @c WIDEn family, and traps anything asking for more than two
 * hops.
 *
 * Explicit routes that name individual digipeaters, such as
 * @c WIDE1-1,CITYA,WIDE2-1,CITYB, are served by the preemptive scan described
 * under ::app_config_t::digi_preempt, which is off by default.
 */

#ifndef DIGIREPEATER_H
#define DIGIREPEATER_H

#include "ax25.h"

/**
 * @brief Process one received AX.25 frame through the digipeater path logic.
 *
 * The first unused address in the path decides everything unless preemptive
 * digipeating is switched on: it is matched against the alias table, and only
 * if it names an alias this station honours is the frame repeated.
 * ::app_config_t::digi_preempt widens that to a scan running from the first
 * unused address to the end of the path, looking for this station's own
 * callsign or one of its fixed (non @c n-N) alias rows; a match found further
 * down claims the frame, the addresses jumped over are either marked used or
 * discarded, and everything behind the match stays live.
 *
 * A hop count larger than the matched alias row's @c max_n is
 * trapped - clamped down to the limit, or refused outright, per
 * @c g_config.digi_trap_n_clamp. A frame that already carries this station's
 * callsign marked used is refused whatever its path still holds, and so is one
 * whose source address and information field match a frame repeated inside the
 * duplicate-suppression window (g_config.dup_cache_timeout_ms, see
 * isDuplicatePacketScoped()) - the two together are what stop digipeaters
 * within earshot of each other from re-repeating the same transmission.
 *
 * @param packet Decoded frame (as produced by ax25_decode()). Modified in place
 *               when the path needs to be rewritten (hop-count decrement,
 *               callsign insertion, etc).
 *
 * @return 0  - do not repeat (drop / duplicate / not for us / already relayed)
 *         1  - repeat as-is (path already carries our used call, e.g. bypass "*")
 *         2  - repeat with modified path (packet.info + rewritten header must be
 *              re-encoded and transmitted on RF)
 */
int digiProcess(ax25_msg_t *packet);

#endif // DIGIREPEATER_H
