/**
 * @file lastheard.h
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
 * @brief Small in-RAM table of decoded RF stations, used to feed the "LAST
 * HEARD" panel on the web dashboard.
 *
 * The table holds one entry per callsign, ordered most recently heard first.
 * Every decoded AX.25 frame from a callsign already in the table refreshes that
 * entry - time, path, symbol - and increments its packet counter, so the
 * dashboard can show how many times each station has been heard the same way
 * the reference project's "PACKET" column does, without a talkative station
 * pushing every other callsign out of the table.
 *
 * Thread-safe: lastheard_add() may be called from any task (radio RX callback).
 * Entries are timestamped with time(NULL) (wall clock, once NTP has synced) and
 * rendered as UTC so the web client can show a human time-of-day.
 */

#ifndef LASTHEARD_H
#define LASTHEARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the last-heard table. Must be called once (e.g. from
 * aprs_service_start()) before the first lastheard_add(). Safe to call more
 * than once.
 */
void lastheard_init(void);

/**
 * @brief Record one heard station.
 *
 * A callsign already in the table keeps its entry: @p path, @p sym_table,
 * @p sym_code and the timestamp are refreshed in place, its packet counter is
 * incremented, and the entry becomes the most recent one. An unknown callsign
 * takes a new entry at the front, evicting the least recently heard station
 * once the table is full.
 *
 * @param callsign   Source callsign, e.g. "HS5TQA-7" (already includes SSID
 *                    if non-zero).
 * @param path       Digipeat path as shown after the source call, e.g.
 *                    "WIDE1-1" or "DIRECT" (no leading/trailing comma).
 * @param via_rf     true if heard on RF, false if it only arrived via
 *                    APRS-IS (INET). Rendered as the "RF:"/"INET:" prefix on
 *                    the path column, matching the reference dashboard.
 * @param sym_table  APRS symbol table byte ('/' or '\' or overlay char), 0 if
 *                    unknown/not a position packet.
 * @param sym_code   APRS symbol code byte, 0 if unknown/not a position packet.
 */
void lastheard_add(const char *callsign, const char *path, bool via_rf, char sym_table, char sym_code);

/**
 * @brief Serialize the table (one element per station, most recent first) as a
 * JSON array.
 *
 * Each element looks like
 * @c {"time":"HH:MM:SSZ","call":"HS5TQA-7","path":"RF: WIDE1-1","sym":"91-1","packets":3}.
 * The trailing @c Z marks the time as UTC. The result is NUL-terminated and
 * truncated to fit @p out_size.
 *
 * @param out      Destination buffer.
 * @param out_size Size of @p out, in bytes.
 * @return Number of bytes written (excluding the NUL), or 0 on error.
 */
size_t lastheard_dump_json(char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif // LASTHEARD_H
