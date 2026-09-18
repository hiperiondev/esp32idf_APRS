// @file lastheard.c
//
// @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
// @date 2026
// @copyright GNU General Public License v3
// @see https://github.com/hiperiondev/esp32idf_APRS
//
// @note
// This is based on other projects:
//     VP-Digi: https://github.com/sq8vps/vp-digi
//     ESP32APRS: https://github.com/nakhonthai/ESP32APRS_Audio
//     LibAPRS: https://github.com/markqvist/LibAPRS
//
//     please contact their authors for more information.
//
// @brief In-RAM table of decoded stations feeding the dashboard "LAST HEARD"
// panel: one entry per callsign, most recently heard first, with thread-safe
// insertion, per-callsign packet counting, an 18-hour heard histogram for the
// "?APRSH" query, UTC timestamping and JSON serialization.

#include "lastheard.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "json_escape.h" // json_escape()

#define LASTHEARD_CALL_LEN 12
#define LASTHEARD_PATH_LEN 48

// Wall-clock sanity floor (2020-09-13), the same threshold the rest of the
// firmware tests. time() below this means SNTP has not synced since boot, so
// the value is an uptime counter rather than a date: no epoch hour number can
// be derived from it, and no time of day can be shown for it.
#define LASTHEARD_CLOCK_VALID_EPOCH 1600000000LL

// One heard station. The fields are grouped widest-first - the wall-clock
// stamps, then the 32-bit counters, then the arrays, then the single bytes -
// so the row packs with one byte of tail padding instead of scattering holes
// between members. The table is LASTHEARD_CAPACITY of these in .bss, so every
// byte of padding here is paid for LASTHEARD_CAPACITY times over.
typedef struct {
    // Per-channel last-heard stamps, kept alongside the whole-entry time
    // because the two answer different questions. time is when the station was
    // last heard at all, which is what the dashboard shows; the other two are
    // when it was last heard on each channel, which is what the INET->RF
    // message gate needs - a station can be locally audible and
    // Internet-connected at once, and the gate tests each independently.
    // 0 means never heard that way.
    //
    // rf_used_hops belongs to rf_time the same way: it describes the frame that
    // set that stamp, so the hop-limited query reads the path length of the
    // reception it is testing the age of, even when a later APRS-IS sighting
    // has already moved the entry to the front of the table.
    time_t time;
    time_t rf_time;
    time_t inet_time;
    // Epoch hour number hourly[0] belongs to, so lastheard_add() can tell how
    // many whole hours to roll the histogram forward on the next frame from
    // this station. An hour number is a fifth of the range of the seconds it
    // is derived from, so 32 bits hold every hour a wall clock can name for
    // the next few hundred thousand years and the field costs half of what the
    // time_t it comes from would.
    uint32_t hour_slot;
    uint32_t packets; // total times this callsign has been heard
    // Hourly heard histogram for the "?APRSH" query (APRS101 ch.15): hourly[0]
    // is the current clock hour, hourly[LASTHEARD_HEARD_HOURS - 1] the oldest.
    //
    // histogram_valid says whether hour_slot holds an hour number at all. It is
    // a field of its own rather than a reserved hour_slot value because 0 is a
    // perfectly real epoch hour: it is the one every frame carries during the
    // first hour after boot, before SNTP steps the clock. Until it is set,
    // hourly[0] is a provisional bucket holding every frame heard so far, and
    // no rolling is attempted.
    uint16_t hourly[LASTHEARD_HEARD_HOURS];
    char callsign[LASTHEARD_CALL_LEN];
    char path[LASTHEARD_PATH_LEN]; // e.g. "RF: WIDE1-1" / "INET: DIRECT"
    char sym_table;
    char sym_code;
    uint8_t rf_used_hops; // digipeater addresses actually repeated in the latest RF frame
    bool via_rf;          // latest frame from this station was heard off the air
    bool via_bm;          // latest frame from this station was BrandMeister traffic gated onto APRS-IS
    bool direct;          // latest frame from this station carried no used digipeater
    bool histogram_valid; // hour_slot names a real clock hour
} lastheard_entry_t;

