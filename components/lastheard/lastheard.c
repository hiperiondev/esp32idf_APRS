/**
 * @file lastheard.c
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
 * @brief In-RAM table of decoded stations feeding the dashboard "LAST HEARD"
 * panel: one entry per callsign, most recently heard first, with thread-safe
 * insertion, per-callsign packet counting, UTC timestamping and JSON
 * serialization.
 */

#include "lastheard.h"

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
    uint32_t packets; // total times this callsign has been heard
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

void lastheard_init(void) {
    if (s_inited)
        return;
    memset(s_buf, 0, sizeof(s_buf));
    s_lock = xSemaphoreCreateMutex();
    s_inited = true;
}

void lastheard_add(const char *callsign, const char *path, bool via_rf, char sym_table, char sym_code) {
    if (!s_inited)
        lastheard_init();
    if (!callsign || !callsign[0])
        return;
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE)
        return; // never block the radio/network task indefinitely

    // Compare against the same truncation that gets stored, so two callsigns
    // that differ only past LASTHEARD_CALL_LEN still resolve to one slot.
    char call[LASTHEARD_CALL_LEN];
    strncpy(call, callsign, sizeof(call) - 1);
    call[sizeof(call) - 1] = 0;

    size_t found = LASTHEARD_CAPACITY;
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_buf[i].callsign, call) == 0) {
            found = i;
            break;
        }
    }

    uint32_t packets = 1;
    size_t shift_from;
    if (found < LASTHEARD_CAPACITY) {
        // Known station: carry its running packet count forward and reuse its
        // slot, closing the gap it leaves behind as it moves to the front.
        packets = s_buf[found].packets + 1;
        shift_from = found;
    } else {
        // New station: push everything down one slot, dropping the oldest
        // entry once the table is full.
        if (s_count < LASTHEARD_CAPACITY)
            s_count++;
        shift_from = s_count - 1;
    }
    if (shift_from > 0)
        memmove(&s_buf[1], &s_buf[0], shift_from * sizeof(s_buf[0]));

    lastheard_entry_t *e = &s_buf[0];
    e->time = time(NULL);
    strncpy(e->callsign, call, sizeof(e->callsign) - 1);
    e->callsign[sizeof(e->callsign) - 1] = 0;
    snprintf(e->path, sizeof(e->path), "%s: %s", via_rf ? "RF" : "INET", (path && path[0]) ? path : "DIRECT");
    e->sym_table = sym_table;
    e->sym_code = sym_code;
    e->packets = packets;

    xSemaphoreGive(s_lock);
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
