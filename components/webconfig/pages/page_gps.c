// @file page_gps.c
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
// @brief Web admin "GPS" page: the receiver's enable switch, a colour-coded
// module-status badge for diagnosing the serial link at a glance, and a live
// view of everything the NMEA GNSS receiver reports, refreshed once per
// second from the JSON endpoint below. Also serves the plain-numeric JSON
// endpoint every other page's "Use GPS" checkbox polls to auto-fill its own
// position/motion fields (see web_field_use_gps_data() in web_common.c).

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "gps.h"
#include "pages.h"
#include "str_append.h" // str_append(), str_append_truncated()
#include "translations.h"
#include "web_common.h"

static const char *TAG = "page_gps";

// Room for the whole live-value document: one JSON member per displayed row,
// the longest of which carry a translated phrase rather than a number, plus
// the "module"/"moduleClass" status-badge pair emitted ahead of them. The
// widest document any of the three languages can produce - every member
// present, every numeric field at its maximum width and the longest
// translation selected for each phrase - measures a little over 500 bytes, so
// this leaves room for rows and translations added later and makes the
// truncation branch below a guard rather than an expected path.
#define GPS_VALUES_BUF 1024

// Emits one label/value row of the live table. The value cell is left holding
// a placeholder and identified by "gps_<key>", which is the same key the JSON
// endpoint uses for that member - that pairing is the whole contract between
// this function and gps_values_json(), and it is what lets the page script
// fill the table without knowing what any individual row means.
static void gps_row(httpd_req_t *req, const char *label, const char *key) {
    char row[200];
    snprintf(row, sizeof(row), "<tr><td>%s</td><td id='gps_%s'>-</td></tr>", label, key);
    httpd_resp_sendstr_chunk(req, row);
}

// Emits the one row rendered as a coloured badge rather than plain text: the
// module-status summary at the top of the Receiver Status table. The span
// carries both the placeholder text and a starting "badge" class so the cell
// already looks like a badge before the first live refresh lands; the script
// below replaces the class along with the text on every subsequent update.
static void gps_row_badge(httpd_req_t *req, const char *label, const char *key) {
    char row[220];
    snprintf(row, sizeof(row), "<tr><td>%s</td><td><span id='gps_%s' class='badge'>-</span></td></tr>", label, key);
    httpd_resp_sendstr_chunk(req, row);
}

// Opens a fieldset holding one live table.
static void gps_table_open(httpd_req_t *req, const char *legend) {
    web_fieldset_open(req, legend);
    httpd_resp_sendstr_chunk(req, "<div class='table-wrap'><table>");
}

static void gps_table_close(httpd_req_t *req) {
    httpd_resp_sendstr_chunk(req, "</table></div>");
    web_fieldset_close(req);
}

// Translated name of a fix quality as reported in the GGA sentence.
static const char *quality_text(gps_fix_quality_t q) {
    switch (q) {
        case GPS_FIX_GPS:
            return TR_GPS_Q_GPS;
        case GPS_FIX_DGPS:
            return TR_GPS_Q_DGPS;
        case GPS_FIX_PPS:
            return TR_GPS_Q_PPS;
        case GPS_FIX_RTK:
            return TR_GPS_Q_RTK;
        case GPS_FIX_RTK_FLOAT:
            return TR_GPS_Q_RTK_FLOAT;
        case GPS_FIX_ESTIMATED:
            return TR_GPS_Q_ESTIMATED;
        case GPS_FIX_MANUAL:
            return TR_GPS_Q_MANUAL;
        case GPS_FIX_SIMULATED:
            return TR_GPS_Q_SIMULATED;
        case GPS_FIX_NONE:
        default:
            return TR_GPS_Q_NONE;
    }
}

// Translated name of a solution dimensionality as reported in the GSA sentence.
static const char *mode_text(gps_fix_mode_t m) {
    switch (m) {
        case GPS_MODE_2D:
            return TR_GPS_M_2D;
        case GPS_MODE_3D:
            return TR_GPS_M_3D;
        case GPS_MODE_NOFIX:
        default:
            return TR_GPS_M_NOFIX;
    }
}

