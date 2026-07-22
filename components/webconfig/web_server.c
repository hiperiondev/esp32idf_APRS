/**
 * @file web_server.c
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
 * @brief Web admin HTTP server bring-up: starts esp_http_server and registers
 * every admin route (dashboard, configuration pages, JSON endpoints and static
 * assets) onto its handlers.
 */

#include "web_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "pages.h"
#include "web_common.h"

static const char *TAG = "web_server";

static esp_err_t css_handler(httpd_req_t *req) {
    return web_handle_css(req);
}

static void reg(httpd_handle_t s, const char *uri, httpd_method_t m, esp_err_t (*h)(httpd_req_t *)) {
    httpd_uri_t u = { .uri = uri, .method = m, .handler = h, .user_ctx = NULL };
    httpd_register_uri_handler(s, &u);
}

void web_server_start(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 64;
    // OTA firmware upload (esp_ota_write + esp_ota_end's image-verify sha256
    // pass) needs a bit more headroom than the rest of the admin pages.
    // Several POST handlers (e.g. /wireless, /igate, /system, /wx) keep a
    // 1.2-3KB form-parsing buffer alive on this task's stack for the whole
    // handler, including through app_config_save()'s fopen/fprintf/rename
    // chain into LittleFS. 10240 left too little margin for that plus a
    // FreeRTOS context-save landing mid-write, which showed up as an
    // intermittent "double exception" Guru Meditation on frequent saves
    // (stack pointer walking past the end of the task's stack region).
    // Bumped with headroom; verify actual usage in the field with
    // uxTaskGetStackHighWaterMark() on the httpd task if tuning further.
    config.stack_size = 20480;
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start httpd");
        return;
    }

    // -- Implemented --
    reg(server, "/", HTTP_GET, page_root);
    reg(server, "/logout", HTTP_GET, page_logout);
    reg(server, "/dashboard", HTTP_GET, page_dashboard);
    reg(server, "/station", HTTP_GET, page_station_get);
    reg(server, "/station", HTTP_POST, page_station_post);
    reg(server, "/bulletins", HTTP_GET, page_bulletins_get);
    reg(server, "/bulletins", HTTP_POST, page_bulletins_post);
    reg(server, "/objects", HTTP_GET, page_objects_get);
    reg(server, "/objects", HTTP_POST, page_objects_post);
    reg(server, "/sidebarInfo", HTTP_GET, page_sidebar_info);
    reg(server, "/igate_traffic", HTTP_GET, page_igate_traffic);
    reg(server, "/lastheard", HTTP_GET, page_lastheard);
    reg(server, "/dashinfo", HTTP_GET, page_dashinfo);
    reg(server, "/style.css", HTTP_GET, css_handler);

    reg(server, "/storage", HTTP_GET, page_storage_get);
    reg(server, "/download", HTTP_GET, page_download);
    reg(server, "/delete", HTTP_GET, page_delete);
    reg(server, "/format", HTTP_POST, page_format);
    reg(server, "/upload", HTTP_POST, page_upload);

    reg(server, "/wireless", HTTP_GET, page_wireless_get);
    reg(server, "/wireless", HTTP_POST, page_wireless_post);
    reg(server, "/wifiscan", HTTP_GET, page_wifi_scan_get);
    reg(server, "/system", HTTP_GET, page_system_get);
    reg(server, "/system", HTTP_POST, page_system_post);
    reg(server, "/default", HTTP_POST, page_default_reset);
    reg(server, "/about", HTTP_GET, page_about_get);
    reg(server, "/ota_update", HTTP_POST, page_ota_update_post);

    reg(server, "/igate", HTTP_GET, page_igate_get);
    reg(server, "/igate", HTTP_POST, page_igate_post);
    reg(server, "/digi", HTTP_GET, page_digi_get);
    reg(server, "/digi", HTTP_POST, page_digi_post);
    reg(server, "/tracker", HTTP_GET, page_tracker_get);
    reg(server, "/tracker", HTTP_POST, page_tracker_post);
    reg(server, "/wx", HTTP_GET, page_wx_get);
    reg(server, "/wx", HTTP_POST, page_wx_post);
    reg(server, "/wx/values", HTTP_GET, page_wx_values_get);
    reg(server, "/tlm", HTTP_GET, page_tlm_get);
    reg(server, "/tlm", HTTP_POST, page_tlm_post);

    reg(server, "/radio", HTTP_GET, page_radio_get);
    reg(server, "/radio", HTTP_POST, page_radio_post);
    reg(server, "/radio/looptest", HTTP_GET, page_radio_looptest_get);
    reg(server, "/msg", HTTP_GET, page_msg_get);
    reg(server, "/msg", HTTP_POST, page_msg_post);
    reg(server, "/msgchat", HTTP_GET, page_msgchat_get);
    reg(server, "/msgchat", HTTP_POST, page_msgchat_post);
    reg(server, "/msgchat/list", HTTP_GET, page_msgchat_list);

    reg(server, "/symbol", HTTP_GET, page_symbol_get);
    reg(server, "/test", HTTP_GET, page_test_get);

    // -- Everything in the original menu now has a real handler. No stubs left. --

    ESP_LOGI(TAG, "Web admin server started");
}
