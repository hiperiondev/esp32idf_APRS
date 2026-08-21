// @file page_objects.c
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
// @brief Web admin "Objects and Items" page: edits the OBJITEM_COUNT APRS
// Objects/Items. Each block has the three request-mandated toggles (Enable /
// Send via RF / Send via Internet), plus the on-air parameters drawn from the
// YAAC object editor (name, active/kill, Object-vs-Item, scope, latitude/
// longitude, symbol+overlay, course/speed, comment, interval).
//   https://www.ka2ddo.org/ka2ddo/YAACdocs/objecteditor.html
//
// Objects/Items live in their own LittleFS file (/storage/objitems.json), NOT
// in g_config - see objects_items.h. This page loads/saves them through the
// objitems_* API rather than touching g_config or app_config_save().

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "objects_items.h"
#include "pages.h"
#include "str_append.h" // str_copy_utf8_safe()
#include "translations.h"
#include "web_common.h"

static const char *TAG = "page_objects";

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

// Area-shape <select> for one element. The option value is the on-air area
// type digit, so the labels follow the APRS101 chapter 11 table rather than a
// pattern: the filled variants sit five digits above their open counterparts,
// but digit 6 is the second line direction, not a filled line.
static void render_area_type_select(httpd_req_t *req, const char *name, uint8_t cur) {
    static const char *const shape[OBJITEM_AREA_TYPE_MAX + 1] = {
        TR_F_OBJITEM_SHAPE_CIRCLE,
        TR_F_OBJITEM_SHAPE_LINE_DOWN_RIGHT,
        TR_F_OBJITEM_SHAPE_ELLIPSE,
        TR_F_OBJITEM_SHAPE_TRIANGLE,
        TR_F_OBJITEM_SHAPE_BOX,
        TR_F_OBJITEM_SHAPE_CIRCLE TR_F_OBJITEM_SHAPE_FILLED,
        TR_F_OBJITEM_SHAPE_LINE_DOWN_LEFT,
        TR_F_OBJITEM_SHAPE_ELLIPSE TR_F_OBJITEM_SHAPE_FILLED,
        TR_F_OBJITEM_SHAPE_TRIANGLE TR_F_OBJITEM_SHAPE_FILLED,
        TR_F_OBJITEM_SHAPE_BOX TR_F_OBJITEM_SHAPE_FILLED,
    };
    web_select_open(req, TR_F_OBJITEM_AREA_SHAPE, name);
    for (int t = 0; t <= OBJITEM_AREA_TYPE_MAX; t++)
        web_select_option(req, t, shape[t], cur == t);
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

// Range-unit <select>. Values match objitem_t.range_km (0 = miles, 1 = km).
static void render_range_unit_select(httpd_req_t *req, const char *name, bool range_km) {
    web_select_open(req, TR_F_OBJITEM_RANGE_UNIT, name);
    web_select_option(req, 0, TR_F_OBJITEM_RANGE_UNIT_MI, !range_km);
    web_select_option(req, 1, TR_F_OBJITEM_RANGE_UNIT_KM, range_km);
    web_select_close(req);
}

// The standard APRS QRU group names and their meanings, shown as the choices in
// the QRU dropdown. Names are fixed APRS identifiers (never translated); only
// the meanings are localized.
static const struct {
    const char *name;
    const char *meaning;
} k_qru_groups[] = {
    { "AMBU", TR_F_QRU_AMBU }, { "CLUB", TR_F_QRU_CLUB },   { "ECHO", TR_F_QRU_ECHO },         { "FIRE", TR_F_QRU_FIRE },   { "FOOD", TR_F_QRU_FOOD },
    { "FUEL", TR_F_QRU_FUEL }, { "HOSP", TR_F_QRU_HOSP },   { "LIFEBOAT", TR_F_QRU_LIFEBOAT }, { "LTHS", TR_F_QRU_LTHS },   { "POLI", TR_F_QRU_POLI },
    { "POST", TR_F_QRU_POST }, { "RD13", TR_F_QRU_RD13 },   { "RD23", TR_F_QRU_RD23 },         { "RD2M", TR_F_QRU_RD2M },   { "RD3C", TR_F_QRU_RD3C },
    { "RD70", TR_F_QRU_RD70 }, { "RP10", TR_F_QRU_RP10 },   { "RP13", TR_F_QRU_RP13 },         { "RP23", TR_F_QRU_RP23 },   { "RP2M", TR_F_QRU_RP2M },
    { "RP3C", TR_F_QRU_RP3C }, { "RP6M", TR_F_QRU_RP6M },   { "RP70", TR_F_QRU_RP70 },         { "RT13", TR_F_QRU_RT13 },   { "RT23", TR_F_QRU_RT23 },
    { "RT3C", TR_F_QRU_RT3C }, { "SRAIL", TR_F_QRU_SRAIL }, { "STOR", TR_F_QRU_STOR },         { "T2SRV", TR_F_QRU_T2SRV }, { "VETE", TR_F_QRU_VETE },
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
        snprintf(buf, sizeof(buf), "<option value='%s'%s>%s - %s</option>", k_qru_groups[g].name, sel ? " selected" : "", k_qru_groups[g].name,
                 k_qru_groups[g].meaning);
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

// PHG (Power-Height-Gain-Directivity) block for one Object/Item, mirroring the
// "My Station" PHG section on the Station page. Two request-mandated toggles
// lead the block: "Enable PHG" turns the whole block on, and "Use My Station
// Data" makes the element reuse the shared station PHG - the four sub-fields are
// then filled from the Station page and locked (not editable). The client-side
// script wired up in page_objects_get() enforces the enable/lock behaviour and
// keeps the computed PHG text in sync, exactly like the Station page does.
//
// Height keeps feet as its underlying <select> value (the APRS PHG code table's
// own unit) while every visible label is shown in meters, identical to the
// Station page.
//
// This block shares the transmitted info field's 7-byte data-extension slot
// with the element's own course/speed (Group 3 above): a non-zero Speed there
// always wins that slot on-air, so PHG is only actually transmitted while
// Speed is 0, regardless of the "Enable PHG" checkbox below - see the
// Group 3 course/speed comment and objitem_build_info_field() in
// objects_items.c.
static void render_objitem_phg(httpd_req_t *req, int i, const objitem_t *b) {
    char name[20];
    char buf[256];

    web_fieldset_open(req, TR_F_PHG_SECTION);

    // -- The two request-mandated PHG toggles, first (each carries an id so the
    //    page script can find it). --
    snprintf(buf, sizeof(buf), "<label><input type='checkbox' name='oPhgEn%d' id='oPhgEn%d' %s> %s</label>", i + 1, i + 1, b->phg_enable ? "checked" : "",
             TR_F_ENABLE_PHG);
    web_raw(req, buf);
    snprintf(buf, sizeof(buf), "<label><input type='checkbox' name='oPhgUS%d' id='oPhgUS%d' %s> %s</label>", i + 1, i + 1, b->phg_use_station ? "checked" : "",
             TR_USE_MY_STATION_DATA);
    web_raw(req, buf);

    // -- Power (Watts, APRS P-digit code table). --
    snprintf(name, sizeof(name), "oPhgP%d", i + 1);
    web_select_open(req, TR_F_RADIO_TX_POWER, name);
    {
        static const int watts[] = { 0, 1, 5, 10, 15, 25, 35, 50, 65, 80 };
        for (size_t k = 0; k < sizeof(watts) / sizeof(watts[0]); k++) {
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%d", watts[k]);
            web_select_option(req, watts[k], lbl, b->phg_power == (uint16_t)watts[k]);
        }
    }
    web_select_close(req);

    // -- Gain (dB, APRS G-digit code table). --
    snprintf(name, sizeof(name), "oPhgG%d", i + 1);
    web_select_open(req, TR_F_ANTENNA_GAIN, name);
    for (int g = 0; g <= 9; g++) {
        char lbl[16];
        snprintf(lbl, sizeof(lbl), "%d", g);
        web_select_option(req, g, lbl, (int)lroundf(b->phg_gain) == g);
    }
    web_select_close(req);

    // -- Height (option value stays in feet; label shown in meters). --
    snprintf(name, sizeof(name), "oPhgH%d", i + 1);
    web_select_open(req, TR_F_HEIGHT_M, name);
    {
        // APRS PHG height code table, feet (10*2^n). Capped at 40960 ft so
        // every offered value fits the uint16_t store (81920 would overflow) -
        // 40960 ft HAAT is already far beyond any real fixed station.
        static const int feet[] = { 10, 20, 40, 80, 160, 320, 640, 1280, 2560, 5120, 10240, 20480, 40960 };
        for (size_t k = 0; k < sizeof(feet) / sizeof(feet[0]); k++) {
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%d", (int)lroundf(feet[k] * 0.3048f));
            web_select_option(req, feet[k], lbl, b->phg_height == (uint16_t)feet[k]);
        }
    }
    web_select_close(req);

    // -- Directivity. --
    snprintf(name, sizeof(name), "oPhgD%d", i + 1);
    web_select_open(req, TR_F_ANTENNA_DIRECTION, name);
    {
        static const char *const dirs[] = { TR_DIR_OMNI, TR_DIR_N, TR_DIR_NE, TR_DIR_E, TR_DIR_SE, TR_DIR_S, TR_DIR_SW, TR_DIR_W, TR_DIR_NW };
        for (int d = 0; d < 9; d++)
            web_select_option(req, d, dirs[d], b->phg_dir == (uint8_t)d);
    }
    web_select_close(req);

    // -- Computed PHG string: read-only and display-only, so it carries no
    //    name attribute and is never submitted or stored. The page script
    //    fills it from the four sub-fields above, which are the values the
    //    object encoder reads. --
    snprintf(buf, sizeof(buf), "<label>%s</label><input type='text' id='oPhg%d' maxlength='7' readonly>", TR_F_PHG_TEXT, i + 1);
    web_raw(req, buf);

    web_fieldset_close(req);
}

esp_err_t page_objects_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    objitems_t set;
    objitems_load(&set);

    web_send_header(req, TR_F_OBJITEMS, "objects");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/objects'>");

    httpd_resp_sendstr_chunk(req, "<div id='objWrap'>");
    for (int i = 0; i < OBJITEM_COUNT; i++) {
        const objitem_t *b = &set.item[i];

        char legend[28];
        snprintf(legend, sizeof(legend), TR_F_OBJITEM_FMT, i + 1);

        // Collapsible accordion card: only the first object/item starts
        // open; accordionClick() (injected below, shared with the Bulletins
        // page) closes any other open card whenever one is clicked, so at
        // most one element is expanded at a time. Reuses the shared
        // .achan* accordion styling.
        char head[256];
        snprintf(head, sizeof(head),
                 "<div class='achan%s' id='obj%d'>"
                 "<div class='achan-head' onclick='accordionClick(\"obj\",%d,%d)'>"
                 "<span class='achan-name'>%s</span>"
                 "<span class='achan-caret'>&#9654;</span>"
                 "</div><div class='achan-body'>",
                 (i == 0) ? " open" : "", i, i, OBJITEM_COUNT, legend);
        httpd_resp_sendstr_chunk(req, head);

        char name[20];

        // -- Group 1: Transmission Control - the three request-mandated
        //    checks, first. --
        web_fieldset_open(req, TR_F_OBJITEM_TX_CONTROL);
        snprintf(name, sizeof(name), "oEn%d", i + 1);
        web_field_checkbox(req, TR_F_ENABLE, name, b->enable);
        snprintf(name, sizeof(name), "oRf%d", i + 1);
        web_field_checkbox(req, TR_F_SEND_VIA_RF, name, b->send_rf);
        snprintf(name, sizeof(name), "oInet%d", i + 1);
        web_field_checkbox(req, TR_F_SEND_VIA_INTERNET, name, b->send_inet);
        web_fieldset_close(req);

        // -- Group 2: Identity / state. web_field_text() escapes internally
        //    now. --
        web_fieldset_open(req, TR_F_OBJITEM_IDENTITY);
        snprintf(name, sizeof(name), "oName%d", i + 1);
        web_field_text(req, TR_F_OBJECT_ITEM_NAME, name, b->name, OBJITEM_NAME_MAX);

        snprintf(name, sizeof(name), "oType%d", i + 1);
        render_type_select(req, name, b->is_item);

        // Permanent (Object only): fixed "111111z" pseudo-timestamp instead
        // of the live UTC one (freqspec.txt). Carries its own id so the page
        // script can grey it out while an Item is selected, since it has no
        // effect there.
        {
            char buf[256];
            snprintf(buf, sizeof(buf), "<label id='oPermLbl%d'><input type='checkbox' name='oPerm%d' id='oPerm%d' %s%s> %s</label>", i + 1, i + 1, i + 1,
                     b->permanent ? "checked " : "", b->is_item ? "disabled" : "", TR_F_OBJITEM_PERMANENT);
            web_raw(req, buf);
        }
        web_raw(req, "<p style='color:var(--sub);font-size:11px'>" TR_F_OBJITEM_PERMANENT_NOTE "</p>");

        snprintf(name, sizeof(name), "oAct%d", i + 1);
        web_field_checkbox(req, TR_F_OBJITEM_ACTIVE, name, b->active);

        snprintf(name, sizeof(name), "oScope%d", i + 1);
        render_scope_select(req, name, b->scope);
        web_fieldset_close(req);

        // -- Group 3: Position + symbol/overlay (2-char table+code control) +
        //    course/speed. --
        web_fieldset_open(req, TR_F_OBJITEM_POS_SYMBOL);
        snprintf(name, sizeof(name), "oLat%d", i + 1);
        web_field_float(req, TR_F_FIXED_LATITUDE, name, b->lat, "0.0001", WEB_RANGE_LAT_MIN, WEB_RANGE_LAT_MAX);
        snprintf(name, sizeof(name), "oLon%d", i + 1);
        web_field_float(req, TR_F_FIXED_LONGITUDE, name, b->lon, "0.0001", WEB_RANGE_LON_MIN, WEB_RANGE_LON_MAX);

        char sym2[3] = { b->sym[0] ? b->sym[0] : '/', b->sym[1] ? b->sym[1] : '-', 0 };
        snprintf(name, sizeof(name), "oSym%d", i + 1);
        web_field_symbol(req, TR_F_OBJITEM_SYMBOL, name, sym2);

        // Course/speed and the PHG block (Group 9 below) share the same
        // 7-byte data-extension slot in the transmitted info field (APRS101
        // ch.7/ch.11), so the two are mutually exclusive on-air. A non-zero
        // speed here always takes that slot, silently omitting PHG from the
        // packet even if the Group 9 "Enable PHG" checkbox is also checked -
        // see objitem_build_info_field() in objects_items.c. To transmit PHG
        // for this element, leave Speed at 0.
        snprintf(name, sizeof(name), "oCrs%d", i + 1);
        web_field_int(req, TR_F_OBJITEM_COURSE, name, (long)b->course, 0, 359);
        snprintf(name, sizeof(name), "oSpd%d", i + 1);
        web_field_int(req, TR_F_OBJITEM_SPEED, name, (long)b->speed, 0, 999);

        // Compressed position format saves airtime, but shares the 7-byte
        // data-extension slot with the Area/Signpost descriptors (Group 5/6
        // below) and has no PHG equivalent (Group 9), so it is silently
        // ignored at transmit time for those cases regardless of this
        // checkbox - see objitem_build_info_field() in objects_items.c.
        snprintf(name, sizeof(name), "oCompress%d", i + 1);
        web_field_checkbox(req, TR_F_COMPRESS_POSITION, name, b->compress);
        web_fieldset_close(req);

        // -- Group 4: Comment. web_field_text() escapes internally now. --
        web_fieldset_open(req, TR_F_COMMENT);
        snprintf(name, sizeof(name), "oCmt%d", i + 1);
        web_field_text(req, TR_F_COMMENT, name, b->comment, OBJITEM_COMMENT_MAX);
        web_fieldset_close(req);

        // -- Group 5: Area object (used only with the Area symbol "\l"). --
        web_fieldset_open(req, TR_F_OBJITEM_AREA_SECTION);
        snprintf(name, sizeof(name), "oAType%d", i + 1);
        render_area_type_select(req, name, b->area_type);
        snprintf(name, sizeof(name), "oAColor%d", i + 1);
        web_field_int(req, TR_F_OBJITEM_AREA_COLOR, name, (long)b->area_color, 0, 15);
        snprintf(name, sizeof(name), "oALat%d", i + 1);
        web_field_float(req, TR_F_OBJITEM_AREA_LAT_OFF, name, b->area_lat_off, "0.01", 0.0f, OBJITEM_AREA_OFFSET_DEG_MAX);
        snprintf(name, sizeof(name), "oALon%d", i + 1);
        web_field_float(req, TR_F_OBJITEM_AREA_LON_OFF, name, b->area_lon_off, "0.01", 0.0f, OBJITEM_AREA_OFFSET_DEG_MAX);
        snprintf(name, sizeof(name), "oAWidth%d", i + 1);
        web_field_int(req, TR_F_OBJITEM_AREA_WIDTH, name, (long)b->area_line_width, 0, OBJITEM_AREA_WIDTH_MAX);
        web_fieldset_close(req);

        // -- Group 6: Signpost text (used only with the Signpost symbol
        //    "\m"). web_field_text() escapes internally now. --
        web_fieldset_open(req, TR_F_OBJITEM_SIGNPOST_SECTION);
        snprintf(name, sizeof(name), "oSign%d", i + 1);
        web_field_text(req, TR_F_OBJITEM_SIGNPOST, name, b->signpost, OBJITEM_SIGNPOST_MAX);
        web_fieldset_close(req);

        // -- Group 7: Repeater radio parameters (monitor frequency / split RX
        //    frequency / duplex / tone or DCS / narrowband flag / range /
        //    digipeat path / QRU group). --
        web_fieldset_open(req, TR_F_OBJITEM_REPEATER_SECTION);
        snprintf(name, sizeof(name), "oFreq%d", i + 1);
        web_field_float(req, TR_F_OBJITEM_FREQ, name, b->freq_mhz, "0.001", 0.0f, 999.999f);
        snprintf(name, sizeof(name), "oRxEn%d", i + 1);
        web_field_checkbox(req, TR_F_OBJITEM_RX_FREQ_ENABLE, name, b->rx_freq_enable);
        snprintf(name, sizeof(name), "oRxFreq%d", i + 1);
        web_field_float(req, TR_F_OBJITEM_RX_FREQ, name, b->rx_freq_mhz, "0.001", 0.0f, 999.999f);
        snprintf(name, sizeof(name), "oDup%d", i + 1);
        render_duplex_select(req, name, b->duplex);
        snprintf(name, sizeof(name), "oOfs%d", i + 1);
        web_field_int(req, TR_F_OBJITEM_OFFSET, name, (long)b->offset_khz, 0, 65535);
        snprintf(name, sizeof(name), "oDcsEn%d", i + 1);
        web_field_checkbox(req, TR_F_OBJITEM_DCS_ENABLE, name, b->dcs_enable);
        snprintf(name, sizeof(name), "oTone%d", i + 1);
        web_field_float(req, TR_F_OBJITEM_TONE, name, b->tone_tenths / 10.0f, "0.1", 0.0f, 254.1f);
        snprintf(name, sizeof(name), "oDcs%d", i + 1);
        web_field_int(req, TR_F_OBJITEM_DCS_CODE, name, (long)b->dcs_code, 0, 511);
        snprintf(name, sizeof(name), "oNarrow%d", i + 1);
        web_field_checkbox(req, TR_F_OBJITEM_NARROW, name, b->narrow);
        snprintf(name, sizeof(name), "oRng%d", i + 1);
        web_field_int(req, TR_F_OBJITEM_RANGE, name, (long)b->range, 0, 99);
        snprintf(name, sizeof(name), "oRngU%d", i + 1);
        render_range_unit_select(req, name, b->range_km);

        // Path: one checkbox per shared Digipeater Path Alias, same control
        // shared with the Digipeater/Tracker/WX/Messaging/Telemetry pages.
        char path_prefix[24];
        snprintf(path_prefix, sizeof(path_prefix), "oPath%d_", i + 1);
        web_field_path_checkboxes(req, path_prefix, b->path_mask);

        // QRU group membership: dropdown of the standard APRS QRU groups.
        snprintf(name, sizeof(name), "oQru%d", i + 1);
        render_qru_select(req, name, b->qru);
        web_fieldset_close(req);

        // -- Group 8: Beacon timing - initial repeat rate, then the decay
        //    ramp (slow rate + ratio). --
        web_fieldset_open(req, TR_F_OBJITEM_TIMING_SECTION);
        snprintf(name, sizeof(name), "oInt%d", i + 1);
        web_field_int(req, TR_F_OBJITEM_INIT_RATE, name, (long)b->interval_s, WEB_RANGE_INTERVAL_S_MIN, WEB_RANGE_INTERVAL_LONG_S_MAX);
        snprintf(name, sizeof(name), "oSlow%d", i + 1);
        web_field_int(req, TR_F_OBJITEM_SLOW_RATE, name, (long)b->slow_interval_s, WEB_RANGE_INTERVAL_S_MIN, WEB_RANGE_INTERVAL_LONG_S_MAX);
        snprintf(name, sizeof(name), "oDecay%d", i + 1);
        web_field_float(req, TR_F_OBJITEM_DECAY, name, b->decay_x10 / 10.0f, "0.1", 0.0f, 10.0f);
        web_fieldset_close(req);

        // -- Group 9: PHG block (same sub-fields as the Station page), its
        //    own fieldset already, appended last. --
        render_objitem_phg(req, i, b);

        httpd_resp_sendstr_chunk(req, "</div></div>");
    }
    httpd_resp_sendstr_chunk(req, "</div>");

    // Generic single-open accordion helper, shared verbatim with the
    // Bulletins page: closes every card of the given id-prefix except the
    // one just clicked (toggling it), so only one is expanded at a time.
    httpd_resp_sendstr_chunk(req, "<script>"
                                  "function accordionClick(p,i,n){"
                                  "for(var k=0;k<n;k++){"
                                  "var c=document.getElementById(p+k);"
                                  "if(!c)continue;"
                                  "if(k===i)c.classList.toggle('open');"
                                  "else c.classList.remove('open');"
                                  "}"
                                  "}"
                                  "</script>");

    // Permanent-checkbox gating: "Permanent" only applies to an Object, so it
    // is greyed out whenever the matching Type <select> is switched to Item.
    {
        char js[512];
        snprintf(js, sizeof(js),
                 "<script>(function(){"
                 "function applyPerm(n){"
                 "var t=document.querySelector(\"[name='oType\"+n+\"']\"),p=document.getElementById('oPerm'+n);"
                 "if(!t||!p)return;"
                 "p.disabled=(t.value==='1');"
                 "}"
                 "document.addEventListener('DOMContentLoaded',function(){"
                 "for(var n=1;n<=%d;n++){(function(n){"
                 "var t=document.querySelector(\"[name='oType\"+n+\"']\");"
                 "if(t)t.addEventListener('change',function(){applyPerm(n);});"
                 "applyPerm(n);"
                 "})(n);}"
                 "});"
                 "})();</script>",
                 OBJITEM_COUNT);
        httpd_resp_sendstr_chunk(req, js);
    }

    // Shared "My Station" PHG snapshot, exposed to the PHG script below so an
    // element with "Use My Station Data" ticked can mirror the Station page's
    // values live. Only the four sub-fields travel: the displayed PHG string is
    // always derived from them by the same formula on every page.
    {
        char sbuf[256];
        snprintf(sbuf, sizeof(sbuf), "<script>window.__stnPHG={p:%u,g:%d,h:%u,d:%u};</script>", (unsigned)g_config.my_phg_power,
                 (int)lroundf(g_config.my_phg_gain), (unsigned)g_config.my_phg_height, (unsigned)g_config.my_phg_dir);
        httpd_resp_sendstr_chunk(req, sbuf);
    }

    // Per-element PHG behaviour, shared across all OBJITEM_COUNT blocks:
    //   * "Enable PHG" off  -> the four PHG sub-fields are disabled.
    //   * "Use My Station Data" on -> the sub-fields are filled from the shared
    //     station PHG and disabled (locked), and the computed text is
    //     recalculated from those mirrored values.
    //   * otherwise the sub-fields are editable and the computed PHG text is
    //     recalculated live from them (same formula as the Station page).
    // Disabled controls don't POST, so the save handler snapshots the station
    // PHG for any "Use My Station Data" element and keeps the stored own-values
    // for a disabled block - see page_objects_post().
    {
        char js[1700];
        snprintf(js, sizeof(js),
                 "<script>(function(){"
                 "var ST=window.__stnPHG||{p:0,g:0,h:10,d:0};"
                 "function q(n){return document.querySelector(\"[name='\"+n+\"']\");}"
                 "function calc(n){"
                 "var p=parseInt(q('oPhgP'+n).value)||0,g=parseInt(q('oPhgG'+n).value)||0,"
                 "h=parseInt(q('oPhgH'+n).value)||10,d=parseInt(q('oPhgD'+n).value)||0;"
                 "var P=Math.min(9,Math.max(0,Math.round(Math.sqrt(p))));"
                 "var H=Math.min(13,Math.max(0,Math.round(Math.log(h/10)/Math.log(2))));"
                 "var G=Math.min(9,Math.max(0,g)),D=Math.min(8,Math.max(0,d));"
                 "var o=document.getElementById('oPhg'+n);"
                 "if(o)o.value='PHG'+P+String.fromCharCode(48+H)+G+D;"
                 "}"
                 "function apply(n){"
                 "var en=document.getElementById('oPhgEn'+n),us=document.getElementById('oPhgUS'+n);"
                 "if(!en)return;"
                 "var on=en.checked,useS=us&&us.checked;"
                 "if(useS){q('oPhgP'+n).value=ST.p;q('oPhgG'+n).value=ST.g;q('oPhgH'+n).value=ST.h;q('oPhgD'+n).value=ST.d;}"
                 "var dis=(!on)||useS;"
                 "['oPhgP'+n,'oPhgG'+n,'oPhgH'+n,'oPhgD'+n].forEach(function(nm){var el=q(nm);if(el)el.disabled=dis;});"
                 "calc(n);"
                 "}"
                 "document.addEventListener('DOMContentLoaded',function(){"
                 "for(var n=1;n<=%d;n++){(function(n){"
                 "var en=document.getElementById('oPhgEn'+n),us=document.getElementById('oPhgUS'+n);"
                 "if(en)en.addEventListener('change',function(){apply(n);});"
                 "if(us)us.addEventListener('change',function(){apply(n);});"
                 "['oPhgP'+n,'oPhgG'+n,'oPhgH'+n,'oPhgD'+n].forEach(function(nm){var el=q(nm);if(el)el.addEventListener('change',function(){calc(n);});});"
                 "apply(n);"
                 "})(n);}"
                 "});"
                 "})();</script>",
                 OBJITEM_COUNT);
        httpd_resp_sendstr_chunk(req, js);
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

    // Snapshot the shared "My Station" PHG once (under the config lock) for any
    // element whose "Use My Station Data" is enabled - the same convention the
    // IGate/Digipeater/Tracker/WX pages use for callsign/position. Locked PHG
    // sub-fields are disabled in the browser and therefore not POSTed, so their
    // values are taken from here instead of the form.
    uint16_t st_phg_power, st_phg_height;
    float st_phg_gain;
    uint8_t st_phg_dir;
    // Snapshot the shared Digipeater Path Alias presets too (see
    // app_config_path_mask_clamp() below), for the same "copy it out while
    // locked, then work on the local copy" reason as the PHG fields above.
    char path_preset[4][72];
    app_config_lock();
    st_phg_power = g_config.my_phg_power;
    st_phg_gain = g_config.my_phg_gain;
    st_phg_height = g_config.my_phg_height;
    st_phg_dir = g_config.my_phg_dir;
    for (int i = 0; i < 4; i++) {
        strncpy(path_preset[i], g_config.path[i], sizeof(path_preset[i]) - 1);
        path_preset[i][sizeof(path_preset[i]) - 1] = 0;
    }
    app_config_unlock();

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
        web_form_get(body, name, nm, sizeof(nm)); // URL-decoded (CR/LF-free), clamped, NUL-terminated

        snprintf(name, sizeof(name), "oType%d", i + 1);
        b->is_item = (web_form_get_int(body, name, b->is_item ? 1 : 0) != 0);

        // Permanent only applies to an Object; the page script disables the
        // checkbox while Item is selected, so a disabled control never POSTs
        // and saving an Item element clears it here. It has no effect on the
        // wire either way for an Item - see objitem_build_info_field() in
        // objects_items.c.
        snprintf(name, sizeof(name), "oPerm%d", i + 1);
        b->permanent = web_form_get_bool(body, name);

        // An Item name must be 3..9 characters (APRS101 Ch. 11); an Object
        // name is always exactly 9, space-padded on air, so it has no
        // minimum here. Pad a too-short Item name out to the minimum with
        // trailing spaces rather than rejecting the save outright.
        if (b->is_item) {
            size_t nl = strlen(nm);
            while (nl < OBJITEM_NAME_MIN)
                nm[nl++] = ' ';
            nm[nl] = 0;
        }

        // nm is already clamped to OBJITEM_NAME_MAX chars; both buffers are the
        // same size, so a plain copy is safe. memcpy of the full buffer keeps
        // the ESP-IDF build's -Wstringop-truncation happy (unlike strncpy).
        memcpy(b->name, nm, sizeof(b->name));
        b->name[OBJITEM_NAME_MAX] = 0;

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

        snprintf(name, sizeof(name), "oCompress%d", i + 1);
        b->compress = web_form_get_bool(body, name);

        snprintf(name, sizeof(name), "oCmt%d", i + 1);
        char cmt[OBJITEM_COMMENT_MAX + 1];
        cmt[0] = 0;
        web_form_get(body, name, cmt, sizeof(cmt));
        // web_form_get() clamps to sizeof(cmt) on a plain byte count, so an
        // operator-typed multi-byte UTF-8 character sitting right at that
        // boundary could arrive already split; re-cut here so the stored
        // comment - which this station repeats on every future beacon -
        // never carries an incomplete character even in that edge case.
        str_copy_utf8_safe(cmt, b->comment, sizeof(b->comment));

        // -- Area object. --
        snprintf(name, sizeof(name), "oAType%d", i + 1);
        int atype = web_form_get_int(body, name, (int)b->area_type);
        if (atype < 0)
            atype = 0;
        if (atype > OBJITEM_AREA_TYPE_MAX)
            atype = OBJITEM_AREA_TYPE_MAX;
        b->area_type = (uint8_t)atype;
        snprintf(name, sizeof(name), "oAColor%d", i + 1);
        int acolor = web_form_get_int(body, name, (int)b->area_color);
        if (acolor < 0)
            acolor = 0;
        if (acolor > OBJITEM_AREA_COLOR_MAX)
            acolor = OBJITEM_AREA_COLOR_MAX;
        b->area_color = (uint8_t)acolor;
        // Both offsets are clamped to the largest value the two-digit on-air
        // code can express, so the stored value and the transmitted shape
        // always describe the same area.
        snprintf(name, sizeof(name), "oALat%d", i + 1);
        float alat = web_form_get_float(body, name, b->area_lat_off);
        if (alat < 0)
            alat = 0;
        if (alat > OBJITEM_AREA_OFFSET_DEG_MAX)
            alat = OBJITEM_AREA_OFFSET_DEG_MAX;
        b->area_lat_off = alat;
        snprintf(name, sizeof(name), "oALon%d", i + 1);
        float alon = web_form_get_float(body, name, b->area_lon_off);
        if (alon < 0)
            alon = 0;
        if (alon > OBJITEM_AREA_OFFSET_DEG_MAX)
            alon = OBJITEM_AREA_OFFSET_DEG_MAX;
        b->area_lon_off = alon;
        snprintf(name, sizeof(name), "oAWidth%d", i + 1);
        int awidth = web_form_get_int(body, name, (int)b->area_line_width);
        if (awidth < 0)
            awidth = 0;
        if (awidth > OBJITEM_AREA_WIDTH_MAX)
            awidth = OBJITEM_AREA_WIDTH_MAX;
        b->area_line_width = (uint16_t)awidth;

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
        snprintf(name, sizeof(name), "oRxEn%d", i + 1);
        b->rx_freq_enable = web_form_get_bool(body, name);
        snprintf(name, sizeof(name), "oRxFreq%d", i + 1);
        float rx_freq = web_form_get_float(body, name, b->rx_freq_mhz);
        b->rx_freq_mhz = rx_freq < 0 ? 0 : rx_freq;
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
        snprintf(name, sizeof(name), "oDcsEn%d", i + 1);
        b->dcs_enable = web_form_get_bool(body, name);
        snprintf(name, sizeof(name), "oTone%d", i + 1);
        float tone_hz = web_form_get_float(body, name, b->tone_tenths / 10.0f);
        if (tone_hz < 0)
            tone_hz = 0;
        int tone_tenths = (int)(tone_hz * 10.0f + 0.5f);
        if (tone_tenths > 65535)
            tone_tenths = 65535;
        b->tone_tenths = (uint16_t)tone_tenths;
        snprintf(name, sizeof(name), "oDcs%d", i + 1);
        int dcs = web_form_get_int(body, name, (int)b->dcs_code);
        if (dcs < 0)
            dcs = 0;
        if (dcs > 511)
            dcs = 511;
        b->dcs_code = (uint16_t)dcs;
        snprintf(name, sizeof(name), "oNarrow%d", i + 1);
        b->narrow = web_form_get_bool(body, name);

        snprintf(name, sizeof(name), "oRng%d", i + 1);
        int range = web_form_get_int(body, name, (int)b->range);
        if (range < 0)
            range = 0;
        if (range > 99)
            range = 99;
        b->range = (uint16_t)range;
        snprintf(name, sizeof(name), "oRngU%d", i + 1);
        b->range_km = web_form_get_int(body, name, b->range_km ? 1 : 0) != 0;

        // -- Path bitmask (one checkbox per shared Digipeater Path Alias). --
        char path_prefix[24];
        snprintf(path_prefix, sizeof(path_prefix), "oPath%d_", i + 1);
        b->path_mask = app_config_path_mask_clamp(web_form_get_path_mask(body, path_prefix), path_preset);

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

        // -- PHG block. --
        snprintf(name, sizeof(name), "oPhgEn%d", i + 1);
        b->phg_enable = web_form_get_bool(body, name);
        snprintf(name, sizeof(name), "oPhgUS%d", i + 1);
        b->phg_use_station = web_form_get_bool(body, name);
        if (b->phg_use_station) {
            // Locked to the shared station PHG: the sub-fields are disabled in
            // the browser and don't POST, so take their values from the snapshot.
            b->phg_power = st_phg_power;
            b->phg_gain = st_phg_gain;
            b->phg_height = st_phg_height;
            b->phg_dir = st_phg_dir;
        } else {
            snprintf(name, sizeof(name), "oPhgP%d", i + 1);
            int pw = web_form_get_int(body, name, (int)b->phg_power);
            if (pw < 0)
                pw = 0;
            b->phg_power = (uint16_t)pw;
            snprintf(name, sizeof(name), "oPhgG%d", i + 1);
            int gn = web_form_get_int(body, name, (int)lroundf(b->phg_gain));
            if (gn < 0)
                gn = 0;
            if (gn > 9)
                gn = 9;
            b->phg_gain = (float)gn;
            snprintf(name, sizeof(name), "oPhgH%d", i + 1);
            int ht = web_form_get_int(body, name, (int)b->phg_height);
            if (ht <= 0)
                ht = 10;
            b->phg_height = (uint16_t)ht;
            snprintf(name, sizeof(name), "oPhgD%d", i + 1);
            int dr = web_form_get_int(body, name, (int)b->phg_dir);
            if (dr < 0)
                dr = 0;
            if (dr > 8)
                dr = 8;
            b->phg_dir = (uint8_t)dr;
        }

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

    // The page rendered next is built from the live settings, so the save
    // result is what decides whether the operator is told this reached flash.
    bool ok = objitems_save(&set);
    if (!ok)
        ESP_LOGE(TAG, "objects/items could not be written to flash");

    web_send_save_result(req, ok, "/objects");
    return ESP_OK;
}
