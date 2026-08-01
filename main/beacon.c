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
 * @brief Own-station position and status beacon tasks: builds APRS position
 * reports from the saved Tracker/IGate/Digipeater coordinates, resolves the
 * configured path bitmask into a digipeater path, and transmits them on RF
 * and/or APRS-IS at each beacon's own interval. Also builds and transmits
 * each station's APRS status report (DTI '>', APRS101 ch.16) from that page's
 * own status text, at its own independent interval.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "aprs_coord.h"
#include "aprs_path.h" // aprs_path_build_suffix()
#include "aprs_service.h"
#include "beacon.h"
#include "beacon_scheduler.h" // beacon_scheduler_jitter()
#include "igate.h"
#include "sched_time.h" // sched_mono_seconds() / sched_clamp_interval()

static const char *TAG = "beacon";

// Same software-identifier destination call used by the message component
// (components/message/message.c) for consistency across the firmware.
#define BEACON_DEST "APE32L"

#define BEACON_MIN_INTERVAL_S     30   // sanity floor, in case an *_interval is set very low
#define BEACON_DEFAULT_INTERVAL_S 1800 // 30 min, used when *_interval == 0

// Generic parameters for one station's fixed-position beacon, filled in by
// each of the small per-service wrappers below (tracker / igate / digi) so
// the actual packet-building logic only has to be written once.
// This owns copies of every string it needs (call/symbol/comment) and a
// snapshot of the four path presets, rather than pointing into g_config. The
// web task rewrites g_config field-by-field on a settings save, so a builder
// that dereferenced g_config strings directly could read one mid-strcpy (torn
// or momentarily unterminated) and run off the end. Each per-service function
// fills this under app_config_lock() and then builds/transmits from the copy.
typedef struct {
    char call[10];
    uint8_t ssid;
    uint8_t pathSel;
    bool timestamp;
    float lat;
    float lon;
    float alt;
    bool sendAltitude;
    char symbol[3]; // table + code + NUL
    char comment[COMMENT_SIZE];
    char pathPreset[4][72]; // snapshot of g_config.path[0..3]
    // When set, buildPositionPacket() emits the APRS base-91 compressed
    // position field (aprs_coord_format_compressed()) instead of the
    // uncompressed "DDMM.mmN/DDDMM.mmW" pair, saving airtime at 1200 baud.
    // Sourced from g_config.{trk,igate,digi}_compress by each per-service
    // function below.
    bool compress;
    // PHG (Power-Height-Gain-Directivity) data extension, only used by the
    // IGate beacon so far (see igateBeaconService()). Not settable for the
    // Tracker/Digipeater beacons, so phgEnable stays false and the field is
    // never emitted for those. Shares the same 7-byte info-field slot used
    // for CSE/SPD by moving stations - mutually exclusive with movement,
    // which is fine here since these are fixed-position beacons.
    bool phgEnable;
    uint16_t phgPower;  // Watts
    float phgGain;      // dB
    uint16_t phgHeight; // feet (APRS PHG code table's own unit)
    uint8_t phgDir;     // 0=Omni, 1-8 = N,NE,E,SE,S,SW,W,NW
} beacon_params_t;

