/**
 * @file web_common.c
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
 * @brief Shared web admin helpers: HTTP Basic authentication, request body
 * reading, URL-decoded form field extraction, and the common HTML chrome (page
 * header/footer, fieldsets, form controls) and stylesheet used by every admin
 * page.
 */

#include "web_common.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include "translations.h"

static const char *TAG = "web_common";

// ---------------------------------------------------------------- Basic Auth
bool web_check_auth(httpd_req_t *req) {
    if (g_config.http_username[0] == 0)
        return true; // auth disabled if no user set

    char hdr[160];
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK) {
        goto need_auth;
    }
    if (strncmp(hdr, "Basic ", 6) != 0)
        goto need_auth;

    {
        unsigned char decoded[128];
        size_t outlen = 0;
        int rc = mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &outlen, (const unsigned char *)(hdr + 6), strlen(hdr + 6));
        if (rc != 0)
            goto need_auth;
        decoded[outlen] = 0;

        char *sep = strchr((char *)decoded, ':');
        if (!sep)
            goto need_auth;
        *sep = 0;
        const char *user = (char *)decoded;
        const char *pass = sep + 1;

        if (strcmp(user, g_config.http_username) == 0 && strcmp(pass, g_config.http_password) == 0) {
            return true;
        }
    }

need_auth:
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP32APRS\"");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, "<h1>" TR_UNAUTHORIZED "</h1>");
    return false;
}

// ---------------------------------------------------------------- Body read
int web_read_body(httpd_req_t *req, char *buf, size_t buf_size) {
    if (req->content_len >= buf_size) {
        ESP_LOGW(TAG, "body too large: %d >= %d", (int)req->content_len, (int)buf_size);
        return -1;
    }
    int total = 0;
    while (total < req->content_len) {
        int r = httpd_req_recv(req, buf + total, req->content_len - total);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT)
                continue;
            return -1;
        }
        total += r;
    }
    buf[total] = 0;
    return total;
}

// ---------------------------------------------------------------- URL decode
void web_urldecode(const char *src, char *dst, size_t dst_size) {
    size_t di = 0;
    while (*src && di + 1 < dst_size) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = { src[1], src[2], 0 };
            dst[di++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[di++] = ' ';
            src++;
        } else {
            dst[di++] = *src++;
        }
    }
    dst[di] = 0;
}

bool web_form_get(const char *body, const char *key, char *out, size_t out_size) {
    if (!body)
        return false;
    size_t keylen = strlen(key);
    const char *p = body;
    while (p && *p) {
        const char *amp = strchr(p, '&');
        size_t seg_len = amp ? (size_t)(amp - p) : strlen(p);
        if (seg_len > keylen && p[keylen] == '=' && strncmp(p, key, keylen) == 0) {
            const char *valstart = p + keylen + 1;
            size_t vallen = seg_len - keylen - 1;
            char tmp[512];
            if (vallen >= sizeof(tmp))
                vallen = sizeof(tmp) - 1;
            memcpy(tmp, valstart, vallen);
            tmp[vallen] = 0;
            web_urldecode(tmp, out, out_size);
            return true;
        }
        // exact match with no '=' (rare) - checkbox absent case handled by caller default
        p = amp ? amp + 1 : NULL;
    }
    return false;
}

bool web_form_get_bool(const char *body, const char *key) {
    char v[16];
    if (!web_form_get(body, key, v, sizeof(v)))
        return false;
    return (strcmp(v, "on") == 0 || strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0);
}

int web_form_get_int(const char *body, const char *key, int def) {
    char v[32];
    if (!web_form_get(body, key, v, sizeof(v)) || v[0] == 0)
        return def;
    return atoi(v);
}

float web_form_get_float(const char *body, const char *key, float def) {
    char v[32];
    if (!web_form_get(body, key, v, sizeof(v)) || v[0] == 0)
        return def;
    return strtof(v, NULL);
}

