#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint32_t TickType_t; typedef int BaseType_t; typedef unsigned UBaseType_t;
typedef void *TaskHandle_t; typedef void *SemaphoreHandle_t; typedef void *QueueHandle_t;
typedef struct { int x; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {0}
#define portENTER_CRITICAL(m) (void)(m)
#define portEXIT_CRITICAL(m) (void)(m)
#define taskENTER_CRITICAL(m) (void)(m)
#define taskEXIT_CRITICAL(m) (void)(m)
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
#define pdTICKS_TO_MS(x) (x)
#define portTICK_PERIOD_MS 1
#define portMAX_DELAY 0xffffffffu
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define tskNO_AFFINITY 0x7fffffff
#define configMAX_PRIORITIES 25
BaseType_t xPortInIsrContext(void);
