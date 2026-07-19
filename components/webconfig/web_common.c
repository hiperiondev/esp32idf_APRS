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
#include <strings.h> // strncasecmp (multipart header parsing)

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

// ---------------------------------------------------------------- Output-side escaping
// (web_urldecode above handles the *input* direction, for query strings
// esp_httpd hands us; these three handle the *output* direction, for
// user-supplied strings - chiefly filenames - that get echoed back into
// hrefs/HTML/JS. Skipping this is what let a filename with a space or a
// quote character in it silently break the Storage page's delete/download
// links and onclick handlers.)

void web_urlencode(const char *src, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0)
        return;
    static const char hex[] = "0123456789ABCDEF";
    size_t di = 0;
    for (const unsigned char *p = (const unsigned char *)src; src && *p && di + 1 < dst_size; p++) {
        unsigned char c = *p;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[di++] = (char)c;
        } else {
            if (di + 3 >= dst_size)
                break;
            dst[di++] = '%';
            dst[di++] = hex[(c >> 4) & 0xF];
            dst[di++] = hex[c & 0xF];
        }
    }
    dst[di] = 0;
}

void web_html_attr_escape(const char *src, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0)
        return;
    size_t di = 0;
    for (const char *p = src; src && *p && di + 1 < dst_size; p++) {
        const char *ent = NULL;
        switch (*p) {
            case '&':
                ent = "&amp;";
                break;
            case '<':
                ent = "&lt;";
                break;
            case '>':
                ent = "&gt;";
                break;
            case '"':
                ent = "&quot;";
                break;
            case '\'':
                ent = "&#39;";
                break;
            default:
                break;
        }
        if (ent) {
            size_t elen = strlen(ent);
            if (di + elen >= dst_size)
                break;
            memcpy(dst + di, ent, elen);
            di += elen;
        } else {
            dst[di++] = *p;
        }
    }
    dst[di] = 0;
}

void web_sanitize_filename(const char *src, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0)
        return;
    dst[0] = 0;
    if (!src)
        return;

    // Some browsers send the full local path for <input type=file>; keep
    // only whatever follows the last separator.
    const char *base = src;
    for (const char *p = src; *p; p++) {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }

    size_t di = 0;
    for (const char *p = base; *p && di + 1 < dst_size; p++) {
        unsigned char c = (unsigned char)*p;
        dst[di++] = (isalnum(c) || c == '.' || c == '-' || c == '_' || c == ' ') ? (char)c : '_';
    }
    dst[di] = 0;

    // Strip leading dots so "..", ".htaccess"-style, or empty-after-dots
    // names can't happen.
    size_t lead = 0;
    while (dst[lead] == '.')
        lead++;
    if (lead > 0)
        memmove(dst, dst + lead, di - lead + 1);

    if (dst[0] == 0)
        snprintf(dst, dst_size, "upload.bin");
}

// ---------------------------------------------------------------- Multipart upload (streaming)
// See web_common.h for the contract. Implementation notes:
//
// The parser keeps one heap scratch buffer (MP_BUF_CAP bytes) and never
// holds more than that much of the request in RAM at once, regardless of
// how large the uploaded file is - it feeds completed chunks of the file
// part to `cb` as soon as it's sure they aren't a prefix of the closing
// boundary marker, then discards them. This is what lets a multi-hundred-KB
// firmware image stream straight into esp_ota_write() on a device with a
// few hundred KB of free heap.
#define MP_BUF_CAP     4096
#define MP_MAX_HEADER  512
#define MP_MAX_PARTS   32 // sanity cap against a pathological/adversarial body

static const uint8_t *mp_mem_find(const uint8_t *hay, size_t haylen, const char *needle, size_t needlelen) {
    if (needlelen == 0 || haylen < needlelen)
        return NULL;
    for (size_t i = 0; i + needlelen <= haylen; i++) {
        if (memcmp(hay + i, needle, needlelen) == 0)
            return hay + i;
    }
    return NULL;
}

// Case-insensitive substring search (header names/values are case-insensitive).
static const char *mp_ci_strstr(const char *hay, const char *needle) {
    size_t nlen = strlen(needle);
    for (const char *p = hay; *p; p++) {
        if (strncasecmp(p, needle, nlen) == 0)
            return p;
    }
    return NULL;
}

