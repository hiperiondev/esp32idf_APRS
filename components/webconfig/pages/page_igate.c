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

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "aprs_filter.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

// Warning banner for the APRS-IS filter field, set at save time (grammar
// error and/or truncation) and shown once on the next GET of this page.
// Free text (not TR_*), so no translation table entry - it always embeds the
// dynamic term/character details.
static char s_filterWarning[160] = { 0 };

esp_err_t page_igate_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_IGATE, "igate");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/igate'>");

    // IGATE ------------------------------------------------------------
    // Enable/direction toggles only; callsign/SSID/symbol/object/path now
    // live in their own independent "Station" fieldset below, matching the
    // Digipeater/Tracker pages' convention of a separate Station block.
    web_fieldset_open(req, TR_F_IGATE);
    web_field_checkbox(req, TR_F_ENABLE_IGATE, "igateEn", g_config.igate_en);
    web_field_use_station_data(req, "igateUseStation", g_config.igate_use_station, "igateMycall", "igateLAT", "igateLON", "igateALT");
    web_field_checkbox(req, TR_F_RF_TO_INTERNET, "rf2inet", g_config.rf2inet);
    web_field_checkbox(req, TR_F_INTERNET_TO_RF, "inet2rf", g_config.inet2rf);
    web_fieldset_close(req);

    // STATION ------------------------------------------------------------
    web_fieldset_open(req, TR_F_STATION);
    web_field_text(req, TR_F_MY_CALLSIGN, "igateMycall", g_config.aprs_mycall, 9);
    web_field_int(req, TR_F_SSID, "igateSSID", g_config.aprs_ssid, WEB_RANGE_SSID_MIN, WEB_RANGE_SSID_MAX);

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
        snprintf(lbl, sizeof(lbl), "DST-TRACE %d: %.60s - %.90s", i + 1, g_config.path[i][0] ? g_config.path[i] : TR_PATH_CUSTOM_UNSET, TR_PATH_CUSTOM_HINT);
        web_select_option(req, 5 + i, lbl, g_config.igate_path == (uint8_t)(5 + i));
    }
    web_select_close(req);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_APRS_IS_SERVER);
    {
        char esc_pc[sizeof(g_config.aprs_passcode) * 6 + 1];
        web_html_attr_escape(g_config.aprs_passcode, esc_pc, sizeof(esc_pc));
        char pcbuf[560];
        snprintf(pcbuf, sizeof(pcbuf),
                 "<label>%s</label>"
                 "<div style='display:flex;gap:6px;align-items:center'>"
                 "<input type='password' name='igatePasscode' id='igatePasscode' value='%s' maxlength='5' style='flex:1'>"
                 "<button type='button' class='secondary' onclick='aprsAutoGenPasscode()'>%s</button>"
                 "</div>"
                 "<label class='pwd-show'><input type='checkbox' onclick=\"togglePwd('igatePasscode',this)\"> " TR_SHOW_PASSWORD "</label>",
                 TR_F_APRS_PASSCODE, esc_pc, TR_BTN_AUTO_GENERATE);
        web_raw(req, pcbuf);
    }
    web_field_text(req, TR_F_SERVER_HOST, "igateHost", g_config.aprs_host, 19);
    web_field_int(req, TR_F_SERVER_PORT, "igatePort", g_config.aprs_port, APRS_PORT_MIN, APRS_PORT_MAX);
    web_field_text(req, TR_F_FILTER, "igateFilter", g_config.aprs_filter, 29);
    if (s_filterWarning[0]) {
        char esc_warn[sizeof(s_filterWarning) * 6 + 1];
        web_html_attr_escape(s_filterWarning, esc_warn, sizeof(esc_warn));
        char wbuf[sizeof(esc_warn) + 80];
        snprintf(wbuf, sizeof(wbuf), "<div style='color:#cf222e;font-size:.85em;margin:-6px 0 8px'>%s</div>", esc_warn);
        web_raw(req, wbuf);
        s_filterWarning[0] = 0; // shown once
    }
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
    web_field_int(req, TR_F_BEACON_INTERVAL_S, "igateINV", g_config.igate_interval, WEB_RANGE_INTERVAL_S_MIN, WEB_RANGE_INTERVAL_S_MAX);

    web_field_float(req, TR_F_LATITUDE, "igateLAT", g_config.igate_lat, "0.0001", WEB_RANGE_LAT_MIN, WEB_RANGE_LAT_MAX);
    web_field_float(req, TR_F_LONGITUDE, "igateLON", g_config.igate_lon, "0.0001", WEB_RANGE_LON_MIN, WEB_RANGE_LON_MAX);
    web_field_float(req, TR_F_ALTITUDE_M, "igateALT", g_config.igate_alt, "1", WEB_RANGE_ALT_M_MIN, WEB_RANGE_ALT_M_MAX);
    web_field_checkbox(req, TR_F_COMPRESS_POSITION, "igateCompress", g_config.igate_compress);

    // TX Channel: RF / Internet (same data as igate_loc2rf/igate_loc2inet).
    {
        char buf[600];
        snprintf(buf, sizeof(buf),
                 "<label>%s</label>"
                 "<div style='display:flex;gap:14px;align-items:center'>"
                 "<label style='display:inline-flex;align-items:center;margin:0'><input type='checkbox' name='igatePos2rf' "
                 "style='width:16px;height:16px;margin:0 6px 0 0' %s> RF</label>"
                 "<label style='display:inline-flex;align-items:center;margin:0'><input type='checkbox' name='igatePos2inet' "
                 "style='width:16px;height:16px;margin:0 6px 0 0' %s> Internet</label>"
                 "</div>",
                 TR_F_TX_CHANNEL, g_config.igate_loc2rf ? "checked" : "", g_config.igate_loc2inet ? "checked" : "");
        web_raw(req, buf);
    }
    web_fieldset_close(req);

    // PHG ------------------------------------------------------------------
    // Mirrors the "My Station" PHG section on the Station page field-for-field
    // (same power/gain/height/direction code tables), plus two toggles ahead
    // of the fields: "Enable PHG" gates whether the PHG data extension is
    // transmitted in the IGate position beacon at all, and "Use My Station
    // Data" reuses the shared Station PHG values and locks the sub-fields,
    // exactly like the Object/Item PHG blocks on the Objects page. There is
    // no manual "Calculate PHG" button - the computed PHG text is read-only
    // and kept in sync automatically by the script below.
    web_fieldset_open(req, TR_F_PHG_SECTION);
    web_field_checkbox(req, TR_F_ENABLE_PHG, "igatePHGEn", g_config.igate_phg_enable);
    web_field_checkbox(req, TR_USE_MY_STATION_DATA, "igatePHGUseStation", g_config.igate_phg_use_station);
    web_select_open(req, TR_F_RADIO_TX_POWER, "igatePHGPower");
    {
        // APRS PHG power code table (P digit 0-9), rounded to these fixed
        // Watt values only - not a free-edit field. Same table as Station.
        static const int watts[] = { 0, 1, 5, 10, 15, 25, 35, 50, 65, 80 };
        for (size_t i = 0; i < sizeof(watts) / sizeof(watts[0]); i++) {
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%d", watts[i]);
            web_select_option(req, watts[i], lbl, g_config.igate_phg_power == (uint16_t)watts[i]);
        }
    }
    web_select_close(req);
    web_select_open(req, TR_F_ANTENNA_GAIN, "igatePHGGain");
    {
        // APRS PHG gain code table (G digit 0-9), in dB - not a free-edit field.
        for (int i = 0; i <= 9; i++) {
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%d", i);
            web_select_option(req, i, lbl, (int)lroundf(g_config.igate_phg_gain) == i);
        }
    }
    web_select_close(req);
    web_select_open(req, TR_F_HEIGHT_M, "igatePHGHeight");
    {
        // APRS PHG height code table (H digit), 10*2^n feet, extended beyond
        // the standard 0-9 digits to also allow the requested larger values.
        static const int feet[] = { 10, 20, 40, 80, 160, 320, 640, 1280, 2560, 5120, 10240, 20480, 40960, 81920 };
        for (size_t i = 0; i < sizeof(feet) / sizeof(feet[0]); i++) {
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%d", (int)lroundf(feet[i] * 0.3048f));
            // Option value stays in feet (the APRS code table's own unit);
            // only the label shown to the user is converted to meters.
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

    // Computed PHG value --------------------------------------------------
    // Read-only; automatically recalculated by JS whenever any PHG section
    // value changes (no manual "calculate" button), exactly like Station.
    {
        char buf[550];
        snprintf(buf, sizeof(buf),
                 "<label>%s</label>"
                 "<input type='text' name='igatePHG' id='igatePHG' value='%s' maxlength='7' readonly>",
                 TR_F_PHG_TEXT, g_config.igate_phg);
        web_raw(req, buf);
    }
    web_fieldset_close(req);

    // Shared "My Station" PHG snapshot, exposed to the script below so
    // "Use My Station Data" can mirror the Station page's values live.
    // my_phg only ever contains [A-Z0-9] (a computed "PHGxxxx" string), so it
    // is safe inside a single-quoted JS string literal as-is.
    {
        char sbuf[256];
        snprintf(sbuf, sizeof(sbuf), "<script>window.__stnPHG={p:%u,g:%d,h:%u,d:%u,s:'%.7s'};</script>", (unsigned)g_config.my_phg_power,
                 (int)lroundf(g_config.my_phg_gain), (unsigned)g_config.my_phg_height, (unsigned)g_config.my_phg_dir, g_config.my_phg);
        web_raw(req, sbuf);
    }

    // PHG behaviour, same convention as the Objects page's per-element PHG
    // blocks:
    //   * "Enable PHG" off -> the four PHG sub-fields are disabled (and PHG
    //     is not transmitted - enforced server-side too, see beacon.c).
    //   * "Use My Station Data" on -> the sub-fields are filled from the
    //     shared station PHG and disabled (locked), and the computed text
    //     shows the station PHG string.
    //   * otherwise the sub-fields are editable and the computed PHG text is
    //     recalculated live from them (same formula as the Station page).
    // Disabled controls don't POST, so the save handler snapshots the
    // station PHG when "Use My Station Data" is on and keeps the stored
    // own-values when PHG is disabled - see page_igate_post().
    web_raw(req,
            "<script>(function(){"
            "var ST=window.__stnPHG||{p:0,g:0,h:10,d:0,s:''};"
            "function q(n){return document.querySelector(\"[name='\"+n+\"']\");}"
            "function calc(){"
            "var p=parseInt(q('igatePHGPower').value)||0,g=parseInt(q('igatePHGGain').value)||0,"
            "h=parseInt(q('igatePHGHeight').value)||10,d=parseInt(q('igatePHGDir').value)||0;"
            "var P=Math.min(9,Math.max(0,Math.round(Math.sqrt(p))));"
            "var H=Math.min(13,Math.max(0,Math.round(Math.log(h/10)/Math.log(2))));"
            "var G=Math.min(9,Math.max(0,g)),D=Math.min(8,Math.max(0,d));"
            "var o=q('igatePHG');"
            "if(o)o.value='PHG'+P+String.fromCharCode(48+H)+G+D;"
            "}"
            "function apply(){"
            "var en=q('igatePHGEn'),us=q('igatePHGUseStation');"
            "if(!en)return;"
            "var on=en.checked,useS=us&&us.checked;"
            "if(useS){q('igatePHGPower').value=ST.p;q('igatePHGGain').value=ST.g;q('igatePHGHeight').value=ST.h;q('igatePHGDir').value=ST.d;}"
            "var dis=(!on)||useS;"
            "['igatePHGPower','igatePHGGain','igatePHGHeight','igatePHGDir'].forEach(function(nm){var el=q(nm);if(el)el.disabled=dis;});"
            "if(useS){var o=q('igatePHG');if(o)o.value=ST.s;}else{calc();}"
            "}"
            "document.addEventListener('DOMContentLoaded',function(){"
            "var en=q('igatePHGEn'),us=q('igatePHGUseStation');"
            "if(en)en.addEventListener('change',apply);"
            "if(us)us.addEventListener('change',apply);"
            "['igatePHGPower','igatePHGGain','igatePHGHeight','igatePHGDir'].forEach(function(nm){var el=q(nm);if(el)el.addEventListener('change',calc);});"
            "apply();"
            "});"
            "})();</script>");

    // STATUS BEACON ----------------------------------------------------------
    web_fieldset_open(req, TR_F_STATUS_BEACON);
    web_field_int(req, TR_F_STATUS_INTERVAL_S_0_OFF, "igateSTSIntv", g_config.igate_sts_interval, WEB_RANGE_INTERVAL_S_MIN, WEB_RANGE_INTERVAL_S_MAX);
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
            { TR_FILT_MESSAGE, IGATE_FILT_MESSAGE, "Message" },
            { TR_FILT_STATUS, IGATE_FILT_STATUS, "Status" },
            { TR_FILT_TELEMETRY, IGATE_FILT_TELEMETRY, "Telemetry" },
            { TR_FILT_WEATHER, IGATE_FILT_WEATHER, "Weather" },
            { TR_FILT_OBJECT, IGATE_FILT_OBJECT, "Object" },
            { TR_FILT_ITEM, IGATE_FILT_ITEM, "Item" },
            { TR_FILT_QUERY, IGATE_FILT_QUERY, "Query" },
            { TR_FILT_BUOY, IGATE_FILT_BUOY, "Buoy" },
            { TR_FILT_POSITION, IGATE_FILT_POSITION, "Position" },
        };
        web_fieldset_open(req, TR_F_FILTER_RF2INET);
        for (size_t i = 0; i < sizeof(filt) / sizeof(filt[0]); i++) {
            char name[24];
            snprintf(name, sizeof(name), "rf2inetF_%s", filt[i].name);
            web_field_checkbox(req, filt[i].label, name, (g_config.rf2inetFilter & filt[i].bit) != 0);
        }

        // Local range/prefix gate: independent of, and composed with (AND
        // semantics), the payload-type checkboxes just above. Same single
        // <form>/Save button as the rest of the page.
        web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:10px 0 4px'>" TR_NOTE_RANGE_PREFIX "</p>");
        web_field_checkbox(req, TR_F_RANGE_FILTER_EN, "rf2inetRangeEn", g_config.rf2inet_range_en);
        web_field_float(req, TR_F_RANGE_KM, "rf2inetRangeKm", g_config.rf2inet_range_km, "0.1", 0.0f, 20038.0f);
        web_field_checkbox(req, TR_F_PREFIX_FILTER_EN, "rf2inetPrefixEn", g_config.rf2inet_prefix_en);
        web_field_text(req, TR_F_PREFIXES, "rf2inetPrefixes", g_config.rf2inet_prefixes, sizeof(g_config.rf2inet_prefixes) - 1);
        web_fieldset_close(req);

        web_fieldset_open(req, TR_F_FILTER_INET2RF);
        for (size_t i = 0; i < sizeof(filt) / sizeof(filt[0]); i++) {
            char name[24];
            snprintf(name, sizeof(name), "inet2rfF_%s", filt[i].name);
            web_field_checkbox(req, filt[i].label, name, (g_config.inet2rfFilter & filt[i].bit) != 0);
        }

        // Selective third-party ('}') unwrap: off by default, and only ever
        // effective together with the Callsign Filter's Internet->RF mode
        // being set to Whitelist below (see aprs_service.c's inet2rfHandler()
        // and aprs_filter_classify_thirdparty_inner()). Placed here (rather
        // than only in the Callsign Filter section) since it's fundamentally
        // a "what may pass the INET->RF type filter" exception.
        web_field_checkbox(req, TR_F_3RDPARTY_UNWRAP_EN, "inet2rf3rdPartyUnwrapEn", g_config.inet2rf_3rdparty_unwrap_en);
        web_raw(req, "<p style='color:#cf222e;font-size:12px;margin:4px 0'>" TR_NOTE_3RDPARTY_UNWRAP "</p>");
        web_fieldset_close(req);
    }

    // Callsign Filter (budlist) --------------------------------------------
    // Local whitelist/blacklist, independent of - and composed with (AND
    // semantics) - the payload-type filters just above. One shared 8-entry
    // callsign list, one mode per direction. Same single <form>/Save button
    // as the rest of the page.
    web_raw(req, "<h2 style='margin-top:24px'>" TR_F_CALLSIGN_FILTER "</h2>");
    {
        web_fieldset_open(req, TR_F_CALLSIGN_FILTER);

        web_select_open(req, TR_F_BUDLIST_MODE_RF2INET, "rf2inetBudlistMode");
        web_select_option(req, BUDLIST_OFF, TR_BUDLIST_OFF, g_config.rf2inet_budlist_mode == BUDLIST_OFF);
        web_select_option(req, BUDLIST_WHITELIST, TR_BUDLIST_WHITELIST, g_config.rf2inet_budlist_mode == BUDLIST_WHITELIST);
        web_select_option(req, BUDLIST_BLACKLIST, TR_BUDLIST_BLACKLIST, g_config.rf2inet_budlist_mode == BUDLIST_BLACKLIST);
        web_select_close(req);

        web_select_open(req, TR_F_BUDLIST_MODE_INET2RF, "inet2rfBudlistMode");
        web_select_option(req, BUDLIST_OFF, TR_BUDLIST_OFF, g_config.inet2rf_budlist_mode == BUDLIST_OFF);
        web_select_option(req, BUDLIST_WHITELIST, TR_BUDLIST_WHITELIST, g_config.inet2rf_budlist_mode == BUDLIST_WHITELIST);
        web_select_option(req, BUDLIST_BLACKLIST, TR_BUDLIST_BLACKLIST, g_config.inet2rf_budlist_mode == BUDLIST_BLACKLIST);
        web_select_close(req);

        web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_NOTE_BUDLIST "</p>");

        for (int i = 0; i < IGATE_BUDLIST_MAX; i++) {
            char name[16];
            char label[24];
            snprintf(name, sizeof(name), "budlist%d", i);
            snprintf(label, sizeof(label), "%s %d", TR_F_BUDLIST_CALL, i + 1);
            web_field_text(req, label, name, g_config.budlist[i], 9);
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
    // 18 filter checkboxes + the Callsign Filter fieldset: 2 mode selects
    // and 8 callsign inputs, plus the range/prefix gate and third-party
    // unwrap fields), not just the main settings on their own.
    char body[3400];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    app_config_lock();
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
    // APRS-IS server port - clamp defensively against a malformed POST. Port 0
    // fits in the uint16_t field but is not connectable: getaddrinfo() accepts
    // the service string "0" and connect() then fails, which shows up as a
    // five-second reconnect loop naming a port the IGate could never reach.
    // The intermediate int catches a negative value before the cast. Bounds
    // come from app_config.h so the form min/max, this clamp and the
    // flash-load clamp cannot drift apart.
    int aprs_port_in = web_form_get_int(body, "igatePort", g_config.aprs_port);
    if (aprs_port_in < APRS_PORT_MIN)
        aprs_port_in = APRS_PORT_MIN;
    else if (aprs_port_in > APRS_PORT_MAX)
        aprs_port_in = APRS_PORT_MAX;
    g_config.aprs_port = (uint16_t)aprs_port_in;

    // Filter field: still saved verbatim (it's the user's to set), but first
    // checked for (a) truncation against the destination buffer and (b)
    // server-side filter grammar errors, either of which is surfaced as a
    // warning banner under the field on the next GET of this page.
    // web_form_get() itself has no way to report truncation - it clamps
    // silently - so the raw submitted length is measured here, before the
    // clamp, against the destination capacity.
    {
        char rawFilter[512];
        bool present = web_form_get(body, "igateFilter", rawFilter, sizeof(rawFilter));
        size_t destCap = sizeof(g_config.aprs_filter); // includes NUL
        s_filterWarning[0] = 0;

        if (present && strlen(rawFilter) >= destCap) {
            snprintf(s_filterWarning, sizeof(s_filterWarning), "filter string was truncated to %u characters", (unsigned)(destCap - 1));
        }

        web_form_get(body, "igateFilter", g_config.aprs_filter, sizeof(g_config.aprs_filter));

        char reason[100];
        if (!aprs_filter_validate_server_string(g_config.aprs_filter, reason, sizeof(reason))) {
            if (s_filterWarning[0]) {
                // Both conditions hit - truncation is the more actionable /
                // surprising one (data loss), so it takes priority; the
                // truncated result may itself be what fails grammar.
            } else {
                snprintf(s_filterWarning, sizeof(s_filterWarning), "%s", reason);
            }
        }
    }

    // Station Symbol: prefer the separate Table+Symbol fields (top of page);
    // fall back to the legacy combined 2-char field if those aren't present.
    web_form_get_symbol(body, "igateSym", "igateSymbol", g_config.igate_symbol, sizeof(g_config.igate_symbol));

    g_config.igate_lat = g_config.igate_use_station ? g_config.my_lat : web_form_get_float(body, "igateLAT", g_config.igate_lat);
    g_config.igate_lon = g_config.igate_use_station ? g_config.my_lon : web_form_get_float(body, "igateLON", g_config.igate_lon);
    g_config.igate_alt = g_config.igate_use_station ? g_config.my_alt : web_form_get_float(body, "igateALT", g_config.igate_alt);
    g_config.igate_interval = (uint16_t)web_form_get_int(body, "igateINV", g_config.igate_interval);
    g_config.igate_compress = web_form_get_bool(body, "igateCompress");
    web_form_get(body, "igateObject", g_config.igate_object, sizeof(g_config.igate_object));

    // PHG: same convention as the Objects page's per-element PHG blocks.
    // "Use My Station Data" locks (disables) the sub-fields in the browser,
    // so they don't POST - snapshot the shared station PHG under the config
    // lock (already held here) and use that instead of the form in that case.
    g_config.igate_phg_enable = web_form_get_bool(body, "igatePHGEn");
    g_config.igate_phg_use_station = web_form_get_bool(body, "igatePHGUseStation");
    if (g_config.igate_phg_use_station) {
        g_config.igate_phg_power = g_config.my_phg_power;
        g_config.igate_phg_gain = g_config.my_phg_gain;
        g_config.igate_phg_height = g_config.my_phg_height;
        g_config.igate_phg_dir = g_config.my_phg_dir;
        strncpy(g_config.igate_phg, g_config.my_phg, sizeof(g_config.igate_phg) - 1);
        g_config.igate_phg[sizeof(g_config.igate_phg) - 1] = 0;
    } else {
        web_form_get(body, "igatePHG", g_config.igate_phg, sizeof(g_config.igate_phg));
        g_config.igate_phg_power = (uint16_t)web_form_get_int(body, "igatePHGPower", g_config.igate_phg_power);
        g_config.igate_phg_gain = (float)web_form_get_int(body, "igatePHGGain", (int)lroundf(g_config.igate_phg_gain));
        g_config.igate_phg_height = (uint16_t)web_form_get_int(body, "igatePHGHeight", g_config.igate_phg_height);
        g_config.igate_phg_dir = (uint8_t)web_form_get_int(body, "igatePHGDir", g_config.igate_phg_dir);
    }

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
            { IGATE_FILT_MESSAGE, "Message" }, { IGATE_FILT_STATUS, "Status" }, { IGATE_FILT_TELEMETRY, "Telemetry" },
            { IGATE_FILT_WEATHER, "Weather" }, { IGATE_FILT_OBJECT, "Object" }, { IGATE_FILT_ITEM, "Item" },
            { IGATE_FILT_QUERY, "Query" },     { IGATE_FILT_BUOY, "Buoy" },     { IGATE_FILT_POSITION, "Position" },
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

    // Local range/prefix gate (RF->INET): same single-form POST as
    // everything else on this page.
    g_config.rf2inet_range_en = web_form_get_bool(body, "rf2inetRangeEn");
    g_config.rf2inet_range_km = web_form_get_float(body, "rf2inetRangeKm", g_config.rf2inet_range_km);
    g_config.rf2inet_prefix_en = web_form_get_bool(body, "rf2inetPrefixEn");
    web_form_get(body, "rf2inetPrefixes", g_config.rf2inet_prefixes, sizeof(g_config.rf2inet_prefixes));

    // Selective third-party ('}') unwrap (INET->RF): off by default; only
    // actually takes effect when inet2rf_budlist_mode is BUDLIST_WHITELIST
    // (enforced in aprs_service.c's inet2rfHandler(), not here).
    g_config.inet2rf_3rdparty_unwrap_en = web_form_get_bool(body, "inet2rf3rdPartyUnwrapEn");

    // Callsign Filter (budlist): mode selects + shared callsign list. Same
    // single-form POST as everything else on this page.
    {
        int rf2inetMode = web_form_get_int(body, "rf2inetBudlistMode", g_config.rf2inet_budlist_mode);
        int inet2rfMode = web_form_get_int(body, "inet2rfBudlistMode", g_config.inet2rf_budlist_mode);
        g_config.rf2inet_budlist_mode = (rf2inetMode == BUDLIST_WHITELIST || rf2inetMode == BUDLIST_BLACKLIST) ? (budlist_mode_t)rf2inetMode : BUDLIST_OFF;
        g_config.inet2rf_budlist_mode = (inet2rfMode == BUDLIST_WHITELIST || inet2rfMode == BUDLIST_BLACKLIST) ? (budlist_mode_t)inet2rfMode : BUDLIST_OFF;

        for (int i = 0; i < IGATE_BUDLIST_MAX; i++) {
            char name[16];
            snprintf(name, sizeof(name), "budlist%d", i);
            web_form_get_call(body, name, g_config.budlist[i], sizeof(g_config.budlist[i]));
        }
    }

    app_config_unlock();

    app_config_save();
    web_send_saved_redirect(req, "/igate");
    return ESP_OK;
}
