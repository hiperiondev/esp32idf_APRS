// @file web_common.c
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
// @brief Shared web admin helpers: HTTP Basic authentication, request body
// reading, URL-decoded form field extraction, and the common HTML chrome (page
// header/footer, fieldsets, form controls) and stylesheet used by every admin
// page.

#include "web_common.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // strncasecmp (multipart header parsing)

#include "app_config.h"
#include "aprs_coord.h"                         // aprs_symbol_table_is_valid()/aprs_symbol_code_is_valid(): the symbol pair accepted on air
#include "aprs_service.h"                       // APRS_SOFTWARE_NAME: the firmware name shown as the HTTP auth realm and page title
#include "esp32idf_radioamateur_modem_config.h" // MODEM_ADC_GPIO/MODEM_DAC_GPIO/MODEM_PTT_GPIO: fixed audio front-end + PTT pins for the GPIO registry
#include "esp_log.h"
#include "esp_timer.h"    // esp_timer_get_time(): monotonic clock for the login lockout window
#include "gps.h"          // GPS_UART_RX_GPIO/TX_GPIO: fixed pins for the GPIO registry
#include "lwip/sockets.h" // getpeername(): client IP for the per-source login lockout
#include "mbedtls/base64.h"
#include "sensors_local_i2c.h" // SENSORS_LOCAL_I2C_SDA_GPIO/SCL_GPIO: fixed pins for the GPIO registry
#include "str_append.h"        // str_is_line_break_char()
#include "translations.h"

static const char *TAG = "web_common";

// Compares two NUL-terminated strings in time that depends only on the
// longer string's length, not on where the first mismatch occurs. Still
// compares content byte-for-byte (including each string's own terminator),
// so behaves like strcmp()==0 for correctness purposes.
static bool web_const_time_streq(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    unsigned char diff = (la != lb) ? 1 : 0;
    size_t n = la > lb ? la : lb;
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (i < la) ? (unsigned char)a[i] : 0;
        unsigned char cb = (i < lb) ? (unsigned char)b[i] : 0;
        diff |= (unsigned char)(ca ^ cb);
    }
    return diff == 0;
}

// ------------------------------------------------------- Login rate limiting
//
// Tracks failed Basic-Auth attempts per client IP so an attacker on the
// network can't brute-force the admin password at full HTTP round-trip
// speed. After WEB_AUTH_MAX_ATTEMPTS consecutive failures from the same
// source, that source is locked out for a backoff window that doubles with
// every further failure while locked out (capped at WEB_AUTH_LOCKOUT_MAX_S),
// and resets on a successful login. This is intentionally a small fixed-size
// in-RAM table (no persistent storage / no dynamic allocation) sized for a
// handful of concurrent offenders, which is appropriate for a single-board
// admin UI.
#define WEB_AUTH_MAX_ATTEMPTS   5   // failures allowed before the first lockout
#define WEB_AUTH_LOCKOUT_BASE_S 5   // initial lockout duration
#define WEB_AUTH_LOCKOUT_MAX_S  300 // cap on the backoff (5 minutes)
#define WEB_AUTH_TRACK_SLOTS    16  // distinct source IPs tracked at once

typedef struct {
    uint32_t ip;          // source IPv4 address in network byte order; 0 = free slot
    uint16_t fail_count;  // consecutive failures since the last success/reset
    int64_t locked_until; // esp_timer_get_time() microseconds; 0 = not locked
} web_auth_track_t;

static web_auth_track_t s_auth_track[WEB_AUTH_TRACK_SLOTS];

// Best-effort client IPv4 lookup for the connection behind req. Returns 0
// (never a valid unicast source) if it can't be determined, in which case the
// caller tracks that request under the shared "unknown" bucket instead of
// skipping rate limiting altogether. IPv4-only: this project builds with
// CONFIG_LWIP_IPV6 disabled (see sdkconfig), so the httpd socket is always
// plain AF_INET.
static uint32_t web_client_ipv4(httpd_req_t *req) {
    int sockfd = httpd_req_to_sockfd(req);
    if (sockfd < 0)
        return 0;

    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_len) != 0)
        return 0;
    if (addr.sin_family != AF_INET)
        return 0;

    return addr.sin_addr.s_addr;
}

// Finds this source's tracking slot, evicting the least-recently-failed
// entry if the table is full and the source isn't already present. Never
// returns NULL: worst case every source beyond WEB_AUTH_TRACK_SLOTS shares
// slot 0's counter, which only makes lockouts trigger sooner, never later.
static web_auth_track_t *web_auth_track_find(uint32_t ip) {
    web_auth_track_t *free_slot = NULL;
    web_auth_track_t *oldest = &s_auth_track[0];
    for (int i = 0; i < WEB_AUTH_TRACK_SLOTS; i++) {
        if (s_auth_track[i].ip == ip && s_auth_track[i].fail_count > 0)
            return &s_auth_track[i];
        if (!free_slot && s_auth_track[i].ip == 0)
            free_slot = &s_auth_track[i];
        if (s_auth_track[i].locked_until < oldest->locked_until)
            oldest = &s_auth_track[i];
    }
    if (free_slot) {
        free_slot->ip = ip;
        return free_slot;
    }
    oldest->ip = ip;
    oldest->fail_count = 0;
    oldest->locked_until = 0;
    return oldest;
}

