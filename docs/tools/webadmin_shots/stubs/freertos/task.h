#pragma once
#include "FreeRTOS.h"
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t t);
BaseType_t xTaskCreate(void (*f)(void*), const char *n, uint32_t s, void *p, UBaseType_t pr, TaskHandle_t *h);
BaseType_t xTaskCreatePinnedToCore(void (*f)(void*), const char *n, uint32_t s, void *p, UBaseType_t pr, TaskHandle_t *h, int core);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t h);
void vTaskDelete(TaskHandle_t h);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
