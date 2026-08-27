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

/**
 * @brief HTTP Basic Auth check against g_config.http_username /
 * g_config.http_password, combined with the same-origin (CSRF) check for
 * state-changing requests.
 *
 * The same-origin check and the Basic Auth check are independent controls:
 * the same-origin check runs first and unconditionally for every POST
 * request, regardless of whether @c g_config.http_username is set. Leaving
 * the username blank disables the password prompt only; it does not disable
 * the same-origin requirement.
 *
 * On failure this sends the 401 or 403 response itself (with the
 * @c WWW-Authenticate header for the 401 case), so the caller only has to
 * bail out.
 *
 * @param req Incoming request.
 * @return true if the request is authorized; false if it was rejected (401
 *         or 403 already sent).
 */
bool web_check_auth(httpd_req_t *req);

/**
 * @brief Read the whole request body (application/x-www-form-urlencoded) into
 * a caller-allocated buffer.
 *
 * @param req      Incoming request.
 * @param buf      Destination buffer.
 * @param buf_size Size of @p buf, in bytes.
 * @return Number of bytes read, or -1 on error / body too large for @p buf.
 */
int web_read_body(httpd_req_t *req, char *buf, size_t buf_size);

/**
 * @name POST body buffer sizes
 *
 * Most page @c _post() handlers read their form into a small stack buffer
 * (@c char @c body[N]; @c web_read_body(req,body,sizeof(body));), which is
 * self-sizing by construction - @c sizeof(body) can never drift out of sync
 * with the buffer itself.
 *
 * A few pages have forms too large for a comfortable stack allocation on the
 * httpd worker task and instead heap-allocate the buffer with @c malloc().
 * For those, use ONE named constant below for both the @c malloc() call and
 * the matching @c web_read_body() call, instead of repeating the same bare
 * number in two places in the .c file. That way the two can never drift out of
 * sync, and growing a page's form is a single, obvious, grep-able place to
 * update the size. Each is sized with headroom above that page's form as of
 * this writing (see the page's @c _post handler for the current field list);
 * if a page starts rejecting legitimate saves with "body too large" after new
 * fields are added, bump the matching constant here.
 * @{
 */
#define WEBCONFIG_POST_BUF_TLM                                                                                                                                 \
    9000 /**< page_tlm.c - telemetry form (Beacon + Report Parameters + Definition Messages + 5 analog channels + 8 digital bits).                             \
          */
#define WEBCONFIG_POST_BUF_BULLETINS                                                                                                                           \
    2800 /**< page_bulletins.c - 5 bulletins x (3 checkboxes + identifier + group + up to 67-char msg + interval + expire).                                    \
          */
#define WEBCONFIG_POST_BUF_OBJITEMS                                                                                                                            \
    6000 /**< page_objects.c - 5 objects/items x (checkboxes + name/type/active/scope + lat/lon + symbol + course/speed + compress + comment + area +          \
            signpost + freq/duplex/offset/tone + 4 path checkboxes + QRU + intervals + decay). */
#define WEBCONFIG_POST_BUF_TELEGRAM                                                                                                                            \
    3600 /**< page_telegram.c - enable + token + admin id + Mini App URL + 8 authorized users x (id + name + callsign) + 4 allowed group chats x (id + name).  \
          */
/** @} */

/**
 * @brief Find "key=value" inside an application/x-www-form-urlencoded blob
 * (POST body or query string) and URL-decode the value.
 *
 * @param body     The form/query blob to search.
 * @param key      Field name to look up.
 * @param out      Destination buffer for the decoded value.
 * @param out_size Size of @p out, in bytes (including the NUL terminator).
 * @return true if @p key was found (and decoded into @p out), false otherwise.
 */
bool web_form_get(const char *body, const char *key, char *out, size_t out_size);

/**
 * @brief Percent-encode @p src for safe use as one query-string value (e.g.
 * inside @c href='/delete?file=...').
 *
 * Leaves unreserved characters (alnum, @c - @c _ @c . @c ~) as-is and escapes
 * everything else (including spaces) as @c %XX.
 *
 * @param src      Source string.
 * @param dst      Destination buffer.
 * @param dst_size Size of @p dst, in bytes.
 */
