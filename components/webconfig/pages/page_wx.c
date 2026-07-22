/**
 * @file page_wx.c
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
 * @brief Web admin "Weather" page: renders and saves the weather station slot
 * configuration (wind, temperature, rain, humidity, pressure and the remaining
 * measurement slots) in g_config.
 */

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "pages.h"
#include "sensors_local.h"
#include "translations.h"
#include "web_common.h"

/*
 * Human-readable label for every mappable APRS Weather Report field, indexed
 * by ::wx_field_id_t (declared in app_config.h). This list is exactly the set
 * of quantities an APRS WX report can carry on-air - one row per field, in the
 * order weather.c emits them. The old placeholder/extended-sensor rows (UV,
 * soil, water, battery, "Slot 18..26") are gone: they have no weather-report
 * token and belong on the Telemetry page instead.
 */
static const char *WX_FIELD_NAME[WX_SENSOR_NUM] = {
    [WX_FIELD_WIND_DIRECTION] = TR_WX_WIND_DIRECTION,
    [WX_FIELD_WIND_SPEED] = TR_WX_WIND_SPEED,
    [WX_FIELD_WIND_GUST] = TR_WX_WIND_GUST,
    [WX_FIELD_TEMPERATURE] = TR_WX_TEMPERATURE,
    [WX_FIELD_RAIN_1H] = TR_WX_RAIN_1H,
    [WX_FIELD_RAIN_24H] = TR_WX_RAIN_24H,
    [WX_FIELD_RAIN_MIDNIGHT] = TR_WX_RAIN_MIDNIGHT,
    [WX_FIELD_SNOW_24H] = TR_WX_SNOW,
    [WX_FIELD_HUMIDITY] = TR_WX_HUMIDITY,
    [WX_FIELD_PRESSURE] = TR_WX_PRESSURE,
    [WX_FIELD_LUMINOSITY] = TR_WX_LUMINOSITY,
    [WX_FIELD_FLOOD_HEIGHT_FT] = TR_WX_FLOOD_FT,
    [WX_FIELD_FLOOD_HEIGHT_M] = TR_WX_FLOOD_M,
};

/*
 * Bit position in ::sensor_local_wx_mask_t for each ::wx_field_id_t row of
 * this page, so a driver's ::sensor_local_properties_t::wx mask can be
 * tested directly against the field currently being rendered. Kept as an
 * explicit table (rather than relying on the enum values lining up) so the
 * mapping is correct even if either enum's declaration order ever changes.
 */
static const sensor_local_wx_mask_t WX_FIELD_PROPERTY_BIT[WX_SENSOR_NUM] = {
    [WX_FIELD_WIND_DIRECTION] = SENSOR_LOCAL_WX_WIND_DIRECTION,
    [WX_FIELD_WIND_SPEED] = SENSOR_LOCAL_WX_WIND_SPEED,
    [WX_FIELD_WIND_GUST] = SENSOR_LOCAL_WX_WIND_GUST,
    [WX_FIELD_TEMPERATURE] = SENSOR_LOCAL_WX_TEMPERATURE,
    [WX_FIELD_RAIN_1H] = SENSOR_LOCAL_WX_RAIN_1H,
    [WX_FIELD_RAIN_24H] = SENSOR_LOCAL_WX_RAIN_24H,
    [WX_FIELD_RAIN_MIDNIGHT] = SENSOR_LOCAL_WX_RAIN_MIDNIGHT,
    [WX_FIELD_SNOW_24H] = SENSOR_LOCAL_WX_SNOW,
    [WX_FIELD_HUMIDITY] = SENSOR_LOCAL_WX_HUMIDITY,
    [WX_FIELD_PRESSURE] = SENSOR_LOCAL_WX_PRESSURE,
    [WX_FIELD_LUMINOSITY] = SENSOR_LOCAL_WX_LUMINOSITY,
    [WX_FIELD_FLOOD_HEIGHT_FT] = SENSOR_LOCAL_WX_FLOOD_HEIGHT_FT,
    [WX_FIELD_FLOOD_HEIGHT_M] = SENSOR_LOCAL_WX_FLOOD_HEIGHT_M,
};

