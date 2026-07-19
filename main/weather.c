/**
 * @file weather.c
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
 * @brief Own-station APRS Weather Report subsystem: owns the shared
 * ::weather_telemetry_data container, refreshes it from the sensors_local
 * registry once per second (with optional per-field averaging), and encodes
 * and transmits a standard APRS Weather Report at g_config.wx_interval.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_config.h"
#include "aprs_service.h"
#include "igate.h"
#include "sensors_local.h"
#include "weather.h"
#include "weather_telemetry.h"

static const char *TAG = "weather";

// Same software-identifier destination call used by beacon.c / the message
// component, for consistency across the firmware.
#define WX_DEST "APE32L"

#define WX_MIN_INTERVAL_S     30   // sanity floor for wx_interval
#define WX_DEFAULT_INTERVAL_S 600  // used when wx_interval == 0
#define WX_REFRESH_PERIOD_MS  1000 // sensors_local sampling cadence (1 Hz)

/* -------------------------------------------------------------------------
 * The one shared container and its backing storage.
 *
 * weather[0] receives the decoded weather fields; telemetry_report[0] is kept
 * allocated so telemetry-capable local drivers can also write without special
 * casing, and so a future telemetry sender can read the same snapshot.
 * ------------------------------------------------------------------------- */
weather_telemetry_data_t weather_telemetry_data;

static aprs_weather_report_t s_wx;   // backs weather_telemetry_data.weather[0]
static aprs_telemetry_report_t s_tlm; // backs weather_telemetry_data.telemetry_report[0]
static bool s_tlm_analog_en[APRS_TELEMETRY_ANALOG_CHANNELS];
static double s_tlm_analog[APRS_TELEMETRY_ANALOG_CHANNELS];
static bool s_tlm_digital_en[APRS_TELEMETRY_DIGITAL_CHANNELS];
static bool s_tlm_digital[APRS_TELEMETRY_DIGITAL_CHANNELS];

static SemaphoreHandle_t s_lock;

// Per-field averaging accumulators, reset after every beacon. Only used for
// fields whose "Averaged" box is ticked on the Weather page.
static double s_avg_sum[WX_SENSOR_NUM];
static uint32_t s_avg_cnt[WX_SENSOR_NUM];

void weather_lock(void) {
    if (s_lock)
        xSemaphoreTake(s_lock, portMAX_DELAY);
}
void weather_unlock(void) {
    if (s_lock)
        xSemaphoreGive(s_lock);
}

/* -------------------------------------------------------------------------
 * Field accessors: map one wx_field_id_t to (is it present in this report?)
 * and (its engineering-unit value). Wind is the one enum slot that carries
 * three physical values, so it is spread over three rows here.
 * ------------------------------------------------------------------------- */
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

static double wx_field_value(const aprs_weather_report_t *wx, wx_field_id_t f) {
    switch (f) {
        case WX_FIELD_WIND_DIRECTION:
            return (double)wx->wind.direction_deg;
        case WX_FIELD_WIND_SPEED:
            return (double)wx->wind.sustained_mph;
        case WX_FIELD_WIND_GUST:
            return (double)wx->wind.gust_mph;
        case WX_FIELD_TEMPERATURE:
            return (double)wx->temperature_f;
        case WX_FIELD_RAIN_1H:
            return (double)wx->rain_last_hour_hundredths_in;
        case WX_FIELD_RAIN_24H:
            return (double)wx->rain_last_24h_hundredths_in;
        case WX_FIELD_RAIN_MIDNIGHT:
            return (double)wx->rain_since_midnight_hundredths_in;
        case WX_FIELD_SNOW_24H:
            return (double)wx->snow_last_24h_tenths_in;
        case WX_FIELD_HUMIDITY:
            return (double)wx->humidity_percent;
        case WX_FIELD_PRESSURE:
            return (double)wx->barometric_pressure_tenths_mb;
        case WX_FIELD_LUMINOSITY:
            return (double)wx->luminosity_wm2;
        case WX_FIELD_FLOOD_HEIGHT_FT:
            return (double)wx->flood_height_ft;
        case WX_FIELD_FLOOD_HEIGHT_M:
            return (double)wx->flood_height_m;
        default:
            return 0.0;
    }
}

