/**
 * @file page_tlm.c
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
 * @brief Web admin "Telemetry" page: renders and saves the two telemetry
 * channels' configuration (enable, RF/INET destinations, SSID and per-channel
 * parameters) in g_config.
 */

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

static void send_tlm_channel_form(httpd_req_t *req, int ch) {
    const char *pfx = ch == 0 ? "tlm0" : "tlm1";
    bool en = ch == 0 ? g_config.tlm0_en : g_config.tlm1_en;
    bool rf = ch == 0 ? g_config.tlm0_2rf : g_config.tlm1_2rf;
    bool inet = ch == 0 ? g_config.tlm0_2inet : g_config.tlm1_2inet;
    uint8_t ssid = ch == 0 ? g_config.tlm0_ssid : g_config.tlm1_ssid;
    const char *mycall = ch == 0 ? g_config.tlm0_mycall : g_config.tlm1_mycall;
    uint8_t path = ch == 0 ? g_config.tlm0_path : g_config.tlm1_path;
    uint16_t info_iv = ch == 0 ? g_config.tlm0_info_interval : g_config.tlm1_info_interval;
    uint16_t data_iv = ch == 0 ? g_config.tlm0_data_interval : g_config.tlm1_data_interval;

    char legend[64];
    snprintf(legend, sizeof(legend), TR_TLM_CHANNEL_LEGEND, ch);
    web_fieldset_open(req, legend);
    char n[24];
    snprintf(n, sizeof(n), "%sEn", pfx);
    web_field_checkbox(req, TR_F_ENABLE, n, en);
    snprintf(n, sizeof(n), "%sTx2rf", pfx);
    web_field_checkbox(req, TR_F_SEND_VIA_RF, n, rf);
    snprintf(n, sizeof(n), "%sTx2inet", pfx);
    web_field_checkbox(req, TR_F_SEND_VIA_INTERNET, n, inet);
    snprintf(n, sizeof(n), "%sMycall", pfx);
    web_field_text(req, TR_F_MY_CALLSIGN, n, mycall, 9);
    snprintf(n, sizeof(n), "%sSSID", pfx);
    web_field_int(req, TR_F_SSID, n, ssid);
    snprintf(n, sizeof(n), "%sPath", pfx);
    web_field_int(req, TR_F_PATH_BITMASK, n, path);
    snprintf(n, sizeof(n), "%sInfoInv", pfx);
    web_field_int(req, TR_F_PARM_UNIT_EQNS_INTERVAL_S, n, info_iv);
    snprintf(n, sizeof(n), "%sDataInv", pfx);
    web_field_int(req, TR_F_DATA_INTERVAL_S, n, data_iv);
    web_fieldset_close(req);

    // PARM/UNIT names (first 5 of 13, the ones typically shown on the 5-channel table)
    char legend2[64];
    snprintf(legend2, sizeof(legend2), TR_TLM_PARAM_LEGEND, ch);
    web_fieldset_open(req, legend2);
    httpd_resp_sendstr_chunk(req, "<table><tr><th>#</th><th>" TR_TLM_NAME_PARM "</th><th>" TR_F_UNIT "</th></tr>");
    for (int i = 0; i < TLM_PARM_NUM; i++) {
        const char *parm = ch == 0 ? g_config.tlm0_PARM[i] : g_config.tlm1_PARM[i];
        const char *unit = ch == 0 ? g_config.tlm0_UNIT[i] : g_config.tlm1_UNIT[i];
        char row[220];
        snprintf(row, sizeof(row),
                 "<tr><td>%d</td><td><input type='text' name='%sPARM%d' value='%.9s' maxlength='9'></td>"
                 "<td><input type='text' name='%sUNIT%d' value='%.7s' maxlength='7'></td></tr>",
                 i + 1, pfx, i, parm, pfx, i, unit);
        httpd_resp_sendstr_chunk(req, row);
    }
    httpd_resp_sendstr_chunk(req, "</table>");
    web_fieldset_close(req);
}

