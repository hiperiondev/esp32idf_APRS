// @file page_tracker.c
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
// @brief Web admin "Tracker" page: renders and saves the tracker configuration
// (callsign/SSID, fixed position, symbol, comment, path and beacon settings) in
// g_config. The fixed position can be typed in, mirrored from "My Station"
// ("Use My Station Data") or taken live from the GNSS receiver ("Use GPS",
// see web_field_use_gps_data() in web_common.c); the three sources are
// mutually exclusive. A separate "Use live GPS fix" switch (trk_use_live_gps)
// leaves the fixed fields alone and instead has beacon.c read the GNSS
// receiver at every beacon transmission, carrying live course and speed
// alongside position - see trackerBeaconService() in main/beacon.c.

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "gps.h"
#include "pages.h"
#include "str_append.h" // str_copy_utf8_safe()
#include "translations.h"
#include "web_common.h"

static const char *TAG = "page_tracker";

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

// Mic-E position comment <select> (APRS101 ch.10 "Mic-E Message Types").
// Values are the packed 0-13 form app_config.h documents: the seven Standard
// comments in order, then the seven locally defined Custom ones. Emergency
// is absent on purpose - it asks other operators to react to a real
// emergency, so it is not something a dropdown should be able to arm for
// every beacon that follows.
static void render_mice_msg_select(httpd_req_t *req, const char *name, uint8_t cur) {
    static const char *const standard[MICE_POS_COMMENT_CUSTOM_BASE] = { TR_F_MICE_MSG_M0, TR_F_MICE_MSG_M1, TR_F_MICE_MSG_M2, TR_F_MICE_MSG_M3,
                                                                        TR_F_MICE_MSG_M4, TR_F_MICE_MSG_M5, TR_F_MICE_MSG_M6 };

    web_select_open(req, TR_F_MICE_POSITION_COMMENT, name);
    for (int i = 0; i < MICE_POS_COMMENT_CUSTOM_BASE; i++)
        web_select_option(req, i, standard[i], cur == i);

    for (int i = MICE_POS_COMMENT_CUSTOM_BASE; i <= MICE_POS_COMMENT_MAX; i++) {
        char label[40];
        snprintf(label, sizeof(label), "C%d %s %d", i - MICE_POS_COMMENT_CUSTOM_BASE, TR_F_MICE_MSG_CUSTOM, i - MICE_POS_COMMENT_CUSTOM_BASE);
        web_select_option(req, i, label, cur == i);
    }
    web_select_close(req);
}

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
    // Fixed beacon period, in seconds: the interval the tracker beacon is armed
    // with in beacon.c, bounded there by sched_clamp_interval().
    web_field_int(req, TR_F_FIXED_INTERVAL_S, "trkINV", g_config.trk_interval, WEB_RANGE_INTERVAL_S_MIN, WEB_RANGE_INTERVAL_S_MAX);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_STATION);
    web_field_text(req, TR_F_MY_CALLSIGN, "trkMycall", g_config.trk_mycall, 9);
    web_field_int(req, TR_F_SSID, "trkSSID", g_config.trk_ssid, WEB_RANGE_SSID_MIN, WEB_RANGE_SSID_MAX);
    web_field_path_checkboxes(req, "trkPath", g_config.trk_path);
    web_field_use_gps_data(req, "trkUseGps", g_config.trk_use_gps, "trkUseStation", "trkLAT", "trkLON", "trkALT", NULL, NULL);
    web_field_float(req, TR_F_FIXED_LATITUDE, "trkLAT", g_config.trk_lat, "0.0001", WEB_RANGE_LAT_MIN, WEB_RANGE_LAT_MAX);
    web_field_float(req, TR_F_FIXED_LONGITUDE, "trkLON", g_config.trk_lon, "0.0001", WEB_RANGE_LON_MIN, WEB_RANGE_LON_MAX);
    web_field_float(req, TR_F_FIXED_ALTITUDE_M, "trkALT", g_config.trk_alt, "0.1", WEB_RANGE_ALT_M_MIN, WEB_RANGE_ALT_M_MAX);
    // Unlike "Use GPS" above, this does not touch the fixed fields at all: it
    // leaves them as the fallback and instead has beacon.c re-read the GNSS
    // receiver at every tracker beacon transmission (trackerBeaconService(),
    // main/beacon.c), carrying live course and speed alongside position. The
    // fixed fields stay in effect whenever the receiver is off or has no
    // current fix.
    web_field_checkbox(req, TR_F_TRACKER_USE_LIVE_GPS, "trkUseLiveGps", g_config.trk_use_live_gps);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_OPTIONS);
    web_field_checkbox(req, TR_F_COMPRESS_POSITION, "trkCompress", g_config.trk_compress);
    web_field_checkbox(req, TR_F_MICE_POSITION, "trkMice", g_config.trk_mice);
    render_mice_msg_select(req, "trkMiceMsg", g_config.trk_mice_msg);
    web_field_checkbox(req, TR_F_INCLUDE_ALTITUDE, "trkOptAlt", g_config.trk_altitude);
    // The tracker's data extension is PHG and nothing else, so this is one
    // checkbox rather than a type selector: its four sub-fields are the
    // station's own antenna data, edited once on the Station page. Enabling it
    // makes the beacon fall back to the uncompressed layout, which is the only
    // one with a slot for the token - except in Mic-E, which carries it in the
    // text field.
    web_field_checkbox(req, TR_F_TRACKER_PHG, "trkPHG", g_config.trk_phg_enable);
    web_field_symbol(req, TR_F_STATION_SYMBOL, "trkSymbol", g_config.trk_symbol);
    web_field_text(req, TR_F_COMMENT, "trkComment", g_config.trk_comment, COMMENT_SIZE - 1);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_STATUS_BEACON);
    web_field_int(req, TR_F_STATUS_INTERVAL_S_0_OFF, "trkSTSIntv", g_config.trk_sts_interval, WEB_RANGE_INTERVAL_S_MIN, WEB_RANGE_INTERVAL_S_MAX);
    web_field_text(req, TR_F_STATUS_TEXT, "trkStatus", g_config.trk_status, STATUS_SIZE - 1);
    web_fieldset_close(req);

    // REPEATER RADIO PARAMETERS ----------------------------------------------
    // Recommended travelers' voice repeater this station advertises
    // (freqspec.txt), built with objitem_build_freq_block() and prepended to
    // both the position beacon comment and the status report. A frequency of
    // 0 emits no frequency block at all - the tone/duplex/offset sub-fields
    // are then unused.
    web_fieldset_open(req, TR_F_OBJITEM_REPEATER_SECTION);
    web_field_float(req, TR_F_OBJITEM_FREQ, "trkFreq", g_config.trk_freq_mhz, "0.001", 0.0f, 999.999f);
    render_duplex_select(req, "trkDup", g_config.trk_duplex);
    web_field_int(req, TR_F_OBJITEM_OFFSET, "trkOfs", (long)g_config.trk_offset_khz, 0, 65535);
    web_field_float(req, TR_F_OBJITEM_TONE, "trkTone", g_config.trk_tone_tenths / 10.0f, "0.1", 0.0f, 254.1f);
    web_fieldset_close(req);

    web_raw(req, "<p style='color:var(--sub);font-size:12px'>" TR_NOTE_TLM_TRACKER "</p>"
                 "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_tracker_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    // Sized for the whole page in one POST: the main settings, station,
    // options and status beacon fieldsets, plus the repeater radio
    // parameters block.
    char body[1900];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    app_config_lock();
    g_config.trk_en = web_form_get_bool(body, "trkEn");
    g_config.trk_use_station = web_form_get_bool(body, "trkUseStation");
    g_config.trk_use_gps = web_form_get_bool(body, "trkUseGps");
    g_config.trk_use_live_gps = web_form_get_bool(body, "trkUseLiveGps");
    g_config.trk_loc2rf = web_form_get_bool(body, "trkPos2rf");
    g_config.trk_loc2inet = web_form_get_bool(body, "trkPos2inet");
    g_config.trk_timestamp = web_form_get_bool(body, "trkTime");

    if (g_config.trk_use_gps) {
        web_form_get_call(body, "trkMycall", g_config.trk_mycall, sizeof(g_config.trk_mycall));
        gps_data_t g;
        if (gps_snapshot(&g)) {
            if (g.has_position) {
                g_config.trk_lat = (float)g.latitude;
                g_config.trk_lon = (float)g.longitude;
            }
            if (g.has_altitude)
                g_config.trk_alt = (float)g.altitude_m;
        }
    } else if (g_config.trk_use_station) {
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
    g_config.trk_path = app_config_path_mask_clamp(web_form_get_path_mask(body, "trkPath"), g_config.path);

    g_config.trk_interval = (uint16_t)web_form_get_int(body, "trkINV", g_config.trk_interval);

    g_config.trk_compress = web_form_get_bool(body, "trkCompress");
    g_config.trk_phg_enable = web_form_get_bool(body, "trkPHG");
    g_config.trk_mice = web_form_get_bool(body, "trkMice");

    // Same two-layer clamp as every other bounded field on this page: a value
    // outside the selectable range - including the Emergency the <select>
    // does not offer - falls back to the factory default.
    int miceMsg = web_form_get_int(body, "trkMiceMsg", g_config.trk_mice_msg);
    g_config.trk_mice_msg = (uint8_t)((miceMsg < 0 || miceMsg > MICE_POS_COMMENT_MAX) ? MICE_POS_COMMENT_DEFAULT : miceMsg);
    g_config.trk_altitude = web_form_get_bool(body, "trkOptAlt");

    // Station Symbol: Table + Symbol 1-char fields from the shared picker
    // widget, falling back to a combined 2-char field if present.
    web_form_get_symbol(body, "trkSymbol", "trkSymbol", g_config.trk_symbol, sizeof(g_config.trk_symbol));

    // web_form_get() clamps to a plain byte count, so an operator-typed
    // multi-byte UTF-8 character sitting right at that boundary could arrive
    // already split; stage it and re-cut with str_copy_utf8_safe() so the
    // stored comment - repeated on the air on every future beacon - never
    // carries an incomplete character even in that edge case. On a request
    // with no trkComment field, the staging buffer starts as a copy of the
    // current value, so an absent field leaves it unchanged, matching
    // web_form_get()'s own leave-untouched-when-absent behaviour.
    char trkCommentStage[sizeof(g_config.trk_comment)];
    memcpy(trkCommentStage, g_config.trk_comment, sizeof(trkCommentStage));
    web_form_get(body, "trkComment", trkCommentStage, sizeof(trkCommentStage));
    str_copy_utf8_safe(trkCommentStage, g_config.trk_comment, sizeof(g_config.trk_comment));

    g_config.trk_sts_interval = (uint16_t)web_form_get_int(body, "trkSTSIntv", g_config.trk_sts_interval);
    char trkStatusStage[sizeof(g_config.trk_status)];
    memcpy(trkStatusStage, g_config.trk_status, sizeof(trkStatusStage));
    web_form_get(body, "trkStatus", trkStatusStage, sizeof(trkStatusStage));
    str_copy_utf8_safe(trkStatusStage, g_config.trk_status, sizeof(g_config.trk_status));

    // Repeater radio parameters: same two-layer clamp as every other bounded
    // field on this page.
    {
        float freq = web_form_get_float(body, "trkFreq", g_config.trk_freq_mhz);
        g_config.trk_freq_mhz = freq < 0 ? 0 : freq;

        int dup = web_form_get_int(body, "trkDup", g_config.trk_duplex > 0 ? 1 : (g_config.trk_duplex < 0 ? 2 : 0));
        g_config.trk_duplex = (int8_t)(dup == 1 ? 1 : (dup == 2 ? -1 : 0));

        int ofs = web_form_get_int(body, "trkOfs", (int)g_config.trk_offset_khz);
        if (ofs < 0)
            ofs = 0;
        if (ofs > 65535)
            ofs = 65535;
        g_config.trk_offset_khz = (uint16_t)ofs;

        float tone_hz = web_form_get_float(body, "trkTone", g_config.trk_tone_tenths / 10.0f);
        if (tone_hz < 0)
            tone_hz = 0;
        int tone_tenths = (int)(tone_hz * 10.0f + 0.5f);
        if (tone_tenths > 65535)
            tone_tenths = 65535;
        g_config.trk_tone_tenths = (uint16_t)tone_tenths;
    }

    app_config_unlock();

    // The page rendered next is built from the live settings, so the save
    // result is what decides whether the operator is told this reached flash.
    bool ok = app_config_save();
    if (!ok)
        ESP_LOGE(TAG, "tracker settings could not be written to flash");
    web_send_save_result(req, ok, "/tracker");
    return ESP_OK;
}
