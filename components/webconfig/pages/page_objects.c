/**
 * @file page_objects.c
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
 * @brief Web admin "Objects and Items" page: edits the OBJITEM_COUNT APRS
 * Objects/Items. Each block has the three request-mandated toggles (Enable /
 * Send via RF / Send via Internet), plus the on-air parameters drawn from the
 * YAAC object editor (name, active/kill, Object-vs-Item, scope, latitude/
 * longitude, symbol+overlay, course/speed, comment, interval).
 *   https://www.ka2ddo.org/ka2ddo/YAACdocs/objecteditor.html
 *
 * Objects/Items live in their own LittleFS file (/storage/objitems.json), NOT
 * in g_config - see objects_items.h. This page loads/saves them through the
 * objitems_* API rather than touching g_config or app_config_save().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "objects_items.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

// Scope <select> for one element. Values match objitem_scope_t.
static void render_scope_select(httpd_req_t *req, const char *name, objitem_scope_t cur) {
    web_select_open(req, TR_F_OBJITEM_SCOPE, name);
    web_select_option(req, OBJITEM_SCOPE_PRIVATE, TR_F_OBJITEM_SCOPE_PRIVATE, cur == OBJITEM_SCOPE_PRIVATE);
    web_select_option(req, OBJITEM_SCOPE_LOCAL, TR_F_OBJITEM_SCOPE_LOCAL, cur == OBJITEM_SCOPE_LOCAL);
    web_select_option(req, OBJITEM_SCOPE_GLOBAL, TR_F_OBJITEM_SCOPE_GLOBAL, cur == OBJITEM_SCOPE_GLOBAL);
    web_select_close(req);
}

// Object-vs-Item <select> for one element. Value 1 => Item, 0 => Object.
static void render_type_select(httpd_req_t *req, const char *name, bool is_item) {
    web_select_open(req, TR_F_OBJITEM_TYPE, name);
    web_select_option(req, 0, TR_F_OBJITEM_TYPE_OBJECT, !is_item);
    web_select_option(req, 1, TR_F_OBJITEM_TYPE_ITEM, is_item);
    web_select_close(req);
}

esp_err_t page_objects_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    objitems_t set;
    objitems_load(&set);

    web_send_header(req, TR_F_OBJITEMS, "objects");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/objects'>");

    for (int i = 0; i < OBJITEM_COUNT; i++) {
        const objitem_t *b = &set.item[i];

        char legend[28];
        snprintf(legend, sizeof(legend), TR_F_OBJITEM_FMT, i + 1);
        web_fieldset_open(req, legend);

        char name[20];

        // -- The three request-mandated checks, first. --
        snprintf(name, sizeof(name), "oEn%d", i + 1);
        web_field_checkbox(req, TR_F_ENABLE, name, b->enable);
        snprintf(name, sizeof(name), "oRf%d", i + 1);
        web_field_checkbox(req, TR_F_SEND_VIA_RF, name, b->send_rf);
        snprintf(name, sizeof(name), "oInet%d", i + 1);
        web_field_checkbox(req, TR_F_SEND_VIA_INTERNET, name, b->send_inet);

        // -- Identity / state. --
        char esc[OBJITEM_NAME_MAX * 6 + 1];
        web_html_attr_escape(b->name, esc, sizeof(esc));
        snprintf(name, sizeof(name), "oName%d", i + 1);
        web_field_text(req, TR_F_OBJECT_ITEM_NAME, name, esc, OBJITEM_NAME_MAX);

        snprintf(name, sizeof(name), "oType%d", i + 1);
        render_type_select(req, name, b->is_item);

        snprintf(name, sizeof(name), "oAct%d", i + 1);
        web_field_checkbox(req, TR_F_OBJITEM_ACTIVE, name, b->active);

        snprintf(name, sizeof(name), "oScope%d", i + 1);
        render_scope_select(req, name, b->scope);

        // -- Position. --
        snprintf(name, sizeof(name), "oLat%d", i + 1);
        web_field_float(req, TR_F_FIXED_LATITUDE, name, b->lat, "0.0001");
        snprintf(name, sizeof(name), "oLon%d", i + 1);
        web_field_float(req, TR_F_FIXED_LONGITUDE, name, b->lon, "0.0001");

        // -- Symbol + overlay (2-char table+code control). --
        char sym2[3] = { b->sym[0] ? b->sym[0] : '/', b->sym[1] ? b->sym[1] : '-', 0 };
        snprintf(name, sizeof(name), "oSym%d", i + 1);
        web_field_symbol(req, TR_F_OBJITEM_SYMBOL, name, sym2);

        // -- Course / speed. --
        snprintf(name, sizeof(name), "oCrs%d", i + 1);
        web_field_int(req, TR_F_OBJITEM_COURSE, name, (long)b->course);
        snprintf(name, sizeof(name), "oSpd%d", i + 1);
        web_field_int(req, TR_F_OBJITEM_SPEED, name, (long)b->speed);

        // -- Comment. --
        char cesc[OBJITEM_COMMENT_MAX * 6 + 1];
        web_html_attr_escape(b->comment, cesc, sizeof(cesc));
        snprintf(name, sizeof(name), "oCmt%d", i + 1);
        web_field_text(req, TR_F_COMMENT, name, cesc, OBJITEM_COMMENT_MAX);

        // -- Transmit interval. --
        snprintf(name, sizeof(name), "oInt%d", i + 1);
        web_field_int(req, TR_F_BEACON_INTERVAL_S, name, (long)b->interval_s);

        web_fieldset_close(req);
    }

    httpd_resp_sendstr_chunk(req, "<p style='color:var(--sub);font-size:12px'>" TR_NOTE_OBJITEM "</p>"
                                  "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_objects_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    char *body = malloc(WEBCONFIG_POST_BUF_OBJITEMS);
    if (!body) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    if (web_read_body(req, body, WEBCONFIG_POST_BUF_OBJITEMS) < 0) {
        free(body);
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    objitems_t set;
    // Start from the stored set so any field not present in the form keeps its
    // value (notably the runtime kill_left counter); the form overwrites
    // everything it does carry.
    objitems_load(&set);

    for (int i = 0; i < OBJITEM_COUNT; i++) {
        objitem_t *b = &set.item[i];
        char name[20];

        bool was_active = b->active;

        snprintf(name, sizeof(name), "oEn%d", i + 1);
        b->enable = web_form_get_bool(body, name);
        snprintf(name, sizeof(name), "oRf%d", i + 1);
        b->send_rf = web_form_get_bool(body, name);
        snprintf(name, sizeof(name), "oInet%d", i + 1);
        b->send_inet = web_form_get_bool(body, name);

        snprintf(name, sizeof(name), "oName%d", i + 1);
        char nm[OBJITEM_NAME_MAX + 1];
        nm[0] = 0;
        web_form_get(body, name, nm, sizeof(nm)); // URL-decoded, clamped, NUL-terminated
        // nm is already clamped to OBJITEM_NAME_MAX chars; both buffers are the
        // same size, so a plain copy is safe. memcpy of the full buffer keeps
        // the ESP-IDF build's -Wstringop-truncation happy (unlike strncpy).
        memcpy(b->name, nm, sizeof(b->name));
        b->name[OBJITEM_NAME_MAX] = 0;

        snprintf(name, sizeof(name), "oType%d", i + 1);
        b->is_item = (web_form_get_int(body, name, b->is_item ? 1 : 0) != 0);

        snprintf(name, sizeof(name), "oAct%d", i + 1);
        b->active = web_form_get_bool(body, name);

        snprintf(name, sizeof(name), "oScope%d", i + 1);
        int sc = web_form_get_int(body, name, (int)b->scope);
        if (sc < OBJITEM_SCOPE_PRIVATE)
            sc = OBJITEM_SCOPE_PRIVATE;
        if (sc > OBJITEM_SCOPE_GLOBAL)
            sc = OBJITEM_SCOPE_GLOBAL;
        b->scope = (objitem_scope_t)sc;

        snprintf(name, sizeof(name), "oLat%d", i + 1);
        b->lat = web_form_get_float(body, name, b->lat);
        snprintf(name, sizeof(name), "oLon%d", i + 1);
        b->lon = web_form_get_float(body, name, b->lon);

        // Symbol via the split Table/Code fields the picker emits.
        char sym2[3] = { b->sym[0] ? b->sym[0] : '/', b->sym[1] ? b->sym[1] : '-', 0 };
        snprintf(name, sizeof(name), "oSym%d", i + 1);
        web_form_get_symbol(body, name, name, sym2, sizeof(sym2));
        b->sym[0] = sym2[0] ? sym2[0] : '/';
        b->sym[1] = sym2[1] ? sym2[1] : '-';

        snprintf(name, sizeof(name), "oCrs%d", i + 1);
        int crs = web_form_get_int(body, name, (int)b->course);
        if (crs < 0)
            crs = 0;
        b->course = (uint16_t)(crs % 360);
        snprintf(name, sizeof(name), "oSpd%d", i + 1);
        int spd = web_form_get_int(body, name, (int)b->speed);
        if (spd < 0)
            spd = 0;
        b->speed = (uint16_t)spd;

        snprintf(name, sizeof(name), "oCmt%d", i + 1);
        char cmt[OBJITEM_COMMENT_MAX + 1];
        cmt[0] = 0;
        web_form_get(body, name, cmt, sizeof(cmt));
        memcpy(b->comment, cmt, sizeof(b->comment));
        b->comment[OBJITEM_COMMENT_MAX] = 0;

        snprintf(name, sizeof(name), "oInt%d", i + 1);
        int interval = web_form_get_int(body, name, (int)b->interval_s);
        if (interval < 0)
            interval = 0;
        b->interval_s = (uint32_t)interval;

        // If the user just switched this element from active to killed while
        // it is still enabled, arm the kill retransmission sequence so the
        // transmitter sends the required kill reports before auto-disabling.
        // Switching back to active clears any pending kill.
        if (was_active && !b->active && b->enable)
            b->kill_left = OBJITEM_KILL_REPEATS;
        else if (b->active)
            b->kill_left = 0;
    }

    free(body);

    objitems_save(&set);

    web_send_saved_redirect(req, "/objects");
    return ESP_OK;
}
