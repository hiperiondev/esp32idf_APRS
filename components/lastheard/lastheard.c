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

#define LASTHEARD_CAPACITY 30 // stations kept in RAM, most recent first
#define LASTHEARD_CALL_LEN 12
#define LASTHEARD_PATH_LEN 48

typedef struct {
    time_t time;
    char callsign[LASTHEARD_CALL_LEN];
    char path[LASTHEARD_PATH_LEN]; // e.g. "RF: WIDE1-1" / "INET: DIRECT"
    char sym_table;
    char sym_code;
    bool via_rf;       // latest frame from this station was heard off the air
    bool direct;       // latest frame from this station carried no used digipeater
    uint8_t used_hops; // digipeater addresses actually repeated in the latest RF frame
    uint32_t packets;  // total times this callsign has been heard
    // Per-channel last-heard stamps, kept alongside the whole-entry time above
    // because the two answer different questions. time is when the station was
    // last heard at all, which is what the dashboard shows; these two are when
    // it was last heard on each channel, which is what the INET->RF message
    // gate needs - a station can be locally audible and Internet-connected at
    // once, and the gate tests each independently. 0 means never heard that way.
    time_t rf_time;
    time_t inet_time;
    // Hourly heard histogram for the "?APRSH" query (APRS101 ch.15): hourly[0]
    // is the current clock hour, hourly[LASTHEARD_HEARD_HOURS - 1] the oldest.
    // hour_slot is the epoch hour number hourly[0] belongs to, so lastheard_add()
    // can tell how many whole hours to roll the histogram forward on the next
    // frame from this station.
    uint16_t hourly[LASTHEARD_HEARD_HOURS];
    time_t hour_slot;
} lastheard_entry_t;

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
// optionally count one packet into it. A brand-new entry (hour_slot == 0)
// starts with a histogram of all zero except the current hour. A station heard
// again inside the same clock hour it was last heard in just adds to hourly[0].
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
static void rollHourlyHistogram(lastheard_entry_t *e, time_t now, bool count_packet) {
    time_t nowHour = now / 3600;

    if (e->hour_slot == 0) {
        // First frame ever recorded for this entry.
        memset(e->hourly, 0, sizeof(e->hourly));
        e->hour_slot = nowHour;
    } else if (nowHour > e->hour_slot) {
        time_t elapsed = nowHour - e->hour_slot;
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
    // nowHour < e->hour_slot only if the wall clock stepped backwards (e.g. an
    // NTP correction); the histogram is left as-is rather than guessed at.

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
    size_t i = 0;
    for (; src[i] != 0 && i < LASTHEARD_CALL_LEN - 1; i++)
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

void lastheard_add(const char *callsign, const char *path, bool via_rf, bool direct, uint8_t used_hops, char sym_table, char sym_code) {
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
        // rollHourlyHistogram() to start a fresh histogram (hour_slot == 0).
        // Everything moves down one slot, dropping the oldest station once the
        // table is full.
        memset(&entry, 0, sizeof(entry));
        entry.packets = 1;
        if (s_count < LASTHEARD_CAPACITY)
            s_count++;
        shift_from = s_count - 1;
    }

    time_t now = time(NULL);
    rollHourlyHistogram(&entry, now, true);

    entry.time = now;
    strncpy(entry.callsign, call, sizeof(entry.callsign) - 1);
    entry.callsign[sizeof(entry.callsign) - 1] = 0;
    snprintf(entry.path, sizeof(entry.path), "%s: %s", via_rf ? "RF" : "INET", (path && path[0]) ? path : "DIRECT");
    entry.sym_table = sym_table;
    entry.sym_code = sym_code;
    // Both of these describe the path this frame took, so the newest frame
    // always wins: a station that has moved out of direct range stops being
    // listed as direct as soon as its first digipeated frame arrives, and a
    // station last seen on the APRS-IS feed stops counting as locally heard.
    entry.via_rf = via_rf;
    entry.direct = via_rf && direct;
    entry.used_hops = via_rf ? used_hops : 0;

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
        if (rf_only && s_buf[i].used_hops > max_used_hops)
            continue;
        count++;
    }

    xSemaphoreGive(s_lock);
    return count;
}

// Shared body of the two window queries: look the station up under the stored
// key and test one of its per-channel stamps against the window. A station the
// table does not hold answers false, which is the safe answer for every caller
// - the message gate reads "not heard" as "do not transmit".
//
// The test is a difference against the current wall clock, so a stamp taken
// before the clock was set, or one left in the future by a backwards NTP
// correction, reads as outside the window instead of as arbitrarily recent.
static bool heardWithin(const char *callsign, uint32_t seconds, bool rf) {
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
        if (stamp != 0 && now >= stamp && (uint64_t)(now - stamp) <= (uint64_t)seconds)
            within = true;
        break;
    }

    xSemaphoreGive(s_lock);
    return within;
}

bool lastheard_heard_rf_within(const char *callsign, uint32_t seconds) {
    return heardWithin(callsign, seconds, true);
}

bool lastheard_heard_inet_within(const char *callsign, uint32_t seconds) {
    return heardWithin(callsign, seconds, false);
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
        // really on instead of leaving it to the process timezone.
        struct tm tmv;
        gmtime_r(&e->time, &tmv);
        char strTime[12];
        snprintf(strTime, sizeof(strTime), "%02d:%02d:%02dZ", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

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

    pos += (size_t)snprintf(out + pos, out_size - pos, "]");
    return pos;
}
