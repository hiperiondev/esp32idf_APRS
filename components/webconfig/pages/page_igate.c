#include "app_config.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

esp_err_t page_igate_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_IGATE, "igate");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/igate'>");

    web_fieldset_open(req, TR_F_IGATE);
    web_field_checkbox(req, TR_F_ENABLE_IGATE, "igateEn", g_config.igate_en);
    web_field_checkbox(req, TR_F_RF_TO_INTERNET, "rf2inet", g_config.rf2inet);
    web_field_checkbox(req, TR_F_INTERNET_TO_RF, "inet2rf", g_config.inet2rf);
    web_field_checkbox(req, TR_F_BEACON_POSITION_2, "igateBcn", g_config.igate_bcn);
    web_field_checkbox(req, TR_F_BEACON_VIA_RF, "igatePos2rf", g_config.igate_loc2rf);
    web_field_checkbox(req, TR_F_BEACON_VIA_INTERNET, "igatePos2inet", g_config.igate_loc2inet);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_APRS_IS_SERVER);
    web_field_text(req, TR_F_MY_CALLSIGN, "igateMycall", g_config.aprs_mycall, 9);
    web_field_int(req, TR_F_SSID, "igateSSID", g_config.aprs_ssid);
    {
        char pcbuf[560];
        snprintf(pcbuf, sizeof(pcbuf),
                 "<label>%s</label>"
                 "<div style='display:flex;gap:6px;align-items:center'>"
                 "<input type='password' name='igatePasscode' id='igatePasscode' value='%s' maxlength='5' style='flex:1'>"
                 "<button type='button' class='secondary' onclick='aprsAutoGenPasscode()'>%s</button>"
                 "</div>"
                 "<label class='pwd-show'><input type='checkbox' onclick=\"togglePwd('igatePasscode',this)\"> " TR_SHOW_PASSWORD "</label>",
                 TR_F_APRS_PASSCODE, g_config.aprs_passcode, TR_BTN_AUTO_GENERATE);
        web_raw(req, pcbuf);
    }
    web_field_text(req, TR_F_SERVER_HOST, "igateHost", g_config.aprs_host, 19);
    web_field_int(req, TR_F_SERVER_PORT, "igatePort", g_config.aprs_port);
    web_field_text(req, TR_F_FILTER, "igateFilter", g_config.aprs_filter, 29);
    web_fieldset_close(req);

    web_raw(req, "<script>"
                 "function aprsAutoGenPasscode(){"
                 "var callInput=document.querySelector(\"input[name='igateMycall']\");"
                 "var pcInput=document.getElementById('igatePasscode');"
                 "if(!callInput||!pcInput)return;"
                 "var call=callInput.value.toUpperCase().split('-')[0];"
                 "if(!call){pcInput.value='-1';return;}"
                 "var hash=0x73e2;"
                 "for(var i=0;i<call.length;i+=2){"
                 "hash^=call.charCodeAt(i)<<8;"
                 "if(i+1<call.length){hash^=call.charCodeAt(i+1);}"
                 "}"
                 "hash&=0x7fff;"
                 "pcInput.value=String(hash);"
                 "}"
                 "</script>");

    web_fieldset_open(req, TR_F_POSITION);
    web_field_checkbox(req, TR_F_USE_GPS_POSITION, "igateGPS", g_config.igate_gps);
    web_field_float(req, TR_F_LATITUDE, "igateLAT", g_config.igate_lat, "0.0001");
    web_field_float(req, TR_F_LONGITUDE, "igateLON", g_config.igate_lon, "0.0001");
    web_field_float(req, TR_F_ALTITUDE_M, "igateALT", g_config.igate_alt, "1");
    web_field_int(req, TR_F_BEACON_INTERVAL_S, "igateINV", g_config.igate_interval);
    web_field_text(req, TR_F_SYMBOL_2_CHARS, "igateSymbol", g_config.igate_symbol, 2);
    web_field_text(req, TR_F_OBJECT_NAME, "igateObject", g_config.igate_object, 9);
    web_field_text(req, TR_F_PHG, "igatePHG", g_config.igate_phg, 7);
    web_field_int(req, TR_F_PATH_BITMASK, "igatePath", g_config.igate_path);
    web_field_text(req, TR_F_COMMENT, "igateComment", g_config.igate_comment, COMMENT_SIZE - 1);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_STATUS_BEACON);
    web_field_int(req, TR_F_STATUS_INTERVAL_S_0_OFF, "igateSTSIntv", g_config.igate_sts_interval);
    web_field_text(req, TR_F_STATUS_TEXT, "igateStatus", g_config.igate_status, STATUS_SIZE - 1);
    web_fieldset_close(req);

    web_raw(req, "<p style='color:var(--sub);font-size:12px'>" TR_NOTE_TLM_IGATE "</p>"
                 "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_igate_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char body[1600];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    g_config.igate_en = web_form_get_bool(body, "igateEn");
    g_config.rf2inet = web_form_get_bool(body, "rf2inet");
    g_config.inet2rf = web_form_get_bool(body, "inet2rf");
    g_config.igate_bcn = web_form_get_bool(body, "igateBcn");
    g_config.igate_loc2rf = web_form_get_bool(body, "igatePos2rf");
    g_config.igate_loc2inet = web_form_get_bool(body, "igatePos2inet");

    web_form_get(body, "igateMycall", g_config.aprs_mycall, sizeof(g_config.aprs_mycall));
    g_config.aprs_ssid = (uint8_t)web_form_get_int(body, "igateSSID", g_config.aprs_ssid);
    web_form_get(body, "igatePasscode", g_config.aprs_passcode, sizeof(g_config.aprs_passcode));
    web_form_get(body, "igateHost", g_config.aprs_host, sizeof(g_config.aprs_host));
    g_config.aprs_port = (uint16_t)web_form_get_int(body, "igatePort", g_config.aprs_port);
    web_form_get(body, "igateFilter", g_config.aprs_filter, sizeof(g_config.aprs_filter));

    g_config.igate_gps = web_form_get_bool(body, "igateGPS");
    g_config.igate_lat = web_form_get_float(body, "igateLAT", g_config.igate_lat);
    g_config.igate_lon = web_form_get_float(body, "igateLON", g_config.igate_lon);
    g_config.igate_alt = web_form_get_float(body, "igateALT", g_config.igate_alt);
    g_config.igate_interval = (uint16_t)web_form_get_int(body, "igateINV", g_config.igate_interval);
    web_form_get(body, "igateSymbol", g_config.igate_symbol, sizeof(g_config.igate_symbol));
    web_form_get(body, "igateObject", g_config.igate_object, sizeof(g_config.igate_object));
    web_form_get(body, "igatePHG", g_config.igate_phg, sizeof(g_config.igate_phg));
    g_config.igate_path = (uint8_t)web_form_get_int(body, "igatePath", g_config.igate_path);
    web_form_get(body, "igateComment", g_config.igate_comment, sizeof(g_config.igate_comment));

    g_config.igate_sts_interval = (uint16_t)web_form_get_int(body, "igateSTSIntv", g_config.igate_sts_interval);
    web_form_get(body, "igateStatus", g_config.igate_status, sizeof(g_config.igate_status));

    app_config_save();
    web_send_saved_redirect(req, "/igate");
    return ESP_OK;
}
