/**
 * @file page_wireless.c
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
 * @brief Web admin "Wireless" page: renders and saves the WiFi configuration
 * (mode, TX power, station profiles and access point settings) and serves the
 * JSON access point scan.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "esp_wifi.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

esp_err_t page_wireless_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_WIRELESS, "wireless");

    char buf[1900];
    snprintf(buf, sizeof(buf),
             "<form method='POST' action='/wireless'>"
             "<fieldset><legend>" TR_WIFI_MODE_LEGEND "</legend>"
             "<label>" TR_F_MODE "</label><select name='wifiMode'>"
             "<option value='0' %s>" TR_F_OFF "</option>"
             "<option value='1' %s>" TR_WIFI_STATION "</option>"
             "<option value='2' %s>" TR_WIFI_ACCESS_POINT "</option>"
             "<option value='3' %s>" TR_WIFI_AP_STA "</option>"
             "</select>"
             "<label>" TR_WIFI_TX_POWER "</label><input type='number' name='wifiPwr' value='%d' min='0' max='20'>"
             "</fieldset>"
             "<fieldset><legend>" TR_WIFI_ACCESS_POINT "</legend>"
             "<label>" TR_WIFI_AP_SSID "</label><input type='text' name='apSsid' value='%s' maxlength='32'>"
             "<label>" TR_WIFI_AP_PASSWORD "</label><input type='password' name='apPass' id='pwd_apPass' value='%s' maxlength='63' minlength='8'>"
             "<label class='pwd-show'><input type='checkbox' onclick=\"togglePwd('pwd_apPass',this)\"> " TR_SHOW_PASSWORD "</label>"
             "<label>" TR_WIFI_AP_CHANNEL "</label><input type='number' name='apCh' value='%d' min='1' max='13'>"
             "</fieldset>"
             "<button type='button' class='secondary' id='wifiScanBtn' onclick='wifiScan()'>" TR_BTN_WIFI_SCAN "</button> "
             "<span id='wifiScanStatus'></span>",
             g_config.wifi_mode == 0 ? "selected" : "", g_config.wifi_mode == 1 ? "selected" : "", g_config.wifi_mode == 2 ? "selected" : "",
             g_config.wifi_mode == 3 ? "selected" : "", g_config.wifi_power, g_config.wifi_ap_ssid, g_config.wifi_ap_pass, g_config.wifi_ap_ch);
    httpd_resp_sendstr_chunk(req, buf);

    // One shared datalist, filled in by wifiScan() below. It only ever offers
    // *suggestions* - see the SSID field's comment.
    httpd_resp_sendstr_chunk(req, "<datalist id='ssidList'></datalist>");

    for (int i = 0; i < WIFI_STA_NUM; i++) {
        char sec[1100];
        snprintf(sec, sizeof(sec),
                 "<fieldset><legend>" TR_WIFI_CLIENT_LEGEND "</legend>"
                 "<label><input type='checkbox' name='staEn%d' %s> " TR_F_ENABLE "</label>"
                 // SSID used to be a <select> whose only entries were "NONE" and
                 // whatever was already saved, with everything else supplied by
                 // the WiFi Scan button. That made a scan MANDATORY before any
                 // new network could be entered, and left two silent ways to
                 // save a station that cannot work: leaving the box on "NONE"
                 // submits an empty SSID, and a hidden/5 GHz/out-of-range AP
                 // never appears in the list at all. A free-text input with a
                 // datalist keeps the scan results as suggestions while letting
                 // the SSID simply be typed.
                 "<label>" TR_F_SSID "</label>"
                 "<input type='text' name='staSsid%d' id='staSsid%d' list='ssidList' value='%s' maxlength='32' autocomplete='off' "
                 "placeholder='" TR_WIFI_SSID_PLACEHOLDER "'>"
                 "<label>" TR_F_PASSWORD "</label><input type='password' name='staPass%d' id='pwd_staPass%d' value='%s' maxlength='63' minlength='8'>"
                 "<label class='pwd-show'><input type='checkbox' onclick=\"togglePwd('pwd_staPass%d',this)\"> " TR_SHOW_PASSWORD "</label>"
                 "</fieldset>",
                 i, /* legend #%d */
                 i, g_config.wifi_sta[i].enable ? "checked" : "", i, i, g_config.wifi_sta[i].wifi_ssid, i, i,
                 g_config.wifi_sta[i].wifi_pass, i);
        httpd_resp_sendstr_chunk(req, sec);
    }

    httpd_resp_sendstr_chunk(req, "<script>"
                                  "function wifiScan(){"
                                  "var btn=document.getElementById('wifiScanBtn');"
                                  "var status=document.getElementById('wifiScanStatus');"
                                  "btn.disabled=true;status.textContent=' " TR_WIFI_SCANNING "';"
                                  "fetch('/wifiscan').then(function(r){return r.json();}).then(function(data){"
                                  "btn.disabled=false;"
                                  "if(data.error){status.textContent=' '+data.error;return;}"
                                  "var nets=(data.networks||[]).slice().sort(function(a,b){return b.rssi-a.rssi;});"
                                  "status.textContent=' ('+nets.length+')';"
                                  // Fill the shared datalist only. The old code rebuilt each <select>'s
                                  // options and reassigned sel.value, which is what could silently reset a
                                  // field to '' (empty SSID). Suggestions can no longer touch what is typed.
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
    g_config.wifi_power = (int8_t)web_form_get_int(body, "wifiPwr", g_config.wifi_power);
    web_form_get(body, "apSsid", g_config.wifi_ap_ssid, sizeof(g_config.wifi_ap_ssid));
    web_form_get(body, "apPass", g_config.wifi_ap_pass, sizeof(g_config.wifi_ap_pass));
    g_config.wifi_ap_ch = (uint8_t)web_form_get_int(body, "apCh", g_config.wifi_ap_ch);

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

    app_config_save();

    // Tell the user NOW, in the browser, if what they just saved cannot work.
    // Selecting Station or AP+STA in the Mode dropdown does nothing on its own:
    // each WiFi Client block has its own Enable checkbox, and the SSID has to
    // be non-empty. Getting either wrong used to save happily and then fail in
    // silence - the only clue was an error on the serial console after the next
    // reboot, which nobody watching a web UI ever sees.
    if (g_config.wifi_mode == 1 || g_config.wifi_mode == 3) {
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
                                    "<p style='color:#cf222e;font-weight:600'>" TR_WIFI_STA_NEEDS_SSID "</p>"
                                    "<p><a href='/wireless'>&larr; " TR_F_WIRELESS "</a></p>"
                                    "</body></html>");
            return ESP_OK;
        }
    }

    web_send_saved_redirect(req, "/wireless");
    return ESP_OK;
}

// ---------------------------------------------------------------- WiFi scan
// GET /wifiscan - triggers a blocking active scan for nearby access points and
// returns the results as JSON: {"networks":[{"ssid":"...","rssi":-55},...]}
// Requires the radio to currently be in STA or AP+STA mode (a scan cannot run
// while the radio is AP-only or powered off).
esp_err_t page_wifi_scan_get(httpd_req_t *req) {
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
