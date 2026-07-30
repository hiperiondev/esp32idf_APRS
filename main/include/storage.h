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

#define STORAGE_BASE_PATH       "/storage"       /**< VFS mount point for the LittleFS storage partition. */
#define STORAGE_PARTITION_LABEL "storage"        /**< Partition-table label of the LittleFS storage partition. */

/**
 * @brief Mount LittleFS at ::STORAGE_BASE_PATH, formatting automatically on
 * first boot (fresh partition) so config.json + defaults always end up
 * written.
 * @return true on success.
 */
bool storage_init(void);

/**
 * @brief Delete a single file (not directories) under /storage.
 * @param path Path of the file to remove.
 * @return true if the file was deleted.
 */
bool storage_delete(const char *path);

/**
 * @brief Erase and reformat the whole LittleFS partition (the factory
 * "format" button).
 * @return true on success.
 */
bool storage_format(void);

/**
 * @brief Report used/total bytes for the LittleFS partition (for the storage
 * page and sysinfo).
 * @param used  Set to the number of bytes currently used.
 * @param total Set to the total partition size in bytes.
 * @return true on success.
 */
bool storage_usage(size_t *used, size_t *total);

#endif