/*
 * Emits the <select> for one field's "source channel", populated from the live
 * sensors_local registry so each option shows the channel *number and name*
 * ("0: bme280", "1: ds18b20", ...). Index 0xFF (255) is the "(none)" choice.
 * If no local sensor driver has registered yet, only "(none)" is offered.
 *
 * A driver is only listed as a choice for a given row/field if BOTH:
 *   1) it advertises ::SENSOR_LOCAL_DATA_WEATHER in ::sensor_local_driver_t::capabilities
 *      (coarse family check - excludes telemetry-only drivers), AND
 *   2) its ::sensor_local_driver_t::properties (see sensor_local_properties.h)
 *      sets the bit matching THIS row's field (fine-grained check - e.g. a
 *      Temperature+Pressure-only sensor such as bmp180 is offered on the
 *      Temperature and Pressure rows, but not on Wind/Rain/Humidity/etc).
 * A driver with a NULL @c properties pointer (not yet migrated to publish a
 * descriptor) is never offered on any row, since its per-field fitness is
 * unknown.
 * The option value is still the sensor's real registry index (not a
 * sequential position among the filtered list), so it round-trips correctly
 * with wx_sensor_ch[].
 */
static void wx_channel_select(httpd_req_t *req, int field, uint8_t selected) {
    char buf[192];
    // id='wxCh%d' (matching the name) lets the live-value poller below read
    // the row's *currently selected* channel straight from the DOM, so the
    // "Value" column previews whatever channel is picked even before Save is
    // pressed - not just whatever was last saved to flash. onchange triggers
    // an immediate refresh instead of waiting for the next 2s tick.
    snprintf(buf, sizeof(buf), "<select name='wxCh%d' id='wxCh%d' style='width:150px' onchange='wxRefreshValues()'>", field, field);
    httpd_resp_sendstr_chunk(req, buf);

    snprintf(buf, sizeof(buf), "<option value='255'%s>%s</option>", (selected == 0xFF) ? " selected" : "", TR_WX_CHANNEL_NONE);
    httpd_resp_sendstr_chunk(req, buf);

    sensor_local_wx_mask_t field_bit = ((unsigned)field < (unsigned)WX_SENSOR_NUM) ? WX_FIELD_PROPERTY_BIT[field] : SENSOR_LOCAL_WX_NONE;

    size_t n = sensors_local_count();
    for (size_t ch = 0; ch < n; ch++) {
        sensor_local_driver_t *d = sensors_local_get(ch);
        if (d == NULL || !(d->capabilities & SENSOR_LOCAL_DATA_WEATHER))
            continue; /* not a weather sensor: skip (e.g. telemetry-only drivers) */
        if (!sensor_local_properties_has_wx(d->properties, field_bit))
            continue; /* weather sensor, but doesn't produce THIS field (e.g. bmp180 on the Wind row) */
        /* Compose "<sensor name> <sensor channel name>" from the driver's
         * properties (e.g. "BMP180 Temperature"); falls back to just the
         * sensor name (or "?") if the driver hasn't published a dedicated
         * channel label for this field. */
        char nm[80];
        sensor_local_properties_wx_label(d->properties, field_bit, nm, sizeof(nm));
        snprintf(buf, sizeof(buf), "<option value='%u'%s>%u: %.40s</option>", (unsigned)ch, (selected == ch) ? " selected" : "", (unsigned)ch, nm);
        httpd_resp_sendstr_chunk(req, buf);
    }
    httpd_resp_sendstr_chunk(req, "</select>");
}

/*
 * Field present/format helpers, mirroring weather.c's wx_field_present()/
 * wx_field_value() (kept private to that file). Duplicated here rather than
 * shared because this is the only other place that needs to pull a single
 * engineering-unit value out of an ::aprs_weather_report_t; wx_field_format()
 * additionally renders it as a ready-to-display, unit-suffixed JSON string
 * literal (quotes included) for the live-preview endpoint below.
 */
