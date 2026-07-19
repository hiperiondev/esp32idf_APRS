/**
 * @file page_wx.c
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
 * @brief Web admin "Weather" page: renders and saves the weather station slot
 * configuration (wind, temperature, rain, humidity, pressure and the remaining
 * measurement slots) in g_config.
 */

#include <stdio.h>

#include "app_config.h"
#include "pages.h"
#include "sensors_local.h"
#include "translations.h"
#include "web_common.h"

/*
 * Human-readable label for every mappable APRS Weather Report field, indexed
 * by ::wx_field_id_t (declared in app_config.h). This list is exactly the set
 * of quantities an APRS WX report can carry on-air - one row per field, in the
 * order weather.c emits them. The old placeholder/extended-sensor rows (UV,
 * soil, water, battery, "Slot 18..26") are gone: they have no weather-report
 * token and belong on the Telemetry page instead.
 */
static const char *WX_FIELD_NAME[WX_SENSOR_NUM] = {
    [WX_FIELD_WIND_DIRECTION] = TR_WX_WIND_DIRECTION,
    [WX_FIELD_WIND_SPEED] = TR_WX_WIND_SPEED,
    [WX_FIELD_WIND_GUST] = TR_WX_WIND_GUST,
    [WX_FIELD_TEMPERATURE] = TR_WX_TEMPERATURE,
    [WX_FIELD_RAIN_1H] = TR_WX_RAIN_1H,
    [WX_FIELD_RAIN_24H] = TR_WX_RAIN_24H,
    [WX_FIELD_RAIN_MIDNIGHT] = TR_WX_RAIN_MIDNIGHT,
    [WX_FIELD_SNOW_24H] = TR_WX_SNOW,
    [WX_FIELD_HUMIDITY] = TR_WX_HUMIDITY,
    [WX_FIELD_PRESSURE] = TR_WX_PRESSURE,
    [WX_FIELD_LUMINOSITY] = TR_WX_LUMINOSITY,
    [WX_FIELD_FLOOD_HEIGHT_FT] = TR_WX_FLOOD_FT,
    [WX_FIELD_FLOOD_HEIGHT_M] = TR_WX_FLOOD_M,
};

/*
 * Emits the <select> for one field's "source channel", populated from the live
 * sensors_local registry so each option shows the channel *number and name*
 * ("0: bme280", "1: ds18b20", ...). Index 0xFF (255) is the "(none)" choice.
 * If no local sensor driver has registered yet, only "(none)" is offered.
 */
static void wx_channel_select(httpd_req_t *req, int field, uint8_t selected) {
    char buf[160];
    snprintf(buf, sizeof(buf), "<select name='wxCh%d' style='width:150px'>", field);
    httpd_resp_sendstr_chunk(req, buf);

    snprintf(buf, sizeof(buf), "<option value='255'%s>%s</option>", (selected == 0xFF) ? " selected" : "", TR_WX_CHANNEL_NONE);
    httpd_resp_sendstr_chunk(req, buf);

    size_t n = sensors_local_count();
    for (size_t ch = 0; ch < n; ch++) {
        sensor_local_driver_t *d = sensors_local_get(ch);
        const char *nm = (d && d->name) ? d->name : "?";
        snprintf(buf, sizeof(buf), "<option value='%u'%s>%u: %.40s</option>", (unsigned)ch, (selected == ch) ? " selected" : "", (unsigned)ch, nm);
        httpd_resp_sendstr_chunk(req, buf);
    }
    httpd_resp_sendstr_chunk(req, "</select>");
}