// The grouping above is a sizing decision, not a style one, so it is stated as
// a compile-time test rather than trusted to survive the next edit: the row may
// cost the sum of its members plus the tail padding its widest member forces,
// and nothing else. Adding a field of a different width in the middle of the
// byte-sized group at the end is what would break it, and the diagnostic points
// straight at the fix.
_Static_assert(sizeof(lastheard_entry_t) <= 3 * sizeof(time_t) + 2 * sizeof(uint32_t) + LASTHEARD_HEARD_HOURS * sizeof(uint16_t) + LASTHEARD_CALL_LEN +
                                                LASTHEARD_PATH_LEN + 6 + sizeof(time_t),
               "lastheard_entry_t has gained internal padding: group members widest-first");

// One slot per STATION, never per packet: s_buf[0] is the most recently heard
// callsign and s_count grows to LASTHEARD_CAPACITY, at which point the oldest
// station falls off the end. Hearing a callsign that already has a slot
// refreshes that slot and moves it back to the front rather than consuming a
// new one, so a single chatty neighbour (or this station's own digipeated
// traffic) cannot evict every other callsign from the table within seconds -
// which is exactly the information the dashboard panel exists to show.
static lastheard_entry_t s_buf[LASTHEARD_CAPACITY];
static size_t s_count = 0; // live entries, s_buf[0..s_count-1], most recent first
static SemaphoreHandle_t s_lock = NULL;
static bool s_inited = false;

// Roll one entry's hourly histogram forward to the current clock hour, and
// optionally count one packet into it. A brand-new entry (histogram_valid false,
// from the cleared row the insertion path builds) starts with a histogram of all
// zero except the current hour. A station heard again inside the same clock hour
// it was last heard in just adds to hourly[0].
// A station heard again after N whole hours have passed shifts the histogram
// right by N slots (zero-filling the hours that had no traffic) before counting
// this packet, so a station that goes briefly silent still reads as silent for
// those hours rather than skipping straight to "last heard" and hiding the gap.
//
// count_packet separates the two callers this has. The insertion path passes
// true: a frame just arrived and belongs in the current hour. The reader path
// passes false: it rolls the histogram only so the answer reflects the hours
// that have elapsed since the last frame, and reading the graph is not itself
// traffic. Keeping the two apart means the counter is only ever bumped by a
// real packet, so a station sitting at UINT16_MAX for the current hour - where
// the saturation guard below stops counting - is not silently drained by every
// query that reads it.
//
// Rolling means "shift by the number of whole hours elapsed since hour_slot",
// which only has an answer once the wall clock is real. Before the first SNTP
// sync time() counts from the epoch, so the whole roll is skipped and every
// frame heard in that window lands in hourly[0] - one provisional bucket the
// first frame seen under a synchronised clock adopts as the current hour. That
// keeps a station on a link with no route to an NTP server reading as heard
// rather than as a graph wiped on every frame.
static void rollHourlyHistogram(lastheard_entry_t *e, time_t now, bool count_packet) {
    if ((int64_t)now >= LASTHEARD_CLOCK_VALID_EPOCH) {
        uint32_t nowHour = (uint32_t)((int64_t)now / 3600);

        if (!e->histogram_valid) {
            // First frame recorded for this entry under a synchronised clock.
            // Whatever the provisional bucket already holds stays in hourly[0]:
            // those frames were heard, and the current hour is the nearest hour
            // that can honestly be claimed for them.
            e->hour_slot = nowHour;
            e->histogram_valid = true;
        } else if (nowHour > e->hour_slot) {
            uint32_t elapsed = nowHour - e->hour_slot;
            if (elapsed >= LASTHEARD_HEARD_HOURS) {
                // More than a full graph's worth of hours passed: every slot is
                // stale, so start clean rather than shifting in zeros one at a
                // time for no visible effect.
                memset(e->hourly, 0, sizeof(e->hourly));
            } else {
                memmove(&e->hourly[elapsed], &e->hourly[0], (LASTHEARD_HEARD_HOURS - elapsed) * sizeof(e->hourly[0]));
                memset(e->hourly, 0, elapsed * sizeof(e->hourly[0]));
            }
            e->hour_slot = nowHour;
        }
        // nowHour < e->hour_slot only if the wall clock stepped backwards (e.g.
        // an NTP correction); the histogram is left as-is rather than guessed
        // at.
    }

    if (count_packet && e->hourly[0] < UINT16_MAX)
        e->hourly[0]++;
}