static bool wx_field_present(const aprs_weather_report_t *wx, wx_field_id_t f) {
    switch (f) {
        case WX_FIELD_WIND_DIRECTION:
        case WX_FIELD_WIND_SPEED:
            return wx->enabled[APRS_WX_SENSOR_WIND] && !wx->wind.direction_unknown;
        case WX_FIELD_WIND_GUST:
            return wx->enabled[APRS_WX_SENSOR_WIND] && wx->wind.has_gust;
        case WX_FIELD_TEMPERATURE:
            return wx->enabled[APRS_WX_SENSOR_TEMPERATURE];
        case WX_FIELD_RAIN_1H:
            return wx->enabled[APRS_WX_SENSOR_RAIN_LAST_HOUR];
        case WX_FIELD_RAIN_24H:
            return wx->enabled[APRS_WX_SENSOR_RAIN_LAST_24H];
        case WX_FIELD_RAIN_MIDNIGHT:
            return wx->enabled[APRS_WX_SENSOR_RAIN_SINCE_MIDNIGHT];
        case WX_FIELD_SNOW_24H:
            return wx->enabled[APRS_WX_SENSOR_SNOW_LAST_24H];
        case WX_FIELD_HUMIDITY:
            return wx->enabled[APRS_WX_SENSOR_HUMIDITY];
        case WX_FIELD_PRESSURE:
            return wx->enabled[APRS_WX_SENSOR_BAROMETRIC_PRESSURE];
        case WX_FIELD_LUMINOSITY:
            return wx->enabled[APRS_WX_SENSOR_LUMINOSITY];
        case WX_FIELD_FLOOD_HEIGHT_FT:
            return wx->enabled[APRS_WX_SENSOR_FLOOD_HEIGHT_FT];
        case WX_FIELD_FLOOD_HEIGHT_M:
            return wx->enabled[APRS_WX_SENSOR_FLOOD_HEIGHT_M];
        default:
            return false;
    }
}

// Renders "<field's engineering value> <unit>" as a complete, already-quoted
// JSON string literal (e.g. "\"-5 C\""), plain ASCII only so it never needs
// JSON escaping. out must be at least 40 bytes.
//
// The underlying ::aprs_weather_report_t always stores values in the units
// the APRS Weather Report format itself uses on-air (mph, deg F, inches,
// feet - see weather_telemetry.h), since that's what ax25/weather.c needs to
// build the packet. This "Value" preview column is admin-facing UI only, so
// it converts every field to International System (SI) units before display
// - km/h, deg C, mm, hPa, meters - and NEVER shows the raw Imperial number.
static void wx_field_format(const aprs_weather_report_t *wx, wx_field_id_t f, char *out, size_t outsz) {
    switch (f) {
        case WX_FIELD_WIND_DIRECTION:
            snprintf(out, outsz, "\"%u deg\"", (unsigned)wx->wind.direction_deg);
            break;
        case WX_FIELD_WIND_SPEED:
            /* mph -> km/h */
            snprintf(out, outsz, "\"%.1f km/h\"", (double)wx->wind.sustained_mph * 1.609344);
            break;
        case WX_FIELD_WIND_GUST:
            /* mph -> km/h */
            snprintf(out, outsz, "\"%.1f km/h\"", (double)wx->wind.gust_mph * 1.609344);
            break;
        case WX_FIELD_TEMPERATURE:
            /* deg F -> deg C */
            snprintf(out, outsz, "\"%.1f C\"", ((double)wx->temperature_f - 32.0) * 5.0 / 9.0);
            break;
        case WX_FIELD_RAIN_1H:
            /* 1/100 in -> mm */
            snprintf(out, outsz, "\"%.1f mm\"", (wx->rain_last_hour_hundredths_in / 100.0) * 25.4);
            break;
        case WX_FIELD_RAIN_24H:
            /* 1/100 in -> mm */
            snprintf(out, outsz, "\"%.1f mm\"", (wx->rain_last_24h_hundredths_in / 100.0) * 25.4);
            break;
        case WX_FIELD_RAIN_MIDNIGHT:
            /* 1/100 in -> mm */
            snprintf(out, outsz, "\"%.1f mm\"", (wx->rain_since_midnight_hundredths_in / 100.0) * 25.4);
            break;
        case WX_FIELD_SNOW_24H:
            /* 1/10 in -> mm */
            snprintf(out, outsz, "\"%.1f mm\"", (wx->snow_last_24h_tenths_in / 10.0) * 25.4);
            break;
        case WX_FIELD_HUMIDITY:
            snprintf(out, outsz, "\"%u %%\"", (unsigned)wx->humidity_percent);
            break;
        case WX_FIELD_PRESSURE:
            /* Already SI (tenths of mb == tenths of hPa; 1 mb == 1 hPa) */
            snprintf(out, outsz, "\"%.1f hPa\"", wx->barometric_pressure_tenths_mb / 10.0);
            break;
        case WX_FIELD_LUMINOSITY:
            /* Already SI */
            snprintf(out, outsz, "\"%u W/m2\"", (unsigned)wx->luminosity_wm2);
            break;
        case WX_FIELD_FLOOD_HEIGHT_FT:
            /* Field is fed by the "feet" APRS token, but display in meters:
             * use the report's parallel SI field rather than converting. */
            snprintf(out, outsz, "\"%.1f m\"", (double)wx->flood_height_m);
            break;
        case WX_FIELD_FLOOD_HEIGHT_M:
            snprintf(out, outsz, "\"%.1f m\"", (double)wx->flood_height_m);
            break;
        default:
            snprintf(out, outsz, "null");
            break;
    }
}

