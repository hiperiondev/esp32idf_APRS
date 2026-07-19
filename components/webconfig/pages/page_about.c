/**
 * @file page_about.c
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
 * @brief Web admin "About" page: shows project, firmware and credit
 * information, and implements the OTA firmware update flow (upload a .bin
 * over HTTP, stream it straight into the inactive OTA slot, and reboot into
 * it once it has been written and verified).
 */

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pages.h"
#include "translations.h"
#include "web_common.h"

static const char *TAG = "page_about";

esp_err_t page_about_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_ABOUT_TITLE, "about");

    const esp_app_desc_t *desc = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();

    char buf[1300];
    snprintf(buf, sizeof(buf),
             "<fieldset><legend>" TR_ABOUT_FW_LEGEND "</legend>"
             "<p><b>" TR_ABOUT_PROJECT "</b> %s</p>"
             "<p><b>" TR_ABOUT_VERSION "</b> %s</p>"
             "<p><b>" TR_ABOUT_BUILD_DATE "</b> %s %s</p>"
             "<p><b>" TR_ABOUT_IDF_VERSION "</b> %s</p>"
             "<p><b>" TR_ABOUT_PARTITION "</b> %s (offset 0x%lx, size %lu)</p>"
             "</fieldset>",
             desc->project_name, desc->version, desc->date, desc->time, desc->idf_ver, running->label, (unsigned long)running->address,
             (unsigned long)running->size);
    httpd_resp_sendstr_chunk(req, buf);

    // ---- OTA Update ----
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);

    httpd_resp_sendstr_chunk(req, "<fieldset><legend>" TR_ABOUT_OTA_LEGEND "</legend>"
                                  "<p>" TR_ABOUT_OTA_BODY "</p>");

    if (!target) {
        httpd_resp_sendstr_chunk(req, "<p class='msg-err'>" TR_OTA_NO_PARTITION "</p></fieldset>");
        web_send_footer(req);
        return ESP_OK;
    }

    char slot[220];
    snprintf(slot, sizeof(slot), "<p><b>" TR_OTA_TARGET_SLOT "</b> %s (%lu KB)</p>", target->label, (unsigned long)(target->size / 1024));
    httpd_resp_sendstr_chunk(req, slot);

    // Uploaded via JS/XHR (not a plain form submit) so we can show a live
    // progress bar while the .bin streams up - a full-size firmware image
    // can take a while over WiFi and a frozen-looking page invites a second
    // click/tab-close mid-flash, which is exactly what must not happen.
    httpd_resp_sendstr_chunk(
        req, "<label>" TR_OTA_SELECT_FILE "</label><input type='file' id='otaFile' accept='.bin'>"
             "<button type='button' id='otaBtn' onclick='otaUpload()'>" TR_OTA_UPLOAD_BTN "</button>"
             "<progress id='otaProgress' value='0' max='100' style='width:100%;display:none;margin-top:10px'></progress>"
             "<p id='otaStatus'></p>"
             "<script>function otaUpload(){"
             "var f=document.getElementById('otaFile').files[0];"
             "if(!f){alert('" TR_OTA_NO_FILE_SELECTED "');return;}"
             "if(!confirm('" TR_OTA_CONFIRM "'))return;"
             "var btn=document.getElementById('otaBtn');btn.disabled=true;"
             "var pr=document.getElementById('otaProgress');pr.style.display='block';pr.value=0;"
             "var st=document.getElementById('otaStatus');st.className='';st.innerHTML='" TR_OTA_UPLOADING "';"
             "var fd=new FormData();fd.append('firmware',f,f.name);"
             "var xhr=new XMLHttpRequest();"
             "xhr.upload.onprogress=function(e){if(e.lengthComputable){pr.value=Math.round(e.loaded*100/e.total);}};"
             "xhr.onload=function(){document.open();document.write(xhr.responseText);document.close();};"
             "xhr.onerror=function(){btn.disabled=false;st.className='msg-err';st.innerHTML='" TR_OTA_UPLOAD_FAILED "';};"
             "xhr.open('POST','/ota_update',true);xhr.send(fd);"
             "}</script>"
             "</fieldset>");

    httpd_resp_sendstr_chunk(req, "<fieldset><legend>" TR_ABOUT_SOURCE_LEGEND "</legend>"
                                  "<p>" TR_ABOUT_SOURCE_BODY " " TR_ABOUT_WEB_ADMIN "</p></fieldset>");
    web_send_footer(req);
    return ESP_OK;
}

