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

#define STORAGE_BASE_PATH       "/storage"
#define STORAGE_PARTITION_LABEL "storage"

// Mounts LittleFS at STORAGE_BASE_PATH. Formats automatically on first boot
// (fresh partition) so config.json + defaults always end up written.
bool storage_init(void);

// True if path exists under /storage
bool storage_exists(const char *path);

// Delete a single file (not directories) under /storage.
bool storage_delete(const char *path);

// Erase and reformat the whole LittleFS partition (factory "format" button).
bool storage_format(void);

// Returns used/total bytes for the LittleFS partition (for the storage page & sysinfo).
bool storage_usage(size_t *used, size_t *total);

#endif
