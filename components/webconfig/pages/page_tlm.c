// @file page_tlm.c
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
// @brief Web admin "Telemetry" page: renders and saves the own-beacon
// telemetry channel 0 configuration.
//
// This page mirrors the aprs-telemetry-configurator layout, grouped into:
//   - Beacon            : enable, station-data reuse, RF/INET master enables,
//                         callsign/SSID, path bitmask, data interval.
//   - Report Parameters : digipeater path, destination (TOCALL), sequence
//                         auto-increment, analog field width, trailing-channel
//                         omission, trailing comment, and how many analog /
//                         digital channels are transmitted.
//   - Definition Messages : which PARM/UNIT/EQNS/BITS metadata messages are
//                         generated, and their interval.
//   - Analog Channels (A1-A5) : one collapsible accordion card per channel
//                         (only one open at a time) with per-channel
//                         name/unit, calibration quadratic (a,b,c), raw
//                         input range, displayed decimals, enable and
//                         source (the source sensor is chosen from the
//                         sensors_local registry - drivers advertising the
//                         matching analog channel). Each card's header
//                         shows a live "real value" box (raw sensor
//                         reading run through a*x^2+b*x+c, refreshed via
//                         GET /tlm/values every 2s - see
//                         page_tlm_values_get()) and the card body offers
//                         a "2-point calibration wizard" button that
//                         derives b/c (forcing a=0) from two
//                         raw-reading/known-value pairs.
//   - Digital Channels (B1-B8) : per-bit enable, sense, label, ON-state label,
//                         source (the live sensor from the sensors_local
//                         registry driving that bit), and per-bit IGate/RF
//                         routing.
//
// Telemetry configuration lives in its own LittleFS file
// (/storage/telemetry.json), NOT in g_config - see telemetry.h. This page
// therefore loads/saves it through the telemetry_config_* API rather than
// touching g_config or app_config_save() (mirrors page_bulletins.c). The one
// exception is "Use My Station Data", which - exactly like page_wx.c - copies
// the callsign out of g_config.my_callsign when ticked.
//
// Analog channel names/units are stored in telemetry_config_t.PARM[0..4] /
// UNIT[0..4]; each digital bit's label lives in tlm_bit_name[i] and its
// ON-state label in UNIT[TLM_CH + i], matching the on-air PARM/UNIT layout
// documented in telemetry.h.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "pages.h"
#include "sensors_local.h"
#include "telemetry.h"
#include "translations.h"
#include "weather_telemetry.h" // aprs_telemetry_report_t / APRS_TELEMETRY_ANALOG_CHANNELS - GET /tlm/values raw analog read
#include "web_common.h"

static const char *TAG = "page_tlm";

// ------------------------------------------------------------------------
// Small shared render helpers
// ------------------------------------------------------------------------

// Emits the <option> list (including the "(none)" = 255 entry) for a telemetry
// "Source" <select>, populated from the live sensors_local registry: every
// registered driver that advertises ::SENSOR_LOCAL_DATA_TELEMETRY *and* drives
// the channel identified by @p chan_mask is offered, each option showing the
// channel *number and name* ("0: bme280 A1", ...). The caller has already
// emitted the enclosing <select ...> and closes it afterwards. Mirrors the
// channel picker in page_wx.c.
//
// @param ana_idx For an ANALOG (A1-A5) picker, the 0-based analog channel
//                index (0..4), used to look up that driver's suggested
//                Unit/eng_min/eng_max via ::sensor_local_properties_tlm_ana_hint
//                and emit them as `data-unit`/`data-min`/`data-max`
//                attributes on the option (consumed by the page's
//                tlmSourceAutofill() script to auto-fill the row's Unit/Raw
//                Min/Raw Max/B/C fields). Pass -1 for a DIGITAL (B1-B8)
//                picker, where no such hint applies.
static void tlm_channel_options(httpd_req_t *req, sensor_local_tlm_channel_mask_t chan_mask, uint8_t selected, int ana_idx) {
    char buf[192];
    snprintf(buf, sizeof(buf), "<option value='%u'%s>%s</option>", (unsigned)SENSOR_LOCAL_CH_NONE, (selected == SENSOR_LOCAL_CH_NONE) ? " selected" : "",
             TR_WX_CHANNEL_NONE);
    httpd_resp_sendstr_chunk(req, buf);

    size_t n = sensors_local_count();
    for (size_t ch = 0; ch < n; ch++) {
        sensor_local_driver_t *d = sensors_local_get(ch);
        if (d == NULL || !(d->capabilities & SENSOR_LOCAL_DATA_TELEMETRY))
            continue; // not a telemetry sensor: skip
        if (!sensor_local_properties_has_tlm(d->properties, chan_mask))
            continue; // telemetry sensor, but doesn't drive THIS channel
        char nm[80];
        sensor_local_properties_tlm_label(d->properties, chan_mask, nm, sizeof(nm));

        if (ana_idx >= 0) {
            // Analog picker: attach the driver's suggested unit/eng range (if
            // any) as data-* attributes so the browser can auto-fill the
            // row's Unit/Raw Min/Raw Max/B/C fields on selection, without an
            // extra round trip to the server. Uses its own (wider) buffer,
            // since the extra data-* attributes can exceed the 192-byte
            // buffer used for the plain digital-channel option string above.
            const char *hint_unit = NULL;
            float hint_min = 0.0f, hint_max = 0.0f;
            bool has_range = sensor_local_properties_tlm_ana_hint(d->properties, (unsigned)ana_idx, &hint_unit, &hint_min, &hint_max);
            char esc_unit[24];
            web_html_attr_escape(hint_unit ? hint_unit : "", esc_unit, sizeof(esc_unit));
            char abuf[256];
            snprintf(abuf, sizeof(abuf), "<option value='%u'%s data-unit='%s' data-min='%g' data-max='%g' data-hasrange='%d'>%u: %.40s</option>", (unsigned)ch,
                     (selected == ch) ? " selected" : "", esc_unit, (double)hint_min, (double)hint_max, has_range ? 1 : 0, (unsigned)ch, nm);
            httpd_resp_sendstr_chunk(req, abuf);
            continue;
        }
        snprintf(buf, sizeof(buf), "<option value='%u'%s>%u: %.40s</option>", (unsigned)ch, (selected == ch) ? " selected" : "", (unsigned)ch, nm);
        httpd_resp_sendstr_chunk(req, buf);
    }
}