// ---------------------------------------------------------------- HTML shell
// Sidebar menu mirrors the original firmware's route order/labels exactly.
struct menu_item {
    const char *href;
    const char *label;
    const char *key;
};
static const struct menu_item MENU[] = {
#ifdef ENABLE_DASHBOARD
    { "/dashboard", TR_MENU_DASHBOARD, "dashboard" },
#endif
#ifdef ENABLE_RADIO_MODEM
    { "/radio", TR_MENU_RADIO, "radio" },
#endif
#ifdef ENABLE_VPN
    { "/vpn", TR_MENU_VPN, "vpn" },
#endif
#ifdef ENABLE_MQTT
    { "/mqtt", TR_MENU_MQTT, "mqtt" },
#endif
#ifdef ENABLE_MESSAGE
    { "/msg", TR_MENU_MSG, "msg" },
#endif
#ifdef ENABLE_MOD_GPIO
    { "/mod", TR_MENU_MOD, "mod" },
#endif
#ifdef ENABLE_IGATE
    { "/igate", TR_MENU_IGATE, "igate" },
#endif
#ifdef ENABLE_DIGIPEATER
    { "/digi", TR_MENU_DIGI, "digi" },
#endif
#ifdef ENABLE_TRACKER
    { "/tracker", TR_MENU_TRACKER, "tracker" },
#endif
#ifdef ENABLE_WEATHER
    { "/wx", TR_MENU_WX, "wx" },
#endif
#ifdef ENABLE_TELEMETRY
    { "/tlm", TR_MENU_TLM, "tlm" },
#endif
#ifdef ENABLE_SENSORS
    { "/sensor", TR_MENU_SENSOR, "sensor" },
#endif
#ifdef ENABLE_SYSTEM
    { "/system", TR_MENU_SYSTEM, "system" },
#endif
#ifdef ENABLE_WIRELESS
    { "/wireless", TR_MENU_WIRELESS, "wireless" },
#endif
#ifdef ENABLE_GNSS
    { "/gnss", TR_MENU_GNSS, "gnss" },
#endif
#ifdef ENABLE_FILE_STORAGE
    { "/storage", TR_MENU_STORAGE, "storage" },
#endif
#ifdef ENABLE_ABOUT_FIRMWARE
    { "/about", TR_MENU_ABOUT, "about" },
#endif
};
#define MENU_COUNT (sizeof(MENU) / sizeof(MENU[0]))

void web_send_header(httpd_req_t *req, const char *title, const char *active_menu) {
    httpd_resp_set_type(req, "text/html");
    // Config pages render live g_config values into the form on every GET.
    // Without this header, browsers (especially after a POST->redirect->GET
    // save flow) may serve a cached copy of the page instead of re-fetching,
    // so a value that was just saved appears not to have been saved at all.
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                                  "<link rel='stylesheet' href='/style.css'>"
                                  "<title>ESP32APRS</title></head><body>"
                                  "<div class='topbar'><span class='brand'>" TR_BRAND "</span>"
                                  "<a class='logout' href='/logout'>" TR_LOGOUT "</a></div>"
                                  "<div class='layout'><nav class='sidebar'><ul>");

    for (size_t i = 0; i < MENU_COUNT; i++) {
        char line[160];
        bool is_active = active_menu && strcmp(active_menu, MENU[i].key) == 0;
        snprintf(line, sizeof(line), "<li><a href='%s'%s>%s</a></li>", MENU[i].href, is_active ? " class='active'" : "", MENU[i].label);
        httpd_resp_sendstr_chunk(req, line);
    }
    httpd_resp_sendstr_chunk(req, "</ul></nav><main class='content'>");
    if (title) {
        httpd_resp_sendstr_chunk(req, "<h1>");
        httpd_resp_sendstr_chunk(req, title);
        httpd_resp_sendstr_chunk(req, "</h1>");
    }
}

void web_send_footer(httpd_req_t *req) {
    httpd_resp_sendstr_chunk(req, "<script>function togglePwd(id,cb){var el=document.getElementById(id);if(el){el.type=(cb&&cb.checked)?'text':'password';}}</script>");
    httpd_resp_sendstr_chunk(req, "</main></div></body></html>");
    httpd_resp_sendstr_chunk(req, NULL); // end chunked response
}

void web_send_saved_redirect(httpd_req_t *req, const char *location) {
    char buf[256];
    snprintf(buf, sizeof(buf),
             "<!DOCTYPE html><html><head><meta charset='utf-8'>"
             "<meta http-equiv='refresh' content='1;url=%s'></head>"
             "<body>" TR_SAVED_REDIRECT "</body></html>",
             location);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_sendstr(req, buf);
}

