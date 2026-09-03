// @file page_igate.c
//
// @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
// @date 2026
// @copyright GNU General Public License v3
// @see https://github.com/hiperiondev/esp32idf_APRS
//
// @note
// This is based on other projects:
//     VP-Digi: https://github.com/sq8vps/vp-digi
//     ESP32APRS: https://github.com/nakhonthai/ESP32APRS_Audio
//     LibAPRS: https://github.com/markqvist/LibAPRS
//
//     please contact their authors for more information.
//
// @brief Web admin "IGate" page: renders and saves the APRS-IS gateway
// configuration (failover servers, callsign, passcode, filters, RF/INET direction
// and beacon settings) in g_config. Beacon position can be typed in, mirrored
// from "My Station" ("Use My Station Data") or taken live from the GNSS
// receiver ("Use GPS", see web_field_use_gps_data() in web_common.c); the
// three sources are mutually exclusive.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "aprs_df.h" // aprs_df_symbol_matches()
#include "aprs_filter.h"
#include "esp_log.h"
#include "gps.h"
#include "igate.h"
#include "pages.h"
#include "str_append.h" // str_copy_utf8_safe()
#include "translations.h"
#include "web_common.h"

static const char *TAG = "page_igate";

// Bounds a range-gate radius submitted by the form. The browser already
// carries min/max on the input, so this is the second layer: a POST does not
// have to come from that form, and the same bounds are applied a third time on
// load in app_config.c. Written so that a value which is not at or above the
// floor - a NaN parsed out of a malformed field included - takes the floor,
// since a NaN would compare false against the gate's own threshold and quietly
// turn the gate off.
static float clampRangeKm(float km) {
    if (!(km >= APRS_RANGE_KM_MIN))
        return APRS_RANGE_KM_MIN;
    if (km > APRS_RANGE_KM_MAX)
        return APRS_RANGE_KM_MAX;
    return km;
}