static void send_service_tlm_form(httpd_req_t *req, const char *label, const char *pfx, const bool *avg, const uint8_t *sensor, const uint8_t *prec,
                                  const float *offset, char parm[][10], char unit[][8]) {
    char legend[64];
    snprintf(legend, sizeof(legend), TR_TLM_SERVICE_LEGEND, label);
    web_fieldset_open(req, legend);
    httpd_resp_sendstr_chunk(req, "<table><tr><th>#</th><th>" TR_TLM_AVG "</th><th>" TR_TLM_SENSOR "</th><th>" TR_TLM_PREC "</th>"
                                  "<th>" TR_TLM_OFFSET "</th><th>" TR_F_NAME "</th><th>" TR_F_UNIT "</th></tr>");
    for (int i = 0; i < TLM_CH; i++) {
        char row[700];
        snprintf(row, sizeof(row),
                 "<tr><td>%d</td>"
                 "<td><input type='checkbox' name='%sTlmAvg%d' %s></td>"
                 "<td><input type='number' style='width:60px' name='%sTlmSen%d' value='%d'></td>"
                 "<td><input type='number' style='width:50px' name='%sTlmPrec%d' value='%d'></td>"
                 "<td><input type='number' step='0.01' style='width:80px' name='%sTlmOff%d' value='%g'></td>"
                 "<td><input type='text' style='width:70px' name='%sTlmPARM%d' value='%.9s' maxlength='9'></td>"
                 "<td><input type='text' style='width:60px' name='%sTlmUNIT%d' value='%.7s' maxlength='7'></td></tr>",
                 i + 1, pfx, i, avg[i] ? "checked" : "", pfx, i, sensor[i], pfx, i, prec[i], pfx, i, (double)offset[i], pfx, i, parm[i], pfx, i, unit[i]);
        httpd_resp_sendstr_chunk(req, row);
    }
    httpd_resp_sendstr_chunk(req, "</table>");
    web_fieldset_close(req);
}

esp_err_t page_tlm_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_TELEMETRY, "tlm");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/tlm'>");

    send_tlm_channel_form(req, 0);
    send_tlm_channel_form(req, 1);
    send_service_tlm_form(req, TR_F_TRACKER, "trk", g_config.trk_tlm_avg, g_config.trk_tlm_sensor, g_config.trk_tlm_precision, g_config.trk_tlm_offset,
                          g_config.trk_tlm_PARM, g_config.trk_tlm_UNIT);
    send_service_tlm_form(req, TR_DASH_DIGI_SHORT, "digi", g_config.digi_tlm_avg, g_config.digi_tlm_sensor, g_config.digi_tlm_precision,
                          g_config.digi_tlm_offset, g_config.digi_tlm_PARM, g_config.digi_tlm_UNIT);
    send_service_tlm_form(req, TR_F_IGATE, "igate", g_config.igate_tlm_avg, g_config.igate_tlm_sensor, g_config.igate_tlm_precision, g_config.igate_tlm_offset,
                          g_config.igate_tlm_PARM, g_config.igate_tlm_UNIT);

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

