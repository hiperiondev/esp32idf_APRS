// @file beacon.c
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
// @brief Own-station position and status beacon tasks: builds APRS position
// reports from the saved Tracker/IGate/Digipeater coordinates, resolves the
// configured path bitmask into a digipeater path, and transmits them on RF
// and/or APRS-IS at each beacon's own interval. Also builds and transmits
// each station's APRS status report (DTI '>', APRS101 ch.16) from that page's
// own status text, at its own independent interval.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "aprs_coord.h"
#include "aprs_dao.h"  // aprs_dao_build()
#include "aprs_path.h" // aprs_path_build_suffix()
#include "aprs_service.h"
#include "beacon.h"
#include "beacon_scheduler.h" // beacon_scheduler_jitter()
#include "igate.h"
#include "objects_items.h"     // objitem_build_freq_block()
#include "sched_time.h"        // sched_mono_seconds() / sched_clamp_interval()
#include "str_append.h"        // str_append()
#include "weather_telemetry.h" // aprs_mice_encode()

static const char *TAG = "beacon";

// Same software-identifier destination call used by the message component
// (components/message/message.c) for consistency across the firmware.
#define BEACON_DEST APRS_TOCALL

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
    // Position ambiguity (APRS101 ch.6), 0 = full precision through 4 =
    // nearest degree. Station-wide (g_config.pos_ambiguity), so all three
    // beacons carry the same precision. A non-zero level forces the
    // uncompressed layout, which is the only one with digits to blank.
    uint8_t ambiguity;
    // APRS Data Extension (APRS101 ch.7), only used by the IGate beacon so
    // far (see igateBeaconService()). Not settable for the Tracker/Digipeater
    // beacons, so extEnable stays false and the slot is never emitted for
    // those. All three extension types share the same 7-byte info-field slot
    // that moving stations use for CSE/SPD - mutually exclusive with
    // movement, which is fine here since these are fixed-position beacons.
    bool extEnable;
    uint8_t extType;     // aprs_ext_type_t: PHG, RNG or DFS
    uint16_t phgPower;   // Watts (PHG)
    float phgGain;       // dB (PHG and DFS)
    uint16_t phgHeight;  // feet (PHG and DFS; the APRS code table's own unit)
    uint8_t phgDir;      // 0=Omni, 1-8 = N,NE,E,SE,S,SW,W,NW (PHG and DFS)
    uint16_t rangeMiles; // statute miles (RNG)
    uint8_t dfsStrength; // 0-9 S-points (DFS)
    // When set, buildPositionPacket() emits a Mic-E position report
    // (APRS101 ch.10) instead of the uncompressed/compressed layout above,
    // via aprs_mice_encode() (components/weather_telemetry/mice.c). Mic-E
    // has its own fixed on-air layout: no timestamp, no data extension, and
    // no compression, so this takes priority over p->timestamp/extEnable/
    // compress when set - see the mutual-exclusion note in
    // buildPositionPacket() itself. Sourced from g_config.trk_mice; only
    // the Tracker beacon exposes it (Mic-E is the mobile-tracker format
    // this project's other two fixed beacons have no need to originate).
    bool mice;
    // Whether this station accepts APRS messages, which the position report's
    // own data type identifier has to state: '!' and '/' mean it does not,
    // '=' and '@' mean it does (APRS101 ch.6). Receiving clients read that bit
    // to decide whether to offer their operator a reply path, so it is not
    // decorative. Sourced from g_config.msg_enable by each per-service
    // function below, in the same snapshot as every other field here.
    bool msgCapable;
    // When set, the WGS-84 human-readable "!DAO!" precision/datum extension
    // (aprs_dao_build(), aprs12/datum.txt) closes the comment - in the
    // uncompressed layout and in Mic-E alike, where the spec asks for it at
    // the end of the text field too. Sourced from the station-wide
    // g_config.pos_dao_en, like ambiguity above, and only actually applied
    // when ambiguity == 0 and the layout is not the compressed one: a station
    // deliberately obscuring its position, or already sending
    // full-resolution compressed coordinates, must not have that precision
    // handed back through this extension.
    bool daoEnable;
    // Recommended travelers' voice repeater this station advertises
    // (freqspec.txt), built into the standard "FFF.FFFMHz Tnnn +/-nnn" block
    // by objitem_build_freq_block() and prepended to the comment as its first
    // 10 bytes - the fixed-field form some radios (e.g. Yaesu) require to
    // auto-tune, since they do not decode the frequency Object form. The same
    // block leads the Mic-E text field, where the spec reserves the same
    // first-in-the-text position for it. Sourced from each service's own
    // *_freq_mhz/_tone_tenths/_duplex/_offset_khz fields; freqMhz <= 0 means
    // no block is emitted.
    float freqMhz;
    uint16_t freqToneTenths;
    int8_t freqDuplex;
    uint16_t freqOffsetKhz;
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

