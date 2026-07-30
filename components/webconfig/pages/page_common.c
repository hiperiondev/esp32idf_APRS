/**
 * @file page_common.c
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
 * @brief Web admin foundation pages and live JSON endpoints: root redirect,
 * logout, the dashboard, the sidebar/system info strips, and the lastheard and
 * traffic feeds polled by the dashboard.
 */

#include <stdio.h>
#include <string.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>

#include "app_config.h"
#include "aprs_service.h"
#include "cpu_freq.h"
#include "digirepeater.h"
#include "igate.h"
#include "lastheard.h"
#include "pages.h"
#include "storage.h"
#include "trafficlog.h"
#include "translations.h"
#include "web_common.h"

esp_err_t page_root(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/dashboard");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t page_logout(httpd_req_t *req) {
    // Force the browser to drop cached Basic-Auth creds by re-issuing 401.
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP32APRS\"");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, "<h1>" TR_LOGGED_OUT_TITLE "</h1><a href='/'>" TR_LOG_IN_AGAIN "</a>");
    return ESP_OK;
}

esp_err_t page_dashboard(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, NULL, "dashboard");
    httpd_resp_sendstr_chunk(req, "<h1>" TR_F_DASHBOARD "</h1>");

    // -- Compact live system-info strip. Polled every 1s so all System Info
    //    values stay live. --
    httpd_resp_sendstr_chunk(req, "<div id='dashSysInfo'></div>");

    // -- "Modes Enabled" / "Network Status" / "STATISTICS" panel. Polled
    //    every 1s so all STATISTICS values stay live. --
    httpd_resp_sendstr_chunk(req, "<div id='sidebarInfo'></div>");

    char buf[900];

    // -- Radio Info -----------------------------------------------------
    size_t n = 0;
    n += snprintf(buf + n, sizeof(buf) - n, "<fieldset><legend>" TR_DASH_RADIO_INFO "</legend><table>");
    // MODEM status reflects the audio ADC/DAC AFSK modem enable state set on
    // the Radiomodem (Audio / AFSK) page - it is the only modem in the build.
    const char *modemName = g_config.audio_modem_en ? "AFSK (Audio)" : TR_F_OFF;
    n += snprintf(buf + n, sizeof(buf) - n,
                  "<tr><td>" TR_DASH_MODEM "</td><td>%s</td></tr>"
                  "<tr><td>" TR_DASH_FX25 "</td><td>%s</td></tr></table></fieldset>",
                  modemName, g_config.fx25_mode ? TR_ENABLED : TR_F_OFF);
    httpd_resp_sendstr_chunk(req, buf);

    // -- APRS-IS SERVER ---------------------------------------------------
    if (g_config.igate_en) {
        n = snprintf(buf, sizeof(buf),
                     "<fieldset><legend>" TR_DASH_APRS_IS_SERVER "</legend><table>"
                     "<tr><td>" TR_DASH_HOST "</td><td>%s</td></tr>"
                     "<tr><td>" TR_DASH_PORT "</td><td>%d</td></tr></table></fieldset>",
                     g_config.aprs_host, g_config.aprs_port);
        httpd_resp_sendstr_chunk(req, buf);
    }

    // -- WiFi --------------------------------------------------------------
    static const char *WIFI_MODE_NAME[] = { TR_F_OFF, "STA", "AP", "AP+STA" };
    const char *wifiModeName = (g_config.wifi_mode < 4) ? WIFI_MODE_NAME[g_config.wifi_mode] : TR_F_OFF;

    wifi_ap_record_t ap_info;
    bool sta_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    char ssidBuf[40] = "", ssidEsc[40 * 6 + 1] = "";
    if (sta_connected) {
        snprintf(ssidBuf, sizeof(ssidBuf), "%s", (const char *)ap_info.ssid);
        web_html_attr_escape(ssidBuf, ssidEsc, sizeof(ssidEsc));
    }

    n = snprintf(buf, sizeof(buf),
                 "<fieldset><legend>" TR_DASH_WIFI "</legend><table>"
                 "<tr><td>" TR_DASH_MODE "</td><td>%s</td></tr>"
                 "<tr><td>" TR_DASH_SSID "</td><td>%s</td></tr>",
                 wifiModeName, ssidEsc);
    if (sta_connected)
        n += snprintf(buf + n, sizeof(buf) - n, "<tr><td>" TR_DASH_RSSI "</td><td>%d dBm</td></tr></table></fieldset>", ap_info.rssi);
    else
        n += snprintf(buf + n, sizeof(buf) - n, "<tr><td>" TR_DASH_RSSI "</td><td>" TR_DASH_DISCONNECTED "</td></tr></table></fieldset>");
    httpd_resp_sendstr_chunk(req, buf);

    // -- IGate Traffic table: a real (not modal) table at the bottom of the
    //    dashboard, polled from /igate_traffic?since=<seq> and appended to,
    //    mirroring the reference esp32idf_APRS dashboard's traffic monitor
    //    with its TIME / TYPE / DX / PACKET / AUDIO columns. AUDIO shows the
    //    demodulated signal level (mV RMS) for RF-received frames, or '-'
    //    for TX/APRS-IS-only entries where no audio level applies. --
    httpd_resp_sendstr_chunk(req, "<fieldset><legend>" TR_DASH_IGATE_TRAFFIC "</legend>"
                                  "<div class='traffic-actions'>"
                                  "<button id='trafficPauseBtn' class='btn secondary' onclick='trafficTogglePause()'>" TR_TRAFFIC_PAUSE "</button>"
                                  "<button class='btn secondary' onclick='trafficClear()'>" TR_TRAFFIC_CLEAR "</button>"
                                  "</div>"
                                  "<div id='trafficTableWrap' class='traffic-table-wrap'>"
                                  "<table id='trafficTable'><thead><tr>"
                                  "<th>" TR_TRAFFIC_COL_TIME "</th>"
                                  "<th>" TR_TRAFFIC_COL_TYPE "</th>"
                                  "<th>" TR_DASH_LH_ICON "</th>"
                                  "<th>" TR_TRAFFIC_COL_DX "</th>"
                                  "<th>" TR_TRAFFIC_COL_PACKET "</th>"
                                  "<th>" TR_TRAFFIC_COL_AUDIO "</th>"
                                  "</tr></thead><tbody id='trafficBody'>"
                                  "<tr><td colspan='6'>" TR_TRAFFIC_WAITING "</td></tr>"
                                  "</tbody></table></div></fieldset>");

    httpd_resp_sendstr_chunk(req,
        "<script>"
        "var trafficSince=0,trafficPaused=false,trafficRows=[];"
        "var TRAFFIC_MAX_ROWS=200;"
        "function esc(s){return (s==null?'':String(s)).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}"
        "function trafficTogglePause(){"
        "trafficPaused=!trafficPaused;"
        "document.getElementById('trafficPauseBtn').textContent=trafficPaused?'" TR_TRAFFIC_RESUME "':'" TR_TRAFFIC_PAUSE "';"
        "}"
        "function trafficClear(){trafficRows=[];renderTraffic();}"
        "function fmtIcon(sym){"
        "if(!sym)return '-';"
        "return '<img src=\"http://aprs.dprns.com/symbols/icons/'+sym+'.png\" width=16 height=16 onerror=\"this.style.display=\\'none\\'\">';"
        "}"
        "function renderTraffic(){"
        "var body=document.getElementById('trafficBody');"
        "if(!trafficRows.length){body.innerHTML='<tr><td colspan=\"6\">" TR_TRAFFIC_WAITING "</td></tr>';return;}"
        "var rows='';"
        "for(var i=trafficRows.length-1;i>=0;i--){"
        "var it=trafficRows[i];"
        "var au=(it.au!=null&&it.au>=0)?(it.au+' mV'):'-';"
        "rows+='<tr><td>'+(it.t/1000).toFixed(1)+'s</td><td>'+esc(it.d)+'</td><td>'+fmtIcon(it.sym)+'</td><td>'+esc(it.dx)+'</td><td>'+esc(it.pkt||it.m)+'</td><td>'+esc(au)+'</td></tr>';"
        "}"
        "body.innerHTML=rows;"
        "}"
        "function trafficPoll(){"
        "if(trafficPaused)return;"
        "fetch('/igate_traffic?since='+trafficSince).then(function(r){return r.json();}).then(function(d){"
        "trafficSince=d.seq;"
        "if(d.items&&d.items.length){"
        "trafficRows=trafficRows.concat(d.items);"
        "if(trafficRows.length>TRAFFIC_MAX_ROWS)trafficRows=trafficRows.slice(trafficRows.length-TRAFFIC_MAX_ROWS);"
        "renderTraffic();"
        "}"
        "}).catch(function(){}).then(function(){setTimeout(trafficPoll,1500);});"
        "}"
        // -- Reference-dashboard-style periodic reloads. System Info and
        //    STATISTICS (sidebarInfo, which also holds Modes Enabled /
        //    Network Status) are both refreshed every 1s so every value in
        //    those two panels stays live, matching the same 1s cadence used
        //    for Free Heap / Min Free Heap below. --
        "function reloadDashSysInfo(){"
        "fetch('/dashinfo').then(function(r){return r.text();}).then(function(t){"
        "document.getElementById('dashSysInfo').innerHTML=t;"
        "}).catch(function(){}).then(function(){setTimeout(reloadDashSysInfo,1000);});"
        "}"
        "function reloadSidebarInfo(){"
        "fetch('/sidebarInfo').then(function(r){return r.text();}).then(function(t){"
        "document.getElementById('sidebarInfo').innerHTML=t;"
        "}).catch(function(){}).then(function(){setTimeout(reloadSidebarInfo,1000);});"
        "}"
        "reloadDashSysInfo();reloadSidebarInfo();trafficPoll();"
        "</script>");

    web_send_footer(req);
    return ESP_OK;
}

