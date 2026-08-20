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
 * The table holds one entry per callsign - upper-cased on the way in, so case
 * alone never splits a station across entries - ordered most recently heard
 * first, and remembers whether each station's latest frame arrived without passing
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
 * Entries are timestamped with time(NULL) and rendered as UTC so the web client
 * can show a human time-of-day. A frame heard before the first SNTP sync has no
 * time of day to show - time() is still counting from the epoch - so it is
 * stored unstamped and serialized with an empty time field rather than as a
 * time near midnight; the same applies to the hourly histogram, which does not
 * roll until the clock is real (see ::lastheard_heard_history).
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
 * rather than just call counts. Hours can only be counted once the wall clock
 * has been set, so frames that arrive before the first SNTP sync are all
 * accumulated into the current-hour slot and the first frame seen afterwards
 * adopts that slot as the hour it is then in.
 *
 * @param callsign   Source callsign, e.g. "HS5TQA-7" (already includes SSID
 *                    if non-zero). Stored upper-cased and matched without
 *                    regard to case, so a station whose frames arrive with
 *                    different spellings occupies one entry.
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
 * @param used_hops  Number of digipeater addresses in the frame's path whose
 *                    AX.25 "has been repeated" bit was set, i.e. how many
 *                    hops the frame actually took to reach this receiver. 0
 *                    for a direct frame. Recorded per station from its most
 *                    recent RF frame - an APRS-IS frame leaves the stored
 *                    count untouched, since it describes no RF path - and
 *                    used by lastheard_station_count() and
 *                    lastheard_heard_rf_within_hops() to test path
 *                    reachability. Ignored when @p via_rf is false.
 * @param sym_table  APRS symbol table byte ('/' or '\' or overlay char), 0 if
 *                    unknown/not a position packet.
 * @param sym_code   APRS symbol code byte, 0 if unknown/not a position packet.
 */
void lastheard_add(const char *callsign, const char *path, bool via_rf, bool direct, uint8_t used_hops, char sym_table, char sym_code);

/**
 * @brief How many stations the table currently holds.
 *
 * This is a live figure, not a running total: it is the size of the heard
 * list at the moment of the call, and it stops growing once the table is full
 * (a new station then evicts the least recently heard one). It answers the
 * @c LOC_CNT half of the "?IGATE?" Station Capabilities line APRS101
 * chapter 15 defines: "the number of stations heard with this number of used
 * digipeater addresses or fewer", i.e. stations within the IGate's own
 * transmit path in hops, not merely everything decoded off the air.
 *
 * @param rf_only  true to count only the stations whose most recent frame was
 *                 heard off the air, which is what "local" means for an
 *                 IGate; false to count every row, including the ones fed in
 *                 from APRS-IS (in which case @p max_used_hops is ignored,
 *                 since an APRS-IS-sourced row carries no RF hop count).
 * @param max_used_hops When @p rf_only is true, a station counts only if the
 *                 used-hop count of its most recent RF frame (see
 *                 lastheard_add()'s @c used_hops) is at or below this value -
 *                 the reach, in hops, of the configured IGate TX path. Pass
 *                 @c UINT8_MAX to count every RF station regardless of path
 *                 length.
 * @return Number of stations, 0 to @c LASTHEARD_CAPACITY.
 */
size_t lastheard_station_count(bool rf_only, uint8_t max_used_hops);

/**
 * @brief Was this station heard on the local RF channel within the last
 * @p seconds?
 *
 * Part of the locality test an IGate applies before putting a message read from
 * APRS-IS on the air: a message whose sender is itself audible locally was
 * already transmitted on this channel, so gating the copy would echo it. The
 * addressee side of the same test asks the stricter question
 * ::lastheard_heard_rf_within_hops answers, since being audible and being
 * reachable are not the same thing. The stamp this reads is refreshed by every
 * RF frame from the station, independently of the Internet stamp
 * ::lastheard_heard_inet_within reads, so a station that is both audible
 * locally and present on the APRS-IS feed answers true to both.
 *
 * @param callsign Station to look up, matched without regard to case against
 *                 the stored (SSID-bearing) callsign.
 * @param seconds  Width of the window, counted back from now.
 * @return true if the station is in the table and its most recent RF frame
 *         falls inside the window. A station the table does not hold, or one
 *         only ever seen via APRS-IS, answers false.
 */
bool lastheard_heard_rf_within(const char *callsign, uint32_t seconds);

/**
 * @brief Was this station heard on the local RF channel within the last
 * @p seconds, over a path of at most @p max_used_hops digipeater hops?
 *
 * The reachability form of ::lastheard_heard_rf_within: an IGate transmits
 * with a path of its own, and a station whose frames only arrive after several
 * digipeater hops sits outside what that path can reach back, however recently
 * it was decoded. The IGate design notes therefore measure the coverage area in
 * digipeater hops rather than in time alone, and ask that a gateway be set to
 * the minimum number of hops it needs. This is the test the INET->RF message
 * gate applies to an addressee.
 *
 * The hop count read here is the one carried by the station's most recent RF
 * frame (see lastheard_add()'s @c used_hops), so a station that answers true
 * was reachable over that many hops at the time of the stamp this also tests.
 *
 * @param callsign      Station to look up, matched without regard to case
 *                      against the stored (SSID-bearing) callsign.
 * @param seconds       Width of the window, counted back from now.
 * @param max_used_hops Longest path accepted, in used digipeater addresses.
 *                      0 accepts only a station heard direct; @c UINT8_MAX
 *                      accepts any path, making this identical to
 *                      ::lastheard_heard_rf_within.
 * @return true if the station is in the table, its most recent RF frame falls
 *         inside the window, and that frame took at most @p max_used_hops
 *         digipeater hops to arrive. A station the table does not hold, or one
 *         only ever seen via APRS-IS, answers false.
 */
bool lastheard_heard_rf_within_hops(const char *callsign, uint32_t seconds, uint8_t max_used_hops);

/**
 * @brief Was this station seen on the Internet side within the last
 * @p seconds?
 *
 * The other half of the same test: a station that is itself Internet-connected
 * already has anything addressed to it, so gating a message to it onto RF only
 * duplicates what it has. A station counts as seen on the Internet when its
 * frame arrived over the APRS-IS feed, and also when a frame heard off the air
 * carries @c TCPIP or @c TCPXX in its path - the on-air signature of a packet
 * that has already passed through a gateway.
 *
 * @param callsign Station to look up, matched without regard to case against
 *                 the stored (SSID-bearing) callsign.
 * @param seconds  Width of the window, counted back from now.
 * @return true if the station is in the table and its most recent Internet
 *         sighting falls inside the window.
 */
bool lastheard_heard_inet_within(const char *callsign, uint32_t seconds);

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
 * @brief Fetch one station's hourly heard histogram.
 *
 * Answers the graph part of the APRS "?APRSH" directed query (APRS101 ch.15):
 * "Hrd: <packets in hour 0> <hour 1> ... <hour 17>", where hour 0 is the
 * current (most recent) clock hour and hour 17 is 17 hours before it. Hours
 * with no traffic from the station read as 0, including any hour that
 * elapsed while the table held no entry for it yet.
 *
 * While the wall clock is unset (no SNTP sync since boot) no hour can be
 * identified, so the whole graph is the running count of everything heard from
 * the station since boot, in slot 0, and the remaining slots read 0. The counts
 * are kept when the clock is finally set: slot 0 becomes the hour of the first
 * frame seen afterwards and the graph ages normally from there.
 *
 * @param callsign Station to look up, matched without regard to case against
 *                 the stored (SSID-bearing) callsign.
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
 * The trailing @c Z marks the time as UTC. A station last heard before the
 * clock was set carries an empty @c time string instead, since its stamp names
 * no time of day; every other field is present as usual. The result is
 * NUL-terminated and truncated to fit @p out_size.
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
