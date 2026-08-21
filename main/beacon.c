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
#include "aprs_dao.h"       // aprs_dao_build()
#include "aprs_df.h"        // aprs_df_build_extension(), aprs_df_symbol_matches(), APRS_DF_EXT_BUF_SIZE
#include "aprs_free_text.h" // aprs_free_text_build()
#include "aprs_path.h"      // aprs_path_build_suffix()
#include "aprs_service.h"
#include "beacon.h"
#include "beacon_scheduler.h" // beacon_scheduler_jitter()
#include "gps.h"              // gps_snapshot(), gps_data_t
#include "igate.h"
#include "objects_items.h"     // objitem_build_freq_block()
#include "sched_time.h"        // sched_mono_seconds() / sched_clamp_interval()
#include "str_append.h"        // str_append(), str_copy_utf8_safe()
#include "telemetry.h"         // telemetry_build_comment_tlm() / telemetry_config_load()
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
    // Course/speed for the standard "CSE/SPD" data-extension slot (APRS101
    // ch.6/ch.9), the same slot buildDataExtension() fills with PHG/RNG/DFS/DF
    // when hasCourseSpeed is not set. Populated only by the Tracker beacon,
    // when it is transmitting a live GNSS fix
    // (g_config.trk_use_live_gps + a current gps_snapshot() fix) rather than
    // its fixed position; the fixed-position beacons this project also builds
    // (IGate, Digipeater) have no course/speed source and never set this.
    // Course/speed and a PHG/RNG/DFS/DF extension cannot share the slot at
    // once - see the priority note on buildDataExtension() - so a live fix
    // reported while trk_phg_enable is also on gives way to PHG, the same way
    // objects_items.c gives way to PHG over CSE/SPD for a stationary element.
    bool hasCourseSpeed;
    uint16_t courseDeg;  // 0..359, true course over ground
    uint16_t speedKnots; // Speed over ground, knots
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
    // APRS Data Extension (APRS101 ch.7). The IGate and Digipeater beacons
    // each select any of the types with their own sub-fields (see
    // igateBeaconService() and digiBeaconService()); the Tracker beacon offers
    // PHG alone, taken from the station-wide antenna data. All extension types share
    // the same 7-byte info-field slot that moving stations use for CSE/SPD -
    // mutually exclusive with hasCourseSpeed above, since only the Tracker
    // beacon can carry either and the two describe the same bytes. IGate and
    // Digipeater never set hasCourseSpeed, so the slot is free for their own
    // extension exactly as before. Mic-E has no such slot, and carries the
    // token in its text field instead (see buildMicePositionPacket()).
    bool extEnable;
    uint8_t extType;     // aprs_ext_type_t: PHG, RNG, DFS or DF
    uint16_t phgPower;   // Watts (PHG)
    float phgGain;       // dB (PHG and DFS)
    uint16_t phgHeight;  // feet (PHG and DFS; the APRS code table's own unit)
    uint8_t phgDir;      // 0=Omni, 1-8 = N,NE,E,SE,S,SW,W,NW (PHG and DFS)
    uint16_t rangeMiles; // statute miles (RNG)
    uint8_t dfsStrength; // 0-9 S-points (DFS)
    // Bearing and NRQ triplet of a DF report (APRS_EXT_DF). Carried in the
    // modelled type the receive side uses for the same extension, so the
    // firmware describes the field in one place.
    aprs_bearing_nrq_t df;
    // This beacon's own effective transmit interval, in seconds, used only to
    // compute the PHGR "probes" beacon-rate character (aprs.org/aprs12/probes.txt)
    // appended to a PHG extension: 0 leaves the rate off and emits the plain
    // 7-byte "PHGphgd" form, since a receiving station has nothing meaningful
    // to divide by. Populated from the same sched_clamp_interval() value the
    // scheduler itself transmits at, so the rate character always matches the
    // cadence actually observed on air.
    uint32_t beaconIntervalSec;
    // When set, buildPositionPacket() emits a Mic-E position report
    // (APRS101 ch.10) instead of the uncompressed/compressed layout above,
    // via aprs_mice_encode() (components/weather_telemetry/mice.c). Mic-E
    // has its own fixed on-air layout: no timestamp and no compression, so
    // this takes priority over p->timestamp/compress when set - see the
    // mutual-exclusion note in buildPositionPacket() itself. The data
    // extension is the one field it keeps, inside its text. Sourced from g_config.trk_mice; only
    // the Tracker beacon exposes it (Mic-E is the mobile-tracker format
    // this project's other two fixed beacons have no need to originate).
    bool mice;
    // Mic-E position comment (APRS101 ch.10 "Mic-E Message Types"), in the
    // packed 0-13 form ::MICE_POS_COMMENT_MAX documents: 0-6 select the
    // Standard values M0-M6, 7-13 the Custom values C0-C6. Only meaningful
    // when mice is set, since no other layout has a field for it. Sourced
    // from g_config.trk_mice_msg, alongside the Mic-E flag itself.
    uint8_t miceMsg;
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
    // (freqspec.txt), built into the standard frequency block by
    // objitem_build_freq_block() and prepended to the comment as its first
    // 10 bytes - the fixed-field form some radios (e.g. Yaesu) require to
    // auto-tune, since they do not decode the frequency Object form. The same
    // block leads the Mic-E text field, where the spec reserves the same
    // first-in-the-text position for it. Sourced from each service's own
    // *_freq_mhz/_tone_tenths/_duplex/_offset_khz fields; freqMhz <= 0 means
    // no block is emitted. There is no per-service coverage-range setting, so
    // the block's optional range sub-field is never emitted here (only
    // Objects/Items carry one, via objitem_t's own range/range_km fields).
    float freqMhz;
    uint16_t freqToneTenths;
    int8_t freqDuplex;
    uint16_t freqOffsetKhz;
    // The APRS 1.2 base-91 comment telemetry group ("|ss1122|",
    // telemetry_build_comment_tlm()), filled in by appendCommentTelemetry()
    // once this beacon's other fields are set, or left empty when comment
    // telemetry does not apply to this beacon/callsign. Kept separate from
    // comment rather than concatenated onto it, so buildPositionPacket() and
    // buildMicePositionPacket() can reserve room for it - the same way they
    // already reserve room for the trailing !DAO! extension - and truncate
    // only the operator's free-form comment text if the info field would
    // otherwise overflow. Sized from telemetry.h's own worst-case figure for
    // the group, so the two never drift apart.
    char cmtTlm[TLM_COMMENT_GROUP_BUF_SIZE];
} beacon_params_t;

// Encodes a beacon's own transmit interval into the PHGR "probes" rate
// character (aprs.org/aprs12/probes.txt): '0'-'9' for 0-9 beacons per hour,
// then 'A' upward for 10 and above. A cadence that computes to more than
// APRS_EXT_PHG_RATE_MAX beacons/hour is clamped to that ceiling - documented
// on the constant itself - rather than silently wrapped into an unrelated
// printable byte.
static char phgRateChar(uint32_t intervalSec) {
    uint32_t perHour = (3600u + intervalSec / 2u) / intervalSec; // rounded to the nearest whole beacon/hour
    if (perHour > APRS_EXT_PHG_RATE_MAX)
        perHour = APRS_EXT_PHG_RATE_MAX;
    return (perHour <= 9) ? (char)('0' + perHour) : (char)('A' + (perHour - 10));
}

