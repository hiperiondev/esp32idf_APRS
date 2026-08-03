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
 * The table holds one entry per callsign, ordered most recently heard first,
 * and remembers whether each station's latest frame arrived without passing
 * through a digipeater - which is what the "?APRSD" query responder reports.
 * Every decoded AX.25 frame from a callsign already in the table refreshes that
 * entry - time, path, symbol - and increments its packet counter, so the
 * dashboard can show how many times each station has been heard the same way
 * the reference project's "PACKET" column does, without a talkative station
 * pushing every other callsign out of the table.
 *
 * Each entry also keeps an 18-slot hourly histogram of packets heard from that
 * station, most recent clock hour first, which is what APRS101 chapter 15
 * defines as the answer to the "?APRSH" query.
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
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Number of hourly slots kept per station for the "?APRSH" heard
 * history, matching the 18-hour graph APRS101 chapter 15 defines.
 */
#define LASTHEARD_HEARD_HOURS 18

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
 * Every call also advances that station's hourly histogram: any clock hour
 * that has elapsed since the entry was last touched is rolled into the
 * histogram as a zero count, then the current hour's slot is incremented, so
 * ::lastheard_heard_history always reflects real elapsed wall-clock hours
 * rather than just call counts.
 *
 * @param callsign   Source callsign, e.g. "HS5TQA-7" (already includes SSID
 *                    if non-zero).
 * @param path       Digipeat path as shown after the source call, e.g.
 *                    "WIDE1-1" or "DIRECT" (no leading/trailing comma).
 * @param via_rf     true if heard on RF, false if it only arrived via
 *                    APRS-IS (INET). Rendered as the "RF:"/"INET:" prefix on
 *                    the path column, matching the reference dashboard, and
 *                    recorded per station (from its most recent frame) so
 *                    lastheard_station_count() can tell the locally heard
 *                    stations from the ones the APRS-IS feed contributed.
 * @param direct     true if the frame reached this station without having
 *                    been repeated by any digipeater. Recorded per station
 *                    (from its most recent frame) and reported by
 *                    lastheard_directs(); an INET frame is never direct.
 * @param sym_table  APRS symbol table byte ('/' or '\' or overlay char), 0 if
 *                    unknown/not a position packet.
 * @param sym_code   APRS symbol code byte, 0 if unknown/not a position packet.
 */
void lastheard_add(const char *callsign, const char *path, bool via_rf, bool direct, char sym_table, char sym_code);

/**
 * @brief How many stations the table currently holds.
 *
 * This is a live figure, not a running total: it is the size of the heard
 * list at the moment of the call, and it stops growing once the table is full
 * (a new station then evicts the least recently heard one). It answers the
 * @c LOC_CNT half of the "?IGATE?" Station Capabilities line APRS101
 * chapter 15 defines.
 *
 * @param rf_only true to count only the stations whose most recent frame was
 *                heard off the air, which is what "local" means for an IGate;
 *                false to count every row, including the ones fed in from
 *                APRS-IS.
 * @return Number of stations, 0 to @c LASTHEARD_CAPACITY.
 */
size_t lastheard_station_count(bool rf_only);

/**
 * @brief Build the space-separated list of stations most recently heard
 * without any digipeater in between, most recent first.
 *
 * This is the station list an APRS "?APRSD" directed query asks for
 * (APRS101 chapter 15). Callsigns are appended whole - a callsign that would
 * not fit the remaining room ends the list rather than being truncated, so
 * the result never contains a partial callsign.
 *
 * @param out      Destination buffer, NUL-terminated on return (empty if no
 *                 station has been heard directly).
 * @param out_size Size of @p out, in bytes.
 * @return Number of callsigns written.
 */
int lastheard_directs(char *out, size_t out_size);

/**
 * @brief Look one station up in the table.
 *
 * Answers the APRS "?APRSH" directed query (APRS101 chapter 15), which asks
 * whether - and how much - a given station has been heard here.
 *
 * @param callsign Station to look up, matched exactly against the stored
 *                 (SSID-bearing) callsign.
 * @param packets  Out: how many frames from that station have been counted.
 *                 Untouched when the station is unknown. May be NULL.
 * @param last     Out: wall-clock time of its most recent frame. Untouched
 *                 when the station is unknown. May be NULL.
 * @param direct   Out: true if its most recent frame arrived without any
 *                 digipeater. Untouched when the station is unknown. May be
 *                 NULL.
 * @return true if the station is in the table.
 */
bool lastheard_lookup(const char *callsign, uint32_t *packets, time_t *last, bool *direct);

/**
 * @brief Fetch one station's hourly heard histogram.
 *
 * Answers the graph part of the APRS "?APRSH" directed query (APRS101 ch.15):
 * "Hrd: <packets in hour 0> <hour 1> ... <hour 17>", where hour 0 is the
 * current (most recent) clock hour and hour 17 is 17 hours before it. Hours
 * with no traffic from the station read as 0, including any hour that
 * elapsed while the table held no entry for it yet.
 *
 * @param callsign Station to look up, matched exactly against the stored
 *                 (SSID-bearing) callsign.
 * @param out      Out: @c LASTHEARD_HEARD_HOURS counts, index 0 = current
 *                 hour. Untouched when the station is unknown. Must not be
 *                 NULL.
 * @return true if the station is in the table.
 */
bool lastheard_heard_history(const char *callsign, uint16_t out[LASTHEARD_HEARD_HOURS]);

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