// Returns the seconds remaining in this source's lockout (0 if not locked).
// A lockout whose window has just elapsed is rearmed here at one failure
// below the threshold rather than left at its accumulated fail_count: a
// source that keeps presenting the same stale credentials after every
// window expires this way re-triggers only the base lockout duration each
// time, instead of resuming the exponential backoff from wherever it left
// off and ratcheting straight to the cap.
static int web_auth_lockout_remaining_s(uint32_t ip) {
    for (int i = 0; i < WEB_AUTH_TRACK_SLOTS; i++) {
        if (s_auth_track[i].ip == ip && s_auth_track[i].locked_until > 0) {
            int64_t remaining_us = s_auth_track[i].locked_until - esp_timer_get_time();
            if (remaining_us > 0)
                return (int)(remaining_us / 1000000) + 1;
            s_auth_track[i].locked_until = 0;
            s_auth_track[i].fail_count = WEB_AUTH_MAX_ATTEMPTS - 1;
            return 0;
        }
    }
    return 0;
}

static void web_auth_note_failure(uint32_t ip) {
    web_auth_track_t *t = web_auth_track_find(ip);
    if (t->fail_count < UINT16_MAX)
        t->fail_count++;

    if (t->fail_count >= WEB_AUTH_MAX_ATTEMPTS) {
        // Every failure beyond the threshold doubles the lockout, so a
        // client that keeps hammering the lockout window (rather than
        // waiting it out) backs off exponentially instead of retrying at a
        // fixed cadence.
        uint32_t over = t->fail_count - WEB_AUTH_MAX_ATTEMPTS;
        uint32_t shift = over > 8 ? 8 : over; // cap the shift so it can't overflow
        uint32_t lockout_s = WEB_AUTH_LOCKOUT_BASE_S << shift;
        if (lockout_s > WEB_AUTH_LOCKOUT_MAX_S)
            lockout_s = WEB_AUTH_LOCKOUT_MAX_S;
        t->locked_until = esp_timer_get_time() + (int64_t)lockout_s * 1000000;
        ESP_LOGW(TAG, "Web admin login: %u consecutive failures, locked out for %u s", (unsigned)t->fail_count, (unsigned)lockout_s);
    }
}

static void web_auth_note_success(uint32_t ip) {
    for (int i = 0; i < WEB_AUTH_TRACK_SLOTS; i++) {
        if (s_auth_track[i].ip == ip) {
            s_auth_track[i].ip = 0;
            s_auth_track[i].fail_count = 0;
            s_auth_track[i].locked_until = 0;
            return;
        }
    }
}

// ------------------------------------------------------------- CSRF (Origin/Referer)
//
// Every admin route is gated only by HTTP Basic Auth, and browsers attach
// cached Basic credentials to *any* request sent to this device's origin -
// regardless of which page's script or hidden auto-submitting form actually
// triggered it. Without a same-origin check, a page on a completely
// different site, opened by an already-authenticated admin in another tab,
// could silently POST to e.g. /system or /format and have it succeed.
// There's no per-session token to check instead (this server has no
// cookies/sessions at all - only stateless Basic Auth), so the mitigation
// here is a same-origin check on state-changing (POST) requests: compare
// the browser-supplied Origin (or, failing that, Referer) header's host
// against this request's own Host header. Modern browsers always attach an
// Origin header to POST requests, same-origin or not, so a legitimate
// same-site form submission always has one to check; a request with
// neither header, or one whose host doesn't match, cannot be trusted to be
// same-origin and is rejected.
//
// This only ever needs to run for state-changing requests, and that rests
// on one invariant the route table has to keep: no registered GET route may
// have a side effect. It cannot be enforced by extending the check to GETs
// instead - a browser sends neither Origin nor Referer when a URL is typed
// into the address bar or opened from a bookmark, so a same-origin check on
// GET would fail closed on ordinary navigation and lock the admin UI out
// entirely. The obligation therefore sits on whoever adds a route: anything
// that changes state, keys the radio, reconfigures an interface or writes
// flash is registered HTTP_POST, which both brings it under this check and
// puts it out of reach of the ways a browser fetches a URL by itself
// (<img src>, script/stylesheet loads, prefetch, link prerender). See
// page_radio.c's /radio/looptest and page_wireless.c's /wifiscan for two
// endpoints that read like queries but are POST for exactly this reason,
// and page_storage.c for why /delete etc. are POST-only forms.

