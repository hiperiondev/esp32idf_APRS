// @file trafficlog.c
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
// @brief In-RAM traffic ring buffer implementation: thread-safe formatted and
// structured entry insertion, sequence numbering and JSON serialization for the
// dashboard's live traffic feed.

#include "trafficlog.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "json_escape.h" // json_escape()

#define TRAFFICLOG_CAPACITY 64 // number of lines kept in RAM
// Shared text buffer: an entry holds EITHER the free-form "m" line
// (trafficlog_add) OR the raw TNC2 packet (trafficlog_add_pkt), never both, so
// one buffer tagged by 'kind' serves both and the ring costs half of what two
// dedicated fields would.
#define TRAFFICLOG_TEXT_LEN 144
#define TRAFFICLOG_DIR_LEN  12 // max chars for the direction/type tag
#define TRAFFICLOG_DX_LEN   16 // max chars for the DX (callsign) field

// Worst-case length of the reconstructed "m" field for a PKT entry,
// "<dir>: <text>": DIR_LEN + strlen(": ") + TEXT_LEN, all buffers being
// NUL-terminated. Sized so the snprintf() in trafficlog_dump_json() can
// never truncate (keeps -Werror=format-truncation happy and guarantees the
// full packet is always mirrored into the "m" column).
#define TRAFFICLOG_M_LEN (TRAFFICLOG_DIR_LEN + 2 + TRAFFICLOG_TEXT_LEN)

// Which of the two mutually-exclusive roles the shared 'text' buffer plays for
// a given entry. trafficlog_add() produces LINE entries (free-form message),
// trafficlog_add_pkt() produces PKT entries (raw packet). Defaults to LINE (0)
// so a memset-zeroed entry is well-formed.
typedef enum {
    TL_KIND_LINE = 0, // text is the free-form message ("m"); no raw packet ("pkt" is "")
    TL_KIND_PKT = 1,  // text is the raw TNC2 packet ("pkt"); "m" is derived "<dir>: <text>"
} trafficlog_kind_t;

typedef struct {
    uint32_t seq;
    int64_t time_ms;
    char text[TRAFFICLOG_TEXT_LEN]; // shared: free-form "m" line OR raw packet (see 'kind')
    char dir[TRAFFICLOG_DIR_LEN];   // direction/type tag ("d"), e.g. RX/TX/DIGI
    char dx[TRAFFICLOG_DX_LEN];     // station callsign ("dx")
    int audio_mv;                   // demodulated audio level, mV RMS, -1 = n/a ("au")
    char sym_table;                 // APRS symbol table byte, 0 = unknown
    char sym_code;                  // APRS symbol code byte, 0 = unknown
    uint8_t kind;                   // trafficlog_kind_t: what 'text' represents
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

    char tmp[TRAFFICLOG_TEXT_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    trafficlog_entry_t *e = push_entry();
    if (!e)
        return;

    e->kind = TL_KIND_LINE;
    strncpy(e->text, tmp, sizeof(e->text) - 1);
    strncpy(e->dir, "LOG", sizeof(e->dir) - 1);
    // dx left blank, audio_mv left at -1 (set by push_entry); no raw packet.

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

    e->kind = TL_KIND_PKT;
    strncpy(e->dir, dir, sizeof(e->dir) - 1);
    strncpy(e->dx, dx, sizeof(e->dx) - 1);
    strncpy(e->text, packet, sizeof(e->text) - 1); // raw TNC2 packet ("pkt")
    e->audio_mv = audio_mv;
    e->sym_table = sym_table;
    e->sym_code = sym_code;

    // The "m" field ("<DIR>: <packet>") is not stored - it is derived on the
    // fly in trafficlog_dump_json() from dir + text, so the free-form line and
    // the raw packet can share a single buffer.

    xSemaphoreGive(s_lock);
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

        // Reconstruct the two logical text fields from the single shared buffer:
        //   LINE kind -> "m" is the stored text, "pkt" is empty
        //   PKT  kind -> "pkt" is the stored text, "m" is "<dir>: <packet>"
        const char *m_src;
        const char *pkt_src;
        char mbuf[TRAFFICLOG_M_LEN];
        if (e->kind == TL_KIND_PKT) {
            snprintf(mbuf, sizeof(mbuf), "%s%s%s", e->dir, (e->dir[0] && e->text[0]) ? ": " : "", e->text);
            m_src = mbuf;
            pkt_src = e->text;
        } else {
            m_src = e->text;
            pkt_src = "";
        }

        char escM[TRAFFICLOG_M_LEN * 2];
        char escDir[TRAFFICLOG_DIR_LEN * 2];
        char escDx[TRAFFICLOG_DX_LEN * 2];
        char escPkt[TRAFFICLOG_TEXT_LEN * 2];
        json_escape(m_src, escM, sizeof(escM));
        json_escape(e->dir, escDir, sizeof(escDir));
        json_escape(e->dx, escDx, sizeof(escDx));
        json_escape(pkt_src, escPkt, sizeof(escPkt));

        // Matches lastheard_dump_json()'s icon naming: aprs.dprns.com serves
        // icons as /symbols/icons/<symbol_code>-<1_or_2>.png, where 1 = the
        // primary table ('/') and 2 = the alternate table ('\').
        char sym[8] = "";
        if (e->sym_table && e->sym_code) {
            int table = (e->sym_table == '/') ? 1 : 2;
            snprintf(sym, sizeof(sym), "%d-%d", (int)(unsigned char)e->sym_code, table);
        }

        int n = snprintf(out + pos, out_size - pos, "%s{\"t\":%lld,\"m\":\"%s\",\"d\":\"%s\",\"dx\":\"%s\",\"pkt\":\"%s\",\"au\":%d,\"sym\":\"%s\"}",
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