void web_urlencode(const char *src, char *dst, size_t dst_size);

/**
 * @brief Escape @c & @c < @c > @c " @c ' into HTML entities so @p src can be
 * safely embedded in HTML text content or inside a double-quoted HTML
 * attribute without breaking out of either.
 *
 * @param src      Source string.
 * @param dst      Destination buffer.
 * @param dst_size Size of @p dst, in bytes.
 */
void web_html_attr_escape(const char *src, char *dst, size_t dst_size);

/**
 * @brief Reduce an untrusted (e.g. browser-supplied upload) filename to a
 * single safe path component.
 *
 * Strips any directory part, drops leading dots (no "." / ".." / hidden-file
 * games) and replaces anything outside @c [A-Za-z0-9._-\ ] with @c '_'. Falls
 * back to "upload.bin" if that leaves nothing usable.
 *
 * @param src      Untrusted filename.
 * @param dst      Destination buffer.
 * @param dst_size Size of @p dst, in bytes.
 */
void web_sanitize_filename(const char *src, char *dst, size_t dst_size);

/**
 * @brief Callback invoked with each chunk of an uploaded file part as it is
 * streamed off the socket by web_multipart_receive_file().
 *
 * @param cb_ctx Opaque context pointer supplied to web_multipart_receive_file().
 * @param data   Pointer to this chunk of the file's raw bytes.
 * @param len    Number of valid bytes at @p data.
 * @return ESP_OK to continue receiving; any other value aborts the transfer
 *         and is propagated back as the receiver's result (e.g. once
 *         esp_ota_write() fails).
 */
typedef esp_err_t (*web_multipart_data_cb_t)(void *cb_ctx, const uint8_t *data, size_t len);

/**
 * @brief Streaming multipart/form-data file receiver.
 *
 * Parses a multipart/form-data POST body directly off the socket (never
 * buffering the whole body in RAM), looking for the first part that carries a
 * non-empty @c filename="..." (i.e. an @c <input type='file'> the user
 * actually picked a file for). Every raw byte of that part's content is handed
 * to @p cb as it arrives, in order, so the caller can stream it straight into
 * something like esp_ota_write() without ever holding the full upload in
 * memory. Other parts (plain form fields, empty file inputs) are parsed and
 * skipped.
 *
 * @param req               Incoming request.
 * @param cb                Chunk callback invoked for the file part's bytes.
 * @param cb_ctx            Opaque context passed to @p cb.
 * @param filename_out      If non-NULL, receives the uploaded file's original
 *                          name from the Content-Disposition header.
 * @param filename_out_size Size of @p filename_out, in bytes.
 * @return ESP_OK on a fully streamed file part; ESP_ERR_NOT_FOUND for a
 *         well-formed body with no filename'd file part; ESP_ERR_INVALID_ARG
 *         for a missing/unparseable Content-Type or boundary; ESP_ERR_NO_MEM
 *         if the internal scratch buffer could not be allocated; ESP_FAIL for
 *         a malformed body, socket error, or a non-OK return from @p cb.
 */
esp_err_t web_multipart_receive_file(httpd_req_t *req, web_multipart_data_cb_t cb, void *cb_ctx, char *filename_out, size_t filename_out_size);

/**
 * @brief Convenience wrapper on web_form_get(): read a boolean checkbox field.
 * @param body Form/query blob.
 * @param key  Field name.
 * @return true if the field is present with value "on", "1" or "true".
 */
bool web_form_get_bool(const char *body, const char *key);

/**
 * @brief Convenience wrapper on web_form_get(): read an integer field.
 * @param body Form/query blob.
 * @param key  Field name.
 * @param def  Value returned when the field is absent or non-numeric.
 * @return The parsed integer, or @p def.
 */
int web_form_get_int(const char *body, const char *key, int def);

/**
 * @brief Convenience wrapper on web_form_get(): read a floating-point field.
 * @param body Form/query blob.
 * @param key  Field name.
 * @param def  Value returned when the field is absent or non-numeric.
 * @return The parsed float, or @p def.
 */
float web_form_get_float(const char *body, const char *key, float def);

