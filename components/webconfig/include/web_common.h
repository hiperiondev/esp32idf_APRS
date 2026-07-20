/**
 * @file web_common.h
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
 * @brief Shared web admin helpers used by every page: HTTP Basic authentication,
 * request body and form field parsing, and the common HTML chrome and stylesheet
 * emitters.
 */

#ifndef WEB_COMMON_H
#define WEB_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_http_server.h"

// ---- Auth -----------------------------------------------------------------
// HTTP Basic Auth check against g_config.http_username/http_password.
// Sends 401 + WWW-Authenticate itself and returns false if not authorized.
bool web_check_auth(httpd_req_t *req);

// ---- Body / query parsing --------------------------------------------------
// Reads the whole request body (application/x-www-form-urlencoded) into a
// caller-allocated buffer. Returns bytes read, or -1 on error/too large.
int web_read_body(httpd_req_t *req, char *buf, size_t buf_size);

// ---- POST body buffer sizes -------------------------------------------------
// Most page _post() handlers read their form into a small stack buffer
// (char body[N]; web_read_body(req, body, sizeof(body));), which is
// self-sizing by construction - sizeof(body) can never drift out of sync
// with the buffer itself.
//
// A few pages have forms too large for a comfortable stack allocation on
// the httpd worker task and instead heap-allocate the buffer with malloc().
// For those, use ONE named constant below for both the malloc() call and
// the matching web_read_body() call, instead of repeating the same bare
// number in two places in the .c file. That way the two can never drift out
// of sync, and growing a page's form is a single, obvious, grep-able place
// to update the size rather than a magic number easily missed during a
// later edit. Each is sized with headroom above that page's form as of this
// writing (see the page's _post handler for the current field list); if a
// page starts rejecting legitimate saves with "body too large" after new
// fields are added, bump the matching constant here.
#define WEBCONFIG_POST_BUF_MOD    3000 // page_mod.c    - RF/I2C/UART/PPP/power peripheral form
#define WEBCONFIG_POST_BUF_TLM    6000 // page_tlm.c    - telemetry configuration form
#define WEBCONFIG_POST_BUF_BULLETINS 2600 // page_bulletins.c - 5 bulletins x (3 checkboxes + up to 67-char msg + expire)

// Finds "key=value" inside an application/x-www-form-urlencoded blob (POST body
// or query string), URL-decodes it into out (out_size incl. NUL), returns true if found.
bool web_form_get(const char *body, const char *key, char *out, size_t out_size);

// Percent-encodes src for safe use as one query-string value (e.g. inside
// href='/delete?file=...'). Leaves unreserved characters (alnum, - _ . ~)
// as-is, escapes everything else (including spaces) as %XX.
void web_urlencode(const char *src, char *dst, size_t dst_size);

// Escapes & < > " ' into HTML entities, so src can be safely embedded in
// HTML text content or inside a double-quoted HTML attribute without
// breaking out of either.
void web_html_attr_escape(const char *src, char *dst, size_t dst_size);

// Reduces an untrusted (e.g. browser-supplied upload) filename to a single
// safe path component: strips any directory part, drops leading dots (no
// "." / ".." / hidden-file games), and replaces anything outside
// [A-Za-z0-9._- ] with '_'. Falls back to "upload.bin" if that leaves
// nothing usable.
void web_sanitize_filename(const char *src, char *dst, size_t dst_size);

// ---- Streaming multipart/form-data file receiver ---------------------------
// Parses a multipart/form-data POST body directly off the socket (never
// buffers the whole body in RAM), looking for the first part that carries a
// non-empty filename="..." (i.e. an <input type='file'> the user actually
// picked a file for). Every raw byte of that part's content is handed to
// `cb` as it arrives, in order, so the caller can stream it straight into
// something like esp_ota_write() without ever holding the full upload in
// memory. Other parts (plain form fields, empty file inputs) are parsed and
// skipped. filename_out (if non-NULL) receives the uploaded file's original
// name from the Content-Disposition header.
//
// Returns:
//   ESP_OK           - a file part was found and fully streamed to cb
//   ESP_ERR_NOT_FOUND - well-formed multipart body, but no file part had a
//                       filename (e.g. the user submitted with nothing chosen)
//   ESP_ERR_INVALID_ARG - missing/unparseable Content-Type or boundary
//   ESP_ERR_NO_MEM    - couldn't allocate the internal scratch buffer
//   ESP_FAIL          - malformed body, socket error, or cb returned non-OK
//                       (cb's return value is propagated here so it can abort
//                       the transfer, e.g. once esp_ota_write() fails)
typedef esp_err_t (*web_multipart_data_cb_t)(void *cb_ctx, const uint8_t *data, size_t len);

esp_err_t web_multipart_receive_file(httpd_req_t *req, web_multipart_data_cb_t cb, void *cb_ctx, char *filename_out, size_t filename_out_size);

// Convenience wrappers built on web_form_get:
bool web_form_get_bool(const char *body, const char *key); // "on"/"1"/"true" -> true
int web_form_get_int(const char *body, const char *key, int def);
float web_form_get_float(const char *body, const char *key, float def);