esp_err_t web_handle_css(httpd_req_t *req) {
    static const char *css =
        // Palette/typography matched to hiperiondev/ESP32_WSPR's embedded web admin
        ":root{--bg:#f5f4f0;--card:#ffffff;--border:#d0cfc9;--accent:#1a56db;"
        "--green:#1a7f37;--red:#cf222e;--text:#1c1c1c;--sub:#57534e;}"
        "*{box-sizing:border-box;margin:0;padding:0;}"
        "body{font-family:'Segoe UI',system-ui,sans-serif;background:var(--bg);color:var(--text);}"
        ".topbar{display:flex;justify-content:space-between;align-items:center;padding:14px 20px;"
        "background:var(--card);border-bottom:1px solid var(--border);}"
        ".topbar .brand{font-weight:700;color:var(--accent);font-size:1.05em;}"
        ".topbar .logout{color:var(--sub);text-decoration:none;font-size:.85em;font-weight:600;}"
        ".topbar .logout:hover{color:var(--red);}"
        ".layout{display:flex;min-height:calc(100vh - 52px);}"
        ".sidebar{width:210px;background:var(--card);border-right:1px solid var(--border);padding:12px 0;}"
        ".sidebar ul{list-style:none;}"
        ".sidebar li a{display:block;padding:10px 18px;color:var(--text);text-decoration:none;"
        "font-size:.85em;border-left:3px solid transparent;transition:.15s;}"
        ".sidebar li a:hover{background:var(--bg);border-left-color:var(--border);}"
        ".sidebar li a.active{background:#eef2ff;color:var(--accent);font-weight:700;border-left-color:var(--accent);}"
        ".content{flex:1;padding:24px 28px;max-width:900px;}"
        "h1{color:var(--accent);font-size:1.4em;border-bottom:1px solid var(--border);padding-bottom:10px;margin-bottom:16px;}"
        "fieldset{background:var(--card);border:1px solid var(--border);border-radius:10px;"
        "margin-bottom:16px;padding:18px 20px;}"
        "legend{color:var(--accent);padding:0 8px;font-size:.85em;font-weight:700;}"
        "label{display:block;color:var(--sub);font-size:.8em;margin:12px 0 4px;}"
        "label:first-child{margin-top:0;}"
        "p label{display:inline;}"
        ".pwd-show{display:block;font-size:.72em;font-weight:400;margin:4px 0 0;color:var(--sub);}"
        "input[type=text],input[type=password],input[type=number],select,textarea{"
        "width:100%;padding:8px 10px;border:1px solid var(--border);border-radius:6px;"
        "background:var(--bg);color:var(--text);font-size:.9em;outline:none;transition:.2s;}"
        "input:focus,select:focus,textarea:focus{border-color:var(--accent);}"
        "input[type=checkbox]{width:16px;height:16px;cursor:pointer;accent-color:var(--accent);margin-right:6px;}"
        ".row{display:flex;gap:16px;flex-wrap:wrap;}"
        ".row>div{flex:1;min-width:160px;}"
        "button,.btn{background:#b2f0e8;color:#0d4a42;border:0;border-radius:6px;"
        "padding:10px 20px;font-weight:700;cursor:pointer;font-size:.9em;text-decoration:none;"
        "display:inline-block;margin-top:10px;transition:.2s;}"
        "button:hover,.btn:hover{background:#89e6d8;}"
        "button.secondary,.btn.secondary{background:#e8e7e3;color:var(--sub);}"
        "button.secondary:hover,.btn.secondary:hover{background:#dddcda;}"
        "button.danger,.btn.danger{background:#fff0f0;color:var(--red);border:1px solid var(--red);}"
        "button.danger:hover,.btn.danger:hover{background:var(--red);color:#fff;}"
        "table{border-collapse:collapse;width:100%;font-size:.82em;}"
        "table th,table td{border:1px solid var(--border);padding:7px 9px;text-align:left;}"
        "table th{background:var(--bg);color:var(--sub);}"
        ".login-box{max-width:340px;margin:80px auto;background:var(--card);padding:28px;"
        "border-radius:10px;border:1px solid var(--border);}"
        ".login-box h1{border:0;text-align:center;}"
        ".msg-ok{color:var(--green);} .msg-err{color:var(--red);}"
        ".badge{display:inline-block;padding:2px 8px;border-radius:10px;font-size:.75em;font-weight:700;}"
        ".badge.ok{background:#d1fae5;color:var(--green);}"
        ".badge.warn{background:#fef3c7;color:#92400e;}"
        ".badge.err{background:#fee2e2;color:var(--red);}"
        ".traffic-actions{display:flex;gap:8px;margin-bottom:10px;}"
        ".traffic-actions .btn{margin-top:0;padding:6px 12px;font-size:.8em;}"
        ".traffic-table-wrap{max-height:360px;overflow-y:auto;}"
        "#trafficTable td{font-family:'Consolas','Courier New',monospace;font-size:.95em;"
        "white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:340px;}"
        "#trafficTable th:nth-child(4),#trafficTable td:nth-child(4){max-width:420px;}";
    httpd_resp_set_type(req, "text/css");
    return httpd_resp_sendstr(req, css);
}