// Compares the host[:port] authority component of a "<scheme>://host[:port]/..."
// header value (Origin or Referer) against this request's own Host header
// value. Only the authority component of hdr_value is considered, so a
// Referer's path/query can't cause a false match or mismatch.
static bool web_origin_host_matches(const char *hdr_value, const char *expected_host) {
    const char *authority = strstr(hdr_value, "://");
    if (!authority)
        return false;
    authority += 3;

    size_t host_len = strlen(expected_host);
    if (host_len == 0)
        return false;
    if (strncasecmp(authority, expected_host, host_len) != 0)
        return false;

    // The matched prefix must be the *whole* authority component, not just a
    // prefix of a longer host (e.g. expected "example.com" must not match
    // "example.com.evil.tld"): what follows has to end the authority, i.e.
    // be the path/query/fragment separator or the end of the string.
    char term = authority[host_len];
    return term == 0 || term == '/' || term == '?' || term == '#';
}

// Returns true only if this state-changing request can be confirmed
// same-origin via Origin (preferred) or Referer (fallback). Fails closed:
// a missing Host header, or a request with neither Origin nor Referer, is
// treated as not-same-origin rather than silently allowed through.
static bool web_check_csrf_origin(httpd_req_t *req) {
    char host[128];
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK || host[0] == 0)
        return false;

    char origin[256];
    if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) == ESP_OK && origin[0] != 0)
        return web_origin_host_matches(origin, host);

    char referer[256];
    if (httpd_req_get_hdr_value_str(req, "Referer", referer, sizeof(referer)) == ESP_OK && referer[0] != 0)
        return web_origin_host_matches(referer, host);

    return false; // neither header present: can't confirm same-origin
}

// ---------------------------------------------------------------- Basic Auth
bool web_check_auth(httpd_req_t *req) {
    if (g_config.http_username[0] == 0)
        return true; // auth disabled if no user set

    // Same-origin check first, before Basic Auth is even evaluated: a
    // cross-site request has no business reaching this handler regardless of
    // whether it happens to carry valid cached credentials.
    if (req->method == HTTP_POST && !web_check_csrf_origin(req)) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "text/html");
        httpd_resp_sendstr(req, "<h1>" TR_FORBIDDEN_CSRF "</h1>");
        return false;
    }

    uint32_t client_ip = web_client_ipv4(req);
    int lockout_remaining_s = web_auth_lockout_remaining_s(client_ip);
    if (lockout_remaining_s > 0) {
        // Reject before even looking at the Authorization header: a locked
        // source doesn't get another guess to spend, and doesn't get any
        // extra timing signal either.
        httpd_resp_set_status(req, "429 Too Many Requests");
        char retry_hdr[16];
        snprintf(retry_hdr, sizeof(retry_hdr), "%d", lockout_remaining_s);
        httpd_resp_set_hdr(req, "Retry-After", retry_hdr);
        httpd_resp_set_type(req, "text/html");
        httpd_resp_sendstr(req, "<h1>" TR_UNAUTHORIZED "</h1>");
        return false;
    }

    char hdr[160];
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK) {
        goto challenge; // no Authorization header: first half of the Basic handshake, not a guess
    }
    if (strncmp(hdr, "Basic ", 6) != 0)
        goto challenge; // not a Basic credential: same as no header for counting purposes

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

        // Constant-time compare: strcmp() short-circuits on the first
        // mismatching byte, which leaks (via response timing) how many
        // leading characters of a guess were correct. Both fields are
        // fixed-size buffers in g_config, so comparing the full field width
        // costs nothing here.
        if (web_const_time_streq(user, g_config.http_username) && web_const_time_streq(pass, g_config.http_password)) {
            web_auth_note_success(client_ip);
            return true;
        }
    }

need_auth:
    // Reached only when credentials were actually presented and rejected:
    // a malformed Basic payload, or a user/password mismatch. This is the
    // one case that counts as a guess against the lockout budget.
    web_auth_note_failure(client_ip);