/**
 * @brief URL-decode @p src into @p dst, dropping any decoded CR, LF or NUL
 * byte along the way.
 *
 * Every caller reaches this through web_form_get(), and every field
 * web_form_get() reads eventually ends up as one line of a line-oriented
 * output (an APRS-IS line, the AX.25 TNC2 text form, a JSON config value),
 * none of which escape an embedded line break. Filtering the three bytes out
 * here, at decode time, means a percent-encoded `%0D%0A` in any POSTed form
 * field is stripped the same way a literal CR/LF typed into it would be.
 *
 * @param src      Source (percent-encoded) string.
 * @param dst      Destination buffer; must be at least @c strlen(src)+1 bytes.
 * @param dst_size Size of @p dst, in bytes.
 */
void web_urldecode(const char *src, char *dst, size_t dst_size);

/**
 * @brief Read a callsign form field, clamped to at most 6 characters.
 *
 * Reads @p key like web_form_get(), then clamps the result to the 6-character
 * AX.25 limit so an over-long value can never later overflow a 7-byte
 * ax25_call_t.call field.
 *
 * @param body     Form/query blob.
 * @param key      Field name.
 * @param out      Destination buffer.
 * @param out_size Size of @p out, in bytes; must be >= 7.
 */
static inline void web_form_get_call(const char *body, const char *key, char *out, size_t out_size) {
    web_form_get(body, key, out, out_size);
    if (out_size > 6)
        out[6] = '\0';
}

/**
 * @name Shared numeric input ranges
 *
 * Inclusive bounds for the value domains that repeat across several admin
 * pages. Every numeric field is rendered by web_field_int()/web_field_float(),
 * which always emit @c min and @c max attributes, so these constants are what
 * the browser validates against - they are the first line of defence that
 * stops a typo from ever reaching the POST handler, with the handler's own
 * clamp (or the service's runtime bound) as the second. Keeping the recurring
 * domains here means one edit changes every page that shares them.
 *
 * The interval ceilings are the storage width of the interval fields they
 * bound - ::WEB_RANGE_INTERVAL_S_MAX for the @c uint16_t seconds fields
 * (beacons, status, weather, telemetry), ::WEB_RANGE_INTERVAL_LONG_S_MAX for
 * the @c uint32_t ones (bulletins, objects/items), where a full day is the
 * useful limit rather than the type's. The runtime floor and the "0 = use the
 * service default" convention are applied by sched_clamp_interval() in the
 * services themselves, so 0 stays a legal input here.
 * @{
 */
#define WEB_RANGE_SSID_MIN            0      /**< Lowest AX.25 SSID. */
#define WEB_RANGE_SSID_MAX            15     /**< Highest AX.25 SSID. */
#define WEB_RANGE_INTERVAL_S_MIN      0      /**< Lowest transmit interval, seconds (0 = service default / off). */
#define WEB_RANGE_INTERVAL_S_MAX      65535  /**< Highest transmit interval for a uint16_t field, seconds. */
#define WEB_RANGE_INTERVAL_LONG_S_MAX 86400  /**< Highest transmit interval for a uint32_t field, seconds (24 h). */
#define WEB_RANGE_LAT_MIN             (-90)  /**< Southernmost latitude, decimal degrees. */
#define WEB_RANGE_LAT_MAX             90     /**< Northernmost latitude, decimal degrees. */
#define WEB_RANGE_LON_MIN             (-180) /**< Westernmost longitude, decimal degrees. */
#define WEB_RANGE_LON_MAX             180    /**< Easternmost longitude, decimal degrees. */
#define WEB_RANGE_ALT_M_MIN           (-500) /**< Lowest altitude, meters (below the Dead Sea shore). */
#define WEB_RANGE_ALT_M_MAX           10000  /**< Highest altitude, meters (above Everest, below airliner cruise). */
/** @} */

/**
 * @brief Read an SSID form field, clamped to the valid AX.25 range 0..15.
 * @param body Form/query blob.
 * @param key  Field name.
 * @param def  Value used when the field is absent or non-numeric.
 * @return The SSID, clamped to ::WEB_RANGE_SSID_MIN .. ::WEB_RANGE_SSID_MAX.
 */