// Digital bit (B1-B8) "Source" picker: bare <select name='tlmBitCh<bit>'> for a
// table cell. bit is 0-based; digital channels live at mask bits 1<<(5+bit).
static void tlm_bit_channel_select(httpd_req_t *req, int bit, uint8_t selected) {
    char buf[80];
    snprintf(buf, sizeof(buf), "<select name='tlmBitCh%d' style='min-width:150px'>", bit);
    httpd_resp_sendstr_chunk(req, buf);
    tlm_channel_options(req, (sensor_local_tlm_channel_mask_t)(1u << (5 + bit)), selected, -1);
    httpd_resp_sendstr_chunk(req, "</select>");
}

// Analog channel (A1-A5) "Source" picker: labeled <select name='anaCh<idx>'>
// for use inside a fieldset. idx is 0-based (A1 = index 0 = mask 1<<0).
static void tlm_ana_channel_select(httpd_req_t *req, int idx, uint8_t selected) {
    char name[16];
    snprintf(name, sizeof(name), "anaCh%d", idx);
    web_select_open(req, TR_TLM_SOURCE, name);
    tlm_channel_options(req, (sensor_local_tlm_channel_mask_t)(1u << idx), selected, idx);
    web_select_close(req);
}

// Report "Path (digipeaters)" picker: a <select name='rptPath'> whose choices
// are the Digipeater Path Aliases (g_config.path[0..3]); the value stored is
// the alias string itself. The currently-stored path is pre-selected and, if
// it matches none of the aliases, preserved as an extra selected option so a
// hand-entered value is never silently dropped.
static void tlm_path_select(httpd_req_t *req, const char *current) {
    char aliases[4][72];
    app_config_lock();
    for (int i = 0; i < 4; i++) {
        strncpy(aliases[i], g_config.path[i], sizeof(aliases[i]) - 1);
        aliases[i][sizeof(aliases[i]) - 1] = 0;
    }
    app_config_unlock();

    web_select_open(req, TR_TLM_PATH_DIGIS, "rptPath");

    char esc[200], buf[520];
    bool matched = (current == NULL || current[0] == 0);
    snprintf(buf, sizeof(buf), "<option value=''%s>%s</option>", matched ? " selected" : "", TR_WX_CHANNEL_NONE);
    httpd_resp_sendstr_chunk(req, buf);

    for (int i = 0; i < 4; i++) {
        if (aliases[i][0] == 0)
            continue;
        bool sel = (current != NULL && strcmp(current, aliases[i]) == 0);
        if (sel)
            matched = true;
        web_html_attr_escape(aliases[i], esc, sizeof(esc));
        snprintf(buf, sizeof(buf), "<option value='%s'%s>%s</option>", esc, sel ? " selected" : "", esc);
        httpd_resp_sendstr_chunk(req, buf);
    }

    // Keep an off-list stored path visible & selected instead of losing it.
    if (!matched && current != NULL && current[0] != 0) {
        web_html_attr_escape(current, esc, sizeof(esc));
        snprintf(buf, sizeof(buf), "<option value='%s' selected>%s</option>", esc, esc);
        httpd_resp_sendstr_chunk(req, buf);
    }

    web_select_close(req);
}

// ------------------------------------------------------------------------
// Sections (GET)
// ------------------------------------------------------------------------

static void send_beacon_form(httpd_req_t *req, const telemetry_config_t *cfg) {
    web_fieldset_open(req, TR_F_BEACON);
    web_field_checkbox(req, TR_TLM_ENABLE_TELEMETRY, "tlm0En", cfg->en);
    // "Use My Station Data": disables + auto-fills the callsign field from
    // g_config.my_callsign (same helper page_wx.c uses); no lat/lon/alt here.
    web_field_use_station_data(req, "tlm0UseStation", cfg->use_station, "tlm0Mycall", NULL, NULL, NULL);
    web_field_checkbox(req, TR_F_SEND_VIA_RF, "tlm0Tx2rf", cfg->tx2rf);
    web_field_checkbox(req, TR_F_SEND_VIA_INTERNET, "tlm0Tx2inet", cfg->tx2inet);
    web_field_text(req, TR_F_MY_CALLSIGN, "tlm0Mycall", cfg->mycall, 9);
    web_field_int(req, TR_F_SSID, "tlm0SSID", cfg->ssid, WEB_RANGE_SSID_MIN, WEB_RANGE_SSID_MAX);
    web_field_path_checkboxes(req, "tlm0Path", cfg->path);
    web_field_int(req, TR_F_DATA_INTERVAL_S, "tlm0DataInv", cfg->data_interval, WEB_RANGE_INTERVAL_S_MIN, WEB_RANGE_INTERVAL_S_MAX);
    web_fieldset_close(req);
}

