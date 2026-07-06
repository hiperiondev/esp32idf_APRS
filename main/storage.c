#include <stdio.h>
#include <string.h>

#include "esp_littlefs.h"
#include "esp_log.h"

#include "storage.h"

static const char *TAG = "storage";
static bool s_mounted = false;

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

bool storage_exists(const char *path) {
    if (!s_mounted)
        return false;
    char full[300];
    if (path[0] == '/')
        snprintf(full, sizeof(full), "%s%s", STORAGE_BASE_PATH, path);
    else
        snprintf(full, sizeof(full), "%s/%s", STORAGE_BASE_PATH, path);
    FILE *f = fopen(full, "r");
    if (!f)
        return false;
    fclose(f);
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
    return remove(full) == 0;
}

bool storage_format(void) {
    esp_err_t ret = esp_littlefs_format(STORAGE_PARTITION_LABEL);
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
