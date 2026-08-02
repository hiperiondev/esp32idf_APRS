/**
 * @file page_station.c
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
 * @brief Web admin "Station" page: renders and saves the single shared "My
 * Station" identity (callsign, latitude, longitude) and its PHG
 * (Power-Height-Gain-Directivity) radio-coverage parameters in g_config.
 * This is the data every other page's "Use My Station Data" checkbox pulls
 * from instead of having the same callsign/position retyped on every
 * IGate/Digipeater/Tracker/Weather page.
 *
 * The PHG height selector is stored internally in feet (the unit the APRS
 * PHG code table is itself defined in - power^2 Watts, 10*2^n feet, dB gain,
 * 45 degrees-per-step directivity) but is displayed/edited on this page in
 * meters, the SI unit, converting to/from feet only for the underlying code.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "objects_items.h"
#include "pages.h"
#include "telemetry.h"
#include "translations.h"
#include "web_common.h"

/**
 * @brief Re-mirror the just-saved "My Station" identity/position/PHG into
 * every other page's fields whose "Use My Station Data" (or "Use My Station
 * Data" PHG variant) is currently enabled.
 *
 * Every consumer page (IGate/Digipeater/Tracker/WX/Message/Telemetry/Objects)
 * only takes its snapshot of g_config.my_* at the moment *that* page itself is
 * saved, since the fields are disabled client-side and never POST while
 * "Use My Station Data" is checked. That means saving the Station page alone
 * left every other page's stored snapshot stale until the user happened to
 * re-save that other page too. This function is called right after the
 * Station page persists g_config.my_*, so all dependent snapshots are
 * refreshed in the same action and nothing goes stale.
 *
 * @note Must be called with app_config_lock() already held (all g_config
 * fields touched here belong to that same lock), and BEFORE app_config_save()
 * so the refreshed values are part of the same config.json write.
 */
static void station_resync_dependents(void) {
    // -- IGate ---------------------------------------------------------------
    if (g_config.igate_use_station) {
        strncpy(g_config.aprs_mycall, g_config.my_callsign, sizeof(g_config.aprs_mycall) - 1);
        g_config.aprs_mycall[sizeof(g_config.aprs_mycall) - 1] = 0;
        g_config.igate_lat = g_config.my_lat;
        g_config.igate_lon = g_config.my_lon;
        g_config.igate_alt = g_config.my_alt;
    }
    if (g_config.igate_phg_use_station) {
        g_config.igate_phg_power = g_config.my_phg_power;
        g_config.igate_phg_gain = g_config.my_phg_gain;
        g_config.igate_phg_height = g_config.my_phg_height;
        g_config.igate_phg_dir = g_config.my_phg_dir;
        strncpy(g_config.igate_phg, g_config.my_phg, sizeof(g_config.igate_phg) - 1);
        g_config.igate_phg[sizeof(g_config.igate_phg) - 1] = 0;
    }

    // -- Digipeater ------------------------------------------------------------
    if (g_config.digi_use_station) {
        strncpy(g_config.digi_mycall, g_config.my_callsign, sizeof(g_config.digi_mycall) - 1);
        g_config.digi_mycall[sizeof(g_config.digi_mycall) - 1] = 0;
        g_config.digi_lat = g_config.my_lat;
        g_config.digi_lon = g_config.my_lon;
        g_config.digi_alt = g_config.my_alt;
    }

    // -- Tracker ---------------------------------------------------------------
    if (g_config.trk_use_station) {
        strncpy(g_config.trk_mycall, g_config.my_callsign, sizeof(g_config.trk_mycall) - 1);
        g_config.trk_mycall[sizeof(g_config.trk_mycall) - 1] = 0;
        g_config.trk_lat = g_config.my_lat;
        g_config.trk_lon = g_config.my_lon;
        g_config.trk_alt = g_config.my_alt;
    }

    // -- Weather -----------------------------------------------------------
    if (g_config.wx_use_station) {
        strncpy(g_config.wx_mycall, g_config.my_callsign, sizeof(g_config.wx_mycall) - 1);
        g_config.wx_mycall[sizeof(g_config.wx_mycall) - 1] = 0;
        g_config.wx_lat = g_config.my_lat;
        g_config.wx_lon = g_config.my_lon;
        g_config.wx_alt = g_config.my_alt;
    }

    // -- Messaging -----------------------------------------------------------
    if (g_config.msg_use_station) {
        strncpy(g_config.msg_mycall, g_config.my_callsign, sizeof(g_config.msg_mycall) - 1);
        g_config.msg_mycall[sizeof(g_config.msg_mycall) - 1] = 0;
    }

    // -- Telemetry (own JSON store, /storage/telemetry.json) -----------------
    // Loaded/saved independently of g_config, so it needs its own
    // read-modify-write here rather than a direct g_config field touch.
    {
        telemetry_config_t tcfg;
        if (telemetry_config_load(&tcfg)) {
            if (tcfg.use_station) {
                strncpy(tcfg.mycall, g_config.my_callsign, sizeof(tcfg.mycall) - 1);
                tcfg.mycall[sizeof(tcfg.mycall) - 1] = 0;
                telemetry_config_save(&tcfg);
            }
        }
    }

    // -- Objects/Items PHG (own JSON store, /storage/objitems.json) ----------
    {
        objitems_t set;
        if (objitems_load(&set)) {
            bool changed = false;
            for (int i = 0; i < OBJITEM_COUNT; i++) {
                objitem_t *b = &set.item[i];
                if (b->phg_use_station) {
                    b->phg_power = g_config.my_phg_power;
                    b->phg_gain = g_config.my_phg_gain;
                    b->phg_height = g_config.my_phg_height;
                    b->phg_dir = g_config.my_phg_dir;
                    memcpy(b->phg, g_config.my_phg, sizeof(b->phg));
                    b->phg[sizeof(b->phg) - 1] = 0;
                    changed = true;
                }
            }
            if (changed)
                objitems_save(&set);
        }
    }
}