// Builds the "PHGphgd" data-extension token from PHG sub-fields, using the
// same rounding/clamping and single-character-per-digit encoding as the
// Station/Objects web pages (see objitem_build_phg() in objects_items.c and
// the calcStationPHG() JS on the Station page). When intervalSec is nonzero,
// the PHGR "probes" beacon-rate character and its mandatory trailing slash
// (1.2 addition, aprs.org/aprs12/probes.txt) are appended right after the
// standard four digits, so a station tracking this beacon's reception can
// compute how reliably it is heard; intervalSec == 0 emits the plain 7-byte
// form instead. `out` must be >= 10 bytes.
static void buildPhgExtension(uint16_t power, float gain, uint16_t height, uint8_t dir, uint32_t intervalSec, char *out, size_t outMax) {
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
    if (D > 8)
        D = 8;

    if (intervalSec > 0)
        snprintf(out, outMax, "PHG%c%c%c%c%c/", '0' + P, '0' + H, '0' + G, '0' + D, phgRateChar(intervalSec));
    else
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
    if (D > 8)
        D = 8;

    snprintf(out, outMax, "%c%c%c", '0' + H, '0' + G, '0' + D);
}

// Case-insensitive exact-length compare of two NUL-terminated base callsigns,
// same rule aprs_service.c's base_call_equals() applies to APRS-IS echo
// detection: callsigns are conventionally uppercase on air, but the two
// config stores compared here (g_config.*_mycall and telemetry.json's
// mycall) are each entered independently, so a stray-case entry in either
// must not silently defeat the match below.
static bool call_equals_ci(const char *a, const char *b) {
    if (!a || !b)
        return false;
    size_t i = 0;
    for (; a[i] && b[i]; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'a' && ca <= 'z')
            ca -= 32;
        if (cb >= 'a' && cb <= 'z')
            cb -= 32;
        if (ca != cb)
            return false;
    }
    return a[i] == b[i]; // both NUL at the same position
}

// Fills dst with the on-air form of one of this station's free-text fields:
// reserved characters removed, and the APRS-IS no-archive marker prefixed
// when the station-wide privacy setting asks for it. The whole policy lives
// in aprs_free_text_build(), which every originating service shares, so a
// beacon comment, a weather comment, an object comment and a bulletin are all
// assembled by the same code.
//
// Must be called with app_config_lock() already held: g_config.my_no_archive
// is read here rather than snapshotted, the same way the callers read the
// g_config text field they pass as src.
static void buildCommentField(const char *src, char *dst, size_t dst_size) {
    aprs_free_text_build(src, g_config.my_no_archive, dst, dst_size);
}

// Resolves the optional APRS 1.2 base-91 comment telemetry group
// (telemetry_build_comment_tlm(), "|ss1122|") into p->cmtTlm, if and only if
// this beacon's own callsign/SSID is the one the Telemetry page has
// configured: the comment form only makes sense riding along on the
// telemetry station's own position report, since a receiving station reads
// it as THIS report's source station's telemetry. Called once per beacon
// service right after that service fills p->comment from its own g_config
// field. Kept out of p->comment itself so buildPositionPacket() and
// buildMicePositionPacket() can place it after the operator's comment text
// and before any trailing !DAO! extension, and reserve its bytes the same
// way they already reserve the DAO's, rather than let it be truncated along
// with an over-long comment. A group that would not fit p->cmtTlm is
// silently dropped (telemetry_build_comment_tlm() logs why) rather than
// truncated, since a truncated base-91 pair decodes to a wrong value instead
// of a missing one.
static void appendCommentTelemetry(beacon_params_t *p) {
    p->cmtTlm[0] = 0;

    telemetry_config_t tlmCfg;
    telemetry_config_load(&tlmCfg);
    if (!tlmCfg.mycall[0] || !call_equals_ci(tlmCfg.mycall, p->call) || tlmCfg.ssid != p->ssid)
        return;

    telemetry_build_comment_tlm(p->cmtTlm, sizeof(p->cmtTlm));
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
    if (S > APRS_EXT_DFS_STRENGTH_MAX)
        S = APRS_EXT_DFS_STRENGTH_MAX;

    char hgd[4];
    buildHgdCodes(gain, height, dir, hgd, sizeof(hgd));
    snprintf(out, outMax, "DFS%c%s", '0' + S, hgd);
}

// Selects and builds the beacon's data extension into `out`, which is left as
// an empty string when nothing is enabled or when the selected one may
// not travel with this beacon's symbol. `out` must be >= APRS_DF_EXT_BUF_SIZE:
// that is the size the DF form needs, and aprs_df_build_extension() empties
// the buffer instead of writing a short token below it, because a truncated
// DF report would decode as a different bearing rather than as a missing one.
// Every call site asserts that size at compile time, so an undersized buffer
// is a build error rather than an extension that silently never goes out.
// Exactly one thing is ever emitted into the slot: an enabled PHG/RNG/DFS/DF
// extension takes priority (the operator turned it on explicitly, the same
// precedence objects_items.c gives PHG over CSE/SPD for a stationary
// element); with none enabled, a live course/speed reading fills the slot
// instead, as the plain "CSE/SPD" pair APRS101 ch.7 describes for a moving
// station. They all occupy the same 7-byte slot after the symbol code,
// except PHG when p->beaconIntervalSec is known, which adds the two-byte
// PHGR probe rate/slash suffix described on buildPhgExtension(), and DF,
// which reaches APRS_DF_EXT_TAIL_LEN bytes past the slot.
static void buildDataExtension(const beacon_params_t *p, char *out, size_t outMax) {
    out[0] = 0;
    if (!p->extEnable) {
        // No PHG/RNG/DFS/DF selected: a live GNSS fix (Tracker beacon only,
        // see hasCourseSpeed's own comment) still has a natural home in this
        // slot, the CSE/SPD pair every APRS client already knows how to read
        // off a moving station.
        if (p->hasCourseSpeed)
            snprintf(out, outMax, "%03u/%03u", (unsigned)(p->courseDeg % 360), (unsigned)(p->speedKnots > 999 ? 999 : p->speedKnots));
        return;
    }

    switch ((aprs_ext_type_t)p->extType) {
        case APRS_EXT_RNG:
            buildRangeExtension(p->rangeMiles, out, outMax);
            return;
        case APRS_EXT_DF:
            // DF report (APRS101 ch.8). BRG/NRQ is only meaningful on the DF
            // symbol, and the token is 15 bytes where the slot is 7, so a
            // receiver seeing any other symbol would read the trailing
            // "/BRG/NRQ" as the first eight characters of the comment field.
            // The token therefore travels only with the DF symbol pair, and
            // the symbol that suppressed it is named once.
            {
                // Same fallbacks the position builders apply to an unset
                // symbol, so the test is made on the pair that actually goes
                // on the air.
                char symTable = p->symbol[0] ? p->symbol[0] : '/';
                char symCode = p->symbol[1] ? p->symbol[1] : '>';
                if (!aprs_df_symbol_matches(symTable, symCode)) {
                    static bool dfSymbolWarned = false;
                    if (!dfSymbolWarned) {
                        dfSymbolWarned = true;
                        ESP_LOGW(TAG, "DF report not transmitted: it needs the DF symbol \"%c%c\", this beacon uses \"%c%c\"", APRS_DF_SYMBOL_TABLE,
                                 APRS_DF_SYMBOL_CODE, symTable, symCode);
                    }
                    return;
                }
            }
            // DF is only ever selected on the IGate and Digipeater beacon
            // pages (see igateBeaconService()/digiBeaconService()), which
            // stay fixed stations with no course/speed source, so the
            // chapter's "000/000" pair - meaning exactly that - is always
            // correct here, while still carrying the bearing that follows it.
            aprs_df_build_extension(0, 0, &p->df, out, outMax);
            return;
        case APRS_EXT_DFS:
            buildDfsExtension(p->dfsStrength, p->phgGain, p->phgHeight, p->phgDir, out, outMax);
            return;
        case APRS_EXT_PHG:
        default:
            buildPhgExtension(p->phgPower, p->phgGain, p->phgHeight, p->phgDir, p->beaconIntervalSec, out, outMax);
            return;
    }
}