// Encodes the antenna height, gain and directivity sub-fields shared by the
// "PHGphgd" and "DFSshgd" data extensions into their single-character codes.
// Height is the APRS code table's 10 * 2^h feet, gain is the dB value itself,
// and directivity is 0 (omni) through 8. `out` must be >= 4 bytes; it receives
// the three characters and a NUL.
static void buildHgdCodes(float gain, uint16_t height, uint8_t dir, char *out, size_t outMax) {
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

    snprintf(out, outMax, "%c%c%c", '0' + H, '0' + G, '0' + D);
}

// Builds the 7-byte "RNGrrrr" Pre-Calculated Radio Range data extension
// (APRS101 ch.7): four decimal digits of omnidirectional range in statute
// miles, zero-padded. `out` must be >= 8 bytes. A range of 0 is a legal
// on-air value ("RNG0000"); the caller decides whether an unset range is
// worth transmitting.
static void buildRangeExtension(uint16_t rangeMiles, char *out, size_t outMax) {
    unsigned r = rangeMiles;
    if (r > APRS_EXT_RANGE_MILES_MAX)
        r = APRS_EXT_RANGE_MILES_MAX;
    snprintf(out, outMax, "RNG%04u", r);
}

// Builds the 7-byte "DFSshgd" Omni-DF Signal Strength data extension
// (APRS101 ch.7). It has the same shape as PHG with the transmitter-power
// digit replaced by a received signal strength in S-points, where 0 means
// this station does NOT hear the signal at all - which receiving software
// draws as an exclusion circle rather than a coverage circle. `out` must be
// >= 8 bytes.
static void buildDfsExtension(uint8_t strength, float gain, uint16_t height, uint8_t dir, char *out, size_t outMax) {
    int S = (int)strength;
    if (S < 0)
        S = 0;
    if (S > APRS_EXT_DFS_STRENGTH_MAX)
        S = APRS_EXT_DFS_STRENGTH_MAX;

    char hgd[4];
    buildHgdCodes(gain, height, dir, hgd, sizeof(hgd));
    snprintf(out, outMax, "DFS%c%s", '0' + S, hgd);
}

// Selects and builds the beacon's data extension into `out` (>= 8 bytes),
// which is left as an empty string when no extension is enabled. Exactly one
// extension is ever emitted: they all occupy the same 7-byte slot after the
// symbol code.
static void buildDataExtension(const beacon_params_t *p, char *out, size_t outMax) {
    out[0] = 0;
    if (!p->extEnable)
        return;

    switch ((aprs_ext_type_t)p->extType) {
        case APRS_EXT_RNG:
            buildRangeExtension(p->rangeMiles, out, outMax);
            return;
        case APRS_EXT_DFS:
            buildDfsExtension(p->dfsStrength, p->phgGain, p->phgHeight, p->phgDir, out, outMax);
            return;
        case APRS_EXT_PHG:
        default:
            buildPhgExtension(p->phgPower, p->phgGain, p->phgHeight, p->phgDir, out, outMax);
            return;
    }
}

