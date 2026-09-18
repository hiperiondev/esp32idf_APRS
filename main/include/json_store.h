/**
 * @file json_store.h
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
 * @brief Scaffolding shared by every subsystem that keeps its own JSON file on
 * LittleFS (the per-functionality configuration files, bulletins.json,
 * objitems.json, telemetry.json, telegram.json, winlink_mail.json).
 *
 * What each of those files contains is entirely its owner's business, and stays
 * there. What they all have to get right in exactly the same way is the part
 * around it, and that is what lives here:
 *
 *  - the lazily created per-module mutex that keeps the web save handler and
 *    the scheduler pass off each other's load/save;
 *  - reading a file into one heap buffer and handing back the parsed document,
 *    telling the caller apart the three cases it has to react to differently
 *    (absent, empty, unreadable/corrupt);
 *  - opening the temp file with a pinned stdio buffer;
 *  - committing that temp file over the live one in a single rename.
 *
 * The two write-side steps carry guarantees that are easy to lose and expensive
 * to lose - a transient 4 KB allocation that crashes the save on a fragmented
 * heap, and a commit sequence that can leave no file at all across a power cut -
 * so they are written once and every store gets them.
 *
 * Everything here is static inline except json_store_open_tmp(), which lives in
 * main/json_store.c because it owns the one stdio buffer all the stores write
 * their temp file through: a single definition, not one per including module.
 */

#ifndef JSON_STORE_H
#define JSON_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/** Size of the pinned stdio buffer given to every store's temp file. */
#define JSON_STORE_STDIO_BUF_SIZE 512

/**
 * @brief Report whether a NUL-terminated buffer holds one well-formed JSON
 * document.
 *
 * @details Walks the buffer in place against the RFC 8259 grammar and allocates
 * nothing at all, which is the entire point: it is asked precisely when the
 * allocating parser has just failed, so anything it allocated itself could fail
 * for the same reason and turn a diagnosis into a second failure.
 *
 * It exists to tell an out-of-memory parse apart from a genuinely corrupt file.
 * cJSON reports both by returning NULL, and the two demand opposite responses -
 * leave the file alone and retry, or replace it - so ::json_store_read consults
 * this before deciding which happened.
 *
 * The scan is deliberately no more permissive than cJSON: everything it accepts
 * cJSON accepts too, so text it calls well-formed can only have failed to parse
 * for want of memory. Where the two differ, this one is the stricter, which
 * errs toward calling a file corrupt rather than toward retrying a file that
 * will never parse. Nesting deeper than sixteen levels is rejected, so a
 * pathological file cannot drive the scan off the caller's stack.
 *
 * A leading UTF-8 byte order mark is skipped, as cJSON skips it, so a file an
 * operator saved from a desktop editor is judged the same way by both.
 *
 * @param text NUL-terminated candidate JSON text; NULL reads as not well-formed.
 * @return true if the whole buffer is exactly one well-formed JSON value, with
 *         nothing but whitespace after it.
 */
bool json_store_text_is_well_formed(const char *text);

/**
 * @brief Outcome of json_store_read().
 */
typedef enum {
    JSON_STORE_OK = 0,  /**< File read and parsed; the document is returned. */
    JSON_STORE_MISSING, /**< File does not exist yet (first boot, or erased). */
    JSON_STORE_EMPTY,   /**< File exists but holds no bytes. */
    JSON_STORE_OOM,     /**< File exists but there was no RAM to read it into. */
    JSON_STORE_CORRUPT, /**< File was read but is not parseable JSON. */
} json_store_status_t;

/**
 * @brief Create a store's mutex if it does not exist yet.
 *
 * The lazy create carries no double-init guard because the first call always
 * happens single-threaded: a store's start function runs from main.c during
 * bring-up, and its web page cannot be fetched before the network and the HTTP
 * server are up, both of which happen after that. Calling this from the start
 * function puts the creation on that known-quiet path instead of on whichever
 * task happens to reach the store first.
 *
 * @param lock Address of the module's semaphore handle.
 */
