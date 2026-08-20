// @file page_digi.c
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
// @brief Web admin "Digipeater" page: renders and saves the digipeater
// configuration (callsign/SSID, the n-N alias table with its trapping,
// preemptive and legacy routing policies, the beacon settings and the beacon's
// APRS data extension) in g_config. Position can be typed in, mirrored from
// "My Station" ("Use My Station Data") or taken live from the GNSS receiver
// ("Use GPS", see web_field_use_gps_data() in web_common.c); the three
// sources are mutually exclusive.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "aprs_df.h" // aprs_df_symbol_matches()
#include "esp_log.h"
#include "gps.h"
#include "pages.h"
#include "str_append.h" // str_copy_utf8_safe()
#include "translations.h"
#include "web_common.h"

static const char *TAG = "page_digi";

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

esp_err_t page_digi_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_DIGIPEATER, "digi");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/digi'>");

    web_fieldset_open(req, TR_F_DIGIPEATER);
    web_field_checkbox(req, TR_F_ENABLE_DIGIPEATER, "digiEn", g_config.digi_en);
    web_field_use_station_data(req, "digiUseStation", g_config.digi_use_station, "digiMycall", "digiLAT", "digiLON", "digiAlt");
    web_field_use_gps_data(req, "digiUseGps", g_config.digi_use_gps, "digiUseStation", "digiLAT", "digiLON", "digiAlt", NULL, NULL);
    web_field_checkbox(req, TR_F_ADD_TIMESTAMP, "digiTime", g_config.digi_timestamp);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_STATION);
    web_field_text(req, TR_F_MY_CALLSIGN, "digiMycall", g_config.digi_mycall, 9);
    web_field_int(req, TR_F_SSID, "digiSSID", g_config.digi_ssid, WEB_RANGE_SSID_MIN, WEB_RANGE_SSID_MAX);
    web_field_path_checkboxes(req, "digiPath", g_config.digi_path);
    web_fieldset_close(req);

    // n-N Path Aliases. This table is the whole of what the digipeater
    // repeats: an alias absent from it is not honoured, whatever it is called.
    // Each row carries its own hop limit so a fill-in WIDE1-1 and a two-hop
    // WIDE2-2 can coexist, and its own mode so a regional alias can be run
    // untraced if the operator wants it that way.
    web_fieldset_open(req, TR_F_DIGI_ALIASES);
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_NOTE_DIGI_ALIASES "</p>");
    web_field_checkbox(req, TR_F_DIGI_FILLIN_ONLY, "digiFillinOnly", g_config.digi_fillin_only);
    web_select_open(req, TR_F_DIGI_TRAP_ACTION, "digiTrapNClamp");
    web_select_option(req, 1, TR_DIGI_TRAP_CLAMP, g_config.digi_trap_n_clamp);
    web_select_option(req, 0, TR_DIGI_TRAP_DROP, !g_config.digi_trap_n_clamp);
    web_select_close(req);
    for (int i = 0; i < DIGI_ALIAS_MAX; i++) {
        char name[20];
        char label[64];

        snprintf(label, sizeof(label), "%s %d", TR_F_DIGI_ALIAS, i + 1);
        snprintf(name, sizeof(name), "digiAlias%d", i);
        web_field_text(req, label, name, g_config.digi_alias[i].alias, DIGI_ALIAS_LEN - 1);

        snprintf(label, sizeof(label), "%s %d", TR_F_DIGI_MAX_N, i + 1);
        snprintf(name, sizeof(name), "digiAliasN%d", i);
        web_field_int(req, label, name, g_config.digi_alias[i].max_n, 1, DIGI_ALIAS_MAX_N);

        snprintf(label, sizeof(label), "%s %d", TR_F_DIGI_ALIAS_MODE, i + 1);
        snprintf(name, sizeof(name), "digiAliasM%d", i);
        web_select_open(req, label, name);
        web_select_option(req, DIGI_ALIAS_OFF, TR_DIGI_MODE_OFF, g_config.digi_alias[i].mode == DIGI_ALIAS_OFF);
        web_select_option(req, DIGI_ALIAS_TRACE, TR_DIGI_MODE_TRACE, g_config.digi_alias[i].mode == DIGI_ALIAS_TRACE);
        web_select_option(req, DIGI_ALIAS_FLOOD, TR_DIGI_MODE_FLOOD, g_config.digi_alias[i].mode == DIGI_ALIAS_FLOOD);
        web_select_close(req);
    }
    web_select_open(req, TR_F_DIGI_PREEMPT, "digiPreempt");
    web_select_option(req, DIGI_PREEMPT_OFF, TR_DIGI_PREEMPT_OFF, g_config.digi_preempt == DIGI_PREEMPT_OFF);
    web_select_option(req, DIGI_PREEMPT_MARK, TR_DIGI_PREEMPT_MARK, g_config.digi_preempt == DIGI_PREEMPT_MARK);
    web_select_option(req, DIGI_PREEMPT_DROP, TR_DIGI_PREEMPT_DROP, g_config.digi_preempt == DIGI_PREEMPT_DROP);
    web_select_close(req);
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_NOTE_DIGI_PREEMPT "</p>");
    web_field_checkbox(req, TR_F_DIGI_DEST_SSID, "digiDestSsidEn", g_config.digi_dest_ssid_en);
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_NOTE_DIGI_DEST_SSID "</p>");
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_BEACON_POSITION);
    web_field_checkbox(req, TR_F_BEACON_POSITION_2, "digiBcn", g_config.digi_bcn);
    web_field_checkbox(req, TR_F_BEACON_VIA_RF, "digiPos2rf", g_config.digi_loc2rf);
    web_field_checkbox(req, TR_F_BEACON_VIA_INTERNET, "digiPos2inet", g_config.digi_loc2inet);
    web_field_checkbox(req, TR_F_COMPRESS_POSITION, "digiCompress", g_config.digi_compress);
    web_field_float(req, TR_F_LATITUDE, "digiLAT", g_config.digi_lat, "0.0001", WEB_RANGE_LAT_MIN, WEB_RANGE_LAT_MAX);
    web_field_float(req, TR_F_LONGITUDE, "digiLON", g_config.digi_lon, "0.0001", WEB_RANGE_LON_MIN, WEB_RANGE_LON_MAX);
    web_field_float(req, TR_F_ALTITUDE_M, "digiAlt", g_config.digi_alt, "1", WEB_RANGE_ALT_M_MIN, WEB_RANGE_ALT_M_MAX);
    web_field_int(req, TR_F_BEACON_INTERVAL_S, "digiINV", g_config.digi_interval, WEB_RANGE_INTERVAL_S_MIN, WEB_RANGE_INTERVAL_S_MAX);
    web_field_symbol(req, TR_F_STATION_SYMBOL, "digiSym", g_config.digi_symbol);
    web_field_text(req, TR_F_COMMENT, "digiComment", g_config.digi_comment, COMMENT_SIZE - 1);
    web_fieldset_close(req);

    // DATA EXTENSION ---------------------------------------------------------
    // The digipeater position beacon can carry one of the fixed-station APRS
    // data extensions in the slot after the symbol code (APRS101 ch.7), which
    // introduces PHG as the digipeater's field above all: it is the coverage
    // circle other stations reason about when they choose a path, and mapping
    // clients draw it for digipeaters first. "Enable data extension" gates
    // whether any of them is transmitted, and "Extension type" picks which:
    //
    //   PHG - power/height/gain/directivity, the classic coverage estimate.
    //   RNG - a single pre-calculated radio range in statute miles, for an
    //         operator who already knows their real coverage radius.
    //   DFS - the same height/gain/directivity codes as PHG, but reporting
    //         received signal strength instead of transmitted power, which is
    //         what an omnidirectional direction-finding station transmits.
    //   DF  - the DF report of APRS101 ch.8: the bearing to a signal and the
    //         NRQ triplet that qualifies it. The chapter makes it meaningful
    //         only on the DF symbol (table '/', code '\\'), so it is
    //         transmitted only with that symbol and the note below says so
    //         whenever the two do not agree.
    //
    // Field-for-field the same block the IGate page renders, on this role's
    // own settings: the power/gain/height/direction selectors mirror the
    // "My Station" PHG section on the Station page (same code tables), DFS
    // reuses the gain/height/direction three and adds its own strength code,
    // and RNG uses none of them. "Use My Station Data" reuses the shared
    // Station PHG values and locks the sub-fields. The computed PHG text is
    // read-only and kept in sync by the script below.
    web_fieldset_open(req, TR_F_EXT_SECTION);
    web_field_checkbox(req, TR_F_ENABLE_EXT, "digiPHGEn", g_config.digi_phg_enable);
    web_select_open(req, TR_F_EXT_TYPE, "digiExtType");
    {
        static const char *extNames[] = { TR_EXT_PHG, TR_EXT_RNG, TR_EXT_DFS, TR_EXT_DF };
        for (size_t i = 0; i < sizeof(extNames) / sizeof(extNames[0]); i++)
            web_select_option(req, i, extNames[i], g_config.digi_ext_type == (uint8_t)i);
    }
    web_select_close(req);
    web_field_int(req, TR_F_EXT_RANGE_MI, "digiRng", g_config.digi_range_miles, APRS_EXT_RANGE_MILES_MIN, APRS_EXT_RANGE_MILES_MAX);
    web_field_int(req, TR_F_EXT_DFS_STRENGTH, "digiDfsS", g_config.digi_dfs_strength, APRS_EXT_DFS_STRENGTH_MIN, APRS_EXT_DFS_STRENGTH_MAX);
    web_field_int(req, TR_F_EXT_DF_BEARING, "digiDfBrg", g_config.digi_df_bearing, APRS_EXT_DF_BEARING_MIN, APRS_EXT_DF_BEARING_MAX);
    web_field_int(req, TR_F_EXT_DF_NRQ_N, "digiDfN", g_config.digi_df_nrq_n, APRS_EXT_DF_NRQ_MIN, APRS_EXT_DF_NRQ_MAX);
    web_field_int(req, TR_F_EXT_DF_NRQ_R, "digiDfR", g_config.digi_df_nrq_r, APRS_EXT_DF_NRQ_MIN, APRS_EXT_DF_NRQ_MAX);
    web_field_int(req, TR_F_EXT_DF_NRQ_Q, "digiDfQ", g_config.digi_df_nrq_q, APRS_EXT_DF_NRQ_MIN, APRS_EXT_DF_NRQ_MAX);

    // The DF report only travels with the DF symbol (APRS101 ch.8), so the
    // condition the beacon encoder enforces is stated here instead of leaving
    // it to be discovered off the air. Rendered with the stored symbol and
    // extension type already applied, so it is right without scripting, and
    // kept in step by apply() below as the operator edits either one.
    {
        bool dfSelected = g_config.digi_phg_enable && g_config.digi_ext_type == (uint8_t)APRS_EXT_DF;
        bool dfSymbol = aprs_df_symbol_matches(g_config.digi_symbol[0], g_config.digi_symbol[1]);
        char buf[420];
        snprintf(buf, sizeof(buf), "<p id='digiDfSymNote' style='color:var(--sub);font-size:12px;margin:4px 0 0%s'>%s</p>",
                 (dfSelected && !dfSymbol) ? "" : ";display:none", TR_NOTE_EXT_DF_SYMBOL);
        web_raw(req, buf);
    }
    web_field_checkbox(req, TR_USE_MY_STATION_DATA, "digiPHGUseStation", g_config.digi_phg_use_station);
    web_select_open(req, TR_F_RADIO_TX_POWER, "digiPHGPower");
    {
        // APRS PHG power code table (P digit 0-9), rounded to these fixed
        // Watt values only - not a free-edit field. Same table as Station.
        static const int watts[] = { 0, 1, 5, 10, 15, 25, 35, 50, 65, 80 };
        for (size_t i = 0; i < sizeof(watts) / sizeof(watts[0]); i++) {
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%d", watts[i]);
            web_select_option(req, watts[i], lbl, g_config.digi_phg_power == (uint16_t)watts[i]);
        }
    }
    web_select_close(req);
    web_select_open(req, TR_F_ANTENNA_GAIN, "digiPHGGain");
    {
        // APRS PHG gain code table (G digit 0-9), in dB - not a free-edit field.
        for (int i = 0; i <= 9; i++) {
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%d", i);
            web_select_option(req, i, lbl, (int)lroundf(g_config.digi_phg_gain) == i);
        }
    }
    web_select_close(req);
    web_select_open(req, TR_F_HEIGHT_M, "digiPHGHeight");
    {
        // APRS PHG height code table (H digit), 10*2^n feet, extended beyond
        // the standard 0-9 digits to also allow the requested larger values.
        static const int feet[] = { 10, 20, 40, 80, 160, 320, 640, 1280, 2560, 5120, 10240, 20480, 40960, 81920 };
        for (size_t i = 0; i < sizeof(feet) / sizeof(feet[0]); i++) {
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%d", (int)lroundf(feet[i] * 0.3048f));
            // Option value stays in feet (the APRS code table's own unit);
            // only the label shown to the user is converted to meters.
            web_select_option(req, feet[i], lbl, g_config.digi_phg_height == (uint16_t)feet[i]);
        }
    }
    web_select_close(req);
    web_select_open(req, TR_F_ANTENNA_DIRECTION, "digiPHGDir");
    {
        static const char *dirs[] = { TR_DIR_OMNI, TR_DIR_N, TR_DIR_NE, TR_DIR_E, TR_DIR_SE, TR_DIR_S, TR_DIR_SW, TR_DIR_W, TR_DIR_NW };
        for (int i = 0; i < 9; i++)
            web_select_option(req, i, dirs[i], g_config.digi_phg_dir == (uint8_t)i);
    }
    web_select_close(req);

    // Computed PHG value --------------------------------------------------
    // Read-only and display-only: it carries no name attribute, so it is never
    // submitted and never stored. The script below fills it on load and on
    // every change of the four PHG sub-fields, which are the values the beacon
    // encoder reads. Same convention as the Station and IGate pages.
    {
        char buf[550];
        snprintf(buf, sizeof(buf), "<label>%s</label><input type='text' id='digiPHG' maxlength='7' readonly>", TR_F_PHG_TEXT);
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

    // PHG behaviour, same convention as the IGate page and the Objects page's
    // per-element PHG blocks:
    //   * "Enable data extension" off -> the four PHG sub-fields are disabled
    //     (and nothing is transmitted in the slot - enforced server-side too,
    //     see beacon.c).
    //   * "Use My Station Data" on -> the sub-fields are filled from the
    //     shared station PHG and disabled (locked), and the computed text is
    //     recalculated from those mirrored values.
    //   * otherwise the sub-fields are editable and the computed PHG text is
    //     recalculated live from them (same formula as the Station page).
    // Disabled controls don't POST, so the save handler snapshots the
    // station PHG when "Use My Station Data" is on and keeps the stored
    // own-values when the extension is disabled - see page_digi_post().
    web_raw(req, "<script>(function(){"
                 "var ST=window.__stnPHG||{p:0,g:0,h:10,d:0};"
                 "function q(n){return document.querySelector(\"[name='\"+n+\"']\");}"
                 "function calc(){"
                 "var p=parseInt(q('digiPHGPower').value)||0,g=parseInt(q('digiPHGGain').value)||0,"
                 "h=parseInt(q('digiPHGHeight').value)||10,d=parseInt(q('digiPHGDir').value)||0;"
                 "var P=Math.min(9,Math.max(0,Math.round(Math.sqrt(p))));"
                 "var H=Math.min(13,Math.max(0,Math.round(Math.log(h/10)/Math.log(2))));"
                 "var G=Math.min(9,Math.max(0,g)),D=Math.min(8,Math.max(0,d));"
                 "var o=document.getElementById('digiPHG');"
                 "if(o)o.value='PHG'+P+String.fromCharCode(48+H)+G+D;"
                 "}"
                 "function apply(){"
                 "var en=q('digiPHGEn'),us=q('digiPHGUseStation'),ty=q('digiExtType');"
                 "if(!en)return;"
                 "var on=en.checked,useS=us&&us.checked,t=ty?parseInt(ty.value):0;"
                 "if(useS){q('digiPHGPower').value=ST.p;q('digiPHGGain').value=ST.g;q('digiPHGHeight').value=ST.h;q('digiPHGDir').value=ST.d;}"
                 "if(ty)ty.disabled=!on;"
                 // PHG uses all four sub-fields, DFS every one but the transmit
                 // power, RNG and DF none of them; the range, strength and bearing
                 // /NRQ inputs each belong to exactly one type. Disabling rather
                 // than hiding keeps the layout stable and, since a disabled
                 // control does not POST, matches what the save handler below
                 // actually stores.
                 "var dis=(!on)||useS;"
                 "q('digiPHGPower').disabled=dis||t!==0;"
                 "['digiPHGGain','digiPHGHeight','digiPHGDir'].forEach(function(nm){var el=q(nm);if(el)el.disabled=dis||t===1||t===3;});"
                 "var r=q('digiRng');if(r)r.disabled=(!on)||t!==1;"
                 "var sg=q('digiDfsS');if(sg)sg.disabled=(!on)||t!==2;"
                 "['digiDfBrg','digiDfN','digiDfR','digiDfQ'].forEach(function(nm){var el=q(nm);if(el)el.disabled=(!on)||t!==3;});"
                 // The DF report needs the DF symbol pair to be readable as one,
                 // so the note appears exactly when the selected type is DF and
                 // the symbol edited above is anything else - the same test the
                 // beacon encoder makes before it emits the token.
                 "var nt=document.getElementById('digiDfSymNote');"
                 "if(nt){var st=q('digiSymTable'),sc=q('digiSymCode');"
                 "var isDF=st&&sc&&st.value==='/'&&sc.value==='\\\\';"
                 "nt.style.display=(on&&t===3&&!isDF)?'':'none';}"
                 "calc();"
                 "}"
                 "document.addEventListener('DOMContentLoaded',function(){"
                 "var en=q('digiPHGEn'),us=q('digiPHGUseStation'),ty=q('digiExtType');"
                 "if(en)en.addEventListener('change',apply);"
                 "if(us)us.addEventListener('change',apply);"
                 "if(ty)ty.addEventListener('change',apply);"
                 "['digiPHGPower','digiPHGGain','digiPHGHeight','digiPHGDir'].forEach(function(nm){var el=q(nm);if(el)el.addEventListener('change',calc);});"
                 // The symbol lives in another fieldset of this same form, and the
                 // DF note depends on it, so editing it re-evaluates the note.
                 "['digiSymTable','digiSymCode'].forEach(function(nm){var el=q(nm);if(el)el.addEventListener('input',apply);});"
                 "apply();"
                 "});"
                 "})();</script>");

    web_fieldset_open(req, TR_F_STATUS_BEACON);
    web_field_int(req, TR_F_STATUS_INTERVAL_S_0_OFF, "digiSTSIntv", g_config.digi_sts_interval, WEB_RANGE_INTERVAL_S_MIN, WEB_RANGE_INTERVAL_S_MAX);
    web_field_text(req, TR_F_STATUS_TEXT, "digiStatus", g_config.digi_status, STATUS_SIZE - 1);
    web_fieldset_close(req);

    // REPEATER RADIO PARAMETERS ----------------------------------------------
    // Recommended travelers' voice repeater this digipeater advertises
    // (freqspec.txt), built with objitem_build_freq_block() and prepended to
    // both the position beacon comment and the status report. A frequency of
    // 0 emits no frequency block at all - the tone/duplex/offset sub-fields
    // are then unused.
    web_fieldset_open(req, TR_F_OBJITEM_REPEATER_SECTION);
    web_field_float(req, TR_F_OBJITEM_FREQ, "digiFreq", g_config.digi_freq_mhz, "0.001", 0.0f, 999.999f);
    render_duplex_select(req, "digiDup", g_config.digi_duplex);
    web_field_int(req, TR_F_OBJITEM_OFFSET, "digiOfs", (long)g_config.digi_offset_khz, 0, 65535);
    web_field_float(req, TR_F_OBJITEM_TONE, "digiTone", g_config.digi_tone_tenths / 10.0f, "0.1", 0.0f, 254.1f);
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
    // Sized for the whole page in one POST: the main settings and the beacon
    // fieldsets, the data-extension block, the repeater radio parameters
    // block, the four path presets, and the alias table's four rows of
    // {alias, hop limit, mode} plus its four policy controls.
    char body[3000];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    app_config_lock();
    g_config.digi_en = web_form_get_bool(body, "digiEn");
    g_config.digi_use_station = web_form_get_bool(body, "digiUseStation");
    g_config.digi_use_gps = web_form_get_bool(body, "digiUseGps");
    g_config.digi_timestamp = web_form_get_bool(body, "digiTime");

    if (g_config.digi_use_gps) {
        web_form_get_call(body, "digiMycall", g_config.digi_mycall, sizeof(g_config.digi_mycall));
        gps_data_t g;
        if (gps_snapshot(&g)) {
            if (g.has_position) {
                g_config.digi_lat = (float)g.latitude;
                g_config.digi_lon = (float)g.longitude;
            }
            if (g.has_altitude)
                g_config.digi_alt = (float)g.altitude_m;
        }
    } else if (g_config.digi_use_station) {
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
    g_config.digi_path = app_config_path_mask_clamp(web_form_get_path_mask(body, "digiPath"), g_config.path);

    // n-N alias table. Every row is validated here as well as in the browser:
    // the form's own min/max is only advice, and these values decide how this
    // station repeats other people's traffic.
    g_config.digi_fillin_only = web_form_get_bool(body, "digiFillinOnly");
    g_config.digi_trap_n_clamp = web_form_get_int(body, "digiTrapNClamp", g_config.digi_trap_n_clamp ? 1 : 0) != 0;
    for (int i = 0; i < DIGI_ALIAS_MAX; i++) {
        char name[20];

        snprintf(name, sizeof(name), "digiAlias%d", i);
        web_form_get_call(body, name, g_config.digi_alias[i].alias, sizeof(g_config.digi_alias[i].alias));

        snprintf(name, sizeof(name), "digiAliasN%d", i);
        int maxN = web_form_get_int(body, name, g_config.digi_alias[i].max_n);
        if (maxN < 1)
            maxN = 1;
        else if (maxN > DIGI_ALIAS_MAX_N)
            maxN = DIGI_ALIAS_MAX_N;
        g_config.digi_alias[i].max_n = (uint8_t)maxN;

        snprintf(name, sizeof(name), "digiAliasM%d", i);
        int mode = web_form_get_int(body, name, g_config.digi_alias[i].mode);
        g_config.digi_alias[i].mode = (mode == DIGI_ALIAS_TRACE || mode == DIGI_ALIAS_FLOOD) ? (uint8_t)mode : (uint8_t)DIGI_ALIAS_OFF;
    }
    int preempt = web_form_get_int(body, "digiPreempt", g_config.digi_preempt);
    g_config.digi_preempt = (preempt == DIGI_PREEMPT_DROP || preempt == DIGI_PREEMPT_MARK) ? (uint8_t)preempt : (uint8_t)DIGI_PREEMPT_OFF;
    g_config.digi_dest_ssid_en = web_form_get_bool(body, "digiDestSsidEn");

    g_config.digi_bcn = web_form_get_bool(body, "digiBcn");
    g_config.digi_loc2rf = web_form_get_bool(body, "digiPos2rf");
    g_config.digi_loc2inet = web_form_get_bool(body, "digiPos2inet");
    g_config.digi_compress = web_form_get_bool(body, "digiCompress");
    g_config.digi_interval = (uint16_t)web_form_get_int(body, "digiINV", g_config.digi_interval);

    // Station Symbol: Table + Symbol 1-char fields from the shared picker
    // widget, falling back to a legacy combined 2-char field if present.
    web_form_get_symbol(body, "digiSym", "digiSymbol", g_config.digi_symbol, sizeof(g_config.digi_symbol));

    // Data extension: same convention as the IGate page. "Use My Station
    // Data" locks (disables) the sub-fields in the browser, so they don't
    // POST - snapshot the shared station PHG under the config lock (already
    // held here) and use that instead of the form in that case.
    g_config.digi_phg_enable = web_form_get_bool(body, "digiPHGEn");
    // The type select and the type-specific numbers are disabled in the
    // browser for whichever extension is not selected, so they do not POST;
    // web_form_get_int() therefore keeps the stored value for those, which is
    // what lets an operator switch types back and forth without losing the
    // settings of the other one. All of them are clamped here as well as in
    // config_from_json(), the two-layer pattern the rest of the pages use.
    {
        int extType = web_form_get_int(body, "digiExtType", (int)g_config.digi_ext_type);
        if (extType < APRS_EXT_PHG || extType > APRS_EXT_DF)
            extType = APRS_EXT_PHG;
        g_config.digi_ext_type = (uint8_t)extType;

        int rng = web_form_get_int(body, "digiRng", (int)g_config.digi_range_miles);
        if (rng < APRS_EXT_RANGE_MILES_MIN)
            rng = APRS_EXT_RANGE_MILES_MIN;
        if (rng > APRS_EXT_RANGE_MILES_MAX)
            rng = APRS_EXT_RANGE_MILES_MAX;
        g_config.digi_range_miles = (uint16_t)rng;

        int dfs = web_form_get_int(body, "digiDfsS", (int)g_config.digi_dfs_strength);
        if (dfs < APRS_EXT_DFS_STRENGTH_MIN)
            dfs = APRS_EXT_DFS_STRENGTH_MIN;
        if (dfs > APRS_EXT_DFS_STRENGTH_MAX)
            dfs = APRS_EXT_DFS_STRENGTH_MAX;
        g_config.digi_dfs_strength = (uint8_t)dfs;

        // The bearing wraps rather than clamps: 360 and 0 degrees name the
        // same direction and the on-air field is three digits wide, so an
        // out-of-range value still has one correct reading.
        int brg = web_form_get_int(body, "digiDfBrg", (int)g_config.digi_df_bearing) % 360;
        if (brg < 0)
            brg += 360;
        g_config.digi_df_bearing = (uint16_t)brg;

        static const char *nrqKeys[] = { "digiDfN", "digiDfR", "digiDfQ" };
        uint8_t *nrqFields[] = { &g_config.digi_df_nrq_n, &g_config.digi_df_nrq_r, &g_config.digi_df_nrq_q };
        for (size_t i = 0; i < sizeof(nrqKeys) / sizeof(nrqKeys[0]); i++) {
            int digit = web_form_get_int(body, nrqKeys[i], (int)*nrqFields[i]);
            if (digit < APRS_EXT_DF_NRQ_MIN)
                digit = APRS_EXT_DF_NRQ_MIN;
            if (digit > APRS_EXT_DF_NRQ_MAX)
                digit = APRS_EXT_DF_NRQ_MAX;
            *nrqFields[i] = (uint8_t)digit;
        }
    }
    g_config.digi_phg_use_station = web_form_get_bool(body, "digiPHGUseStation");
    if (g_config.digi_phg_use_station) {
        g_config.digi_phg_power = g_config.my_phg_power;
        g_config.digi_phg_gain = g_config.my_phg_gain;
        g_config.digi_phg_height = g_config.my_phg_height;
        g_config.digi_phg_dir = g_config.my_phg_dir;
    } else {
        g_config.digi_phg_power = (uint16_t)web_form_get_int(body, "digiPHGPower", g_config.digi_phg_power);
        g_config.digi_phg_gain = (float)web_form_get_int(body, "digiPHGGain", (int)lroundf(g_config.digi_phg_gain));
        g_config.digi_phg_height = (uint16_t)web_form_get_int(body, "digiPHGHeight", g_config.digi_phg_height);
        g_config.digi_phg_dir = (uint8_t)web_form_get_int(body, "digiPHGDir", g_config.digi_phg_dir);
    }

    // web_form_get() clamps to a plain byte count, so an operator-typed
    // multi-byte UTF-8 character sitting right at that boundary could arrive
    // already split; stage it and re-cut with str_copy_utf8_safe() so the
    // stored comment - repeated on the air on every future beacon - never
    // carries an incomplete character even in that edge case. On a request
    // with no digiComment field, the staging buffer starts as a copy of the
    // current value, so an absent field leaves it unchanged, matching
    // web_form_get()'s own leave-untouched-when-absent behaviour.
    char digiCommentStage[sizeof(g_config.digi_comment)];
    memcpy(digiCommentStage, g_config.digi_comment, sizeof(digiCommentStage));
    web_form_get(body, "digiComment", digiCommentStage, sizeof(digiCommentStage));
    str_copy_utf8_safe(digiCommentStage, g_config.digi_comment, sizeof(g_config.digi_comment));

    g_config.digi_sts_interval = (uint16_t)web_form_get_int(body, "digiSTSIntv", g_config.digi_sts_interval);
    char digiStatusStage[sizeof(g_config.digi_status)];
    memcpy(digiStatusStage, g_config.digi_status, sizeof(digiStatusStage));
    web_form_get(body, "digiStatus", digiStatusStage, sizeof(digiStatusStage));
    str_copy_utf8_safe(digiStatusStage, g_config.digi_status, sizeof(g_config.digi_status));

    // Repeater radio parameters: same two-layer clamp as every other bounded
    // field on this page.
    {
        float freq = web_form_get_float(body, "digiFreq", g_config.digi_freq_mhz);
        g_config.digi_freq_mhz = freq < 0 ? 0 : freq;

        int dup = web_form_get_int(body, "digiDup", g_config.digi_duplex > 0 ? 1 : (g_config.digi_duplex < 0 ? 2 : 0));
        g_config.digi_duplex = (int8_t)(dup == 1 ? 1 : (dup == 2 ? -1 : 0));

        int ofs = web_form_get_int(body, "digiOfs", (int)g_config.digi_offset_khz);
        if (ofs < 0)
            ofs = 0;
        if (ofs > 65535)
            ofs = 65535;
        g_config.digi_offset_khz = (uint16_t)ofs;

        float tone_hz = web_form_get_float(body, "digiTone", g_config.digi_tone_tenths / 10.0f);
        if (tone_hz < 0)
            tone_hz = 0;
        int tone_tenths = (int)(tone_hz * 10.0f + 0.5f);
        if (tone_tenths > 65535)
            tone_tenths = 65535;
        g_config.digi_tone_tenths = (uint16_t)tone_tenths;
    }

    web_form_get(body, "path0", g_config.path[0], sizeof(g_config.path[0]));
    web_form_get(body, "path1", g_config.path[1], sizeof(g_config.path[1]));
    web_form_get(body, "path2", g_config.path[2], sizeof(g_config.path[2]));
    web_form_get(body, "path3", g_config.path[3], sizeof(g_config.path[3]));

    app_config_unlock();

    // The page rendered next is built from the live settings, so the save
    // result is what decides whether the operator is told this reached flash.
    bool ok = app_config_save();
    if (!ok)
        ESP_LOGE(TAG, "digipeater settings could not be written to flash");
    web_send_save_result(req, ok, "/digi");
    return ESP_OK;
}
