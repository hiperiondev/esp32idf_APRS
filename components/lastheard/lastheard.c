#include "lastheard.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define LASTHEARD_CAPACITY  30 // stations kept in RAM, most recent first
#define LASTHEARD_CALL_LEN  12
#define LASTHEARD_PATH_LEN  48

typedef struct {
    bool used;
    time_t time;
    char callsign[LASTHEARD_CALL_LEN];
    char path[LASTHEARD_PATH_LEN]; // e.g. "RF: WIDE1-1" / "INET: DIRECT"
    char sym_table;
    char sym_code;
    uint32_t packets; // total times this callsign has been heard
} lastheard_entry_t;

static lastheard_entry_t s_buf[LASTHEARD_CAPACITY];
static size_t s_head = 0; // index the *next* entry will be written to
static size_t s_count = 0;
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

    // Carry the running packet count forward if this callsign is already
    // the most-recently-seen entry (avoids the count resetting to 1 on
    // every single frame from the same station).
    uint32_t packets = 1;
    for (size_t i = 0; i < s_count; i++) {
        size_t idx = (s_head + LASTHEARD_CAPACITY - 1 - i) % LASTHEARD_CAPACITY;
        if (strncmp(s_buf[idx].callsign, callsign, LASTHEARD_CALL_LEN) == 0) {
            packets = s_buf[idx].packets + 1;
            break;
        }
    }

    lastheard_entry_t *e = &s_buf[s_head];
    e->used = true;
    e->time = time(NULL);
    strncpy(e->callsign, callsign, sizeof(e->callsign) - 1);
    e->callsign[sizeof(e->callsign) - 1] = 0;
    snprintf(e->path, sizeof(e->path), "%s: %s", via_rf ? "RF" : "INET", (path && path[0]) ? path : "DIRECT");
    e->sym_table = sym_table;
    e->sym_code = sym_code;
    e->packets = packets;

    s_head = (s_head + 1) % LASTHEARD_CAPACITY;
    if (s_count < LASTHEARD_CAPACITY)
        s_count++;

    xSemaphoreGive(s_lock);
}

static size_t json_escape(const char *src, char *dst, size_t dst_size) {
    size_t di = 0;
    for (const char *p = src; *p && di + 2 < dst_size; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            dst[di++] = '\\';
            dst[di++] = (char)c;
        } else if (c < 0x20) {
            continue;
        } else {
            dst[di++] = (char)c;
        }
    }
    dst[di] = 0;
    return di;
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

    // Most-recently-added first.
    for (size_t i = 0; i < s_count && pos + 4 < out_size; i++) {
        size_t idx = (s_head + LASTHEARD_CAPACITY - 1 - i) % LASTHEARD_CAPACITY;
        lastheard_entry_t *e = &s_buf[idx];
        if (!e->used)
            continue;

        struct tm tmv;
        localtime_r(&e->time, &tmv);
        char strTime[12];
        snprintf(strTime, sizeof(strTime), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

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
