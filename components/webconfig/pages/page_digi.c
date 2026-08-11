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
// preemptive and legacy routing policies, and the beacon settings) in
// g_config.

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "pages.h"
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
    // fieldsets, the repeater radio parameters block, the four path presets,
    // and the alias table's four rows of {alias, hop limit, mode} plus its
    // four policy controls.
    char body[2500];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    app_config_lock();
    g_config.digi_en = web_form_get_bool(body, "digiEn");
    g_config.digi_use_station = web_form_get_bool(body, "digiUseStation");
    g_config.digi_timestamp = web_form_get_bool(body, "digiTime");

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

    web_form_get(body, "digiComment", g_config.digi_comment, sizeof(g_config.digi_comment));

    g_config.digi_sts_interval = (uint16_t)web_form_get_int(body, "digiSTSIntv", g_config.digi_sts_interval);
    web_form_get(body, "digiStatus", g_config.digi_status, sizeof(g_config.digi_status));

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
