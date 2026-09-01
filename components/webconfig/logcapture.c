// @file logcapture.c
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
// @brief Console log mirror implementation: the esp_log_set_vprintf() hook,
// the line assembler that turns formatted console text into bounded rows, the
// ring buffer that holds them, and the idle timeout that switches the mirror
// off again when nobody is reading it.

#include "logcapture.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "json_escape.h" // json_escape()

// Working buffer the hook formats one console write into before the line
// assembler walks it. Two full rows wide, so the common case - one log
// statement, one line, well under the row width - never comes close to it,
// and a statement long enough to wrap still arrives whole. Static rather than
// on the stack: the hook runs on whichever task called ESP_LOGx, including
// tasks whose stacks are sized for their own work and nothing else, so half a
// kilobyte of stack there would be a fault waiting for the wrong caller. It is
// only ever touched under s_lock.
#define LOGCAPTURE_SCRATCH_LEN ((LOGCAPTURE_LINE_MAX + 1) * 2)

// The two enclosing quotes, plus a worst case of two output characters for
// every stored character, plus the terminating NUL.
_Static_assert(LOGCAPTURE_JSON_LINE_MAX >= (LOGCAPTURE_LINE_MAX * 2) + 3, "LOGCAPTURE_JSON_LINE_MAX is too small for LOGCAPTURE_LINE_MAX");

typedef struct {
    uint32_t seq;                       // ever-increasing line number, 0 = unused slot
    char text[LOGCAPTURE_LINE_MAX + 1]; // one row, NUL-terminated
} logcapture_line_t;

static logcapture_line_t *s_ring = NULL; // allocated by logcapture_start(), released by logcapture_stop()
static size_t s_head = 0;                // index the next line will be written to
static size_t s_count = 0;               // lines currently held, saturates at LOGCAPTURE_CAPACITY
static uint32_t s_next_seq = 1;          // sequence number the next line will carry

// Partial line carried between hook calls: a console write does not have to
// end at a line boundary, and printf() without a trailing newline is ordinary
// in ESP-IDF startup output.
static char s_acc[LOGCAPTURE_LINE_MAX + 1];
static size_t s_acc_len = 0;
// Where the assembler stands in an ANSI escape sequence, which can straddle
// two console writes and so has to survive between them.
typedef enum {
    LOGCAPTURE_ESC_NONE = 0,  // ordinary text
    LOGCAPTURE_ESC_START = 1, // an ESC has been seen, its introducer has not
    LOGCAPTURE_ESC_CSI = 2,   // inside a CSI sequence, discarding up to its final byte
} logcapture_esc_t;

static logcapture_esc_t s_escape = LOGCAPTURE_ESC_NONE;

static char s_scratch[LOGCAPTURE_SCRATCH_LEN];

static SemaphoreHandle_t s_lock = NULL;
static esp_timer_handle_t s_idle_timer = NULL;
static vprintf_like_t s_console_vprintf = NULL;
// Read by the hook on every console write without taking the lock, so a write
// that arrives while the mirror is off costs one load and nothing else.
static volatile bool s_running = false;

// Appends the accumulated characters to the ring as one row and starts a new
// one. Called with s_lock held.
static void logcapture_flush_line(void) {
    if (s_acc_len == 0)
        return; // an empty row carries nothing an operator can read

    s_acc[s_acc_len] = 0;

    logcapture_line_t *slot = &s_ring[s_head];
    slot->seq = s_next_seq++;
    memcpy(slot->text, s_acc, s_acc_len + 1);

    s_head = (s_head + 1) % LOGCAPTURE_CAPACITY;
    if (s_count < LOGCAPTURE_CAPACITY)
        s_count++;

    s_acc_len = 0;
}

