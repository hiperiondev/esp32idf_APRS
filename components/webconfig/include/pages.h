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
 *
 * @details
 * Every handler has the ESP-IDF HTTP server signature
 * @c esp_err_t(httpd_req_t*): it receives the incoming request, writes the
 * response with the esp_http_server chunked-send API (usually through the
 * helpers in web_common.h) and returns ::ESP_OK on success or an esp_err_t
 * error to make the server abort the connection. The handlers come in a few
 * shapes:
 *   - @c page_*_get   render an HTML settings page (GET).
 *   - @c page_*_post  parse a submitted form, persist it and redirect (POST).
 *   - JSON endpoints (dashboard pollers, live previews, scans, loop test)
 *     that return @c application/json for the browser to consume with fetch().
 *
 * All of them require the caller to have already passed HTTP Basic auth
 * (web_check_auth()); web_server_start() wires that in when it registers the
 * routes. Each prototype documents the HTTP method and URI it is bound to.
 */

#ifndef PAGES_H
#define PAGES_H

#include "esp_http_server.h"

/**
 * @name Foundation pages (dashboard, station, storage, system, OTA)
 * @{
 */

/** @brief GET  / - login landing / root redirect. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_root(httpd_req_t *req);
/** @brief GET  /logout - clear HTTP auth and show the "logged out" page. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_logout(httpd_req_t *req);
/** @brief GET  /dashboard - main status dashboard (live panels are filled by the JSON pollers below). @param req Incoming request. @return ESP_OK or an
 * esp_err_t error. */
esp_err_t page_dashboard(httpd_req_t *req);
/** @brief GET  /station - "My Station" form (callsign + lat/lon/alt shared by every page's "Use My Station Data"). @param req Incoming request. @return ESP_OK
 * or an esp_err_t error. */
esp_err_t page_station_get(httpd_req_t *req);
/** @brief POST /station - persist the "My Station" identity/position. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_station_post(httpd_req_t *req);
/** @brief GET  /bulletins - APRS bulletin editor (BLN1..BLN5, stored in LittleFS, not g_config). @param req Incoming request. @return ESP_OK or an esp_err_t
 * error. */
esp_err_t page_bulletins_get(httpd_req_t *req);
/** @brief POST /bulletins - persist the bulletin set and (re)arm expiry. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_bulletins_post(httpd_req_t *req);
/** @brief GET  /objects - APRS Objects/Items editor (stored in LittleFS, not g_config). @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_objects_get(httpd_req_t *req);
/** @brief POST /objects - persist the Objects/Items set. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_objects_post(httpd_req_t *req);
/** @brief GET  /sidebarInfo - JSON summary strip shown in the sidebar. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_sidebar_info(httpd_req_t *req);
/** @brief GET  /igate_traffic?since=<seq> - JSON incremental traffic-log feed for the dashboard. @param req Incoming request. @return ESP_OK or an esp_err_t
 * error. */
esp_err_t page_igate_traffic(httpd_req_t *req);
/** @brief GET  /lastheard - JSON feed backing the dashboard LAST HEARD table. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_lastheard(httpd_req_t *req);
/** @brief GET  /dashinfo - compact live sysinfo strip polled by the dashboard. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_dashinfo(httpd_req_t *req);
/** @brief GET  /heapinfo - free/min-free heap JSON, polled every 1 s by the dashboard. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_heapinfo(httpd_req_t *req);
/** @brief GET  /storage - LittleFS storage management page (list/download/delete/format). @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_storage_get(httpd_req_t *req);
/** @brief GET  /download?file=... - stream one stored file back to the browser. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_download(httpd_req_t *req);
/** @brief GET/POST /delete?file=... - delete one stored file. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_delete(httpd_req_t *req);
/** @brief GET/POST /format - erase and reformat the LittleFS partition ("factory format"). @param req Incoming request. @return ESP_OK or an esp_err_t error.
 */
esp_err_t page_format(httpd_req_t *req);
/** @brief POST /upload - multipart file upload into LittleFS. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_upload(httpd_req_t *req);
/** @brief GET  /wireless - WiFi STA/AP configuration form. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_wireless_get(httpd_req_t *req);
/** @brief POST /wireless - persist WiFi configuration. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_wireless_post(httpd_req_t *req);
/** @brief POST /wifiscan - JSON list of nearby access points (SSID/RSSI/auth). POST, not GET: the scan flips an AP-only radio to AP+STA, so it is a
 * state-changing request and has to go through the same-origin check in web_check_auth(). @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_wifi_scan_post(httpd_req_t *req);
/** @brief GET  /system - System settings (time sync, CPU frequency, HTTP auth, hostname, logging). @param req Incoming request. @return ESP_OK or an esp_err_t
 * error. */