// Build the lookup key for one callsign: truncated to the width the table
// stores and upper-cased. Every entry point in this file goes through it, so
// insertion and both readers agree on what "the same station" means.
//
// The two feeds that reach lastheard_add() hand over the callsign exactly as
// it arrived - the RF path decodes the shifted AX.25 address bytes, the
// APRS-IS path takes the raw TNC2 text - and neither guarantees the upper case
// callsigns are conventionally written in. Folding the case here is what keeps
// one station to one slot out of the LASTHEARD_CAPACITY available, so its
// packet count and its hourly histogram accumulate in a single row instead of
// being split between spellings, and the text the dashboard shows is uniform.
//
// dst is LASTHEARD_CALL_LEN bytes and always comes back NUL-terminated; src may
// be longer than the stored width, in which case it is cut to fit.
static void makeCallKey(char *dst, const char *src) {
    // Bound checked ahead of the dereference so the loop stays safe even if a
    // future caller ever hands in a src that is not NUL-terminated; both
    // current callers (AX.25 decode and APRS-IS text) already guarantee
    // termination, so this ordering costs nothing today and removes any
    // reliance on that guarantee holding forever.
    size_t i = 0;
    for (; i < LASTHEARD_CALL_LEN - 1 && src[i] != 0; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = 0;
}

void lastheard_init(void) {
    if (s_inited)
        return;
    memset(s_buf, 0, sizeof(s_buf));
    s_lock = xSemaphoreCreateMutex();
    s_inited = true;
}

void lastheard_add(const char *callsign, const char *path, bool via_rf, bool direct, uint8_t used_hops, char sym_table, char sym_code, bool via_bm) {
    if (!s_inited)
        lastheard_init();
    if (!callsign || !callsign[0])
        return;
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE)
        return; // never block the radio/network task indefinitely

    // Match on the same key the readers use, so two callsigns that differ only
    // past LASTHEARD_CALL_LEN or only in case still resolve to one slot.
    char call[LASTHEARD_CALL_LEN];
    makeCallKey(call, callsign);

    size_t found = LASTHEARD_CAPACITY;
    for (size_t i = 0; i < s_count; i++) {
        if (strcasecmp(s_buf[i].callsign, call) == 0) {
            found = i;
            break;
        }
    }

    // The row is assembled in a local and written to the front of the table
    // once, rather than being edited in place at s_buf[0] after the shift:
    // that slot still holds the station that was most recently heard before
    // this frame (the memmove moves rows down into the gap, it does not clear
    // the front), so every field carried over from a known station - its
    // packet count and its hourly histogram alike - travels with the row here
    // instead of having to be saved and restored one at a time.
    lastheard_entry_t entry;
    size_t shift_from;
    if (found < LASTHEARD_CAPACITY) {
        // Known station: its whole accumulated row moves to the front, and the
        // gap it leaves behind is closed by the shift below.
        entry = s_buf[found];
        if (entry.packets < UINT32_MAX)
            entry.packets++;
        shift_from = found;
    } else {
        // New station: a cleared row, which is also what tells
        // rollHourlyHistogram() to start a fresh histogram (histogram_valid
        // false, hourly all zero). Everything moves down one slot, dropping the
        // oldest station once the table is full.
        memset(&entry, 0, sizeof(entry));
        entry.packets = 1;
        if (s_count < LASTHEARD_CAPACITY)
            s_count++;
        shift_from = s_count - 1;
    }

    time_t now = time(NULL);
    rollHourlyHistogram(&entry, now, true);

    // The displayed stamp is only written from a synchronised clock: an uptime
    // counter rendered as a time of day would put "00:0x:xxZ" on the dashboard
    // next to a real reception, which reads as a genuine - and wrong - UTC
    // time rather than as the missing value it is. 0 means "heard, but at an
    // unknown time", and lastheard_dump_json() renders it as an empty field.
    // The per-channel stamps below keep taking the raw value: the gate tests
    // them as differences against the same clock, which stays meaningful
    // before a sync, and a pre-sync stamp reads as long expired afterwards.
    entry.time = ((int64_t)now >= LASTHEARD_CLOCK_VALID_EPOCH) ? now : 0;
    strncpy(entry.callsign, call, sizeof(entry.callsign) - 1);
    entry.callsign[sizeof(entry.callsign) - 1] = 0;
    // The channel prefix names where the frame came from, and BrandMeister is
    // a third answer to that question rather than a decoration on the second:
    // it is still the APRS-IS side, but the station behind it is reachable
    // over the network and not on the local channel, which is exactly what an
    // operator reading the table needs to know. Carrying it in the path string
    // keeps the /lastheard document's shape unchanged.
    const char *channel = via_rf ? "RF" : (via_bm ? "BM" : "INET");
    snprintf(entry.path, sizeof(entry.path), "%s: %s", channel, (path && path[0]) ? path : "DIRECT");
    entry.sym_table = sym_table;
    entry.sym_code = sym_code;
    // via_rf and direct describe the path this frame took, so the newest frame
    // always wins: a station that has moved out of direct range stops being
    // listed as direct as soon as its first digipeated frame arrives, and a
    // station last seen on the APRS-IS feed stops counting as locally heard.
    //
    // The hop count is written by RF frames only, so it keeps describing the
    // reception rf_time is the stamp of. An APRS-IS sighting carries no RF path
    // to measure, and letting it write a zero here would read back as "heard
    // direct" to the hop-limited query.
    entry.via_rf = via_rf;
    entry.direct = via_rf && direct;
    // Follows the newest frame like via_rf does, and an RF frame is never
    // BrandMeister traffic, so a station heard on the local channel loses the
    // mark on that frame alone.
    entry.via_bm = !via_rf && via_bm;
    if (via_rf)
        entry.rf_used_hops = used_hops;

    // The per-channel stamps accumulate rather than replace each other, so a
    // station present on both channels keeps both times and each one ages out
    // on its own.
    //
    // A frame heard off the air also counts as an Internet sighting when its
    // path carries TCPIP or TCPXX: that is what an already-gated packet looks
    // like on RF, and it is the signature the IGate specification defines as
    // "heard via the Internet". Everything the APRS-IS feed contributes is an
    // Internet sighting by construction, whatever its path reads.
    if (via_rf)
        entry.rf_time = now;
    else
        entry.inet_time = now;
    if (path != NULL && (strstr(path, "TCPIP") != NULL || strstr(path, "TCPXX") != NULL))
        entry.inet_time = now;

    if (shift_from > 0)
        memmove(&s_buf[1], &s_buf[0], shift_from * sizeof(s_buf[0]));
    s_buf[0] = entry;

    xSemaphoreGive(s_lock);
}

