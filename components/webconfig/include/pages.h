/**
 * @file pages.h
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
 * @brief Declarations of every web admin page and JSON endpoint handler
 * registered by web_server_start().
 */

#ifndef PAGES_H
#define PAGES_H

#include "esp_http_server.h"

// -- Implemented this turn (foundation pages) --
esp_err_t page_root(httpd_req_t *req);          // GET  /
esp_err_t page_logout(httpd_req_t *req);        // GET  /logout
esp_err_t page_dashboard(httpd_req_t *req);     // GET  /dashboard
esp_err_t page_station_get(httpd_req_t *req);   // GET  /station (My Station: callsign + lat/lon/alt shared by "Use My Station Data")
esp_err_t page_station_post(httpd_req_t *req);  // POST /station
esp_err_t page_bulletins_get(httpd_req_t *req);  // GET  /bulletins (APRS BLN1..BLN5, stored in LittleFS, not g_config)
esp_err_t page_bulletins_post(httpd_req_t *req); // POST /bulletins
esp_err_t page_objects_get(httpd_req_t *req);    // GET  /objects (APRS Objects/Items, stored in LittleFS, not g_config)
esp_err_t page_objects_post(httpd_req_t *req);   // POST /objects
esp_err_t page_sidebar_info(httpd_req_t *req);  // GET  /sidebarInfo
esp_err_t page_igate_traffic(httpd_req_t *req); // GET  /igate_traffic?since=<seq> (JSON)
esp_err_t page_lastheard(httpd_req_t *req);     // GET  /lastheard (JSON, dashboard LAST HEARD table)
esp_err_t page_dashinfo(httpd_req_t *req);      // GET  /dashinfo (compact live sysinfo strip for the dashboard)
esp_err_t page_storage_get(httpd_req_t *req);   // GET  /storage
esp_err_t page_download(httpd_req_t *req);      // GET  /download?file=...
esp_err_t page_delete(httpd_req_t *req);        // GET/POST /delete?file=...
esp_err_t page_format(httpd_req_t *req);        // GET/POST /format
esp_err_t page_upload(httpd_req_t *req);        // POST /upload (multipart)
esp_err_t page_wireless_get(httpd_req_t *req);  // GET  /wireless
esp_err_t page_wireless_post(httpd_req_t *req); // POST /wireless
esp_err_t page_wifi_scan_get(httpd_req_t *req); // GET  /wifiscan (JSON AP list)
esp_err_t page_system_get(httpd_req_t *req);    // GET  /system
esp_err_t page_system_post(httpd_req_t *req);   // POST /system
esp_err_t page_about_get(httpd_req_t *req);     // GET  /about
esp_err_t page_ota_update_post(httpd_req_t *req); // POST /ota_update (multipart firmware upload -> flash + reboot)
esp_err_t page_default_reset(httpd_req_t *req); // GET/POST /default (factory reset)

// -- APRS services --
esp_err_t page_igate_get(httpd_req_t *req);
esp_err_t page_igate_post(httpd_req_t *req);
esp_err_t page_digi_get(httpd_req_t *req);
esp_err_t page_digi_post(httpd_req_t *req);
esp_err_t page_tracker_get(httpd_req_t *req);
esp_err_t page_tracker_post(httpd_req_t *req);
esp_err_t page_wx_get(httpd_req_t *req);
esp_err_t page_wx_post(httpd_req_t *req);
esp_err_t page_wx_values_get(httpd_req_t *req); // GET /wx/values (JSON live sensor-mapping preview, polled every 2s)
esp_err_t page_tlm_get(httpd_req_t *req);
esp_err_t page_tlm_post(httpd_req_t *req);

// -- RF / networking --
esp_err_t page_radio_get(httpd_req_t *req);
esp_err_t page_radio_post(httpd_req_t *req);
esp_err_t page_radio_looptest_get(httpd_req_t *req); // GET /radio/looptest (JSON loop-test result)
esp_err_t page_msg_get(httpd_req_t *req);
esp_err_t page_msg_post(httpd_req_t *req);

// -- Snd/Rcv Msg (chat-style message inbox/compose, gated by ENABLE_MSG_CHAT) --
esp_err_t page_msgchat_get(httpd_req_t *req);  // GET  /msgchat
esp_err_t page_msgchat_post(httpd_req_t *req); // POST /msgchat (send; JSON {"ok":true|false,"error":...})
esp_err_t page_msgchat_list(httpd_req_t *req); // GET  /msgchat/list (JSON message history, polled)

// -- Misc --
esp_err_t page_symbol_get(httpd_req_t *req);
esp_err_t page_test_get(httpd_req_t *req);

#endif
