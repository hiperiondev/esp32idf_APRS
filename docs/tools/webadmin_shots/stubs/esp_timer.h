#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct esp_timer *esp_timer_handle_t;
typedef struct { void (*callback)(void*); void *arg; int dispatch_method; const char *name; int skip_unhandled_events; } esp_timer_create_args_t;
int64_t esp_timer_get_time(void);
esp_err_t esp_timer_create(const esp_timer_create_args_t *a, esp_timer_handle_t *h);
esp_err_t esp_timer_start_once(esp_timer_handle_t h, uint64_t us);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t h, uint64_t us);
esp_err_t esp_timer_stop(esp_timer_handle_t h);