static void send_report_params_form(httpd_req_t *req, const telemetry_config_t *cfg) {
    web_fieldset_open(req, TR_TLM_REPORT_PARAMS);
    tlm_path_select(req, cfg->report_path);
    web_field_text(req, TR_TLM_DESTINATION, "tocall", cfg->tocall, 6);
    web_field_checkbox(req, TR_TLM_AUTO_INC_SEQ, "autoSeq", cfg->auto_seq);

    web_select_open(req, TR_TLM_ANALOG_FIELD_WIDTH, "fieldW");
    web_select_option(req, 3, TR_TLM_FIELDW_3DIGIT, cfg->field_width == 3);
    web_select_option(req, 0, TR_TLM_FIELDW_AUTO, cfg->field_width != 3);
    web_select_close(req);

    web_field_checkbox(req, TR_TLM_OMIT_TRAILING, "omitTrail", cfg->omit_trailing);
    web_field_text(req, TR_TLM_TRAIL_COMMENT, "trailCmt", cfg->trail_comment, 31);
    web_field_checkbox(req, TR_TLM_COMMENT_TLM, "cmtTlm", cfg->comment_telemetry);

    web_select_open(req, TR_TLM_ANALOG_COUNT, "anaCount");
    for (int c = 1; c <= TLM_CH; c++) {
        char lbl[8];
        snprintf(lbl, sizeof(lbl), "%d", c);
        web_select_option(req, c, lbl, cfg->analog_count == c);
    }
    web_select_close(req);

    web_select_open(req, TR_TLM_DIGITAL_COUNT, "digCount");
    for (int c = 0; c <= TLM_BIT_NUM; c++) {
        char lbl[8];
        snprintf(lbl, sizeof(lbl), "%d", c);
        web_select_option(req, c, lbl, cfg->digital_count == c);
    }
    web_select_close(req);
    web_fieldset_close(req);
}

static void send_defmsg_form(httpd_req_t *req, const telemetry_config_t *cfg) {
    web_fieldset_open(req, TR_TLM_DEF_MESSAGES);
    web_field_checkbox(req, TR_TLM_GEN_PARM, "genPARM", cfg->gen_parm);
    web_field_checkbox(req, TR_TLM_GEN_UNIT, "genUNIT", cfg->gen_unit);
    web_field_checkbox(req, TR_TLM_GEN_EQNS, "genEQNS", cfg->gen_eqns);
    web_field_checkbox(req, TR_TLM_GEN_BITS, "genBITS", cfg->gen_bits);
    web_field_int(req, TR_F_PARM_UNIT_EQNS_INTERVAL_S, "tlm0InfoInv", cfg->info_interval, WEB_RANGE_INTERVAL_S_MIN, WEB_RANGE_INTERVAL_S_MAX);
    web_fieldset_close(req);
}

