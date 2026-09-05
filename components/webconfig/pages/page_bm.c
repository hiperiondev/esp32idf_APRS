// @file page_bm.c
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
// @brief Web admin "BrandMeister" page: the interconnect's own enable switch,
// the worldwide monitor subscription, message routing for stations reachable
// through the network, and the optional gateway callsign list.
//
// The interconnect carries no DMR of any kind. BrandMeister's APRS side is an
// APRS-IS client - each master runs a gateway process that logs into a public
// APRS-IS server and injects DMR-sourced traffic as ordinary TNC2 lines - so
// everything this page configures rides the IGate's existing APRS-IS session.
// See main/include/aprs_bm.h for what "BrandMeister traffic" means here.

#include <stdio.h>

#include "app_config.h"
#include "aprs_bm.h"
#include "esp_log.h"
#include "lastheard.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

static const char *TAG = "page_bm";

// True when the worldwide monitor subscription cannot be granted: this station
// gates the APRS-IS feed to RF and no INET->RF range gate stands between the
// feed and the transmitter. A pure function of inet2rf and inet2rf_range_en,
// so the page derives it on every GET instead of carrying a flag from a POST
// to the request that follows it. The web server serves several sockets at
// once, and a flag stored between two requests reaches whichever browser asks
// next rather than the one that saved.
//
// bm_monitor is the third term of the rendered result rather than of the
// condition: the warning explains why the switch above it will not stay
// ticked, so it is shown whenever the precondition is unmet - whether the
// operator has just been refused, is about to be, or loaded a config.json in
// which the monitor was set by hand.
static bool bm_monitor_blocked(void) {
    return g_config.inet2rf && !g_config.inet2rf_range_en;
}

// Emits one label/value row of the read-only status table.
static void bm_row(httpd_req_t *req, const char *label, const char *value, bool good) {
    char esc[240];
    web_html_attr_escape(value, esc, sizeof(esc));
    char row[400];
    snprintf(row, sizeof(row), "<tr><td>%s</td><td style='color:%s'>%s</td></tr>", label, good ? "var(--sub)" : "var(--red)", esc);
    httpd_resp_sendstr_chunk(req, row);
}