// Builds a Mic-E-format beacon line (APRS101 ch.10) into `out`, in place of
// the uncompressed/compressed layout buildPositionPacket() otherwise emits.
// Mic-E splits its payload between the AX.25 destination address (encoded
// here via aprs_mice_encode(), which returns 6 raw bytes - not necessarily
// printable ASCII) and the information field, so this builds the "call>dst"
// portion of the TNC2 line itself instead of reusing BEACON_DEST. This
// project's beacons are fixed-position with no live course/speed source
// (see docs/reference/limitations.rst), so the report always carries the
// on-air "unknown" course/speed pattern (000/000); the message code is
// fixed at Off Duty (M0), the conventional default for a fixed/base station.
//
// Mic-E has one free-text slot, and aprs12/mic-e-examples.txt fixes the order
// of what may go in it: the frequency block first (radios auto-tune from the
// leading bytes and stop looking after them), then the operator's comment,
// then "!DAO!" last. The altitude field and the Manufacturer/Version pair sit
// outside that text, before and after it respectively, and are added by
// aprs_mice_encode() itself.
//
// Returns the packet length, or 0 if nothing usable is configured or the
// position/line does not fit the Mic-E format.
static int buildMicePositionPacket(const beacon_params_t *p, char *out, size_t outMax) {
    if (!p->call[0])
        return 0;

    char callField[16];
    if (p->ssid > 0)
        snprintf(callField, sizeof(callField), "%s-%d", p->call, (int)p->ssid);
    else
        snprintf(callField, sizeof(callField), "%s", p->call);

    char path[80];
    aprs_path_build_suffix(p->pathSel, p->pathPreset, path, sizeof(path));

    aprs_mice_report_t report;
    memset(&report, 0, sizeof(report));
    report.position.latitude_deg = (double)p->lat;
    report.position.longitude_deg = (double)p->lon;
    // Mic-E carries position ambiguity natively (APRS101 ch.10), by blanking
    // the same least significant digits the uncompressed format spaces out,
    // so the station-wide setting applies here unchanged.
    report.position.ambiguity = (aprs_position_ambiguity_t)p->ambiguity;
    report.position.symbol.table = (p->symbol[0] == '\\') ? APRS_SYMBOL_TABLE_ALTERNATE : APRS_SYMBOL_TABLE_PRIMARY;
    report.position.symbol.code = p->symbol[1] ? p->symbol[1] : '>';
    report.message_code = APRS_MICE_MSG_OFF_DUTY;
    report.course_speed.is_unknown = true;
    // Messaging capability, taken from the same flag that picks '='/'@' over
    // '!'/'/' in buildPositionPacket(), so the two layouts state the same
    // thing about this station whichever one the operator selects.
    report.msg_capable = p->msgCapable;
    report.device_id[0] = APRS_MICE_DEVICE_ID[0];
    report.device_id[1] = APRS_MICE_DEVICE_ID[1];

    if (p->sendAltitude) {
        int feet = (int)(p->alt * 3.28084f);
        if (feet < 0)
            feet = 0;
        report.position.has_altitude = true;
        report.position.altitude_ft = feet;
    }

    // !DAO! precision/datum extension (aprs12/datum.txt). Mic-E blanks
    // destination-address digits to express position ambiguity exactly as the
    // uncompressed layout blanks minute digits, so the same rule applies: a
    // station that asked for a coarser report must not have the precision it
    // hid handed straight back through this extension. Built first because it
    // has to survive to the end of the text field, so its bytes are reserved
    // before the comment is allowed to fill the rest.
    char dao[APRS_DAO_BUF_SIZE] = { 0 };
    if (p->daoEnable && p->ambiguity == 0)
        aprs_dao_build(p->lat, p->lon, dao);

    char freqBlock[24];
    objitem_build_freq_block(p->freqMhz, p->freqToneTenths, p->freqDuplex, p->freqOffsetKhz, freqBlock, sizeof(freqBlock));

    size_t daoLen = strlen(dao);
    size_t textMax = sizeof(report.status_text);
    size_t bodyMax = (textMax > daoLen) ? textMax - daoLen : 1;
    size_t used = 0;
    if (freqBlock[0])
        str_append(report.status_text, bodyMax, &used, "%s ", freqBlock);
    if (p->comment[0])
        str_append(report.status_text, bodyMax, &used, "%s", p->comment);
    if (daoLen > 0)
        str_append(report.status_text, textMax, &used, "%s", dao);
    report.has_status_text = (report.status_text[0] != 0);

    char dstCall[8];
    char infoField[APRS_MICE_INFO_BUF_SIZE];
    if (!aprs_mice_encode(&report, dstCall, infoField, sizeof(infoField))) {
        ESP_LOGW(TAG, "Mic-E beacon not built - position out of range for Mic-E encoding");
        return 0;
    }

    int n = snprintf(out, outMax, "%s>%s%s:%s", callField, dstCall, path, infoField);
    if (n < 0)
        return 0;
    if ((size_t)n >= outMax || n > APRS_TNC2_MAX_LEN) {
        ESP_LOGW(TAG, "Mic-E beacon packet too long (%d bytes, max %d) - shorten the comment or the path", n, APRS_TNC2_MAX_LEN);
        return 0;
    }
    return n;
}