esp_err_t page_wx_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_WEATHER, "wx");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/wx'>");

    web_fieldset_open(req, TR_F_WEATHER_STATION);
    web_field_checkbox(req, TR_F_ENABLE_WX, "wxEn", g_config.wx_en);
    web_field_checkbox(req, TR_F_SEND_VIA_RF, "wxTx2rf", g_config.wx_2rf);
    web_field_checkbox(req, TR_F_SEND_VIA_INTERNET, "wxTx2inet", g_config.wx_2inet);
    web_field_checkbox(req, TR_F_ADD_TIMESTAMP, "wxTime", g_config.wx_timestamp);
    web_field_text(req, TR_F_MY_CALLSIGN, "wxMycall", g_config.wx_mycall, 9);
    web_field_int(req, TR_F_SSID, "wxSSID", g_config.wx_ssid);
    web_field_int(req, TR_F_PATH_BITMASK, "wxPath", g_config.wx_path);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_POSITION);
    web_field_checkbox(req, TR_F_USE_GPS, "wxGPS", g_config.wx_gps);
    web_field_float(req, TR_F_LATITUDE, "wxLAT", g_config.wx_lat, "0.0001");
    web_field_float(req, TR_F_LONGITUDE, "wxLON", g_config.wx_lon, "0.0001");
    web_field_float(req, TR_F_ALTITUDE_M, "wxALT", g_config.wx_alt, "1");
    web_field_int(req, TR_F_BEACON_INTERVAL_S, "wxInv", g_config.wx_interval);
    web_field_text(req, TR_F_OBJECT_NAME, "wxObject", g_config.wx_object, 9);
    web_field_text(req, TR_F_COMMENT, "wxComment", g_config.wx_comment, COMMENT_SIZE - 1);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_SENSOR_MAPPING_ENABLE_AVERAGED_SOURCE_CHANNEL);
    httpd_resp_sendstr_chunk(req, "<table><tr><th>" TR_WX_FIELD "</th><th>" TR_F_ENABLE "</th><th>" TR_TLM_AVG "</th><th>" TR_WX_CHANNEL "</th></tr>");
    for (int i = 0; i < WX_SENSOR_NUM; i++) {
        char row[300];
        snprintf(row, sizeof(row),
                 "<tr><td>%s</td>"
                 "<td><input type='checkbox' name='wxEn%d' %s></td>"
                 "<td><input type='checkbox' name='wxAvg%d' %s></td>"
                 "<td>",
                 WX_FIELD_NAME[i], i, g_config.wx_sensor_enable[i] ? "checked" : "", i, g_config.wx_sensor_avg[i] ? "checked" : "");
        httpd_resp_sendstr_chunk(req, row);
        wx_channel_select(req, i, g_config.wx_sensor_ch[i]);
        httpd_resp_sendstr_chunk(req, "</td></tr>");
    }
    httpd_resp_sendstr_chunk(req, "</table>");
    web_fieldset_close(req);

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_wx_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char body[2600];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    g_config.wx_en = web_form_get_bool(body, "wxEn");
    g_config.wx_2rf = web_form_get_bool(body, "wxTx2rf");
    g_config.wx_2inet = web_form_get_bool(body, "wxTx2inet");
    g_config.wx_timestamp = web_form_get_bool(body, "wxTime");
    web_form_get_call(body, "wxMycall", g_config.wx_mycall, sizeof(g_config.wx_mycall));
    g_config.wx_ssid = web_form_get_ssid(body, "wxSSID", g_config.wx_ssid);
    g_config.wx_path = (uint8_t)web_form_get_int(body, "wxPath", g_config.wx_path);

    g_config.wx_gps = web_form_get_bool(body, "wxGPS");
    g_config.wx_lat = web_form_get_float(body, "wxLAT", g_config.wx_lat);
    g_config.wx_lon = web_form_get_float(body, "wxLON", g_config.wx_lon);
    g_config.wx_alt = web_form_get_float(body, "wxALT", g_config.wx_alt);
    g_config.wx_interval = (uint16_t)web_form_get_int(body, "wxInv", g_config.wx_interval);
    web_form_get(body, "wxObject", g_config.wx_object, sizeof(g_config.wx_object));
    web_form_get(body, "wxComment", g_config.wx_comment, sizeof(g_config.wx_comment));

    for (int i = 0; i < WX_SENSOR_NUM; i++) {
        char key[16];
        snprintf(key, sizeof(key), "wxEn%d", i);
        g_config.wx_sensor_enable[i] = web_form_get_bool(body, key);
        snprintf(key, sizeof(key), "wxAvg%d", i);
        g_config.wx_sensor_avg[i] = web_form_get_bool(body, key);
        snprintf(key, sizeof(key), "wxCh%d", i);
        g_config.wx_sensor_ch[i] = (uint8_t)web_form_get_int(body, key, g_config.wx_sensor_ch[i]);
    }

    app_config_save();
    web_send_saved_redirect(req, "/wx");
    return ESP_OK;
}