// One collapsible accordion card per analog channel (A1-A5): a
// always-visible header (tag, name, live calibrated value, caret) plus a
// body - hidden unless this is the open channel - holding every editable
// field. Only channel 0 starts open; tlmAccordionClick() (see the injected
// script in page_tlm_get()) closes any other open card whenever one is
// clicked, so at most one channel is expanded at a time.
//
// The header's live value and the body's equation-preview line are filled
// in/refreshed by tlmRefreshValues() (polls GET /tlm/values every 2s and
// on any relevant input's change/input event) rather than at render time,
// since the calibrated value depends on live sensor data, not just the
// stored config.
static void send_analog_form(httpd_req_t *req, const telemetry_config_t *cfg) {
    web_fieldset_open(req, TR_TLM_ANALOG_LEGEND);
    web_field_checkbox(req, TR_F_BEACON_VIA_RF, "anaRF", cfg->analog_tx2rf);
    web_field_checkbox(req, TR_F_BEACON_VIA_INTERNET, "anaInet", cfg->analog_tx2inet);

    httpd_resp_sendstr_chunk(req, "<div id='achanWrap'>");
    for (int i = 0; i < TLM_CH; i++) {
        char buf[420], name[24];
        char esc_name[24], esc_unit[16];
        web_html_attr_escape(cfg->PARM[i], esc_name, sizeof(esc_name));
        web_html_attr_escape(cfg->UNIT[i], esc_unit, sizeof(esc_unit));

        snprintf(buf, sizeof(buf),
                 "<div class='achan%s' id='achan%d'>"
                 "<div class='achan-head' onclick='tlmAccordionClick(%d)'>"
                 "<span class='achan-tag'>A%d</span>"
                 "<span class='achan-name' id='achanName%d'>%.23s <span class='faint'>&mdash; %.15s</span></span>"
                 "<span class='achan-val' id='achanVal%d'>&hellip;</span>"
                 "<span class='achan-caret'>&#9654;</span>"
                 "</div><div class='achan-body'>",
                 (i == 0) ? " open" : "", i, i, i + 1, i, esc_name[0] ? esc_name : "(unnamed)", esc_unit[0] ? esc_unit : "no unit", i);
        httpd_resp_sendstr_chunk(req, buf);

        snprintf(name, sizeof(name), "anaEn%d", i);
        web_field_checkbox(req, TR_F_ENABLE, name, cfg->ana_enable[i]);

        tlm_ana_channel_select(req, i, cfg->tlm_ana_channel[i]);

        snprintf(name, sizeof(name), "anaName%d", i);
        web_field_text(req, TR_F_NAME, name, cfg->PARM[i], 8);

        snprintf(name, sizeof(name), "anaUnit%d", i);
        web_field_text(req, TR_TLM_UNIT, name, cfg->UNIT[i], 6);

        snprintf(name, sizeof(name), "anaRawMin%d", i);
        web_field_int(req, TR_TLM_RAW_MIN, name, cfg->ana_raw_min[i], TLM_RAW_RANGE_MIN, TLM_RAW_RANGE_MAX);

        snprintf(name, sizeof(name), "anaRawMax%d", i);
        web_field_int(req, TR_TLM_RAW_MAX, name, cfg->ana_raw_max[i], TLM_RAW_RANGE_MIN, TLM_RAW_RANGE_MAX);

        snprintf(name, sizeof(name), "anaA%d", i);
        web_field_float(req, TR_TLM_COEF_A, name, cfg->ana_a[i], "any", TLM_COEF_RANGE_MIN, TLM_COEF_RANGE_MAX);

        snprintf(name, sizeof(name), "anaB%d", i);
        web_field_float(req, TR_TLM_COEF_B, name, cfg->ana_b[i], "any", TLM_COEF_RANGE_MIN, TLM_COEF_RANGE_MAX);

        snprintf(name, sizeof(name), "anaC%d", i);
        web_field_float(req, TR_TLM_COEF_C, name, cfg->ana_c[i], "any", TLM_COEF_RANGE_MIN, TLM_COEF_RANGE_MAX);

        snprintf(name, sizeof(name), "anaDec%d", i);
        web_field_int(req, TR_TLM_DECIMALS, name, cfg->ana_dec[i], 0, 9);

        snprintf(buf, sizeof(buf), "<div class='eqn-preview' id='achanEqn%d'>&hellip;</div>", i);
        httpd_resp_sendstr_chunk(req, buf);

        snprintf(buf, sizeof(buf),
                 "<div class='btnrow' style='display:flex;gap:8px;margin-top:10px;'>"
                 "<button type='button' onclick='tlmCalibWizard(%d)'>%s</button></div></div></div>",
                 i, TR_TLM_CALIB_WIZARD);
        httpd_resp_sendstr_chunk(req, buf);
    }
    httpd_resp_sendstr_chunk(req, "</div>");
    web_fieldset_close(req);

    // Accordion open/close + live-value poller + 2-point calibration wizard.
    //
    // tlmRefreshValues(): every 2s (and immediately on load, plus on any
    // channel-select/coefficient-field change via the oninput/onchange
    // handlers wired below), reads whichever source channel is *currently
    // selected* in each row straight from the DOM, asks the firmware for
    // that channel's live raw reading via GET /tlm/values, applies the
    // quadratic a*x^2+b*x+c using whatever coefficients are *currently
    // typed* in the form (so edits preview instantly, even before Save),
    // and fills in both the header's compact value and the body's full
    // equation-preview line. tlmReqSeq/tlmLastApplied guard against a
    // request race the same way page_wx.c's wxRefreshValues() does: an
    // onchange/oninput-triggered refresh can resolve out of order against
    // the running 2s poll, so only the highest-numbered response in flight
    // is ever allowed to write into the DOM.
    //
    // tlmAccordionClick(i): opens channel i and closes every other channel,
    // so only one A-channel is ever expanded at a time.
    //
    // tlmCalibWizard(i): a 2-point linear calibration helper. Prompts for
    // two raw readings (x1,x2) and their known real-world values (y1,y2),
    // solves the line through them (b=(y2-y1)/(x2-x1), c=y1-b*x1), forces
    // the quadratic term to 0, and writes a/b/c back into the form's own
    // inputs (firing their oninput handler so the preview updates
    // immediately). It edits the real form fields directly rather than an
    // in-memory JS state object, so a Save persists exactly what the wizard
    // computed.
    {
        // Sent as separate chunks (rather than one giant snprintf) so no
        // fixed-size buffer has to hold the whole script at once - the
        // static JS below is fixed literal text with no risk of overflow,
        // and only the small piece that needs TLM_CH/translated strings
        // substituted in uses a (generously sized, but still bounded)
        // snprintf buffer.
        char head[64];
        snprintf(head, sizeof(head), "<script>var TLM_N=%d;", (int)TLM_CH);
        httpd_resp_sendstr_chunk(req, head);

        httpd_resp_sendstr_chunk(
            req, "var tlmReqSeq=0;"
                 "var tlmLastApplied=0;"
                 "function tlmFmtNum(v,dec){"
                 "if(dec===undefined)dec=2;"
                 "if(Math.abs(v)<1e-9)v=0;"
                 "return v.toFixed(dec).replace(/\\.0+$/,'').replace(/(\\.\\d*?)0+$/,'$1').replace(/\\.$/,'');"
                 "}"
                 "function tlmCoefs(i){"
                 "function g(id,def){var e=document.getElementsByName(id)[0];var v=e?parseFloat(e.value):NaN;return isNaN(v)?def:v;}"
                 "return {a:g('anaA'+i,0),b:g('anaB'+i,0),c:g('anaC'+i,0),dec:Math.max(0,Math.min(6,Math.round(g('anaDec'+i,2)))),"
                 "unit:(document.getElementsByName('anaUnit'+i)[0]||{}).value||'',name:(document.getElementsByName('anaName'+i)[0]||{}).value||''};"
                 "}"
                 // tlmSourceAutofill(i): fired when row i's "Source" <select> (anaCh<i>)
                 // changes. Reads the just-selected <option>'s data-unit/data-min/
                 // data-max/data-hasrange attributes (emitted server-side by
                 // tlm_channel_options() from the driver's sensor_local_properties_t
                 // hint, see sensor_local_properties_tlm_ana_hint()) and, if that
                 // sensor advertises a suggested engineering range, fills in Unit/
                 // Raw Min/Raw Max and derives the standard APRS101 8-bit linear
                 // calibration a=0, b=(max-min)/255, c=min - firing input/change on
                 // each field so the live preview updates immediately. Fields stay
                 // freely editable afterwards; picking "(none)" or a sensor with no
                 // hint simply leaves whatever is already typed untouched.
                 "function tlmSourceAutofill(i){"
                 "var sel=document.getElementsByName('anaCh'+i)[0];"
                 "if(!sel)return;"
                 "var opt=sel.options[sel.selectedIndex];"
                 "if(!opt)return;"
                 "function setv(name,v){var e=document.getElementsByName(name)[0];if(!e)return;e.value=v;e.dispatchEvent(new "
                 "Event('input'));e.dispatchEvent(new Event('change'));}"
                 "var unit=opt.getAttribute('data-unit');"
                 "if(unit)setv('anaUnit'+i,unit);"
                 "if(opt.getAttribute('data-hasrange')==='1'){"
                 "var min=parseFloat(opt.getAttribute('data-min'));"
                 "var max=parseFloat(opt.getAttribute('data-max'));"
                 "if(!isNaN(min)&&!isNaN(max)){"
                 "setv('anaRawMin'+i,0);"
                 "setv('anaRawMax'+i,255);"
                 "setv('anaA'+i,0);"
                 "setv('anaB'+i,Math.round(((max-min)/255)*1e6)/1e6);"
                 "setv('anaC'+i,Math.round(min*1e6)/1e6);"
                 "}"
                 "}"
                 "}"
                 "function tlmAccordionClick(i){"
                 "for(var k=0;k<TLM_N;k++){"
                 "var c=document.getElementById('achan'+k);"
                 "if(!c)continue;"
                 "if(k===i)c.classList.toggle('open');"
                 "else c.classList.remove('open');"
                 "}"
                 "}"
                 "function tlmRefreshValues(){"
                 "var seq=++tlmReqSeq;"
                 "var q=[];"
                 "for(var i=0;i<TLM_N;i++){"
                 "var sel=document.getElementsByName('anaCh'+i)[0];"
                 "q.push('ch'+i+'='+(sel?sel.value:255));"
                 "}"
                 "fetch('/tlm/values?'+q.join('&')).then(function(r){return r.json();}).then(function(raws){"
                 "if(seq<tlmLastApplied)return;"
                 "tlmLastApplied=seq;"
                 "for(var i=0;i<TLM_N;i++){"
                 "var raw=raws[i];"
                 "var cf=tlmCoefs(i);"
                 "var valEl=document.getElementById('achanVal'+i);"
                 "var eqnEl=document.getElementById('achanEqn'+i);"
                 "var nameEl=document.getElementById('achanName'+i);"
                 "if(nameEl)nameEl.innerHTML=(cf.name||'(unnamed)')+' <span class=\\'faint\\'>&mdash; '+(cf.unit||'no unit')+'</span>';"
                 "if(raw===null||raw===undefined){"
                 "if(valEl)valEl.innerHTML='-';"
                 "if(eqnEl)eqnEl.innerHTML='value = <b>'+cf.a+'</b>&middot;x&sup2; + <b>'+cf.b+'</b>&middot;x + <b>'+cf.c+'</b> (no source selected)';"
                 "continue;"
                 "}"
                 "var val=cf.a*raw*raw+cf.b*raw+cf.c;"
                 "if(valEl)valEl.innerHTML=tlmFmtNum(val,cf.dec)+' <span class=\\'faint\\'>'+(cf.unit||'')+'</span>';"
                 "if(eqnEl)eqnEl.innerHTML='value = <b>'+cf.a+'</b>&middot;x&sup2; + <b>'+cf.b+'</b>&middot;x + <b>'+cf.c+'</b> &nbsp;&rarr;&nbsp; at "
                 "x='+raw+': <b>'+tlmFmtNum(val,cf.dec)+' '+(cf.unit||'')+'</b>';"
                 "}"
                 "}).catch(function(){});"
                 "}");

        // Translated prompt/alert strings for the calibration wizard, kept
        // in their own small snprintf (bounded and easy to verify against
        // the buffer size) rather than folded into the large static block
        // above.
        char calib[1400];
        snprintf(calib, sizeof(calib),
                 "function tlmCalibWizard(i){"
                 "var rawMinEl=document.getElementsByName('anaRawMin'+i)[0];"
                 "var rawMaxEl=document.getElementsByName('anaRawMax'+i)[0];"
                 "var x1=parseFloat(prompt('%s',''+(rawMinEl?rawMinEl.value:0)));"
                 "if(isNaN(x1)){alert('%s');return;}"
                 "var y1=parseFloat(prompt('%s','0'));"
                 "if(isNaN(y1)){alert('%s');return;}"
                 "var x2=parseFloat(prompt('%s',''+(rawMaxEl?rawMaxEl.value:255)));"
                 "if(isNaN(x2)){alert('%s');return;}"
                 "var y2=parseFloat(prompt('%s','100'));"
                 "if(isNaN(y2)){alert('%s');return;}"
                 "if(x2===x1){alert('%s');return;}"
                 "var b=(y2-y1)/(x2-x1);"
                 "var c=y1-b*x1;"
                 "function setv(name,v){var e=document.getElementsByName(name)[0];if(!e)return;e.value=v;e.dispatchEvent(new "
                 "Event('input'));e.dispatchEvent(new Event('change'));}"
                 "setv('anaA'+i,0);"
                 "setv('anaB'+i,Math.round(b*1e6)/1e6);"
                 "setv('anaC'+i,Math.round(c*1e6)/1e6);"
                 "tlmRefreshValues();"
                 "}",
                 TR_TLM_CALIB_PROMPT_X1, TR_TLM_CALIB_CANCELLED, TR_TLM_CALIB_PROMPT_Y1, TR_TLM_CALIB_CANCELLED, TR_TLM_CALIB_PROMPT_X2, TR_TLM_CALIB_CANCELLED,
                 TR_TLM_CALIB_PROMPT_Y2, TR_TLM_CALIB_CANCELLED, TR_TLM_CALIB_SAME_X);
        httpd_resp_sendstr_chunk(req, calib);

        httpd_resp_sendstr_chunk(req, "document.addEventListener('input',function(e){"
                                      "var n=e.target.name||'';"
                                      "if(/^ana(A|B|C|Dec|Name|Unit)\\d+$/.test(n))tlmRefreshValues();"
                                      "});"
                                      "document.addEventListener('change',function(e){"
                                      "var n=e.target.name||'';"
                                      "var m=/^anaCh(\\d+)$/.exec(n);"
                                      "if(m){tlmSourceAutofill(parseInt(m[1],10));tlmRefreshValues();}"
                                      "});"
                                      "tlmRefreshValues();"
                                      "var tlmValTimer=setInterval(tlmRefreshValues,2000);"
                                      "window.addEventListener('beforeunload',function(){clearInterval(tlmValTimer);});"
                                      "</script>");
    }
}