static inline uint8_t web_form_get_ssid(const char *body, const char *key, uint8_t def) {
    int v = web_form_get_int(body, key, def);
    if (v < WEB_RANGE_SSID_MIN)
        v = WEB_RANGE_SSID_MIN;
    if (v > WEB_RANGE_SSID_MAX)
        v = WEB_RANGE_SSID_MAX;
    return (uint8_t)v;
}

/**
 * @brief Send the common HTML shell opening: @c <head>, top bar and sidebar.
 *
 * The page must close @c </div></div></body></html> itself via
 * web_send_footer(), or just use a single-shot pattern.
 *
 * @param req         Incoming request.
 * @param title       Page title (shown in the browser tab and header).
 * @param active_menu Sidebar menu id to highlight as the current page.
 */
void web_send_header(httpd_req_t *req, const char *title, const char *active_menu);

/**
 * @brief Send the common HTML shell closing that matches web_send_header().
 * @param req Incoming request.
 */
void web_send_footer(httpd_req_t *req);

/**
 * @brief Send the outcome of a settings save as the response to a POST
 * handler.
 *
 * On success this is the small "saved, redirecting..." page that bounces the
 * browser back to @p location after a second. On failure it is an error body
 * carrying ::TR_SAVE_FAILED and a plain link back to @p location, with no
 * automatic redirect, so the operator sees that the values now on screen are
 * the ones in RAM and not the ones on flash.
 *
 * This is the only correct response for handlers that persist anything: the
 * page they render afterwards is built from the live settings and would look
 * identical whether or not the write reached flash.
 *
 * @param req      Incoming request.
 * @param ok       Result reported by the save/format call being answered for.
 * @param location URL the browser is sent back to.
 */
void web_send_save_result(httpd_req_t *req, bool ok, const char *location);

/**
 * @brief Serve the shared stylesheet (the CSS referenced by web_send_header()).
 * @param req Incoming request.
 * @return ESP_OK or an esp_err_t error.
 */
esp_err_t web_handle_css(httpd_req_t *req);

/**
 * @brief Longest label or fieldset legend, in BYTES, that the form-field
 * emitters below render in full.
 *
 * Every emitter renders its label into a fixed-size stack buffer, so the label
 * has to carry a length bound the compiler can see; this constant is that
 * bound, shared by all of them so no single helper can silently clip a label
 * the others render whole. It is sized against the longest label in any
 * shipped translation - currently @c TR_F_QUERY_EXT, which lists the whole
 * extended directed-query set - with room for a translation to grow.
 *
 * The unit is bytes, not characters: the tables are UTF-8, so an accented
 * Spanish or Italian label spends two bytes on some characters. Clipping is
 * done on a character boundary, never mid-sequence, so a label that ever does
 * exceed this bound still renders as valid UTF-8.
 */
#define WEB_LABEL_MAX_BYTES 128

/** @cond INTERNAL */
#define WEB_STRINGIFY_(x) #x
#define WEB_STRINGIFY(x)  WEB_STRINGIFY_(x)
/** @endcond */

/**
 * @brief Conversion specifier for a label slot: a @c %s bounded by
 * ::WEB_LABEL_MAX_BYTES, spliced into an emitter's format string.
 *
 * The bound has to reach the compiler as a literal precision. Writing the
 * precision as a runtime @c "%.*s" argument instead makes GCC assume the slot
 * can emit up to @c INT_MAX bytes, which loses every @c -Wformat-truncation
 * guarantee these emitters are built to keep - and this project compiles that
 * warning as an error. Stringifying the constant keeps one definition of the
 * bound while still handing the compiler a literal it can reason about.
 */
#define WEB_LABEL_FMT "%." WEB_STRINGIFY(WEB_LABEL_MAX_BYTES) "s"

/**
 * @name Safe small-buffer form-field emitters
 *
 * Each helper uses its own small, generously-sized internal buffer, so no page
 * needs one giant @c snprintf that risks @c -Werror=format-truncation like the
 * earlier hand-rolled pages did. Together they render one field/control per
 * call inside a fieldset.
 *
 * Labels and legends are bounded at ::WEB_LABEL_MAX_BYTES bytes and each
 * helper's buffer is sized to hold a label of that length plus the widest
 * markup it can emit, so no in-tree label is ever clipped.
 * @{
 */

