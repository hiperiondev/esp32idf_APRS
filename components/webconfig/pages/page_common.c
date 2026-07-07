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

esp_err_t page_not_yet(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_COMING_SOON_TITLE, NULL);
    httpd_resp_sendstr_chunk(req, "<p>" TR_COMING_SOON_BODY "</p>"
                                  "<a class='btn' href='/dashboard'>" TR_BTN_BACK_DASH "</a>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_dashboard(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, NULL, "dashboard");
    httpd_resp_sendstr_chunk(req, "<h1>" TR_F_DASHBOARD " <a class='btn' href='/sysinfo'>" TR_DASH_FULL_SYSINFO "</a></h1>");

    // -- Compact live system-info strip. Polled every 60s, same cadence as
    //    the reference dashboard's reloadSysInfo(). --
    httpd_resp_sendstr_chunk(req, "<div id='dashSysInfo'></div>");

    // -- "Modes Enabled" / "Network Status" / "STATISTICS" panel. Polled
    //    every 10s, same cadence as the reference's reloadSidebarInfo(). --
    httpd_resp_sendstr_chunk(req, "<div id='sidebarInfo'></div>");

    char buf[900];

    // -- Radio Info -----------------------------------------------------
    size_t n = 0;
    n += snprintf(buf + n, sizeof(buf) - n, "<fieldset><legend>" TR_DASH_RADIO_INFO "</legend><table>");
    if (g_config.rf_en) {
        n += snprintf(buf + n, sizeof(buf) - n,
                      "<tr><td>" TR_DASH_FREQ_TX "</td><td>%.4f MHz</td></tr>"
                      "<tr><td>" TR_DASH_FREQ_RX "</td><td>%.4f MHz</td></tr>"
                      "<tr><td>" TR_DASH_TX_PWR "</td><td>%s</td></tr>",
                      g_config.freq_tx, g_config.freq_rx, g_config.rf_power ? TR_DASH_TX_PWR_HIGH : TR_DASH_TX_PWR_LOW);
    }
    const char *modemName = TR_F_OFF;
    if (g_config.rf_en) {
        switch (g_config.modem_type) {
            case RF_MODE_LoRa:
                modemName = "LoRa";
                break;
            case RF_MODE_G3RUH:
                modemName = "AFSK/G3RUH";
                break;
            case RF_MODE_GFSK:
                modemName = "GFSK";
                break;
            case RF_MODE_DPRS:
                modemName = "D-PRS";
                break;
            default:
                modemName = TR_F_OFF;
                break;
        }
    } else {
        // No RF module in use: MODEM status reflects the audio ADC/DAC AFSK
        // modem enable state set on the Radio / Modem (Audio / AFSK) page.
        modemName = g_config.audio_modem_en ? "AFSK (Audio)" : TR_F_OFF;
    }
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
    char ssidBuf[40] = "";
    if (sta_connected)
        snprintf(ssidBuf, sizeof(ssidBuf), "%s", (const char *)ap_info.ssid);

    n = snprintf(buf, sizeof(buf),
                 "<fieldset><legend>" TR_DASH_WIFI "</legend><table>"
                 "<tr><td>" TR_DASH_MODE "</td><td>%s</td></tr>"
                 "<tr><td>" TR_DASH_SSID "</td><td>%s</td></tr>",
                 wifiModeName, ssidBuf);
    if (sta_connected)
        n += snprintf(buf + n, sizeof(buf) - n, "<tr><td>" TR_DASH_RSSI "</td><td>%d dBm</td></tr></table></fieldset>", ap_info.rssi);
    else
        n += snprintf(buf + n, sizeof(buf) - n, "<tr><td>" TR_DASH_RSSI "</td><td>" TR_DASH_DISCONNECTED "</td></tr></table></fieldset>");
    httpd_resp_sendstr_chunk(req, buf);

    // -- IGate Traffic table: a real (not modal) table at the bottom of the
    //    dashboard, polled from /igate_traffic?since=<seq> and appended to,
    //    mirroring the reference ESP32APRS_Audio dashboard's traffic monitor
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
        // -- Reference-dashboard-style periodic reloads --
        "function reloadDashSysInfo(){"
        "fetch('/dashinfo').then(function(r){return r.text();}).then(function(t){"
        "document.getElementById('dashSysInfo').innerHTML=t;"
        "}).catch(function(){}).then(function(){setTimeout(reloadDashSysInfo,60000);});"
        "}"
        "function reloadSidebarInfo(){"
        "fetch('/sidebarInfo').then(function(r){return r.text();}).then(function(t){"
        "document.getElementById('sidebarInfo').innerHTML=t;"
        "}).catch(function(){}).then(function(){setTimeout(reloadSidebarInfo,10000);});"
        "}"
        "reloadDashSysInfo();reloadSidebarInfo();trafficPoll();"
        "</script>");

    web_send_footer(req);
    return ESP_OK;
}

