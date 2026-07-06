#include <stdio.h>

#include "esp_app_desc.h"
#include "esp_ota_ops.h"

#include "pages.h"
#include "translations.h"
#include "web_common.h"

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
             "</fieldset>"
             "<fieldset><legend>" TR_ABOUT_OTA_LEGEND "</legend>"
             "<p>" TR_ABOUT_OTA_BODY "</p>"
             "</fieldset>"
             "<fieldset><legend>" TR_ABOUT_SOURCE_LEGEND "</legend>"
             "<p>" TR_ABOUT_SOURCE_BODY " " TR_ABOUT_WEB_ADMIN "</p></fieldset>",
             desc->project_name, desc->version, desc->date, desc->time, desc->idf_ver, running->label, (unsigned long)running->address,
             (unsigned long)running->size);
    httpd_resp_sendstr_chunk(req, buf);
    web_send_footer(req);
    return ESP_OK;
}