// One-glance verdict on whether the receiver is usable, folding link and fix
// state into the single question an operator wiring up a module actually
// asks: is it plugged in the right way round and producing a position?
//
// "link" (sentences arriving on the ESP32's RX pin, wired to the module's TX
// output) and "fix" (a valid solution once sentences are flowing) are the two
// halves of that question, and they fail for different reasons: a dead link
// points at wiring, baud rate or a module with no power, while a live link
// with no fix points at antenna placement or cold-start time instead. Kept
// separate from quality_text()/mode_text() because those describe what the
// last GGA/GSA sentence said, while this describes whether the module is
// worth listening to at all right now.
typedef enum {
    GPS_STATUS_DISABLED,  // Switched off, or the UART could not be brought up.
    GPS_STATUS_NO_LINK,   // Enabled but no sentence has verified within the link timeout.
    GPS_STATUS_SEARCHING, // Sentences are arriving but no sentence reports a valid fix yet.
    GPS_STATUS_FIX_OK,    // Sentences are arriving and the last navigation solution is valid.
} gps_status_t;

static gps_status_t module_status(const gps_data_t *g, bool have) {
    if (!have)
        return GPS_STATUS_DISABLED;
    if (!g->link_up)
        return GPS_STATUS_NO_LINK;
    if (!g->valid)
        return GPS_STATUS_SEARCHING;
    return GPS_STATUS_FIX_OK;
}

// Translated label shown inside the status badge.
static const char *module_status_text(gps_status_t s) {
    switch (s) {
        case GPS_STATUS_DISABLED:
            return TR_GPS_STATUS_DISABLED;
        case GPS_STATUS_NO_LINK:
            return TR_GPS_STATUS_NO_LINK;
        case GPS_STATUS_SEARCHING:
            return TR_GPS_STATUS_SEARCHING;
        case GPS_STATUS_FIX_OK:
        default:
            return TR_GPS_STATUS_FIX_OK;
    }
}

// CSS badge class (see the .badge.ok/.warn/.err rules in web_common.c) that
// colours the status text above: red for a module that cannot be heard at
// all, amber for one that is heard but has not solved a position yet, green
// once it has.
static const char *module_status_class(gps_status_t s) {
    switch (s) {
        case GPS_STATUS_DISABLED:
        case GPS_STATUS_NO_LINK:
            return "err";
        case GPS_STATUS_SEARCHING:
            return "warn";
        case GPS_STATUS_FIX_OK:
        default:
            return "ok";
    }
}

