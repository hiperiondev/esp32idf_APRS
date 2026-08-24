// @file json_store.c
//
// @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
// @date 2026
// @copyright GNU General Public License v3
// @see https://github.com/hiperiondev/esp32idf_APRS
//
// @note
// This is based on other projects:
//     VP-Digi: https://github.com/sq8vps/vp-digi
//     ESP32APRS: https://github.com/nakhonthai/ESP32APRS_Audio
//     LibAPRS: https://github.com/markqvist/LibAPRS
//
//     please contact their authors for more information.
//
// @brief Out-of-line part of the shared JSON-file store scaffolding: the one
// stdio buffer every store's temp file is written through, and the open call
// that pins it.

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "json_store.h"

// The single stdio buffer shared by every store. Defined here, in one
// translation unit, rather than in the header: as a static local of an inline
// function each of the five including modules would get a private copy, five
// times the .bss for a buffer only one saver can ever be using.
//
// Two things make one buffer enough. Every saver takes its own module mutex
// and then storage_write_lock() (main/storage.c) around the whole temp-file +
// rename sequence, and that gate is filesystem-wide, so no two savers overlap.
// And the buffer belongs to the stream only between json_store_open_tmp() and
// the fclose() inside json_store_commit(), which is entirely inside that gate.
//
// It stays static rather than becoming a local of json_store_open_tmp()
// because a local would put half a kilobyte on the stack of whichever task is
// saving - usually the HTTP server task, whose stack this firmware sizes
// tightly.
static char s_stdio_buf[JSON_STORE_STDIO_BUF_SIZE];

FILE *json_store_open_tmp(const char *tmp_path, const char *tag, SemaphoreHandle_t owner_lock) {
    // The caller's module lock must be held by the calling task: it is what
    // keeps this store's own load/save pass off the save in progress, and its
    // absence would mean the wider write gate was not taken either.
    configASSERT(xSemaphoreGetMutexHolder(owner_lock) == xTaskGetCurrentTaskHandle());

    FILE *f = fopen(tmp_path, "w");
    if (f == NULL) {
        ESP_LOGE(tag, "open tmp for write failed");
        return NULL;
    }

    setvbuf(f, s_stdio_buf, _IOFBF, sizeof(s_stdio_buf));
    return f;
}
