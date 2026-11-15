#pragma once
#include "esp_err.h"
#include <stdarg.h>
#define ESP_LOGE(t, ...) do{}while(0)
#define ESP_LOGW(t, ...) do{}while(0)
#define ESP_LOGI(t, ...) do{}while(0)
#define ESP_LOGD(t, ...) do{}while(0)
#define ESP_LOGV(t, ...) do{}while(0)
typedef int (*vprintf_like_t)(const char *, va_list);
vprintf_like_t esp_log_set_vprintf(vprintf_like_t f);