// Builds the live-value document into out.
//
// Every member is either a quoted, ready-to-display string or the literal
// null, and a member is emitted as null exactly when the receiver has not
// reported that quantity - which the page renders as "-". Formatting happens
// here rather than in the browser so the units and the translated phrases stay
// with the rest of the firmware's strings. "module" and "moduleClass" are the
// exception to the null rule: they are the one-glance status verdict from
// module_status() and are always present, since there is always an answer to
// whether the module is currently usable even when every other field is
// absent.
//
// Nothing in the document comes from the network or from operator input: every
// value is either a number this firmware formatted or one of the translated
// literals above, all plain ASCII, so no JSON escaping is needed for any of it.
static void gps_values_json(const gps_data_t *g, bool have, char *out, size_t out_size) {
    size_t used = 0;

    str_append(out, out_size, &used, "{");

    gps_status_t status = module_status(g, have);
    str_append(out, out_size, &used, "\"module\":\"%s\",\"moduleClass\":\"%s\"", module_status_text(status), module_status_class(status));

    if (!have) {
        // The subsystem never came up (no UART). Reporting the link as down
        // and everything else as absent is the truthful rendering of that, and
        // it is what the page already knows how to display.
        str_append(out, out_size, &used, ",\"link\":\"%s\"}", TR_GPS_LINK_SILENT);
        return;
    }

    str_append(out, out_size, &used, ",\"link\":\"%s\"", g->link_up ? TR_GPS_LINK_RECEIVING : TR_GPS_LINK_SILENT);
    str_append(out, out_size, &used, ",\"nav\":\"%s\"", g->valid ? TR_GPS_NAV_ACTIVE : TR_GPS_NAV_WARNING);
    str_append(out, out_size, &used, ",\"quality\":\"%s\"", quality_text(g->quality));
    str_append(out, out_size, &used, ",\"mode\":\"%s\"", mode_text(g->mode));

    if (g->has_position) {
        str_append(out, out_size, &used, ",\"lat\":\"%.6f deg\"", g->latitude);
        str_append(out, out_size, &used, ",\"lon\":\"%.6f deg\"", g->longitude);
    } else {
        str_append(out, out_size, &used, ",\"lat\":null,\"lon\":null");
    }

    if (g->has_altitude)
        str_append(out, out_size, &used, ",\"alt\":\"%.1f m\"", g->altitude_m);
    else
        str_append(out, out_size, &used, ",\"alt\":null");

    if (g->has_geoid)
        str_append(out, out_size, &used, ",\"geoid\":\"%.1f m\"", g->geoid_m);
    else
        str_append(out, out_size, &used, ",\"geoid\":null");

    if (g->has_speed)
        str_append(out, out_size, &used, ",\"speed\":\"%.1f km/h\"", g->speed_kmh);
    else
        str_append(out, out_size, &used, ",\"speed\":null");

    if (g->has_course)
        str_append(out, out_size, &used, ",\"course\":\"%.1f deg\"", g->course_deg);
    else
        str_append(out, out_size, &used, ",\"course\":null");

    if (g->has_magvar)
        str_append(out, out_size, &used, ",\"magvar\":\"%.1f deg\"", g->magvar_deg);
    else
        str_append(out, out_size, &used, ",\"magvar\":null");

    if (g->has_date)
        str_append(out, out_size, &used, ",\"date\":\"%04u-%02u-%02u\"", (unsigned)g->year, (unsigned)g->month, (unsigned)g->day);
    else
        str_append(out, out_size, &used, ",\"date\":null");

    if (g->has_time)
        str_append(out, out_size, &used, ",\"time\":\"%02u:%02u:%02u Z\"", (unsigned)g->hour, (unsigned)g->minute, (unsigned)g->second);
    else
        str_append(out, out_size, &used, ",\"time\":null");

    if (g->has_sats_used)
        str_append(out, out_size, &used, ",\"satsUsed\":\"%u\"", (unsigned)g->sats_used);
    else
        str_append(out, out_size, &used, ",\"satsUsed\":null");

    if (g->has_sats_in_view)
        str_append(out, out_size, &used, ",\"satsView\":\"%u\"", (unsigned)g->sats_in_view);
    else
        str_append(out, out_size, &used, ",\"satsView\":null");

    if (g->has_hdop)
        str_append(out, out_size, &used, ",\"hdop\":\"%.2f\"", g->hdop);
    else
        str_append(out, out_size, &used, ",\"hdop\":null");

    if (g->has_pdop)
        str_append(out, out_size, &used, ",\"pdop\":\"%.2f\"", g->pdop);
    else
        str_append(out, out_size, &used, ",\"pdop\":null");

    if (g->has_vdop)
        str_append(out, out_size, &used, ",\"vdop\":\"%.2f\"", g->vdop);
    else
        str_append(out, out_size, &used, ",\"vdop\":null");

    str_append(out, out_size, &used, ",\"ok\":\"%lu\"", (unsigned long)g->sentences_ok);
    str_append(out, out_size, &used, ",\"bad\":\"%lu\"", (unsigned long)g->sentences_bad);

    // Both ages read as "never" until the event they measure has happened at
    // least once, which is the difference between a receiver that has produced
    // no fix yet and one whose fix went away a moment ago.
    if (g->has_link_age)
        str_append(out, out_size, &used, ",\"linkAge\":\"%lu s\"", (unsigned long)g->link_age_s);
    else
        str_append(out, out_size, &used, ",\"linkAge\":null");

    if (g->has_fix_age)
        str_append(out, out_size, &used, ",\"fixAge\":\"%lu s\"", (unsigned long)g->fix_age_s);
    else
        str_append(out, out_size, &used, ",\"fixAge\":null");

    str_append(out, out_size, &used, "}");

    if (str_append_truncated(used, out_size)) {
        // A cut-off document is not valid JSON, so the page's fetch would
        // reject it and every cell would silently freeze at its last value.
        // Answering with an empty object instead leaves the table showing the
        // placeholder, which is visible, and logs the real cause.
        ESP_LOGE(TAG, "live value document did not fit %u bytes", (unsigned)out_size);
        snprintf(out, out_size, "{}");
    }
}

esp_err_t page_gps_values_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    gps_data_t g;
    bool have = gps_snapshot(&g);

    char json[GPS_VALUES_BUF];
    gps_values_json(&g, have, json, sizeof(json));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