// Walks one formatted console write and turns it into rows.
//
// Three things are dropped on the way in. The ANSI colour sequences ESP-IDF
// wraps each line in are meaningless in a browser and would otherwise be
// shown as literal bracket noise. Carriage returns are dropped because the
// console emits CRLF and the row is stored without any terminator. Remaining
// control characters carry no text either, and dropping them here is what
// lets json_escape() stay a byte-for-byte copy of everything that survives.
//
// A row that reaches LOGCAPTURE_LINE_MAX characters is closed and the rest of
// the text continues on the next one, so a long line is wrapped rather than
// truncated and nothing the console printed is lost. Called with s_lock held.
static void logcapture_consume(const char *text) {
    for (const char *p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;

        if (s_escape == LOGCAPTURE_ESC_START) {
            // The byte after ESC decides the shape of the sequence. "[" opens
            // a CSI, which runs on until its final byte; anything else is a
            // two-character escape that ends right here. Consuming this byte
            // before testing for a final byte is what keeps the "[" of
            // "ESC [ 0;32 m" from being read as the end of the sequence it
            // actually begins - it sits inside the final-byte range itself.
            s_escape = (c == '[') ? LOGCAPTURE_ESC_CSI : LOGCAPTURE_ESC_NONE;
            continue;
        }
        if (s_escape == LOGCAPTURE_ESC_CSI) {
            // Parameter and intermediate bytes run from 0x20 to 0x3F; the
            // first byte at or above 0x40 is the final one and ends it.
            if (c >= 0x40 && c <= 0x7E)
                s_escape = LOGCAPTURE_ESC_NONE;
            continue;
        }

        if (c == 0x1B) {
            s_escape = LOGCAPTURE_ESC_START;
            continue;
        }
        if (c == '\n') {
            logcapture_flush_line();
            continue;
        }
        if (c < 0x20 && c != '\t')
            continue;

        s_acc[s_acc_len++] = (char)c;
        if (s_acc_len >= LOGCAPTURE_LINE_MAX)
            logcapture_flush_line();
    }
}

// The esp_log_set_vprintf() hook.
//
// The console write happens first and unconditionally, so the serial log is
// exactly what it would be with no mirror installed, and stays that way even
// if everything below is skipped. The mirror then works from its own copy of
// the argument list, since the console writer has already consumed the
// original.
//
// Two callers are skipped rather than served. One is an interrupt: taking a
// semaphore there is not allowed, and a log statement from an ISR is already
// outside what the ordinary log path supports. The other is a task that finds
// the lock held - a line lost to that is a line another task was writing at
// the same microsecond, and blocking a task inside its own ESP_LOGx call to
// avoid losing it would be the worse trade by far.
static int logcapture_vprintf(const char *fmt, va_list ap) {
    va_list mirror;
    va_copy(mirror, ap);

    int written = s_console_vprintf ? s_console_vprintf(fmt, ap) : vprintf(fmt, ap);

    if (s_running && !xPortInIsrContext() && s_lock != NULL) {
        if (xSemaphoreTake(s_lock, 0) == pdTRUE) {
            if (s_ring != NULL) {
                vsnprintf(s_scratch, sizeof(s_scratch), fmt, mirror);
                logcapture_consume(s_scratch);
            }
            xSemaphoreGive(s_lock);
        }
    }

    va_end(mirror);
    return written;
}

// Fires when nothing has read the ring for LOGCAPTURE_IDLE_TIMEOUT_S. This is
// what makes "leaving the page stops the capture" hold even when the browser
// never gets to say so: a closed tab, a sleeping phone and a lost Wi-Fi link
// all look the same from here, and all of them release the ring.
static void logcapture_idle_timeout(void *arg) {
    (void)arg;
    logcapture_stop();
}

// Creates the objects the mirror needs and installs the console hook, once.
// Every entry point that can reach it (start/stop/touch/read) is called from
// the HTTP server task, so this needs no lock of its own; the hook is only
// installed at the very end, when everything it uses exists.
static bool logcapture_install(void) {
    if (s_console_vprintf != NULL)
        return true;

    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL)
            return false;
    }

    if (s_idle_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = logcapture_idle_timeout,
            .arg = NULL,
            .name = "logcap_idle",
        };
        if (esp_timer_create(&args, &s_idle_timer) != ESP_OK)
            return false;
    }

    s_console_vprintf = esp_log_set_vprintf(logcapture_vprintf);
    return true;
}