/* -------------------------------------------------------------------------
 * 1 Hz refresh: take a clean snapshot from every capable local driver and,
 * for any field with "Averaged" ticked, fold this sample into its accumulator.
 * ------------------------------------------------------------------------- */
static void weather_refresh_now(void) {
    weather_lock();

    // Clear the enabled flags so only sensors that report *this* cycle count;
    // values are overwritten by the drivers that do report.
    memset(s_wx.enabled, 0, sizeof(s_wx.enabled));
    memset(s_tlm_analog_en, 0, sizeof(s_tlm_analog_en));
    memset(s_tlm_digital_en, 0, sizeof(s_tlm_digital_en));

    // Let every capable, healthy driver write straight into the container.
    sensors_local_save(&weather_telemetry_data, SENSOR_LOCAL_DATA_ALL);

    // Fold this fresh sample into the running averages where requested.
    for (int f = 0; f < WX_SENSOR_NUM; f++) {
        if (!g_config.wx_sensor_avg[f])
            continue;
        if (!wx_field_present(&s_wx, (wx_field_id_t)f))
            continue;
        s_avg_sum[f] += wx_field_value(&s_wx, (wx_field_id_t)f);
        s_avg_cnt[f]++;
    }

    weather_unlock();
}

/* -------------------------------------------------------------------------
 * Encoding helpers
 * ------------------------------------------------------------------------- */

// wx_path is a bitmask over g_config.path[0..3]; mirror beacon.c's scheme.
static void build_path_suffix(uint8_t bitmask, char *out, size_t outMax) {
    out[0] = 0;
    if (bitmask == 0 || outMax == 0)
        return;
    size_t used = 0;
    for (int bit = 0; bit < 4; bit++) {
        if (!(bitmask & (1 << bit)) || !g_config.path[bit][0])
            continue;
        int n = snprintf(out + used, outMax - used, ",%s", g_config.path[bit]);
        if (n < 0)
            break;
        if ((size_t)n >= outMax - used) {
            used = outMax - 1;
            break;
        }
        used += (size_t)n;
    }
}

static void lat_lon_to_aprs(float lat, float lon, char *latOut, size_t latMax, char *lonOut, size_t lonMax) {
    float alat = fabsf(lat);
    int dLat = (int)alat;
    snprintf(latOut, latMax, "%02d%05.2f%c", dLat, (alat - dLat) * 60.0f, lat >= 0 ? 'N' : 'S');
    float alon = fabsf(lon);
    int dLon = (int)alon;
    snprintf(lonOut, lonMax, "%03d%05.2f%c", dLon, (alon - dLon) * 60.0f, lon >= 0 ? 'E' : 'W');
}

// Resolved (post-averaging) view of one field for the encoder.
typedef struct {
    bool present;
    double value;
} wx_resolved_t;

// Snapshot + resolve all fields under the lock: apply the operator's enable
// mask, use the averaged value where averaging is on (and had samples), and
// reset the accumulators for the next interval. Also copies the position and
// timestamp bits the encoder needs, so the lock is released quickly.
static void resolve_fields(wx_resolved_t out[WX_SENSOR_NUM]) {
    weather_lock();
    for (int f = 0; f < WX_SENSOR_NUM; f++) {
        bool en = g_config.wx_sensor_enable[f] && wx_field_present(&s_wx, (wx_field_id_t)f);
        double v = wx_field_value(&s_wx, (wx_field_id_t)f);
        if (en && g_config.wx_sensor_avg[f] && s_avg_cnt[f] > 0)
            v = s_avg_sum[f] / (double)s_avg_cnt[f];
        out[f].present = en;
        out[f].value = v;
        s_avg_sum[f] = 0.0;
        s_avg_cnt[f] = 0;
    }
    weather_unlock();
}