// Builds the "Use GPS" live-fill document into out: plain numbers (or the
// literal null) for exactly the quantities a page's position/motion fields
// can be auto-filled from. Kept separate from gps_values_json() above, which
// formats each value as a display string for the read-only status table -
// what every "Use GPS" checkbox needs instead is a bare number it can drop
// straight into an <input>. A member is emitted as null exactly when the
// receiver has not reported that quantity, the same rule gps_values_json()
// uses, so the checkbox's script sees the same "not available yet" signal
// the status page shows.
static void gps_live_json(const gps_data_t *g, bool have, char *out, size_t out_size) {
    size_t used = 0;

    str_append(out, out_size, &used, "{");

    if (!have) {
        str_append(out, out_size, &used, "\"lat\":null,\"lon\":null,\"alt\":null,\"speed\":null,\"course\":null}");
        return;
    }

    if (g->has_position)
        str_append(out, out_size, &used, "\"lat\":%.6f,\"lon\":%.6f", g->latitude, g->longitude);
    else
        str_append(out, out_size, &used, "\"lat\":null,\"lon\":null");

    if (g->has_altitude)
        str_append(out, out_size, &used, ",\"alt\":%.1f", g->altitude_m);
    else
        str_append(out, out_size, &used, ",\"alt\":null");

    if (g->has_speed)
        str_append(out, out_size, &used, ",\"speed\":%.1f", g->speed_kmh);
    else
        str_append(out, out_size, &used, ",\"speed\":null");

    if (g->has_course)
        str_append(out, out_size, &used, ",\"course\":%.1f", g->course_deg);
    else
        str_append(out, out_size, &used, ",\"course\":null");

    str_append(out, out_size, &used, "}");

    if (str_append_truncated(used, out_size)) {
        ESP_LOGE(TAG, "live-fill document did not fit %u bytes", (unsigned)out_size);
        snprintf(out, out_size, "{}");
    }
}

esp_err_t page_gps_live_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    gps_data_t g;
    bool have = gps_snapshot(&g);

    char json[192];
    gps_live_json(&g, have, json, sizeof(json));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