size_t lastheard_station_count(bool rf_only, uint8_t max_used_hops) {
    if (!s_inited)
        return 0;
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE)
        return 0;

    size_t count = 0;
    for (size_t i = 0; i < s_count; i++) {
        if (!s_buf[i].callsign[0])
            continue;
        if (rf_only && !s_buf[i].via_rf)
            continue;
        if (rf_only && s_buf[i].rf_used_hops > max_used_hops)
            continue;
        count++;
    }

    xSemaphoreGive(s_lock);
    return count;
}

// Shared body of the three window queries: look the station up under the stored
// key and test one of its per-channel stamps against the window. A station the
// table does not hold answers false, which is the safe answer for every caller
// - the message gate reads "not heard" as "do not transmit".
//
// The test is a difference against the current wall clock, so a stamp taken
// before the clock was set, or one left in the future by a backwards NTP
// correction, reads as outside the window instead of as arbitrarily recent.
//
// max_used_hops bounds the path length of the RF reception as well as its age;
// UINT8_MAX asks about the age alone. It applies to the RF stamp only, since an
// Internet sighting has no RF path to measure.
static bool heardWithin(const char *callsign, uint32_t seconds, bool rf, uint8_t max_used_hops) {
    if (callsign == NULL || callsign[0] == 0 || !s_inited)
        return false;
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE)
        return false;

    char call[LASTHEARD_CALL_LEN];
    makeCallKey(call, callsign);

    bool within = false;
    time_t now = time(NULL);
    for (size_t i = 0; i < s_count; i++) {
        if (strcasecmp(s_buf[i].callsign, call) != 0)
            continue;
        time_t stamp = rf ? s_buf[i].rf_time : s_buf[i].inet_time;
        bool hopsOk = !rf || s_buf[i].rf_used_hops <= max_used_hops;
        if (hopsOk && stamp != 0 && now >= stamp && (uint64_t)(now - stamp) <= (uint64_t)seconds)
            within = true;
        break;
    }

    xSemaphoreGive(s_lock);
    return within;
}

