/**
 * @file storage.c
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
 * @brief LittleFS storage back end: mounts (and formats on first boot) the
 * "storage" partition at /storage, and implements file existence, delete, whole
 * partition format and usage queries.
 */

#include <stdio.h>
#include <string.h>

#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "storage.h"

static const char *TAG = "storage";
static bool s_mounted = false;

// Filesystem-wide gate for multi-step write sequences - see the contract in
// storage.h. Created lazily behind a portMUX critical section (the mutex it
// creates cannot guard its own creation) so the first caller wins the race
// whichever task it runs on, and so this file keeps no init-order dependency
// on storage_init(): the same one-time-init pattern app_config.c's
// config_mutex() and igate.c's ensureSockMutex() use.
static SemaphoreHandle_t s_write_mutex = NULL;
static portMUX_TYPE s_write_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;

// Bumped whenever a file changes underneath the subsystem that owns it - a
// whole-partition format, a delete from the web Storage page, or a file
// uploaded over an existing one. Read by the subsystems that keep a parsed
// copy of their LittleFS file in RAM, so those copies are dropped without
// storage.c needing to know which modules exist.
static uint32_t s_generation = 0;

static SemaphoreHandle_t write_mutex(void) {
    if (!s_write_mutex) {
        portENTER_CRITICAL(&s_write_mutex_init_lock);
        if (!s_write_mutex)
            s_write_mutex = xSemaphoreCreateMutex();
        portEXIT_CRITICAL(&s_write_mutex_init_lock);
    }
    return s_write_mutex;
}

void storage_write_lock(void) {
    SemaphoreHandle_t m = write_mutex();
    if (m)
        xSemaphoreTake(m, portMAX_DELAY);
}

void storage_write_unlock(void) {
    if (s_write_mutex)
        xSemaphoreGive(s_write_mutex);
}

uint32_t storage_generation(void) {
    return s_generation;
}

void storage_note_external_change(void) {
    s_generation++;
}

bool storage_init(void) {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = STORAGE_BASE_PATH,
        .partition_label = STORAGE_PARTITION_LABEL,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format LittleFS");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition '%s'", STORAGE_PARTITION_LABEL);
        } else {
            ESP_LOGE(TAG, "Failed to init LittleFS (%s)", esp_err_to_name(ret));
        }
        return false;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(STORAGE_PARTITION_LABEL, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS mounted: %d/%d bytes used", (int)used, (int)total);
    }
    s_mounted = true;
    return true;
}

bool storage_delete(const char *path) {
    if (!s_mounted)
        return false;
    char full[300];
    if (path[0] == '/')
        snprintf(full, sizeof(full), "%s%s", STORAGE_BASE_PATH, path);
    else
        snprintf(full, sizeof(full), "%s/%s", STORAGE_BASE_PATH, path);
    if (remove(full) != 0)
        return false;
    // The file just removed may be one a subsystem holds a parsed copy of, so
    // announce it the same way a format does.
    s_generation++;
    return true;
}

bool storage_format(void) {
    // Hold the writer gate across the erase. Every save on this device is a
    // temp-file + rename sequence, and the modules that perform them
    // (app_config.c, telemetry.c, bulletins.c, objects_items.c) take this same
    // gate, so any save that is part-way through - mid-fwrite, or between the
    // fclose and the rename - runs to completion before the partition goes
    // away, and no save can start against a partition that is being erased.
    storage_write_lock();
    esp_err_t ret = esp_littlefs_format(STORAGE_PARTITION_LABEL);
    if (ret == ESP_OK) {
        // Everything on the partition is gone, so every RAM copy of a file
        // taken before this point is stale. Bumping the generation inside the
        // gate means a reader can never observe the new, empty filesystem
        // together with the old generation.
        s_generation++;
    }
    storage_write_unlock();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Format failed: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGW(TAG, "LittleFS formatted");
    return true;
}

bool storage_usage(size_t *used, size_t *total) {
    return esp_littlefs_info(STORAGE_PARTITION_LABEL, total, used) == ESP_OK;
}