challenge:
    // Sends the 401 challenge without touching the lockout counter: every
    // browser reaches this on the first, credential-less half of the Basic
    // Auth handshake, so it must never be charged against the budget that
    // protects the real guesses handled above.
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"" APRS_SOFTWARE_NAME "\"");
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
// CR, LF and NUL are dropped as they are decoded rather than passed through:
// every caller of web_form_get() eventually writes the decoded value into a
// line-oriented output (an APRS-IS line, the AX.25 TNC2 text form, a JSON
// config value), and %0D/%0A let an operator smuggle either byte past a
// plain-text form field. Filtering here, at the one place every POSTed form
// value in this firmware is decoded, closes that path for every field
// without relying on each page's own POST handler to do it separately.
void web_urldecode(const char *src, char *dst, size_t dst_size) {
    size_t di = 0;
    while (*src && di + 1 < dst_size) {
        char c;
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = { src[1], src[2], 0 };
            c = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            c = ' ';
            src++;
        } else {
            c = *src++;
        }
        if (!str_is_line_break_char(c))
            dst[di++] = c;
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
#define MP_BUF_CAP    4096
#define MP_MAX_HEADER 512
#define MP_MAX_PARTS  32 // sanity cap against a pathological/adversarial body

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

#define MP_FILL()                                                                                                                                              \
    do {                                                                                                                                                       \
        while (buf_len < MP_BUF_CAP && remaining > 0) {                                                                                                        \
            int want = (int)(MP_BUF_CAP - buf_len);                                                                                                            \
            if (want > remaining)                                                                                                                              \
                want = remaining;                                                                                                                              \
            int r = httpd_req_recv(req, (char *)buf + buf_len, want);                                                                                          \
            if (r > 0) {                                                                                                                                       \
                buf_len += (size_t)r;                                                                                                                          \
                remaining -= r;                                                                                                                                \
            } else if (r == HTTPD_SOCK_ERR_TIMEOUT) {                                                                                                          \
                continue;                                                                                                                                      \
            } else {                                                                                                                                           \
                goto done;                                                                                                                                     \
            }                                                                                                                                                  \
        }                                                                                                                                                      \
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
        if (hdrblock_len >= sizeof(headers)) {
            // Don't silently truncate: a truncated copy could miss the
            // Content-Disposition/filename entirely (has_filename would come
            // back false even though the part is really a file part),
            // producing a confusing partial/failed upload instead of a clear
            // error. A legitimate browser upload's per-part headers are a
            // few dozen bytes; anything this large is malformed or hostile.
            goto done;
        }
        memcpy(headers, buf, hdrblock_len);
        headers[hdrblock_len] = 0;
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
#ifdef ENABLE_STATION
    { "/station", TR_MENU_STATION, "station" },
#endif
#ifdef ENABLE_MSG_CHAT
    { "/msgchat", TR_MENU_MSGCHAT, "msgchat" },
#endif
#ifdef ENABLE_BULLETINS
    { "/bulletins", TR_MENU_BULLETINS, "bulletins" },
#endif
#ifdef ENABLE_OBJECTS_ITEMS
    { "/objects", TR_MENU_OBJITEMS, "objects" },
#endif
#ifdef ENABLE_RADIO_MODEM
    { "/radio", TR_MENU_RADIO, "radio" },
#endif
#ifdef ENABLE_MESSAGE
    { "/msg", TR_MENU_MSG, "msg" },
#endif
#ifdef ENABLE_QUERY
    { "/query", TR_MENU_QUERY, "query" },
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
#ifdef ENABLE_GPS
    { "/gps", TR_MENU_GPS, "gps" },
#endif
#ifdef ENABLE_SYSTEM
    { "/system", TR_MENU_SYSTEM, "system" },
#endif
#ifdef ENABLE_WIRELESS
    { "/wireless", TR_MENU_WIRELESS, "wireless" },
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
                                  "<title>" APRS_SOFTWARE_NAME "</title></head><body>"
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
    httpd_resp_sendstr_chunk(
        req, "<script>function togglePwd(id,cb){var el=document.getElementById(id);if(el){el.type=(cb&&cb.checked)?'text':'password';}}</script>");
    httpd_resp_sendstr_chunk(req, "</main></div></body></html>");
    httpd_resp_sendstr_chunk(req, NULL); // end chunked response
}

void web_send_save_result(httpd_req_t *req, bool ok, const char *location) {
    // Sized for the longest translation of either body plus two copies of the
    // location, so neither branch can be truncated by snprintf().
    char buf[512];

    if (ok) {
        snprintf(buf, sizeof(buf),
                 "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                 "<meta http-equiv='refresh' content='1;url=%s'></head>"
                 "<body>" TR_SAVED_REDIRECT "</body></html>",
                 location);
    } else {
        // No meta refresh on this branch: the page the user came from is
        // rendered from the live settings, so bouncing straight back to it
        // would redisplay exactly what was typed and read as a success. The
        // failure stays on screen until the operator follows the link.
        snprintf(buf, sizeof(buf),
                 "<!DOCTYPE html><html><head><meta charset='utf-8'></head>"
                 "<body><p style='color:#cf222e;font-weight:600'>" TR_SAVE_FAILED "</p>"
                 "<p><a href='%s'>&larr; %s</a></p></body></html>",
                 location, location);
    }

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
        "#trafficTable th:nth-child(4),#trafficTable td:nth-child(4){max-width:420px;}"
        // Chat panel (Snd/Rcv Msg page): its height is set from the page's own
        // script, to the exact height of the last few message bubbles, so the
        // panel shows that many messages and scrolls through the rest of the
        // stored conversation. The bounds here only frame that: min-height
        // keeps the panel a readable size while the conversation is empty or
        // one line long, max-height keeps tall messages from pushing the
        // compose row off a short screen.
        ".chat-box{min-height:120px;max-height:70vh;overflow-y:auto;display:flex;flex-direction:column;"
        "gap:8px;padding:10px;background:var(--bg);border:1px solid var(--border);border-radius:8px;}"
        ".chat-empty{color:var(--sub);font-size:.85em;text-align:center;padding:20px 0;}"
        ".chat-bubble{max-width:78%;padding:8px 12px;border-radius:12px;font-size:.85em;word-break:break-word;}"
        ".chat-bubble .chat-meta{display:block;font-size:.75em;opacity:.7;margin-bottom:3px;}"
        ".chat-bubble.rx{align-self:flex-start;background:#e8e7e3;color:var(--text);border-bottom-left-radius:2px;}"
        ".chat-bubble.tx{align-self:flex-end;background:var(--accent);color:#fff;border-bottom-right-radius:2px;}"
        ".chat-bubble.tx.pending{background:#7d9be8;}"
        ".chat-compose{margin-top:14px;}"
        ".chat-compose .row{align-items:flex-start;}"
        ".chat-counter{font-size:.72em;color:var(--sub);text-align:right;margin-top:2px;}"
        "#msgChatStatus{font-size:.8em;margin-top:8px;display:block;}"
        // Collapsible analog-channel accordion (Telemetry page): one card per
        // channel, header always visible (tag + name + live value + caret),
        // body only rendered for the currently-open channel.
        ".faint{color:var(--sub);opacity:.8;}"
        ".achan{border:1px solid var(--border);border-radius:8px;margin-bottom:10px;overflow:hidden;background:var(--bg);}"
        ".achan:last-child{margin-bottom:0;}"
        ".achan-head{display:flex;align-items:center;gap:10px;padding:11px 12px;cursor:pointer;user-select:none;min-height:40px;}"
        ".achan-head .achan-tag{font-weight:700;color:var(--accent);font-size:.9em;width:28px;flex:none;}"
        ".achan-head .achan-name{flex:1;color:var(--text);font-size:.9em;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}"
        ".achan-head .achan-val{font-variant-numeric:tabular-nums;color:#92400e;font-size:.9em;min-width:70px;text-align:right;flex:none;}"
        ".achan-head .achan-caret{color:var(--sub);font-size:10px;transition:transform .15s;flex:none;}"
        ".achan.open .achan-caret{transform:rotate(90deg);}"
        ".achan-body{display:none;padding:14px;border-top:1px solid var(--border);background:var(--card);}"
        ".achan.open .achan-body{display:block;}"
        ".eqn-preview{margin-top:6px;font-size:.85em;color:var(--sub);background:var(--bg);"
        "border:1px solid var(--border);border-radius:6px;padding:6px 8px;}"
        ".eqn-preview b{color:var(--accent);}"
        "@media (max-width:420px){.achan-head{flex-wrap:wrap;row-gap:4px;}.achan-head .achan-name{flex-basis:100%;order:3;white-space:normal;}}";
    httpd_resp_set_type(req, "text/css");
    return httpd_resp_sendstr(req, css);
}

// ---------------------------------------------------------------- Field helpers
void web_raw(httpd_req_t *req, const char *html) {
    httpd_resp_sendstr_chunk(req, html);
}

// Copy a label or legend into `dst` for rendering, keeping at most
// WEB_LABEL_MAX_BYTES bytes of it. The translation tables are UTF-8, so an
// accented Spanish or Italian character spends two bytes; cutting at a fixed
// byte count could leave a dangling lead or continuation byte that renders as
// a replacement glyph. Backing up over any trailing continuation bytes
// (0b10xxxxxx) and then over their lead byte keeps the copy valid text.
// Every emitter below routes its label through this, so one over-long label
// cannot render differently depending on which control it lands in.
static void label_clamp(char dst[WEB_LABEL_MAX_BYTES + 1], const char *src) {
    if (!src) {
        dst[0] = 0;
        return;
    }
    size_t n = strlen(src);
    if (n > WEB_LABEL_MAX_BYTES) {
        n = WEB_LABEL_MAX_BYTES;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80)
            n--;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

void web_fieldset_open(httpd_req_t *req, const char *legend) {
    char leg[WEB_LABEL_MAX_BYTES + 1];
    label_clamp(leg, legend);
    char buf[WEB_LABEL_MAX_BYTES + 64];
    snprintf(buf, sizeof(buf), "<fieldset><legend>" WEB_LABEL_FMT "</legend>", leg);
    httpd_resp_sendstr_chunk(req, buf);
}

void web_fieldset_close(httpd_req_t *req) {
    httpd_resp_sendstr_chunk(req, "</fieldset>");
}

// Both helpers below render arbitrary, saved user/attacker text (e.g.
// free-text comments, or a WiFi SSID copied from an over-the-air scan
// result) back into an HTML attribute on every GET of the owning page. That
// text is HTML-escaped here, once, so no caller has to remember to do it:
// escaping at only some call sites would let a quote/angle-bracket character
// in a comment, status, filter, hostname or WiFi SSID field break out of
// value='...' and inject markup/script that then ran in the admin's
// authenticated session on the next page load.
void web_field_text(httpd_req_t *req, const char *label, const char *name, const char *value, int maxlen) {
    char esc[512];
    web_html_attr_escape(value ? value : "", esc, sizeof(esc));
    char lbl[WEB_LABEL_MAX_BYTES + 1];
    label_clamp(lbl, label);
    char buf[WEB_LABEL_MAX_BYTES + 576];
    snprintf(buf, sizeof(buf), "<label>" WEB_LABEL_FMT "</label><input type='text' name='%.30s' value='%.400s' maxlength='%d'>", lbl, name, esc, maxlen);
    httpd_resp_sendstr_chunk(req, buf);
}

// Both numeric emitters below always render the field's accepted range as HTML
// min/max attributes, so every numeric input on every page is validated by the
// browser before the form is submitted. The bounds are supplied by the caller
// because they belong to the field's own value domain, and the caller is what
// also clamps the posted value server-side: the browser check is the first
// line of defence against a typo, the handler's clamp is the one that holds
// against a crafted POST.
void web_field_int(httpd_req_t *req, const char *label, const char *name, long value, long min, long max) {
    char lbl[WEB_LABEL_MAX_BYTES + 1];
    label_clamp(lbl, label);
    char buf[WEB_LABEL_MAX_BYTES + 192];
    snprintf(buf, sizeof(buf), "<label>" WEB_LABEL_FMT "</label><input type='number' name='%.30s' value='%ld' min='%ld' max='%ld'>", lbl, name, value, min,
             max);
    httpd_resp_sendstr_chunk(req, buf);
}

void web_field_float(httpd_req_t *req, const char *label, const char *name, float value, const char *step, float min, float max) {
    char lbl[WEB_LABEL_MAX_BYTES + 1];
    label_clamp(lbl, label);
    char buf[WEB_LABEL_MAX_BYTES + 192];
    snprintf(buf, sizeof(buf), "<label>" WEB_LABEL_FMT "</label><input type='number' step='%.10s' name='%.30s' value='%g' min='%g' max='%g'>", lbl,
             step ? step : "0.01", name, (double)value, (double)min, (double)max);
    httpd_resp_sendstr_chunk(req, buf);
}

void web_field_checkbox(httpd_req_t *req, const char *label, const char *name, bool checked) {
    char lbl[WEB_LABEL_MAX_BYTES + 1];
    label_clamp(lbl, label);
    char buf[WEB_LABEL_MAX_BYTES + 128];
    snprintf(buf, sizeof(buf), "<label><input type='checkbox' name='%.30s' %s> " WEB_LABEL_FMT "</label>", name, checked ? "checked" : "", lbl);
    httpd_resp_sendstr_chunk(req, buf);
}

void web_field_path_checkboxes(httpd_req_t *req, const char *name_prefix, uint8_t mask) {
    char buf[160];
    snprintf(buf, sizeof(buf), "<label>%s</label>", TR_F_PATH);
    httpd_resp_sendstr_chunk(req, buf);

    char aliases[4][72];
    app_config_lock();
    for (int i = 0; i < 4; i++) {
        strncpy(aliases[i], g_config.path[i], sizeof(aliases[i]) - 1);
        aliases[i][sizeof(aliases[i]) - 1] = 0;
    }
    app_config_unlock();

    for (int k = 0; k < 4; k++) {
        // Sized to hold the escaped alias plus the "Path N (...)" wrapper so
        // the ESP-IDF -Wformat-truncation error cannot fire; the checkbox
        // helper caps the displayed label length itself.
        char plabel[72 * 6 + 32];
        if (aliases[k][0]) {
            char pesc[72 * 6 + 1];
            web_html_attr_escape(aliases[k], pesc, sizeof(pesc));
            snprintf(plabel, sizeof(plabel), TR_F_OBJITEM_PATH_FMT " (%s)", k + 1, pesc);
        } else {
            snprintf(plabel, sizeof(plabel), TR_F_OBJITEM_PATH_FMT, k + 1);
        }
        char name[48];
        snprintf(name, sizeof(name), "%.30s%d", name_prefix, k + 1);
        web_field_checkbox(req, plabel, name, (mask & (1u << k)) != 0);
    }
}

uint8_t web_form_get_path_mask(const char *body, const char *name_prefix) {
    uint8_t mask = 0;
    for (int k = 0; k < 4; k++) {
        char name[48];
        snprintf(name, sizeof(name), "%.30s%d", name_prefix, k + 1);
        if (web_form_get_bool(body, name))
            mask |= (uint8_t)(1u << k);
    }
    return mask;
}

void web_select_open(httpd_req_t *req, const char *label, const char *name) {
    char lbl[WEB_LABEL_MAX_BYTES + 1];
    label_clamp(lbl, label);
    char buf[WEB_LABEL_MAX_BYTES + 128];
    snprintf(buf, sizeof(buf), "<label>" WEB_LABEL_FMT "</label><select name='%.30s'>", lbl, name);
    httpd_resp_sendstr_chunk(req, buf);
}

void web_select_option(httpd_req_t *req, int value, const char *label, bool selected) {
    web_select_option_state(req, value, label, selected, false);
}

void web_select_option_state(httpd_req_t *req, int value, const char *label, bool selected, bool disabled) {
    char buf[400];
    snprintf(buf, sizeof(buf), "<option value='%d' %s %s>%.300s</option>", value, selected ? "selected" : "", disabled ? "disabled" : "", label);
    httpd_resp_sendstr_chunk(req, buf);
}

void web_select_close(httpd_req_t *req) {
    httpd_resp_sendstr_chunk(req, "</select>");
}

// ---------------------------------------------------------------- GPIO registry
// Every GPIO field g_config currently has, grouped by the feature that owns
// it. This is the ONLY place that mapping lives - a page's GPIO <select>
// never has to know about another page's fields directly, it just asks this
// registry (via web_gpio_owner_tag()) whether a given pin is free.
//
// Entries whose feature has an on/off toggle are only reported while that
// toggle is enabled (a disabled feature doesn't really "hold" its pin); the
// always-on RF module / audio path / message alarm pins, and the sensor
// bus's compile-time-fixed I2C pins, are reported unconditionally.
int web_gpio_collect_used(const char *skip_tag, web_gpio_owner_t *out, int max) {
    int n = 0;

#define WEB_GPIO_ADD(gpio_value, owner_tag)                                                                                                                    \
    do {                                                                                                                                                       \
        int8_t _g = (int8_t)(gpio_value);                                                                                                                      \
        if (n < max && _g >= 0 && (!skip_tag || strcmp((owner_tag), skip_tag) != 0)) {                                                                         \
            out[n].gpio = _g;                                                                                                                                  \
            out[n].tag = (owner_tag);                                                                                                                          \
            n++;                                                                                                                                               \
        }                                                                                                                                                      \
    } while (0)

    // msg_alarm_gpio can hold a real pin number even while the Message Alarm
    // "Enable" checkbox is off (Save doesn't clear it, so re-enabling later
    // keeps the same pin) - only count it as reserved while actually enabled.
    if (g_config.msg_alarm_enable)
        WEB_GPIO_ADD(g_config.msg_alarm_gpio, "Message Alarm");

    // Audio front-end and PTT: all fixed at compile time
    // (esp32idf_radioamateur_modem_config.h / the top-level CMakeLists.txt),
    // always reserved - the modem's ADC/DAC/PTT trio is hardwired on the
    // board. PTT is a compile-time constant like its ADC/DAC siblings and is
    // reported the same way they are - always shown as "used" here so it
    // correctly greys out in every other GPIO picker (e.g. Message Alarm)
    // even though it has no web-admin field of its own.
    WEB_GPIO_ADD(MODEM_ADC_GPIO, "Radio Modem");
    WEB_GPIO_ADD(MODEM_DAC_GPIO, "Radio Modem");
    WEB_GPIO_ADD(MODEM_PTT_GPIO, "PTT");

    // Local sensor I2C bus: fixed at compile time (sensors_local_i2c.h),
    // always reserved regardless of any run-time enable flag and of which
    // sensor drivers are compiled in - the bus belongs to the board's wiring,
    // not to any one chip on it.
    WEB_GPIO_ADD(SENSORS_LOCAL_I2C_SDA_GPIO, "Sensor I2C");
    WEB_GPIO_ADD(SENSORS_LOCAL_I2C_SCL_GPIO, "Sensor I2C");

    // GNSS receiver serial port: fixed at compile time (gps.h), always
    // reserved. Both pins are listed, not just the one carrying sentences:
    // the transmit pin is physically wired to the module's input on this
    // board, so handing it to another peripheral would drive that input.
    WEB_GPIO_ADD(GPS_UART_RX_GPIO, "GPS");
    WEB_GPIO_ADD(GPS_UART_TX_GPIO, "GPS");

#undef WEB_GPIO_ADD
    return n;
}

const char *web_gpio_owner_tag(int gpio, const char *skip_tag) {
    web_gpio_owner_t used[WEB_GPIO_MAX_OWNERS];
    int n = web_gpio_collect_used(skip_tag, used, WEB_GPIO_MAX_OWNERS);
    for (int i = 0; i < n; i++) {
        if (used[i].gpio == gpio)
            return used[i].tag;
    }
    return NULL;
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

    char lbl[WEB_LABEL_MAX_BYTES + 1];
    label_clamp(lbl, label);

    char buf[WEB_LABEL_MAX_BYTES + 1536];
    snprintf(buf, sizeof(buf),
             "<label>" WEB_LABEL_FMT "</label>"
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
             lbl, name_prefix, name_prefix, code_num, table_num, TR_F_SYMBOL_TABLE, ids, ids, table_ch, ids, idc, TR_F_SYMBOL_CODE, idc, idc, sym_ch, ids, idc,
             TR_SYM_PICK_HINT, TR_BTN_PICK_SYMBOL);
    httpd_resp_sendstr_chunk(req, buf);

    // Tiny helper script: updates the graphical icon live as the user edits
    // the Table/Code inputs, without waiting for a page reload. Safe to emit
    // once per field; browsers just redefine the same function identically.
    static const char *script = "<script>function aprsSymUpd(t,c){"
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
    if (!got_t && !got_s) {
        char legacy[4] = { 0 };
        if (!legacy_name || !web_form_get(body, legacy_name, legacy, sizeof(legacy)))
            return;
        t[0] = legacy[0];
        s[0] = legacy[1];
    }

    // Both bytes are free text in the form, so they are bounded here as well
    // as in the configuration loader: the table identifier is one of the four
    // forms chapter 21 defines and the code is a printable character the
    // symbol tables are actually indexed by. A byte outside those sets is not
    // cosmetic - a digit in the table position of a compressed report makes
    // every receiver read the report as uncompressed, and a '_' in the code
    // position makes every classifier read the report as weather.
    out[0] = aprs_symbol_table_is_valid(t[0]) ? t[0] : APRS_SYMBOL_TABLE_DEFAULT;
    out[1] = aprs_symbol_code_is_valid(s[0]) ? s[0] : APRS_SYMBOL_CODE_DEFAULT;
    out[2] = 0;
}

void web_field_use_station_data(httpd_req_t *req, const char *checkbox_name, bool checked, const char *call_name, const char *lat_name, const char *lon_name,
                                const char *alt_name) {
    // Build each field's `document.querySelector(...)` expression (or the
    // literal "null" if the page doesn't have that field), then splice all
    // four into the script below in one go.
    char qcall[80] = "null", qlat[80] = "null", qlon[80] = "null", qalt[80] = "null";
    if (call_name)
        snprintf(qcall, sizeof(qcall), "document.querySelector(\"[name='%.30s']\")", call_name);
    if (lat_name)
        snprintf(qlat, sizeof(qlat), "document.querySelector(\"[name='%.30s']\")", lat_name);
    if (lon_name)
        snprintf(qlon, sizeof(qlon), "document.querySelector(\"[name='%.30s']\")", lon_name);
    if (alt_name)
        snprintf(qalt, sizeof(qalt), "document.querySelector(\"[name='%.30s']\")", alt_name);

    // Callsigns only ever contain [A-Z0-9-], so a plain copy into a JS
    // single-quoted string literal is safe here (no escaping needed), unlike
    // arbitrary free-text user input.
    char buf[2200];
    snprintf(buf, sizeof(buf),
             "<label><input type='checkbox' name='%.30s' id='%.30s' %s> " TR_USE_MY_STATION_DATA "</label>"
             "<script>(function(){"
             "function apply(){"
             "var cb=document.getElementById('%.30s');if(!cb)return;"
             "var on=cb.checked;"
             "var call=%s,lat=%s,lon=%s,alt=%s;"
             "if(call){if(on)call.value='%.9s';call.disabled=on;}"
             "if(lat){if(on)lat.value='%g';lat.disabled=on;}"
             "if(lon){if(on)lon.value='%g';lon.disabled=on;}"
             "if(alt){if(on)alt.value='%g';alt.disabled=on;}"
             "}"
             "document.addEventListener('DOMContentLoaded',function(){"
             "var cb=document.getElementById('%.30s');if(cb){cb.addEventListener('change',apply);apply();}"
             "});"
             "})();</script>",
             checkbox_name, checkbox_name, checked ? "checked" : "", checkbox_name, qcall, qlat, qlon, qalt, g_config.my_callsign, (double)g_config.my_lat,
             (double)g_config.my_lon, (double)g_config.my_alt, checkbox_name);
    httpd_resp_sendstr_chunk(req, buf);
}
