/**
 * @file sensors_local.c
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
 * @brief Run-time registry of dynamically loaded local sensor drivers and the
 *        common ::sensors_local_save aggregation entry. See sensors_local.h.
 */

#include "sensors_local.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "sensors_local";

/* --------------------------------------------------------------------------
 * The "dynamic functions pointer" table: a heap array of driver pointers that
 * grows on demand. Each slot points at a caller-owned sensor_local_driver_t
 * (a bundle of function pointers), never a copy.
 * -------------------------------------------------------------------------- */
static sensor_local_driver_t **s_registry = NULL; /**< Dynamic array of driver pointers. */
static size_t s_count = 0;                        /**< Number of live entries. */
static size_t s_capacity = 0;                     /**< Allocated slots in s_registry. */

/* Guards the registry. Created in sensors_local_init(); NULL means "not yet
 * created", which happens during the C-constructor phase (single threaded,
 * scheduler not running) where locking is neither possible nor needed. */
static SemaphoreHandle_t s_lock = NULL;

static inline void registry_lock(void) {
    if (s_lock != NULL)
        xSemaphoreTake(s_lock, portMAX_DELAY);
}

static inline void registry_unlock(void) {
    if (s_lock != NULL)
        xSemaphoreGive(s_lock);
}

/* Find index of a driver by name. Caller must hold the lock. Returns SIZE_MAX
 * when not present. */
static size_t registry_index_of(const char *name) {
    for (size_t i = 0; i < s_count; i++) {
        if (s_registry[i] != NULL && s_registry[i]->name != NULL && strcmp(s_registry[i]->name, name) == 0)
            return i;
    }
    return (size_t)-1;
}

/* Lazily run a driver's init() exactly once. Caller must hold the lock. */
static esp_err_t ensure_initialized(sensor_local_driver_t *d) {
    if (d->failed)
        return ESP_FAIL;
    if (d->initialized)
        return ESP_OK;

    if (d->init != NULL) {
        esp_err_t err = d->init(d);
        if (err != ESP_OK) {
            d->failed = true;
            ESP_LOGW(TAG, "driver '%s' init failed: %s", d->name ? d->name : "?", esp_err_to_name(err));
            return err;
        }
    }
    d->initialized = true;
    return ESP_OK;
}

esp_err_t sensors_local_init(void) {
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            ESP_LOGE(TAG, "could not create registry mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG, "initialised (%u driver(s) already registered)", (unsigned)s_count);
    return ESP_OK;
}

esp_err_t sensors_local_register(sensor_local_driver_t *driver) {
    if (driver == NULL || driver->save == NULL || driver->name == NULL || driver->name[0] == '\0') {
        ESP_LOGE(TAG, "rejected malformed driver descriptor");
        return ESP_ERR_INVALID_ARG;
    }

    registry_lock();

    if (registry_index_of(driver->name) != (size_t)-1) {
        registry_unlock();
        ESP_LOGE(TAG, "driver '%s' already registered", driver->name);
        return ESP_ERR_INVALID_STATE;
    }

    if (s_count == s_capacity) {
        size_t new_cap = (s_capacity == 0) ? 4 : s_capacity * 2;
        sensor_local_driver_t **grown = realloc(s_registry, new_cap * sizeof(*grown));
        if (grown == NULL) {
            registry_unlock();
            ESP_LOGE(TAG, "out of memory growing registry to %u", (unsigned)new_cap);
            return ESP_ERR_NO_MEM;
        }
        s_registry = grown;
        s_capacity = new_cap;
    }

    driver->initialized = false;
    driver->failed = false;
    s_registry[s_count++] = driver;

    registry_unlock();
    ESP_LOGI(TAG, "registered driver '%s' (caps=0x%02x)", driver->name, (unsigned)driver->capabilities);
    return ESP_OK;
}

esp_err_t sensors_local_unregister(const char *name) {
    if (name == NULL)
        return ESP_ERR_INVALID_ARG;

    registry_lock();
    size_t idx = registry_index_of(name);
    if (idx == (size_t)-1) {
        registry_unlock();
        return ESP_ERR_NOT_FOUND;
    }

    sensor_local_driver_t *d = s_registry[idx];
    /* Compact the array so positions stay contiguous. */
    memmove(&s_registry[idx], &s_registry[idx + 1], (s_count - idx - 1) * sizeof(*s_registry));
    s_count--;
    registry_unlock();

    if (d->deinit != NULL && d->initialized)
        d->deinit(d);
    d->initialized = false;

    ESP_LOGI(TAG, "unregistered driver '%s'", name);
    return ESP_OK;
}

size_t sensors_local_count(void) {
    registry_lock();
    size_t n = s_count;
    registry_unlock();
    return n;
}

sensor_local_driver_t *sensors_local_get(size_t index) {
    registry_lock();
    sensor_local_driver_t *d = (index < s_count) ? s_registry[index] : NULL;
    registry_unlock();
    return d;
}

sensor_local_driver_t *sensors_local_find(const char *name) {
    if (name == NULL)
        return NULL;
    registry_lock();
    size_t idx = registry_index_of(name);
    sensor_local_driver_t *d = (idx == (size_t)-1) ? NULL : s_registry[idx];
    registry_unlock();
    return d;
}

esp_err_t sensors_local_init_all(void) {
    esp_err_t result = ESP_OK;
    registry_lock();
    for (size_t i = 0; i < s_count; i++) {
        if (ensure_initialized(s_registry[i]) != ESP_OK)
            result = ESP_FAIL;
    }
    registry_unlock();
    return result;
}

esp_err_t sensors_local_save(weather_telemetry_data_t *data, sensor_local_data_kind_t kind) {
    if (data == NULL) {
        ESP_LOGE(TAG, "sensors_local_save: NULL data container");
        return ESP_ERR_INVALID_ARG;
    }
    if (kind == SENSOR_LOCAL_DATA_NONE)
        return ESP_OK;

    registry_lock();
    for (size_t i = 0; i < s_count; i++) {
        sensor_local_driver_t *d = s_registry[i];

        /* Skip drivers that cannot serve any of the requested families. */
        if ((d->capabilities & (uint32_t)kind) == 0)
            continue;

        if (ensure_initialized(d) != ESP_OK)
            continue; /* already logged; keep going */

        /* Only ask the driver for the families it actually advertises. */
        sensor_local_data_kind_t ask = (sensor_local_data_kind_t)(d->capabilities & (uint32_t)kind);

        esp_err_t err = d->save(d, data, ask);
        if (err != ESP_OK)
            ESP_LOGW(TAG, "driver '%s' save failed: %s", d->name ? d->name : "?", esp_err_to_name(err));
    }
    registry_unlock();
    return ESP_OK;
}

void sensors_local_deinit(void) {
    registry_lock();
    for (size_t i = 0; i < s_count; i++) {
        sensor_local_driver_t *d = s_registry[i];
        if (d != NULL && d->deinit != NULL && d->initialized)
            d->deinit(d);
        if (d != NULL)
            d->initialized = false;
    }
    free(s_registry);
    s_registry = NULL;
    s_count = 0;
    s_capacity = 0;
    registry_unlock();
    ESP_LOGI(TAG, "registry cleared");
}
