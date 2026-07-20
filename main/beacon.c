/**
 * @file beacon.c
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
 * @brief Own-station position beacon tasks: builds APRS position reports from the
 * saved Tracker/IGate/Digipeater coordinates, resolves the configured path
 * bitmask into a digipeater path, and transmits them on RF and/or APRS-IS at each
 * beacon's own interval.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "aprs_service.h"
#include "beacon.h"
#include "igate.h"

static const char *TAG = "beacon";

// Same software-identifier destination call used by the message component
// (components/message/message.c) for consistency across the firmware.
#define BEACON_DEST "APE32L"

#define BEACON_MIN_INTERVAL_S     30   // sanity floor, in case an *_interval is set very low
#define BEACON_DEFAULT_INTERVAL_S 1800 // 30 min, used when *_interval == 0

// g_config.trk_path / igate_path / digi_path are a BITMASK over the four
// user-defined path presets in g_config.path[0..3] (see the "Path (bitmask)"
// field on the Tracker/IGate/Digipeater webconfig pages, TR_F_PATH_BITMASK).
// Bit N selects g_config.path[N]; any set bit whose slot is empty simply
// contributes nothing. Multiple bits => multiple presets, comma-joined.
//
// NOTE: this replaces an earlier "-N" / single-index scheme that misread the
// bitmask as a small integer and appended it directly to the destination
// call as an SSID (e.g. "APE32L-1"). That's not a path at all - it left
// beacons with no real digipeater path (or a bogus destination SSID),
// silently trimmed on-air with no path, so beacons could reach the local
// IGate but consistently failed to be digipeated/heard further out and
// looked "broken" from anywhere but direct RF earshot.
static void buildPathSuffix(uint8_t pathBitmask, char *out, size_t outMax) {
    out[0] = 0;
    if (pathBitmask == 0 || outMax == 0)
        return;

    size_t used = 0;
    for (int bit = 0; bit < 4; bit++) {
        if (!(pathBitmask & (1 << bit)))
            continue;
        if (!g_config.path[bit][0])
            continue; // bit selected but that preset slot isn't configured

        int n = snprintf(out + used, outMax - used, ",%s", g_config.path[bit]);
        if (n < 0)
            break;
        if ((size_t)n >= outMax - used) {
            used = outMax - 1; // truncated - stop here, out is still valid/terminated
            break;
        }
        used += (size_t)n;
    }
}

// Decimal degrees -> APRS uncompressed "DDMM.mmN" / "DDDMM.mmW" fields.
static void latLonToAprs(float lat, float lon, char *latOut, size_t latMax, char *lonOut, size_t lonMax) {
    float alat = fabsf(lat);
    int dLat = (int)alat;
    float mLat = (alat - dLat) * 60.0f;
    snprintf(latOut, latMax, "%02d%05.2f%c", dLat, mLat, lat >= 0 ? 'N' : 'S');

    float alon = fabsf(lon);
    int dLon = (int)alon;
    float mLon = (alon - dLon) * 60.0f;
    snprintf(lonOut, lonMax, "%03d%05.2f%c", dLon, mLon, lon >= 0 ? 'E' : 'W');
}

// Generic parameters for one station's fixed-position beacon, filled in by
// each of the small per-service wrappers below (tracker / igate / digi) so
// the actual packet-building logic only has to be written once.
typedef struct {
    const char *call;
    uint8_t ssid;
    uint8_t pathSel;
    bool timestamp;
    float lat;
    float lon;
    float alt;
    bool sendAltitude;
    const char *symbol; // 2 chars: table + code
    const char *comment;
} beacon_params_t;

// Builds the full TNC2 text line for one beacon transmission. Returns the
// packet length, or 0 if nothing usable is configured.
static int buildPositionPacket(const beacon_params_t *p, char *out, size_t outMax) {
    if (!p->call[0])
        return 0;

    char callField[16];
    if (p->ssid > 0)
        snprintf(callField, sizeof(callField), "%s-%d", p->call, (int)p->ssid);
    else
        snprintf(callField, sizeof(callField), "%s", p->call);

    char path[80];
    buildPathSuffix(p->pathSel, path, sizeof(path));

    char latStr[10], lonStr[11];
    latLonToAprs(p->lat, p->lon, latStr, sizeof(latStr), lonStr, sizeof(lonStr));

    // symbol = 2 chars: [0] symbol table ('/' primary or '\' alternate), [1] symbol code.
    // Falls back to a plain "car" symbol on the primary table if unset.
    char symTable = p->symbol[0] ? p->symbol[0] : '/';
    char symCode = p->symbol[1] ? p->symbol[1] : '>';

    char extra[40] = { 0 };
    if (p->sendAltitude) {
        int feet = (int)(p->alt * 3.28084f);
        if (feet < 0)
            feet = 0;
        snprintf(extra, sizeof(extra), "/A=%06d", feet);
    }

    char infoField[256]; // ts(7)+lat(9)+symTable(1)+lon(10)+symCode(1)+extra(40)+comment(up to 128)+NUL
    if (p->timestamp) {
        time_t now = time(NULL);
        struct tm tmv;
        gmtime_r(&now, &tmv);
        char ts[8];
        snprintf(ts, sizeof(ts), "%02d%02d%02dz", tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
        snprintf(infoField, sizeof(infoField), "/%s%s%c%s%c%s%s", ts, latStr, symTable, lonStr, symCode, extra, p->comment);
    } else {
        snprintf(infoField, sizeof(infoField), "!%s%c%s%c%s%s", latStr, symTable, lonStr, symCode, extra, p->comment);
    }

    int n = snprintf(out, outMax, "%s>%s%s:%s", callField, BEACON_DEST, path, infoField);
    // snprintf() returns the length it *would* have written; on truncation that
    // exceeds the buffer. Callers use this value as the memcpy length for the
    // TNC2 send path, so clamp it to what was actually written (outMax-1, since
    // snprintf always NUL-terminates when outMax > 0).
    if (n < 0)
        return 0;
    if (outMax > 0 && (size_t)n >= outMax)
        n = (int)outMax - 1;
    return n;
}

static uint32_t clampInterval(uint32_t interval) {
    if (interval < BEACON_MIN_INTERVAL_S)
        return interval == 0 ? BEACON_DEFAULT_INTERVAL_S : BEACON_MIN_INTERVAL_S;
    return interval;
}

// ---------------------------------------------------------------------------
// Tracker beacon (Tracker web admin page: g_config.trk_*)
// ---------------------------------------------------------------------------
static void trackerBeaconTask(void *arg) {
    for (;;) {
        if (!g_config.trk_en || (!g_config.trk_loc2rf && !g_config.trk_loc2inet)) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        beacon_params_t p = {
            .call = g_config.trk_mycall[0] ? g_config.trk_mycall : g_config.aprs_mycall,
            .ssid = g_config.trk_mycall[0] ? g_config.trk_ssid : g_config.aprs_ssid,
            .pathSel = g_config.trk_path,
            .timestamp = g_config.trk_timestamp,
            .lat = g_config.trk_lat,
            .lon = g_config.trk_lon,
            .alt = g_config.trk_alt,
            .sendAltitude = g_config.trk_altitude,
            .symbol = g_config.trk_symbol,
            .comment = g_config.trk_comment,
        };

        char packet[400]; // callField+dest+path+infoField(up to 256), grown for 128-byte comments
        int len = buildPositionPacket(&p, packet, sizeof(packet));
        if (len > 0) {
            // Log the RF and internet legs from what actually happened,
            // rather than an unconditional "beacon TX" line. igate_send_raw()
            // returns false (no bytes sent) whenever the APRS-IS uplink isn't
            // connected yet (e.g. no internet route at boot) - previously the
            // log line printed regardless, which made it look like an
            // internet transmission had gone out before the internet
            // connection was up, when the send was actually silently dropped.
            if (g_config.trk_loc2rf) {
                if (aprs_service_send_tnc2(packet, (size_t)len))
                    ESP_LOGI(TAG, "Tracker beacon TX (RF): %s", packet);
                else
                    ESP_LOGW(TAG, "Tracker beacon NOT sent over RF - modem not ready or busy: %s", packet);
            }
            if (g_config.trk_loc2inet) {
                if (igate_send_raw(packet, (size_t)len))
                    ESP_LOGI(TAG, "Tracker beacon TX (INET): %s", packet);
                else
                    ESP_LOGW(TAG, "Tracker beacon NOT sent over INET - APRS-IS not connected yet: %s", packet);
            }

            ESP_LOGD(TAG, "trk_beacon_task stack free: %u bytes", (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
        } else {
            ESP_LOGW(TAG, "Tracker beacon enabled but no callsign configured (set Tracker or APRS callsign) - skipping");
        }

        vTaskDelay(pdMS_TO_TICKS(clampInterval(g_config.trk_interval) * 1000UL));
    }
}

// ---------------------------------------------------------------------------
// IGate beacon (IGate web admin page: g_config.igate_*)
// ---------------------------------------------------------------------------
static void igateBeaconTask(void *arg) {
    for (;;) {
        if (!g_config.igate_en || !g_config.igate_bcn || (!g_config.igate_loc2rf && !g_config.igate_loc2inet)) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        beacon_params_t p = {
            .call = g_config.aprs_mycall,
            .ssid = g_config.aprs_ssid,
            .pathSel = g_config.igate_path,
            .timestamp = g_config.igate_timestamp,
            .lat = g_config.igate_lat,
            .lon = g_config.igate_lon,
            .alt = g_config.igate_alt,
            .sendAltitude = g_config.igate_alt != 0.0f,
            .symbol = g_config.igate_symbol,
            .comment = g_config.igate_comment,
        };

        char packet[400]; // callField+dest+path+infoField(up to 256), grown for 128-byte comments
        int len = buildPositionPacket(&p, packet, sizeof(packet));
        if (len > 0) {
            // See the identical note in trackerBeaconTask(): log what each
            // leg actually did instead of an unconditional line, so a
            // not-yet-connected APRS-IS uplink doesn't look like a
            // premature internet transmission.
            if (g_config.igate_loc2rf) {
                if (aprs_service_send_tnc2(packet, (size_t)len))
                    ESP_LOGI(TAG, "IGate beacon TX (RF): %s", packet);
                else
                    ESP_LOGW(TAG, "IGate beacon NOT sent over RF - modem not ready or busy: %s", packet);
            }
            if (g_config.igate_loc2inet) {
                if (igate_send_raw(packet, (size_t)len))
                    ESP_LOGI(TAG, "IGate beacon TX (INET): %s", packet);
                else
                    ESP_LOGW(TAG, "IGate beacon NOT sent over INET - APRS-IS not connected yet: %s", packet);
            }

            ESP_LOGD(TAG, "igate_beacon_task stack free: %u bytes", (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
        } else {
            ESP_LOGW(TAG, "IGate beacon enabled but no APRS callsign configured - skipping");
        }

        vTaskDelay(pdMS_TO_TICKS(clampInterval(g_config.igate_interval) * 1000UL));
    }
}

// ---------------------------------------------------------------------------
// Digipeater beacon (Digipeater web admin page: g_config.digi_*)
// ---------------------------------------------------------------------------
static void digiBeaconTask(void *arg) {
    for (;;) {
        if (!g_config.digi_en || !g_config.digi_bcn || (!g_config.digi_loc2rf && !g_config.digi_loc2inet)) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        const char *call = g_config.digi_mycall[0] ? g_config.digi_mycall : g_config.aprs_mycall;
        uint8_t ssid = g_config.digi_mycall[0] ? g_config.digi_ssid : g_config.aprs_ssid;

        beacon_params_t p = {
            .call = call,
            .ssid = ssid,
            .pathSel = g_config.digi_path,
            .timestamp = g_config.digi_timestamp,
            .lat = g_config.digi_lat,
            .lon = g_config.digi_lon,
            .alt = g_config.digi_alt,
            .sendAltitude = g_config.digi_alt != 0.0f,
            .symbol = g_config.digi_symbol,
            .comment = g_config.digi_comment,
        };

        char packet[400]; // callField+dest+path+infoField(up to 256), grown for 128-byte comments
        int len = buildPositionPacket(&p, packet, sizeof(packet));
        if (len > 0) {
            // See the identical note in trackerBeaconTask(): log what each
            // leg actually did instead of an unconditional line, so a
            // not-yet-connected APRS-IS uplink doesn't look like a
            // premature internet transmission.
            if (g_config.digi_loc2rf) {
                if (aprs_service_send_tnc2(packet, (size_t)len))
                    ESP_LOGI(TAG, "Digipeater beacon TX (RF): %s", packet);
                else
                    ESP_LOGW(TAG, "Digipeater beacon NOT sent over RF - modem not ready or busy: %s", packet);
            }
            if (g_config.digi_loc2inet) {
                if (igate_send_raw(packet, (size_t)len))
                    ESP_LOGI(TAG, "Digipeater beacon TX (INET): %s", packet);
                else
                    ESP_LOGW(TAG, "Digipeater beacon NOT sent over INET - APRS-IS not connected yet: %s", packet);
            }

            ESP_LOGD(TAG, "digi_beacon_task stack free: %u bytes", (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
        } else {
            ESP_LOGW(TAG, "Digipeater beacon enabled but no callsign configured (set Digipeater or APRS callsign) - skipping");
        }

        vTaskDelay(pdMS_TO_TICKS(clampInterval(g_config.digi_interval) * 1000UL));
    }
}

// Stack size note: buildPositionPacket()'s call chain does several
// snprintf()s, including float formatting in latLonToAprs() (%05.2f).
// Newlib's float-capable *printf pulls in a noticeably deeper call tree
// than integer-only formatting, and buildPathSuffix() adds one *more*
// snprintf on top of that whenever a non-empty path preset is selected
// (trk_path=1 in this config selects "WIDE1-1,WIDE2-1", while the digi/igate
// path bits happen to point at empty preset slots and skip that extra
// call entirely). That's exactly why only the tracker beacon corrupts in
// practice - it's the one task whose call depth actually reaches that
// additional snprintf. 6144 bytes was "usually enough" but left too little
// headroom for that combined call tree, silently overrunning the stack and
// corrupting/zeroing nearby locals so only a trailing substring (e.g. just
// the path suffix) of the packet survived - which is exactly the ",WIDE1-1"
// truncation seen in the field. Bumped up with real margin on all three
// tasks, since the corruption is a function of *what gets configured*
// (which path bits are set, comment/altitude present, etc.), not which
// specific task it is - so all three need the same safety margin, not just
// tracker. Also logging each task's watermark so a tight stack shows up in
// the logs instead of as a mysteriously truncated packet.
#define BEACON_TASK_STACK_BYTES 12288 // was 10240; infoField 200->256 and packet 300->400 (128-byte comment support) ate into the existing margin

void beacon_start(void) {
    xTaskCreate(trackerBeaconTask, "trk_beacon_task", BEACON_TASK_STACK_BYTES, NULL, 4, NULL);
    ESP_LOGI(TAG, "Tracker beacon task started (en=%d rf=%d inet=%d interval=%us)", g_config.trk_en, g_config.trk_loc2rf,
             g_config.trk_loc2inet, (unsigned)g_config.trk_interval);

    xTaskCreate(igateBeaconTask, "igate_beacon_task", BEACON_TASK_STACK_BYTES, NULL, 4, NULL);
    ESP_LOGI(TAG, "IGate beacon task started (en=%d bcn=%d rf=%d inet=%d interval=%us)", g_config.igate_en, g_config.igate_bcn,
             g_config.igate_loc2rf, g_config.igate_loc2inet, (unsigned)g_config.igate_interval);

    xTaskCreate(digiBeaconTask, "digi_beacon_task", BEACON_TASK_STACK_BYTES, NULL, 4, NULL);
    ESP_LOGI(TAG, "Digipeater beacon task started (en=%d bcn=%d rf=%d inet=%d interval=%us)", g_config.digi_en, g_config.digi_bcn,
             g_config.digi_loc2rf, g_config.digi_loc2inet, (unsigned)g_config.digi_interval);
}