// ---------------------------------------------------------------- OTA upload

typedef struct {
    esp_ota_handle_t handle;
    size_t total;
    esp_err_t werr;
} ota_upload_ctx_t;

static esp_err_t ota_write_cb(void *ctx_v, const uint8_t *data, size_t len) {
    ota_upload_ctx_t *ctx = (ota_upload_ctx_t *)ctx_v;
    esp_err_t err = esp_ota_write(ctx->handle, data, len);
    if (err != ESP_OK) {
        ctx->werr = err;
        return err;
    }
    ctx->total += len;
    return ESP_OK;
}

// Reboots shortly after the success response has been flushed to the
// browser, so the user actually gets to see the "rebooting..." message
// (and the XHR completes cleanly) instead of the connection dying mid-response.
static void ota_reboot_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

// Emits the OTA fieldset with an error message plus a link back to /about,
// as a full standalone page (used for the XHR response body, which replaces
// the whole document - see xhr.onload in page_about_get()).
static void ota_send_error_page(httpd_req_t *req, const char *detail) {
    web_send_header(req, TR_ABOUT_TITLE, "about");
    char buf[600];
    snprintf(buf, sizeof(buf),
             "<fieldset><legend>" TR_ABOUT_OTA_LEGEND "</legend>"
             "<p class='msg-err'>" TR_OTA_UPLOAD_FAILED "%s%s</p>"
             "<a class='btn' href='/about'>" TR_ABOUT_TITLE "</a></fieldset>",
             detail && detail[0] ? ": " : ".", detail ? detail : "");
    httpd_resp_sendstr_chunk(req, buf);
    web_send_footer(req);
}

esp_err_t page_ota_update_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) {
        ota_send_error_page(req, TR_OTA_NO_PARTITION);
        return ESP_OK;
    }

    ota_upload_ctx_t ctx = { .handle = 0, .total = 0, .werr = ESP_OK };
    esp_err_t err = esp_ota_begin(target, OTA_SIZE_UNKNOWN, &ctx.handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        char detail[128];
        snprintf(detail, sizeof(detail), "%s%s", TR_OTA_BEGIN_FAILED, esp_err_to_name(err));
        ota_send_error_page(req, detail);
        return ESP_OK;
    }

    char filename[96] = { 0 };
    esp_err_t perr = web_multipart_receive_file(req, ota_write_cb, &ctx, filename, sizeof(filename));

    if (perr != ESP_OK || ctx.werr != ESP_OK || ctx.total == 0) {
        esp_ota_abort(ctx.handle);
        const char *detail = (ctx.werr != ESP_OK) ? esp_err_to_name(ctx.werr) : (perr == ESP_ERR_NOT_FOUND) ? TR_OTA_NO_FILE_CHOSEN : NULL;
        ESP_LOGE(TAG, "OTA upload aborted: parse=%s write=%s bytes=%u", esp_err_to_name(perr), esp_err_to_name(ctx.werr), (unsigned)ctx.total);
        ota_send_error_page(req, detail);
        return ESP_OK;
    }

    err = esp_ota_end(ctx.handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        ota_send_error_page(req, err == ESP_ERR_OTA_VALIDATE_FAILED ? TR_OTA_VALIDATE_FAILED : esp_err_to_name(err));
        return ESP_OK;
    }

    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        ota_send_error_page(req, esp_err_to_name(err));
        return ESP_OK;
    }

    ESP_LOGI(TAG, "OTA update OK: %s, %u bytes -> %s, rebooting", filename[0] ? filename : "(unnamed)", (unsigned)ctx.total, target->label);

    web_send_header(req, TR_ABOUT_TITLE, "about");
    char okmsg[600];
    snprintf(okmsg, sizeof(okmsg),
             "<fieldset><legend>" TR_ABOUT_OTA_LEGEND "</legend>"
             "<p class='msg-ok'>" TR_OTA_SUCCESS "</p>"
             "<p>%s &mdash; %u bytes &rarr; %s</p>"
             "<p>" TR_OTA_REBOOTING "</p></fieldset>",
             filename[0] ? filename : "firmware.bin", (unsigned)ctx.total, target->label);
    httpd_resp_sendstr_chunk(req, okmsg);
    web_send_footer(req);

    xTaskCreate(ota_reboot_task, "ota_reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}
