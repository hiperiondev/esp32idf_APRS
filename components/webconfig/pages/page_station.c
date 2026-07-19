/**
 * @file page_station.c
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
 * @brief Web admin "Station" page: renders and saves the single shared "My
 * Station" identity (callsign, latitude, longitude, altitude) in g_config.
 * This is the data every other page's "Use My Station Data" checkbox pulls
 * from instead of having the same callsign/position retyped on every
 * IGate/Digipeater/Tracker/Weather page.
 */

#include "app_config.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

esp_err_t page_station_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_STATION, "station");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/station'>");

    web_fieldset_open(req, TR_F_STATION);
    web_field_text(req, TR_F_MY_CALLSIGN, "myCallsign", g_config.my_callsign, 9);
    web_field_float(req, TR_F_LATITUDE, "myLAT", g_config.my_lat, "0.0001");
    web_field_float(req, TR_F_LONGITUDE, "myLON", g_config.my_lon, "0.0001");
    web_field_float(req, TR_F_ALTITUDE_M, "myALT", g_config.my_alt, "1");
    web_fieldset_close(req);

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_station_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char body[300];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    web_form_get_call(body, "myCallsign", g_config.my_callsign, sizeof(g_config.my_callsign));
    g_config.my_lat = web_form_get_float(body, "myLAT", g_config.my_lat);
    g_config.my_lon = web_form_get_float(body, "myLON", g_config.my_lon);
    g_config.my_alt = web_form_get_float(body, "myALT", g_config.my_alt);

    app_config_save();
    web_send_saved_redirect(req, "/station");
    return ESP_OK;
}