// GET /wx/values?ch0=<channel>&ch1=<channel>&... - one "chN" query parameter
// per WX_SENSOR_NUM row, giving the channel currently selected in that row's
// <select> (255/absent = "(none)"). Reads that ONE channel's driver fresh
// (sensors_local_save_one(), no averaging, no dependency on Save having been
// pressed) and returns a JSON array of the same length, each slot either a
// quoted "value unit" string or `null` if the row has no channel selected, the
// channel doesn't exist / isn't a weather sensor, or it has no reading for
// that particular field this cycle. Polled every 2s by the page's script.
esp_err_t page_wx_values_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    char query[512];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
        query[0] = 0;

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "[");
    for (int i = 0; i < WX_SENSOR_NUM; i++) {
        if (i > 0)
            httpd_resp_sendstr_chunk(req, ",");

        char key[16];
        snprintf(key, sizeof(key), "ch%d", i);
        int ch = web_form_get_int(query, key, 255);

        char out[40] = "null";
        if (ch >= 0 && ch < 255) {
            aprs_weather_report_t wx = { 0 };
            weather_telemetry_data_t scratch = { 0 };
            scratch.weather = &wx;
            scratch.weather_qty = 1;

            if (sensors_local_save_one((size_t)ch, &scratch, SENSOR_LOCAL_DATA_WEATHER) == ESP_OK && wx_field_present(&wx, (wx_field_id_t)i))
                wx_field_format(&wx, (wx_field_id_t)i, out, sizeof(out));
        }
        httpd_resp_sendstr_chunk(req, out);
    }
    httpd_resp_sendstr_chunk(req, "]");
    // Required to close the chunked response (see the wifi-scan JSON
    // endpoint in page_wireless.c for the same pattern). Without this final
    // zero-length chunk the HTTP body is never marked complete, so the
    // page's fetch('/wx/values') never resolves and the Value column can
    // never be populated, no matter which channel is selected.
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