static void parse_tlm_channel(const char *body, int ch) {
    const char *pfx = ch == 0 ? "tlm0" : "tlm1";
    char n[24];
    snprintf(n, sizeof(n), "%sEn", pfx);
    bool en = web_form_get_bool(body, n);
    snprintf(n, sizeof(n), "%sTx2rf", pfx);
    bool rf = web_form_get_bool(body, n);
    snprintf(n, sizeof(n), "%sTx2inet", pfx);
    bool inet = web_form_get_bool(body, n);
    char mycall[10];
    snprintf(n, sizeof(n), "%sMycall", pfx);
    web_form_get_call(body, n, mycall, sizeof(mycall));
    snprintf(n, sizeof(n), "%sSSID", pfx);
    int ssid = (int)web_form_get_ssid(body, n, 0);
    snprintf(n, sizeof(n), "%sPath", pfx);
    int path = web_form_get_int(body, n, 0);
    snprintf(n, sizeof(n), "%sInfoInv", pfx);
    int info_iv = web_form_get_int(body, n, 0);
    snprintf(n, sizeof(n), "%sDataInv", pfx);
    int data_iv = web_form_get_int(body, n, 0);

    if (ch == 0) {
        g_config.tlm0_en = en;
        g_config.tlm0_2rf = rf;
        g_config.tlm0_2inet = inet;
        strncpy(g_config.tlm0_mycall, mycall, sizeof(g_config.tlm0_mycall) - 1);
        g_config.tlm0_ssid = ssid;
        g_config.tlm0_path = path;
        g_config.tlm0_info_interval = info_iv;
        g_config.tlm0_data_interval = data_iv;
    } else {
        g_config.tlm1_en = en;
        g_config.tlm1_2rf = rf;
        g_config.tlm1_2inet = inet;
        strncpy(g_config.tlm1_mycall, mycall, sizeof(g_config.tlm1_mycall) - 1);
        g_config.tlm1_ssid = ssid;
        g_config.tlm1_path = path;
        g_config.tlm1_info_interval = info_iv;
        g_config.tlm1_data_interval = data_iv;
    }
    for (int i = 0; i < TLM_PARM_NUM; i++) {
        char key[24];
        snprintf(key, sizeof(key), "%sPARM%d", pfx, i);
        char(*P)[10] = ch == 0 ? g_config.tlm0_PARM : g_config.tlm1_PARM;
        web_form_get(body, key, P[i], 10);
        snprintf(key, sizeof(key), "%sUNIT%d", pfx, i);
        char(*U)[8] = ch == 0 ? g_config.tlm0_UNIT : g_config.tlm1_UNIT;
        web_form_get(body, key, U[i], 8);
    }
}

static void parse_service_tlm(const char *body, const char *pfx, bool *avg, uint8_t *sensor, uint8_t *prec, float *offset, char parm[][10], char unit[][8]) {
    for (int i = 0; i < TLM_CH; i++) {
        char key[24];
        snprintf(key, sizeof(key), "%sTlmAvg%d", pfx, i);
        avg[i] = web_form_get_bool(body, key);
        snprintf(key, sizeof(key), "%sTlmSen%d", pfx, i);
        sensor[i] = (uint8_t)web_form_get_int(body, key, sensor[i]);
        snprintf(key, sizeof(key), "%sTlmPrec%d", pfx, i);
        prec[i] = (uint8_t)web_form_get_int(body, key, prec[i]);
        snprintf(key, sizeof(key), "%sTlmOff%d", pfx, i);
        offset[i] = web_form_get_float(body, key, offset[i]);
        snprintf(key, sizeof(key), "%sTlmPARM%d", pfx, i);
        web_form_get(body, key, parm[i], 10);
        snprintf(key, sizeof(key), "%sTlmUNIT%d", pfx, i);
        web_form_get(body, key, unit[i], 8);
    }
}

esp_err_t page_tlm_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char *body = malloc(6000);
    if (!body) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    if (web_read_body(req, body, 6000) < 0) {
        free(body);
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    parse_tlm_channel(body, 0);
    parse_tlm_channel(body, 1);
    parse_service_tlm(body, "trk", g_config.trk_tlm_avg, g_config.trk_tlm_sensor, g_config.trk_tlm_precision, g_config.trk_tlm_offset, g_config.trk_tlm_PARM,
                      g_config.trk_tlm_UNIT);
    parse_service_tlm(body, "digi", g_config.digi_tlm_avg, g_config.digi_tlm_sensor, g_config.digi_tlm_precision, g_config.digi_tlm_offset,
                      g_config.digi_tlm_PARM, g_config.digi_tlm_UNIT);
    parse_service_tlm(body, "igate", g_config.igate_tlm_avg, g_config.igate_tlm_sensor, g_config.igate_tlm_precision, g_config.igate_tlm_offset,
                      g_config.igate_tlm_PARM, g_config.igate_tlm_UNIT);

    free(body);
    app_config_save();
    web_send_saved_redirect(req, "/tlm");
    return ESP_OK;
}