/** @brief Open a @c <fieldset> with a @c <legend>. @param req Request. @param legend Fieldset legend text. */
void web_fieldset_open(httpd_req_t *req, const char *legend);
/** @brief Close the fieldset opened by web_fieldset_open(). @param req Request. */
void web_fieldset_close(httpd_req_t *req);
/** @brief Render a labelled single-line text input. @param req Request. @param label Field label. @param name Form field name. @param value Current value.
 * @param maxlen HTML maxlength. */
void web_field_text(httpd_req_t *req, const char *label, const char *name, const char *value, int maxlen);
/**
 * @brief Render a labelled integer input, bounded client-side.
 *
 * @p min and @p max are emitted as the input's HTML @c min / @c max
 * attributes, so the browser refuses to submit an out-of-range value and the
 * spinner arrows stop at the bounds. That is validation, not enforcement - a
 * crafted POST bypasses it entirely - so the handler consuming the field
 * still clamps whatever it stores. Feed both from a shared constant
 * (::WEB_RANGE_SSID_MIN, ::RF_TX_BUFFERS_MAX, ...) wherever one exists, so
 * the form and the clamp behind it cannot drift apart.
 *
 * @param req   Request.
 * @param label Field label.
 * @param name  Form field name.
 * @param value Current value.
 * @param min   Lowest accepted value (inclusive).
 * @param max   Highest accepted value (inclusive).
 */
void web_field_int(httpd_req_t *req, const char *label, const char *name, long value, long min, long max);
/**
 * @brief Render a labelled floating-point input, bounded client-side.
 *
 * Same @c min / @c max contract as web_field_int(), with the bounds expressed
 * in the field's own units (degrees, MHz, km, ...).
 *
 * @param req   Request.
 * @param label Field label.
 * @param name  Form field name.
 * @param value Current value.
 * @param step  HTML step attribute (e.g. "0.01", or "any" to leave the value unquantized).
 * @param min   Lowest accepted value (inclusive).
 * @param max   Highest accepted value (inclusive).
 */
void web_field_float(httpd_req_t *req, const char *label, const char *name, float value, const char *step, float min, float max);
/** @brief Render a labelled checkbox. @param req Request. @param label Field label. @param name Form field name. @param checked Initial checked state. */
void web_field_checkbox(httpd_req_t *req, const char *label, const char *name, bool checked);
/** @brief Open a labelled @c <select>. @param req Request. @param label Field label. @param name Form field name. */
void web_select_open(httpd_req_t *req, const char *label, const char *name);
/** @brief Emit one @c <option> inside an open @c <select>. @param req Request. @param value Option value. @param label Option text. @param selected Whether
 * this option is currently selected. */
void web_select_option(httpd_req_t *req, int value, const char *label, bool selected);
/**
 * @brief Emit one @c <option>, additionally letting the caller mark it
 * disabled (greyed out / unselectable in the browser, e.g. because that GPIO
 * is already claimed elsewhere - see web_gpio_owner_tag()).
 * @param req      Request.
 * @param value    Option value.
 * @param label    Option text.
 * @param selected Whether this option is currently selected.
 * @param disabled Whether this option is rendered disabled.
 */
void web_select_option_state(httpd_req_t *req, int value, const char *label, bool selected, bool disabled);
/** @brief Close the @c <select> opened by web_select_open(). @param req Request. */
void web_select_close(httpd_req_t *req);
/** @brief Send a raw HTML fragment verbatim (sendstr_chunk passthrough). @param req Request. @param html HTML text to emit unescaped. */
void web_raw(httpd_req_t *req, const char *html);

/** @} */

/**
 * @brief Render one checkbox per configured Digipeater Path Alias
 * (g_config.path[0..3]).
 *
 * Renders under a "Path" label, reflecting/collecting the given bitmask
 * (bit N <-> g_config.path[N]). Shared by every page that stores a path
 * selection as a bitmask (Digipeater, Tracker, WX, Messaging, Telemetry,
 * Objects and Items) so they all render and behave identically instead of
 * duplicating the same loop.
 *
 * @param req         Incoming request.
 * @param name_prefix Prefix used to build the checkbox names ("<prefix>1".."<prefix>4").
 * @param mask        Current path bitmask to reflect as checked boxes.
 */