esp_err_t page_station_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_STATION, "station");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/station'>");

    web_fieldset_open(req, TR_F_STATION);
    web_field_text(req, TR_F_MY_CALLSIGN, "myCallsign", g_config.my_callsign, 9);
    web_field_float(req, TR_F_LATITUDE, "myLAT", g_config.my_lat, "0.0001", WEB_RANGE_LAT_MIN, WEB_RANGE_LAT_MAX);
    web_field_float(req, TR_F_LONGITUDE, "myLON", g_config.my_lon, "0.0001", WEB_RANGE_LON_MIN, WEB_RANGE_LON_MAX);

    // Position ambiguity and the Maidenhead status prefix are station-wide:
    // both describe how precisely this station is willing to state where it
    // is, which is a property of the station rather than of any one beacon, so
    // all three position beacons (Tracker / IGate / Digipeater) and all three
    // status reports pick them up from here.
    //
    // Ambiguity blanks the least significant minute digits on air (APRS101
    // ch.6). It is what a fixed station uses to publish an approximate
    // location instead of its exact address. Because the compressed position
    // format has no digits to blank, enabling ambiguity makes those beacons
    // fall back to the uncompressed format even if "compressed" is ticked on
    // their own page - the alternative would be silently transmitting the
    // exact position the operator asked to hide.
    web_select_open(req, TR_F_POS_AMBIGUITY, "myAmbiguity");
    {
        static const char *levels[] = { TR_AMB_NONE, TR_AMB_TENTH, TR_AMB_MINUTE, TR_AMB_TEN_MINUTES, TR_AMB_DEGREE };
        for (int i = 0; i <= POS_AMBIGUITY_MAX; i++)
            web_select_option(req, i, levels[i], g_config.pos_ambiguity == (uint8_t)i);
    }
    web_select_close(req);
    web_field_checkbox(req, TR_F_STATUS_GRID, "myStatusGrid", g_config.status_grid_en);
    web_fieldset_close(req);

    // PHG (Power-Height-Gain-Directivity) ------------------------------------
    // Power (Watts), Gain (dB) and Directivity are already SI/unit-agnostic
    // APRS code values. Height is the one sub-field the APRS PHG spec only
    // defines in feet (10*2^n), so the <select> keeps feet as its underlying
    // value (what the PHG calculation below needs) but every visible label is
    // converted to and shown in meters instead.
    web_fieldset_open(req, TR_F_PHG_SECTION);
    web_select_open(req, TR_F_RADIO_TX_POWER, "myPHGPower");
    {
        // APRS PHG power code table (P digit 0-9), rounded to these fixed
        // Watt values only - not a free-edit field.
        static const int watts[] = { 0, 1, 5, 10, 15, 25, 35, 50, 65, 80 };
        for (size_t i = 0; i < sizeof(watts) / sizeof(watts[0]); i++) {
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%d", watts[i]);
            web_select_option(req, watts[i], lbl, g_config.my_phg_power == (uint16_t)watts[i]);
        }
    }
    web_select_close(req);
    web_select_open(req, TR_F_ANTENNA_GAIN, "myPHGGain");
    {
        // APRS PHG gain code table (G digit 0-9), in dB - not a free-edit field.
        for (int i = 0; i <= 9; i++) {
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%d", i);
            web_select_option(req, i, lbl, (int)lroundf(g_config.my_phg_gain) == i);
        }
    }
    web_select_close(req);
    web_select_open(req, TR_F_HEIGHT_M, "myPHGHeight");
    {
        // APRS PHG height code table (H digit), 10*2^n feet, extended beyond
        // the standard 0-9 digits to also allow the requested larger values.
        static const int feet[] = { 10, 20, 40, 80, 160, 320, 640, 1280, 2560, 5120, 10240, 20480, 40960, 81920 };
        for (size_t i = 0; i < sizeof(feet) / sizeof(feet[0]); i++) {
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%d", (int)lroundf(feet[i] * 0.3048f));
            // Option value stays in feet (the APRS code table's own unit);
            // only the label shown to the user is converted to meters.
            web_select_option(req, feet[i], lbl, g_config.my_phg_height == (uint16_t)feet[i]);
        }
    }
    web_select_close(req);
    web_select_open(req, TR_F_ANTENNA_DIRECTION, "myPHGDir");
    {
        static const char *dirs[] = { TR_DIR_OMNI, TR_DIR_N, TR_DIR_NE, TR_DIR_E, TR_DIR_SE, TR_DIR_S, TR_DIR_SW, TR_DIR_W, TR_DIR_NW };
        for (int i = 0; i < 9; i++)
            web_select_option(req, i, dirs[i], g_config.my_phg_dir == (uint8_t)i);
    }
    web_select_close(req);

    // Computed PHG value --------------------------------------------------
    // Read-only; automatically recalculated by JS whenever any PHG section
    // value changes (no manual "calculate" button). Kept inside the PHG
    // fieldset since it's derived directly from the fields above it.
    {
        char buf[550];
        snprintf(buf, sizeof(buf),
                 "<label>%s</label>"
                 "<input type='text' name='myPHG' id='myPHG' value='%s' maxlength='7' readonly>",
                 TR_F_PHG_TEXT, g_config.my_phg);
        web_raw(req, buf);
    }
    web_fieldset_close(req);

    web_raw(req, "<script>"
                 "function calcStationPHG(){"
                 "var p=parseInt(document.querySelector(\"select[name='myPHGPower']\").value)||0;"
                 "var g=parseInt(document.querySelector(\"select[name='myPHGGain']\").value)||0;"
                 "var h=parseInt(document.querySelector(\"select[name='myPHGHeight']\").value)||10;"
                 "var d=parseInt(document.querySelector(\"select[name='myPHGDir']\").value)||0;"
                 "var P=Math.min(9,Math.max(0,Math.round(Math.sqrt(p))));"
                 "var H=Math.min(13,Math.max(0,Math.round(Math.log(h/10)/Math.log(2))));"
                 "var G=Math.min(9,Math.max(0,g));"
                 "var D=Math.min(8,Math.max(0,d));"
                 // Per the APRS spec, each PHG digit is a single ASCII character.
                 // H=0-9 uses '0'-'9'; since DOS 8.0, H>=10 uses the following
                 // ASCII characters ':' ';' '<' '=' ... (ASCII(Hchar)-51=H) so
                 // the field stays exactly one character wide.
                 "var Hc=String.fromCharCode(48+H);"
                 "document.getElementById('myPHG').value='PHG'+P+Hc+G+D;"
                 "}"
                 "document.addEventListener('DOMContentLoaded',function(){"
                 "['myPHGPower','myPHGGain','myPHGHeight','myPHGDir'].forEach(function(n){"
                 "var el=document.querySelector(\"select[name='\"+n+\"']\");"
                 "if(el)el.addEventListener('change',calcStationPHG);"
                 "});"
                 "calcStationPHG();"
                 "});"
                 "</script>");

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_station_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char body[480];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    app_config_lock();
    web_form_get_call(body, "myCallsign", g_config.my_callsign, sizeof(g_config.my_callsign));
    g_config.my_lat = web_form_get_float(body, "myLAT", g_config.my_lat);
    g_config.my_lon = web_form_get_float(body, "myLON", g_config.my_lon);
    {
        int amb = web_form_get_int(body, "myAmbiguity", (int)g_config.pos_ambiguity);
        if (amb < 0)
            amb = 0;
        if (amb > POS_AMBIGUITY_MAX)
            amb = POS_AMBIGUITY_MAX;
        g_config.pos_ambiguity = (uint8_t)amb;
    }
    g_config.status_grid_en = web_form_get_bool(body, "myStatusGrid");
    web_form_get(body, "myPHG", g_config.my_phg, sizeof(g_config.my_phg));
    g_config.my_phg_power = (uint16_t)web_form_get_int(body, "myPHGPower", g_config.my_phg_power);
    g_config.my_phg_gain = (float)web_form_get_int(body, "myPHGGain", (int)lroundf(g_config.my_phg_gain));
    // Select value is the underlying feet code (see the GET handler); saved
    // as-is, only its displayed label is converted to meters.
    g_config.my_phg_height = (uint16_t)web_form_get_int(body, "myPHGHeight", g_config.my_phg_height);
    g_config.my_phg_dir = (uint8_t)web_form_get_int(body, "myPHGDir", g_config.my_phg_dir);

    // Every other page's "Use My Station Data" checkbox only re-snapshots
    // these values when that OTHER page itself is saved (its fields are
    // disabled client-side and never POST while the checkbox is on). Refresh
    // all of those stored snapshots now, in the same action, so they don't go
    // stale until the user happens to revisit and re-save each page.
    station_resync_dependents();

    app_config_unlock();

    app_config_save();
    web_send_saved_redirect(req, "/station");
    return ESP_OK;
}
