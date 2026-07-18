/**
 * @file trafficlog.c
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
 * @brief In-RAM traffic ring buffer implementation: thread-safe formatted and
 * structured entry insertion, sequence numbering and JSON serialization for the
 * dashboard's live traffic feed.
 */

#include "trafficlog.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TRAFFICLOG_CAPACITY 64  // number of lines kept in RAM
#define TRAFFICLOG_LINE_LEN 140 // max chars per formatted "m" line (truncated beyond this)
#define TRAFFICLOG_DIR_LEN 12   // max chars for the direction/type tag
#define TRAFFICLOG_DX_LEN 16    // max chars for the DX (callsign) field
#define TRAFFICLOG_PKT_LEN 128  // max chars for the raw PACKET field

typedef struct {
    uint32_t seq;
    int64_t time_ms;
    char line[TRAFFICLOG_LINE_LEN]; // legacy free-form message ("m")
    char dir[TRAFFICLOG_DIR_LEN];   // direction/type tag ("d"), e.g. RX/TX/DIGI
    char dx[TRAFFICLOG_DX_LEN];     // station callsign ("dx")
    char packet[TRAFFICLOG_PKT_LEN];// raw TNC2 packet text ("pkt")
    int audio_mv;                   // demodulated audio level, mV RMS, -1 = n/a ("au")
    char sym_table;                 // APRS symbol table byte, 0 = unknown
    char sym_code;                  // APRS symbol code byte, 0 = unknown
} trafficlog_entry_t;

static trafficlog_entry_t s_buf[TRAFFICLOG_CAPACITY];
static size_t s_head = 0; // index the *next* entry will be written to
static size_t s_count = 0;
static uint32_t s_next_seq = 1;
static SemaphoreHandle_t s_lock = NULL;
static bool s_inited = false;

void trafficlog_init(void) {
    if (s_inited)
        return;
    s_lock = xSemaphoreCreateMutex();
    s_inited = true;
}

// Appends a new (mostly) zeroed entry to the ring buffer and returns a
// pointer to it, already stamped with seq/time_ms. Caller fills the rest
// while still holding s_lock. Returns NULL if the lock couldn't be taken.
static trafficlog_entry_t *push_entry(void) {
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE)
        return NULL; // never block the caller (radio/network tasks) indefinitely

    trafficlog_entry_t *e = &s_buf[s_head];
    memset(e, 0, sizeof(*e));
    e->seq = s_next_seq++;
    e->time_ms = esp_timer_get_time() / 1000;
    e->audio_mv = -1;

    s_head = (s_head + 1) % TRAFFICLOG_CAPACITY;
    if (s_count < TRAFFICLOG_CAPACITY)
        s_count++;

    return e;
}

void trafficlog_add(const char *fmt, ...) {
    if (!s_inited)
        trafficlog_init();

    char tmp[TRAFFICLOG_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    trafficlog_entry_t *e = push_entry();
    if (!e)
        return;

    strncpy(e->line, tmp, sizeof(e->line) - 1);
    strncpy(e->dir, "LOG", sizeof(e->dir) - 1);
    // dx/packet left blank, audio_mv left at -1 (set by push_entry)

    xSemaphoreGive(s_lock);
}

void trafficlog_add_pkt(const char *dir, const char *dx, const char *packet, int audio_mv, char sym_table, char sym_code) {
    if (!s_inited)
        trafficlog_init();

    if (!dir)
        dir = "";
    if (!dx)
        dx = "";
    if (!packet)
        packet = "";

    trafficlog_entry_t *e = push_entry();
    if (!e)
        return;

    strncpy(e->dir, dir, sizeof(e->dir) - 1);
    strncpy(e->dx, dx, sizeof(e->dx) - 1);
    strncpy(e->packet, packet, sizeof(e->packet) - 1);
    e->audio_mv = audio_mv;
    e->sym_table = sym_table;
    e->sym_code = sym_code;

    // Keep the legacy "m" field populated too ("<DIR>: <packet>") so any
    // consumer that only understands the old plain-text format still
    // shows something sensible.
    snprintf(e->line, sizeof(e->line), "%s%s%s", dir, (dir[0] && packet[0]) ? ": " : "", packet);

    xSemaphoreGive(s_lock);
}

// Escapes a string for embedding inside a JSON string literal. Drops raw
// control characters (except turning '\n' into a literal "\n") since these
// are single log lines/packets and never need to preserve arbitrary binary
// data.
static size_t json_escape(const char *src, char *dst, size_t dst_size) {
    size_t di = 0;
    for (const char *p = src; *p && di + 2 < dst_size; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            dst[di++] = '\\';
            dst[di++] = (char)c;
        } else if (c == '\n') {
            dst[di++] = '\\';
            dst[di++] = 'n';
        } else if (c == '\r' || c < 0x20) {
            continue;
        } else {
            dst[di++] = (char)c;
        }
    }
    dst[di] = 0;
    return di;
}

size_t trafficlog_dump_json(uint32_t since_seq, char *out, size_t out_size) {
    if (!s_inited || out == NULL || out_size < 16)
        return 0;

    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        // Couldn't get the lock in time - return an empty-but-valid payload
        // rather than blocking the HTTP worker task.
        int n = snprintf(out, out_size, "{\"seq\":%lu,\"items\":[]}", (unsigned long)since_seq);
        return (n > 0) ? (size_t)n : 0;
    }

    uint32_t latest = s_next_seq - 1;
    size_t start = (s_count < TRAFFICLOG_CAPACITY) ? 0 : s_head;

    size_t pos = (size_t)snprintf(out, out_size, "{\"seq\":%lu,\"items\":[", (unsigned long)latest);
    bool first = true;

    for (size_t i = 0; i < s_count && pos + 4 < out_size; i++) {
        size_t idx = (start + i) % TRAFFICLOG_CAPACITY;
        trafficlog_entry_t *e = &s_buf[idx];
        if (e->seq <= since_seq)
            continue;

        char escM[TRAFFICLOG_LINE_LEN * 2];
        char escDir[TRAFFICLOG_DIR_LEN * 2];
        char escDx[TRAFFICLOG_DX_LEN * 2];
        char escPkt[TRAFFICLOG_PKT_LEN * 2];
        json_escape(e->line, escM, sizeof(escM));
        json_escape(e->dir, escDir, sizeof(escDir));
        json_escape(e->dx, escDx, sizeof(escDx));
        json_escape(e->packet, escPkt, sizeof(escPkt));

        // Matches lastheard_dump_json()'s icon naming: aprs.dprns.com serves
        // icons as /symbols/icons/<symbol_code>-<1_or_2>.png, where 1 = the
        // primary table ('/') and 2 = the alternate table ('\').
        char sym[8] = "";
        if (e->sym_table && e->sym_code) {
            int table = (e->sym_table == '/') ? 1 : 2;
            snprintf(sym, sizeof(sym), "%d-%d", (int)(unsigned char)e->sym_code, table);
        }

        int n = snprintf(out + pos, out_size - pos,
                          "%s{\"t\":%lld,\"m\":\"%s\",\"d\":\"%s\",\"dx\":\"%s\",\"pkt\":\"%s\",\"au\":%d,\"sym\":\"%s\"}",
                          first ? "" : ",", (long long)e->time_ms, escM, escDir, escDx, escPkt, e->audio_mv, sym);
        if (n < 0)
            break;
        if (pos + (size_t)n + 2 >= out_size) // leave room for the closing "]}"
            break;
        pos += (size_t)n;
        first = false;
    }

    xSemaphoreGive(s_lock);

    pos += (size_t)snprintf(out + pos, out_size - pos, "]}");
    return pos;
}