// GET /dashinfo -> compact live system-info strip shown at the top of the
// dashboard, mirroring the reference dashboard's AJAX-refreshed #sysInfo bar
// (Up Time / RAM / LittleFS / CPU speed / CPU temperature).
esp_err_t page_dashinfo(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    size_t used = 0, total = 0;
    storage_usage(&used, &total);
    uint32_t cpu_mhz = esp_rom_get_cpu_ticks_per_us();

    char buf[600];
    snprintf(buf, sizeof(buf),
             "<fieldset><legend>" TR_DASH_SYSINFO "</legend><table><tr>"
             "<th>" TR_DASH_UPTIME "</th><th>" TR_DASH_FREE_HEAP "</th><th>" TR_DASH_LITTLEFS "</th><th>" TR_SYSINFO_CPU_FREQ "</th><th>" TR_SYSINFO_CPU_TEMP
             "</th>"
             "</tr><tr>"
             "<td>%lld s</td><td>%lu bytes</td><td>%u / %u bytes</td><td>%lu MHz</td><td>N/A</td>"
             "</tr></table></fieldset>",
             esp_timer_get_time() / 1000000LL, (unsigned long)esp_get_free_heap_size(), (unsigned)used, (unsigned)total, (unsigned long)cpu_mhz);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

// GET /lastheard -> JSON array of recently-heard stations (see
// components/lastheard). Not currently rendered on the dashboard (the
// separate Last Heard table was removed in favor of the IGate Traffic
// table below), kept available for other UI / future use.
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
    digi_stats_t digis = digi_get_stats();

    char buf[1600];
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
    n += snprintf(buf + n, sizeof(buf) - n,
                  "<fieldset><legend>" TR_DASH_NETWORK_STATUS "</legend><table><tr>"
                  "<th style='background:%s'>APRS-IS</th>"
                  "<th style='background:%s'>" TR_DASH_FX25 "</th>"
                  "</tr></table></fieldset>",
                  igate_is_connected() ? "#0b0" : "#606060", (g_config.fx25_mode > 0) ? "#0b0" : "#606060");

    // -- STATISTICS -----------------------------------------------------
    n += snprintf(buf + n, sizeof(buf) - n,
                  "<fieldset><legend>" TR_DASH_STATISTICS "</legend><table>"
                  "<tr><td>" TR_DASH_RADIO_RX "</td><td>%lu</td></tr>"
                  "<tr><td>" TR_DASH_PACKET_RX "</td><td>%lu</td></tr>"
                  "<tr><td>" TR_DASH_PACKET_TX "</td><td>%lu</td></tr>"
                  "<tr><td>" TR_DASH_RF2INET "</td><td>%lu</td></tr>"
                  "<tr><td>" TR_DASH_INET2RF "</td><td>%lu</td></tr>"
                  "<tr><td>" TR_DASH_DIGI_STAT "</td><td>%lu</td></tr>"
                  "<tr><td>" TR_DASH_DROP_ERR "</td><td>%lu/%lu</td></tr>"
                  "</table></fieldset>",
                  (unsigned long)digis.rxPkts, (unsigned long)(igs.rxCount + digis.rxPkts), (unsigned long)igs.txCount, (unsigned long)igs.rxCount,
                  (unsigned long)igs.txCount, (unsigned long)digis.txPkts, (unsigned long)(igs.dropCount + digis.dropRx), (unsigned long)digis.erPkts);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

esp_err_t page_sysinfo(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_SYSTEM_INFORMATION, NULL);

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    uint32_t cpu_mhz = esp_rom_get_cpu_ticks_per_us();
    size_t used = 0, total = 0;
    storage_usage(&used, &total);

    char buf[1400];
    snprintf(buf, sizeof(buf),
             "<fieldset><legend>" TR_SYSINFO_CHIP "</legend>"
             "<p><b>" TR_SYSINFO_MODEL "</b> %d &nbsp; <b>" TR_SYSINFO_CORES "</b> %d &nbsp; <b>" TR_SYSINFO_REVISION "</b> %d</p>"
             "<p><b>" TR_SYSINFO_CPU_FREQ "</b> %lu MHz</p>"
             "<p><b>" TR_SYSINFO_FLASH_SIZE "</b> %lu bytes</p></fieldset>"
             "<fieldset><legend>" TR_SYSINFO_MEMORY "</legend>"
             "<p><b>" TR_DASH_FREE_HEAP "</b> %lu bytes &nbsp; <b>" TR_SYSINFO_MIN_FREE_HEAP "</b> %lu bytes</p></fieldset>"
             "<fieldset><legend>" TR_SYSINFO_LITTLEFS "</legend>"
             "<p><b>" TR_SYSINFO_USED "</b> %u bytes &nbsp; <b>" TR_SYSINFO_TOTAL "</b> %u bytes</p></fieldset>"
             "<fieldset><legend>" TR_SYSINFO_FIRMWARE "</legend>"
             "<p><b>" TR_SYSINFO_IDF_VERSION "</b> %s</p></fieldset>",
             (int)chip.model, (int)chip.cores, (int)chip.revision, (unsigned long)cpu_mhz, (unsigned long)flash_size,
             (unsigned long)esp_get_free_heap_size(), (unsigned long)esp_get_minimum_free_heap_size(), (unsigned)used, (unsigned)total, IDF_VER);
    httpd_resp_sendstr_chunk(req, buf);
    web_send_footer(req);
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
