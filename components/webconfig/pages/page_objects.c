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

#include "app_config.h" // g_config.path[] presets for the digipeat-path checkboxes
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

// Area-shape <select> for one element. Values 0..4 are the open shapes,
// 5..9 the colour-filled variants (value == the on-air area type digit).
static void render_area_type_select(httpd_req_t *req, const char *name, uint8_t cur) {
    static const char *const base[5] = {
        TR_F_OBJITEM_SHAPE_CIRCLE, TR_F_OBJITEM_SHAPE_LINE, TR_F_OBJITEM_SHAPE_ELLIPSE, TR_F_OBJITEM_SHAPE_TRIANGLE, TR_F_OBJITEM_SHAPE_BOX,
    };
    web_select_open(req, TR_F_OBJITEM_AREA_SHAPE, name);
    for (int t = 0; t < 10; t++) {
        char label[64];
        if (t < 5)
            snprintf(label, sizeof(label), "%s", base[t]);
        else
            snprintf(label, sizeof(label), "%s%s", base[t - 5], TR_F_OBJITEM_SHAPE_FILLED);
        web_select_option(req, t, label, cur == t);
    }
    web_select_close(req);
}

// Duplex-direction <select>. UI values 0=simplex, 1=+, 2=- (converted to the
// stored int8_t duplex on POST).
static void render_duplex_select(httpd_req_t *req, const char *name, int8_t duplex) {
    int cur = duplex > 0 ? 1 : (duplex < 0 ? 2 : 0);
    web_select_open(req, TR_F_OBJITEM_DUPLEX, name);
    web_select_option(req, 0, TR_F_OBJITEM_DUPLEX_SIMPLEX, cur == 0);
    web_select_option(req, 1, TR_F_OBJITEM_DUPLEX_PLUS, cur == 1);
    web_select_option(req, 2, TR_F_OBJITEM_DUPLEX_MINUS, cur == 2);
    web_select_close(req);
}

// The standard APRS QRU group names and their meanings, shown as the choices in
// the QRU dropdown. Names are fixed APRS identifiers (never translated); only
// the meanings are localized.
static const struct {
    const char *name;
    const char *meaning;
} k_qru_groups[] = {
    { "AMBU", TR_F_QRU_AMBU },         { "CLUB", TR_F_QRU_CLUB }, { "ECHO", TR_F_QRU_ECHO },
    { "FIRE", TR_F_QRU_FIRE },         { "FOOD", TR_F_QRU_FOOD }, { "FUEL", TR_F_QRU_FUEL },
    { "HOSP", TR_F_QRU_HOSP },         { "LIFEBOAT", TR_F_QRU_LIFEBOAT }, { "LTHS", TR_F_QRU_LTHS },
    { "POLI", TR_F_QRU_POLI },         { "POST", TR_F_QRU_POST }, { "RD13", TR_F_QRU_RD13 },
    { "RD23", TR_F_QRU_RD23 },         { "RD2M", TR_F_QRU_RD2M }, { "RD3C", TR_F_QRU_RD3C },
    { "RD70", TR_F_QRU_RD70 },         { "RP10", TR_F_QRU_RP10 }, { "RP13", TR_F_QRU_RP13 },
    { "RP23", TR_F_QRU_RP23 },         { "RP2M", TR_F_QRU_RP2M }, { "RP3C", TR_F_QRU_RP3C },
    { "RP6M", TR_F_QRU_RP6M },         { "RP70", TR_F_QRU_RP70 }, { "RT13", TR_F_QRU_RT13 },
    { "RT23", TR_F_QRU_RT23 },         { "RT3C", TR_F_QRU_RT3C }, { "SRAIL", TR_F_QRU_SRAIL },
    { "STOR", TR_F_QRU_STOR },         { "T2SRV", TR_F_QRU_T2SRV }, { "VETE", TR_F_QRU_VETE },
    { "WOTA", TR_F_QRU_WOTA },
};

