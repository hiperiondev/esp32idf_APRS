#pragma once
#include <stdint.h>
typedef enum { CHIP_ESP32=1 } esp_chip_model_t;
typedef struct { esp_chip_model_t model; uint32_t features; uint16_t revision; uint8_t cores; } esp_chip_info_t;
#define CHIP_FEATURE_WIFI_BGN (1<<1)
#define CHIP_FEATURE_BT (1<<4)
#define CHIP_FEATURE_BLE (1<<5)
#define CHIP_FEATURE_EMB_FLASH 1
void esp_chip_info(esp_chip_info_t *i);