esp_err_t page_bm_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_BM, "bm");

    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/bm'>");

    // SERVICE -------------------------------------------------------------
    web_fieldset_open(req, TR_BM_FS_SERVICE);
    web_field_checkbox(req, TR_BM_ENABLE, "bmEn", g_config.bm_en);
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_BM_NOTE_SERVICE "</p>");
    web_fieldset_close(req);

    // MONITOR -------------------------------------------------------------
    // The subscription itself is a term in the IGate page's filter field, not
    // a setting here: the filter string belongs to the operator, and a page
    // that rewrote it behind their back would make the IGate page misreport
    // what was actually sent to the server. This switch records the intent,
    // enforces the precondition, and tells them the exact term to add.
    web_fieldset_open(req, TR_BM_FS_MONITOR);
    web_field_checkbox(req, TR_BM_MONITOR, "bmMonitor", g_config.bm_monitor);
    if (bm_monitor_blocked())
        web_raw(req, "<div style='color:var(--red);font-size:.85em;margin:-6px 0 8px'>" TR_BM_WARN_NEEDS_RANGE "</div>");
    web_raw(req, "<p style='color:var(--red);font-size:12px;margin:4px 0'>" TR_BM_NOTE_MONITOR "</p>");
    {
        char term[200];
        snprintf(term, sizeof(term), "<p style='color:var(--sub);font-size:12px;margin:4px 0'>%s <code>%s</code></p>", TR_BM_NOTE_FILTER_TERM,
                 APRS_BM_MONITOR_FILTER_TERM);
        web_raw(req, term);
    }
    web_fieldset_close(req);

    // MESSAGING -----------------------------------------------------------
    web_fieldset_open(req, TR_BM_FS_MESSAGING);
    web_field_checkbox(req, TR_BM_MSG_INET_ONLY, "bmMsgInetOnly", g_config.bm_msg_inet_only);
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_BM_NOTE_MSG_INET_ONLY "</p>");
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_BM_NOTE_DELIVERY "</p>");
    web_fieldset_close(req);

    // GATEWAYS ------------------------------------------------------------
    web_fieldset_open(req, TR_BM_FS_GATEWAYS);
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_BM_NOTE_GATEWAYS "</p>");
    for (int i = 0; i < APRS_BM_GATEWAYS_MAX; i++) {
        char label[40], name[16];
        snprintf(label, sizeof(label), "%s %d", TR_BM_GATEWAY, i + 1);
        snprintf(name, sizeof(name), "bmGw%d", i);
        web_field_text(req, label, name, g_config.bm_gateways[i], (int)(sizeof(g_config.bm_gateways[i]) - 1));
    }
    web_fieldset_close(req);

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");

    // STATUS --------------------------------------------------------------
    // Read-only, outside the form: these are conclusions about the current
    // configuration, not settings, and three of the four are owned by the
    // IGate page. Rendered server-side on each GET rather than polled - none
    // of it changes without a save somewhere.
    web_fieldset_open(req, TR_BM_FS_STATUS);
    httpd_resp_sendstr_chunk(req, "<table>");
    bm_row(req, TR_BM_ST_SERVICE, g_config.bm_en ? TR_BM_ST_ON : TR_BM_ST_OFF, g_config.bm_en);
    {
        bool present = aprs_bm_filter_has_monitor_term(g_config.aprs_filter);
        bm_row(req, TR_BM_ST_FILTER_TERM, present ? TR_BM_ST_PRESENT : TR_BM_ST_ABSENT, present || !g_config.bm_monitor);
    }
    {
        char kmbuf[48];
        const char *txt;
        bool good;
        if (!g_config.inet2rf) {
            // Nothing from the feed reaches the transmitter at all, so the
            // gate has nothing to govern and its state says nothing useful.
            txt = TR_BM_ST_GATE_NA;
            good = true;
        } else if (!g_config.inet2rf_range_en || g_config.inet2rf_range_km <= 0.0f) {
            txt = TR_BM_ST_GATE_OFF;
            good = !g_config.bm_monitor;
        } else {
            snprintf(kmbuf, sizeof(kmbuf), "%.1f km", (double)g_config.inet2rf_range_km);
            txt = kmbuf;
            good = true;
        }
        bm_row(req, TR_BM_ST_RANGE_GATE, txt, good);
    }
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "%u", (unsigned)lastheard_bm_count());
        bm_row(req, TR_BM_ST_STATIONS, buf, true);
    }
    httpd_resp_sendstr_chunk(req, "</table>");
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_BM_NOTE_STATUS "</p>");
    web_fieldset_close(req);

    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_bm_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    char body[512];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    app_config_lock();

    g_config.bm_en = web_form_get_bool(body, "bmEn");
    g_config.bm_msg_inet_only = web_form_get_bool(body, "bmMsgInetOnly");
    // Read with web_form_get() rather than web_form_get_call(): that helper
    // cuts at six characters, which is right for a base call but wrong here -
    // a BrandMeister master is named with its SSID ("PI1DMR-10") and an entry
    // may end in '*' to match by prefix. The classifier compares without
    // regard to case, so the stored spelling is the operator's.
    for (int i = 0; i < APRS_BM_GATEWAYS_MAX; i++) {
        char name[16];
        snprintf(name, sizeof(name), "bmGw%d", i);
        web_form_get(body, name, g_config.bm_gateways[i], sizeof(g_config.bm_gateways[i]));
    }

    // The interlock. Asking for the worldwide subscription while this station
    // gates the APRS-IS feed to RF, with no local distance gate in the way, is
    // refused outright rather than accepted and quietly corrected somewhere
    // else.
    //
    // The reason it cannot be solved with a smarter server filter: APRS-IS
    // filter terms are OR'd, never AND'd, so "u/APBM* r/lat/lon/150" asks the
    // server for BrandMeister traffic worldwide OR anything within 150 km. The
    // intersection is not expressible to the server at all, which leaves the
    // INET->RF range gate on the IGate page as the only thing standing between
    // a worldwide feed and the transmitter.
    //
    // The refusal needs nothing carried over to the redirected GET: that
    // handler re-derives the same condition from the configuration this one has
    // just written, so the explanation appears under the switch on its own.
    bool wantMonitor = web_form_get_bool(body, "bmMonitor");
    if (wantMonitor && bm_monitor_blocked()) {
        wantMonitor = false;
        ESP_LOGW(TAG, "BrandMeister monitor refused: INET->RF gating is on and the INET->RF range gate is off");
    }
    g_config.bm_monitor = wantMonitor;

    app_config_unlock();

    bool ok = app_config_save();
    if (!ok)
        ESP_LOGE(TAG, "BrandMeister settings could not be written to flash");
    web_send_save_result(req, ok, "/bm");
    return ESP_OK;
}
