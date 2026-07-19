/**
 * @file page_tracker.c
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
 * @brief Web admin "Tracker" page: renders and saves the tracker configuration
 * (callsign/SSID, fixed position, symbol, comment, path and beacon settings) in
 * g_config.
 */

#include <string.h>

#include "app_config.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

esp_err_t page_tracker_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_TRACKER, "tracker");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/tracker'>");

    web_fieldset_open(req, TR_F_TRACKER);
    web_field_checkbox(req, TR_F_ENABLE_TRACKER, "trkEn", g_config.trk_en);
    web_field_use_station_data(req, "trkUseStation", g_config.trk_use_station, "trkMycall", "trkLAT", "trkLON", "trkALT");
    web_field_checkbox(req, TR_F_BEACON_VIA_RF, "trkPos2rf", g_config.trk_loc2rf);
    web_field_checkbox(req, TR_F_BEACON_VIA_INTERNET, "trkPos2inet", g_config.trk_loc2inet);
    web_field_checkbox(req, TR_F_ADD_TIMESTAMP, "trkTime", g_config.trk_timestamp);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_STATION);
    web_field_text(req, TR_F_MY_CALLSIGN, "trkMycall", g_config.trk_mycall, 9);
    web_field_int(req, TR_F_SSID, "trkSSID", g_config.trk_ssid);
    web_field_int(req, TR_F_PATH_BITMASK, "trkPath", g_config.trk_path);
    web_field_checkbox(req, TR_F_USE_GPS, "trkGPS", g_config.trk_gps);
    web_field_float(req, TR_F_FIXED_LATITUDE, "trkLAT", g_config.trk_lat, "0.0001");
    web_field_float(req, TR_F_FIXED_LONGITUDE, "trkLON", g_config.trk_lon, "0.0001");
    web_field_float(req, TR_F_FIXED_ALTITUDE_M, "trkALT", g_config.trk_alt, "1");
    web_fieldset_close(req);

#ifdef ENABLE_SMARTBEACONING
    web_fieldset_open(req, TR_F_SMARTBEACONING);
    web_field_checkbox(req, TR_F_ENABLE_SMARTBEACONING, "trkSmart", g_config.trk_smartbeacon);
    web_field_int(req, TR_F_FIXED_INTERVAL_S, "trkINV", g_config.trk_interval);
    web_field_int(req, TR_F_LOW_SPEED_KM_H, "trkLSpeed", g_config.trk_lspeed);
    web_field_int(req, TR_F_HIGH_SPEED_KM_H, "trkHSpeed", g_config.trk_hspeed);
    web_field_int(req, TR_F_MAX_INTERVAL_MIN, "trkMaxInv", g_config.trk_maxinterval);
    web_field_int(req, TR_F_MIN_INTERVAL_S, "trkMinInv", g_config.trk_mininterval);
    web_field_int(req, TR_F_MIN_TURN_ANGLE_DEG, "trkMinDir", g_config.trk_minangle);
    web_field_int(req, TR_F_SLOW_RATE_INTERVAL_S, "trkSlowInv", g_config.trk_slowinterval);
    web_fieldset_close(req);