void web_field_path_checkboxes(httpd_req_t *req, const char *name_prefix, uint8_t mask);

/**
 * @brief Read back the bitmask written by web_field_path_checkboxes().
 *
 * Checks "<name_prefix>1".."<name_prefix>4" and ORs in bit (k-1) for each
 * present.
 *
 * @param body        POST body / query blob.
 * @param name_prefix Same prefix passed to web_field_path_checkboxes().
 * @return The reconstructed path bitmask.
 */
uint8_t web_form_get_path_mask(const char *body, const char *name_prefix);

/**
 * @brief Maximum number of GPIO owners the usage registry can report in one
 * pass (see web_gpio_collect_used()).
 */
#define WEB_GPIO_MAX_OWNERS 48

/**
 * @brief One entry of the GPIO usage registry: a pin and a short label of
 * whatever currently owns it.
 */
typedef struct {
    int8_t gpio;     /**< Pin number (0-39); never -1 (unassigned entries are skipped). */
    const char *tag; /**< Short human label of what is using it, e.g. "PTT", "Sensor I2C". */
} web_gpio_owner_t;

/**
 * @brief Collect every GPIO currently assigned somewhere in g_config.
 *
 * Single source of truth for "which GPIO is already assigned to what" across
 * the whole config, so any page's GPIO @c <select> can show every pin - not
 * just the ones it happens to accept - and grey out the ones another feature
 * is already using, labelled with that feature's name, instead of silently
 * hiding them. Add a line to this function's implementation whenever a new
 * GPIO field grows a web picker; every existing and future picker then takes
 * it into account automatically.
 *
 * @param skip_tag Owner tag to exclude (pass the tag of the field you are
 *                 rendering, so its own current value never blocks itself).
 * @param out      Caller-provided array of at least @p max entries.
 * @param max      Capacity of @p out.
 * @return Number of entries written (<= @p max).
 */
int web_gpio_collect_used(const char *skip_tag, web_gpio_owner_t *out, int max);

/**
 * @brief Convenience lookup for a single pin's owner.
 * @param gpio     Pin number to test.
 * @param skip_tag Owner tag to ignore (as in web_gpio_collect_used()).
 * @return The short owner tag if @p gpio is already assigned to something
 *         other than @p skip_tag, or NULL if the pin is free.
 */
const char *web_gpio_owner_tag(int gpio, const char *skip_tag);

/**
 * @brief Render the "Station Symbol" control (the same one used on the IGate
 * page) for any 2-char table+symbol field.
 *
 * Emits a live graphical icon of the currently selected symbol (auto-updated
 * via JS on input), Table + Symbol 1-char inputs, and a link to the /symbol
 * reference page. A NUL / missing byte in @p sym2 defaults to '/' and '&'.
 *
 * @param req         Incoming request.
 * @param label       Field label.
 * @param name_prefix Used to build the two input names, "<name_prefix>Table"
 *                    and "<name_prefix>Code" (e.g. "digiSymbol" ->
 *                    "digiSymbolTable" / "digiSymbolCode").
 * @param sym2        2-byte "<table><code>" value (e.g. g_config.igate_symbol).
 */
void web_field_symbol(httpd_req_t *req, const char *label, const char *name_prefix, const char *sym2);

/**
 * @brief Render a "Use My Station Data" checkbox plus the inline JS that binds
 * it to a page's callsign/lat/lon/alt fields.
 *
 * Placed right after a page's "enable" checkbox. On load and on every toggle
 * the JS: when checked, copies g_config.my_callsign/my_lat/my_lon/my_alt into
 * the given form fields and disables them for editing (so the page's own
 * values can't drift from Station); when unchecked, re-enables those same
 * fields for normal editing.
 *
 * @param req           Incoming request.
 * @param checkbox_name Form field name and DOM id for the checkbox itself
 *                      (e.g. "digiUseStation").
 * @param checked       Initial checked state.
 * @param call_name     @c name of the page's callsign input (or NULL if absent).
 * @param lat_name      @c name of the page's latitude input (or NULL).
 * @param lon_name      @c name of the page's longitude input (or NULL).
 * @param alt_name      @c name of the page's altitude input (or NULL).
 */