// Duplex-direction <select>. UI values 0=simplex, 1=+, 2=- (converted to the
// stored int8_t duplex on POST).
static void render_duplex_select(httpd_req_t *req, const char *name, int8_t duplex) {
    int cur = duplex > 0 ? 1 : (duplex < 0 ? 2 : 0);
    web_select_open(req, TR_F_OBJITEM_DUPLEX, name);
    web_select_option(req, 0, TR_F_OBJITEM_DUPLEX_SIMPLEX, cur == 0);
    web_select_option(req, 1, TR_F_OBJITEM_DUPLEX_PLUS, cur == 1);
    web_select_option(req, 2, TR_F_OBJITEM_DUPLEX_MINUS, cur == 2);
    web_select_close(req);
}

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
    web_field_use_gps_data(req, "igateUseGps", g_config.igate_use_gps, "igateUseStation", "igateLAT", "igateLON", "igateALT", NULL, NULL);
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
    web_field_text(req, TR_F_FILTER, "igateFilter", g_config.aprs_filter, (int)(sizeof(g_config.aprs_filter) - 1));
    if (s_filterWarning[0]) {
        char esc_warn[sizeof(s_filterWarning) * 6 + 1];
        web_html_attr_escape(s_filterWarning, esc_warn, sizeof(esc_warn));
        char wbuf[sizeof(esc_warn) + 80];
        snprintf(wbuf, sizeof(wbuf), "<div style='color:#cf222e;font-size:.85em;margin:-6px 0 8px'>%s</div>", esc_warn);
        web_raw(req, wbuf);
        s_filterWarning[0] = 0; // shown once
    }

    // Traffic-log display gate, placed right under the server-side filter it
    // is most easily confused with: that string decides what the server sends
    // this station, this checkbox decides how much of what arrives - and of
    // what the radio hears - the traffic table shows. It changes nothing about
    // what is gated or transmitted, which is what the note says.
    web_field_checkbox(req, TR_F_LOG_AFTER_FILTERS, "igateLogAfterFilters", g_config.igate_log_after_filters);
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:-4px 0 8px'>" TR_NOTE_LOG_AFTER_FILTERS "</p>");

    web_field_text(req, TR_F_COMMENT, "igateComment", g_config.igate_comment, COMMENT_SIZE - 1);
    web_field_checkbox(req, TR_F_TIME_STAMP, "igateTime", g_config.igate_timestamp);
    web_fieldset_close(req);

    // APRS-IS SERVERS ----------------------------------------------------
    // One fieldset per failover slot: an enable checkbox plus host/port, so
    // the operator can list up to APRS_SERVER_NUM alternative APRS-IS
    // servers. The IGate task cycles through the enabled slots in order and
    // retries the next one every second on a connection failure.
    for (int i = 0; i < APRS_SERVER_NUM; i++) {
        char legend[40];
        snprintf(legend, sizeof(legend), "%s %d", TR_F_APRS_IS_SERVER, i + 1);
        web_fieldset_open(req, legend);
        {
            char nameEn[16], nameHost[16], namePort[16];
            snprintf(nameEn, sizeof(nameEn), "srvEn%d", i);
            snprintf(nameHost, sizeof(nameHost), "srvHost%d", i);
            snprintf(namePort, sizeof(namePort), "srvPort%d", i);
            web_field_checkbox(req, TR_F_ENABLE, nameEn, g_config.aprs_server[i].enable);
            web_field_text(req, TR_F_SERVER_HOST, nameHost, g_config.aprs_server[i].host, 19);
            web_field_int(req, TR_F_SERVER_PORT, namePort, g_config.aprs_server[i].port, APRS_PORT_MIN, APRS_PORT_MAX);
        }
        web_fieldset_close(req);
    }

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
    web_field_float(req, TR_F_ALTITUDE_M, "igateALT", g_config.igate_alt, "0.1", WEB_RANGE_ALT_M_MIN, WEB_RANGE_ALT_M_MAX);
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

    // DATA EXTENSION ---------------------------------------------------------
    // The IGate position beacon can carry one of the fixed-station APRS data
    // extensions in the slot after the symbol code (APRS101 ch.7).
    // "Enable data extension" gates whether any of them is transmitted, and
    // "Extension type" picks which:
    //
    //   PHG - power/height/gain/directivity, the classic coverage estimate.
    //   RNG - a single pre-calculated radio range in statute miles, for an
    //         operator who already knows their real coverage radius.
    //   DFS - the same height/gain/directivity codes as PHG, but reporting
    //         received signal strength instead of transmitted power, which is
    //         what an omnidirectional direction-finding station transmits.
    //   DF  - the DF report of APRS101 ch.8: the bearing to a signal and the
    //         NRQ triplet that qualifies it, which is how a station reports a
    //         fix it took itself rather than a coverage estimate. The chapter
    //         makes it meaningful only on the DF symbol (table '/', code
    //         '\\'), so it is transmitted only with that symbol and the note
    //         below says so whenever the two do not agree.
    //
    // The power/gain/height/direction fields below mirror the "My Station" PHG
    // section on the Station page field-for-field (same code tables); DFS
    // reuses the gain/height/direction three and adds its own strength code,
    // and RNG uses none of them. "Use My Station Data" reuses the shared
    // Station PHG values and locks the sub-fields, exactly like the
    // Object/Item PHG blocks on the Objects page. There is no manual
    // "Calculate PHG" button - the computed PHG text is read-only and kept in
    // sync automatically by the script below.
    web_fieldset_open(req, TR_F_EXT_SECTION);
    web_field_checkbox(req, TR_F_ENABLE_EXT, "igatePHGEn", g_config.igate_phg_enable);
    web_select_open(req, TR_F_EXT_TYPE, "igateExtType");
    {
        static const char *extNames[] = { TR_EXT_PHG, TR_EXT_RNG, TR_EXT_DFS, TR_EXT_DF };
        for (size_t i = 0; i < sizeof(extNames) / sizeof(extNames[0]); i++)
            web_select_option(req, i, extNames[i], g_config.igate_ext_type == (uint8_t)i);
    }
    web_select_close(req);
    web_field_int(req, TR_F_EXT_RANGE_MI, "igateRng", g_config.igate_range_miles, APRS_EXT_RANGE_MILES_MIN, APRS_EXT_RANGE_MILES_MAX);
    web_field_int(req, TR_F_EXT_DFS_STRENGTH, "igateDfsS", g_config.igate_dfs_strength, APRS_EXT_DFS_STRENGTH_MIN, APRS_EXT_DFS_STRENGTH_MAX);
    web_field_int(req, TR_F_EXT_DF_BEARING, "igateDfBrg", g_config.igate_df_bearing, APRS_EXT_DF_BEARING_MIN, APRS_EXT_DF_BEARING_MAX);
    web_field_int(req, TR_F_EXT_DF_NRQ_N, "igateDfN", g_config.igate_df_nrq_n, APRS_EXT_DF_NRQ_MIN, APRS_EXT_DF_NRQ_MAX);
    web_field_int(req, TR_F_EXT_DF_NRQ_R, "igateDfR", g_config.igate_df_nrq_r, APRS_EXT_DF_NRQ_MIN, APRS_EXT_DF_NRQ_MAX);
    web_field_int(req, TR_F_EXT_DF_NRQ_Q, "igateDfQ", g_config.igate_df_nrq_q, APRS_EXT_DF_NRQ_MIN, APRS_EXT_DF_NRQ_MAX);

    // The DF report only travels with the DF symbol (APRS101 ch.8), so the
    // condition the beacon encoder enforces is stated here instead of leaving
    // it to be discovered off the air. Rendered with the stored symbol and
    // extension type already applied, so it is right without scripting, and
    // kept in step by apply() below as the operator edits either one.
    {
        bool dfSelected = g_config.igate_phg_enable && g_config.igate_ext_type == (uint8_t)APRS_EXT_DF;
        bool dfSymbol = aprs_df_symbol_matches(g_config.igate_symbol[0], g_config.igate_symbol[1]);
        char buf[420];
        snprintf(buf, sizeof(buf), "<p id='igateDfSymNote' style='color:var(--sub);font-size:12px;margin:4px 0 0%s'>%s</p>",
                 (dfSelected && !dfSymbol) ? "" : ";display:none", TR_NOTE_EXT_DF_SYMBOL);
        web_raw(req, buf);
    }
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
    // Read-only and display-only: it carries no name attribute, so it is never
    // submitted and never stored. The script below fills it on load and on
    // every change of the four PHG sub-fields, which are the values the beacon
    // encoder reads. Same convention as the Station page.
    {
        char buf[550];
        snprintf(buf, sizeof(buf), "<label>%s</label><input type='text' id='igatePHG' maxlength='7' readonly>", TR_F_PHG_TEXT);
        web_raw(req, buf);
    }
    web_fieldset_close(req);

    // Shared "My Station" PHG snapshot, exposed to the script below so
    // "Use My Station Data" can mirror the Station page's values live. Only
    // the four sub-fields travel: the displayed PHG string is always derived
    // from them by the same formula on every page.
    {
        char sbuf[256];
        snprintf(sbuf, sizeof(sbuf), "<script>window.__stnPHG={p:%u,g:%d,h:%u,d:%u};</script>", (unsigned)g_config.my_phg_power,
                 (int)lroundf(g_config.my_phg_gain), (unsigned)g_config.my_phg_height, (unsigned)g_config.my_phg_dir);
        web_raw(req, sbuf);
    }

    // PHG behaviour, same convention as the Objects page's per-element PHG
    // blocks:
    //   * "Enable PHG" off -> the four PHG sub-fields are disabled (and PHG
    //     is not transmitted - enforced server-side too, see beacon.c).
    //   * "Use My Station Data" on -> the sub-fields are filled from the
    //     shared station PHG and disabled (locked), and the computed text is
    //     recalculated from those mirrored values.
    //   * otherwise the sub-fields are editable and the computed PHG text is
    //     recalculated live from them (same formula as the Station page).
    // Disabled controls don't POST, so the save handler snapshots the
    // station PHG when "Use My Station Data" is on and keeps the stored
    // own-values when PHG is disabled - see page_igate_post().
    web_raw(req,
            "<script>(function(){"
            "var ST=window.__stnPHG||{p:0,g:0,h:10,d:0};"
            "function q(n){return document.querySelector(\"[name='\"+n+\"']\");}"
            "function calc(){"
            "var p=parseInt(q('igatePHGPower').value)||0,g=parseInt(q('igatePHGGain').value)||0,"
            "h=parseInt(q('igatePHGHeight').value)||10,d=parseInt(q('igatePHGDir').value)||0;"
            "var P=Math.min(9,Math.max(0,Math.round(Math.sqrt(p))));"
            "var H=Math.min(13,Math.max(0,Math.round(Math.log(h/10)/Math.log(2))));"
            "var G=Math.min(9,Math.max(0,g)),D=Math.min(8,Math.max(0,d));"
            "var o=document.getElementById('igatePHG');"
            "if(o)o.value='PHG'+P+String.fromCharCode(48+H)+G+D;"
            "}"
            "function apply(){"
            "var en=q('igatePHGEn'),us=q('igatePHGUseStation'),ty=q('igateExtType');"
            "if(!en)return;"
            "var on=en.checked,useS=us&&us.checked,t=ty?parseInt(ty.value):0;"
            "if(useS){q('igatePHGPower').value=ST.p;q('igatePHGGain').value=ST.g;q('igatePHGHeight').value=ST.h;q('igatePHGDir').value=ST.d;}"
            "if(ty)ty.disabled=!on;"
            // PHG uses all four sub-fields, DFS every one but the transmit
            // power, RNG and DF none of them; the range, strength and bearing
            // /NRQ inputs each belong to exactly one type. Disabling rather
            // than hiding keeps the layout stable and, since a disabled
            // control does not POST, matches what the save handler below
            // actually stores.
            "var dis=(!on)||useS;"
            "q('igatePHGPower').disabled=dis||t!==0;"
            "['igatePHGGain','igatePHGHeight','igatePHGDir'].forEach(function(nm){var el=q(nm);if(el)el.disabled=dis||t===1||t===3;});"
            "var r=q('igateRng');if(r)r.disabled=(!on)||t!==1;"
            "var sg=q('igateDfsS');if(sg)sg.disabled=(!on)||t!==2;"
            "['igateDfBrg','igateDfN','igateDfR','igateDfQ'].forEach(function(nm){var el=q(nm);if(el)el.disabled=(!on)||t!==3;});"
            // The DF report needs the DF symbol pair to be readable as one,
            // so the note appears exactly when the selected type is DF and
            // the symbol edited above is anything else - the same test the
            // beacon encoder makes before it emits the token.
            "var nt=document.getElementById('igateDfSymNote');"
            "if(nt){var st=q('igateSymTable'),sc=q('igateSymCode');"
            "var isDF=st&&sc&&st.value==='/'&&sc.value==='\\\\';"
            "nt.style.display=(on&&t===3&&!isDF)?'':'none';}"
            "calc();"
            "}"
            "document.addEventListener('DOMContentLoaded',function(){"
            "var en=q('igatePHGEn'),us=q('igatePHGUseStation'),ty=q('igateExtType');"
            "if(en)en.addEventListener('change',apply);"
            "if(us)us.addEventListener('change',apply);"
            "if(ty)ty.addEventListener('change',apply);"
            "['igatePHGPower','igatePHGGain','igatePHGHeight','igatePHGDir'].forEach(function(nm){var el=q(nm);if(el)el.addEventListener('change',calc);});"
            // The symbol lives in another fieldset of this same form, and the
            // DF note depends on it, so editing it re-evaluates the note.
            "['igateSymTable','igateSymCode'].forEach(function(nm){var el=q(nm);if(el)el.addEventListener('input',apply);});"
            "apply();"
            "});"
            "})();</script>");

    // STATUS BEACON ----------------------------------------------------------
    web_fieldset_open(req, TR_F_STATUS_BEACON);
    web_field_int(req, TR_F_STATUS_INTERVAL_S_0_OFF, "igateSTSIntv", g_config.igate_sts_interval, WEB_RANGE_INTERVAL_S_MIN, WEB_RANGE_INTERVAL_S_MAX);
    web_field_text(req, TR_F_STATUS_TEXT, "igateStatus", g_config.igate_status, STATUS_SIZE - 1);
    web_fieldset_close(req);

    // REPEATER RADIO PARAMETERS ----------------------------------------------
    // Recommended travelers' voice repeater this IGate advertises
    // (freqspec.txt), built with objitem_build_freq_block() and prepended to
    // both the position beacon comment and the status report. A frequency of
    // 0 emits no frequency block at all - the tone/duplex/offset sub-fields
    // are then unused.
    web_fieldset_open(req, TR_F_OBJITEM_REPEATER_SECTION);
    web_field_float(req, TR_F_OBJITEM_FREQ, "igateFreq", g_config.igate_freq_mhz, "0.001", 0.0f, 999.999f);
    render_duplex_select(req, "igateDup", g_config.igate_duplex);
    web_field_int(req, TR_F_OBJITEM_OFFSET, "igateOfs", (long)g_config.igate_offset_khz, 0, 65535);
    web_field_float(req, TR_F_OBJITEM_TONE, "igateTone", g_config.igate_tone_tenths / 10.0f, "0.1", 0.0f, 254.1f);
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
            { TR_FILT_BUOY, IGATE_FILT_BUOY, "Buoy" },
            { TR_FILT_POSITION, IGATE_FILT_POSITION, "Position" },
            { TR_FILT_OTHER, IGATE_FILT_OTHER, "Other" },
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
        web_field_float(req, TR_F_RANGE_KM, "rf2inetRangeKm", g_config.rf2inet_range_km, "0.1", APRS_RANGE_KM_MIN, APRS_RANGE_KM_MAX);
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

        // Local distance gate for this direction, the mirror of the RF->INET
        // one above and rendered the same way. It lives here rather than on
        // the BrandMeister page because it governs every line the feed
        // offers the transmitter, whatever its origin; that page reads its
        // state and refuses the worldwide subscription while it is off.
        web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:10px 0 4px'>" TR_NOTE_INET2RF_RANGE "</p>");
        web_field_checkbox(req, TR_F_RANGE_FILTER_EN, "inet2rfRangeEn", g_config.inet2rf_range_en);
        web_field_float(req, TR_F_RANGE_KM, "inet2rfRangeKm", g_config.inet2rf_range_km, "0.1", APRS_RANGE_KM_MIN, APRS_RANGE_KM_MAX);
        web_fieldset_close(req);
    }

    // Message Gating (INET->RF) --------------------------------------------
    // Separate from the payload-type filter above: that decides which KINDS of
    // traffic may reach RF, this decides whether a given message has anyone
    // local to reach. Both have to agree before a message is transmitted.
    web_raw(req, "<h2 style='margin-top:24px'>" TR_F_MSG_GATING "</h2>");
    {
        web_fieldset_open(req, TR_F_MSG_GATING);
        web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_NOTE_MSG_GATING "</p>");
        web_field_checkbox(req, TR_F_MSG_GATE_EN, "igateMsgGateEn", g_config.igate_msg_gate_en);
        web_field_int(req, TR_F_MSG_LOCAL_WINDOW_S, "igateLocalWindowSec", g_config.igate_local_window_sec, IGATE_LOCAL_WINDOW_SEC_MIN,
                      IGATE_LOCAL_WINDOW_SEC_MAX);
        // How recently the addressee was heard and how far away it was when it
        // was heard are separate questions, so the window above is paired with
        // a hop limit here.
        web_field_int(req, TR_F_MSG_MAX_HOPS, "igateMsgMaxHops", g_config.igate_msg_max_hops, IGATE_MSG_MAX_HOPS_MIN, IGATE_MSG_MAX_HOPS_MAX);
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

    // Satellite Gate List ----------------------------------------------------
    // Callsigns of known satellite/ISS digipeaters (RS0ISS, PSAT, etc): a
    // frame routed through one of these is only gated to APRS-IS if that
    // digipeater's path entry is actually marked used (see igateProcess()'s
    // satellite-gate check). Active birds change over time, so this list is
    // editable here instead of requiring a firmware rebuild - same single
    // <form>/Save button as the rest of the page.
    web_raw(req, "<h2 style='margin-top:24px'>" TR_F_SATGATE "</h2>");
    {
        web_fieldset_open(req, TR_F_SATGATE);
        web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_NOTE_SATGATE "</p>");

        for (int i = 0; i < IGATE_SATGATE_MAX; i++) {
            char name[16];
            char label[48];
            snprintf(name, sizeof(name), "satgate%d", i);
            snprintf(label, sizeof(label), "%.40s %d", TR_F_SATGATE_CALL, i + 1);
            web_field_text(req, label, name, g_config.satgate[i], 9);
        }

        web_fieldset_close(req);
    }

    // Duplicate Suppression ---------------------------------------------------
    // Shared by the IGate RF->INET gate and the digipeater RF->RF repeat
    // window (see ::dup_scope_t). A busy digipeater on a congested frequency
    // or a very sparse rural IGate are different regimes, so both the cache
    // size and the window are web-configurable rather than fixed at compile
    // time.
    web_raw(req, "<h2 style='margin-top:24px'>" TR_F_DUP_CACHE "</h2>");
    {
        web_fieldset_open(req, TR_F_DUP_CACHE);
        web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_NOTE_DUP_CACHE "</p>");
        web_field_int(req, TR_F_DUP_CACHE_SIZE, "dupCacheSize", g_config.dup_cache_size, DUP_CACHE_SIZE_MIN, DUP_CACHE_SIZE_MAX);
        web_field_int(req, TR_F_DUP_CACHE_TIMEOUT_MS, "dupCacheTimeoutMs", g_config.dup_cache_timeout_ms, DUP_CACHE_TIMEOUT_MS_MIN, DUP_CACHE_TIMEOUT_MS_MAX);
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
    // unwrap fields, plus the Satellite Gate List's 8 callsign inputs, the
    // Message Gating fieldset's switch and window, and the Duplicate
    // Suppression fieldset's 2 numeric fields), not just the main settings on
    // their own.
    char body[4000];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    app_config_lock();

    // Snapshot of every field that reaches APRS-IS only via connectAprsIs()'s
    // once-at-connect-time read (login identity, server slots, filter),
    // taken before any of them are overwritten below. Compared against the
    // post-save values further down to decide whether the running IGate
    // session needs to be told about the change: connectAprsIs() itself only
    // ever runs again on its own if the link happens to drop, so without
    // this comparison a corrected passcode, a narrowed filter or a switched
    // server would sit in g_config with no visible effect until then.
    char prevMycall[sizeof(g_config.aprs_mycall)];
    char prevPasscode[sizeof(g_config.aprs_passcode)];
    char prevFilter[sizeof(g_config.aprs_filter)];
    uint8_t prevSsid = g_config.aprs_ssid;
    aprs_server_t prevServer[APRS_SERVER_NUM];
    memcpy(prevMycall, g_config.aprs_mycall, sizeof(prevMycall));
    memcpy(prevPasscode, g_config.aprs_passcode, sizeof(prevPasscode));
    memcpy(prevFilter, g_config.aprs_filter, sizeof(prevFilter));
    memcpy(prevServer, g_config.aprs_server, sizeof(prevServer));

    g_config.igate_en = web_form_get_bool(body, "igateEn");
    g_config.igate_use_station = web_form_get_bool(body, "igateUseStation");
    g_config.igate_use_gps = web_form_get_bool(body, "igateUseGps");
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
    // APRS-IS failover server slots - clamp each port defensively against a
    // malformed POST. Port 0 fits in the uint16_t field but is not
    // connectable: getaddrinfo() accepts the service string "0" and
    // connect() then fails, which shows up as a reconnect loop naming a port
    // the IGate could never reach. The intermediate int catches a negative
    // value before the cast. Bounds come from app_config.h so the form
    // min/max, this clamp and the flash-load clamp cannot drift apart.
    for (int i = 0; i < APRS_SERVER_NUM; i++) {
        char nameEn[16], nameHost[16], namePort[16];
        snprintf(nameEn, sizeof(nameEn), "srvEn%d", i);
        snprintf(nameHost, sizeof(nameHost), "srvHost%d", i);
        snprintf(namePort, sizeof(namePort), "srvPort%d", i);
        g_config.aprs_server[i].enable = web_form_get_bool(body, nameEn);
        web_form_get(body, nameHost, g_config.aprs_server[i].host, sizeof(g_config.aprs_server[i].host));
        int srv_port_in = web_form_get_int(body, namePort, g_config.aprs_server[i].port);
        if (srv_port_in < APRS_PORT_MIN)
            srv_port_in = APRS_PORT_MIN;
        else if (srv_port_in > APRS_PORT_MAX)
            srv_port_in = APRS_PORT_MAX;
        g_config.aprs_server[i].port = (uint16_t)srv_port_in;
    }

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

    // Traffic-log display gate. Nothing to push to the running session: it is
    // read straight from g_config every time an entry is about to be added.
    g_config.igate_log_after_filters = web_form_get_bool(body, "igateLogAfterFilters");

    // Decide which of the two live-update paths (if any) the running IGate
    // session needs, by comparing against the pre-save snapshot taken above.
    // A changed identity or server slot needs the session dropped and
    // reopened (igate_request_reconnect()) since those only ever go out in
    // the login line; a changed filter with everything else unchanged can be
    // pushed to the open session instead (igate_request_filter_update()),
    // preserving it. Actually calling the igate component happens after
    // app_config_unlock() below, so this section only records the two
    // booleans.
    bool identityOrServerChanged = memcmp(prevMycall, g_config.aprs_mycall, sizeof(prevMycall)) != 0 || prevSsid != g_config.aprs_ssid ||
                                   memcmp(prevPasscode, g_config.aprs_passcode, sizeof(prevPasscode)) != 0 ||
                                   memcmp(prevServer, g_config.aprs_server, sizeof(prevServer)) != 0;
    bool filterChanged = memcmp(prevFilter, g_config.aprs_filter, sizeof(prevFilter)) != 0;

    // Station Symbol: prefer the separate Table+Symbol fields (top of page);
    // fall back to the legacy combined 2-char field if those aren't present.
    web_form_get_symbol(body, "igateSym", "igateSymbol", g_config.igate_symbol, sizeof(g_config.igate_symbol));

    if (g_config.igate_use_gps) {
        // Same rationale as the Station page's own "Use GPS": trust the
        // receiver's current snapshot, not whatever the disabled fields
        // carried in the POST, and leave a field it has not solved yet at
        // its previous stored value rather than zeroing it.
        gps_data_t g;
        if (gps_snapshot(&g)) {
            if (g.has_position) {
                g_config.igate_lat = (float)g.latitude;
                g_config.igate_lon = (float)g.longitude;
            }
            if (g.has_altitude)
                g_config.igate_alt = (float)g.altitude_m;
        }
    } else {
        g_config.igate_lat = g_config.igate_use_station ? g_config.my_lat : web_form_get_float(body, "igateLAT", g_config.igate_lat);
        g_config.igate_lon = g_config.igate_use_station ? g_config.my_lon : web_form_get_float(body, "igateLON", g_config.igate_lon);
        g_config.igate_alt = g_config.igate_use_station ? g_config.my_alt : web_form_get_float(body, "igateALT", g_config.igate_alt);
    }
    g_config.igate_interval = (uint16_t)web_form_get_int(body, "igateINV", g_config.igate_interval);
    g_config.igate_compress = web_form_get_bool(body, "igateCompress");

    // PHG: same convention as the Objects page's per-element PHG blocks.
    // "Use My Station Data" locks (disables) the sub-fields in the browser,
    // so they don't POST - snapshot the shared station PHG under the config
    // lock (already held here) and use that instead of the form in that case.
    g_config.igate_phg_enable = web_form_get_bool(body, "igatePHGEn");
    // The type select and the two type-specific numbers are disabled in the
    // browser for whichever extension is not selected, so they do not POST;
    // web_form_get_int() therefore keeps the stored value for those, which is
    // what lets an operator switch types back and forth without losing the
    // settings of the other one. Both are clamped here as well as in
    // config_from_json(), the two-layer pattern the rest of the pages use.
    {
        int extType = web_form_get_int(body, "igateExtType", (int)g_config.igate_ext_type);
        if (extType < APRS_EXT_PHG || extType > APRS_EXT_DF)
            extType = APRS_EXT_PHG;
        g_config.igate_ext_type = (uint8_t)extType;

        int rng = web_form_get_int(body, "igateRng", (int)g_config.igate_range_miles);
        if (rng < APRS_EXT_RANGE_MILES_MIN)
            rng = APRS_EXT_RANGE_MILES_MIN;
        if (rng > APRS_EXT_RANGE_MILES_MAX)
            rng = APRS_EXT_RANGE_MILES_MAX;
        g_config.igate_range_miles = (uint16_t)rng;

        int dfs = web_form_get_int(body, "igateDfsS", (int)g_config.igate_dfs_strength);
        if (dfs < APRS_EXT_DFS_STRENGTH_MIN)
            dfs = APRS_EXT_DFS_STRENGTH_MIN;
        if (dfs > APRS_EXT_DFS_STRENGTH_MAX)
            dfs = APRS_EXT_DFS_STRENGTH_MAX;
        g_config.igate_dfs_strength = (uint8_t)dfs;

        // The bearing wraps rather than clamps: 360 and 0 degrees name the
        // same direction and the on-air field is three digits wide, so an
        // out-of-range value still has one correct reading.
        int brg = web_form_get_int(body, "igateDfBrg", (int)g_config.igate_df_bearing) % 360;
        if (brg < 0)
            brg += 360;
        g_config.igate_df_bearing = (uint16_t)brg;

        static const char *nrqKeys[] = { "igateDfN", "igateDfR", "igateDfQ" };
        uint8_t *nrqFields[] = { &g_config.igate_df_nrq_n, &g_config.igate_df_nrq_r, &g_config.igate_df_nrq_q };
        for (size_t i = 0; i < sizeof(nrqKeys) / sizeof(nrqKeys[0]); i++) {
            int digit = web_form_get_int(body, nrqKeys[i], (int)*nrqFields[i]);
            if (digit < APRS_EXT_DF_NRQ_MIN)
                digit = APRS_EXT_DF_NRQ_MIN;
            if (digit > APRS_EXT_DF_NRQ_MAX)
                digit = APRS_EXT_DF_NRQ_MAX;
            *nrqFields[i] = (uint8_t)digit;
        }
    }
    g_config.igate_phg_use_station = web_form_get_bool(body, "igatePHGUseStation");
    if (g_config.igate_phg_use_station) {
        g_config.igate_phg_power = g_config.my_phg_power;
        g_config.igate_phg_gain = g_config.my_phg_gain;
        g_config.igate_phg_height = g_config.my_phg_height;
        g_config.igate_phg_dir = g_config.my_phg_dir;
    } else {
        g_config.igate_phg_power = (uint16_t)web_form_get_int(body, "igatePHGPower", g_config.igate_phg_power);
        g_config.igate_phg_gain = (float)web_form_get_int(body, "igatePHGGain", (int)lroundf(g_config.igate_phg_gain));
        g_config.igate_phg_height = (uint16_t)web_form_get_int(body, "igatePHGHeight", g_config.igate_phg_height);
        g_config.igate_phg_dir = (uint8_t)web_form_get_int(body, "igatePHGDir", g_config.igate_phg_dir);
    }

    g_config.igate_path = (uint8_t)web_form_get_int(body, "igatePath", g_config.igate_path);
    // web_form_get() clamps to a plain byte count, so an operator-typed
    // multi-byte UTF-8 character sitting right at that boundary could arrive
    // already split; stage it and re-cut with str_copy_utf8_safe() so the
    // stored comment - repeated on the air on every future beacon - never
    // carries an incomplete character even in that edge case. On a request
    // with no igateComment field, the staging buffer starts as a copy of the
    // current value, so an absent field leaves it unchanged, matching
    // web_form_get()'s own leave-untouched-when-absent behaviour.
    char igateCommentStage[sizeof(g_config.igate_comment)];
    memcpy(igateCommentStage, g_config.igate_comment, sizeof(igateCommentStage));
    web_form_get(body, "igateComment", igateCommentStage, sizeof(igateCommentStage));
    str_copy_utf8_safe(igateCommentStage, g_config.igate_comment, sizeof(g_config.igate_comment));

    g_config.igate_sts_interval = (uint16_t)web_form_get_int(body, "igateSTSIntv", g_config.igate_sts_interval);
    char igateStatusStage[sizeof(g_config.igate_status)];
    memcpy(igateStatusStage, g_config.igate_status, sizeof(igateStatusStage));
    web_form_get(body, "igateStatus", igateStatusStage, sizeof(igateStatusStage));
    str_copy_utf8_safe(igateStatusStage, g_config.igate_status, sizeof(g_config.igate_status));

    // Repeater radio parameters: same two-layer clamp as every other bounded
    // field on this page.
    {
        float freq = web_form_get_float(body, "igateFreq", g_config.igate_freq_mhz);
        g_config.igate_freq_mhz = freq < 0 ? 0 : freq;

        int dup = web_form_get_int(body, "igateDup", g_config.igate_duplex > 0 ? 1 : (g_config.igate_duplex < 0 ? 2 : 0));
        g_config.igate_duplex = (int8_t)(dup == 1 ? 1 : (dup == 2 ? -1 : 0));

        int ofs = web_form_get_int(body, "igateOfs", (int)g_config.igate_offset_khz);
        if (ofs < 0)
            ofs = 0;
        if (ofs > 65535)
            ofs = 65535;
        g_config.igate_offset_khz = (uint16_t)ofs;

        float tone_hz = web_form_get_float(body, "igateTone", g_config.igate_tone_tenths / 10.0f);
        if (tone_hz < 0)
            tone_hz = 0;
        int tone_tenths = (int)(tone_hz * 10.0f + 0.5f);
        if (tone_tenths > 65535)
            tone_tenths = 65535;
        g_config.igate_tone_tenths = (uint16_t)tone_tenths;
    }

    // [IGATE] Filter checkboxes -> bitmasks. Both fieldsets belong to the same
    // single page form (one Save button for the whole page), so the computed
    // mask always reflects exactly what's currently checked -
    // including "everything unchecked" correctly clearing the mask to 0.
    {
        static const struct {
            uint16_t bit;
            const char *name;
        } filt[] = {
            { IGATE_FILT_MESSAGE, "Message" }, { IGATE_FILT_STATUS, "Status" },     { IGATE_FILT_TELEMETRY, "Telemetry" },
            { IGATE_FILT_WEATHER, "Weather" }, { IGATE_FILT_OBJECT, "Object" },     { IGATE_FILT_ITEM, "Item" },
            { IGATE_FILT_BUOY, "Buoy" },       { IGATE_FILT_POSITION, "Position" }, { IGATE_FILT_OTHER, "Other" },
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
    g_config.rf2inet_range_km = clampRangeKm(g_config.rf2inet_range_km);
    g_config.inet2rf_range_en = web_form_get_bool(body, "inet2rfRangeEn");
    g_config.inet2rf_range_km = clampRangeKm(web_form_get_float(body, "inet2rfRangeKm", g_config.inet2rf_range_km));

    // Turning the gate off withdraws the precondition the BrandMeister
    // monitor subscription was accepted under, so the subscription goes with
    // it rather than being left on with nothing between a worldwide feed and
    // the transmitter. The same rule is enforced on the BrandMeister page and
    // again on load; this is the third place it can be reached from.
    if (g_config.bm_monitor && g_config.inet2rf && !g_config.inet2rf_range_en) {
        ESP_LOGW(TAG, "INET->RF range gate turned off - BrandMeister monitor subscription disabled with it");
        g_config.bm_monitor = false;
    }

    // Selective third-party ('}') unwrap (INET->RF): off by default; only
    // actually takes effect when inet2rf_budlist_mode is BUDLIST_WHITELIST
    // (enforced in aprs_service.c's inet2rfHandler(), not here).
    g_config.inet2rf_3rdparty_unwrap_en = web_form_get_bool(body, "inet2rf3rdPartyUnwrapEn");

    // Message gating: same two-layer clamp as every other bounded field, so a
    // crafted POST cannot widen the window past what the form advertises.
    g_config.igate_msg_gate_en = web_form_get_bool(body, "igateMsgGateEn");
    {
        int windowSec = web_form_get_int(body, "igateLocalWindowSec", g_config.igate_local_window_sec);
        if (windowSec < IGATE_LOCAL_WINDOW_SEC_MIN)
            windowSec = IGATE_LOCAL_WINDOW_SEC_MIN;
        else if (windowSec > IGATE_LOCAL_WINDOW_SEC_MAX)
            windowSec = IGATE_LOCAL_WINDOW_SEC_MAX;
        g_config.igate_local_window_sec = (uint16_t)windowSec;

        int maxHops = web_form_get_int(body, "igateMsgMaxHops", g_config.igate_msg_max_hops);
        if (maxHops < IGATE_MSG_MAX_HOPS_MIN)
            maxHops = IGATE_MSG_MAX_HOPS_MIN;
        else if (maxHops > IGATE_MSG_MAX_HOPS_MAX)
            maxHops = IGATE_MSG_MAX_HOPS_MAX;
        g_config.igate_msg_max_hops = (uint8_t)maxHops;
    }

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

    // Satellite Gate List: same single-form POST as everything else on this
    // page. An empty slot is valid (skipped by igateProcess()).
    for (int i = 0; i < IGATE_SATGATE_MAX; i++) {
        char name[16];
        snprintf(name, sizeof(name), "satgate%d", i);
        web_form_get_call(body, name, g_config.satgate[i], sizeof(g_config.satgate[i]));
    }

    // Duplicate Suppression: cache size and window, clamped to the same
    // DUP_CACHE_SIZE_*/DUP_CACHE_TIMEOUT_MS_* bounds the web form itself
    // advertises, so a malformed POST can never push either value out of
    // range.
    {
        int cacheSize = web_form_get_int(body, "dupCacheSize", g_config.dup_cache_size);
        if (cacheSize < DUP_CACHE_SIZE_MIN)
            cacheSize = DUP_CACHE_SIZE_MIN;
        else if (cacheSize > DUP_CACHE_SIZE_MAX)
            cacheSize = DUP_CACHE_SIZE_MAX;
        g_config.dup_cache_size = (uint8_t)cacheSize;

        int cacheTimeoutMs = web_form_get_int(body, "dupCacheTimeoutMs", g_config.dup_cache_timeout_ms);
        if (cacheTimeoutMs < DUP_CACHE_TIMEOUT_MS_MIN)
            cacheTimeoutMs = DUP_CACHE_TIMEOUT_MS_MIN;
        else if (cacheTimeoutMs > DUP_CACHE_TIMEOUT_MS_MAX)
            cacheTimeoutMs = DUP_CACHE_TIMEOUT_MS_MAX;
        g_config.dup_cache_timeout_ms = (uint16_t)cacheTimeoutMs;
    }

    app_config_unlock();

    // Push the identity/server/filter change (if any) to the running IGate
    // session now that g_config is updated and unlocked - see the
    // comparison above. An identity or server change takes priority over a
    // simultaneous filter change: igate_request_reconnect() drops and
    // reopens the session with the new filter already in its login line, so
    // requesting both would only send the filter twice. Saving an unrelated
    // IGate field trips neither.
    if (identityOrServerChanged)
        igate_request_reconnect();
    else if (filterChanged)
        igate_request_filter_update();

    // The page rendered next is built from the live settings, so the save
    // result is what decides whether the operator is told this reached flash.
    bool ok = app_config_save();
    if (!ok)
        ESP_LOGE(TAG, "IGate settings could not be written to flash");
    web_send_save_result(req, ok, "/igate");
    return ESP_OK;
}