// Translates esp_reset_reason() into a short human-readable label for the
// dashboard's System Info strip.
static const char *dash_reboot_reason_str(void) {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:
            return "Power-on";
        case ESP_RST_EXT:
            return "External pin";
        case ESP_RST_SW:
            return "Software reset";
        case ESP_RST_PANIC:
            return "Panic/exception";
        case ESP_RST_INT_WDT:
            return "Interrupt watchdog";
        case ESP_RST_TASK_WDT:
            return "Task watchdog";
        case ESP_RST_WDT:
            return "Other watchdog";
        case ESP_RST_DEEPSLEEP:
            return "Deep sleep wake";
        case ESP_RST_BROWNOUT:
            return "Brownout";
        case ESP_RST_SDIO:
            return "SDIO";
        default:
            return "Unknown";
    }
}

// GET /dashinfo -> compact live system-info strip shown at the top of the
// dashboard, mirroring the reference dashboard's AJAX-refreshed #sysInfo bar
// (Up Time / RAM / LittleFS / CPU speed / Reboot reason). Polled every 1s
// from the dashboard's reloadDashSysInfo() so all values stay live.
esp_err_t page_dashinfo(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    size_t used = 0, total = 0;
    storage_usage(&used, &total);
    uint32_t cpu_mhz = esp_rom_get_cpu_ticks_per_us();

    // Break the raw uptime seconds down into days / hours / minutes / seconds
    // for display (e.g. "2d 3h 43m 7s") instead of a single raw seconds count.
    int64_t uptime_s = esp_timer_get_time() / 1000000LL;
    int64_t uptime_days = uptime_s / 86400;
    int64_t uptime_hour = (uptime_s % 86400) / 3600;
    int64_t uptime_min = (uptime_s % 3600) / 60;
    int64_t uptime_sec = uptime_s % 60;

    char buf[800];
    snprintf(buf, sizeof(buf),
             "<fieldset><legend>" TR_DASH_SYSINFO "</legend><table><tr>"
             "<th>" TR_DASH_UPTIME "</th><th>" TR_DASH_FREE_HEAP "</th><th>" TR_SYSINFO_MIN_FREE_HEAP "</th><th>" TR_DASH_LITTLEFS "</th><th>" TR_SYSINFO_CPU_FREQ
             "</th><th>" TR_DASH_REBOOT_REASON "</th>"
             "</tr><tr>"
             "<td>%lldd %lldh %lldm %llds</td><td><span id='dashFreeHeap'>%lu</span> bytes</td><td><span id='dashMinFreeHeap'>%lu</span> bytes</td>"
             "<td>%u / %u bytes</td><td>%lu MHz</td><td>%s</td>"
             "</tr></table></fieldset>",
             uptime_days, uptime_hour, uptime_min, uptime_sec, (unsigned long)esp_get_free_heap_size(), (unsigned long)esp_get_minimum_free_heap_size(), (unsigned)used,
             (unsigned)total, (unsigned long)cpu_mhz, dash_reboot_reason_str());

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

// GET /heapinfo -> tiny JSON {free, minFree} used to refresh just the Free
// Heap / Min Free Heap cells on the dashboard every second, without
// re-rendering the whole (slower-changing) #dashSysInfo fieldset.
esp_err_t page_heapinfo(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    char json[80];
    size_t n = snprintf(json, sizeof(json), "{\"free\":%lu,\"minFree\":%lu}", (unsigned long)esp_get_free_heap_size(),
                         (unsigned long)esp_get_minimum_free_heap_size());

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, json, (ssize_t)n);
    return ESP_OK;
}

