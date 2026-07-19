/**
 * @file page_igate.c
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
 * @brief Web admin "IGate" page: renders and saves the APRS-IS gateway
 * configuration (server, port, callsign, passcode, filters, RF/INET direction and
 * beacon settings) in g_config.
 */

#include <stdio.h>
#include <string.h>

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
    web_field_use_station_data(req, "igateUseStation", g_config.igate_use_station, "igateMycall", "igateLAT", "igateLON", "igateALT");
    web_field_checkbox(req, TR_F_RF_TO_INTERNET, "rf2inet", g_config.rf2inet);
    web_field_checkbox(req, TR_F_INTERNET_TO_RF, "inet2rf", g_config.inet2rf);
    web_field_text(req, TR_F_MY_CALLSIGN, "igateMycall", g_config.aprs_mycall, 9);
    web_field_int(req, TR_F_SSID, "igateSSID", g_config.aprs_ssid);

    // Station Symbol: Table char + Symbol char shown as two separate 1-char
    // inputs, plus a live graphical icon of the currently selected symbol
    // (matches the /symbol reference page), backed by the same 2-char
    // igate_symbol[3] storage ("<table><symbol>"). Uses the shared picker
    // widget so Digipeater/Tracker render identically.
    web_field_symbol(req, TR_F_STATION_SYMBOL, "igateSym", g_config.igate_symbol);

    web_field_text(req, TR_F_OBJECT_NAME, "igateObject", g_config.igate_object, 9);

    // PATH: dropdown - 0 = direct, 1-4 = "-N" shorthand, 5-8 = custom named
    // path presets configured on the System page (g_config.path[0..3]).
    // Every option carries a short trailing explanation (" - ...") so the
    // user understands what each choice actually does, not just its code.
    web_select_open(req, TR_F_PATH, "igatePath");
    {
        char lbl[160];
        snprintf(lbl, sizeof(lbl), "%.60s - %.90s", TR_PATH_DIRECT, TR_PATH_DIRECT_HINT);
        web_select_option(req, 0, lbl, g_config.igate_path == 0);
    }
    for (int n = 1; n <= 4; n++) {
        char lbl[160];
        snprintf(lbl, sizeof(lbl), "-%d - %d %.90s", n, n, TR_PATH_HOP_HINT);
        web_select_option(req, n, lbl, g_config.igate_path == n);
    }
    for (int i = 0; i < 4; i++) {
        char lbl[220];
        snprintf(lbl, sizeof(lbl), "DST-TRACE %d: %.60s - %.90s", i + 1, g_config.path[i][0] ? g_config.path[i] : TR_PATH_CUSTOM_UNSET,
                 TR_PATH_CUSTOM_HINT);
        web_select_option(req, 5 + i, lbl, g_config.igate_path == (uint8_t)(5 + i));
    }
    web_select_close(req);

    web_fieldset_open(req, TR_F_APRS_IS_SERVER);
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
    web_field_text(req, TR_F_COMMENT, "igateComment", g_config.igate_comment, COMMENT_SIZE - 1);
    web_field_checkbox(req, TR_F_TIME_STAMP, "igateTime", g_config.igate_timestamp);
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

    // POSITION -----------------------------------------------------------
    web_fieldset_open(req, TR_F_POSITION);
    web_field_checkbox(req, TR_F_BEACON_POSITION_2, "igateBcn", g_config.igate_bcn);
    web_field_int(req, TR_F_BEACON_INTERVAL_S, "igateINV", g_config.igate_interval);

    // Location: Fix / GPS radio buttons (mutually exclusive), replaces the
    // old plain "Use GPS position" checkbox with the same underlying field.
    {
        char buf[600];
        snprintf(buf, sizeof(buf),
                 "<label>%s</label>"
                 "<div style='display:flex;gap:14px;align-items:center'>"
                 "<label style='display:inline-flex;align-items:center;margin:0'><input type='radio' name='igateLocMode' value='fix' style='width:auto;margin:0 6px 0 0' %s> %s</label>"
                 "<label style='display:inline-flex;align-items:center;margin:0'><input type='radio' name='igateLocMode' value='gps' style='width:auto;margin:0 6px 0 0' %s> %s</label>"
                 "</div>",
                 TR_F_LOCATION, g_config.igate_gps ? "" : "checked", TR_F_LOC_FIX, g_config.igate_gps ? "checked" : "", TR_F_LOC_GPS);
        web_raw(req, buf);
    }
    web_field_float(req, TR_F_LATITUDE, "igateLAT", g_config.igate_lat, "0.0001");
    web_field_float(req, TR_F_LONGITUDE, "igateLON", g_config.igate_lon, "0.0001");
    web_field_float(req, TR_F_ALTITUDE_M, "igateALT", g_config.igate_alt, "1");

    // TX Channel: RF / Internet (same data as igate_loc2rf/igate_loc2inet).
    {
        char buf[600];
        snprintf(buf, sizeof(buf),
                 "<label>%s</label>"
                 "<div style='display:flex;gap:14px;align-items:center'>"
                 "<label style='display:inline-flex;align-items:center;margin:0'><input type='checkbox' name='igatePos2rf' style='width:16px;height:16px;margin:0 6px 0 0' %s> RF</label>"
                 "<label style='display:inline-flex;align-items:center;margin:0'><input type='checkbox' name='igatePos2inet' style='width:16px;height:16px;margin:0 6px 0 0' %s> Internet</label>"
                 "</div>",
                 TR_F_TX_CHANNEL, g_config.igate_loc2rf ? "checked" : "", g_config.igate_loc2inet ? "checked" : "");
        web_raw(req, buf);
    }
    web_fieldset_close(req);

    // PHG ------------------------------------------------------------------
    web_fieldset_open(req, TR_F_PHG_SECTION);
    web_select_open(req, TR_F_RADIO_TX_POWER, "igatePHGPower");
    {
        static const int watts[] = { 0, 1, 4, 9, 16, 25, 36, 49, 64, 81 };
        for (size_t i = 0; i < sizeof(watts) / sizeof(watts[0]); i++) {
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%d", watts[i]);
            web_select_option(req, watts[i], lbl, g_config.igate_phg_power == (uint16_t)watts[i]);
        }
    }
    web_select_close(req);
    web_field_float(req, TR_F_ANTENNA_GAIN, "igatePHGGain", g_config.igate_phg_gain, "0.1");
    web_select_open(req, TR_F_HEIGHT, "igatePHGHeight");
    {
        static const int feet[] = { 10, 20, 40, 80, 160, 320, 640, 1280, 2560, 5120 };
        for (size_t i = 0; i < sizeof(feet) / sizeof(feet[0]); i++) {
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%d", feet[i]);
            web_select_option(req, feet[i], lbl, g_config.igate_phg_height == (uint16_t)feet[i]);
        }
    }
    web_select_close(req);
    web_select_open(req, TR_F_ANTENNA_DIRECTION, "igatePHGDir");
    {
        static const char *dirs[] = { TR_DIR_OMNI, TR_DIR_N, TR_DIR_NE, TR_DIR_E, TR_DIR_SE, TR_DIR_S, TR_DIR_SW, TR_DIR_W, TR_DIR_NW };
        for (int i = 0; i < 9; i++)
            web_select_option(req, i, dirs[i], g_config.igate_phg_dir == (uint8_t)i);
    }
    web_select_close(req);
    {
        char buf[500];
        snprintf(buf, sizeof(buf),
                 "<label>%s</label>"
                 "<div style='display:flex;gap:6px;align-items:center'>"
                 "<input type='text' name='igatePHG' id='igatePHG' value='%s' maxlength='7' style='flex:1'>"
                 "<button type='button' class='secondary' onclick='calcPHG()'>%s</button>"
                 "</div>",
                 TR_F_PHG_TEXT, g_config.igate_phg, TR_BTN_CALCULATE_PHG);
        web_raw(req, buf);
    }
    web_fieldset_close(req);

    web_raw(req, "<script>"
                 "function calcPHG(){"
                 "var p=parseInt(document.querySelector(\"select[name='igatePHGPower']\").value)||0;"
                 "var g=parseFloat(document.querySelector(\"input[name='igatePHGGain']\").value)||0;"
                 "var h=parseInt(document.querySelector(\"select[name='igatePHGHeight']\").value)||10;"
                 "var d=parseInt(document.querySelector(\"select[name='igatePHGDir']\").value)||0;"
                 "var P=Math.min(9,Math.max(0,Math.round(Math.sqrt(p))));"
                 "var H=Math.min(9,Math.max(0,Math.round(Math.log(h/10)/Math.log(2))));"
                 "var G=Math.min(9,Math.max(0,Math.round(g)));"
                 "var D=Math.min(8,Math.max(0,d));"
                 "document.getElementById('igatePHG').value='PHG'+P+H+G+D;"
                 "}"
                 "</script>");

    // STATUS BEACON ----------------------------------------------------------
    web_fieldset_open(req, TR_F_STATUS_BEACON);
    web_field_int(req, TR_F_STATUS_INTERVAL_S_0_OFF, "igateSTSIntv", g_config.igate_sts_interval);
    web_field_text(req, TR_F_STATUS_TEXT, "igateStatus", g_config.igate_status, STATUS_SIZE - 1);
    web_fieldset_close(req);

    web_raw(req, "<p style='color:var(--sub);font-size:12px'>" TR_NOTE_TLM_IGATE "</p>");

    // [IGATE] Filter --------------------------------------------------------
    // Same <form> as everything above (no separate form/submit here) so the
    // single Save button at the bottom of the page persists the whole page
    // - main settings and filters - in one POST.
    web_raw(req, "<h2 style='margin-top:24px'>" TR_F_IGATE_FILTER "</h2>");
    {
        static const struct {
            const char *label;
            uint16_t bit;
            const char *name;
        } filt[] = {
            { TR_FILT_MESSAGE, IGATE_FILT_MESSAGE, "Message" },       { TR_FILT_STATUS, IGATE_FILT_STATUS, "Status" },
            { TR_FILT_TELEMETRY, IGATE_FILT_TELEMETRY, "Telemetry" }, { TR_FILT_WEATHER, IGATE_FILT_WEATHER, "Weather" },
            { TR_FILT_OBJECT, IGATE_FILT_OBJECT, "Object" },          { TR_FILT_ITEM, IGATE_FILT_ITEM, "Item" },
            { TR_FILT_QUERY, IGATE_FILT_QUERY, "Query" },             { TR_FILT_BUOY, IGATE_FILT_BUOY, "Buoy" },
            { TR_FILT_POSITION, IGATE_FILT_POSITION, "Position" },
        };
        web_fieldset_open(req, TR_F_FILTER_RF2INET);
        for (size_t i = 0; i < sizeof(filt) / sizeof(filt[0]); i++) {
            char name[24];
            snprintf(name, sizeof(name), "rf2inetF_%s", filt[i].name);
            web_field_checkbox(req, filt[i].label, name, (g_config.rf2inetFilter & filt[i].bit) != 0);
        }
        web_fieldset_close(req);

        web_fieldset_open(req, TR_F_FILTER_INET2RF);
        for (size_t i = 0; i < sizeof(filt) / sizeof(filt[0]); i++) {
            char name[24];
            snprintf(name, sizeof(name), "inet2rfF_%s", filt[i].name);
            web_field_checkbox(req, filt[i].label, name, (g_config.inet2rfFilter & filt[i].bit) != 0);
        }
        web_fieldset_close(req);
    }
    web_raw(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");

    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_igate_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    // Sized for the whole page's form data in one POST now that Filters
    // share the same <form> as the rest of the page (main settings + up to
    // 18 filter checkboxes), not just the main settings on their own.
    char body[3000];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    g_config.igate_en = web_form_get_bool(body, "igateEn");
    g_config.igate_use_station = web_form_get_bool(body, "igateUseStation");
    g_config.rf2inet = web_form_get_bool(body, "rf2inet");
    g_config.inet2rf = web_form_get_bool(body, "inet2rf");
    g_config.igate_bcn = web_form_get_bool(body, "igateBcn");
    g_config.igate_loc2rf = web_form_get_bool(body, "igatePos2rf");
    g_config.igate_loc2inet = web_form_get_bool(body, "igatePos2inet");
    g_config.igate_timestamp = web_form_get_bool(body, "igateTime");

    if (g_config.igate_use_station) {
        strncpy(g_config.aprs_mycall, g_config.my_callsign, sizeof(g_config.aprs_mycall) - 1);
        g_config.aprs_mycall[sizeof(g_config.aprs_mycall) - 1] = 0;
    } else {
        web_form_get_call(body, "igateMycall", g_config.aprs_mycall, sizeof(g_config.aprs_mycall));
    }
    g_config.aprs_ssid = web_form_get_ssid(body, "igateSSID", g_config.aprs_ssid);
    web_form_get(body, "igatePasscode", g_config.aprs_passcode, sizeof(g_config.aprs_passcode));
    web_form_get(body, "igateHost", g_config.aprs_host, sizeof(g_config.aprs_host));
    g_config.aprs_port = (uint16_t)web_form_get_int(body, "igatePort", g_config.aprs_port);
    web_form_get(body, "igateFilter", g_config.aprs_filter, sizeof(g_config.aprs_filter));

    // Station Symbol: prefer the separate Table+Symbol fields (top of page);
    // fall back to the legacy combined 2-char field if those aren't present.
    web_form_get_symbol(body, "igateSym", "igateSymbol", g_config.igate_symbol, sizeof(g_config.igate_symbol));

    // Location mode radio -> igate_gps bool
    {
        char mode[8] = { 0 };
        web_form_get(body, "igateLocMode", mode, sizeof(mode));
        if (mode[0])
            g_config.igate_gps = (strcmp(mode, "gps") == 0);
        else
            g_config.igate_gps = web_form_get_bool(body, "igateGPS");
    }

    g_config.igate_lat = g_config.igate_use_station ? g_config.my_lat : web_form_get_float(body, "igateLAT", g_config.igate_lat);
    g_config.igate_lon = g_config.igate_use_station ? g_config.my_lon : web_form_get_float(body, "igateLON", g_config.igate_lon);
    g_config.igate_alt = g_config.igate_use_station ? g_config.my_alt : web_form_get_float(body, "igateALT", g_config.igate_alt);
    g_config.igate_interval = (uint16_t)web_form_get_int(body, "igateINV", g_config.igate_interval);
    web_form_get(body, "igateObject", g_config.igate_object, sizeof(g_config.igate_object));
    web_form_get(body, "igatePHG", g_config.igate_phg, sizeof(g_config.igate_phg));
    g_config.igate_phg_power = (uint16_t)web_form_get_int(body, "igatePHGPower", g_config.igate_phg_power);
    g_config.igate_phg_gain = web_form_get_float(body, "igatePHGGain", g_config.igate_phg_gain);
    g_config.igate_phg_height = (uint16_t)web_form_get_int(body, "igatePHGHeight", g_config.igate_phg_height);
    g_config.igate_phg_dir = (uint8_t)web_form_get_int(body, "igatePHGDir", g_config.igate_phg_dir);
    g_config.igate_path = (uint8_t)web_form_get_int(body, "igatePath", g_config.igate_path);
    web_form_get(body, "igateComment", g_config.igate_comment, sizeof(g_config.igate_comment));

    g_config.igate_sts_interval = (uint16_t)web_form_get_int(body, "igateSTSIntv", g_config.igate_sts_interval);
    web_form_get(body, "igateStatus", g_config.igate_status, sizeof(g_config.igate_status));

    // [IGATE] Filter checkboxes -> bitmasks. Both fieldsets are now part of
    // the same single page form (one Save button for the whole page), so the
    // computed mask always reflects exactly what's currently checked -
    // including "everything unchecked" correctly clearing the mask to 0.
    {
        static const struct {
            uint16_t bit;
            const char *name;
        } filt[] = {
            { IGATE_FILT_MESSAGE, "Message" }, { IGATE_FILT_STATUS, "Status" },     { IGATE_FILT_TELEMETRY, "Telemetry" },
            { IGATE_FILT_WEATHER, "Weather" }, { IGATE_FILT_OBJECT, "Object" },     { IGATE_FILT_ITEM, "Item" },
            { IGATE_FILT_QUERY, "Query" },     { IGATE_FILT_BUOY, "Buoy" },         { IGATE_FILT_POSITION, "Position" },
        };
        uint16_t rf2inetF = 0, inet2rfF = 0;
        for (size_t i = 0; i < sizeof(filt) / sizeof(filt[0]); i++) {
            char name[24];
            snprintf(name, sizeof(name), "rf2inetF_%s", filt[i].name);
            if (web_form_get_bool(body, name))
                rf2inetF |= filt[i].bit;
            snprintf(name, sizeof(name), "inet2rfF_%s", filt[i].name);
            if (web_form_get_bool(body, name))
                inet2rfF |= filt[i].bit;
        }
        g_config.rf2inetFilter = rf2inetF;
        g_config.inet2rfFilter = inet2rfF;
    }

    app_config_save();
    web_send_saved_redirect(req, "/igate");
    return ESP_OK;
}