esp_err_t page_system_get(httpd_req_t *req);
/** @brief POST /system - persist System settings and apply the ones that take effect live. @param req Incoming request. @return ESP_OK or an esp_err_t error.
 */
esp_err_t page_system_post(httpd_req_t *req);
/** @brief GET  /about - firmware/build info and the OTA update form. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_about_get(httpd_req_t *req);
/** @brief POST /ota_update - multipart firmware upload written to the inactive OTA slot, then reboot. @param req Incoming request. @return ESP_OK or an
 * esp_err_t error. */
esp_err_t page_ota_update_post(httpd_req_t *req);
/** @brief GET/POST /default - factory reset (wipe config back to defaults and reboot). @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_default_reset(httpd_req_t *req);

/** @} */

/**
 * @name APRS service pages (IGate / Digi / Tracker / Weather / Telemetry)
 * @{
 */

/** @brief GET  /igate - IGate (APRS-IS gateway) settings form. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_igate_get(httpd_req_t *req);
/** @brief POST /igate - persist IGate settings. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_igate_post(httpd_req_t *req);
/** @brief GET  /digi - Digipeater settings form. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_digi_get(httpd_req_t *req);
/** @brief POST /digi - persist Digipeater settings. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_digi_post(httpd_req_t *req);
/** @brief GET  /tracker - Tracker (fixed-position beacon) settings form. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_tracker_get(httpd_req_t *req);
/** @brief POST /tracker - persist Tracker settings. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_tracker_post(httpd_req_t *req);
/** @brief GET  /wx - Weather station settings + sensor mapping form. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_wx_get(httpd_req_t *req);
/** @brief POST /wx - persist Weather settings and sensor mapping. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_wx_post(httpd_req_t *req);
/** @brief GET  /wx/values - JSON live sensor-mapping preview, polled every 2 s. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_wx_values_get(httpd_req_t *req);
/** @brief GET  /tlm - Telemetry configuration form. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_tlm_get(httpd_req_t *req);
/** @brief POST /tlm - persist Telemetry configuration (to /storage/telemetry.json). @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_tlm_post(httpd_req_t *req);
/** @brief GET  /tlm/values - JSON live raw-analog preview, polled every 2 s. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_tlm_values_get(httpd_req_t *req);

/** @} */

/**
 * @name RF / networking pages (Radiomodem, Message)
 * @{
 */

/** @brief GET  /radio - Radiomodem (audio AFSK modem) settings form. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_radio_get(httpd_req_t *req);
/** @brief POST /radio - persist Radiomodem settings and re-apply them to the running modem. @param req Incoming request. @return ESP_OK or an esp_err_t error.
 */
esp_err_t page_radio_post(httpd_req_t *req);
/** @brief POST /radio/looptest - JSON result of the ADC->DAC modem loopback self-test. POST, not GET: the test keys the transmitter, so it is a
 * state-changing request and has to go through the same-origin check in web_check_auth(). @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_radio_looptest_post(httpd_req_t *req);
/** @brief GET  /msg - APRS Message service settings form. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_msg_get(httpd_req_t *req);
/** @brief POST /msg - persist APRS Message service settings. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_msg_post(httpd_req_t *req);

/** @} */

/**
 * @name Snd/Rcv Msg chat interface (gated by ENABLE_MSG_CHAT)
 * @{
 */

/** @brief GET  /msgchat - chat-style message inbox/compose page. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_msgchat_get(httpd_req_t *req);
/** @brief POST /msgchat - send a message; replies JSON {"ok":true|false,"error":...}. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_msgchat_post(httpd_req_t *req);
/** @brief GET  /msgchat/list - JSON message history, polled by the chat page. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_msgchat_list(httpd_req_t *req);

/** @} */

/**
 * @name Miscellaneous pages
 * @{
 */

/** @brief GET  /symbol - APRS symbol reference/picker page. @param req Incoming request. @return ESP_OK or an esp_err_t error. */
esp_err_t page_symbol_get(httpd_req_t *req);

/** @} */

#endif // PAGES_H