static void send_digital_form(httpd_req_t *req, const telemetry_config_t *cfg) {
    web_fieldset_open(req, TR_TLM_DIGITAL_LEGEND);
    web_field_checkbox(req, TR_F_BEACON_VIA_RF, "digRF", cfg->digital_tx2rf);
    web_field_checkbox(req, TR_F_BEACON_VIA_INTERNET, "digInet", cfg->digital_tx2inet);
    web_field_text(req, TR_F_NAME, "projTitle", cfg->proj_title, 23);

    httpd_resp_sendstr_chunk(req, "<div class='table-wrap'><table><tr>"
                                  "<th>" TR_TLM_BIT "</th><th>" TR_F_ENABLE "</th><th>" TR_TLM_SENSE "</th>"
                                  "<th>" TR_TLM_LABEL "</th><th>" TR_TLM_ON_STATE "</th><th>" TR_TLM_SOURCE "</th>"
                                  "<th>" TR_F_IGATE "</th><th>" TR_TLM_RF "</th></tr>");
    for (int i = 0; i < TLM_BIT_NUM; i++) {
        char esc_label[128], esc_on[48], row[600];
        web_html_attr_escape(cfg->tlm_bit_name[i], esc_label, sizeof(esc_label));
        web_html_attr_escape(cfg->UNIT[TLM_CH + i], esc_on, sizeof(esc_on));

        snprintf(row, sizeof(row),
                 "<tr><td>B%d</td>"
                 "<td><input type='checkbox' name='bitEn%d' %s></td>"
                 "<td><input type='checkbox' name='bitSense%d' %s></td>"
                 "<td><input type='text' name='bitName%d' value='%s' maxlength='20'></td>"
                 "<td><input type='text' name='bitOn%d' value='%s' maxlength='6'></td>"
                 "<td>",
                 i + 1, i, cfg->bit_enable[i] ? "checked" : "", i, cfg->bit_sense[i] ? "checked" : "", i, esc_label, i, esc_on);
        httpd_resp_sendstr_chunk(req, row);

        // "Source" column: pick the registered bit sensor from sensors_local.
        tlm_bit_channel_select(req, i, cfg->tlm_bit_channel[i]);

        snprintf(row, sizeof(row),
                 "</td><td><input type='checkbox' name='tlmBitIgate%d' %s></td>"
                 "<td><input type='checkbox' name='tlmBitRF%d' %s></td></tr>",
                 i, cfg->tlm_bit_igate[i] ? "checked" : "", i, cfg->tlm_bit_rf[i] ? "checked" : "");
        httpd_resp_sendstr_chunk(req, row);
    }
    httpd_resp_sendstr_chunk(req, "</table></div>");
    web_fieldset_close(req);
}

