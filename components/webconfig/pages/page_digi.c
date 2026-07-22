/**
 * @file page_digi.c
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
 * @brief Web admin "Digipeater" page: renders and saves the digipeater
 * configuration (callsign/SSID, path handling, filters and beacon settings) in
 * g_config.
 */

#include <string.h>

#include "app_config.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

esp_err_t page_digi_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_DIGIPEATER, "digi");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/digi'>");

    web_fieldset_open(req, TR_F_DIGIPEATER);
    web_field_checkbox(req, TR_F_ENABLE_DIGIPEATER, "digiEn", g_config.digi_en);
    web_field_use_station_data(req, "digiUseStation", g_config.digi_use_station, "digiMycall", "digiLAT", "digiLON", "digiAlt");
    web_field_checkbox(req, TR_F_AUTO_WIDEN_N, "digiAuto", g_config.digi_auto);
    web_field_checkbox(req, TR_F_ADD_TIMESTAMP, "digiTime", g_config.digi_timestamp);
    web_field_int(req, TR_F_DIGI_DELAY_MS, "digiDelay", g_config.digi_delay);
    web_field_int(req, TR_F_DUPE_FILTER_WINDOW_S, "digiFilter", g_config.digiFilter);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_STATION);
    web_field_text(req, TR_F_MY_CALLSIGN, "digiMycall", g_config.digi_mycall, 9);
    web_field_int(req, TR_F_SSID, "digiSSID", g_config.digi_ssid);
    web_field_int(req, TR_F_PATH_BITMASK, "digiPath", g_config.digi_path);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_BEACON_POSITION);
    web_field_checkbox(req, TR_F_BEACON_POSITION_2, "digiBcn", g_config.digi_bcn);
    web_field_checkbox(req, TR_F_BEACON_VIA_RF, "digiPos2rf", g_config.digi_loc2rf);
    web_field_checkbox(req, TR_F_BEACON_VIA_INTERNET, "digiPos2inet", g_config.digi_loc2inet);
    web_field_checkbox(req, TR_F_USE_GPS_POSITION, "digiGPS", g_config.digi_gps);
    web_field_float(req, TR_F_LATITUDE, "digiLAT", g_config.digi_lat, "0.0001");
    web_field_float(req, TR_F_LONGITUDE, "digiLON", g_config.digi_lon, "0.0001");
    web_field_float(req, TR_F_ALTITUDE_M, "digiAlt", g_config.digi_alt, "1");
    web_field_int(req, TR_F_BEACON_INTERVAL_S, "digiINV", g_config.digi_interval);
    web_field_symbol(req, TR_F_STATION_SYMBOL, "digiSym", g_config.digi_symbol);
    web_field_text(req, TR_F_PHG, "digiPHG", g_config.digi_phg, 7);
    web_field_text(req, TR_F_COMMENT, "digiComment", g_config.digi_comment, COMMENT_SIZE - 1);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_STATUS_BEACON);
    web_field_int(req, TR_F_STATUS_INTERVAL_S_0_OFF, "digiSTSIntv", g_config.digi_sts_interval);
    web_field_text(req, TR_F_STATUS_TEXT, "digiStatus", g_config.digi_status, STATUS_SIZE - 1);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_SYS_DIGI_PATH_ALIASES);
    web_field_text(req, TR_SYS_PATH_1, "path0", g_config.path[0], 71);
    web_field_text(req, TR_SYS_PATH_2, "path1", g_config.path[1], 71);
    web_field_text(req, TR_SYS_PATH_3, "path2", g_config.path[2], 71);
    web_field_text(req, TR_SYS_PATH_4, "path3", g_config.path[3], 71);
    web_fieldset_close(req);

    web_raw(req, "<p style='color:var(--sub);font-size:12px'>" TR_NOTE_TLM_DIGI "</p>"
                 "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_digi_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char body[1750];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    app_config_lock();
    g_config.digi_en = web_form_get_bool(body, "digiEn");
    g_config.digi_use_station = web_form_get_bool(body, "digiUseStation");
    g_config.digi_auto = web_form_get_bool(body, "digiAuto");
    g_config.digi_timestamp = web_form_get_bool(body, "digiTime");
    g_config.digi_delay = (uint16_t)web_form_get_int(body, "digiDelay", g_config.digi_delay);
    g_config.digiFilter = (uint16_t)web_form_get_int(body, "digiFilter", g_config.digiFilter);

    if (g_config.digi_use_station) {
        // Fields are disabled client-side while this is checked, so the
        // form never submits them - pull from the shared Station data instead.
        strncpy(g_config.digi_mycall, g_config.my_callsign, sizeof(g_config.digi_mycall) - 1);
        g_config.digi_mycall[sizeof(g_config.digi_mycall) - 1] = 0;
        g_config.digi_lat = g_config.my_lat;
        g_config.digi_lon = g_config.my_lon;
        g_config.digi_alt = g_config.my_alt;
    } else {
        web_form_get_call(body, "digiMycall", g_config.digi_mycall, sizeof(g_config.digi_mycall));
        g_config.digi_lat = web_form_get_float(body, "digiLAT", g_config.digi_lat);
        g_config.digi_lon = web_form_get_float(body, "digiLON", g_config.digi_lon);
        g_config.digi_alt = web_form_get_float(body, "digiAlt", g_config.digi_alt);
    }
    g_config.digi_ssid = web_form_get_ssid(body, "digiSSID", g_config.digi_ssid);
    g_config.digi_path = (uint8_t)web_form_get_int(body, "digiPath", g_config.digi_path);

    g_config.digi_bcn = web_form_get_bool(body, "digiBcn");
    g_config.digi_loc2rf = web_form_get_bool(body, "digiPos2rf");
    g_config.digi_loc2inet = web_form_get_bool(body, "digiPos2inet");
    g_config.digi_gps = web_form_get_bool(body, "digiGPS");
    g_config.digi_interval = (uint16_t)web_form_get_int(body, "digiINV", g_config.digi_interval);

    // Station Symbol: Table + Symbol 1-char fields from the shared picker
    // widget, falling back to a legacy combined 2-char field if present.
    web_form_get_symbol(body, "digiSym", "digiSymbol", g_config.digi_symbol, sizeof(g_config.digi_symbol));

    web_form_get(body, "digiPHG", g_config.digi_phg, sizeof(g_config.digi_phg));
    web_form_get(body, "digiComment", g_config.digi_comment, sizeof(g_config.digi_comment));

    g_config.digi_sts_interval = (uint16_t)web_form_get_int(body, "digiSTSIntv", g_config.digi_sts_interval);
    web_form_get(body, "digiStatus", g_config.digi_status, sizeof(g_config.digi_status));

    web_form_get(body, "path0", g_config.path[0], sizeof(g_config.path[0]));
    web_form_get(body, "path1", g_config.path[1], sizeof(g_config.path[1]));
    web_form_get(body, "path2", g_config.path[2], sizeof(g_config.path[2]));
    web_form_get(body, "path3", g_config.path[3], sizeof(g_config.path[3]));

    app_config_unlock();

    app_config_save();
    web_send_saved_redirect(req, "/digi");
    return ESP_OK;
}
