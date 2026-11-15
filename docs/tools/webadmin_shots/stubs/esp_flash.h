#pragma once
#include "esp_err.h"
typedef struct esp_flash_t esp_flash_t;
extern esp_flash_t *esp_flash_default_chip;
esp_err_t esp_flash_get_size(esp_flash_t *c, uint32_t *s);
