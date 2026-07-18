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

#include "esp_http_server.h"

// ---- Auth -----------------------------------------------------------------
// HTTP Basic Auth check against g_config.http_username/http_password.
// Sends 401 + WWW-Authenticate itself and returns false if not authorized.
bool web_check_auth(httpd_req_t *req);

// ---- Body / query parsing --------------------------------------------------
// Reads the whole request body (application/x-www-form-urlencoded) into a
// caller-allocated buffer. Returns bytes read, or -1 on error/too large.
int web_read_body(httpd_req_t *req, char *buf, size_t buf_size);

// Finds "key=value" inside an application/x-www-form-urlencoded blob (POST body
// or query string), URL-decodes it into out (out_size incl. NUL), returns true if found.
bool web_form_get(const char *body, const char *key, char *out, size_t out_size);

// Convenience wrappers built on web_form_get:
bool web_form_get_bool(const char *body, const char *key); // "on"/"1"/"true" -> true
int web_form_get_int(const char *body, const char *key, int def);
float web_form_get_float(const char *body, const char *key, float def);

// URL-decode in place-ish (dst must be big enough, >= strlen(src)+1).
void web_urldecode(const char *src, char *dst, size_t dst_size);

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

// Parses the POST body produced by web_field_symbol back into a 2-char
// "<table><symbol>" value at out (out_size >= 3). Prefers the split
// "<name_prefix>Table"/"<name_prefix>Code" fields; falls back to the given
// legacy combined field name (e.g. "digiSymbol") if neither split field is
// present, so older/custom form submissions keep working. Leaves *out
// unchanged if nothing relevant is found in body.
void web_form_get_symbol(const char *body, const char *name_prefix, const char *legacy_name, char *out, size_t out_size);

#endif