static inline void json_store_lock_ensure(SemaphoreHandle_t *lock) {
    if (*lock == NULL)
        *lock = xSemaphoreCreateMutex();
}

/**
 * @brief Take a store's mutex, creating it on first use.
 *
 * @param lock Address of the module's semaphore handle.
 */
static inline void json_store_lock_take(SemaphoreHandle_t *lock) {
    json_store_lock_ensure(lock);
    if (*lock != NULL)
        xSemaphoreTake(*lock, portMAX_DELAY);
}

/**
 * @brief Release a mutex taken with json_store_lock_take().
 *
 * @param lock Address of the module's semaphore handle.
 */
static inline void json_store_lock_give(SemaphoreHandle_t *lock) {
    if (*lock != NULL)
        xSemaphoreGive(*lock);
}

/**
 * @brief Read a store's file and parse it.
 *
 * The file is read into a single heap buffer sized from its own length and
 * released as soon as cJSON has consumed it, so the peak cost of a load is one
 * copy of the file plus the parsed tree rather than anything proportional to
 * the largest file the device might ever hold.
 *
 * The failure cases are reported separately because callers act on them
 * differently. An absent or empty file usually means "write the defaults out".
 * A corrupt one is a decision each store makes for itself - the boot
 * configuration rewrites defaults over it so the device always comes up, while
 * the optional stores leave it alone for the operator to look at. And an
 * out-of-memory read must never be mistaken for either: the file is very
 * probably fine, so overwriting it on the strength of a failed malloc would
 * destroy a good configuration.
 *
 * ::JSON_STORE_OOM therefore covers both places memory can run out, not just
 * the obvious one. The read buffer is a single allocation sized from the file
 * and fails visibly; the parse that follows makes hundreds of small ones and
 * fails by returning NULL, which is also how cJSON reports text that is not
 * JSON at all. ::json_store_text_is_well_formed tells those two apart without
 * allocating anything, so a parse that ran out of memory on a good file is
 * reported as ::JSON_STORE_OOM rather than as ::JSON_STORE_CORRUPT.
 *
 * @param path    Full path of the file to read.
 * @param tag     Caller's log tag.
 * @param what    Human-readable name of the content, for the log lines
 *                (e.g. "bulletins").
 * @param out_doc Receives the parsed document on ::JSON_STORE_OK, NULL
 *                otherwise. The caller owns it and must cJSON_Delete() it.
 * @return The outcome; only ::JSON_STORE_OK yields a document.
 */
static inline json_store_status_t json_store_read(const char *path, const char *tag, const char *what, cJSON **out_doc) {
    *out_doc = NULL;

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        ESP_LOGI(tag, "%s not present - starting with empty %s", path, what);
        return JSON_STORE_MISSING;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        return JSON_STORE_EMPTY;
    }

    char *buf = malloc((size_t)sz + 1);
    if (buf == NULL) {
        fclose(f);
        ESP_LOGW(tag, "OOM reading %s", what);
        return JSON_STORE_OOM;
    }

    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = 0;
    fclose(f);

    cJSON *doc = cJSON_Parse(buf);
    if (doc == NULL) {
        // cJSON returns NULL for two unrelated reasons - the text is not JSON,
        // or it ran out of memory building the tree - and reports both the same
        // way. The two must not be confused: a store that treats a failed
        // allocation as a corrupt file writes defaults over a configuration
        // that was perfectly good, so a transient shortage becomes permanent
        // data loss.
        //
        // The text itself is what settles it. A scan that allocates nothing
        // decides whether these bytes are well-formed JSON; if they are, the
        // parse can only have failed for want of memory, and the caller is told
        // to leave the file alone and try again later.
        bool well_formed = json_store_text_is_well_formed(buf);
        free(buf);
        if (well_formed) {
            ESP_LOGW(tag, "OOM parsing %s - the file scans as valid JSON and is left untouched", what);
            return JSON_STORE_OOM;
        }
        ESP_LOGW(tag, "%s corrupt", path);
        return JSON_STORE_CORRUPT;
    }
    free(buf);

    *out_doc = doc;
    return JSON_STORE_OK;
}

