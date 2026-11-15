#pragma once
#include "esp_err.h"
#include "esp_app_desc.h"
#include <stddef.h>
typedef uint32_t esp_ota_handle_t;
typedef struct { int type; int subtype; uint32_t address; uint32_t size; char label[17]; } esp_partition_t;
#define OTA_SIZE_UNKNOWN 0xffffffff
#define OTA_WITH_SEQUENTIAL_WRITES 0xfffffffe
const esp_partition_t *esp_ota_get_running_partition(void);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *s);
const esp_partition_t *esp_ota_get_boot_partition(void);
esp_err_t esp_ota_begin(const esp_partition_t *p, size_t s, esp_ota_handle_t *h);
esp_err_t esp_ota_write(esp_ota_handle_t h, const void *d, size_t s);
esp_err_t esp_ota_end(esp_ota_handle_t h);
esp_err_t esp_ota_abort(esp_ota_handle_t h);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p);