esp_err_t page_gps_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_GPS, "gps");

    // The switch is the only thing on this page that is saved, so it gets its
    // own small form. The live tables below sit outside it: they are readings,
    // not settings, and putting them inside would invite a Save that appears
    // to store them.
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/gps'>");
    web_fieldset_open(req, TR_GPS_FS_SERVICE);
    web_field_checkbox(req, TR_GPS_ENABLE, "gpsEn", g_config.gps_en);
    web_fieldset_close(req);
    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");

    gps_table_open(req, TR_GPS_FS_STATUS);
    gps_row_badge(req, TR_GPS_MODULE_STATUS, "module");
    gps_row(req, TR_GPS_LINK, "link");
    gps_row(req, TR_GPS_NAV_STATUS, "nav");
    gps_row(req, TR_GPS_FIX_QUALITY, "quality");
    gps_row(req, TR_GPS_FIX_MODE, "mode");
    gps_table_close(req);

    gps_table_open(req, TR_GPS_FS_POSITION);
    gps_row(req, TR_GPS_LATITUDE, "lat");
    gps_row(req, TR_GPS_LONGITUDE, "lon");
    gps_row(req, TR_GPS_ALTITUDE, "alt");
    gps_row(req, TR_GPS_GEOID, "geoid");
    gps_table_close(req);

    gps_table_open(req, TR_GPS_FS_MOTION);
    gps_row(req, TR_GPS_SPEED, "speed");
    gps_row(req, TR_GPS_COURSE, "course");
    gps_row(req, TR_GPS_MAGVAR, "magvar");
    gps_table_close(req);

    gps_table_open(req, TR_GPS_FS_TIME);
    gps_row(req, TR_GPS_UTC_DATE, "date");
    gps_row(req, TR_GPS_UTC_TIME, "time");
    gps_table_close(req);

    gps_table_open(req, TR_GPS_FS_SATELLITES);
    gps_row(req, TR_GPS_SATS_USED, "satsUsed");
    gps_row(req, TR_GPS_SATS_IN_VIEW, "satsView");
    gps_row(req, TR_GPS_HDOP, "hdop");
    gps_row(req, TR_GPS_PDOP, "pdop");
    gps_row(req, TR_GPS_VDOP, "vdop");
    gps_table_close(req);

    gps_table_open(req, TR_GPS_FS_LINK);
    gps_row(req, TR_GPS_SENTENCES_OK, "ok");
    gps_row(req, TR_GPS_SENTENCES_BAD, "bad");
    gps_row(req, TR_GPS_LINK_AGE, "linkAge");
    gps_row(req, TR_GPS_FIX_AGE, "fixAge");
    gps_table_close(req);

    // The port and pins are compile-time board wiring (gps.h), so they are
    // shown as text rather than polled: there is nothing here that can change
    // while the page is open, and nothing an operator can select.
    {
        char wiring[400];
        snprintf(wiring, sizeof(wiring),
                 "<div class='table-wrap'><table><tr><td>%s</td><td>UART%d</td></tr>"
                 "<tr><td>%s</td><td>GPIO%d</td></tr>"
                 "<tr><td>%s</td><td>GPIO%d</td></tr>"
                 "<tr><td>%s</td><td>%d 8N1</td></tr></table></div>",
                 TR_GPS_PORT, (int)GPS_UART_PORT, TR_GPS_RX_PIN, (int)GPS_UART_RX_GPIO, TR_GPS_TX_PIN, (int)GPS_UART_TX_GPIO, TR_GPS_BAUD, (int)GPS_UART_BAUD);
        web_fieldset_open(req, TR_GPS_FS_WIRING);
        httpd_resp_sendstr_chunk(req, wiring);
        web_fieldset_close(req);
    }

    // Live refresh: one fetch per second, writing each member of the returned
    // object into the cell whose id is "gps_" plus that member's key. Driving
    // the update from the response's own keys - rather than from a list of row
    // names repeated here - means a row added above needs no change in this
    // script, and a member the firmware omits leaves its cell showing the
    // placeholder instead of a stale reading. "moduleClass" is the one member
    // that is not itself a row: it carries the badge colour for "module"
    // instead of text of its own, so it is applied as a class on that cell
    // rather than written into any cell of its own.
    //
    // A member arriving as null is rendered as the placeholder for the same
    // reason: the receiver has not reported that quantity, and blanking the
    // cell would be indistinguishable from a page that had not loaded.
    //
    // Requests are not allowed to overlap: on a busy station a response can
    // take longer than the interval, and without the in-flight guard the
    // queued fetches would pile up against the four-connection limit the admin
    // server runs with. Polling stops when the page is hidden and resumes when
    // it is shown again, so a tab left open in the background does not keep
    // the station answering requests nobody is reading.
    httpd_resp_sendstr_chunk(req, "<script>"
                                  "var gpsBusy=false;"
                                  "function gpsRefresh(){"
                                  "if(gpsBusy||document.hidden)return;"
                                  "gpsBusy=true;"
                                  "fetch('/gps/values').then(function(r){return r.json();}).then(function(v){"
                                  "if(v.moduleClass){"
                                  "var badge=document.getElementById('gps_module');"
                                  "if(badge)badge.className='badge '+v.moduleClass;"
                                  "}"
                                  "for(var k in v){"
                                  "if(k==='moduleClass')continue;"
                                  "var td=document.getElementById('gps_'+k);"
                                  "if(td)td.textContent=(v[k]===null||v[k]===undefined)?'-':v[k];"
                                  "}"
                                  "}).catch(function(){}).then(function(){gpsBusy=false;});"
                                  "}"
                                  "gpsRefresh();"
                                  "var gpsTimer=setInterval(gpsRefresh,1000);"
                                  "document.addEventListener('visibilitychange',function(){if(!document.hidden)gpsRefresh();});"
                                  "window.addEventListener('beforeunload',function(){clearInterval(gpsTimer);});"
                                  "</script>");

    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_gps_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    char body[256];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    app_config_lock();
    g_config.gps_en = web_form_get_bool(body, "gpsEn");
    app_config_unlock();

    // Applied before the save so the switch takes effect even if the write to
    // flash fails: the operator is told about the failed write separately, and
    // a receiver that obeys the screen is less confusing than one that waits
    // for a reboot that may never confirm anything.
    gps_apply_config();

    bool ok = app_config_save();
    if (!ok)
        ESP_LOGE(TAG, "GPS settings could not be written to flash");
    web_send_save_result(req, ok, "/gps");
    return ESP_OK;
}