void web_field_use_station_data(httpd_req_t *req, const char *checkbox_name, bool checked, const char *call_name, const char *lat_name, const char *lon_name,
                                const char *alt_name);

/**
 * @brief Render a "Use GPS" checkbox plus the inline JS that binds it to a
 * page's latitude/longitude/altitude/speed/course fields and keeps them
 * filled live from the GNSS receiver.
 *
 * Placed next to a page's "Use My Station Data" checkbox (see
 * web_field_use_station_data()) wherever the page has at least one field the
 * receiver can supply. On load and on every toggle the JS: when checked,
 * disables the given fields for editing and starts polling @c GET /gps/live
 * once a second, writing each reported quantity into its field as it arrives
 * (a field the receiver has not reported yet - @c null in the response - is
 * left holding whatever value it last had, so a momentary drop-out does not
 * blank a field the operator is about to save); when unchecked, the polling
 * stops and the fields are re-enabled for normal editing. The two checkboxes
 * are mutually exclusive on a page that offers both: checking this one
 * unchecks and disables "Use My Station Data" and vice versa, since a field
 * can only be driven from one live source at a time.
 *
 * Latitude and longitude are rounded to 4 decimal places and altitude to 1
 * decimal place before being written into their fields, matching the @c step
 * attribute web_field_float() renders for those inputs. This keeps every
 * live-filled value compliant with its field's own HTML validation, so the
 * browser never blocks Save on a field the operator did not type into.
 *
 * This only wires up the client-side fill; it has no effect on what gets
 * POSTed or stored. The page's own POST handler is what must snapshot the
 * receiver's current values into the field's g_config member when this
 * checkbox is on, the same way it already does for "Use My Station Data"
 * (see gps_snapshot() in gps.h).
 *
 * @param req              Incoming request.
 * @param checkbox_name    Form field name and DOM id for the checkbox itself
 *                         (e.g. "digiUseGps").
 * @param checked          Initial checked state.
 * @param station_checkbox_name @c id of the page's "Use My Station Data"
 *                         checkbox to unselect when this one is checked (or
 *                         NULL if the page has none).
 * @param lat_name         @c name of the page's latitude input (or NULL).
 * @param lon_name         @c name of the page's longitude input (or NULL).
 * @param alt_name         @c name of the page's altitude input (or NULL).
 * @param speed_name       @c name of the page's speed input (or NULL).
 * @param course_name      @c name of the page's course input (or NULL).
 */
void web_field_use_gps_data(httpd_req_t *req, const char *checkbox_name, bool checked, const char *station_checkbox_name, const char *lat_name,
                            const char *lon_name, const char *alt_name, const char *speed_name, const char *course_name);

/**
 * @brief Parse the POST body produced by web_field_symbol() back into a 2-char
 * "<table><symbol>" value.
 *
 * Prefers the split "<name_prefix>Table" / "<name_prefix>Code" fields; falls
 * back to the given legacy combined field name (e.g. "digiSymbol") if neither
 * split field is present, so older/custom form submissions keep working.
 * Leaves @p out unchanged if nothing relevant is found in @p body.
 *
 * Both bytes are validated before they are stored, since the form accepts any
 * character the operator types: the table identifier must be one of the forms
 * APRS 1.2 chapter 21 defines ('/', '\\', 'A'-'Z' or '0'-'9') and the code
 * must be printable. A byte outside those sets is replaced with
 * ::APRS_SYMBOL_TABLE_DEFAULT or ::APRS_SYMBOL_CODE_DEFAULT respectively -
 * the configuration loader applies the same two bounds to what it reads from
 * flash.
 *
 * @param body        POST body.
 * @param name_prefix Same prefix passed to web_field_symbol().
 * @param legacy_name Fallback combined field name.
 * @param out         Destination buffer; @p out_size must be >= 3.
 * @param out_size    Size of @p out, in bytes.
 */
void web_form_get_symbol(const char *body, const char *name_prefix, const char *legacy_name, char *out, size_t out_size);

#endif // WEB_COMMON_H
