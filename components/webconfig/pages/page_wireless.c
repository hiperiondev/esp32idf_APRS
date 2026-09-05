// @file page_wireless.c
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
// @brief Web admin "Wireless" page: renders and saves the WiFi configuration
// (mode, TX power, station profiles and access point settings) and serves the
// JSON access point scan.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

static const char *TAG = "page_wireless";

esp_err_t page_wireless_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_WIRELESS, "wireless");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/wireless'>");

    web_fieldset_open(req, TR_WIFI_MODE_LEGEND);
    // The option values are the WIFI_MODE_CFG_OFF .. WIFI_MODE_CFG_APSTA
    // selectors, in that order; the matching constants pick the selected
    // entry below.
    web_select_open(req, TR_F_MODE, "wifiMode");
    web_select_option(req, 0, TR_F_OFF, g_config.wifi_mode == WIFI_MODE_CFG_OFF);
    web_select_option(req, 1, TR_WIFI_STATION, g_config.wifi_mode == WIFI_MODE_CFG_STA);
    web_select_option(req, 2, TR_WIFI_ACCESS_POINT, g_config.wifi_mode == WIFI_MODE_CFG_AP);
    web_select_option(req, 3, TR_WIFI_AP_STA, g_config.wifi_mode == WIFI_MODE_CFG_APSTA);
    web_select_close(req);
    // Same two-layer arrangement as the AP channel below: these bounds only
    // stop the browser from submitting a power the WiFi driver would refuse,
    // and the POST handler clamps the value again.
    web_field_int(req, TR_WIFI_TX_POWER, "wifiPwr", g_config.wifi_power, WIFI_TX_POWER_DBM_MIN, WIFI_TX_POWER_DBM_MAX);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_WIFI_ACCESS_POINT);
    web_field_text(req, TR_WIFI_AP_SSID, "apSsid", g_config.wifi_ap_ssid, 32);
    {
        char buf[600];
        char esc_ap_pass[64 * 6 + 1];
        web_html_attr_escape(g_config.wifi_ap_pass, esc_ap_pass, sizeof(esc_ap_pass));
        snprintf(buf, sizeof(buf),
                 "<label>" TR_WIFI_AP_PASSWORD "</label><input type='password' name='apPass' id='pwd_apPass' value='%s' maxlength='63' minlength='8'>",
                 esc_ap_pass);
        httpd_resp_sendstr_chunk(req, buf);
    }
    httpd_resp_sendstr_chunk(req, "<label class='pwd-show'><input type='checkbox' onclick=\"togglePwd('pwd_apPass',this)\"> " TR_SHOW_PASSWORD "</label>");
    // min/max here only stop the browser from submitting an out of range
    // channel; the value is clamped again in the POST handler, which is what
    // actually protects the stored configuration.
    web_field_int(req, TR_WIFI_AP_CHANNEL, "apCh", g_config.wifi_ap_ch, WIFI_AP_CH_MIN, WIFI_AP_CH_MAX);
    web_fieldset_close(req);

    httpd_resp_sendstr_chunk(req, "<button type='button' class='secondary' id='wifiScanBtn' onclick='wifiScan()'>" TR_BTN_WIFI_SCAN "</button> "
                                  "<span id='wifiScanStatus'></span>");

    // One shared datalist, filled in by wifiScan() below. It only ever offers
    // *suggestions* - see the SSID field's comment.
    httpd_resp_sendstr_chunk(req, "<datalist id='ssidList'></datalist>");

    for (int i = 0; i < WIFI_STA_NUM; i++) {
        web_fieldset_open(req, TR_WIFI_CLIENT_LEGEND);
        {
            char buf[80];
            snprintf(buf, sizeof(buf), "<label><input type='checkbox' name='staEn%d' %s> " TR_F_ENABLE "</label>", i,
                     g_config.wifi_sta[i].enable ? "checked" : "");
            httpd_resp_sendstr_chunk(req, buf);
        }
        // SSID is a free-text input backed by a datalist: the WiFi Scan
        // button fills the datalist with suggestions, but the SSID can
        // simply be typed. This means no scan is required before entering a
        // new network, and a hidden/5 GHz/out-of-range AP that never appears
        // in a scan can still be entered by hand.
        {
            char buf[600];
            char esc_sta_ssid[33 * 6 + 1];
            web_html_attr_escape(g_config.wifi_sta[i].wifi_ssid, esc_sta_ssid, sizeof(esc_sta_ssid));
            snprintf(buf, sizeof(buf),
                     "<label>" TR_F_SSID "</label>"
                     "<input type='text' name='staSsid%d' id='staSsid%d' list='ssidList' value='%s' maxlength='32' autocomplete='off' "
                     "placeholder='" TR_WIFI_SSID_PLACEHOLDER "'>",
                     i, i, esc_sta_ssid);
            httpd_resp_sendstr_chunk(req, buf);
        }
        {
            char buf[600];
            char esc_sta_pass[64 * 6 + 1];
            web_html_attr_escape(g_config.wifi_sta[i].wifi_pass, esc_sta_pass, sizeof(esc_sta_pass));
            snprintf(buf, sizeof(buf),
                     "<label>" TR_F_PASSWORD "</label><input type='password' name='staPass%d' id='pwd_staPass%d' value='%s' maxlength='63' minlength='8'>", i,
                     i, esc_sta_pass);
            httpd_resp_sendstr_chunk(req, buf);
        }
        {
            char buf[120];
            snprintf(buf, sizeof(buf),
                     "<label class='pwd-show'><input type='checkbox' onclick=\"togglePwd('pwd_staPass%d',this)\"> " TR_SHOW_PASSWORD "</label>", i);
            httpd_resp_sendstr_chunk(req, buf);
        }
        web_fieldset_close(req);
    }

    httpd_resp_sendstr_chunk(req, "<script>"
                                  "function wifiScan(){"
                                  "var btn=document.getElementById('wifiScanBtn');"
                                  "var status=document.getElementById('wifiScanStatus');"
                                  "btn.disabled=true;status.textContent=' " TR_WIFI_SCANNING "';"
                                  // POST, not GET: this route reconfigures the radio to scan, so
                                  // it is registered POST-only and goes through the same-origin check.
                                  "fetch('/wifiscan',{method:'POST'}).then(function(r){return r.json();}).then(function(data){"
                                  "btn.disabled=false;"
                                  "if(data.error){status.textContent=' '+data.error;return;}"
                                  "var nets=(data.networks||[]).slice().sort(function(a,b){return b.rssi-a.rssi;});"
                                  "status.textContent=' ('+nets.length+')';"
                                  // Fill the shared datalist only, so scan suggestions never touch
                                  // what is typed in an SSID field.
                                  "var dl=document.getElementById('ssidList');"
                                  "while(dl.firstChild){dl.removeChild(dl.firstChild);}"
                                  "var seen={};"
                                  "for(var j=0;j<nets.length;j++){"
                                  "var ssid=nets[j].ssid;"
                                  "if(!ssid||seen[ssid])continue;"
                                  "seen[ssid]=true;"
                                  "var opt=document.createElement('option');"
                                  "opt.value=ssid;opt.label=ssid+' ('+nets[j].rssi+' dBm)';"
                                  "dl.appendChild(opt);"
                                  "}"
                                  "}).catch(function(){btn.disabled=false;status.textContent=' " TR_WIFI_SCAN_FAILED "';});"
                                  "}"
                                  "</script>");

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_wireless_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char body[3072]; // enlarged: worst-case fully percent-encoded 5x(32+63 char) STA fields + AP fields
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    app_config_lock();
    g_config.wifi_mode = (uint8_t)web_form_get_int(body, "wifiMode", g_config.wifi_mode);
    // Clamped through an int, so a crafted POST carrying a value outside
    // int8_t range is caught before the cast can fold it into a small negative
    // number and overflow the quarter-dBm multiply main.c applies.
    int wifiPwr = web_form_get_int(body, "wifiPwr", g_config.wifi_power);
    if (wifiPwr < WIFI_TX_POWER_DBM_MIN)
        wifiPwr = WIFI_TX_POWER_DBM_MIN;
    else if (wifiPwr > WIFI_TX_POWER_DBM_MAX)
        wifiPwr = WIFI_TX_POWER_DBM_MAX;
    g_config.wifi_power = (int8_t)wifiPwr;
    web_form_get(body, "apSsid", g_config.wifi_ap_ssid, sizeof(g_config.wifi_ap_ssid));
    web_form_get(body, "apPass", g_config.wifi_ap_pass, sizeof(g_config.wifi_ap_pass));
    // The form's min/max attributes are browser side only: a crafted POST can
    // carry any integer. esp_wifi_set_config() refuses an AP channel outside
    // the regulatory range, so anything out of bounds is folded back to the
    // default instead of being written to config.json.
    int apCh = web_form_get_int(body, "apCh", g_config.wifi_ap_ch);
    if (apCh < WIFI_AP_CH_MIN || apCh > WIFI_AP_CH_MAX)
        apCh = WIFI_AP_CH_DEFAULT;
    g_config.wifi_ap_ch = (uint8_t)apCh;

    for (int i = 0; i < WIFI_STA_NUM; i++) {
        char key[16];
        snprintf(key, sizeof(key), "staEn%d", i);
        g_config.wifi_sta[i].enable = web_form_get_bool(body, key);
        snprintf(key, sizeof(key), "staSsid%d", i);
        web_form_get(body, key, g_config.wifi_sta[i].wifi_ssid, sizeof(g_config.wifi_sta[i].wifi_ssid));
        snprintf(key, sizeof(key), "staPass%d", i);
        web_form_get(body, key, g_config.wifi_sta[i].wifi_pass, sizeof(g_config.wifi_sta[i].wifi_pass));
    }

    app_config_unlock();

    // The page rendered next is built from the live settings, so the save
    // result is what decides whether the operator is told this reached flash.
    // Reported before the usability check below, because a write that never
    // reached flash is the more fundamental of the two problems.
    if (!app_config_save()) {
        ESP_LOGE(TAG, "wireless settings could not be written to flash");
        web_send_save_result(req, false, "/wireless");
        return ESP_OK;
    }

    // Tell the user NOW, in the browser, if what they just saved cannot work.
    // Selecting Station or AP+STA in the Mode dropdown does nothing on its own:
    // each WiFi Client block has its own Enable checkbox, and the SSID has to
    // be non-empty. Without this check, getting either wrong would save happily
    // and then fail silently - the only clue an error on the serial console
    // after the next reboot, which nobody watching a web UI ever sees.
    if (g_config.wifi_mode == WIFI_MODE_CFG_STA || g_config.wifi_mode == WIFI_MODE_CFG_APSTA) {
        bool usable = false;
        for (int i = 0; i < WIFI_STA_NUM; i++) {
            if (g_config.wifi_sta[i].enable && g_config.wifi_sta[i].wifi_ssid[0]) {
                usable = true;
                break;
            }
        }
        if (!usable) {
            httpd_resp_set_type(req, "text/html");
            httpd_resp_sendstr(req, "<!DOCTYPE html><html><head><meta charset='utf-8'></head><body>"
                                    "<p style='color:var(--red);font-weight:600'>" TR_WIFI_STA_NEEDS_SSID "</p>"
                                    "<p><a href='/wireless'>&larr; " TR_F_WIRELESS "</a></p>"
                                    "</body></html>");
            return ESP_OK;
        }
    }

    web_send_save_result(req, true, "/wireless");
    return ESP_OK;
}

