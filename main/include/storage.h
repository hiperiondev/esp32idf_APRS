/**
 * @file storage.h
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
 * @brief LittleFS storage back end mounted at /storage.
 *
 * Mounts the "storage" partition, formatting automatically on first boot (fresh
 * partition) so the per-functionality configuration files and their defaults
 * always end up written, and exposes file existence / delete / format / usage
 * helpers used by the configuration loader and by the web admin storage page.
 */

#ifndef APP_STORAGE_H
#define APP_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "must_check.h" // APRS_MUST_CHECK: the persistence entry points below may not have their result discarded

#define STORAGE_BASE_PATH       "/storage" /**< VFS mount point for the LittleFS storage partition. */
#define STORAGE_PARTITION_LABEL "storage"  /**< Partition-table label of the LittleFS storage partition. */

/**
 * @brief Mount LittleFS at ::STORAGE_BASE_PATH, formatting automatically on
 * first boot (fresh partition) so the configuration files + defaults always
 * end up written.
 * @return true on success.
 */
bool storage_init(void);

/**
 * @brief Delete a single flat file under /storage.
 *
 * @p path must name a plain file directly under the storage root: an
 * optional single leading '/' is accepted, but a '..' segment or any other
 * '/' or '\' anywhere in the name is rejected internally, independent of any
 * sanitization the caller may already have performed. Bumps
 * ::storage_generation on success, since the file may be one a subsystem
 * holds a RAM copy of. The removal is made under ::storage_write_lock, so it
 * never lands between the steps of a save of the same file.
 *
 * @param path Path of the file to remove.
 * @return true if the file was deleted.
 *
 * @note Declared ::APRS_MUST_CHECK: a call site that discards the result
 * reports success to the user for a write that may never have reached
 * flash, so ignoring it fails the build.
 */
bool storage_delete(const char *path) APRS_MUST_CHECK;

/**
 * @brief Erase and reformat the whole LittleFS partition (the factory
 * "format" button).
 *
 * Takes the multi-step writer gate (::storage_write_lock) for the whole
 * operation, so a save that is part-way through its temp-file + rename
 * sequence completes before the partition is erased, and bumps
 * ::storage_generation so subsystems that keep a RAM copy of a file know that
 * copy no longer reflects the filesystem.
 *
 * @return true on success.
 *
 * @note Declared ::APRS_MUST_CHECK: a call site that discards the result
 * reports success to the user for a write that may never have reached
 * flash, so ignoring it fails the build.
 */
bool storage_format(void) APRS_MUST_CHECK;

/**
 * @brief Take the filesystem-wide writer gate.
 *
 * esp_littlefs already serializes each individual VFS call, so this gate is
 * not about metadata integrity: it exists for sequences that are only correct
 * as a whole - the "write temp file, then rename it over the live one" save
 * used by the per-functionality configuration files, telemetry.json,
 * bulletins.json, objitems.json and
 * telegram.json, the whole-partition erase done by ::storage_format, and the
 * single-file changes of ::storage_delete and ::storage_replace.
 *
 * Ordering contract: a caller may take its own module lock first and this gate
 * second (that is what the savers do), never the other way round.
 * ::storage_format takes only this gate, so the two can never deadlock.
 */
void storage_write_lock(void);

/**
 * @brief Release the gate taken by ::storage_write_lock.
 */
void storage_write_unlock(void);

/**
 * @brief Monotonic counter of changes made to the filesystem from outside the
 * subsystem that owns the affected file.
 *
 * Incremented by ::storage_format, by ::storage_delete and by
 * ::storage_replace (which the web Storage page's upload handler calls). Subsystems that cache a parsed file in RAM store the value seen when
 * they read it and drop the cache when it no longer matches, so a file
 * replaced, uploaded over or erased behind their back is picked up without
 * every module having to be told about it individually.
 *
 * The counter is bumped from whichever task performs the change, so it is
 * incremented and read atomically. Callers may therefore read it without
 * holding any lock; a value read concurrently with a change is either the old
 * or the new one, never a partial update.
 *
 * @return The current generation.
 */
uint32_t storage_generation(void);

/**
 * @brief Move a fully written temporary file over a file under /storage, as
 * one step under ::storage_write_lock, and bump ::storage_generation.
 *
 * This is how a file that does not belong to any subsystem's own saver - the
 * web Storage page's upload - reaches the filesystem: the content is written
 * to @p tmp_name first and only a complete file replaces @p name, so an
 * upload that fails part-way leaves the previous file untouched, and the
 * replacement can never interleave with a module's own temp-file + rename
 * save of the same file. The generation bump tells every subsystem that keeps
 * a parsed copy of a file in RAM (the per-functionality configuration files,
 * telemetry.json, bulletins.json, objitems.json) that the copy may be stale.
 *
 * Both names follow the rules of ::storage_delete: a plain file directly under
 * the storage root, with an optional single leading '/'.
 *
 * @param tmp_name Name of the complete temporary file.
 * @param name     Name of the file it replaces (or creates).
 * @return true if the file was moved into place; false if either name is
 *         unusable or the rename failed, in which case the temporary file is
 *         left for the caller to remove.
 *
 * @note Declared ::APRS_MUST_CHECK: a call site that discards the result
 * reports success to the user for a write that may never have reached
 * flash, so ignoring it fails the build.
 */
bool storage_replace(const char *tmp_name, const char *name) APRS_MUST_CHECK;

/**
 * @brief Report used/total bytes for the LittleFS partition (for the storage
 * page and sysinfo).
 * @param used  Set to the number of bytes currently used.
 * @param total Set to the total partition size in bytes.
 * @return true on success.
 */
bool storage_usage(size_t *used, size_t *total);

#endif