// URL-decode in place-ish (dst must be big enough, >= strlen(src)+1).
void web_urldecode(const char *src, char *dst, size_t dst_size);

// ---- AX.25 field clamps ----------------------------------------------------
// Reads a callsign form field like web_form_get(), then clamps the result to at
// most 6 characters (the AX.25 limit) so an over-long value can never later
// overflow a 7-byte ax25_call_t.call field. `out_size` must be >= 7.
static inline void web_form_get_call(const char *body, const char *key, char *out, size_t out_size) {
    web_form_get(body, key, out, out_size);
    if (out_size > 6)
        out[6] = '\0';
}

// Reads an SSID form field like web_form_get_int(), clamped to the valid AX.25
// range 0..15.
static inline uint8_t web_form_get_ssid(const char *body, const char *key, uint8_t def) {
    int v = web_form_get_int(body, key, def);
    if (v < 0)
        v = 0;
    if (v > 15)
        v = 15;
    return (uint8_t)v;
}

// ---- HTML shell -------------------------------------------------------------
// Sends the common <head>+top bar+sidebar opening HTML (page must close </div></div></body></html>
// itself via web_send_footer, or just use web_send_page for a single-shot send).
void web_send_header(httpd_req_t *req, const char *title, const char *active_menu);
void web_send_footer(httpd_req_t *req);

// Sends a small "saved, redirecting..." response used after POST handlers.
void web_send_saved_redirect(httpd_req_t *req, const char *location);

// Sends the shared stylesheet.
esp_err_t web_handle_css(httpd_req_t *req);

// ---- Safe small-buffer form-field helpers (each call uses its own small,
// generously-sized buffer, so no page needs one giant snprintf that risks
// -Werror=format-truncation like the earlier hand-rolled pages did). ----
void web_fieldset_open(httpd_req_t *req, const char *legend);
void web_fieldset_close(httpd_req_t *req);
void web_field_text(httpd_req_t *req, const char *label, const char *name, const char *value, int maxlen);
void web_field_password(httpd_req_t *req, const char *label, const char *name, const char *value, int maxlen);
void web_field_int(httpd_req_t *req, const char *label, const char *name, long value);
void web_field_float(httpd_req_t *req, const char *label, const char *name, float value, const char *step);
void web_field_checkbox(httpd_req_t *req, const char *label, const char *name, bool checked);
void web_select_open(httpd_req_t *req, const char *label, const char *name);
void web_select_option(httpd_req_t *req, int value, const char *label, bool selected);
void web_select_close(httpd_req_t *req);
void web_raw(httpd_req_t *req, const char *html); // sendstr_chunk passthrough

// ---- APRS symbol picker ------------------------------------------------
// Renders the same "Station Symbol" control used on the IGate page for any
// 2-char table+symbol field: a live graphical icon of the symbol currently
// selected (auto-updates via JS on input), Table + Symbol 1-char inputs, and
// a link to the /symbol reference page. `sym2` is the 2-byte "<table><code>"
// value (e.g. g_config.igate_symbol); a NUL / missing byte defaults to '/'
// and '&'. `name_prefix` is used to build the two input names:
// "<name_prefix>Table" and "<name_prefix>Code" (e.g. "digiSymbol" ->
// "digiSymbolTable" / "digiSymbolCode").
void web_field_symbol(httpd_req_t *req, const char *label, const char *name_prefix, const char *sym2);

// Renders a "Use My Station Data" checkbox (to be placed right after a page's
// "enable" checkbox) plus the inline JS that, on load and on every toggle:
//  - when checked, copies g_config.my_callsign/my_lat/my_lon/my_alt into the
//    given callsign/latitude/longitude/altitude form fields and disables
//    them for editing (so the page's own values can't drift from Station);
//  - when unchecked, re-enables those same fields for normal editing.
// `checkbox_name` is both the form field name and DOM id for the checkbox
// itself (e.g. "digiUseStation"). call_name/lat_name/lon_name/alt_name are
// the `name` attributes of that page's existing callsign/lat/lon/alt inputs
// (e.g. "digiMycall"/"digiLAT"/"digiLON"/"digiAlt"); pass NULL for any field
// the page doesn't have.
void web_field_use_station_data(httpd_req_t *req, const char *checkbox_name, bool checked, const char *call_name, const char *lat_name,
                                 const char *lon_name, const char *alt_name);

// Parses the POST body produced by web_field_symbol back into a 2-char
// "<table><symbol>" value at out (out_size >= 3). Prefers the split
// "<name_prefix>Table"/"<name_prefix>Code" fields; falls back to the given
// legacy combined field name (e.g. "digiSymbol") if neither split field is
// present, so older/custom form submissions keep working. Leaves *out
// unchanged if nothing relevant is found in body.
void web_form_get_symbol(const char *body, const char *name_prefix, const char *legacy_name, char *out, size_t out_size);

#endif