// Appends the weather data tokens (wind + gust + temp always, the rest only
// when present) to `out`. `positionless` selects the "cddd sSSS" wind prefix
// used by positionless reports instead of the "ddd/sss" used by positioned
// reports. Returns bytes written.
static int build_wx_tokens(const wx_resolved_t r[WX_SENSOR_NUM], bool positionless, char *out, size_t outMax) {
    size_t u = 0;
#define WX_APP(...)                                                                                                                                            \
    do {                                                                                                                                                       \
        int _n = snprintf(out + u, outMax - u, __VA_ARGS__);                                                                                                   \
        if (_n < 0)                                                                                                                                            \
            return (int)u;                                                                                                                                     \
        if ((size_t)_n >= outMax - u) {                                                                                                                        \
            return (int)(outMax - 1);                                                                                                                          \
        }                                                                                                                                                      \
        u += (size_t)_n;                                                                                                                                       \
    } while (0)

    // Wind direction + sustained speed. Always emitted (placeholders if absent)
    // so the report stays a spec-valid WX frame.
    if (r[WX_FIELD_WIND_DIRECTION].present && r[WX_FIELD_WIND_SPEED].present) {
        int dir = (int)lround(r[WX_FIELD_WIND_DIRECTION].value);
        if (dir <= 0)
            dir = 360; // APRS uses 001-360; 000 means "unknown"
        if (dir > 360)
            dir %= 360;
        int spd = (int)lround(r[WX_FIELD_WIND_SPEED].value);
        if (spd < 0)
            spd = 0;
        if (spd > 999)
            spd = 999;
        if (positionless)
            WX_APP("c%03ds%03d", dir, spd);
        else
            WX_APP("%03d/%03d", dir, spd);
    } else {
        WX_APP(positionless ? "c...s..." : ".../...");
    }

    // Gust.
    if (r[WX_FIELD_WIND_GUST].present) {
        int g = (int)lround(r[WX_FIELD_WIND_GUST].value);
        if (g < 0)
            g = 0;
        if (g > 999)
            g = 999;
        WX_APP("g%03d", g);
    } else {
        WX_APP("g...");
    }

    // Temperature (deg F, may be negative). Always emitted.
    if (r[WX_FIELD_TEMPERATURE].present) {
        int t = (int)lround(r[WX_FIELD_TEMPERATURE].value);
        if (t > 999)
            t = 999;
        if (t < -99)
            t = -99;
        if (t < 0)
            WX_APP("t-%02d", -t);
        else
            WX_APP("t%03d", t);
    } else {
        WX_APP("t...");
    }

    // Optional tokens - emitted only when actually present.
    if (r[WX_FIELD_RAIN_1H].present)
        WX_APP("r%03d", (int)lround(r[WX_FIELD_RAIN_1H].value) % 1000);
    if (r[WX_FIELD_RAIN_24H].present)
        WX_APP("p%03d", (int)lround(r[WX_FIELD_RAIN_24H].value) % 1000);
    if (r[WX_FIELD_RAIN_MIDNIGHT].present)
        WX_APP("P%03d", (int)lround(r[WX_FIELD_RAIN_MIDNIGHT].value) % 1000);
    if (r[WX_FIELD_HUMIDITY].present) {
        int h = (int)lround(r[WX_FIELD_HUMIDITY].value);
        if (h >= 100)
            h = 0; // on-air "00" encodes 100 %RH
        if (h < 0)
            h = 0;
        WX_APP("h%02d", h % 100);
    }
    if (r[WX_FIELD_PRESSURE].present) {
        long b = lround(r[WX_FIELD_PRESSURE].value);
        if (b < 0)
            b = 0;
        if (b > 99999)
            b = 99999;
        WX_APP("b%05ld", b);
    }
    if (r[WX_FIELD_LUMINOSITY].present) {
        int l = (int)lround(r[WX_FIELD_LUMINOSITY].value);
        if (l < 0)
            l = 0;
        if (l < 1000)
            WX_APP("L%03d", l);
        else
            WX_APP("l%03d", (l - 1000) % 1000);
    }
    if (r[WX_FIELD_SNOW_24H].present)
        WX_APP("s%03d", (int)lround(r[WX_FIELD_SNOW_24H].value) % 1000);
    if (r[WX_FIELD_FLOOD_HEIGHT_FT].present)
        WX_APP("F%06.1f", r[WX_FIELD_FLOOD_HEIGHT_FT].value);
    if (r[WX_FIELD_FLOOD_HEIGHT_M].present)
        WX_APP("f%06.1f", r[WX_FIELD_FLOOD_HEIGHT_M].value);

#undef WX_APP
    return (int)u;
}

