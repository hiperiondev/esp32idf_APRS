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
 * partition) so config.json and defaults always end up written, and exposes file
 * existence / delete / format / usage helpers used by the configuration loader
 * and by the web admin storage page.
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
 * first boot (fresh partition) so config.json + defaults always end up
 * written.
 * @return true on success.
 */
bool storage_init(void);

/**
 * @brief Delete a single file (not directories) under /storage.
 *
 * Bumps ::storage_generation on success, since the file may be one a
 * subsystem holds a RAM copy of.
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
 * used by config.json, telemetry.json, bulletins.json and objitems.json, and
 * the whole-partition erase done by ::storage_format.
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
 * Incremented by ::storage_format, by ::storage_delete, and by
 * ::storage_note_external_change (which the web Storage page's upload handler
 * calls). Subsystems that cache a parsed file in RAM store the value seen when
 * they read it and drop the cache when it no longer matches, so a file
 * replaced, uploaded over or erased behind their back is picked up without
 * every module having to be told about it individually.
 *
 * @return The current generation.
 */
uint32_t storage_generation(void);

/**
 * @brief Report that a file under /storage was created, replaced or removed by
 * something other than the subsystem that owns it, bumping
 * ::storage_generation.
 *
 * Used by the web Storage page's file upload, which can drop a new
 * config.json / telemetry.json / bulletins.json / objitems.json straight onto
 * the filesystem.
 */
void storage_note_external_change(void);

/**
 * @brief Report used/total bytes for the LittleFS partition (for the storage
 * page and sysinfo).
 * @param used  Set to the number of bytes currently used.
 * @param total Set to the total partition size in bytes.
 * @return true on success.
 */
bool storage_usage(size_t *used, size_t *total);

#endif