bool lastheard_heard_rf_within(const char *callsign, uint32_t seconds) {
    return heardWithin(callsign, seconds, true, UINT8_MAX);
}

bool lastheard_heard_rf_within_hops(const char *callsign, uint32_t seconds, uint8_t max_used_hops) {
    return heardWithin(callsign, seconds, true, max_used_hops);
}

bool lastheard_heard_inet_within(const char *callsign, uint32_t seconds) {
    return heardWithin(callsign, seconds, false, UINT8_MAX);
}

bool lastheard_last_seen_bm(const char *callsign) {
    if (callsign == NULL || callsign[0] == 0 || !s_inited)
        return false;
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE)
        return false;

    char call[LASTHEARD_CALL_LEN];
    makeCallKey(call, callsign);

    // No time window here, unlike heardWithin(): this reports which channel
    // the station was last seen on, not how recently. A station that has aged
    // out of every window is still one whose only known route is the network,
    // and routing a message to it over RF on the strength of a stale clock
    // would put a frame on the air for a station that was never on the local
    // channel to begin with.
    bool bm = false;
    for (size_t i = 0; i < s_count; i++) {
        if (strcasecmp(s_buf[i].callsign, call) != 0)
            continue;
        bm = s_buf[i].via_bm;
        break;
    }

    xSemaphoreGive(s_lock);
    return bm;
}

size_t lastheard_bm_count(void) {
    if (!s_inited)
        return 0;
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE)
        return 0;

    size_t n = 0;
    for (size_t i = 0; i < s_count; i++) {
        if (s_buf[i].via_bm)
            n++;
    }

    xSemaphoreGive(s_lock);
    return n;
}

int lastheard_directs(char *out, size_t out_size) {
    if (out == NULL || out_size == 0)
        return 0;
    out[0] = 0;
    if (!s_inited)
        return 0;
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE)
        return 0;

    size_t pos = 0;
    int count = 0;
    for (size_t i = 0; i < s_count; i++) {
        if (!s_buf[i].direct || !s_buf[i].callsign[0])
            continue;
        size_t need = strlen(s_buf[i].callsign) + (count > 0 ? 1u : 0u);
        if (pos + need >= out_size)
            break; // stop on the first callsign that does not fit whole
        if (count > 0)
            out[pos++] = ' ';
        size_t n = strlen(s_buf[i].callsign);
        memcpy(out + pos, s_buf[i].callsign, n);
        pos += n;
        count++;
    }
    out[pos] = 0;

    xSemaphoreGive(s_lock);
    return count;
}