// Builds the 7-byte "PHGphgd" data-extension token from PHG sub-fields, using
// the same rounding/clamping and single-character-per-digit encoding as the
// Station/Objects web pages (see objitem_build_phg() in objects_items.c and
// the calcStationPHG() JS on the Station page). `out` must be >= 8 bytes.
static void buildPhgExtension(uint16_t power, float gain, uint16_t height, uint8_t dir, char *out, size_t outMax) {
    int P = (int)lroundf(sqrtf((float)power));
    if (P < 0)
        P = 0;
    if (P > 9)
        P = 9;

    float hf = (height >= 10) ? (float)height : 10.0f;
    int H = (int)lroundf(log2f(hf / 10.0f));
    if (H < 0)
        H = 0;
    if (H > 13) // keeps the height character a single printable ASCII byte
        H = 13;

    int G = (int)lroundf(gain);
    if (G < 0)
        G = 0;
    if (G > 9)
        G = 9;

    int D = (int)dir;
    if (D < 0)
        D = 0;
    if (D > 8)
        D = 8;

    snprintf(out, outMax, "PHG%c%c%c%c", '0' + P, '0' + H, '0' + G, '0' + D);
}

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
    aprs_path_build_suffix(p->pathSel, p->pathPreset, path, sizeof(path));

    // symbol = 2 chars: [0] symbol table ('/' primary or '\' alternate), [1] symbol code.
    // Falls back to a plain "car" symbol on the primary table if unset.
    char symTable = p->symbol[0] ? p->symbol[0] : '/';
    char symCode = p->symbol[1] ? p->symbol[1] : '>';

    // PHG data-extension token, when enabled. Emitted right after the symbol
    // code and before the altitude/comment, matching the Object/Item info
    // field layout (main/objects_items.c) and the order the APRS spec
    // defines for this slot.
    char phg[8] = { 0 };
    if (p->phgEnable)
        buildPhgExtension(p->phgPower, p->phgGain, p->phgHeight, p->phgDir, phg, sizeof(phg));

    // The compressed position format has no PHG equivalent (APRS101 ch.9:
    // "this format does not support PHG"), so a PHG-enabled beacon always
    // falls back to the uncompressed layout regardless of the compress
    // flag - emitting compressed PHG bytes would just be wrong data, and
    // dropping PHG silently to keep compression would lose a field the user
    // explicitly enabled. None of these three fixed-position beacons track
    // course/speed, so when compression is used the cs/T slot always
    // carries "no cs/T data" (3 spaces).
    bool useCompressed = p->compress && !p->phgEnable;

    // Sized for the larger of the two layouts: uncompressed is up to 21
    // bytes (9-char latStr content + symTable + 10-char lonStr content +
    // symCode), compressed is a fixed 13 bytes (symTable + 4 lat + 4 lon +
    // symCode + 3 cs/T), plus NUL either way.
    char posField[22];
    if (useCompressed) {
        aprs_coord_format_compressed(p->lat, p->lon, symTable, symCode, "   ", posField, sizeof(posField));
    } else {
        char latStr[10], lonStr[11];
        aprs_coord_format(p->lat, p->lon, latStr, sizeof(latStr), lonStr, sizeof(lonStr));
        snprintf(posField, sizeof(posField), "%s%c%s%c", latStr, symTable, lonStr, symCode);
    }

    char extra[40] = { 0 };
    if (p->sendAltitude) {
        int feet = (int)(p->alt * 3.28084f);
        if (feet < 0)
            feet = 0;
        snprintf(extra, sizeof(extra), "/A=%06d", feet);
    }

    char infoField[256]; // ts(7)+posField(up to 21)+phg(7)+extra(40)+comment(up to 128)+NUL
    if (p->timestamp) {
        time_t now = time(NULL);
        struct tm tmv;
        gmtime_r(&now, &tmv);
        char ts[8];
        snprintf(ts, sizeof(ts), "%02d%02d%02dz", tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
        snprintf(infoField, sizeof(infoField), "/%s%s%s%s%s", ts, posField, phg, extra, p->comment);
    } else {
        snprintf(infoField, sizeof(infoField), "!%s%s%s%s", posField, phg, extra, p->comment);
    }

    int n = snprintf(out, outMax, "%s>%s%s:%s", callField, BEACON_DEST, path, infoField);
    // snprintf() returns the length it *would* have written, so a result at or
    // past outMax means the line did not fit. Refuse it instead of returning a
    // clamped length: the RF leg cannot encode more than APRS_TNC2_MAX_LEN
    // bytes into an AX.25 frame, so a clamped length would only put a truncated
    // beacon on the air (or none at all, while the same over-long line still
    // went out over APRS-IS). Returning 0 makes the caller skip both legs and
    // say so, which is what tells the operator to shorten the comment or the
    // path.
    if (n < 0)
        return 0;
    if ((size_t)n >= outMax || n > APRS_TNC2_MAX_LEN) {
        ESP_LOGW(TAG, "beacon packet too long (%d bytes, max %d) - shorten the comment or the path", n, APRS_TNC2_MAX_LEN);
        return 0;
    }
    return n;
}