// QRU membership dropdown: a visible <select> listing the standard APRS QRU
// group names (each with its meaning) plus a leading "(none)" entry.
// web_select_option() only supports integer option values, so the
// string-valued options are emitted directly here. Any stored value that is
// not one of the standard names is preserved as an extra selected option so a
// pre-existing custom tag is never silently dropped.
static void render_qru_select(httpd_req_t *req, const char *name, const char *cur) {
    char buf[192];
    snprintf(buf, sizeof(buf), "<label>%.60s</label><select name='%.30s'>", TR_F_OBJITEM_QRU, name);
    web_raw(req, buf);

    bool none = (cur == NULL || cur[0] == 0);
    snprintf(buf, sizeof(buf), "<option value=''%s>%s</option>", none ? " selected" : "", TR_F_OBJITEM_QRU_NONE);
    web_raw(req, buf);

    bool matched = false;
    for (size_t g = 0; g < sizeof(k_qru_groups) / sizeof(k_qru_groups[0]); g++) {
        bool sel = !none && strcmp(cur, k_qru_groups[g].name) == 0;
        if (sel)
            matched = true;
        // Names are safe identifiers and meanings are our own controlled
        // strings (no '<', '>' or '&'), so no HTML escaping is needed here.
        snprintf(buf, sizeof(buf), "<option value='%s'%s>%s - %s</option>", k_qru_groups[g].name, sel ? " selected" : "",
                 k_qru_groups[g].name, k_qru_groups[g].meaning);
        web_raw(req, buf);
    }

    if (!none && !matched) {
        char vesc[OBJITEM_QRU_MAX * 6 + 1];
        web_html_attr_escape(cur, vesc, sizeof(vesc));
        snprintf(buf, sizeof(buf), "<option value='%s' selected>%s</option>", vesc, vesc);
        web_raw(req, buf);
    }

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

        // -- Area object (used only with the Area symbol "\l"). --
        snprintf(name, sizeof(name), "oAType%d", i + 1);
        render_area_type_select(req, name, b->area_type);
        snprintf(name, sizeof(name), "oAColor%d", i + 1);
        web_field_int(req, TR_F_OBJITEM_AREA_COLOR, name, (long)b->area_color);
        snprintf(name, sizeof(name), "oALat%d", i + 1);
        web_field_float(req, TR_F_OBJITEM_AREA_LAT_OFF, name, b->area_lat_off, "0.01");
        snprintf(name, sizeof(name), "oALon%d", i + 1);
        web_field_float(req, TR_F_OBJITEM_AREA_LON_OFF, name, b->area_lon_off, "0.01");

        // -- Signpost text (used only with the Signpost symbol "\m"). --
        char sesc[OBJITEM_SIGNPOST_MAX * 6 + 1];
        web_html_attr_escape(b->signpost, sesc, sizeof(sesc));
        snprintf(name, sizeof(name), "oSign%d", i + 1);
        web_field_text(req, TR_F_OBJITEM_SIGNPOST, name, sesc, OBJITEM_SIGNPOST_MAX);

        // -- Repeater radio parameters (monitor frequency / duplex / tone). --
        snprintf(name, sizeof(name), "oFreq%d", i + 1);
        web_field_float(req, TR_F_OBJITEM_FREQ, name, b->freq_mhz, "0.001");
        snprintf(name, sizeof(name), "oDup%d", i + 1);
        render_duplex_select(req, name, b->duplex);
        snprintf(name, sizeof(name), "oOfs%d", i + 1);
        web_field_int(req, TR_F_OBJITEM_OFFSET, name, (long)b->offset_khz);
        snprintf(name, sizeof(name), "oTone%d", i + 1);
        web_field_float(req, TR_F_OBJITEM_TONE, name, b->tone_tenths / 10.0f, "0.1");

        // -- Digipeat paths: one checkbox per shared path preset g_config.path[k].
        //    The label shows the preset's current value so the operator sees
        //    exactly what each path sends. --
        web_raw(req, "<label>" TR_F_OBJITEM_DIGIPATH "</label>");
        for (int k = 0; k < OBJITEM_PATH_PRESETS; k++) {
            // Sized to hold the full escaped preset plus the "Path N (...)"
            // wrapper so the ESP-IDF -Wformat-truncation error cannot fire;
            // the checkbox helper caps the displayed label length itself.
            char plabel[72 * 6 + 32];
            if (g_config.path[k][0]) {
                char pesc[72 * 6 + 1];
                web_html_attr_escape(g_config.path[k], pesc, sizeof(pesc));
                snprintf(plabel, sizeof(plabel), TR_F_OBJITEM_PATH_FMT " (%s)", k + 1, pesc);
            } else {
                snprintf(plabel, sizeof(plabel), TR_F_OBJITEM_PATH_FMT, k + 1);
            }
            snprintf(name, sizeof(name), "oPath%d_%d", i + 1, k + 1);
            web_field_checkbox(req, plabel, name, (b->path_mask & (1u << k)) != 0);
        }

        // -- QRU group membership: dropdown of the standard APRS QRU groups. --
        snprintf(name, sizeof(name), "oQru%d", i + 1);
        render_qru_select(req, name, b->qru);

        // -- Initial repeat rate, then the decay ramp (slow rate + ratio). --
        snprintf(name, sizeof(name), "oInt%d", i + 1);
        web_field_int(req, TR_F_OBJITEM_INIT_RATE, name, (long)b->interval_s);
        snprintf(name, sizeof(name), "oSlow%d", i + 1);
        web_field_int(req, TR_F_OBJITEM_SLOW_RATE, name, (long)b->slow_interval_s);
        snprintf(name, sizeof(name), "oDecay%d", i + 1);
        web_field_float(req, TR_F_OBJITEM_DECAY, name, b->decay_x10 / 10.0f, "0.1");

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

        // -- Area object. --
        snprintf(name, sizeof(name), "oAType%d", i + 1);
        int atype = web_form_get_int(body, name, (int)b->area_type);
        if (atype < 0)
            atype = 0;
        if (atype > 9)
            atype = 9;
        b->area_type = (uint8_t)atype;
        snprintf(name, sizeof(name), "oAColor%d", i + 1);
        int acolor = web_form_get_int(body, name, (int)b->area_color);
        if (acolor < 0)
            acolor = 0;
        if (acolor > 15)
            acolor = 15;
        b->area_color = (uint8_t)acolor;
        snprintf(name, sizeof(name), "oALat%d", i + 1);
        float alat = web_form_get_float(body, name, b->area_lat_off);
        b->area_lat_off = alat < 0 ? 0 : alat;
        snprintf(name, sizeof(name), "oALon%d", i + 1);
        float alon = web_form_get_float(body, name, b->area_lon_off);
        b->area_lon_off = alon < 0 ? 0 : alon;

        // -- Signpost text. --
        snprintf(name, sizeof(name), "oSign%d", i + 1);
        char sp[OBJITEM_SIGNPOST_MAX + 1];
        sp[0] = 0;
        web_form_get(body, name, sp, sizeof(sp));
        memcpy(b->signpost, sp, sizeof(b->signpost));
        b->signpost[OBJITEM_SIGNPOST_MAX] = 0;

        // -- Repeater radio parameters. --
        snprintf(name, sizeof(name), "oFreq%d", i + 1);
        float freq = web_form_get_float(body, name, b->freq_mhz);
        b->freq_mhz = freq < 0 ? 0 : freq;
        snprintf(name, sizeof(name), "oDup%d", i + 1);
        int dup = web_form_get_int(body, name, b->duplex > 0 ? 1 : (b->duplex < 0 ? 2 : 0));
        b->duplex = (int8_t)(dup == 1 ? 1 : (dup == 2 ? -1 : 0));
        snprintf(name, sizeof(name), "oOfs%d", i + 1);
        int ofs = web_form_get_int(body, name, (int)b->offset_khz);
        if (ofs < 0)
            ofs = 0;
        if (ofs > 65535)
            ofs = 65535;
        b->offset_khz = (uint16_t)ofs;
        snprintf(name, sizeof(name), "oTone%d", i + 1);
        float tone_hz = web_form_get_float(body, name, b->tone_tenths / 10.0f);
        if (tone_hz < 0)
            tone_hz = 0;
        int tone_tenths = (int)(tone_hz * 10.0f + 0.5f);
        if (tone_tenths > 65535)
            tone_tenths = 65535;
        b->tone_tenths = (uint16_t)tone_tenths;

        // -- Digipeat paths bitmask (one checkbox per preset). --
        uint8_t mask = 0;
        for (int k = 0; k < OBJITEM_PATH_PRESETS; k++) {
            snprintf(name, sizeof(name), "oPath%d_%d", i + 1, k + 1);
            if (web_form_get_bool(body, name))
                mask |= (uint8_t)(1u << k);
        }
        b->path_mask = mask;

        // -- QRU group membership. --
        snprintf(name, sizeof(name), "oQru%d", i + 1);
        char qg[OBJITEM_QRU_MAX + 1];
        qg[0] = 0;
        web_form_get(body, name, qg, sizeof(qg));
        memcpy(b->qru, qg, sizeof(b->qru));
        b->qru[OBJITEM_QRU_MAX] = 0;

        snprintf(name, sizeof(name), "oInt%d", i + 1);
        int interval = web_form_get_int(body, name, (int)b->interval_s);
        if (interval < 0)
            interval = 0;
        b->interval_s = (uint32_t)interval;

        // -- Decay ramp: slow repeat rate + decay ratio. --
        snprintf(name, sizeof(name), "oSlow%d", i + 1);
        int slow = web_form_get_int(body, name, (int)b->slow_interval_s);
        if (slow < 0)
            slow = 0;
        b->slow_interval_s = (uint32_t)slow;
        snprintf(name, sizeof(name), "oDecay%d", i + 1);
        float ratio = web_form_get_float(body, name, b->decay_x10 / 10.0f);
        if (ratio < 0)
            ratio = 0;
        int decay_x10 = (int)(ratio * 10.0f + 0.5f);
        if (decay_x10 > 65535)
            decay_x10 = 65535;
        b->decay_x10 = (uint16_t)decay_x10;

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