#endif // ENABLE_SMARTBEACONING

    web_fieldset_open(req, TR_F_OPTIONS);
    web_field_checkbox(req, TR_F_COMPRESS_POSITION, "trkCompress", g_config.trk_compress);
    web_field_checkbox(req, TR_F_INCLUDE_ALTITUDE, "trkOptAlt", g_config.trk_altitude);
    web_field_checkbox(req, TR_F_INCLUDE_RSSI, "trkOptRSSI", g_config.trk_rssi);
    web_field_checkbox(req, TR_F_LOG_TRACK, "trkLog", g_config.trk_log);
    web_field_symbol(req, TR_F_SYMBOL_IDLE, "trkSymbol", g_config.trk_symbol);
    web_field_symbol(req, TR_F_SYMBOL_MOVING, "trkSymbolMove", g_config.trk_symmove);
    web_field_symbol(req, TR_F_SYMBOL_STOPPED, "trkSymbolStop", g_config.trk_symstop);
    web_field_text(req, TR_F_OBJECT_ITEM_NAME, "trkItem", g_config.trk_item, 9);
    web_field_text(req, TR_F_COMMENT, "trkComment", g_config.trk_comment, COMMENT_SIZE - 1);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_STATUS_BEACON);
    web_field_int(req, TR_F_STATUS_INTERVAL_S_0_OFF, "trkSTSIntv", g_config.trk_sts_interval);
    web_field_text(req, TR_F_STATUS_TEXT, "trkStatus", g_config.trk_status, STATUS_SIZE - 1);
    web_fieldset_close(req);

    web_raw(req, "<p style='color:var(--sub);font-size:12px'>" TR_NOTE_TLM_TRACKER "</p>"
                 "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_tracker_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char body[1800];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    g_config.trk_en = web_form_get_bool(body, "trkEn");
    g_config.trk_use_station = web_form_get_bool(body, "trkUseStation");
    g_config.trk_loc2rf = web_form_get_bool(body, "trkPos2rf");
    g_config.trk_loc2inet = web_form_get_bool(body, "trkPos2inet");
    g_config.trk_timestamp = web_form_get_bool(body, "trkTime");

    if (g_config.trk_use_station) {
        strncpy(g_config.trk_mycall, g_config.my_callsign, sizeof(g_config.trk_mycall) - 1);
        g_config.trk_mycall[sizeof(g_config.trk_mycall) - 1] = 0;
        g_config.trk_lat = g_config.my_lat;
        g_config.trk_lon = g_config.my_lon;
        g_config.trk_alt = g_config.my_alt;
    } else {
        web_form_get_call(body, "trkMycall", g_config.trk_mycall, sizeof(g_config.trk_mycall));
        g_config.trk_lat = web_form_get_float(body, "trkLAT", g_config.trk_lat);
        g_config.trk_lon = web_form_get_float(body, "trkLON", g_config.trk_lon);
        g_config.trk_alt = web_form_get_float(body, "trkALT", g_config.trk_alt);
    }
    g_config.trk_ssid = web_form_get_ssid(body, "trkSSID", g_config.trk_ssid);
    g_config.trk_path = (uint8_t)web_form_get_int(body, "trkPath", g_config.trk_path);
    g_config.trk_gps = web_form_get_bool(body, "trkGPS");

#ifdef ENABLE_SMARTBEACONING
    g_config.trk_smartbeacon = web_form_get_bool(body, "trkSmart");
    g_config.trk_interval = (uint16_t)web_form_get_int(body, "trkINV", g_config.trk_interval);
    g_config.trk_lspeed = (uint8_t)web_form_get_int(body, "trkLSpeed", g_config.trk_lspeed);
    g_config.trk_hspeed = (uint16_t)web_form_get_int(body, "trkHSpeed", g_config.trk_hspeed);
    g_config.trk_maxinterval = (uint8_t)web_form_get_int(body, "trkMaxInv", g_config.trk_maxinterval);
    g_config.trk_mininterval = (uint8_t)web_form_get_int(body, "trkMinInv", g_config.trk_mininterval);
    g_config.trk_minangle = (uint8_t)web_form_get_int(body, "trkMinDir", g_config.trk_minangle);
    g_config.trk_slowinterval = (uint16_t)web_form_get_int(body, "trkSlowInv", g_config.trk_slowinterval);
#endif // ENABLE_SMARTBEACONING

    g_config.trk_compress = web_form_get_bool(body, "trkCompress");
    g_config.trk_altitude = web_form_get_bool(body, "trkOptAlt");
    g_config.trk_rssi = web_form_get_bool(body, "trkOptRSSI");
    g_config.trk_log = web_form_get_bool(body, "trkLog");

    // Station Symbols (idle/moving/stopped): Table + Symbol 1-char fields
    // from the shared picker widget, each falling back to its legacy
    // combined 2-char field if present.
    web_form_get_symbol(body, "trkSymbol", "trkSymbol", g_config.trk_symbol, sizeof(g_config.trk_symbol));
    web_form_get_symbol(body, "trkSymbolMove", "trkSymbolMove", g_config.trk_symmove, sizeof(g_config.trk_symmove));
    web_form_get_symbol(body, "trkSymbolStop", "trkSymbolStop", g_config.trk_symstop, sizeof(g_config.trk_symstop));

    web_form_get(body, "trkItem", g_config.trk_item, sizeof(g_config.trk_item));
    web_form_get(body, "trkComment", g_config.trk_comment, sizeof(g_config.trk_comment));

    g_config.trk_sts_interval = (uint16_t)web_form_get_int(body, "trkSTSIntv", g_config.trk_sts_interval);
    web_form_get(body, "trkStatus", g_config.trk_status, sizeof(g_config.trk_status));

    app_config_save();
    web_send_saved_redirect(req, "/tracker");
    return ESP_OK;
}