// Generic parameters for one station's status-report beacon (APRS101 ch.16).
// Filled in by each per-service wrapper below (tracker / igate / digi) from a
// snapshot of that page's own status text/interval, following the same
// copy-under-lock policy as beacon_params_t above.
typedef struct {
    char call[10];
    uint8_t ssid;
    uint8_t pathSel;
    char statusText[STATUS_SIZE];
    char pathPreset[4][72]; // snapshot of g_config.path[0..3]
} status_params_t;

// Builds the full TNC2 text line for one status-report transmission. The
// info field is DTI '>' followed by the free-text status (APRS101 ch.16);
// the status text itself may start with a Maidenhead locator or a beam
// heading/power token per the spec - this builder does not interpret it,
// it simply carries whatever the station operator configured on the page.
// Returns the packet length, or 0 if nothing usable is configured.
static int buildStatusPacket(const status_params_t *p, char *out, size_t outMax) {
    if (!p->call[0] || !p->statusText[0])
        return 0;

    char callField[16];
    if (p->ssid > 0)
        snprintf(callField, sizeof(callField), "%s-%d", p->call, (int)p->ssid);
    else
        snprintf(callField, sizeof(callField), "%s", p->call);

    char path[80];
    aprs_path_build_suffix(p->pathSel, p->pathPreset, path, sizeof(path));

    char infoField[STATUS_SIZE + 1]; // '>' + status text + NUL
    snprintf(infoField, sizeof(infoField), ">%s", p->statusText);

    int n = snprintf(out, outMax, "%s>%s%s:%s", callField, BEACON_DEST, path, infoField);
    // A status line is at most 152 bytes: call field (up to 15) + '>' +
    // destination (6) + path (up to 79) + ':' + info field (up to 50), so it
    // always fits the APRS_TNC2_BUF_SIZE buffer every caller provides. The
    // check is kept anyway, on the same terms as buildPositionPacket(): refuse
    // rather than clamp, so neither leg ever carries a truncated - and
    // therefore malformed - status report.
    if (n < 0)
        return 0;
    if ((size_t)n >= outMax || n > APRS_TNC2_MAX_LEN) {
        ESP_LOGW(TAG, "status packet too long (%d bytes, max %d) - shorten the status text or the path", n, APRS_TNC2_MAX_LEN);
        return 0;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Status beacons (Tracker / IGate / Digipeater "Status Beacon" fieldset).
//
// Each of the three position beacons above has a matching status-report
// beacon (APRS101 ch.16): its own interval (*_sts_interval, 0 = off) and
// free-text status (*_status), edited on the same web admin page as the
// position beacon and sent over the same RF/INET legs (*_loc2rf/*_loc2inet)
// as that page's position beacon, at its own independent schedule.
// ---------------------------------------------------------------------------
static int64_t s_trk_sts_next_due = 0;
static int64_t s_igate_sts_next_due = 0;
static int64_t s_digi_sts_next_due = 0;

static uint32_t trackerStatusService(void) {
    if (!g_config.trk_en || g_config.trk_sts_interval == 0 || (!g_config.trk_loc2rf && !g_config.trk_loc2inet)) {
        s_trk_sts_next_due = 0;
        return 5;
    }

    int64_t now = sched_mono_seconds();
    if (now >= s_trk_sts_next_due) {
        status_params_t p = { 0 };
        app_config_lock();
        {
            bool useTrk = g_config.trk_mycall[0] != 0;
            memcpy(p.call, useTrk ? g_config.trk_mycall : g_config.aprs_mycall, sizeof(p.call));
            p.ssid = useTrk ? g_config.trk_ssid : g_config.aprs_ssid;
            p.pathSel = g_config.trk_path;
            memcpy(p.statusText, g_config.trk_status, sizeof(p.statusText));
            memcpy(p.pathPreset, g_config.path, sizeof(p.pathPreset));
        }
        app_config_unlock();

        char packet[APRS_TNC2_BUF_SIZE]; // sized by the RF leg's own limit, so a line that does not fit is refused at build time
        int len = buildStatusPacket(&p, packet, sizeof(packet));
        if (len > 0) {
            if (g_config.trk_loc2rf) {
                if (aprs_service_send_tnc2(packet, (size_t)len))
                    ESP_LOGI(TAG, "Tracker status TX (RF): %s", packet);
                else
                    ESP_LOGW(TAG, "Tracker status NOT sent over RF - modem not ready or busy: %s", packet);
            }
            if (g_config.trk_loc2inet) {
                if (igate_send_raw(packet, (size_t)len))
                    ESP_LOGI(TAG, "Tracker status TX (INET): %s", packet);
                else
                    ESP_LOGW(TAG, "Tracker status NOT sent over INET - APRS-IS not connected yet: %s", packet);
            }
        }

        s_trk_sts_next_due =
            now + (int64_t)beacon_scheduler_jitter(sched_clamp_interval(g_config.trk_sts_interval, BEACON_MIN_INTERVAL_S, BEACON_DEFAULT_INTERVAL_S));
    }

    int64_t rem = s_trk_sts_next_due - sched_mono_seconds();
    if (rem < 1)
        rem = 1;
    return (uint32_t)rem;
}

static uint32_t igateStatusService(void) {
    if (!g_config.igate_en || g_config.igate_sts_interval == 0 || (!g_config.igate_loc2rf && !g_config.igate_loc2inet)) {
        s_igate_sts_next_due = 0;
        return 5;
    }

    int64_t now = sched_mono_seconds();
    if (now >= s_igate_sts_next_due) {
        status_params_t p = { 0 };
        app_config_lock();
        {
            memcpy(p.call, g_config.aprs_mycall, sizeof(p.call));
            p.ssid = g_config.aprs_ssid;
            p.pathSel = g_config.igate_path;
            memcpy(p.statusText, g_config.igate_status, sizeof(p.statusText));
            memcpy(p.pathPreset, g_config.path, sizeof(p.pathPreset));
        }
        app_config_unlock();

        char packet[APRS_TNC2_BUF_SIZE]; // sized by the RF leg's own limit, so a line that does not fit is refused at build time
        int len = buildStatusPacket(&p, packet, sizeof(packet));
        if (len > 0) {
            if (g_config.igate_loc2rf) {
                if (aprs_service_send_tnc2(packet, (size_t)len))
                    ESP_LOGI(TAG, "IGate status TX (RF): %s", packet);
                else
                    ESP_LOGW(TAG, "IGate status NOT sent over RF - modem not ready or busy: %s", packet);
            }
            if (g_config.igate_loc2inet) {
                if (igate_send_raw(packet, (size_t)len))
                    ESP_LOGI(TAG, "IGate status TX (INET): %s", packet);
                else
                    ESP_LOGW(TAG, "IGate status NOT sent over INET - APRS-IS not connected yet: %s", packet);
            }
        }

        s_igate_sts_next_due =
            now + (int64_t)beacon_scheduler_jitter(sched_clamp_interval(g_config.igate_sts_interval, BEACON_MIN_INTERVAL_S, BEACON_DEFAULT_INTERVAL_S));
    }

    int64_t rem = s_igate_sts_next_due - sched_mono_seconds();
    if (rem < 1)
        rem = 1;
    return (uint32_t)rem;
}

static uint32_t digiStatusService(void) {
    if (!g_config.digi_en || g_config.digi_sts_interval == 0 || (!g_config.digi_loc2rf && !g_config.digi_loc2inet)) {
        s_digi_sts_next_due = 0;
        return 5;
    }

    int64_t now = sched_mono_seconds();
    if (now >= s_digi_sts_next_due) {
        status_params_t p = { 0 };
        app_config_lock();
        {
            bool useDigi = g_config.digi_mycall[0] != 0;
            memcpy(p.call, useDigi ? g_config.digi_mycall : g_config.aprs_mycall, sizeof(p.call));
            p.ssid = useDigi ? g_config.digi_ssid : g_config.aprs_ssid;
            p.pathSel = g_config.digi_path;
            memcpy(p.statusText, g_config.digi_status, sizeof(p.statusText));
            memcpy(p.pathPreset, g_config.path, sizeof(p.pathPreset));
        }
        app_config_unlock();

        char packet[APRS_TNC2_BUF_SIZE]; // sized by the RF leg's own limit, so a line that does not fit is refused at build time
        int len = buildStatusPacket(&p, packet, sizeof(packet));
        if (len > 0) {
            if (g_config.digi_loc2rf) {
                if (aprs_service_send_tnc2(packet, (size_t)len))
                    ESP_LOGI(TAG, "Digipeater status TX (RF): %s", packet);
                else
                    ESP_LOGW(TAG, "Digipeater status NOT sent over RF - modem not ready or busy: %s", packet);
            }
            if (g_config.digi_loc2inet) {
                if (igate_send_raw(packet, (size_t)len))
                    ESP_LOGI(TAG, "Digipeater status TX (INET): %s", packet);
                else
                    ESP_LOGW(TAG, "Digipeater status NOT sent over INET - APRS-IS not connected yet: %s", packet);
            }
        }

        s_digi_sts_next_due =
            now + (int64_t)beacon_scheduler_jitter(sched_clamp_interval(g_config.digi_sts_interval, BEACON_MIN_INTERVAL_S, BEACON_DEFAULT_INTERVAL_S));
    }

    int64_t rem = s_digi_sts_next_due - sched_mono_seconds();
    if (rem < 1)
        rem = 1;
    return (uint32_t)rem;
}

// ---------------------------------------------------------------------------
// Tracker beacon (Tracker web admin page: g_config.trk_*)
// ---------------------------------------------------------------------------
// Per-beacon monotonic "next due" timestamp (seconds). 0 = due now, so an
// enabled beacon transmits once on the first pass after start and only then
// begins counting out its interval.
static int64_t s_trk_next_due = 0;

// One serviced pass of the tracker beacon. Called by the shared beacon
// scheduler (beacon_scheduler.c) via beacon_service(); returns the number of
// seconds until this beacon next wants servicing.
static uint32_t trackerBeaconService(void) {
    if (!g_config.trk_en || (!g_config.trk_loc2rf && !g_config.trk_loc2inet)) {
        s_trk_next_due = 0; // reset so (re-)enabling the beacon fires an immediate TX
        return 5;           // idle re-check cadence while the beacon is off
    }

    int64_t now = sched_mono_seconds();
    if (now >= s_trk_next_due) {
        beacon_params_t p = { 0 }; // zero-init: Tracker never sets phg*, so phgEnable must default false
        app_config_lock();
        {
            bool useTrk = g_config.trk_mycall[0] != 0;
            memcpy(p.call, useTrk ? g_config.trk_mycall : g_config.aprs_mycall, sizeof(p.call));
            p.ssid = useTrk ? g_config.trk_ssid : g_config.aprs_ssid;
            p.pathSel = g_config.trk_path;
            p.timestamp = g_config.trk_timestamp;
            p.lat = g_config.trk_lat;
            p.lon = g_config.trk_lon;
            p.alt = g_config.trk_alt;
            p.sendAltitude = g_config.trk_altitude;
            p.compress = g_config.trk_compress;
            memcpy(p.symbol, g_config.trk_symbol, sizeof(p.symbol));
            memcpy(p.comment, g_config.trk_comment, sizeof(p.comment));
            memcpy(p.pathPreset, g_config.path, sizeof(p.pathPreset));
        }
        app_config_unlock();

        char packet[APRS_TNC2_BUF_SIZE]; // sized by the RF leg's own limit, so a line that does not fit is refused at build time
        int len = buildPositionPacket(&p, packet, sizeof(packet));
        if (len > 0) {
            // Log the RF and internet legs from what actually happened,
            // rather than an unconditional "beacon TX" line. igate_send_raw()
            // returns false (no bytes sent) whenever the APRS-IS uplink isn't
            // connected yet (e.g. no internet route at boot), so logging per
            // leg avoids reporting an internet transmission that was actually
            // dropped because the connection was not up.
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
            ESP_LOGW(TAG, "Tracker beacon not built - no callsign configured (set Tracker or APRS callsign), or the line did not fit; skipping");
        }

        s_trk_next_due = now + (int64_t)beacon_scheduler_jitter(sched_clamp_interval(g_config.trk_interval, BEACON_MIN_INTERVAL_S, BEACON_DEFAULT_INTERVAL_S));
    }

    int64_t rem = s_trk_next_due - sched_mono_seconds();
    if (rem < 1)
        rem = 1;
    return (uint32_t)rem;
}

// ---------------------------------------------------------------------------
// IGate beacon (IGate web admin page: g_config.igate_*)
// ---------------------------------------------------------------------------
static int64_t s_igate_next_due = 0;

static uint32_t igateBeaconService(void) {
    if (!g_config.igate_en || !g_config.igate_bcn || (!g_config.igate_loc2rf && !g_config.igate_loc2inet)) {
        s_igate_next_due = 0;
        return 5;
    }

    int64_t now = sched_mono_seconds();
    if (now >= s_igate_next_due) {
        beacon_params_t p = { 0 };
        app_config_lock();
        {
            memcpy(p.call, g_config.aprs_mycall, sizeof(p.call));
            p.ssid = g_config.aprs_ssid;
            p.pathSel = g_config.igate_path;
            p.timestamp = g_config.igate_timestamp;
            p.lat = g_config.igate_lat;
            p.lon = g_config.igate_lon;
            p.alt = g_config.igate_alt;
            p.sendAltitude = g_config.igate_alt != 0.0f;
            p.compress = g_config.igate_compress;
            memcpy(p.symbol, g_config.igate_symbol, sizeof(p.symbol));
            memcpy(p.comment, g_config.igate_comment, sizeof(p.comment));
            memcpy(p.pathPreset, g_config.path, sizeof(p.pathPreset));
            p.phgEnable = g_config.igate_phg_enable;
            p.phgPower = g_config.igate_phg_power;
            p.phgGain = g_config.igate_phg_gain;
            p.phgHeight = g_config.igate_phg_height;
            p.phgDir = g_config.igate_phg_dir;
        }
        app_config_unlock();

        char packet[APRS_TNC2_BUF_SIZE]; // sized by the RF leg's own limit, so a line that does not fit is refused at build time
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
            ESP_LOGW(TAG, "IGate beacon not built - no APRS callsign configured, or the line did not fit; skipping");
        }

        s_igate_next_due =
            now + (int64_t)beacon_scheduler_jitter(sched_clamp_interval(g_config.igate_interval, BEACON_MIN_INTERVAL_S, BEACON_DEFAULT_INTERVAL_S));
    }

    int64_t rem = s_igate_next_due - sched_mono_seconds();
    if (rem < 1)
        rem = 1;
    return (uint32_t)rem;
}

// ---------------------------------------------------------------------------
// Digipeater beacon (Digipeater web admin page: g_config.digi_*)
// ---------------------------------------------------------------------------
static int64_t s_digi_next_due = 0;

static uint32_t digiBeaconService(void) {
    if (!g_config.digi_en || !g_config.digi_bcn || (!g_config.digi_loc2rf && !g_config.digi_loc2inet)) {
        s_digi_next_due = 0;
        return 5;
    }

    int64_t now = sched_mono_seconds();
    if (now >= s_digi_next_due) {
        beacon_params_t p = { 0 }; // zero-init: Digipeater never sets phg*, so phgEnable must default false
        app_config_lock();
        {
            bool useDigi = g_config.digi_mycall[0] != 0;
            memcpy(p.call, useDigi ? g_config.digi_mycall : g_config.aprs_mycall, sizeof(p.call));
            p.ssid = useDigi ? g_config.digi_ssid : g_config.aprs_ssid;
            p.pathSel = g_config.digi_path;
            p.timestamp = g_config.digi_timestamp;
            p.lat = g_config.digi_lat;
            p.lon = g_config.digi_lon;
            p.alt = g_config.digi_alt;
            p.sendAltitude = g_config.digi_alt != 0.0f;
            p.compress = g_config.digi_compress;
            memcpy(p.symbol, g_config.digi_symbol, sizeof(p.symbol));
            memcpy(p.comment, g_config.digi_comment, sizeof(p.comment));
            memcpy(p.pathPreset, g_config.path, sizeof(p.pathPreset));
        }
        app_config_unlock();

        char packet[APRS_TNC2_BUF_SIZE]; // sized by the RF leg's own limit, so a line that does not fit is refused at build time
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
            ESP_LOGW(TAG, "Digipeater beacon not built - no callsign configured (set Digipeater or APRS callsign), or the line did not fit; skipping");
        }

        s_digi_next_due =
            now + (int64_t)beacon_scheduler_jitter(sched_clamp_interval(g_config.digi_interval, BEACON_MIN_INTERVAL_S, BEACON_DEFAULT_INTERVAL_S));
    }

    int64_t rem = s_digi_next_due - sched_mono_seconds();
    if (rem < 1)
        rem = 1;
    return (uint32_t)rem;
}