bool lastheard_heard_history(const char *callsign, uint16_t out[LASTHEARD_HEARD_HOURS]) {
    if (callsign == NULL || callsign[0] == 0 || out == NULL || !s_inited)
        return false;
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE)
        return false;

    // Look the station up under the same key lastheard_add() stores it by, so
    // a long or lower-case callsign still finds the row the table holds.
    char call[LASTHEARD_CALL_LEN];
    makeCallKey(call, callsign);

    bool found = false;
    for (size_t i = 0; i < s_count; i++) {
        if (strcasecmp(s_buf[i].callsign, call) != 0)
            continue;
        // Bring the histogram up to the current wall-clock hour before handing
        // it out, so a station that has gone quiet reports the real gap
        // instead of stale counts from its last frame's hour. Rolling is all
        // this does: reading the graph is not traffic, so no hour is counted
        // into and the stored row reads the same however often it is queried.
        rollHourlyHistogram(&s_buf[i], time(NULL), false);
        memcpy(out, s_buf[i].hourly, sizeof(s_buf[i].hourly));
        found = true;
        break;
    }

    xSemaphoreGive(s_lock);
    return found;
}

size_t lastheard_dump_json(char *out, size_t out_size) {
    if (!s_inited || out == NULL || out_size < 4)
        return 0;

    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        int n = snprintf(out, out_size, "[]");
        return (n > 0) ? (size_t)n : 0;
    }

    size_t pos = 0;
    out[pos++] = '[';
    bool first = true;

    // s_buf is already ordered most-recently-heard first, one row per station.
    for (size_t i = 0; i < s_count && pos + 4 < out_size; i++) {
        lastheard_entry_t *e = &s_buf[i];

        // UTC, and labelled as such: the firmware runs with TZ=UTC0 (see
        // time_sync.c), so gmtime_r() states the timescale these stamps are
        // really on instead of leaving it to the process timezone. A row
        // stamped 0 was heard before the clock was set and has no time of day
        // to state, so its field goes out empty for the client to render as it
        // sees fit.
        char strTime[12];
        strTime[0] = 0;
        if (e->time != 0) {
            struct tm tmv;
            gmtime_r(&e->time, &tmv);
            snprintf(strTime, sizeof(strTime), "%02d:%02d:%02dZ", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        }

        char call_esc[LASTHEARD_CALL_LEN * 2];
        char path_esc[LASTHEARD_PATH_LEN * 2];
        json_escape(e->callsign, call_esc, sizeof(call_esc));
        json_escape(e->path, path_esc, sizeof(path_esc));

        char sym[8] = "";
        if (e->sym_table && e->sym_code) {
            int table = (e->sym_table == '/') ? 1 : 2; // primary/alternate icon set, matches aprs.dprns.com/symbols/icons/<code>-<table>.png
            snprintf(sym, sizeof(sym), "%d-%d", (int)(unsigned char)e->sym_code, table);
        }

        int n = snprintf(out + pos, out_size - pos, "%s{\"time\":\"%s\",\"call\":\"%s\",\"path\":\"%s\",\"sym\":\"%s\",\"packets\":%lu}", first ? "" : ",",
                         strTime, call_esc, path_esc, sym, (unsigned long)e->packets);
        if (n < 0)
            break;
        if (pos + (size_t)n + 2 >= out_size)
            break;
        pos += (size_t)n;
        first = false;
    }

    xSemaphoreGive(s_lock);

    // Only advance pos if the closing bracket was actually written whole:
    // out_size - pos is at least 1 here (the loop guard leaves room for it),
    // so snprintf never fails outright, but with exactly one byte free it
    // still only fits the NUL and reports the one byte it could not write.
    // Checking the return against the remaining space keeps pos an honest
    // count of bytes actually in out rather than of bytes that were wanted.
    int n = snprintf(out + pos, out_size - pos, "]");
    if (n > 0 && pos + (size_t)n < out_size)
        pos += (size_t)n;
    return pos;
}