// Builds a Mic-E-format beacon line (APRS101 ch.10) into `out`, in place of
// the uncompressed/compressed layout buildPositionPacket() otherwise emits.
// Mic-E splits its payload between the AX.25 destination address (encoded
// here via aprs_mice_encode(), which returns 6 raw bytes - not necessarily
// printable ASCII) and the information field, so this builds the "call>dst"
// portion of the TNC2 line itself instead of reusing BEACON_DEST. The IGate
// and Digipeater beacons are always fixed-position with no course/speed
// source, so their report carries the on-air "unknown" course/speed pattern
// (000/000); the Tracker beacon carries its real course/speed instead
// whenever it is transmitting a live GNSS fix (hasCourseSpeed). The position
// comment is whatever the operator selected on the Tracker page, which
// cannot be Emergency.
//
// Mic-E has one free-text slot, and aprs12/mic-e-examples.txt fixes the order
// of what may go in it: the frequency block first (radios auto-tune from the
// leading bytes and stop looking after them), then the data extension the
// 1.2 revision allows here, then the operator's comment, then "!DAO!" last.
// The altitude field and the Manufacturer/Version pair sit outside that text,
// before and after it respectively, and are added by aprs_mice_encode()
// itself.
//
// `path` is the TNC2 path suffix, leading comma included, that goes between
// the destination address and the ':' - the caller picks it per leg, so an
// RF transmission carries the operator's digipeater selection and an APRS-IS
// transmission carries APRS_PATH_TCPIP_SUFFIX.
//
// Returns the packet length, or 0 if nothing usable is configured or the
// position/line does not fit the Mic-E format.
static int buildMicePositionPacket(const beacon_params_t *p, const char *path, char *out, size_t outMax) {
    if (!p->call[0])
        return 0;

    char callField[16];
    if (p->ssid > 0)
        snprintf(callField, sizeof(callField), "%s-%d", p->call, (int)p->ssid);
    else
        snprintf(callField, sizeof(callField), "%s", p->call);

    aprs_mice_report_t report;
    memset(&report, 0, sizeof(report));
    report.position.latitude_deg = (double)p->lat;
    report.position.longitude_deg = (double)p->lon;
    // Mic-E carries position ambiguity natively (APRS101 ch.10), by blanking
    // the same least significant digits the uncompressed format spaces out,
    // so the station-wide setting applies here unchanged.
    report.position.ambiguity = (aprs_position_ambiguity_t)p->ambiguity;
    // Symbol table byte as configured: '/' primary, '\' alternate, or one of
    // the overlay characters APRS 1.2 ch.21 allows in their place, 'A'-'Z' and
    // '0'-'9'. Mic-E carries this byte at info[8] with exactly the meaning the
    // uncompressed layout gives it, so an overlay travels as itself and is
    // handed to the encoder in the overlay field, which keeps the same symbol
    // on the map whichever layout the operator selects. The 'a'-'j' spelling
    // of ::APRS_COMPRESSED_OVERLAY_DIGIT_BASE belongs to the compressed layout
    // alone, whose first byte doubles as the layout marker, and has no place
    // here.
    char symTable = p->symbol[0] ? p->symbol[0] : APRS_SYMBOL_TABLE_DEFAULT;
    if (!aprs_symbol_table_is_valid(symTable))
        symTable = APRS_SYMBOL_TABLE_DEFAULT;
    if (symTable == '/' || symTable == '\\') {
        report.position.symbol.table = (symTable == '\\') ? APRS_SYMBOL_TABLE_ALTERNATE : APRS_SYMBOL_TABLE_PRIMARY;
    } else {
        // An overlay always reads against the alternate table: the overlay
        // character replaces the table byte on air, and the table it selects
        // is the one the symbol code is looked up in.
        report.position.symbol.table = APRS_SYMBOL_TABLE_ALTERNATE;
        report.position.symbol.overlay = symTable;
    }
    report.position.symbol.code = p->symbol[1] ? p->symbol[1] : '>';
    // Position comment, unpacked from the single 0-13 UI value into the
    // code plus alphabet the encoder takes. Emergency is not reachable from
    // this range by design (see ::MICE_POS_COMMENT_MAX), so no beacon this
    // station originates can claim one.
    uint8_t miceMsg = (p->miceMsg > MICE_POS_COMMENT_MAX) ? MICE_POS_COMMENT_DEFAULT : p->miceMsg;
    report.is_custom_message = (miceMsg >= MICE_POS_COMMENT_CUSTOM_BASE);
    report.message_code = (aprs_mice_message_code_t)(report.is_custom_message ? miceMsg - MICE_POS_COMMENT_CUSTOM_BASE : miceMsg);
    // Course/speed (APRS101 ch.10). "Unknown" (000/000) is the on-air
    // convention for a station with nothing to report here - the only case
    // before live GPS was wired in, and still what every fixed-position
    // beacon this project builds sends. A live GNSS fix (hasCourseSpeed,
    // Tracker beacon only, see its own comment on beacon_params_t) carries
    // its real course and speed instead.
    report.course_speed.is_unknown = !p->hasCourseSpeed;
    if (p->hasCourseSpeed) {
        report.course_speed.course_deg = p->courseDeg % 360;
        report.course_speed.speed_knots = p->speedKnots;
    }
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
    objitem_build_freq_block(p->freqMhz, p->freqToneTenths, p->freqDuplex, p->freqOffsetKhz, 0, false, freqBlock, sizeof(freqBlock));

    // Data extension (PHG / RNG / DFS / DF). Mic-E has no 7-byte slot of its own
    // after a symbol code, but the 1.2 revision states that the Mic-E text
    // field may carry any ordinary position comment field, PHG included, which
    // is how a station beaconing in Mic-E advertises its coverage. It is
    // written ahead of the operator's comment, so it stays in the leading,
    // fixed-position part of the text where a decoder looks for it, and behind
    // the frequency block, which radios expect in the first bytes of the text
    // and stop looking for after them. Sized as in buildPositionPacket(), so
    // the widest token fits here too.
    char ext[APRS_DF_EXT_BUF_SIZE] = { 0 };
    _Static_assert(sizeof(ext) >= APRS_DF_EXT_BUF_SIZE, "beacon data-extension buffer must hold the DF form, which is dropped whole below that size");
    buildDataExtension(p, ext, sizeof(ext));

    // Layout order for the text field is [freqBlock][ext][comment][comment
    // telemetry][!DAO!]: the base-91 comment telemetry group must follow the
    // operator's free-form comment text but precede the DAO extension
    // (APRS101 ch.13, aprs12/datum.txt). Both the telemetry group and the DAO
    // extension have their bytes reserved ahead of time, the same way the DAO
    // extension alone used to be, so an over-long comment truncates only the
    // comment - never the telemetry group or the DAO bytes trailing it.
    size_t daoLen = strlen(dao);
    size_t cmtTlmLen = strlen(p->cmtTlm);
    size_t textMax = sizeof(report.status_text);
    size_t reserved = daoLen + cmtTlmLen;
    size_t bodyMax = (textMax > reserved) ? textMax - reserved : 1;
    size_t used = 0;
    if (freqBlock[0])
        str_append(report.status_text, bodyMax, &used, "%s ", freqBlock);
    if (ext[0])
        str_append(report.status_text, bodyMax, &used, "%s", ext);
    if (p->comment[0])
        str_append(report.status_text, bodyMax, &used, "%s", p->comment);
    if (cmtTlmLen > 0)
        str_append(report.status_text, textMax - daoLen, &used, "%s", p->cmtTlm);
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

// Builds the full TNC2 text line for one beacon transmission. `path` is the
// path suffix for the leg this line is destined for, on the same terms as
// buildMicePositionPacket() above. Returns the packet length, or 0 if nothing
// usable is configured.
static int buildPositionPacket(const beacon_params_t *p, const char *path, char *out, size_t outMax) {
    if (!p->call[0])
        return 0;

    // Mic-E has its own fixed on-air layout (no timestamp, no compression -
    // see the mice field's own comment in beacon_params_t),
    // so it is handled by a dedicated builder and takes priority over every
    // other layout choice below.
    if (p->mice)
        return buildMicePositionPacket(p, path, out, outMax);

    char callField[16];
    if (p->ssid > 0)
        snprintf(callField, sizeof(callField), "%s-%d", p->call, (int)p->ssid);
    else
        snprintf(callField, sizeof(callField), "%s", p->call);

    // symbol = 2 chars: [0] symbol table ('/' primary or '\' alternate), [1] symbol code.
    // Falls back to a plain "car" symbol on the primary table if unset.
    char symTable = p->symbol[0] ? p->symbol[0] : '/';
    char symCode = p->symbol[1] ? p->symbol[1] : '>';

    // Data-extension token (PHG / RNG / DFS / DF when enabled, or live
    // CSE/SPD when none is and the beacon carries a course/speed reading -
    // see buildDataExtension()). Emitted right after the symbol code and
    // before the altitude/comment, matching the Object/Item info field
    // layout (main/objects_items.c) and the order the APRS spec defines for
    // this slot. Sized for the longest of them, which is the DF report: its
    // own buffer-size constant covers the 15-byte "CSE/SPD/BRG/NRQ" token
    // plus NUL, and is wider than the 9-byte PHGR-suffixed PHG form.
    char ext[APRS_DF_EXT_BUF_SIZE] = { 0 };
    _Static_assert(sizeof(ext) >= APRS_DF_EXT_BUF_SIZE, "beacon data-extension buffer must hold the DF form, which is dropped whole below that size");
    buildDataExtension(p, ext, sizeof(ext));

    // A pre-calculated radio range is the one data extension the compressed
    // layout carries natively: APRS101 ch.9 gives the cs bytes a radio range
    // form of their own ('{' followed by the range digit), so an RNG beacon
    // folds into the compressed field instead of forcing the uncompressed
    // one.
    bool extIsRange = p->extEnable && (aprs_ext_type_t)p->extType == APRS_EXT_RNG;

    // What the slot really holds decides the layout, not what was selected:
    // a DF report the symbol does not allow leaves the slot empty, and an
    // empty slot is no reason to give up compression. A live course/speed
    // reading (buildDataExtension()'s own CSE/SPD fallback, hasCourseSpeed)
    // has a compressed-format equivalent, so it does not count as "present"
    // here even though it filled ext[] - it is folded into the compressed
    // field's own cs/T slot below instead of forcing the uncompressed one.
    bool extIsCourseSpeed = !p->extEnable && p->hasCourseSpeed;
    bool extPresent = (ext[0] != 0) && !extIsCourseSpeed;

    // Two things force the uncompressed layout:
    //
    //   * A PHG, DFS or DF data extension. The compressed format has no room
    //     for the 7-byte slot (APRS101 ch.9 states it does not support PHG),
    //     and a DF report is wider than the slot still; emitting those bytes
    //     inside a compressed report would just be wrong data, and dropping
    //     the extension to keep compression would lose a field the operator
    //     explicitly enabled.
    //   * Position ambiguity. Ambiguity is expressed by blanking decimal
    //     digits, and the compressed format has no decimal digits to blank -
    //     a compressed report always states a position to full resolution, so
    //     honouring the compress flag here would transmit the exact position
    //     the operator asked to obscure.
    //
    // A fixed-position beacon - the only kind these three ever sent before
    // live GPS was wired in - never sets hasCourseSpeed, so for one the
    // compressed cs/T slot still carries only the radio range form or "no
    // cs/T data" (3 spaces), exactly as before.
    bool useCompressed = p->compress && (!extPresent || extIsRange) && p->ambiguity == 0;

    // The operator selected two settings that cannot both be honoured, so the
    // one that is dropped is named rather than left to be discovered off the
    // air. Position ambiguity is not part of this: it blanks digits of the
    // uncompressed layout, which keeps its extension slot, so the two travel
    // together without either giving way. A live course/speed reading is not
    // part of this either, for the same reason RNG is not: it has a
    // compressed-format home of its own and never forces the uncompressed
    // layout.
    if (p->compress && !useCompressed && extPresent && !extIsRange)
        ESP_LOGW(TAG, "Compressed position disabled for this beacon: the selected data extension needs the uncompressed layout");

    // Altitude has a compressed form of its own (APRS101 ch.9): the same two
    // cs bytes, read as an altitude instead of as a course/speed pair when the
    // type byte names GGA as the NMEA source. It costs nothing on air, where
    // the uncompressed "/A=" token costs nine bytes, so a compressed report
    // carrying an altitude uses it - unless the cs slot is already spoken for
    // by a radio range or a live course/speed reading, neither of which has
    // anywhere else to go. Altitude always has the "/A=" form to fall back
    // on, so it is the one that gives way.
    bool useCompressedAltitude = useCompressed && p->sendAltitude && !extIsRange && !extIsCourseSpeed;

    // Sized for the larger of the two layouts: uncompressed is up to 21
    // bytes (9-char latStr content + symTable + 10-char lonStr content +
    // symCode), compressed is a fixed 13 bytes (symTable + 4 lat + 4 lon +
    // symCode + 3 cs/T), plus NUL either way.
    char posField[22];
    if (useCompressed) {
        char csT[3] = { ' ', ' ', ' ' };
        if (extIsRange) {
            aprs_compressed_cs_from_range(p->rangeMiles, csT);
            ext[0] = 0; // the range travels in the compressed field's own cs/T slot
        } else if (extIsCourseSpeed) {
            aprs_compressed_cs_from_course_speed(p->courseDeg, p->speedKnots, csT);
            ext[0] = 0; // course/speed travels in the compressed field's own cs/T slot
        } else if (useCompressedAltitude) {
            int feet = (int)(p->alt * 3.28084f);
            if (feet < 0)
                feet = 0;
            aprs_compressed_cs_from_altitude((unsigned)feet, csT);
        }
        aprs_coord_format_compressed(p->lat, p->lon, symTable, symCode, csT, posField, sizeof(posField));
    } else {
        char latStr[10], lonStr[11];
        aprs_coord_format_ambiguous(p->lat, p->lon, p->ambiguity, latStr, sizeof(latStr), lonStr, sizeof(lonStr));
        snprintf(posField, sizeof(posField), "%s%c%s%c", latStr, symTable, lonStr, symCode);
    }

    // Uncompressed altitude token, used whenever the compressed form above is
    // not the one carrying it. Emitting both would state the altitude twice,
    // at two different resolutions.
    char extra[40] = { 0 };
    if (p->sendAltitude && !useCompressedAltitude) {
        int feet = (int)(p->alt * 3.28084f);
        if (feet < 0)
            feet = 0;
        snprintf(extra, sizeof(extra), "/A=%06d", feet);
    }

    // !DAO! precision/datum extension (aprs12/datum.txt), appended after the
    // comment telemetry group below. Only meaningful, and only applied,
    // alongside the uncompressed layout at full precision: the compressed
    // format already carries full resolution, and a station that asked for a
    // coarser-than-full-precision report (ambiguity > 0) must not have that
    // precision handed straight back through this extension.
    char dao[APRS_DAO_BUF_SIZE] = { 0 };
    if (p->daoEnable && p->ambiguity == 0 && !useCompressed)
        aprs_dao_build(p->lat, p->lon, dao);

    // Frequency block (freqspec.txt): built once here and prepended to the
    // comment as its first bytes, ahead of anything the operator typed. The
    // frequency token itself is exactly 10 bytes whichever of the spec's
    // three forms the configured frequency selects, satisfying the
    // fixed-field requirement for radios (e.g. Yaesu) that only look at the
    // first 10 bytes of the comment and do not decode the frequency Object
    // form. A single space separates it from the rest of the comment.
    //
    // Layout order is [freqBlock][comment][comment telemetry][!DAO!]
    // (APRS101 ch.13, aprs12/datum.txt): the base-91 comment telemetry group
    // must follow the operator's free-form comment text but precede the DAO
    // extension. Both the telemetry group and the DAO extension have their
    // bytes reserved ahead of the operator's comment text, so an over-long
    // comment truncates only the comment - never the telemetry group or the
    // DAO bytes trailing it.
    char comment[COMMENT_SIZE];
    {
        char freqBlock[24];
        objitem_build_freq_block(p->freqMhz, p->freqToneTenths, p->freqDuplex, p->freqOffsetKhz, 0, false, freqBlock, sizeof(freqBlock));

        size_t cmtTlmLen = strlen(p->cmtTlm);
        size_t daoLen = strlen(dao);
        size_t reserved = cmtTlmLen + daoLen;
        size_t bodyMax = (sizeof(comment) > reserved) ? sizeof(comment) - reserved : 1;

        size_t used = 0;
        if (freqBlock[0])
            str_append(comment, bodyMax, &used, "%s ", freqBlock);
        if (p->comment[0])
            str_append(comment, bodyMax, &used, "%s", p->comment);
        if (cmtTlmLen > 0)
            str_append(comment, sizeof(comment) - daoLen, &used, "%s", p->cmtTlm);
        if (daoLen > 0)
            str_append(comment, sizeof(comment), &used, "%s", dao);
    }

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
    // comment already carries the trailing comment telemetry group and !DAO!
    // extension in the order the earlier reservation established, so neither
    // is passed here separately.
    char infoField[256]; // ts(7)+posField(up to 21)+ext(7)+extra(40)+comment(up to 128, includes telemetry+dao)+NUL
    if (p->timestamp) {
        time_t now = time(NULL);
        struct tm tmv;
        gmtime_r(&now, &tmv);
        char ts[8];
        snprintf(ts, sizeof(ts), "%02d%02d%02dz", tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
        snprintf(infoField, sizeof(infoField), "%c%s%s%s%s%s", p->msgCapable ? '@' : '/', ts, posField, ext, extra, comment);
    } else {
        snprintf(infoField, sizeof(infoField), "%c%s%s%s%s", p->msgCapable ? '=' : '!', posField, ext, extra, comment);
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
    // report's comment. freqMhz <= 0 means no block is emitted. As with
    // beacon_params_t, there is no per-service range setting, so the block's
    // optional range sub-field is never emitted here.
    float freqMhz;
    uint16_t freqToneTenths;
    int8_t freqDuplex;
    uint16_t freqOffsetKhz;
} status_params_t;

// Builds the meteor-scatter beam heading and ERP block (APRS101 ch.16) into
// `out` (>= 4 bytes), or leaves it empty when either half is switched off.
// The block is '^' followed by two code characters:
//
//   H = beam heading / 10, as '0'-'9' for 0-90 degrees and 'A'-'Z' for
//       100-350 degrees.
//   P = ERP code, where the effective radiated power is 10 * P^2 watts and
//       the code itself is the single character '0' + P. The spec's table
//       runs from 10 W ('1') to 7290 W ('K'), which is one contiguous run of
//       printable ASCII, so the code is computed rather than looked up. The
//       configured power is matched to the nearest table entry.
static void buildBeamErpBlock(int16_t beamDeg, uint16_t erpWatts, char *out, size_t outMax) {
    out[0] = 0;
    if (beamDeg < 0 || erpWatts == 0)
        return;

    unsigned h = ((unsigned)beamDeg % 360u) / 10u;
    char hChar = (h <= 9) ? (char)('0' + h) : (char)('A' + (h - 10));

    long p = lroundf(sqrtf((float)erpWatts / 10.0f));
    if (p < STATUS_ERP_CODE_MIN)
        p = STATUS_ERP_CODE_MIN;
    if (p > STATUS_ERP_CODE_MAX)
        p = STATUS_ERP_CODE_MAX;

    snprintf(out, outMax, "^%c%c", hChar, (char)('0' + p));
}

// Builds the full TNC2 text line for one status-report transmission. The
// info field is DTI '>' followed by the free-text status (APRS101 ch.16).
//
// After the '>', up to two optional blocks precede the operator's own status
// text, in the fixed order the spec and this project's conventions put them
// in:
//
//   1. Either the "DDHHMMz" zulu timestamp or the Maidenhead grid locator,
//      immediately after '>' - APRS101 ch.16 defines the two as mutually
//      exclusive forms of the same leading field, so a status report never
//      carries both. When both g_config.status_timestamp_en and the grid
//      option are set, the locator wins - it carries this station's position,
//      which the timestamp does not - and the timestamp is left out. The
//      locator itself is the fixed 6-char field (always upper case) that
//      aprs_maidenhead_locator() produces, immediately followed by the
//      symbol table byte and the symbol code with no separating space, the
//      ">IO91SX/G" form the spec defines.
//   2. The frequency block (freqspec.txt) - the fixed 10-byte frequency
//      field, its tone and its duplex shift - when this beacon has a monitor
//      frequency configured - the second advertisement
//      the spec endorses, for radios that decode neither the frequency
//      Object form nor the leading bytes of a position report's comment.
//
// A single space separates the last present block from the status text that
// follows it; with no block present the text follows the DTI directly, and
// with the locator present the space follows the symbol code. The separator
// belongs to the block rather than to the text, so a report that carries
// neither a leading field nor a frequency block - including one whose blocks
// the length budget below has just dropped - reads ">My status text", the
// form APRS101 ch.16 defines. Receivers that understand a given leading field
// read it out on its own; the rest simply show the whole thing as status
// text. The configured status text itself is never interpreted: whatever the
// operator typed is carried verbatim.
//
// One optional block follows that text instead of preceding it: the
// meteor-scatter beam heading and ERP pair, which APRS101 ch.16 defines as
// the last two characters of the status text preceded by '^' - a fixed
// position it can only hold by being last. It is emitted only when both
// halves are configured (::app_config_t::status_beam_deg and
// ::app_config_t::status_erp_watts).
//
// All pieces share the APRS_STATUS_INFO_MAX budget APRS101 ch.16 sets for the
// information field, and a full status text plus every optional block asks
// for more than that, so the blocks are dropped in a defined order until the
// field fits: the leading field (timestamp or locator) first, then the
// frequency block. The beam heading and ERP pair is never dropped: it is
// three bytes, and a station running meteor scatter is transmitting the
// report for those three bytes. The operator's text is what the report exists
// to carry, so it does not give way either - if the text plus that pair does
// not fit on its own the report is refused outright, on the same terms
// buildPositionPacket() refuses an over-long position.
//
// `path` is the path suffix for the leg this line is destined for, on the
// same terms as the position builders above.
//
// Returns the packet length, or 0 if nothing usable is configured.
static int buildStatusPacket(const status_params_t *p, const char *path, char *out, size_t outMax) {
    if (!p->call[0] || !p->statusText[0])
        return 0;

    char callField[16];
    if (p->ssid > 0)
        snprintf(callField, sizeof(callField), "%s-%d", p->call, (int)p->ssid);
    else
        snprintf(callField, sizeof(callField), "%s", p->call);

    char freqBlock[24];
    objitem_build_freq_block(p->freqMhz, p->freqToneTenths, p->freqDuplex, p->freqOffsetKhz, 0, false, freqBlock, sizeof(freqBlock));

    // The grid locator takes precedence over the timestamp: APRS101 ch.16
    // allows only one of the two immediately after the '>' DTI, and the
    // locator is the one that carries information the timestamp does not.
    bool useGrid = p->gridEnable;
    bool useTs = g_config.status_timestamp_en && !useGrid;
    if (p->gridEnable && g_config.status_timestamp_en)
        ESP_LOGW(TAG, "status report: Maidenhead locator and timestamp both enabled - timestamp omitted");

    char ts[8] = { 0 };
    if (useTs) {
        time_t now = time(NULL);
        struct tm tmv;
        gmtime_r(&now, &tmv);
        snprintf(ts, sizeof(ts), "%02d%02d%02dz", tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    }

    char grid[APRS_MAIDENHEAD_BUF_SIZE] = { 0 };
    char symTable = 0, symCode = 0;
    if (useGrid) {
        aprs_maidenhead_locator(p->lat, p->lon, grid, sizeof(grid));
        symTable = p->symbol[0] ? p->symbol[0] : '/';
        symCode = p->symbol[1] ? p->symbol[1] : '>';
    }

    // Meteor-scatter beam heading and ERP, station-wide like the Maidenhead
    // option above and read the same way: the two values are scalars, so the
    // builder takes them straight from g_config rather than through the
    // caller's snapshot, which exists to keep the strings from tearing.
    char hp[4] = { 0 };
    buildBeamErpBlock(g_config.status_beam_deg, g_config.status_erp_watts, hp, sizeof(hp));

    // '>' (1) + leading field (timestamp "DDHHMMz" = 7, or locator "IO91SX"
    // + symbol table + symbol code = 9) + freq block (up to 20,
    // space-separated) + separating space + status text (up to
    // STATUS_SIZE - 1) + the 3-byte beam/ERP block + NUL, so the buffer holds
    // every assembly attempt below even before the blocks are dropped. Built with str_append() so a
    // would-be negative/oversize snprintf() result from any one piece
    // saturates the buffer instead of underflowing the running offset.
    char infoField[STATUS_SIZE + 40];
    bool useFreq = freqBlock[0] != 0;
    size_t used;
    for (;;) {
        used = 0;
        str_append(infoField, sizeof(infoField), &used, ">");
        if (useGrid)
            str_append(infoField, sizeof(infoField), &used, "%s%c%c", grid, symTable, symCode);
        else if (useTs)
            str_append(infoField, sizeof(infoField), &used, "%s", ts);
        if (useFreq)
            str_append(infoField, sizeof(infoField), &used, "%s", freqBlock);
        // The separating space is emitted only when a block already occupies
        // the field past the '>' DTI, so a report carrying neither leading
        // field nor frequency block puts the operator's text straight after
        // the DTI. The test reads the running offset rather than the option
        // flags because it has to hold for every attempt: each retry below
        // drops a block and rebuilds the field from scratch, and the space
        // has to go with whatever block it followed.
        if (used > 1)
            str_append(infoField, sizeof(infoField), &used, " ");
        str_append(infoField, sizeof(infoField), &used, "%s", p->statusText);
        if (hp[0])
            str_append(infoField, sizeof(infoField), &used, "%s", hp);

        if (used <= APRS_STATUS_INFO_MAX)
            break;
        // Over budget: give up the optional blocks, least useful first. The
        // leading field only restates information this station already
        // beacons elsewhere (its position or the current time), while the
        // frequency block is the one thing in the report a radio can act on,
        // so the leading field goes first.
        if (useGrid || useTs) {
            if (useGrid)
                ESP_LOGW(TAG, "status info field %u bytes (max %d) - Maidenhead locator omitted", (unsigned)used, APRS_STATUS_INFO_MAX);
            else
                ESP_LOGW(TAG, "status info field %u bytes (max %d) - timestamp omitted", (unsigned)used, APRS_STATUS_INFO_MAX);
            useGrid = false;
            useTs = false;
            continue;
        }
        if (useFreq) {
            useFreq = false;
            ESP_LOGW(TAG, "status info field %u bytes (max %d) - frequency block omitted", (unsigned)used, APRS_STATUS_INFO_MAX);
            continue;
        }
        ESP_LOGW(TAG, "status info field %u bytes (max %d) - shorten the status text", (unsigned)used, APRS_STATUS_INFO_MAX);
        return 0;
    }

    int n = snprintf(out, outMax, "%s>%s%s:%s", callField, BEACON_DEST, path, infoField);
    // A status line is at most 172 bytes: call field (up to 15) + '>' +
    // destination (6) + path (up to 79) + ':' + info field (up to
    // APRS_STATUS_INFO_MAX, which the assembly above has already enforced),
    // so it always fits the APRS_TNC2_BUF_SIZE buffer every caller provides.
    // The check is kept anyway, on the same terms as buildPositionPacket():
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
// Per-leg transmission
//
// A beacon that is enabled on both legs is built twice, once per leg, because
// the path is the one part of the line that differs between them: the RF leg
// carries the digipeater path the operator selected on that beacon's page,
// and the APRS-IS leg carries APRS_PATH_TCPIP_SUFFIX, which is what
// aprs-is.net/Connecting.aspx requires of a client's own traffic. Both lines
// come from the same builder and the same parameter snapshot, so nothing else
// about them can drift apart.
//
// Each leg is also reported from what actually happened rather than from an
// unconditional "beacon TX" line: igate_send_raw() returns false whenever the
// APRS-IS uplink is not connected yet (no internet route at boot, for
// instance), and aprs_service_send_tnc2() returns false when the modem is
// busy, so a per-leg line keeps a transmission that never left from being
// logged as one that did.
// ---------------------------------------------------------------------------

// Builds and sends one position beacon per enabled leg. `label` names the
// beacon in the log lines ("Tracker beacon"), and `hint` completes the
// "not built" warning with the setting the operator has to fill in.
static void txPositionBeacon(const beacon_params_t *p, bool toRf, bool toInet, const char *label, const char *hint) {
    char packet[APRS_TNC2_BUF_SIZE]; // sized by the RF leg's own limit, so a line that does not fit is refused at build time

    if (toRf) {
        char path[80];
        aprs_path_build_suffix(p->pathSel, p->pathPreset, path, sizeof(path));
        int len = buildPositionPacket(p, path, packet, sizeof(packet));
        if (len > 0) {
            if (aprs_service_send_tnc2(packet, (size_t)len))
                ESP_LOGI(TAG, "%s TX (RF): %s", label, packet);
            else
                ESP_LOGW(TAG, "%s NOT sent over RF - modem not ready or busy: %s", label, packet);
        } else {
            ESP_LOGW(TAG, "%s not built - %s, or the line did not fit; skipping", label, hint);
        }
    }

    if (toInet) {
        int len = buildPositionPacket(p, APRS_PATH_TCPIP_SUFFIX, packet, sizeof(packet));
        if (len > 0) {
            if (igate_send_raw(packet, (size_t)len))
                ESP_LOGI(TAG, "%s TX (INET): %s", label, packet);
            else
                ESP_LOGW(TAG, "%s NOT sent over INET - APRS-IS not connected yet: %s", label, packet);
        } else {
            ESP_LOGW(TAG, "%s not built - %s, or the line did not fit; skipping", label, hint);
        }
    }

    ESP_LOGD(TAG, "%s: scheduler stack free: %u bytes", label, (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
}

// Builds and sends one status report per enabled leg, on the same terms as
// txPositionBeacon() above.
static void txStatusBeacon(const status_params_t *p, bool toRf, bool toInet, const char *label, const char *hint) {
    char packet[APRS_TNC2_BUF_SIZE]; // sized by the RF leg's own limit, so a line that does not fit is refused at build time

    if (toRf) {
        char path[80];
        aprs_path_build_suffix(p->pathSel, p->pathPreset, path, sizeof(path));
        int len = buildStatusPacket(p, path, packet, sizeof(packet));
        if (len > 0) {
            if (aprs_service_send_tnc2(packet, (size_t)len))
                ESP_LOGI(TAG, "%s TX (RF): %s", label, packet);
            else
                ESP_LOGW(TAG, "%s NOT sent over RF - modem not ready or busy: %s", label, packet);
        } else {
            ESP_LOGW(TAG, "%s not built - %s, or the line did not fit; skipping", label, hint);
        }
    }

    if (toInet) {
        int len = buildStatusPacket(p, APRS_PATH_TCPIP_SUFFIX, packet, sizeof(packet));
        if (len > 0) {
            if (igate_send_raw(packet, (size_t)len))
                ESP_LOGI(TAG, "%s TX (INET): %s", label, packet);
            else
                ESP_LOGW(TAG, "%s NOT sent over INET - APRS-IS not connected yet: %s", label, packet);
        } else {
            ESP_LOGW(TAG, "%s not built - %s, or the line did not fit; skipping", label, hint);
        }
    }
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
            char trkStsStripped[STATUS_SIZE];
            buildCommentField(g_config.trk_status, trkStsStripped, sizeof(trkStsStripped));
            str_copy_utf8_safe(trkStsStripped, p.statusText, sizeof(p.statusText));
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

        txStatusBeacon(&p, g_config.trk_loc2rf, g_config.trk_loc2inet, "Tracker status",
                       "no callsign or status text configured (set the Tracker or APRS callsign and the status text)");

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
            char igateStsStripped[STATUS_SIZE];
            buildCommentField(g_config.igate_status, igateStsStripped, sizeof(igateStsStripped));
            str_copy_utf8_safe(igateStsStripped, p.statusText, sizeof(p.statusText));
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

        txStatusBeacon(&p, g_config.igate_loc2rf, g_config.igate_loc2inet, "IGate status", "no APRS callsign or status text configured");

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
            char digiStsStripped[STATUS_SIZE];
            buildCommentField(g_config.digi_status, digiStsStripped, sizeof(digiStsStripped));
            str_copy_utf8_safe(digiStsStripped, p.statusText, sizeof(p.statusText));
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

        txStatusBeacon(&p, g_config.digi_loc2rf, g_config.digi_loc2inet, "Digipeater status",
                       "no callsign or status text configured (set the Digipeater or APRS callsign and the status text)");

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
        beacon_params_t p = { 0 };
        bool useLiveGps;
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
            p.miceMsg = g_config.trk_mice_msg;
            p.msgCapable = g_config.msg_enable;
            p.ambiguity = g_config.pos_ambiguity;
            p.daoEnable = g_config.pos_dao_en;
            memcpy(p.symbol, g_config.trk_symbol, sizeof(p.symbol));
            char trkCommentStripped[COMMENT_SIZE];
            buildCommentField(g_config.trk_comment, trkCommentStripped, sizeof(trkCommentStripped));
            str_copy_utf8_safe(trkCommentStripped, p.comment, sizeof(p.comment));
            memcpy(p.pathPreset, g_config.path, sizeof(p.pathPreset));
            p.freqMhz = g_config.trk_freq_mhz;
            p.freqToneTenths = g_config.trk_tone_tenths;
            p.freqDuplex = g_config.trk_duplex;
            p.freqOffsetKhz = g_config.trk_offset_khz;
            // The Tracker's one data extension is PHG, describing the coverage
            // of this station's own antenna, so its sub-fields come from the
            // station-wide PHG block on the Station page rather than from a
            // second copy of the same four settings on this page.
            p.extEnable = g_config.trk_phg_enable;
            p.extType = APRS_EXT_PHG;
            p.phgPower = g_config.my_phg_power;
            p.phgGain = g_config.my_phg_gain;
            p.phgHeight = g_config.my_phg_height;
            p.phgDir = g_config.my_phg_dir;
            p.beaconIntervalSec = sched_clamp_interval(g_config.trk_interval, BEACON_MIN_INTERVAL_S, BEACON_DEFAULT_INTERVAL_S);
            useLiveGps = g_config.trk_use_live_gps;
        }
        app_config_unlock();

        // Live GPS fix (APRS101 ch.6/ch.9/ch.10): when the operator has "Use
        // live GPS fix" on, read the GNSS receiver fresh for this
        // transmission and let a current fix override the fixed
        // lat/lon/alt/course/speed snapshotted above. gps_snapshot() takes
        // its own lock (gps.c), independent of app_config_lock(), so this is
        // called after that lock is released rather than nested inside it.
        // Anything short of a full, current fix - the receiver switched off,
        // still acquiring, or its link gone stale (gps_data_t::link_up past
        // ::GPS_LINK_TIMEOUT_S) - leaves p exactly as filled from
        // g_config.trk_* above, so the beacon keeps beaconing the fixed
        // position rather than transmitting nothing or a stale fix.
        if (useLiveGps) {
            gps_data_t g;
            if (gps_snapshot(&g) && g.valid && g.has_position) {
                p.lat = (float)g.latitude;
                p.lon = (float)g.longitude;
                if (g.has_altitude)
                    p.alt = (float)g.altitude_m;
                // Course/speed only carry meaning together, and only once
                // the receiver has actually reported both this cycle; a
                // stationary fix legitimately has no course, so course alone
                // is not enough to claim movement.
                if (g.has_course && g.has_speed) {
                    p.hasCourseSpeed = true;
                    p.courseDeg = (uint16_t)(((int)(g.course_deg + 0.5)) % 360);
                    // gps_data_t carries speed in km/h (gps.h); the position
                    // report's CSE/SPD, compressed cs/T and Mic-E course/speed
                    // fields all take knots, and 1.852 is this project's own
                    // km/h-per-knot factor (see gps.c's NMEA speed parsing).
                    double speedKt = g.speed_kmh / 1.852;
                    p.speedKnots = (uint16_t)(speedKt < 0.0 ? 0 : speedKt + 0.5);
                }
            }
        }

        appendCommentTelemetry(&p);

        txPositionBeacon(&p, g_config.trk_loc2rf, g_config.trk_loc2inet, "Tracker beacon", "no callsign configured (set Tracker or APRS callsign)");

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
            char igateCommentStripped[COMMENT_SIZE];
            buildCommentField(g_config.igate_comment, igateCommentStripped, sizeof(igateCommentStripped));
            str_copy_utf8_safe(igateCommentStripped, p.comment, sizeof(p.comment));
            memcpy(p.pathPreset, g_config.path, sizeof(p.pathPreset));
            p.extEnable = g_config.igate_phg_enable;
            p.extType = g_config.igate_ext_type;
            p.phgPower = g_config.igate_phg_power;
            p.phgGain = g_config.igate_phg_gain;
            p.phgHeight = g_config.igate_phg_height;
            p.phgDir = g_config.igate_phg_dir;
            p.rangeMiles = g_config.igate_range_miles;
            p.dfsStrength = g_config.igate_dfs_strength;
            p.df.bearing_deg = g_config.igate_df_bearing;
            p.df.number = g_config.igate_df_nrq_n;
            p.df.range_code = g_config.igate_df_nrq_r;
            p.df.quality = g_config.igate_df_nrq_q;
            p.beaconIntervalSec = sched_clamp_interval(g_config.igate_interval, BEACON_MIN_INTERVAL_S, BEACON_DEFAULT_INTERVAL_S);
            p.freqMhz = g_config.igate_freq_mhz;
            p.freqToneTenths = g_config.igate_tone_tenths;
            p.freqDuplex = g_config.igate_duplex;
            p.freqOffsetKhz = g_config.igate_offset_khz;
        }
        app_config_unlock();
        appendCommentTelemetry(&p);

        txPositionBeacon(&p, g_config.igate_loc2rf, g_config.igate_loc2inet, "IGate beacon", "no APRS callsign configured");

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
        char igateCommentStripped[COMMENT_SIZE];
        buildCommentField(g_config.igate_comment, igateCommentStripped, sizeof(igateCommentStripped));
        str_copy_utf8_safe(igateCommentStripped, p->comment, sizeof(p->comment));
        memcpy(p->pathPreset, g_config.path, sizeof(p->pathPreset));
        p->extEnable = g_config.igate_phg_enable;
        p->extType = g_config.igate_ext_type;
        p->phgPower = g_config.igate_phg_power;
        p->phgGain = g_config.igate_phg_gain;
        p->phgHeight = g_config.igate_phg_height;
        p->phgDir = g_config.igate_phg_dir;
        p->rangeMiles = g_config.igate_range_miles;
        p->dfsStrength = g_config.igate_dfs_strength;
        p->df.bearing_deg = g_config.igate_df_bearing;
        p->df.number = g_config.igate_df_nrq_n;
        p->df.range_code = g_config.igate_df_nrq_r;
        p->df.quality = g_config.igate_df_nrq_q;
        p->beaconIntervalSec = sched_clamp_interval(g_config.igate_interval, BEACON_MIN_INTERVAL_S, BEACON_DEFAULT_INTERVAL_S);
        p->ambiguity = g_config.pos_ambiguity;
        p->daoEnable = g_config.pos_dao_en;
        p->freqMhz = g_config.igate_freq_mhz;
        p->freqToneTenths = g_config.igate_tone_tenths;
        p->freqDuplex = g_config.igate_duplex;
        p->freqOffsetKhz = g_config.igate_offset_khz;
    }
    app_config_unlock();
    appendCommentTelemetry(p);
}

int beacon_build_igate_position_packet(const char *path, char *out, size_t out_max) {
    beacon_params_t p;
    fillIgatePositionParams(&p);
    return buildPositionPacket(&p, path, out, out_max);
}

int beacon_build_igate_status_packet(const char *path, char *out, size_t out_max) {
    // Same snapshot igateStatusService() takes, so the on-demand copy and the
    // periodic status beacon are byte-for-byte identical.
    status_params_t p = { 0 };
    app_config_lock();
    {
        memcpy(p.call, g_config.aprs_mycall, sizeof(p.call));
        p.ssid = g_config.aprs_ssid;
        p.pathSel = g_config.igate_path;
        char igateStsStripped[STATUS_SIZE];
        buildCommentField(g_config.igate_status, igateStsStripped, sizeof(igateStsStripped));
        str_copy_utf8_safe(igateStsStripped, p.statusText, sizeof(p.statusText));
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
    return buildStatusPacket(&p, path, out, out_max);
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
        beacon_params_t p = { 0 };
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
            char digiCommentStripped[COMMENT_SIZE];
            buildCommentField(g_config.digi_comment, digiCommentStripped, sizeof(digiCommentStripped));
            str_copy_utf8_safe(digiCommentStripped, p.comment, sizeof(p.comment));
            memcpy(p.pathPreset, g_config.path, sizeof(p.pathPreset));
            // PHG is the coverage circle other stations reason about when they
            // pick a path, which is why APRS101 ch.7 presents it as a
            // digipeater's field first of all, so this role selects among the
            // same four extensions the IGate role does and from its own
            // sub-fields.
            p.extEnable = g_config.digi_phg_enable;
            p.extType = g_config.digi_ext_type;
            p.phgPower = g_config.digi_phg_power;
            p.phgGain = g_config.digi_phg_gain;
            p.phgHeight = g_config.digi_phg_height;
            p.phgDir = g_config.digi_phg_dir;
            p.rangeMiles = g_config.digi_range_miles;
            p.dfsStrength = g_config.digi_dfs_strength;
            p.df.bearing_deg = g_config.digi_df_bearing;
            p.df.number = g_config.digi_df_nrq_n;
            p.df.range_code = g_config.digi_df_nrq_r;
            p.df.quality = g_config.digi_df_nrq_q;
            p.beaconIntervalSec = sched_clamp_interval(g_config.digi_interval, BEACON_MIN_INTERVAL_S, BEACON_DEFAULT_INTERVAL_S);
            p.freqMhz = g_config.digi_freq_mhz;
            p.freqToneTenths = g_config.digi_tone_tenths;
            p.freqDuplex = g_config.digi_duplex;
            p.freqOffsetKhz = g_config.digi_offset_khz;
        }
        app_config_unlock();
        appendCommentTelemetry(&p);

        txPositionBeacon(&p, g_config.digi_loc2rf, g_config.digi_loc2inet, "Digipeater beacon", "no callsign configured (set Digipeater or APRS callsign)");

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