// Builds the full TNC2 line for the weather beacon. Returns length or 0.
static int build_wx_packet(const wx_resolved_t r[WX_SENSOR_NUM], char *out, size_t outMax) {
    const char *call = g_config.wx_mycall[0] ? g_config.wx_mycall : g_config.aprs_mycall;
    uint8_t ssid = g_config.wx_mycall[0] ? g_config.wx_ssid : g_config.aprs_ssid;
    if (!call[0])
        return 0;

    char callField[16];
    if (ssid > 0)
        snprintf(callField, sizeof(callField), "%s-%d", call, (int)ssid);
    else
        snprintf(callField, sizeof(callField), "%s", call);

    char path[80];
    build_path_suffix(g_config.wx_path, path, sizeof(path));

    // Timestamp (DHM zulu for positioned/object, MDHM for positionless).
    // Reduce each field modulo its cycle so the formatter can prove a 2-digit
    // width (and so a bad clock can never emit an over-long field on-air).
    time_t now = time(NULL);
    struct tm tmv;
    gmtime_r(&now, &tmv);
    unsigned t_mon = ((unsigned)tmv.tm_mon + 1u) % 13u; // 1..12
    unsigned t_day = (unsigned)tmv.tm_mday % 32u;       // 0..31
    unsigned t_hour = (unsigned)tmv.tm_hour % 24u;      // 0..23
    unsigned t_min = (unsigned)tmv.tm_min % 60u;        // 0..59

    bool have_pos = !(g_config.wx_lat == 0.0f && g_config.wx_lon == 0.0f);
    bool is_object = g_config.wx_object[0] != 0;

    char wxTokens[160];
    char info[256];

    if (is_object) {
        // Object report carrying weather: ";NAME     *DDHHMMz{lat}/{lon}_{wx}{comment}"
        char latStr[10], lonStr[11], ts[8], name[10];
        lat_lon_to_aprs(g_config.wx_lat, g_config.wx_lon, latStr, sizeof(latStr), lonStr, sizeof(lonStr));
        snprintf(ts, sizeof(ts), "%02u%02u%02uz", t_day, t_hour, t_min);
        snprintf(name, sizeof(name), "%-9.9s", g_config.wx_object); // fixed 9 chars, space padded
        build_wx_tokens(r, false, wxTokens, sizeof(wxTokens));
        snprintf(info, sizeof(info), ";%s*%s%s/%s_%s%s", name, ts, latStr, lonStr, wxTokens, g_config.wx_comment);
    } else if (have_pos) {
        // Positioned weather report, with or without a timestamp.
        char latStr[10], lonStr[11];
        lat_lon_to_aprs(g_config.wx_lat, g_config.wx_lon, latStr, sizeof(latStr), lonStr, sizeof(lonStr));
        build_wx_tokens(r, false, wxTokens, sizeof(wxTokens));
        if (g_config.wx_timestamp) {
            char ts[8];
            snprintf(ts, sizeof(ts), "%02u%02u%02uz", t_day, t_hour, t_min);
            snprintf(info, sizeof(info), "@%s%s/%s_%s%s", ts, latStr, lonStr, wxTokens, g_config.wx_comment);
        } else {
            snprintf(info, sizeof(info), "!%s/%s_%s%s", latStr, lonStr, wxTokens, g_config.wx_comment);
        }
    } else {
        // Positionless weather report: "_MMDDHHMM" + c/s wind prefix.
        char ts8[9];
        snprintf(ts8, sizeof(ts8), "%02u%02u%02u%02u", t_mon, t_day, t_hour, t_min);
        build_wx_tokens(r, true, wxTokens, sizeof(wxTokens));
        snprintf(info, sizeof(info), "_%s%s%s", ts8, wxTokens, g_config.wx_comment);
    }

    int n = snprintf(out, outMax, "%s>%s%s:%s", callField, WX_DEST, path, info);
    if (n < 0)
        return 0;
    if (outMax > 0 && (size_t)n >= outMax)
        n = (int)outMax - 1;
    return n;
}

