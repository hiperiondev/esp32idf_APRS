// @file web_server.c
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
// @brief Web admin HTTP server bring-up: starts esp_http_server and registers
// every admin route (dashboard, configuration pages, JSON endpoints and static
// assets) onto its handlers.

#include "web_server.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "pages.h"
#include "web_common.h"

static const char *TAG = "web_server";

static esp_err_t css_handler(httpd_req_t *req) {
    return web_handle_css(req);
}

// Registers one route and reports any failure to the log, naming the URI
// that did not register. httpd_register_uri_handler() fails closed - the
// route simply never answers, with no other indication - once
// config.max_uri_handlers routes are already registered, so this is what
// makes an exhausted handler table (or any other registration failure)
// visible at boot instead of showing up later as an unexplained 404.
static void reg(httpd_handle_t s, const char *uri, httpd_method_t m, esp_err_t (*h)(httpd_req_t *)) {
    httpd_uri_t u = { .uri = uri, .method = m, .handler = h, .user_ctx = NULL };
    esp_err_t err = httpd_register_uri_handler(s, &u);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Failed to register URI '%s': %s", uri, esp_err_to_name(err));
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
    // chain into LittleFS. On top of that peak the stack must still absorb a
    // FreeRTOS context save landing mid-write, so the budget is set with
    // deliberate headroom rather than trimmed to the measured maximum. Use
    // uxTaskGetStackHighWaterMark() on the httpd task before changing it.
    config.stack_size = 20480;
    // The whole firmware shares one pool of CONFIG_LWIP_MAX_SOCKETS (10)
    // sockets. httpd claims max_open_sockets plus 3 of its own (the TCP
    // listener and the two UDP control sockets), so 4 concurrent browser
    // connections leave 3 sockets for the rest of the station: the APRS-IS
    // uplink, DNS lookups and the SNTP client. Four is also what the admin
    // UI needs - a page plus /style.css and the periodic /dashinfo,
    // /sidebarInfo and /heapinfo fetches, all on keep-alive - and every TCP
    // connection that is not open is a connection whose send and receive
    // windows (CONFIG_LWIP_TCP_SND_BUF_DEFAULT and
    // CONFIG_LWIP_TCP_WND_DEFAULT) never come out of the heap.
    config.max_open_sockets = 4;
    // A fifth browser connection evicts the least recently used one instead
    // of being refused, so the cap costs latency under load, never an error
    // page.
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
    reg(server, "/heapinfo", HTTP_GET, page_heapinfo);
    reg(server, "/style.css", HTTP_GET, css_handler);

    reg(server, "/storage", HTTP_GET, page_storage_get);
    reg(server, "/download", HTTP_GET, page_download);
    reg(server, "/delete", HTTP_POST, page_delete);
    reg(server, "/format", HTTP_POST, page_format);
    reg(server, "/upload", HTTP_POST, page_upload);

    reg(server, "/wireless", HTTP_GET, page_wireless_get);
    reg(server, "/wireless", HTTP_POST, page_wireless_post);
    reg(server, "/wifiscan", HTTP_POST, page_wifi_scan_post);
    reg(server, "/system", HTTP_GET, page_system_get);
    reg(server, "/system", HTTP_POST, page_system_post);
    reg(server, "/default", HTTP_POST, page_default_reset);
    reg(server, "/about", HTTP_GET, page_about_get);
    reg(server, "/ota_update", HTTP_POST, page_ota_update_post);

    reg(server, "/igate", HTTP_GET, page_igate_get);
    reg(server, "/igate", HTTP_POST, page_igate_post);
    reg(server, "/bm", HTTP_GET, page_bm_get);
    reg(server, "/bm", HTTP_POST, page_bm_post);
    reg(server, "/digi", HTTP_GET, page_digi_get);
    reg(server, "/digi", HTTP_POST, page_digi_post);
    reg(server, "/tracker", HTTP_GET, page_tracker_get);
    reg(server, "/tracker", HTTP_POST, page_tracker_post);
    reg(server, "/wx", HTTP_GET, page_wx_get);
    reg(server, "/wx", HTTP_POST, page_wx_post);
    reg(server, "/wx/values", HTTP_GET, page_wx_values_get);
    reg(server, "/tlm", HTTP_GET, page_tlm_get);
    reg(server, "/tlm", HTTP_POST, page_tlm_post);
    reg(server, "/tlm/values", HTTP_GET, page_tlm_values_get);
    reg(server, "/gps", HTTP_GET, page_gps_get);
    reg(server, "/gps", HTTP_POST, page_gps_post);
    reg(server, "/gps/values", HTTP_GET, page_gps_values_get);
    reg(server, "/gps/live", HTTP_GET, page_gps_live_get);

    reg(server, "/radio", HTTP_GET, page_radio_get);
    reg(server, "/radio", HTTP_POST, page_radio_post);
    reg(server, "/radio/looptest", HTTP_POST, page_radio_looptest_post);
    reg(server, "/msg", HTTP_GET, page_msg_get);
    reg(server, "/msg", HTTP_POST, page_msg_post);
    reg(server, "/query", HTTP_GET, page_query_get);
    reg(server, "/query", HTTP_POST, page_query_post);
    reg(server, "/msgchat", HTTP_GET, page_msgchat_get);
    reg(server, "/msgchat", HTTP_POST, page_msgchat_post);
    reg(server, "/msgchat/list", HTTP_GET, page_msgchat_list);

    reg(server, "/symbol", HTTP_GET, page_symbol_get);

    // -- Everything in the original menu now has a real handler. No stubs left. --

    ESP_LOGI(TAG, "Web admin server started");
}