// Builds the full TNC2 text line for one beacon transmission. Returns the
// packet length, or 0 if nothing usable is configured.
static int buildPositionPacket(const beacon_params_t *p, char *out, size_t outMax) {
    if (!p->call[0])
        return 0;

    // Mic-E has its own fixed on-air layout (no timestamp, no data extension,
    // no compression - see the mice field's own comment in beacon_params_t),
    // so it is handled by a dedicated builder and takes priority over every
    // other layout choice below.
    if (p->mice)
        return buildMicePositionPacket(p, out, outMax);

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

    // Data-extension token (PHG / RNG / DFS), when enabled. Emitted right
    // after the symbol code and before the altitude/comment, matching the
    // Object/Item info field layout (main/objects_items.c) and the order the
    // APRS spec defines for this slot.
    char ext[8] = { 0 };
    buildDataExtension(p, ext, sizeof(ext));

    // Two things force the uncompressed layout:
    //
    //   * A data extension. The compressed format has no room for the 7-byte
    //     slot (APRS101 ch.9 states it does not support PHG); emitting those
    //     bytes inside a compressed report would just be wrong data, and
    //     dropping the extension silently to keep compression would lose a
    //     field the operator explicitly enabled.
    //   * Position ambiguity. Ambiguity is expressed by blanking decimal
    //     digits, and the compressed format has no decimal digits to blank -
    //     a compressed report always states a position to full resolution, so
    //     honouring the compress flag here would transmit the exact position
    //     the operator asked to obscure.
    //
    // None of these three fixed-position beacons track course/speed, so when
    // compression is used the cs/T slot always carries "no cs/T data"
    // (3 spaces).
    bool useCompressed = p->compress && !p->extEnable && p->ambiguity == 0;

    // Sized for the larger of the two layouts: uncompressed is up to 21
    // bytes (9-char latStr content + symTable + 10-char lonStr content +
    // symCode), compressed is a fixed 13 bytes (symTable + 4 lat + 4 lon +
    // symCode + 3 cs/T), plus NUL either way.
    char posField[22];
    if (useCompressed) {
        aprs_coord_format_compressed(p->lat, p->lon, symTable, symCode, "   ", posField, sizeof(posField));
    } else {
        char latStr[10], lonStr[11];
        aprs_coord_format_ambiguous(p->lat, p->lon, p->ambiguity, latStr, sizeof(latStr), lonStr, sizeof(lonStr));
        snprintf(posField, sizeof(posField), "%s%c%s%c", latStr, symTable, lonStr, symCode);
    }

    char extra[40] = { 0 };
    if (p->sendAltitude) {
        int feet = (int)(p->alt * 3.28084f);
        if (feet < 0)
            feet = 0;
        snprintf(extra, sizeof(extra), "/A=%06d", feet);
    }

    // Frequency block (freqspec.txt): built once here and prepended to the
    // comment as its first bytes, ahead of anything the operator typed. For a
    // 3-digit-MHz amateur repeater frequency (all VHF/UHF ham bands) the
    // "FFF.FFFMHz" token itself is exactly 10 bytes, satisfying the spec's
    // fixed-field requirement for radios (e.g. Yaesu) that only look at the
    // first 10 bytes of the comment and do not decode the frequency Object
    // form. A single space separates it from the rest of the comment. Comment
    // text that would push the combined field past COMMENT_SIZE - 1 is
    // truncated, same as an over-long operator-entered comment always was.
    char comment[COMMENT_SIZE];
    {
        char freqBlock[24];
        objitem_build_freq_block(p->freqMhz, p->freqToneTenths, p->freqDuplex, p->freqOffsetKhz, freqBlock, sizeof(freqBlock));
        if (freqBlock[0]) {
            // Precision on %s bounds the operator-entered part explicitly to
            // whatever room is left after freqBlock and the separating space,
            // so the combined write is provably within sizeof(comment) - the
            // same truncation the plain "%s %s" form already produced, just
            // in a shape the compiler can verify statically.
            int room = (int)sizeof(comment) - (int)strlen(freqBlock) - 1;
            if (room < 0)
                room = 0;
            snprintf(comment, sizeof(comment), "%s %.*s", freqBlock, room, p->comment);
        } else {
            snprintf(comment, sizeof(comment), "%.*s", (int)sizeof(comment) - 1, p->comment);
        }
    }

    // !DAO! precision/datum extension (aprs12/datum.txt), appended after the
    // comment. Only meaningful, and only applied, alongside the uncompressed
    // layout at full precision: the compressed format already carries full
    // resolution, and a station that asked for a coarser-than-full-precision
    // report (ambiguity > 0) must not have that precision handed straight
    // back through this extension.
    char dao[APRS_DAO_BUF_SIZE] = { 0 };
    if (p->daoEnable && p->ambiguity == 0 && !useCompressed)
        aprs_dao_build(p->lat, p->lon, dao);

    // The data type identifier encodes both whether a timestamp follows and
    // whether this station can accept APRS messages (APRS101 ch.6):
    //
    //   no timestamp, no messaging  '!'      no timestamp, messaging  '='
    //   timestamp,    no messaging  '/'      timestamp,    messaging  '@'
    //
    // This station runs a full messaging engine, answers directed queries and
    // acknowledges what it receives, so with messaging on it says so - a
    // beacon that claims otherwise is displayed by Kenwood radios, APRSISCE/32,
    // Xastir, YAAC and aprs.fi alike as a station nobody can reply to.
    char infoField[256]; // ts(7)+posField(up to 21)+ext(7)+extra(40)+comment(up to 128)+dao(5)+NUL
    if (p->timestamp) {
        time_t now = time(NULL);
        struct tm tmv;
        gmtime_r(&now, &tmv);
        char ts[8];
        snprintf(ts, sizeof(ts), "%02d%02d%02dz", tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
        snprintf(infoField, sizeof(infoField), "%c%s%s%s%s%s%s", p->msgCapable ? '@' : '/', ts, posField, ext, extra, comment, dao);
    } else {
        snprintf(infoField, sizeof(infoField), "%c%s%s%s%s%s", p->msgCapable ? '=' : '!', posField, ext, extra, comment, dao);
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
    // Maidenhead grid locator prefix (APRS101 ch.16), station-wide
    // (g_config.status_grid_en). The locator is derived from this beacon's own
    // configured position and carries this beacon's own symbol, so the three
    // status reports stay individually addressable even though the option is
    // set once.
    bool gridEnable;
    float lat;
    float lon;
    char symbol[3]; // table + code + NUL
    // Same frequency block as beacon_params_t.freqMhz and friends, prepended
    // to the status text instead of a position comment - the second
    // advertisement freqspec.txt explicitly endorses, for radios that decode
    // neither the frequency Object form nor the leading bytes of a position
    // report's comment. freqMhz <= 0 means no block is emitted.
    float freqMhz;
    uint16_t freqToneTenths;
    int8_t freqDuplex;
    uint16_t freqOffsetKhz;
} status_params_t;

// Builds the full TNC2 text line for one status-report transmission. The
// info field is DTI '>' followed by the free-text status (APRS101 ch.16).
//
// After the '>', up to three optional blocks precede the operator's own
// status text, in the fixed order the spec and this project's conventions
// put them in:
//
//   1. The optional "DDHHMMz" zulu timestamp (APRS101 ch.16), immediately
//      after '>', when g_config.status_timestamp_en is set.
//   2. The "FFF.FFFMHz Tnnn +/-nnn" frequency block (freqspec.txt), when this
//      beacon has a monitor frequency configured - the second advertisement
//      the spec endorses, for radios that decode neither the frequency
//      Object form nor the leading bytes of a position report's comment.
//   3. With the Maidenhead option on, the 6-char grid locator of this
//      beacon's position, the symbol table byte and the symbol code - the
//      ">IO91SX/G" form the spec defines.
//
// Each present block is followed by a single space separating it from what
// comes next. Receivers that understand a given form read it out on its own;
// the rest simply show the whole thing as status text. The configured status
// text itself is never interpreted: whatever the operator typed is carried
// verbatim at the end.
//
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

    char freqBlock[24];
    objitem_build_freq_block(p->freqMhz, p->freqToneTenths, p->freqDuplex, p->freqOffsetKhz, freqBlock, sizeof(freqBlock));

    char ts[8] = { 0 };
    if (g_config.status_timestamp_en) {
        time_t now = time(NULL);
        struct tm tmv;
        gmtime_r(&now, &tmv);
        snprintf(ts, sizeof(ts), "%02d%02d%02dz", tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    }

    // '>' + timestamp (7) + freq block (up to 23, space-separated) + optional
    // locator block ("IO91SX" + symbol table + symbol code + separating
    // space = 9) + status text + NUL. Built with str_append() so a would-be
    // negative/oversize snprintf() result from any one piece saturates the
    // buffer instead of underflowing the running offset.
    char infoField[STATUS_SIZE + 40];
    size_t used = 0;
    str_append(infoField, sizeof(infoField), &used, ">%s", ts);
    if (freqBlock[0])
        str_append(infoField, sizeof(infoField), &used, "%s ", freqBlock);
    if (p->gridEnable) {
        char grid[APRS_MAIDENHEAD_BUF_SIZE];
        aprs_maidenhead_locator(p->lat, p->lon, grid, sizeof(grid));
        char symTable = p->symbol[0] ? p->symbol[0] : '/';
        char symCode = p->symbol[1] ? p->symbol[1] : '>';
        str_append(infoField, sizeof(infoField), &used, "%s%c%c ", grid, symTable, symCode);
    }
    str_append(infoField, sizeof(infoField), &used, "%s", p->statusText);

    int n = snprintf(out, outMax, "%s>%s%s:%s", callField, BEACON_DEST, path, infoField);
    // A status line is at most 201 bytes: call field (up to 15) + '>' +
    // destination (6) + path (up to 79) + ':' + info field (up to 99, i.e.
    // the 50-byte '>'-plus-text form plus the 7-byte timestamp, the up to
    // 23-byte frequency block and the 9-byte Maidenhead locator block), so it
    // always fits the APRS_TNC2_BUF_SIZE buffer every caller provides. The
    // check is kept anyway, on the same terms as buildPositionPacket():
    // refuse rather than clamp, so neither leg ever carries a truncated - and
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
// position beacon and sent over the same RF/INET legs (_loc2rf, _loc2inet)
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
            p.gridEnable = g_config.status_grid_en;
            p.lat = g_config.trk_lat;
            p.lon = g_config.trk_lon;
            memcpy(p.symbol, g_config.trk_symbol, sizeof(p.symbol));
            p.freqMhz = g_config.trk_freq_mhz;
            p.freqToneTenths = g_config.trk_tone_tenths;
            p.freqDuplex = g_config.trk_duplex;
            p.freqOffsetKhz = g_config.trk_offset_khz;
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
            p.gridEnable = g_config.status_grid_en;
            p.lat = g_config.igate_lat;
            p.lon = g_config.igate_lon;
            memcpy(p.symbol, g_config.igate_symbol, sizeof(p.symbol));
            p.freqMhz = g_config.igate_freq_mhz;
            p.freqToneTenths = g_config.igate_tone_tenths;
            p.freqDuplex = g_config.igate_duplex;
            p.freqOffsetKhz = g_config.igate_offset_khz;
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
            p.gridEnable = g_config.status_grid_en;
            p.lat = g_config.digi_lat;
            p.lon = g_config.digi_lon;
            memcpy(p.symbol, g_config.digi_symbol, sizeof(p.symbol));
            p.freqMhz = g_config.digi_freq_mhz;
            p.freqToneTenths = g_config.digi_tone_tenths;
            p.freqDuplex = g_config.digi_duplex;
            p.freqOffsetKhz = g_config.digi_offset_khz;
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
        beacon_params_t p = { 0 }; // zero-init: Tracker carries no data extension, so extEnable must default false
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
            p.mice = g_config.trk_mice;
            p.msgCapable = g_config.msg_enable;
            p.ambiguity = g_config.pos_ambiguity;
            p.daoEnable = g_config.pos_dao_en;
            memcpy(p.symbol, g_config.trk_symbol, sizeof(p.symbol));
            memcpy(p.comment, g_config.trk_comment, sizeof(p.comment));
            memcpy(p.pathPreset, g_config.path, sizeof(p.pathPreset));
            p.freqMhz = g_config.trk_freq_mhz;
            p.freqToneTenths = g_config.trk_tone_tenths;
            p.freqDuplex = g_config.trk_duplex;
            p.freqOffsetKhz = g_config.trk_offset_khz;
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
            p.msgCapable = g_config.msg_enable;
            p.ambiguity = g_config.pos_ambiguity;
            p.daoEnable = g_config.pos_dao_en;
            memcpy(p.symbol, g_config.igate_symbol, sizeof(p.symbol));
            memcpy(p.comment, g_config.igate_comment, sizeof(p.comment));
            memcpy(p.pathPreset, g_config.path, sizeof(p.pathPreset));
            p.extEnable = g_config.igate_phg_enable;
            p.extType = g_config.igate_ext_type;
            p.phgPower = g_config.igate_phg_power;
            p.phgGain = g_config.igate_phg_gain;
            p.phgHeight = g_config.igate_phg_height;
            p.phgDir = g_config.igate_phg_dir;
            p.rangeMiles = g_config.igate_range_miles;
            p.dfsStrength = g_config.igate_dfs_strength;
            p.freqMhz = g_config.igate_freq_mhz;
            p.freqToneTenths = g_config.igate_tone_tenths;
            p.freqDuplex = g_config.igate_duplex;
            p.freqOffsetKhz = g_config.igate_offset_khz;
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

// Fills a beacon_params_t from g_config.igate_* under app_config_lock(), the
// same snapshot igateBeaconService() takes, so both the periodic IGate
// beacon and any on-demand caller (currently the query responder's "?APRS?"
// reply) build byte-for-byte the same packet from the same source fields.
static void fillIgatePositionParams(beacon_params_t *p) {
    memset(p, 0, sizeof(*p));
    app_config_lock();
    {
        memcpy(p->call, g_config.aprs_mycall, sizeof(p->call));
        p->ssid = g_config.aprs_ssid;
        p->pathSel = g_config.igate_path;
        p->timestamp = g_config.igate_timestamp;
        p->lat = g_config.igate_lat;
        p->lon = g_config.igate_lon;
        p->alt = g_config.igate_alt;
        p->sendAltitude = g_config.igate_alt != 0.0f;
        p->compress = g_config.igate_compress;
        p->msgCapable = g_config.msg_enable;
        memcpy(p->symbol, g_config.igate_symbol, sizeof(p->symbol));
        memcpy(p->comment, g_config.igate_comment, sizeof(p->comment));
        memcpy(p->pathPreset, g_config.path, sizeof(p->pathPreset));
        p->extEnable = g_config.igate_phg_enable;
        p->extType = g_config.igate_ext_type;
        p->phgPower = g_config.igate_phg_power;
        p->phgGain = g_config.igate_phg_gain;
        p->phgHeight = g_config.igate_phg_height;
        p->phgDir = g_config.igate_phg_dir;
        p->rangeMiles = g_config.igate_range_miles;
        p->dfsStrength = g_config.igate_dfs_strength;
        p->ambiguity = g_config.pos_ambiguity;
        p->daoEnable = g_config.pos_dao_en;
        p->freqMhz = g_config.igate_freq_mhz;
        p->freqToneTenths = g_config.igate_tone_tenths;
        p->freqDuplex = g_config.igate_duplex;
        p->freqOffsetKhz = g_config.igate_offset_khz;
    }
    app_config_unlock();
}

int beacon_build_igate_position_packet(char *out, size_t out_max) {
    beacon_params_t p;
    fillIgatePositionParams(&p);
    return buildPositionPacket(&p, out, out_max);
}

int beacon_build_igate_status_packet(char *out, size_t out_max) {
    // Same snapshot igateStatusService() takes, so the on-demand copy and the
    // periodic status beacon are byte-for-byte identical.
    status_params_t p = { 0 };
    app_config_lock();
    {
        memcpy(p.call, g_config.aprs_mycall, sizeof(p.call));
        p.ssid = g_config.aprs_ssid;
        p.pathSel = g_config.igate_path;
        memcpy(p.statusText, g_config.igate_status, sizeof(p.statusText));
        memcpy(p.pathPreset, g_config.path, sizeof(p.pathPreset));
        p.gridEnable = g_config.status_grid_en;
        p.lat = g_config.igate_lat;
        p.lon = g_config.igate_lon;
        memcpy(p.symbol, g_config.igate_symbol, sizeof(p.symbol));
        p.freqMhz = g_config.igate_freq_mhz;
        p.freqToneTenths = g_config.igate_tone_tenths;
        p.freqDuplex = g_config.igate_duplex;
        p.freqOffsetKhz = g_config.igate_offset_khz;
    }
    app_config_unlock();
    return buildStatusPacket(&p, out, out_max);
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
        beacon_params_t p = { 0 }; // zero-init: Digipeater carries no data extension, so extEnable must default false
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
            p.msgCapable = g_config.msg_enable;
            p.ambiguity = g_config.pos_ambiguity;
            p.daoEnable = g_config.pos_dao_en;
            memcpy(p.symbol, g_config.digi_symbol, sizeof(p.symbol));
            memcpy(p.comment, g_config.digi_comment, sizeof(p.comment));
            memcpy(p.pathPreset, g_config.path, sizeof(p.pathPreset));
            p.freqMhz = g_config.digi_freq_mhz;
            p.freqToneTenths = g_config.digi_tone_tenths;
            p.freqDuplex = g_config.digi_duplex;
            p.freqOffsetKhz = g_config.digi_offset_khz;
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
