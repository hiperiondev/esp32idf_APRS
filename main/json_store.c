// @file json_store.c
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
// @brief Out-of-line part of the shared JSON-file store scaffolding: the one
// stdio buffer every store's temp file is written through, and the open call
// that pins it.

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "json_store.h"

// The single stdio buffer shared by every store. Defined here, in one
// translation unit, rather than in the header: as a static local of an inline
// function every including module would get a private copy, one .bss buffer
// per module for something only one saver can ever be using.
//
// Two things make one buffer enough. Every saver takes its own module mutex
// and then storage_write_lock() (main/storage.c) around the whole temp-file +
// rename sequence, and that gate is filesystem-wide, so no two savers overlap.
// And the buffer belongs to the stream only between json_store_open_tmp() and
// the fclose() inside json_store_commit(), which is entirely inside that gate.
//
// It stays static rather than becoming a local of json_store_open_tmp()
// because a local would put half a kilobyte on the stack of whichever task is
// saving - usually the HTTP server task, whose stack this firmware sizes
// tightly.
static char s_stdio_buf[JSON_STORE_STDIO_BUF_SIZE];

FILE *json_store_open_tmp(const char *tmp_path, const char *tag, SemaphoreHandle_t owner_lock) {
    // The caller's module lock must be held by the calling task: it is what
    // keeps this store's own load/save pass off the save in progress, and its
    // absence would mean the wider write gate was not taken either.
    configASSERT(xSemaphoreGetMutexHolder(owner_lock) == xTaskGetCurrentTaskHandle());

    FILE *f = fopen(tmp_path, "w");
    if (f == NULL) {
        ESP_LOGE(tag, "open tmp for write failed");
        return NULL;
    }

    setvbuf(f, s_stdio_buf, _IOFBF, sizeof(s_stdio_buf));
    return f;
}

// Cursor for the well-formedness scan below. Nothing is copied and nothing is
// allocated: the cursor walks the caller's buffer in place.
typedef struct {
    const char *p;
    int depth;
} json_scan_t;

static bool scan_value(json_scan_t *s);

// Deepest nesting the scan will follow. The stores this module serves nest
// three or four levels at most (an object holding an array of objects holding
// scalars), so a limit well above that still rejects a file crafted to recurse
// this scanner off the end of the caller's stack. Each level costs one small
// frame, which is what makes the limit a stack budget rather than a taste.
#define JSON_SCAN_MAX_DEPTH 16

static void scan_ws(json_scan_t *s) {
    while (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r')
        s->p++;
}

// A JSON string: the opening quote is already known to be there.
static bool scan_string(json_scan_t *s) {
    s->p++; // opening quote
    for (;;) {
        unsigned char c = (unsigned char)*s->p;
        if (c == '\0')
            return false; // unterminated
        if (c == '"') {
            s->p++;
            return true;
        }
        if (c == '\\') {
            s->p++;
            switch (*s->p) {
                case '"':
                case '\\':
                case '/':
                case 'b':
                case 'f':
                case 'n':
                case 'r':
                case 't':
                    s->p++;
                    break;
                case 'u':
                    s->p++;
                    for (int i = 0; i < 4; i++) {
                        char h = *s->p;
                        bool hex = (h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F');
                        if (!hex)
                            return false;
                        s->p++;
                    }
                    break;
                default:
                    return false; // unknown escape
            }
            continue;
        }
        if (c < 0x20)
            return false; // raw control character
        s->p++;
    }
}

// A JSON number, in the grammar RFC 8259 gives: an optional minus, an integer
// part that is either a lone zero or a digit string not starting with zero, an
// optional fraction and an optional exponent.
static bool scan_number(json_scan_t *s) {
    if (*s->p == '-')
        s->p++;
    if (*s->p == '0') {
        s->p++;
    } else if (*s->p >= '1' && *s->p <= '9') {
        while (*s->p >= '0' && *s->p <= '9')
            s->p++;
    } else {
        return false;
    }
    if (*s->p == '.') {
        s->p++;
        if (*s->p < '0' || *s->p > '9')
            return false;
        while (*s->p >= '0' && *s->p <= '9')
            s->p++;
    }
    if (*s->p == 'e' || *s->p == 'E') {
        s->p++;
        if (*s->p == '+' || *s->p == '-')
            s->p++;
        if (*s->p < '0' || *s->p > '9')
            return false;
        while (*s->p >= '0' && *s->p <= '9')
            s->p++;
    }
    return true;
}

// One of the three bare literals, matched whole so that a truncated "tru" is
// rejected rather than read as a prefix.
static bool scan_literal(json_scan_t *s, const char *word) {
    size_t n = strlen(word);
    if (strncmp(s->p, word, n) != 0)
        return false;
    s->p += n;
    return true;
}

static bool scan_object(json_scan_t *s) {
    s->p++; // opening brace
    scan_ws(s);
    if (*s->p == '}') {
        s->p++;
        return true;
    }
    for (;;) {
        scan_ws(s);
        if (*s->p != '"')
            return false; // a member name is always a string
        if (!scan_string(s))
            return false;
        scan_ws(s);
        if (*s->p != ':')
            return false;
        s->p++;
        if (!scan_value(s))
            return false;
        scan_ws(s);
        if (*s->p == ',') {
            s->p++;
            continue;
        }
        if (*s->p == '}') {
            s->p++;
            return true;
        }
        return false;
    }
}

static bool scan_array(json_scan_t *s) {
    s->p++; // opening bracket
    scan_ws(s);
    if (*s->p == ']') {
        s->p++;
        return true;
    }
    for (;;) {
        if (!scan_value(s))
            return false;
        scan_ws(s);
        if (*s->p == ',') {
            s->p++;
            continue;
        }
        if (*s->p == ']') {
            s->p++;
            return true;
        }
        return false;
    }
}

static bool scan_value(json_scan_t *s) {
    if (++s->depth > JSON_SCAN_MAX_DEPTH)
        return false;
    scan_ws(s);
    bool ok;
    switch (*s->p) {
        case '{':
            ok = scan_object(s);
            break;
        case '[':
            ok = scan_array(s);
            break;
        case '"':
            ok = scan_string(s);
            break;
        case 't':
            ok = scan_literal(s, "true");
            break;
        case 'f':
            ok = scan_literal(s, "false");
            break;
        case 'n':
            ok = scan_literal(s, "null");
            break;
        default:
            ok = scan_number(s);
            break;
    }
    s->depth--;
    return ok;
}

bool json_store_text_is_well_formed(const char *text) {
    if (text == NULL)
        return false;

    json_scan_t s = { .p = text, .depth = 0 };

    // The byte order mark is not part of JSON, but an editor on a desktop can
    // leave one at the front of a file the operator uploaded, and cJSON skips
    // it before parsing. Skipping it here too keeps the two in step, so such a
    // file is never called well-formed by one and rejected by the other.
    if ((unsigned char)s.p[0] == 0xEF && (unsigned char)s.p[1] == 0xBB && (unsigned char)s.p[2] == 0xBF)
        s.p += 3;

    if (!scan_value(&s))
        return false;
    scan_ws(&s);
    return *s.p == '\0'; // trailing garbage is not well-formed
}