// ---------------------------------------------------------------- Field helpers
void web_raw(httpd_req_t *req, const char *html) {
    httpd_resp_sendstr_chunk(req, html);
}

void web_fieldset_open(httpd_req_t *req, const char *legend) {
    char buf[160];
    snprintf(buf, sizeof(buf), "<fieldset><legend>%.100s</legend>", legend);
    httpd_resp_sendstr_chunk(req, buf);
}

void web_fieldset_close(httpd_req_t *req) {
    httpd_resp_sendstr_chunk(req, "</fieldset>");
}

void web_field_text(httpd_req_t *req, const char *label, const char *name, const char *value, int maxlen) {
    char buf[600];
    snprintf(buf, sizeof(buf), "<label>%.60s</label><input type='text' name='%.30s' value='%.400s' maxlength='%d'>", label, name, value ? value : "", maxlen);
    httpd_resp_sendstr_chunk(req, buf);
}

void web_field_password(httpd_req_t *req, const char *label, const char *name, const char *value, int maxlen) {
    char buf[900];
    snprintf(buf, sizeof(buf),
             "<label>%.60s</label>"
             "<input type='password' name='%.30s' id='pwd_%.30s' value='%.400s' maxlength='%d'>"
             "<label class='pwd-show'><input type='checkbox' onclick=\"togglePwd('pwd_%.30s',this)\"> " TR_SHOW_PASSWORD "</label>",
             label, name, name, value ? value : "", maxlen, name);
    httpd_resp_sendstr_chunk(req, buf);
}

void web_field_int(httpd_req_t *req, const char *label, const char *name, long value) {
    char buf[220];
    snprintf(buf, sizeof(buf), "<label>%.60s</label><input type='number' name='%.30s' value='%ld'>", label, name, value);
    httpd_resp_sendstr_chunk(req, buf);
}

void web_field_float(httpd_req_t *req, const char *label, const char *name, float value, const char *step) {
    char buf[240];
    snprintf(buf, sizeof(buf), "<label>%.60s</label><input type='number' step='%.10s' name='%.30s' value='%g'>", label, step ? step : "0.01", name,
             (double)value);
    httpd_resp_sendstr_chunk(req, buf);
}

void web_field_checkbox(httpd_req_t *req, const char *label, const char *name, bool checked) {
    char buf[220];
    snprintf(buf, sizeof(buf), "<label><input type='checkbox' name='%.30s' %s> %.60s</label>", name, checked ? "checked" : "", label);
    httpd_resp_sendstr_chunk(req, buf);
}

void web_select_open(httpd_req_t *req, const char *label, const char *name) {
    char buf[160];
    snprintf(buf, sizeof(buf), "<label>%.60s</label><select name='%.30s'>", label, name);
    httpd_resp_sendstr_chunk(req, buf);
}

void web_select_option(httpd_req_t *req, int value, const char *label, bool selected) {
    char buf[400];
    snprintf(buf, sizeof(buf), "<option value='%d' %s>%.300s</option>", value, selected ? "selected" : "", label);
    httpd_resp_sendstr_chunk(req, buf);
}

void web_select_close(httpd_req_t *req) {
    httpd_resp_sendstr_chunk(req, "</select>");
}