esp_err_t web_multipart_receive_file(httpd_req_t *req, web_multipart_data_cb_t cb, void *cb_ctx, char *filename_out, size_t filename_out_size) {
    if (filename_out && filename_out_size)
        filename_out[0] = 0;

    char ctype[256];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", ctype, sizeof(ctype)) != ESP_OK)
        return ESP_ERR_INVALID_ARG;
    const char *bmark = mp_ci_strstr(ctype, "boundary=");
    if (!bmark)
        return ESP_ERR_INVALID_ARG;
    bmark += 9;

    char boundary[128];
    size_t bi = 0;
    if (*bmark == '"') {
        bmark++;
        while (*bmark && *bmark != '"' && bi + 1 < sizeof(boundary))
            boundary[bi++] = *bmark++;
    } else {
        while (*bmark && *bmark != ';' && *bmark != ' ' && *bmark != '\r' && *bmark != '\n' && bi + 1 < sizeof(boundary))
            boundary[bi++] = *bmark++;
    }
    boundary[bi] = 0;
    if (bi == 0)
        return ESP_ERR_INVALID_ARG;

    char delim[132];
    int delim_len = snprintf(delim, sizeof(delim), "--%s", boundary);
    if (delim_len <= 0 || (size_t)delim_len >= sizeof(delim))
        return ESP_ERR_INVALID_ARG;

    char close_marker[134];
    int close_len = snprintf(close_marker, sizeof(close_marker), "\r\n%s", delim);
    if (close_len <= 0 || (size_t)close_len >= sizeof(close_marker)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t *buf = malloc(MP_BUF_CAP);
    if (!buf)
        return ESP_ERR_NO_MEM;
    size_t buf_len = 0;
    int remaining = (int)req->content_len;
    esp_err_t result = ESP_FAIL;
    bool found_file_part = false;

#define MP_FILL()                                                                                                                                            \
    do {                                                                                                                                                      \
        while (buf_len < MP_BUF_CAP && remaining > 0) {                                                                                                       \
            int want = (int)(MP_BUF_CAP - buf_len);                                                                                                           \
            if (want > remaining)                                                                                                                             \
                want = remaining;                                                                                                                             \
            int r = httpd_req_recv(req, (char *)buf + buf_len, want);                                                                                         \
            if (r > 0) {                                                                                                                                      \
                buf_len += (size_t)r;                                                                                                                         \
                remaining -= r;                                                                                                                               \
            } else if (r == HTTPD_SOCK_ERR_TIMEOUT) {                                                                                                         \
                continue;                                                                                                                                     \
            } else {                                                                                                                                          \
                goto done;                                                                                                                                    \
            }                                                                                                                                                 \
        }                                                                                                                                                     \
    } while (0)

    MP_FILL();

    // ---- skip preamble up to and including the first boundary delimiter ----
    {
        const uint8_t *p = mp_mem_find(buf, buf_len, delim, (size_t)delim_len);
        if (!p)
            goto done; // no boundary at all: not a well-formed multipart body
        size_t skip = (size_t)(p - buf) + (size_t)delim_len;
        memmove(buf, buf + skip, buf_len - skip);
        buf_len -= skip;
    }

    for (int part_idx = 0; part_idx < MP_MAX_PARTS; part_idx++) {
        // Right after a delimiter: either "--" (terminating boundary, no more
        // parts) or "\r\n" (a part follows).
        if (buf_len < 2)
            MP_FILL();
        if (buf_len < 2) {
            // Body ended right at/after the last part's own closing boundary
            // (no distinguishable final "--" epilogue left to read). If we
            // already streamed a file part, that's a completed upload, not
            // an error - only a genuinely empty/truncated body is malformed.
            result = found_file_part ? ESP_OK : ESP_FAIL;
            goto done;
        }
        if (buf[0] == '-' && buf[1] == '-') {
            result = found_file_part ? ESP_OK : ESP_ERR_NOT_FOUND;
            goto done;
        }
        if (buf[0] != '\r' || buf[1] != '\n')
            goto done; // malformed
        memmove(buf, buf + 2, buf_len - 2);
        buf_len -= 2;

        // ---- part headers, up to the blank line ----
        char headers[MP_MAX_HEADER];
        const uint8_t *hp;
        for (;;) {
            hp = mp_mem_find(buf, buf_len, "\r\n\r\n", 4);
            if (hp)
                break;
            if (buf_len >= MP_BUF_CAP || remaining <= 0)
                goto done; // headers too large or body ended mid-header: malformed
            MP_FILL();
        }
        size_t hdrblock_len = (size_t)(hp - buf);
        size_t hdrcopy_len = hdrblock_len < sizeof(headers) - 1 ? hdrblock_len : sizeof(headers) - 1;
        memcpy(headers, buf, hdrcopy_len);
        headers[hdrcopy_len] = 0;
        size_t consumed = hdrblock_len + 4;
        memmove(buf, buf + consumed, buf_len - consumed);
        buf_len -= consumed;

        // ---- does this part carry filename="..."? ----
        bool has_filename = false;
        {
            const char *cdisp = mp_ci_strstr(headers, "Content-Disposition");
            if (cdisp) {
                const char *fn = mp_ci_strstr(cdisp, "filename=\"");
                if (fn) {
                    fn += 10;
                    const char *end = strchr(fn, '"');
                    if (end && end > fn) {
                        has_filename = true;
                        if (filename_out && filename_out_size) {
                            size_t flen = (size_t)(end - fn);
                            if (flen >= filename_out_size)
                                flen = filename_out_size - 1;
                            memcpy(filename_out, fn, flen);
                            filename_out[flen] = 0;
                        }
                    }
                }
            }
        }
        bool stream_this = has_filename && !found_file_part;

        // ---- part data, up to the next "\r\n--boundary" ----
        for (;;) {
            const uint8_t *cm = mp_mem_find(buf, buf_len, close_marker, (size_t)close_len);
            if (cm) {
                size_t data_len = (size_t)(cm - buf);
                if (stream_this && data_len > 0 && cb(cb_ctx, buf, data_len) != ESP_OK)
                    goto done;
                size_t used = data_len + (size_t)close_len;
                memmove(buf, buf + used, buf_len - used);
                buf_len -= used;
                break;
            }
            // No full marker in the buffer yet: flush everything except the
            // tail that could still be a prefix of the marker, then refill.
            size_t keep = (size_t)close_len - 1;
            if (buf_len > keep) {
                size_t flush = buf_len - keep;
                if (stream_this && cb(cb_ctx, buf, flush) != ESP_OK)
                    goto done;
                memmove(buf, buf + flush, buf_len - flush);
                buf_len -= flush;
            }
            if (remaining <= 0)
                goto done; // ran out of body before finding the closing boundary: truncated/malformed
            MP_FILL();
        }

        if (has_filename)
            found_file_part = true;
        // loop back: buffer now starts right after this part's boundary
        // delimiter, exactly the state the top of the loop expects.
    }
    // MP_MAX_PARTS exceeded without reaching a terminating boundary.
    result = ESP_FAIL;

done:
    free(buf);
#undef MP_FILL
    return result;
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