static uint32_t clamp_interval(uint32_t s) {
    if (s == 0)
        return WX_DEFAULT_INTERVAL_S;
    if (s < WX_MIN_INTERVAL_S)
        return WX_MIN_INTERVAL_S;
    return s;
}

/* -------------------------------------------------------------------------
 * Tasks
 * ------------------------------------------------------------------------- */

// Runs at 1 Hz for the whole life of the firmware: keeps the shared container
// current so any reader (beacon, dashboard, telemetry) sees fresh values.
static void weatherSensorTask(void *arg) {
    for (;;) {
        weather_refresh_now();
        vTaskDelay(pdMS_TO_TICKS(WX_REFRESH_PERIOD_MS));
    }
}

// Emits the APRS weather report at wx_interval when enabled.
static void weatherBeaconTask(void *arg) {
    for (;;) {
        if (!g_config.wx_en || (!g_config.wx_2rf && !g_config.wx_2inet)) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        wx_resolved_t r[WX_SENSOR_NUM];
        resolve_fields(r);

        char packet[300];
        int len = build_wx_packet(r, packet, sizeof(packet));
        if (len > 0) {
            if (g_config.wx_2rf)
                aprs_service_send_tnc2(packet, (size_t)len);
            if (g_config.wx_2inet)
                igate_send_raw(packet, (size_t)len);
            ESP_LOGI(TAG, "WX beacon TX: %s", packet);
        } else {
            ESP_LOGW(TAG, "WX enabled but no callsign configured (set Weather or APRS callsign) - skipping");
        }

        vTaskDelay(pdMS_TO_TICKS(clamp_interval(g_config.wx_interval) * 1000UL));
    }
}

void weather_start(void) {
    // Wire the backing storage into the shared container.
    memset(&s_wx, 0, sizeof(s_wx));
    memset(&s_tlm, 0, sizeof(s_tlm));
    s_tlm.analog_count = APRS_TELEMETRY_ANALOG_CHANNELS;
    s_tlm.analog_enabled = s_tlm_analog_en;
    s_tlm.analog = s_tlm_analog;
    s_tlm.digital_count = APRS_TELEMETRY_DIGITAL_CHANNELS;
    s_tlm.digital_enabled = s_tlm_digital_en;
    s_tlm.digital = s_tlm_digital;
    snprintf(s_tlm.sequence, sizeof(s_tlm.sequence), "000");

    memset(&weather_telemetry_data, 0, sizeof(weather_telemetry_data));
    weather_telemetry_data.weather = &s_wx;
    weather_telemetry_data.weather_qty = 1;
    weather_telemetry_data.telemetry_report = &s_tlm;
    weather_telemetry_data.telemetry_report_qty = 1;

    s_lock = xSemaphoreCreateMutex();

    // Make the registry lock thread-safe now that the scheduler is up, then
    // eagerly bring up any auto-registered drivers so the first sample and the
    // Weather page's channel list are populated.
    sensors_local_init();
    sensors_local_init_all();

    xTaskCreate(weatherSensorTask, "wx_sensor_task", 4096, NULL, 4, NULL);
    xTaskCreate(weatherBeaconTask, "wx_beacon_task", 6144, NULL, 4, NULL);
    ESP_LOGI(TAG, "Weather subsystem started (en=%d rf=%d inet=%d interval=%us, %u local sensor driver(s))", g_config.wx_en, g_config.wx_2rf, g_config.wx_2inet,
             (unsigned)g_config.wx_interval, (unsigned)sensors_local_count());
}