// ---------------------------------------------------------------- Symbol picker
void web_field_symbol(httpd_req_t *req, const char *label, const char *name_prefix, const char *sym2) {
    char table_ch[2] = { (sym2 && sym2[0]) ? sym2[0] : '/', 0 };
    char sym_ch[2] = { (sym2 && sym2[1]) ? sym2[1] : '&', 0 };
    int table_num = (table_ch[0] == '\\') ? 2 : 1;
    int code_num = (int)(unsigned char)sym_ch[0];

    char ids[64];
    snprintf(ids, sizeof(ids), "%.30sTable", name_prefix);
    char idc[64];
    snprintf(idc, sizeof(idc), "%.30sCode", name_prefix);

    char buf[1600];
    snprintf(buf, sizeof(buf),
             "<label>%.60s</label>"
             "<div style='display:flex;gap:6px;align-items:center'>"
             "<span id='%.30s_icn' style='display:inline-flex;align-items:center;justify-content:center;width:34px;height:34px;"
             "border-radius:6px;background:#dcfce7;overflow:hidden;flex:none'>"
             "<img id='%.30s_img' src='http://aprs.dprns.com/symbols/icons/%d-%d.png' width=32 height=32 style='display:block' "
             "onerror=\"this.style.display='none'\">"
             "</span>"
             "<span style='font-size:12px;color:var(--sub)'>%.60s:</span>"
             "<input type='text' id='%.30s' name='%.30s' value='%.4s' maxlength='1' style='width:3em;text-align:center' "
             "oninput=\"aprsSymUpd('%.30s','%.30s')\">"
             "<span style='font-size:12px;color:var(--sub)'>%.60s:</span>"
             "<input type='text' id='%.30s' name='%.30s' value='%.4s' maxlength='1' style='width:3em;text-align:center' "
             "oninput=\"aprsSymUpd('%.30s','%.30s')\">"
             "<a href='/symbol' target='_blank' title='%.60s' class='secondary' style='text-decoration:none;padding:4px 8px'>%.60s</a>"
             "</div>",
             label, name_prefix, name_prefix, code_num, table_num, TR_F_SYMBOL_TABLE, ids, ids, table_ch, ids, idc,
             TR_F_SYMBOL_CODE, idc, idc, sym_ch, ids, idc, TR_SYM_PICK_HINT, TR_BTN_PICK_SYMBOL);
    httpd_resp_sendstr_chunk(req, buf);

    // Tiny helper script: updates the graphical icon live as the user edits
    // the Table/Code inputs, without waiting for a page reload. Safe to emit
    // once per field; browsers just redefine the same function identically.
    static const char *script =
        "<script>function aprsSymUpd(t,c){"
        "var tv=(document.getElementById(t).value||'/').charAt(0)||'/';"
        "var cv=(document.getElementById(c).value||' ').charAt(0)||' ';"
        "var tn=(tv=='\\\\')?2:1;var cn=cv.charCodeAt(0);"
        "var pfx=t.slice(0,-5);"
        "var img=document.getElementById(pfx+'_img');"
        "if(img){img.style.display='block';img.src='http://aprs.dprns.com/symbols/icons/'+cn+'-'+tn+'.png';}"
        "}</script>";
    httpd_resp_sendstr_chunk(req, script);
}

void web_form_get_symbol(const char *body, const char *name_prefix, const char *legacy_name, char *out, size_t out_size) {
    if (!out || out_size < 3)
        return;

    char name_t[40], name_c[40];
    snprintf(name_t, sizeof(name_t), "%.30sTable", name_prefix);
    snprintf(name_c, sizeof(name_c), "%.30sCode", name_prefix);

    char t[4] = { 0 }, s[4] = { 0 };
    bool got_t = web_form_get(body, name_t, t, sizeof(t));
    bool got_s = web_form_get(body, name_c, s, sizeof(s));
    if (got_t || got_s) {
        out[0] = t[0] ? t[0] : '/';
        out[1] = s[0] ? s[0] : '&';
        out[2] = 0;
        return;
    }

    char legacy[4] = { 0 };
    if (legacy_name && web_form_get(body, legacy_name, legacy, sizeof(legacy)))
        web_form_get(body, legacy_name, out, out_size);
}