// ---------------------------------------------------------------- WiFi scan
// POST /wifiscan - triggers a blocking active scan for nearby access points and
// returns the results as JSON: {"networks":[{"ssid":"...","rssi":-55},...]}
// Requires the radio to currently be in STA or AP+STA mode (a scan cannot run
// while the radio is AP-only or powered off).
//
// POST rather than GET, even though it only reports back: the scan
// reconfigures the radio, flipping an AP-only interface to AP+STA for the
// duration and back again (see below), and blocks the httpd task while it
// runs, which makes it a state-changing request. web_check_auth() runs its
// same-origin check on those only, so a GET route here would be reachable
// cross-origin from any page an authenticated admin had open, because the
// browser attaches cached Basic credentials to such a request. POST also puts
// the route out of reach of the other ways a browser fetches a URL on its own
// (script/stylesheet loads, prefetch, link prerender, address-bar
// navigation).
esp_err_t page_wifi_scan_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    httpd_resp_set_type(req, "application/json");

    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK) {
        httpd_resp_sendstr(req, "{\"error\":\"WiFi is off\"}");
        return ESP_OK;
    }

    // A scan needs the STA interface enabled. If the radio is currently
    // AP-only (the common/default case for this firmware), temporarily add
    // the STA interface for the duration of the scan and switch back to the
    // original mode afterwards so the AP configuration is not disturbed.
    //
    // Note this makes the driver post WIFI_EVENT_STA_START, which main.c's
    // handler answers with esp_wifi_connect(). That is gated on s_staEnabled
    // there - only true when the *configured* mode includes a station with a
    // usable SSID - so a scan started from AP-only mode will not have an
    // association attempt racing it.
    bool mode_switched = false;
    if (mode == WIFI_MODE_AP) {
        if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) {
            httpd_resp_sendstr(req, "{\"error\":\"could not enable STA for scan\"}");
            return ESP_OK;
        }
        mode_switched = true;
    } else if (mode == WIFI_MODE_NULL) {
        if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
            httpd_resp_sendstr(req, "{\"error\":\"could not enable STA for scan\"}");
            return ESP_OK;
        }
        mode_switched = true;
    }

    wifi_scan_config_t scan_cfg = { 0 };
    scan_cfg.show_hidden = false;
    esp_err_t scan_err = esp_wifi_scan_start(&scan_cfg, true);

    if (scan_err != ESP_OK) {
        if (mode_switched)
            esp_wifi_set_mode(mode); // restore original mode
        httpd_resp_sendstr(req, "{\"error\":\"scan failed\"}");
        return ESP_OK;
    }

    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    if (num > 32)
        num = 32;

    wifi_ap_record_t *records = NULL;
    if (num > 0) {
        records = calloc(num, sizeof(wifi_ap_record_t));
        if (!records) {
            if (mode_switched)
                esp_wifi_set_mode(mode);
            httpd_resp_sendstr(req, "{\"error\":\"out of memory\"}");
            return ESP_OK;
        }
        if (esp_wifi_scan_get_ap_records(&num, records) != ESP_OK) {
            free(records);
            if (mode_switched)
                esp_wifi_set_mode(mode);
            httpd_resp_sendstr(req, "{\"error\":\"scan read failed\"}");
            return ESP_OK;
        }
    }

    if (mode_switched)
        esp_wifi_set_mode(mode); // back to the configured mode

    httpd_resp_sendstr_chunk(req, "{\"networks\":[");
    for (int i = 0; i < num; i++) {
        // Minimal JSON-escape the SSID (quote/backslash) since SSIDs are
        // attacker/user controlled text arriving over the air.
        char safe[65];
        int sj = 0;
        for (int k = 0; k < 32 && records[i].ssid[k] != 0 && sj < (int)sizeof(safe) - 2; k++) {
            unsigned char c = records[i].ssid[k];
            if (c == '"' || c == '\\')
                safe[sj++] = '\\';
            if (c >= 0x20 && c < 0x7f)
                safe[sj++] = (char)c; // drop non-printables
        }
        safe[sj] = 0;

        char item[96];
        snprintf(item, sizeof(item), "%s{\"ssid\":\"%s\",\"rssi\":%d}", i ? "," : "", safe, records[i].rssi);
        httpd_resp_sendstr_chunk(req, item);
    }
    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, NULL);

    if (records)
        free(records);
    return ESP_OK;
}