esp_err_t page_wx_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_WEATHER, "wx");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/wx'>");

    web_fieldset_open(req, TR_F_WEATHER_STATION);
    web_field_checkbox(req, TR_F_ENABLE_WX, "wxEn", g_config.wx_en);
    web_field_use_station_data(req, "wxUseStation", g_config.wx_use_station, "wxMycall", "wxLAT", "wxLON", "wxALT");
    web_field_checkbox(req, TR_F_SEND_VIA_RF, "wxTx2rf", g_config.wx_2rf);
    web_field_checkbox(req, TR_F_SEND_VIA_INTERNET, "wxTx2inet", g_config.wx_2inet);
    web_field_checkbox(req, TR_F_ADD_TIMESTAMP, "wxTime", g_config.wx_timestamp);
    web_field_text(req, TR_F_MY_CALLSIGN, "wxMycall", g_config.wx_mycall, 9);
    web_field_int(req, TR_F_SSID, "wxSSID", g_config.wx_ssid);
    web_field_int(req, TR_F_PATH_BITMASK, "wxPath", g_config.wx_path);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_POSITION);
    web_field_checkbox(req, TR_F_USE_GPS, "wxGPS", g_config.wx_gps);
    web_field_float(req, TR_F_LATITUDE, "wxLAT", g_config.wx_lat, "0.0001");
    web_field_float(req, TR_F_LONGITUDE, "wxLON", g_config.wx_lon, "0.0001");
    web_field_float(req, TR_F_ALTITUDE_M, "wxALT", g_config.wx_alt, "1");
    web_field_int(req, TR_F_BEACON_INTERVAL_S, "wxInv", g_config.wx_interval);
    web_field_text(req, TR_F_OBJECT_NAME, "wxObject", g_config.wx_object, 9);
    web_field_text(req, TR_F_COMMENT, "wxComment", g_config.wx_comment, COMMENT_SIZE - 1);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_SENSOR_MAPPING_ENABLE_AVERAGED_SOURCE_CHANNEL);
    httpd_resp_sendstr_chunk(req,
                              "<table><tr><th>" TR_WX_FIELD "</th><th>" TR_F_ENABLE "</th><th>" TR_TLM_AVG "</th><th>" TR_WX_CHANNEL "</th><th>" TR_WX_VALUE
                              "</th></tr>");
    for (int i = 0; i < WX_SENSOR_NUM; i++) {
        if (i == WX_FIELD_FLOOD_HEIGHT_FT)
            continue; /* Value column is SI-only (meters); the feet slot stays configurable
                       * via WX_FIELD_FLOOD_HEIGHT_M's row/save handling but isn't shown here. */
        char row[300];
        snprintf(row, sizeof(row),
                 "<tr><td>%s</td>"
                 "<td><input type='checkbox' name='wxEn%d' %s></td>"
                 "<td><input type='checkbox' name='wxAvg%d' %s></td>"
                 "<td>",
                 WX_FIELD_NAME[i], i, g_config.wx_sensor_enable[i] ? "checked" : "", i, g_config.wx_sensor_avg[i] ? "checked" : "");
        httpd_resp_sendstr_chunk(req, row);
        wx_channel_select(req, i, g_config.wx_sensor_ch[i]);
        char val_td[40];
        snprintf(val_td, sizeof(val_td), "</td><td id='wxVal%d'>-</td></tr>", i);
        httpd_resp_sendstr_chunk(req, val_td);
    }
    httpd_resp_sendstr_chunk(req, "</table>");
    web_fieldset_close(req);

    // Live "Value" column: every 2s (and immediately on any row's channel
    // change via wx_channel_select()'s onchange), read whatever channel is
    // *currently selected* in each row straight from the DOM and ask the
    // firmware for that one channel's live reading of that row's field via
    // GET /wx/values. Does not require Save to have been pressed first, and
    // stops cleanly if the user navigates away (clearInterval on unload).
    //
    // wxReqSeq/wxLastApplied guard against a request race: onchange fires an
    // immediate refresh *in addition to* the running 2s poll, so two fetches
    // can be in flight at once. Without sequencing, an older poll response
    // (still carrying the channel selections from *before* the user's edit)
    // can resolve after the onchange-triggered response and clobber every
    // Value cell back to the previous sensor's reading - i.e. picking a new
    // sensor "doesn't actualize" the column until the next lucky tick. Each
    // call captures its own sequence number and only the highest-numbered
    // response seen so far is ever allowed to write to the DOM.
    {
        char script[1100];
        snprintf(script, sizeof(script),
                 "<script>"
                 "var WX_N=%d;"
                 "var wxReqSeq=0;"
                 "var wxLastApplied=0;"
                 "function wxRefreshValues(){"
                 "var seq=++wxReqSeq;"
                 "var q=[];"
                 "for(var i=0;i<WX_N;i++){"
                 "var sel=document.getElementById('wxCh'+i);"
                 "q.push('ch'+i+'='+(sel?sel.value:255));"
                 "}"
                 "fetch('/wx/values?'+q.join('&')).then(function(r){return r.json();}).then(function(vals){"
                 "if(seq<wxLastApplied)return;"
                 "wxLastApplied=seq;"
                 "for(var i=0;i<WX_N;i++){"
                 "var td=document.getElementById('wxVal'+i);"
                 "if(td)td.textContent=(vals[i]===null||vals[i]===undefined)?'-':vals[i];"
                 "}"
                 "}).catch(function(){});"
                 "}"
                 "wxRefreshValues();"
                 "var wxValTimer=setInterval(wxRefreshValues,2000);"
                 "window.addEventListener('beforeunload',function(){clearInterval(wxValTimer);});"
                 "</script>",
                 (int)WX_SENSOR_NUM);
        httpd_resp_sendstr_chunk(req, script);
    }

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_wx_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char body[2600];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    app_config_lock();
    g_config.wx_en = web_form_get_bool(body, "wxEn");
    g_config.wx_use_station = web_form_get_bool(body, "wxUseStation");
    g_config.wx_2rf = web_form_get_bool(body, "wxTx2rf");
    g_config.wx_2inet = web_form_get_bool(body, "wxTx2inet");
    g_config.wx_timestamp = web_form_get_bool(body, "wxTime");
    if (g_config.wx_use_station) {
        strncpy(g_config.wx_mycall, g_config.my_callsign, sizeof(g_config.wx_mycall) - 1);
        g_config.wx_mycall[sizeof(g_config.wx_mycall) - 1] = 0;
    } else {
        web_form_get_call(body, "wxMycall", g_config.wx_mycall, sizeof(g_config.wx_mycall));
    }
    g_config.wx_ssid = web_form_get_ssid(body, "wxSSID", g_config.wx_ssid);
    g_config.wx_path = (uint8_t)web_form_get_int(body, "wxPath", g_config.wx_path);

    g_config.wx_gps = web_form_get_bool(body, "wxGPS");
    if (g_config.wx_use_station) {
        g_config.wx_lat = g_config.my_lat;
        g_config.wx_lon = g_config.my_lon;
        g_config.wx_alt = g_config.my_alt;
    } else {
        g_config.wx_lat = web_form_get_float(body, "wxLAT", g_config.wx_lat);
        g_config.wx_lon = web_form_get_float(body, "wxLON", g_config.wx_lon);
        g_config.wx_alt = web_form_get_float(body, "wxALT", g_config.wx_alt);
    }
    g_config.wx_interval = (uint16_t)web_form_get_int(body, "wxInv", g_config.wx_interval);
    web_form_get(body, "wxObject", g_config.wx_object, sizeof(g_config.wx_object));
    web_form_get(body, "wxComment", g_config.wx_comment, sizeof(g_config.wx_comment));

    for (int i = 0; i < WX_SENSOR_NUM; i++) {
        if (i == WX_FIELD_FLOOD_HEIGHT_FT)
            continue; /* not rendered on this page (Value column is SI/meters-only);
                       * leave its saved config untouched rather than force-disabling it. */
        char key[16];
        snprintf(key, sizeof(key), "wxEn%d", i);
        g_config.wx_sensor_enable[i] = web_form_get_bool(body, key);
        snprintf(key, sizeof(key), "wxAvg%d", i);
        g_config.wx_sensor_avg[i] = web_form_get_bool(body, key);
        snprintf(key, sizeof(key), "wxCh%d", i);
        g_config.wx_sensor_ch[i] = (uint8_t)web_form_get_int(body, key, g_config.wx_sensor_ch[i]);
    }

    app_config_unlock();

    app_config_save();
    web_send_saved_redirect(req, "/wx");
    return ESP_OK;
}