// GET /tlm/values?ch0=<channel>&ch1=<channel>&... - one "chN" query
// parameter per analog channel row (A1-A5), giving the sensors_local
// channel index currently selected in that row's "Source" <select>
// (255/absent = "(none)"). Reads the selected drivers fresh
// (sensors_local_save_one(), same as page_wx_values_get() and
// telemetry_refresh_now()'s per-bit digital read) and returns a JSON array
// of RAW analog values (before the quadratic a/b/c scaling - the browser
// applies that itself from whatever coefficients are currently typed in
// the form, so edits preview live without a round trip). Each slot is
// either a plain number or `null` if the row has no channel selected, the
// channel doesn't exist, isn't a telemetry sensor, or didn't mark that
// channel index enabled this cycle. Polled every 2s by the page's script.
//
// Each DISTINCT channel is read exactly once per request: one read fills the
// whole analog[0..4] array, so a driver feeding several rows is asked for its
// reading once and every row it feeds is answered from that snapshot, rather
// than once per row on the same bus the rest of the firmware shares. The same
// reasoning as page_wx_values_get() applies, including why the snapshot is
// not kept between requests.
esp_err_t page_tlm_values_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    char query[256];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
        query[0] = 0;

    // Channel selected by each row, -1 for "(none)" or out of range, and the
    // JSON slot each row will contribute. Both are filled before anything is
    // sent, because a row's value can be produced by a read issued for an
    // earlier row.
    int row_ch[TLM_CH];
    char out[TLM_CH][32];
    for (int i = 0; i < TLM_CH; i++) {
        char key[16];
        snprintf(key, sizeof(key), "ch%d", i);
        int ch = web_form_get_int(query, key, 255);
        row_ch[i] = (ch >= 0 && ch < 255) ? ch : -1;
        snprintf(out[i], sizeof(out[i]), "null");
    }

    for (int i = 0; i < TLM_CH; i++) {
        if (row_ch[i] < 0)
            continue;

        // A channel is read by the first row that names it; later rows on the
        // same channel are served from that read.
        bool already_read = false;
        for (int j = 0; j < i; j++) {
            if (row_ch[j] == row_ch[i]) {
                already_read = true;
                break;
            }
        }
        if (already_read)
            continue;

        bool analog_enabled[APRS_TELEMETRY_ANALOG_CHANNELS] = { 0 };
        double analog[APRS_TELEMETRY_ANALOG_CHANNELS] = { 0 };
        aprs_telemetry_report_t scratch_tlm = { 0 };
        scratch_tlm.analog_count = APRS_TELEMETRY_ANALOG_CHANNELS;
        scratch_tlm.analog_enabled = analog_enabled;
        scratch_tlm.analog = analog;

        weather_telemetry_data_t scratch_data = { 0 };
        scratch_data.telemetry_report = &scratch_tlm;
        scratch_data.telemetry_report_qty = 1;

        if (sensors_local_save_one((size_t)row_ch[i], &scratch_data, SENSOR_LOCAL_DATA_TELEMETRY) != ESP_OK)
            continue; // channel gone or driver failed: every row on it stays null

        // Row k (A1..A5) maps 1:1 onto analog[0..4]: the "Source" <select> on
        // that row only ever offers drivers whose properties advertise THAT
        // analog slot (tlm_channel_options() filters on chan_mask = 1<<k), so
        // analog[k] is always the right slot to read back for it.
        for (int k = i; k < TLM_CH; k++) {
            if (row_ch[k] == row_ch[i] && k < (int)APRS_TELEMETRY_ANALOG_CHANNELS && analog_enabled[k])
                snprintf(out[k], sizeof(out[k]), "%g", analog[k]);
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "[");
    for (int i = 0; i < TLM_CH; i++) {
        if (i > 0)
            httpd_resp_sendstr_chunk(req, ",");
        httpd_resp_sendstr_chunk(req, out[i]);
    }
    httpd_resp_sendstr_chunk(req, "]");
    // Final zero-length chunk to close the response (see page_wx_values_get()
    // for why this is required - without it fetch() never resolves).
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

esp_err_t page_tlm_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    telemetry_config_t cfg;
    telemetry_config_load(&cfg);

    web_send_header(req, TR_F_TELEMETRY, "tlm");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/tlm'>");

    send_beacon_form(req, &cfg);
    send_report_params_form(req, &cfg);
    send_defmsg_form(req, &cfg);
    send_analog_form(req, &cfg);
    send_digital_form(req, &cfg);

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

// ------------------------------------------------------------------------
// Parse (POST)
// ------------------------------------------------------------------------

static void parse_beacon(const char *body, telemetry_config_t *cfg, const char path_preset[4][72]) {
    cfg->en = web_form_get_bool(body, "tlm0En");
    cfg->use_station = web_form_get_bool(body, "tlm0UseStation");
    cfg->tx2rf = web_form_get_bool(body, "tlm0Tx2rf");
    cfg->tx2inet = web_form_get_bool(body, "tlm0Tx2inet");
    if (cfg->use_station) {
        // The callsign input is disabled in the browser when this is ticked,
        // so it is NOT submitted; take it straight from Station instead (same
        // as page_wx.c).
        app_config_lock();
        strncpy(cfg->mycall, g_config.my_callsign, sizeof(cfg->mycall) - 1);
        cfg->mycall[sizeof(cfg->mycall) - 1] = 0;
        app_config_unlock();
    } else {
        char mycall[10];
        web_form_get_call(body, "tlm0Mycall", mycall, sizeof(mycall));
        strncpy(cfg->mycall, mycall, sizeof(cfg->mycall) - 1);
        cfg->mycall[sizeof(cfg->mycall) - 1] = 0;
    }
    cfg->ssid = web_form_get_ssid(body, "tlm0SSID", 0);
    cfg->path = app_config_path_mask_clamp(web_form_get_path_mask(body, "tlm0Path"), path_preset);
    cfg->data_interval = (uint16_t)web_form_get_int(body, "tlm0DataInv", 0);
    cfg->info_interval = (uint16_t)web_form_get_int(body, "tlm0InfoInv", cfg->info_interval);
}

static void parse_report_params(const char *body, telemetry_config_t *cfg) {
    web_form_get(body, "rptPath", cfg->report_path, sizeof(cfg->report_path));
    web_form_get(body, "tocall", cfg->tocall, sizeof(cfg->tocall));
    cfg->auto_seq = web_form_get_bool(body, "autoSeq");
    cfg->field_width = (uint8_t)web_form_get_int(body, "fieldW", cfg->field_width);
    cfg->omit_trailing = web_form_get_bool(body, "omitTrail");
    web_form_get(body, "trailCmt", cfg->trail_comment, sizeof(cfg->trail_comment));
    cfg->comment_telemetry = web_form_get_bool(body, "cmtTlm");
    cfg->analog_count = (uint8_t)web_form_get_int(body, "anaCount", cfg->analog_count);
    cfg->digital_count = (uint8_t)web_form_get_int(body, "digCount", cfg->digital_count);

    cfg->gen_parm = web_form_get_bool(body, "genPARM");
    cfg->gen_unit = web_form_get_bool(body, "genUNIT");
    cfg->gen_eqns = web_form_get_bool(body, "genEQNS");
    cfg->gen_bits = web_form_get_bool(body, "genBITS");
}

static void parse_analog(const char *body, telemetry_config_t *cfg) {
    cfg->analog_tx2rf = web_form_get_bool(body, "anaRF");
    cfg->analog_tx2inet = web_form_get_bool(body, "anaInet");
    for (int i = 0; i < TLM_CH; i++) {
        char key[24];
        snprintf(key, sizeof(key), "anaEn%d", i);
        cfg->ana_enable[i] = web_form_get_bool(body, key);
        snprintf(key, sizeof(key), "anaCh%d", i);
        cfg->tlm_ana_channel[i] = (uint8_t)web_form_get_int(body, key, cfg->tlm_ana_channel[i]);
        snprintf(key, sizeof(key), "anaName%d", i);
        web_form_get(body, key, cfg->PARM[i], sizeof(cfg->PARM[i]));
        snprintf(key, sizeof(key), "anaUnit%d", i);
        web_form_get(body, key, cfg->UNIT[i], sizeof(cfg->UNIT[i]));
        snprintf(key, sizeof(key), "anaRawMin%d", i);
        cfg->ana_raw_min[i] = web_form_get_int(body, key, cfg->ana_raw_min[i]);
        snprintf(key, sizeof(key), "anaRawMax%d", i);
        cfg->ana_raw_max[i] = web_form_get_int(body, key, cfg->ana_raw_max[i]);
        snprintf(key, sizeof(key), "anaA%d", i);
        cfg->ana_a[i] = web_form_get_float(body, key, cfg->ana_a[i]);
        snprintf(key, sizeof(key), "anaB%d", i);
        cfg->ana_b[i] = web_form_get_float(body, key, cfg->ana_b[i]);
        snprintf(key, sizeof(key), "anaC%d", i);
        cfg->ana_c[i] = web_form_get_float(body, key, cfg->ana_c[i]);
        snprintf(key, sizeof(key), "anaDec%d", i);
        cfg->ana_dec[i] = (uint8_t)web_form_get_int(body, key, cfg->ana_dec[i]);
    }
}

static void parse_digital(const char *body, telemetry_config_t *cfg) {
    cfg->digital_tx2rf = web_form_get_bool(body, "digRF");
    cfg->digital_tx2inet = web_form_get_bool(body, "digInet");
    web_form_get(body, "projTitle", cfg->proj_title, sizeof(cfg->proj_title));
    for (int i = 0; i < TLM_BIT_NUM; i++) {
        char key[24];
        snprintf(key, sizeof(key), "bitEn%d", i);
        cfg->bit_enable[i] = web_form_get_bool(body, key);
        snprintf(key, sizeof(key), "bitSense%d", i);
        cfg->bit_sense[i] = web_form_get_bool(body, key);
        snprintf(key, sizeof(key), "bitName%d", i);
        web_form_get(body, key, cfg->tlm_bit_name[i], sizeof(cfg->tlm_bit_name[i]));
        snprintf(key, sizeof(key), "bitOn%d", i);
        web_form_get(body, key, cfg->UNIT[TLM_CH + i], sizeof(cfg->UNIT[TLM_CH + i]));
        // "Source" column posts the sensors_local channel index as tlmBitCh<i>.
        snprintf(key, sizeof(key), "tlmBitCh%d", i);
        cfg->tlm_bit_channel[i] = (uint8_t)web_form_get_int(body, key, cfg->tlm_bit_channel[i]);
        snprintf(key, sizeof(key), "tlmBitIgate%d", i);
        cfg->tlm_bit_igate[i] = web_form_get_bool(body, key);
        snprintf(key, sizeof(key), "tlmBitRF%d", i);
        cfg->tlm_bit_rf[i] = web_form_get_bool(body, key);
    }
}

esp_err_t page_tlm_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char *body = malloc(WEBCONFIG_POST_BUF_TLM);
    if (!body) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    if (web_read_body(req, body, WEBCONFIG_POST_BUF_TLM) < 0) {
        free(body);
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    telemetry_config_t cfg;
    // Start from the stored config so any field not present in the form keeps
    // its value; the form overwrites everything it does carry.
    telemetry_config_load(&cfg);

    // Snapshot the shared Digipeater Path Alias presets under the config lock
    // (see app_config_path_mask_clamp() in parse_beacon()) rather than
    // touching g_config.path directly while unlocked.
    char path_preset[4][72];
    app_config_lock();
    for (int i = 0; i < 4; i++) {
        strncpy(path_preset[i], g_config.path[i], sizeof(path_preset[i]) - 1);
        path_preset[i][sizeof(path_preset[i]) - 1] = 0;
    }
    app_config_unlock();

    parse_beacon(body, &cfg, path_preset);
    parse_report_params(body, &cfg);
    parse_analog(body, &cfg);
    parse_digital(body, &cfg);

    free(body);
    // The page rendered next is built from the live settings, so the save
    // result is what decides whether the operator is told this reached flash.
    bool ok = telemetry_config_save(&cfg);
    if (!ok)
        ESP_LOGE(TAG, "telemetry configuration could not be written to flash");
    web_send_save_result(req, ok, "/tlm");
    return ESP_OK;
}