/**
 * @brief Open a store's temp file for writing, with its stdio buffer pinned.
 *
 * Giving the stream a fixed buffer before the first byte is written is the
 * whole point of this call. Left alone, newlib allocates the stdio buffer
 * lazily on the first fputc() and sizes it from st_blksize - which esp_littlefs
 * reports as the 4096-byte flash block. On this device's small, fragmented heap
 * that transient malloc(4096) is not reliably satisfiable, and a stream that
 * cannot get its buffer falls back to the unbuffered per-byte path
 * (__swbuf_r), which makes a save far slower and far more sensitive to heap
 * exhaustion. Pinning a buffer removes the allocation entirely and coalesces
 * the hundreds of token writes a save performs into a handful of block writes.
 *
 * The buffer is a single static object in main/json_store.c, shared by every
 * store: every configuration file, telemetry.json, bulletins.json,
 * objitems.json, telegram.json and winlink_mail.json all write their temp file
 * through the same
 * ::JSON_STORE_STDIO_BUF_SIZE bytes. It is static rather than a local so it
 * does not add half a kilobyte to the stack of whichever task is saving
 * (usually the HTTP server task, whose stack this firmware sizes tightly), and
 * it is defined once rather than per translation unit because only one saver
 * can ever be using it.
 *
 * That is a guarantee, not an assumption. Every saver takes its module mutex
 * and then ::storage_write_lock (main/storage.c) around the whole temp-file +
 * rename sequence; that gate is filesystem-wide, so two saves can never
 * overlap, and the buffer belongs to a stream only between this call and the
 * fclose() inside json_store_commit(), which is entirely inside the gate.
 * Holding @p owner_lock is the caller-side half of that contract, and the
 * call checks it instead of merely documenting it, so any relaxation of the
 * locking fails loudly instead of letting one save corrupt another's output.
 *
 * @param tmp_path   Path of the temp file to create.
 * @param tag        Caller's log tag.
 * @param owner_lock The module mutex the caller must be holding.
 * @return The open stream, or NULL if it could not be created.
 */
FILE *json_store_open_tmp(const char *tmp_path, const char *tag, SemaphoreHandle_t owner_lock);

/**
 * @brief Finish a store's temp file and commit it over the live one.
 *
 * Any deferred stdio or filesystem error is caught before the commit, so a full
 * or failing filesystem can never put a truncated file in place.
 *
 * The rename is the entire atomicity guarantee: LittleFS replaces an existing
 * destination as one metadata update, so at every instant - including across a
 * power loss - @p path is either the previous file or the new one, never
 * missing. Unlinking the destination first would open exactly the window this
 * design exists to close: a crash in that window leaves no file at all, and the
 * next boot writes factory defaults over the station's configuration. On any
 * failure the temp file is removed, so a stale half-written ".tmp" is never
 * left behind on the filesystem.
 *
 * @param f        Stream returned by json_store_open_tmp(); always closed.
 * @param tmp_path Path of the temp file.
 * @param path     Path of the live file to replace.
 * @param tag      Caller's log tag.
 * @param what     Human-readable name of the content, for the log lines.
 * @return true if the live file now holds the new content.
 */
static inline bool json_store_commit(FILE *f, const char *tmp_path, const char *path, const char *tag, const char *what) {
    bool ok = (fflush(f) == 0) && (ferror(f) == 0);
    if (fclose(f) != 0)
        ok = false;

    if (!ok) {
        ESP_LOGE(tag, "write error while saving %s (free heap=%u bytes)", what, (unsigned)esp_get_free_heap_size());
        remove(tmp_path);
        return false;
    }

    if (rename(tmp_path, path) != 0) {
        ESP_LOGE(tag, "rename tmp->%s failed", path);
        remove(tmp_path);
        return false;
    }

    ESP_LOGI(tag, "Saved %s", what);
    return true;
}

#endif /* JSON_STORE_H */