// GET /lastheard -> JSON array of recently-heard stations (see
// components/lastheard). Not rendered on the dashboard (the IGate Traffic
// table below covers that), kept available for other UI / future use.
esp_err_t page_lastheard(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    char json[2048];
    size_t n = lastheard_dump_json(json, sizeof(json));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, json, (ssize_t)n);
    return ESP_OK;
}

esp_err_t page_sidebar_info(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    igate_stats_t igs = igate_get_stats();

    // Sized generously: worst case is every counter at its uint32 max
    // (10 digits) across Modes/Network/Statistics plus a full 22-row Drop
    // Breakdown table (the longest reason strings run ~40-45 chars each).
    char buf[3600];
    size_t n = 0;

    // -- Modes Enabled ------------------------------------------------------
    // Wrapped in the same <fieldset><legend> card used by Radio Info /
    // APRS-IS SERVER / WiFi so all dashboard boxes share one look and feel.
    n += snprintf(buf + n, sizeof(buf) - n,
                  "<fieldset><legend>" TR_DASH_MODES_ENABLED "</legend><table><tr>"
                  "<th style='background:%s'>" TR_F_IGATE "</th>"
                  "<th style='background:%s'>" TR_DASH_DIGI_SHORT "</th>"
                  "<th style='background:%s'>" TR_F_TRACKER "</th>"
                  "<th style='background:%s'>" TR_DASH_WX_SHORT "</th>"
                  "</tr></table></fieldset>",
                  g_config.igate_en ? "#0b0" : "#606060", g_config.digi_en ? "#0b0" : "#606060", g_config.trk_en ? "#0b0" : "#606060",
                  g_config.wx_en ? "#0b0" : "#606060");

    // -- Network Status -------------------------------------------------------
    // WIFI reflects the STA link state (connected to an AP), the same check
    // used by the WiFi fieldset above, so this column tracks the actual
    // radio link rather than just whether STA/AP+STA mode is configured.
    wifi_ap_record_t sidebar_ap_info;
    bool wifi_connected = (esp_wifi_sta_get_ap_info(&sidebar_ap_info) == ESP_OK);
    n += snprintf(buf + n, sizeof(buf) - n,
                  "<fieldset><legend>" TR_DASH_NETWORK_STATUS "</legend><table><tr>"
                  "<th style='background:%s'>" TR_DASH_WIFI "</th>"
                  "<th style='background:%s'>APRS-IS</th>"
                  "<th style='background:%s'>" TR_DASH_FX25 "</th>"
                  "</tr></table></fieldset>",
                  wifi_connected ? "#0b0" : "#606060",
                  igate_is_connected() ? "#0b0" : "#606060", (g_config.fx25_mode > 0) ? "#0b0" : "#606060");

    // -- STATISTICS -----------------------------------------------------
    // radio_rx/radio_tx/rf2inet/inet2rf/digi come from aprs_service's own
    // counters, tracked at the actual RX/TX/relay points regardless of
    // whether digi_en/igate_en are on - unlike digi_get_stats()/
    // igate_get_stats(), whose internal counters only move while their
    // owning feature is enabled. This keeps the panel populated even for an
    // RX-only/monitor setup with both features off. (digi remains an
    // exception by nature: there is nothing to digipeat with digi_en off, so
    // it's expected to read 0 in that case.)
    //
    // Drop/error counts: every drop/error site in the firmware - IGate
    // RF->INET/INET->RF, the digipeater, and the RX/TX service level in
    // aprs_service.c - reports through igate_note_drop() into
    // igs.dropByReason[], regardless of whether digi_en/igate_en are on, so
    // the RX-only/monitor setup is covered at that single point.
    // svcStats.drop/err and digis.dropRx/erPkts are the *same* events counted
    // a second time by their owning component's own counter, so they must not
    // be added on top of igate_stats_total_drop(&igs) - that would
    // double-count every digi and service-level drop and push the DROP/ERR
    // total ahead of the Drop Breakdown table's sum. igate_stats_total_drop(&igs)
    // alone is the complete, correct total.
    aprs_service_stats_t svcStats = aprs_service_get_stats();
    n += snprintf(buf + n, sizeof(buf) - n,
                  "<fieldset><legend>" TR_DASH_STATISTICS "</legend><table>"
                  "<tr><td>" TR_DASH_RADIO_RX "</td><td>%lu</td></tr>"
                  "<tr><td>" TR_DASH_PACKET_TX "</td><td>%lu</td></tr>"
                  "<tr><td>" TR_DASH_RF2INET "</td><td>%lu</td></tr>"
                  "<tr><td>" TR_DASH_INET2RF "</td><td>%lu</td></tr>"
                  "<tr><td>" TR_DASH_IGATE_RX "</td><td>%lu</td></tr>"
                  "<tr><td>" TR_DASH_IGATE_TX "</td><td>%lu</td></tr>"
                  "<tr><td>" TR_DASH_DIGI_STAT "</td><td>%lu</td></tr>"
                  "<tr><td>" TR_DASH_DROP_ERR "</td><td>%lu/%lu</td></tr>"
                  // Current RF TX ring backlog vs the "TX buffers" cap, so an
                  // operator can see beacons queueing up (and, read together
                  // with DROP above, being lost when the leg saturates) without
                  // a serial cable - the visible counterpart to the drain-wait
                  // that now staggers simultaneously-due beacons.
                  "<tr><td>" TR_DASH_TX_QUEUE "</td><td>%lu/%lu</td></tr>"
                  "</table></fieldset>",
                  (unsigned long)svcStats.radio_rx, (unsigned long)svcStats.radio_tx, (unsigned long)svcStats.rf2inet, (unsigned long)svcStats.inet2rf,
                  (unsigned long)igs.isRxCount, (unsigned long)igs.isTxCount, (unsigned long)svcStats.digi,
                  (unsigned long)igate_stats_total_drop(&igs), (unsigned long)igate_stats_total_err(&igs),
                  (unsigned long)svcStats.tx_queue_depth, (unsigned long)svcStats.tx_queue_limit);

    // -- Drop breakdown -------------------------------------------------
    // Per-reason detail behind the aggregate DROP/ERR tile above: lets an
    // operator tell "N dropped because Weather was unchecked" apart from
    // "N dropped because RFONLY", instead of one opaque total. Every drop/
    // error site in the firmware (IGate RF->INET/INET->RF, the digipeater,
    // and the RX/TX service level) reports through igate_note_drop() with
    // its own drop_reason_t, so every row here is an explicit, named reason
    // - there is no generic/"other" catch-all bucket.
    n += snprintf(buf + n, sizeof(buf) - n, "<fieldset><legend>" TR_DASH_DROP_BREAKDOWN "</legend><table>");
    for (int i = 0; i < DROP_REASON_COUNT; i++) {
        n += snprintf(buf + n, sizeof(buf) - n, "<tr><td>%s</td><td>%lu</td></tr>", igate_drop_reason_name((drop_reason_t)i),
                      (unsigned long)igs.dropByReason[i]);
    }
    n += snprintf(buf + n, sizeof(buf) - n, "</table></fieldset>");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}



// GET /igate_traffic?since=<seq> -> JSON feed of igate/digi/RF traffic lines,
// polled by the "IGate Traffic" box on the dashboard. Mirrors the same lines
// the firmware already prints on the serial console (see trafficlog.h).
esp_err_t page_igate_traffic(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    uint32_t since = 0;
    char query[32];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[16];
        if (httpd_query_key_value(query, "since", val, sizeof(val)) == ESP_OK) {
            since = (uint32_t)strtoul(val, NULL, 10);
        }
    }

    const size_t json_size = 6144;
    char *json = malloc(json_size);
    if (!json) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    size_t n = trafficlog_dump_json(since, json, json_size);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, json, (ssize_t)n);
    free(json);
    return ESP_OK;
}