// Restarts the idle countdown from now. esp_timer_stop() is called first
// because esp_timer_start_once() refuses an already-armed timer.
static void logcapture_arm_idle(void) {
    if (s_idle_timer == NULL)
        return;
    esp_timer_stop(s_idle_timer);
    esp_timer_start_once(s_idle_timer, (uint64_t)LOGCAPTURE_IDLE_TIMEOUT_S * 1000000ULL);
}

bool logcapture_start(void) {
    if (!logcapture_install())
        return false;

    if (s_running) {
        logcapture_arm_idle();
        return true;
    }

    logcapture_line_t *ring = calloc(LOGCAPTURE_CAPACITY, sizeof(logcapture_line_t));
    if (ring == NULL)
        return false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_ring = ring;
    s_head = 0;
    s_count = 0;
    s_next_seq = 1;
    s_acc_len = 0;
    s_escape = LOGCAPTURE_ESC_NONE;
    xSemaphoreGive(s_lock);

    // Published last: from this store on, any task that logs is mirrored, and
    // it must not see the flag before the ring it will write into.
    s_running = true;
    logcapture_arm_idle();
    return true;
}

void logcapture_stop(void) {
    if (s_lock == NULL)
        return;

    // Cleared first, so the hook stops looking at the ring before the ring is
    // freed. A hook already past that test is still holding the lock taken
    // below, which is what makes the free() safe rather than merely likely to
    // be.
    s_running = false;
    if (s_idle_timer != NULL)
        esp_timer_stop(s_idle_timer);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    logcapture_line_t *ring = s_ring;
    s_ring = NULL;
    s_head = 0;
    s_count = 0;
    s_acc_len = 0;
    s_escape = LOGCAPTURE_ESC_NONE;
    xSemaphoreGive(s_lock);

    free(ring);
}

bool logcapture_is_running(void) {
    return s_running;
}

void logcapture_touch(void) {
    if (s_running)
        logcapture_arm_idle();
}

uint32_t logcapture_latest_seq(void) {
    if (s_lock == NULL)
        return 0;

    uint32_t latest = 0;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (s_ring != NULL && s_count > 0)
            latest = s_next_seq - 1;
        xSemaphoreGive(s_lock);
    }
    return latest;
}

size_t logcapture_next_json(uint32_t after_seq, uint32_t max_seq, char *out, size_t out_size, uint32_t *out_seq) {
    if (out == NULL || out_size < LOGCAPTURE_JSON_LINE_MAX)
        return 0;
    if (s_lock == NULL)
        return 0;

    char text[LOGCAPTURE_LINE_MAX + 1];
    uint32_t found = 0;

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) != pdTRUE)
        return 0;

    if (s_ring != NULL) {
        // Driven by sequence number rather than ring position: the oldest
        // line still in range is the answer wherever it happens to sit, so a
        // walk interleaved with evictions skips what is gone instead of
        // repeating or reordering what is left.
        for (size_t i = 0; i < s_count; i++) {
            const logcapture_line_t *e = &s_ring[(s_head + LOGCAPTURE_CAPACITY - s_count + i) % LOGCAPTURE_CAPACITY];
            if (e->seq <= after_seq || e->seq > max_seq)
                continue;
            if (found == 0 || e->seq < found) {
                found = e->seq;
                memcpy(text, e->text, sizeof(text));
            }
        }
    }

    xSemaphoreGive(s_lock);

    if (found == 0) {
        if (out_seq)
            *out_seq = after_seq;
        return 0;
    }

    char esc[(LOGCAPTURE_LINE_MAX * 2) + 1];
    json_escape(text, esc, sizeof(esc));

    int n = snprintf(out, out_size, "\"%s\"", esc);
    if (n < 0 || (size_t)n >= out_size) {
        if (out_seq)
            *out_seq = after_seq;
        return 0;
    }

    if (out_seq)
        *out_seq = found;
    return (size_t)n;
}