// Service all three position beacons in one pass and return the number of
// seconds until the soonest of them next wants servicing. Called from the
// shared beacon scheduler task (beacon_scheduler.c) so the tracker/igate/digi
// beacons do not each need their own FreeRTOS task and 12 KB stack - they
// share the scheduler's single stack, run sequentially (the half-duplex modem
// serialises them anyway), and keep their independent enable flags/intervals
// via the per-beacon s_*_next_due timers above.
uint32_t beacon_service(void) {
    uint32_t soonest = trackerBeaconService();
    uint32_t s;
    s = igateBeaconService();
    if (s < soonest)
        soonest = s;
    s = digiBeaconService();
    if (s < soonest)
        soonest = s;
    s = trackerStatusService();
    if (s < soonest)
        soonest = s;
    s = igateStatusService();
    if (s < soonest)
        soonest = s;
    s = digiStatusService();
    if (s < soonest)
        soonest = s;
    return soonest;
}

void beacon_start(void) {
    // No task creation here: the tracker/igate/digi beacons are driven by the
    // shared beacon scheduler (beacon_scheduler_start()), which calls
    // beacon_service() above. This just logs the configured state.
    ESP_LOGI(TAG, "Tracker beacon configured (en=%d rf=%d inet=%d interval=%us)", g_config.trk_en, g_config.trk_loc2rf, g_config.trk_loc2inet,
             (unsigned)g_config.trk_interval);
    ESP_LOGI(TAG, "IGate beacon configured (en=%d bcn=%d rf=%d inet=%d interval=%us)", g_config.igate_en, g_config.igate_bcn, g_config.igate_loc2rf,
             g_config.igate_loc2inet, (unsigned)g_config.igate_interval);
    ESP_LOGI(TAG, "Digipeater beacon configured (en=%d bcn=%d rf=%d inet=%d interval=%us)", g_config.digi_en, g_config.digi_bcn, g_config.digi_loc2rf,
             g_config.digi_loc2inet, (unsigned)g_config.digi_interval);
    ESP_LOGI(TAG, "Tracker status beacon configured (en=%d interval=%us)", g_config.trk_en, (unsigned)g_config.trk_sts_interval);
    ESP_LOGI(TAG, "IGate status beacon configured (en=%d interval=%us)", g_config.igate_en, (unsigned)g_config.igate_sts_interval);
    ESP_LOGI(TAG, "Digipeater status beacon configured (en=%d interval=%us)", g_config.digi_en, (unsigned)g_config.digi_sts_interval);
}
