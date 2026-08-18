// @file page_query.c
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
// @brief Web admin "Query" page: renders and saves the APRS query responder
// configuration in g_config - which of the two sources a query may arrive on
// is answered (RF, APRS-IS), the three general queries ("?APRS?", "?WX?",
// "?IGATE?"), whether directed ("CALL:?query?") queries are answered at all,
// and whether the extended directed set (?APRSD/?APRSH/?APRSM/?APRSO/?APRSP/
// ?APRSS/?APRST) is answered along with them. It also carries the optional
// periodic Station Capabilities beacon, which sends the same
// "<IGATE,MSG_CNT=n,LOC_CNT=n>" line the "?IGATE?" answer builds without
// waiting to be asked.
//
// The two source switches select where a question is listened for, not where
// the answer is sent: an answer always goes back on the channel its question
// arrived on. Leaving the APRS-IS source off therefore means backbone traffic
// can never key the transmitter.

#include "app_config.h"
#include "esp_log.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

static const char *TAG = "page_query";

esp_err_t page_query_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_QUERY, "query");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/query'>");

    web_fieldset_open(req, TR_F_QUERY);
    web_field_checkbox(req, TR_F_ENABLE_QUERY, "queryEn", g_config.query_en);
    web_field_checkbox(req, TR_F_QUERY_RF, "queryRf", g_config.query_rf);
    web_field_checkbox(req, TR_F_QUERY_INET, "queryInet", g_config.query_inet);
    web_field_checkbox(req, TR_F_QUERY_APRS, "queryAprsEn", g_config.query_aprs_en);
#ifdef ENABLE_WEATHER
    web_field_checkbox(req, TR_F_QUERY_WX, "queryWxEn", g_config.query_wx_en);
#endif
#ifdef ENABLE_IGATE
    web_field_checkbox(req, TR_F_QUERY_IGATE, "queryIgateEn", g_config.query_igate_en);
#endif
    web_field_checkbox(req, TR_F_QUERY_DIRECTED, "queryDirectedEn", g_config.query_directed_en);
    web_field_checkbox(req, TR_F_QUERY_EXT, "queryExtEn", g_config.query_ext_en);
    web_field_int(req, TR_F_QUERY_MIN_INTERVAL, "queryMinInterval", g_config.query_min_interval_sec, 5, WEB_RANGE_INTERVAL_S_MAX);
    web_fieldset_close(req);

    // PERIODIC STATION CAPABILITIES BEACON -----------------------------------
    // Separate from the three general-query switches above because it is a
    // transmitter rather than a responder: it keys the radio on a timer of its
    // own, so it gets its own channel selection instead of inheriting the
    // source switches, which say where a question is listened for. Left off,
    // the line is still sent in reply to "?IGATE?".
    web_fieldset_open(req, TR_F_QUERY_CAP_SECTION);
    web_field_checkbox(req, TR_F_QUERY_CAP_ENABLE, "queryCapEn", g_config.query_cap_beacon_en);
    web_field_int(req, TR_F_QUERY_CAP_INTERVAL, "queryCapIntv", (long)g_config.query_cap_interval_sec, QUERY_CAP_INTERVAL_S_MIN, QUERY_CAP_INTERVAL_S_MAX);
    web_field_checkbox(req, TR_F_BEACON_VIA_RF, "queryCapRf", g_config.query_cap_rf);
    web_field_checkbox(req, TR_F_BEACON_VIA_INTERNET, "queryCapInet", g_config.query_cap_inet);
    web_field_text(req, TR_F_QUERY_CAP_EXTRA, "queryCapExtra", g_config.query_cap_extra, sizeof(g_config.query_cap_extra) - 1);
    web_fieldset_close(req);

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_query_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    // Wide enough for every control this page renders, including the
    // percent-encoded capability-token text field.
    char body[1024];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    app_config_lock();
    g_config.query_en = web_form_get_bool(body, "queryEn");
    g_config.query_rf = web_form_get_bool(body, "queryRf");
    g_config.query_inet = web_form_get_bool(body, "queryInet");
    g_config.query_aprs_en = web_form_get_bool(body, "queryAprsEn");
    // These two mirror the feature gates the GET renders them under. A form
    // only carries the controls that were drawn, and an absent checkbox reads
    // as false, so a build without the feature would otherwise clear the
    // stored value on every save of this page - discarding a setting the
    // operator never saw and cannot restore until the feature is compiled
    // back in.
#ifdef ENABLE_WEATHER
    g_config.query_wx_en = web_form_get_bool(body, "queryWxEn");
#endif
#ifdef ENABLE_IGATE
    g_config.query_igate_en = web_form_get_bool(body, "queryIgateEn");
#endif
    g_config.query_directed_en = web_form_get_bool(body, "queryDirectedEn");
    g_config.query_ext_en = web_form_get_bool(body, "queryExtEn");
    g_config.query_min_interval_sec = (uint16_t)web_form_get_int(body, "queryMinInterval", g_config.query_min_interval_sec);
    if (g_config.query_min_interval_sec < 5) // floor: airtime/loop safety
        g_config.query_min_interval_sec = 5;

    // Capabilities beacon. The interval is clamped here and again in
    // config_from_json(), the two-layer pattern the rest of the pages use, and
    // the free-form tokens go through the shared sanitizer so a ',' or '>'
    // typed into the box cannot invent a token or close the line early.
    g_config.query_cap_beacon_en = web_form_get_bool(body, "queryCapEn");
    {
        int iv = web_form_get_int(body, "queryCapIntv", (int)g_config.query_cap_interval_sec);
        if (iv < QUERY_CAP_INTERVAL_S_MIN)
            iv = QUERY_CAP_INTERVAL_S_MIN;
        if (iv > QUERY_CAP_INTERVAL_S_MAX)
            iv = QUERY_CAP_INTERVAL_S_MAX;
        g_config.query_cap_interval_sec = (uint32_t)iv;
    }
    g_config.query_cap_rf = web_form_get_bool(body, "queryCapRf");
    g_config.query_cap_inet = web_form_get_bool(body, "queryCapInet");
    web_form_get(body, "queryCapExtra", g_config.query_cap_extra, sizeof(g_config.query_cap_extra));
    app_config_query_cap_extra_sanitize(g_config.query_cap_extra);
    app_config_unlock();

    // The page rendered next is built from the live settings, so the save
    // result is what decides whether the operator is told this reached flash.
    bool ok = app_config_save();
    if (!ok)
        ESP_LOGE(TAG, "query settings could not be written to flash");
    web_send_save_result(req, ok, "/query");
    return ESP_OK;
}
