// @file query.c
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
// @brief APRS query responder implementation.
//
// Recognizes the four general queries APRS101 chapter 15 defines - "?APRS?",
// "?WX?", "?IGATE?" and "?QRU?" - in broadcast traffic, and the full directed
// set ("?APRSD", "?APRSH", "?APRSM", "?APRSO", "?APRSP", "?APRSS", "?APRST"
// and its "?PING?" alias) when addressed to this station, then transmits the
// matching response.
//
// Position, status and weather answers reuse the existing beacon builders, so
// a reply is byte-for-byte consistent with what the periodic beacons would
// send. The QRU group roll-call is a status packet built the same way, since
// like the other general queries it is broadcast to everyone listening rather
// than addressed to one station. The list-style directed answers (directs,
// heard, traceroute) are returned as APRS text messages addressed back to the
// querying station, which is the form chapter 15 specifies for a directed
// query.
//
// Every query is handled together with the source it arrived on. The source
// selects the operator switch that says whether that source is answered at all
// (g_config.query_rf / g_config.query_inet) and it selects the channel the
// answer is transmitted on, so a question heard on the air is answered on the
// air and a question read from the APRS-IS feed is answered to APRS-IS. That
// pairing is what keeps internet traffic away from the transmitter: general
// queries are ordinary backbone traffic, and an IGate's feed carries a steady
// stream of them.
//
// Answers are built and transmitted from the beacon scheduler task, never from
// the task a query arrives on: query_process() and query_process_directed()
// parse, rate-limit and record a request, and query_service() drains those
// requests on the scheduler's next pass. Building an answer walks the same
// deep call tree the periodic beacons do - a beacon builder, several of
// newlib's float-capable snprintf()s, the coordinate and path builders, then
// the TNC2/AX.25 encode chain - which is exactly the tree beacon_scheduler.c
// sizes its stack for; the radio RX and APRS-IS tasks that receive queries run
// on a fraction of that stack. Deferring also keeps the receiving task free
// for the length of a transmission burst, which is the reason the "?APRSO"
// answer has always gone out through objitems_request_transmit_all().

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_config.h"
#include "aprs_filter.h"      // aprs_filter_haversine_km()
#include "aprs_path.h"        // aprs_path_build_suffix(), APRS_PATH_PRESET_COUNT / _SIZE, APRS_PATH_TCPIP_SUFFIX
#include "aprs_service.h"     // APRS_TNC2_BUF_SIZE / APRS_TNC2_MAX_LEN
#include "beacon.h"           // beacon_build_igate_position_packet() / beacon_build_igate_status_packet()
#include "beacon_scheduler.h" // beacon_scheduler_wake()
#include "igate.h"            // igate_get_stats()
#include "lastheard.h"        // lastheard_directs() / lastheard_heard_history() / lastheard_station_count()
#include "message.h"          // MSG_CHANNEL_RF / MSG_CHANNEL_INET / message_send_pending_to()
#include "query.h"
#include "sched_time.h" // sched_mono_seconds()
#include "weather.h"    // weather_build_report_packet()
#ifdef ENABLE_OBJECTS_ITEMS
#include "objects_items.h" // objitems_load() / objitems_request_transmit_all() / OBJITEM_COUNT
#endif

static const char *TAG = "query";

// Same software-identifier destination call used by the message and beacon
// components, for consistency across the firmware's own-originated traffic.
#define QUERY_DEST APRS_TOCALL

static void (*s_txHandler)(const char *packet, size_t len, uint8_t channels) = NULL;

// One "last responded" monotonic timestamp per broadcast query type and
// source, so a misbehaving network flooding "?APRS?" broadcasts cannot make
// this station answer more than once per g_config.query_min_interval_sec -
// both to save airtime and to avoid a feedback loop with other
// auto-responders. The source is part of the key so that a talkative APRS-IS
// feed cannot spend the allowance a question heard on the air needs: the two
// answers go to different places and neither costs the other any airtime.
// The first four are the general (broadcast) queries and also answerable
// when directed; everything after QUERY_TYPE_QRU is directed-only, which is
// how APRS101 chapter 15 splits them - a station only answers those on behalf
// of the operator who addressed it.
typedef enum {
    QUERY_TYPE_APRS = 0,
    QUERY_TYPE_WX,
    QUERY_TYPE_IGATE,
    QUERY_TYPE_QRU,      // ?QRU?
    QUERY_TYPE_POSITION, // ?APRSP
    QUERY_TYPE_STATUS,   // ?APRSS
    QUERY_TYPE_DIRECTS,  // ?APRSD
    QUERY_TYPE_HEARD,    // ?APRSH
    QUERY_TYPE_MESSAGES, // ?APRSM
    QUERY_TYPE_OBJECTS,  // ?APRSO
    QUERY_TYPE_TRACE,    // ?APRST / ?PING?
    QUERY_TYPE_COUNT
} query_type_t;

static int64_t s_lastBroadcastRespondSec[QUERY_TYPE_COUNT][QUERY_SRC_COUNT];

// Directed queries bypass the broadcast-type limiter above (they are
// explicitly addressed to this station) but get their own, tighter
// per-source limit so a single remote station can't hammer it. Small fixed
// table rather than a hash map: directed queries are rare traffic, and a
// full table simply means the oldest source gets recycled.
// Idle re-check cadence for the periodic capabilities beacon, matching the one
// the other periodic transmitters return when they are switched off: it is how
// long a web-admin change waits before the scheduler notices it.
#define QUERY_CAP_IDLE_RECHECK_S        5
#define QUERY_DIRECTED_TRACK_MAX        8
#define QUERY_DIRECTED_MIN_INTERVAL_SEC 5

typedef struct {
    char call[12];
    int64_t lastRespondSec;
} query_directed_track_t;

static query_directed_track_t s_directedTrack[QUERY_DIRECTED_TRACK_MAX];

// ---------------------------------------------------------------------------
// Deferred response queue
// ---------------------------------------------------------------------------

// Depth of the request queue drained by query_service(). Both rate limiters
// run before a request reaches it, so this depth only has to absorb the
// handful of distinct questions that can legitimately arrive between two
// scheduler passes; anything beyond it is dropped with a warning rather than
// growing the queue at the expense of a real answer.
#define QUERY_PENDING_MAX 8

// Room for the one piece of per-request text a query can carry: the callsign a
// "?APRSH" asks about, or the route a "?APRST" arrived by. No query carries
// both, so a single field serves either. A route longer than this is cut here
// instead of when the message text is built - the same "as much as fits" rule
// the answer itself would apply.
#define QUERY_DETAIL_LEN 64

typedef struct {
    query_type_t type;
    query_source_t source;         // where the query came from, and where its answer goes
    char fromCall[12];             // querying station; empty for a broadcast query
    char detail[QUERY_DETAIL_LEN]; // "?APRSH" target callsign or "?APRST" route, empty otherwise
} query_request_t;

// Ring of requests waiting to be answered: s_pending[s_pendingHead] is the
// oldest, and the queue holds s_pendingCount entries from there, wrapping at
// QUERY_PENDING_MAX. Producers are the tasks that receive traffic, the single
// consumer is the beacon scheduler task, and s_pendingLock covers both.
static query_request_t s_pending[QUERY_PENDING_MAX];
static uint8_t s_pendingHead = 0;
static uint8_t s_pendingCount = 0;
static SemaphoreHandle_t s_pendingLock = NULL;

void query_init(void) {
    memset(s_lastBroadcastRespondSec, 0, sizeof(s_lastBroadcastRespondSec));
    memset(s_directedTrack, 0, sizeof(s_directedTrack));
    memset(s_pending, 0, sizeof(s_pending));
    s_pendingHead = 0;
    s_pendingCount = 0;
    if (s_pendingLock == NULL)
        s_pendingLock = xSemaphoreCreateMutex();
}

void query_set_tx_handler(void (*handler)(const char *packet, size_t len, uint8_t channels)) {
    s_txHandler = handler;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Compares two callsigns ignoring any "-SSID" suffix, matching the same rule
// message.c's callsignBaseMatch() applies (a message/query addressed to
// "N0CALL", "N0CALL-0".."N0CALL-15" is treated as addressed to "N0CALL").
static bool baseCallMatch(const char *a, const char *b) {
    size_t na = 0, nb = 0;
    while (a[na] && a[na] != '-')
        na++;
    while (b[nb] && b[nb] != '-')
        nb++;
    if (na == 0 || na != nb)
        return false;
    return strncasecmp(a, b, na) == 0;
}

// True if the operator wants queries received on this source answered. The
// two switches name a source, not a destination: an answer always goes back
// the way its question came, so turning a source off is what guarantees that
// traffic from there cannot make this station transmit.
static bool querySourceEnabled(query_source_t source) {
    switch (source) {
        case QUERY_SRC_RF:
            return g_config.query_rf; // single-word read, benign if stale
        case QUERY_SRC_INET:
            return g_config.query_inet; // single-word read, benign if stale
        default:
            return false;
    }
}

// The one TX channel an answer to a query from this source is sent on, in the
// bitmask form message_set_tx_handler() defines.
static uint8_t querySourceChannel(query_source_t source) {
    return (source == QUERY_SRC_INET) ? (uint8_t)MSG_CHANNEL_INET : (uint8_t)MSG_CHANNEL_RF;
}

// Rate-limit gate for broadcast queries: true if it is fine to respond now
// (and, as a side effect, stamps the current time so the next check within
// g_config.query_min_interval_sec fails).
static bool broadcastRateLimitPass(query_type_t type, query_source_t source) {
    int64_t now = sched_mono_seconds();
    uint16_t minInterval = g_config.query_min_interval_sec;
    if (minInterval == 0)
        minInterval = 30; // sanity floor, matches the webconfig page's own default

    if (now - s_lastBroadcastRespondSec[type][source] < (int64_t)minInterval)
        return false;

    s_lastBroadcastRespondSec[type][source] = now;
    return true;
}

// Rate-limit gate for directed queries: tighter, per-source limit so one
// remote station addressing us repeatedly cannot hammer the responder even
// though directed queries bypass the broadcast-type limiter above.
static bool directedRateLimitPass(const char *fromCall) {
    int64_t now = sched_mono_seconds();

    int freeSlot = -1;
    int oldestSlot = 0;
    for (int i = 0; i < QUERY_DIRECTED_TRACK_MAX; i++) {
        if (s_directedTrack[i].call[0] == 0) {
            if (freeSlot < 0)
                freeSlot = i;
            continue;
        }
        if (strcasecmp(s_directedTrack[i].call, fromCall) == 0) {
            if (now - s_directedTrack[i].lastRespondSec < QUERY_DIRECTED_MIN_INTERVAL_SEC)
                return false;
            s_directedTrack[i].lastRespondSec = now;
            return true;
        }
        if (s_directedTrack[i].lastRespondSec < s_directedTrack[oldestSlot].lastRespondSec)
            oldestSlot = i;
    }

    // Unknown source: take a free slot, or recycle the least-recently-used
    // one if the table is full.
    int slot = (freeSlot >= 0) ? freeSlot : oldestSlot;
    strncpy(s_directedTrack[slot].call, fromCall, sizeof(s_directedTrack[slot].call) - 1);
    s_directedTrack[slot].call[sizeof(s_directedTrack[slot].call) - 1] = 0;
    s_directedTrack[slot].lastRespondSec = now;
    return true;
}

// Copies the path a received line travelled - the text between the '>' that
// closes the source call and the ':' that opens the information field, '*'
// markers included - into @p out. This has to be read while the received line
// is still in hand, so a traceroute answer built later still reports the route
// its query actually took. Leaves @p out empty when there is no line or it
// carries no path, which is what makes the answer report an unknown route.
static void extractRoute(const char *tnc2Line, char *out, size_t out_size) {
    out[0] = 0;

    const char *gt = (tnc2Line != NULL) ? strchr(tnc2Line, '>') : NULL;
    const char *colon = (gt != NULL) ? strchr(gt, ':') : NULL;
    if (gt == NULL || colon == NULL || colon <= gt + 1)
        return;

    size_t n = (size_t)(colon - gt - 1);
    if (n >= out_size)
        n = out_size - 1;
    memcpy(out, gt + 1, n);
    out[n] = 0;
}

// Parses the area-restricted form of the general "?APRS?" query, APRS101
// ch.15's "?APRS?LLLLLL,OOOOOO,RRRR": a signed six-digit latitude and a
// signed six-digit longitude, each in hundredths of a degree with no decimal
// point, followed by a range in miles of up to four digits, comma-separated
// and immediately following the keyword with no intervening space. Rejects
// anything that does not fit that fixed layout exactly, which is what makes a
// plain "?APRS?" - whose argument is empty - fall through as a non-area
// query rather than being coerced into one.
static bool parseAreaQuery(const char *arg, float *outLat, float *outLon, float *outRangeMiles) {
    if (arg == NULL)
        return false;

    const char *p = arg;
    bool neg = false;
    if (*p == '+' || *p == '-') {
        neg = (*p == '-');
        p++;
    }
    long latHundredths = 0;
    for (int i = 0; i < 6; i++, p++) {
        if (*p < '0' || *p > '9')
            return false;
        latHundredths = latHundredths * 10 + (*p - '0');
    }
    if (*p != ',')
        return false;
    p++;

    bool lonNeg = false;
    if (*p == '+' || *p == '-') {
        lonNeg = (*p == '-');
        p++;
    }
    long lonHundredths = 0;
    for (int i = 0; i < 6; i++, p++) {
        if (*p < '0' || *p > '9')
            return false;
        lonHundredths = lonHundredths * 10 + (*p - '0');
    }
    if (*p != ',')
        return false;
    p++;

    if (*p < '0' || *p > '9')
        return false;
    long rangeMiles = 0;
    int rangeDigits = 0;
    while (*p >= '0' && *p <= '9' && rangeDigits < 4) {
        rangeMiles = rangeMiles * 10 + (*p - '0');
        p++;
        rangeDigits++;
    }
    // The field ends the query: anything beyond the range digits (other than
    // the end of the info field) means this was not the fixed-width form
    // above, and is left unrecognized rather than guessed at.
    if (*p != 0 && *p != ' ')
        return false;

    *outLat = (neg ? -1.0f : 1.0f) * (float)latHundredths / 100.0f;
    *outLon = (lonNeg ? -1.0f : 1.0f) * (float)lonHundredths / 100.0f;
    *outRangeMiles = (float)rangeMiles;
    return true;
}

// True if an area-restricted "?APRS?LLLLLL,OOOOOO,RRRR" query's circle
// covers this station, so it should answer; also true for the plain,
// non-area form of the query, which every station answers regardless of
// distance. g_config.my_lat/my_lon must be configured for the area form to
// be evaluated - without a known position of our own, distance can't be
// judged, so the query is answered rather than silently dropped.
static bool areaQueryInRange(const char *arg) {
    float qLat, qLon, rangeMiles;
    if (!parseAreaQuery(arg, &qLat, &qLon, &rangeMiles))
        return true;

    float ownLat, ownLon;
    app_config_lock();
    ownLat = g_config.my_lat;
    ownLon = g_config.my_lon;
    app_config_unlock();
    if (ownLat == 0.0f && ownLon == 0.0f)
        return true;

    // RRRR is in miles per APRS101 ch.15; the shared helper works in km.
    static const float KM_PER_MILE = 1.609344f;
    float rangeKm = rangeMiles * KM_PER_MILE;
    float distKm = aprs_filter_haversine_km(ownLat, ownLon, qLat, qLon);
    return distKm <= rangeKm;
}

// Records one answer for query_service() to build and transmit from the beacon
// scheduler task, and wakes that task so the answer goes out on its next pass
// rather than whenever the periodic beacons happen to need servicing.
//
// A request identical to one already waiting is collapsed into it: every
// answer is built from live state (position, traffic counters, the heard
// table) at the moment it is sent, so a second copy would put the same
// information on the air twice for one operator's benefit. The source is part
// of what makes two requests identical, since two answers to the same question
// received on both channels go to two different places.
static void queueResponse(query_type_t type, query_source_t source, const char *fromCall, const char *detail) {
    if (s_pendingLock == NULL)
        return;
    if (xSemaphoreTake(s_pendingLock, pdMS_TO_TICKS(20)) != pdTRUE) {
        ESP_LOGW(TAG, "Query answer dropped - response queue busy");
        return;
    }

    const char *call = (fromCall != NULL) ? fromCall : "";
    const char *arg = (detail != NULL) ? detail : "";

    bool duplicate = false;
    for (uint8_t i = 0; i < s_pendingCount; i++) {
        const query_request_t *r = &s_pending[(s_pendingHead + i) % QUERY_PENDING_MAX];
        if (r->type == type && r->source == source && strcasecmp(r->fromCall, call) == 0 && strcmp(r->detail, arg) == 0) {
            duplicate = true;
            break;
        }
    }

    bool queued = false;
    bool full = false;
    if (!duplicate) {
        if (s_pendingCount < QUERY_PENDING_MAX) {
            query_request_t *r = &s_pending[(s_pendingHead + s_pendingCount) % QUERY_PENDING_MAX];
            memset(r, 0, sizeof(*r));
            r->type = type;
            r->source = source;
            strncpy(r->fromCall, call, sizeof(r->fromCall) - 1);
            strncpy(r->detail, arg, sizeof(r->detail) - 1);
            s_pendingCount++;
            queued = true;
        } else {
            full = true;
        }
    }

    xSemaphoreGive(s_pendingLock);

    if (queued)
        beacon_scheduler_wake();
    else if (full)
        ESP_LOGW(TAG, "Query answer dropped - %d responses already queued", QUERY_PENDING_MAX);
}

// Transmits an already-built TNC2 line back the way its question came, via the
// same channel-bitmask contract message_set_tx_handler() defines. The source
// switches were already applied when the request was accepted, so everything
// that reaches here has a channel to go out on.
static void txPacket(const char *packet, size_t len, query_source_t source) {
    if (!s_txHandler) {
        ESP_LOGW(TAG, "No TX handler registered, dropping: %s", packet);
        return;
    }
    s_txHandler(packet, len, querySourceChannel(source));
}

// The two configuration items an on-air answer's path is built from, copied
// out of g_config in one go. The preset slots are 72-byte strings that the web
// task rewrites in place on a settings save, so a builder reading them
// directly could see one mid-strcpy - torn, or momentarily without its NUL.
// Taking the selection bitmask in the same copy also keeps the two consistent
// with each other: the mask cannot describe one generation of the presets
// while the strings come from the next.
typedef struct {
    uint8_t mask;
    char preset[APRS_PATH_PRESET_COUNT][APRS_PATH_PRESET_SIZE];
} query_path_cfg_t;

// Fills the snapshot. The lock is a leaf lock: it covers the two copies and
// nothing else, and the suffix is built after it has been released.
static void queryPathConfig(query_path_cfg_t *cfg) {
    app_config_lock();
    cfg->mask = g_config.igate_path;
    memcpy(cfg->preset, g_config.path, sizeof(cfg->preset));
    app_config_unlock();
}

// Path suffix for an answer built from a snapshot the caller already holds,
// used where the same selection also has to be measured in hops.
static void queryPathSuffixFrom(const query_path_cfg_t *cfg, query_source_t source, char *out, size_t outMax) {
    if (source == QUERY_SRC_INET) {
        snprintf(out, outMax, "%s", APRS_PATH_TCPIP_SUFFIX);
        return;
    }
    aprs_path_build_suffix(cfg->mask, cfg->preset, out, outMax);
}

// Path suffix for an answer leaving on the channel its question arrived on.
//
// An answer put on the air carries the IGate page's digipeater selection, the
// same path the IGate beacon uses, so it reaches as far as the query did. An
// answer injected into APRS-IS traverses no repeaters at all, and
// aprs-is.net/Connecting.aspx requires a client's own traffic to carry
// APRS_PATH_TCPIP_SUFFIX and nothing else, so a digipeater path there would
// describe hops that never happened - which is why the APRS-IS leg is answered
// before any configuration is read.
static void queryPathSuffix(query_source_t source, char *out, size_t outMax) {
    if (source == QUERY_SRC_INET) {
        snprintf(out, outMax, "%s", APRS_PATH_TCPIP_SUFFIX);
        return;
    }

    query_path_cfg_t cfg;
    queryPathConfig(&cfg);
    queryPathSuffixFrom(&cfg, source, out, outMax);
}

// Resolves the callsign this station answers under: the IGate APRS callsign
// plus its SSID, the same identity the position/status/capability replies are
// built from. Returns false when none is configured.
static bool resolveOwnCall(char *out, size_t out_size) {
    out[0] = 0;
    app_config_lock();
    {
        if (g_config.aprs_mycall[0]) {
            if (g_config.aprs_ssid > 0)
                snprintf(out, out_size, "%s-%d", g_config.aprs_mycall, (int)g_config.aprs_ssid);
            else
                snprintf(out, out_size, "%s", g_config.aprs_mycall);
        }
    }
    app_config_unlock();
    return out[0] != 0;
}

// Builds and transmits an APRS text message carrying a query answer back to
// the station that asked. The addressee field is the fixed 9 characters the
// message format requires, space-padded; no message number is appended, so no
// ack is solicited - a query answer is informational and the querying station
// has nothing to acknowledge. The message goes back on the channel the query
// was received on, so an operator who asked over APRS-IS is answered there.
static void txMessageTo(const char *toCall, const char *text, query_source_t source) {
    char myCall[16];
    if (!resolveOwnCall(myCall, sizeof(myCall))) {
        ESP_LOGW(TAG, "Query answer not sent - no IGate callsign configured");
        return;
    }

    char addr[10];
    memset(addr, ' ', 9);
    addr[9] = 0;
    size_t n = strlen(toCall);
    memcpy(addr, toCall, n > 9 ? 9 : n);

    char path[80];
    queryPathSuffix(source, path, sizeof(path));

    char packet[APRS_TNC2_BUF_SIZE];
    int len = snprintf(packet, sizeof(packet), "%s>%s%s::%s:%s", myCall, QUERY_DEST, path, addr, text);
    if (len < 0 || (size_t)len >= sizeof(packet) || len > APRS_TNC2_MAX_LEN) {
        ESP_LOGW(TAG, "Query answer to %s not sent - built line too long", toCall);
        return;
    }

    txPacket(packet, (size_t)len, source);
    ESP_LOGI(TAG, "Query answered to %s: %s", toCall, packet);
}

// ---------------------------------------------------------------------------
// Response builders
// ---------------------------------------------------------------------------

// "?APRS?" -> the station's own position/status packet, byte-for-byte the
// same the IGate position beacon sends on the channel this answer leaves on
// (the path is the one part that differs between the two channels, and
// queryPathSuffix() picks it the same way the beacon does).
static void respondAPRS(query_source_t source) {
    char path[80];
    queryPathSuffix(source, path, sizeof(path));

    char packet[APRS_TNC2_BUF_SIZE];
    int len = beacon_build_igate_position_packet(path, packet, sizeof(packet));
    if (len <= 0) {
        ESP_LOGW(TAG, "?APRS? query not answered - no IGate callsign/position configured, or the line did not fit");
        return;
    }
    txPacket(packet, (size_t)len, source);
    ESP_LOGI(TAG, "?APRS? query answered: %s", packet);
}

// "?WX?" -> the latest cached weather report, if ENABLE_WEATHER and a
// report exists.
static void respondWX(query_source_t source) {
    char path[80];
    queryPathSuffix(source, path, sizeof(path));

    char packet[APRS_TNC2_BUF_SIZE];
    int len = weather_build_report_packet(path, packet, sizeof(packet));
    if (len <= 0) {
        ESP_LOGW(TAG, "?WX? query not answered - no Weather/APRS callsign configured, no reading cached yet, or the line did not fit");
        return;
    }
    txPacket(packet, (size_t)len, source);
    ESP_LOGI(TAG, "?WX? query answered: %s", packet);
}

// Builds the "<IGATE,MSG_CNT=n,LOC_CNT=n>" Station Capabilities line APRS101
// ch.15 defines, as a complete TNC2 packet ready for the channel named by
// `source`, and returns its length (0 when it could not be built).
//
// MSG_CNT is the running count of APRS message packets this gateway has
// passed in either direction (igate_stats_t::msgCount, bumped wherever a ':'
// data type identifier is gated), not a tally of all gated traffic.
//
// LOC_CNT is a live figure rather than a running total: the number of
// distinct stations currently in the local heard list whose used-hop count is
// within reach of this IGate's own TX path, which is what APRS101 ch.15
// defines it as - "the number of stations heard with this number of used
// digipeater addresses or fewer". A station heard through more hops than the
// configured igate_path would use is left out, since an IS->RF message for it
// would never reach it. Only the stations heard off the air count as local at
// all, so the rows the APRS-IS feed contributed are excluded regardless of
// hop count.
//
// The capability list is open-ended in ch.15, so the two mandatory tokens are
// followed by whatever the operator typed into g_config.query_cap_extra -
// already stripped of the CR/LF and the ',' and '>' delimiters that would
// break the line (app_config_query_cap_extra_sanitize()). Left empty, the
// line carries the gateway token alone: each of this station's other roles
// already announces itself where a receiver looks for it - the digipeater by
// the path it puts its callsign into, the weather station by its own symbol
// and report, the telemetry source by its parameter messages.
//
// Both callers share this one builder so the line an operator sees in reply
// to "?IGATE?" and the one the periodic beacon sends are the same packet,
// counters and all.
static size_t buildCapabilitiesPacket(query_source_t source, char *packet, size_t packet_size) {
    packet[0] = 0;

    char callField[16];
    if (!resolveOwnCall(callField, sizeof(callField)))
        return 0;

    igate_stats_t stats = igate_get_stats();

    // One snapshot feeds both the suffix and the hop count, so the LOC_CNT
    // figure counts stations reachable over exactly the path this line
    // carries even if a settings save lands between the two uses.
    query_path_cfg_t cfg;
    queryPathConfig(&cfg);

    char path[80];
    queryPathSuffixFrom(&cfg, source, path, sizeof(path));

    uint8_t txHops = app_config_path_hop_count(cfg.mask, cfg.preset);

    char extra[QUERY_CAP_EXTRA_SIZE];
    app_config_lock();
    memcpy(extra, g_config.query_cap_extra, sizeof(extra));
    app_config_unlock();
    extra[sizeof(extra) - 1] = 0;

    char info[QUERY_CAP_EXTRA_SIZE + 64];
    snprintf(info, sizeof(info), "<IGATE,MSG_CNT=%u,LOC_CNT=%u%s%s>", (unsigned)stats.msgCount, (unsigned)lastheard_station_count(true, txHops),
             extra[0] ? "," : "", extra);

    int n = snprintf(packet, packet_size, "%s>%s%s:%s", callField, QUERY_DEST, path, info);
    if (n < 0 || (size_t)n >= packet_size || n > APRS_TNC2_MAX_LEN)
        return 0;

    return (size_t)n;
}

// "?IGATE?" -> the Station Capabilities line, built above and transmitted on
// the channel the question arrived on.
static void respondIGate(query_source_t source) {
    if (!g_config.igate_en) { // single-word read, benign if stale
        ESP_LOGD(TAG, "?IGATE? query ignored - IGate service is disabled");
        return;
    }

    char packet[APRS_TNC2_BUF_SIZE];
    size_t len = buildCapabilitiesPacket(source, packet, sizeof(packet));
    if (len == 0) {
        ESP_LOGW(TAG, "?IGATE? query not answered - no IGate callsign configured, or the line did not fit");
        return;
    }

    txPacket(packet, len, source);
    ESP_LOGI(TAG, "?IGATE? query answered: %s", packet);
}

// "?APRSS" -> the station's own status report, byte-for-byte the same the
// IGate status beacon sends on the channel this answer leaves on (including
// the Maidenhead locator block when that option is on).
static void respondStatus(query_source_t source) {
    char path[80];
    queryPathSuffix(source, path, sizeof(path));

    char packet[APRS_TNC2_BUF_SIZE];
    int len = beacon_build_igate_status_packet(path, packet, sizeof(packet));
    if (len <= 0) {
        ESP_LOGW(TAG, "?APRSS query not answered - no IGate callsign/status text configured, or the line did not fit");
        return;
    }
    txPacket(packet, (size_t)len, source);
    ESP_LOGI(TAG, "?APRSS query answered: %s", packet);
}

// "?APRSD" -> the list of stations heard here without a digipeater in
// between, as the "Directs=" message APRS101 ch.15 defines. An empty list is
// still answered: "heard nobody directly" is the true state of a station that
// has just booted or sits in a quiet area, and silence would be
// indistinguishable from the query never arriving.
static void respondDirects(const char *fromCall, query_source_t source) {
    // Sized so the whole answer stays inside one standard message text
    // (APRS_MSG_TEXT_STD_MAX); lastheard_directs() drops whole callsigns
    // rather than truncating one, so a busy station simply reports the most
    // recent ones.
    char list[APRS_MSG_TEXT_STD_MAX - 8];
    int n = lastheard_directs(list, sizeof(list));

    char text[APRS_MSG_TEXT_STD_MAX + 1];
    if (n > 0)
        snprintf(text, sizeof(text), "Directs=%s", list);
    else
        snprintf(text, sizeof(text), "Directs=");

    txMessageTo(fromCall, text, source);
}

// "?APRSH <call>" -> what this station knows about hearing <call>.
//
// APRS101 ch.15 defines the answer as a single-line message "Hrd: " followed
// by the packet count for each of the last LASTHEARD_HEARD_HOURS clock hours,
// grouped six-per-period with "." between groups (matching the worked example
// in the spec: "Hrd: 14 15 4 . 10 6 7 ."). The first count is the current
// clock hour and each one after it is one hour further back, taken straight
// from that station's row in components/lastheard, whose hourly histogram
// exists for exactly this query. A station that is not in the table is
// reported as not heard.
static void respondHeard(const char *fromCall, const char *arg, query_source_t source) {
    char target[12] = { 0 };
    size_t n = 0;
    while (arg[n] && arg[n] != ' ' && n < sizeof(target) - 1) {
        target[n] = arg[n];
        n++;
    }

    char text[APRS_MSG_TEXT_STD_MAX + 1];
    if (target[0] == 0) {
        // No callsign given: the query is meaningless without one, so say so
        // rather than answering about an empty callsign.
        //
        // The wording deliberately keeps the keyword away from the first
        // character. Every answer here leaves as an APRS text message, and a
        // message payload that opens with '?' is itself a directed query by
        // APRS101 ch.15: a peer running a responder would parse this reply as
        // a fresh "?APRSH" and answer about whatever word followed it. Any
        // phrasing works as long as it does not start with '?'.
        snprintf(text, sizeof(text), "Usage: ?APRSH <call>");
        txMessageTo(fromCall, text, source);
        return;
    }

    uint16_t hourly[LASTHEARD_HEARD_HOURS];
    if (!lastheard_heard_history(target, hourly)) {
        snprintf(text, sizeof(text), "%s not heard", target);
        txMessageTo(fromCall, text, source);
        return;
    }

    // Build "Hrd: h0 h1 h2 h3 h4 h5 . h6 h7 h8 h9 h10 h11 . h12 ... h17 ."
    // one hour at a time so a too-long callsign or a bigger LASTHEARD_HEARD_HOURS
    // can never overflow the message text - the format simply gets cut at the
    // last hour that still fits, the same "whole units only" rule the other
    // list-style responses in this file already follow.
    int pos = snprintf(text, sizeof(text), "Hrd:");
    for (size_t i = 0; i < LASTHEARD_HEARD_HOURS && pos > 0 && (size_t)pos < sizeof(text); i++) {
        int n2 = snprintf(text + pos, sizeof(text) - (size_t)pos, " %u", (unsigned int)hourly[i]);
        if (n2 < 0 || (size_t)pos + (size_t)n2 >= sizeof(text))
            break;
        pos += n2;
        if ((i + 1) % 6 == 0) {
            n2 = snprintf(text + pos, sizeof(text) - (size_t)pos, " .");
            if (n2 < 0 || (size_t)pos + (size_t)n2 >= sizeof(text))
                break;
            pos += n2;
        }
    }
    txMessageTo(fromCall, text, source);
}

// "?APRSM" -> re-send the messages this station is still holding for the
// querying operator, up to the MSG_QUERY_BURST_MAX frames one query is worth;
// anything beyond that stays queued and keeps going out on the messaging
// engine's own retry schedule. Nothing pending is reported explicitly, for the
// same reason an empty "Directs=" is sent: the operator asked a question and an
// answer of "none" is information. The messages themselves are real traffic
// this station owes the operator, so they go out on the channels the Message
// page selects; only the "nothing pending" reply follows the query's source.
static void respondMessages(const char *fromCall, query_source_t source) {
    int sent = message_send_pending_to(fromCall);
    if (sent == 0)
        txMessageTo(fromCall, "No messages pending", source);
}

// "?APRSO" -> re-announce the Objects/Items this station originates. The
// elements go out from the beacon scheduler task (see
// objitems_request_transmit_all()), not from here, so answering a query never
// occupies the radio RX task for the length of a transmission burst. They are
// this station's own announcements, so each one is routed by its own "send via"
// configuration; the query's source only governs the reply built here.
static void respondObjects(const char *fromCall, query_source_t source) {
#ifdef ENABLE_OBJECTS_ITEMS
    (void)source;
    objitems_request_transmit_all();
    ESP_LOGI(TAG, "?APRSO query from %s: all Objects/Items queued for transmission", fromCall);
#else
    txMessageTo(fromCall, "No objects", source);
#endif
}

// "?QRU?" -> the group-membership roll call APRS101 ch.15 defines: which of
// this station's own Objects/Items carry a non-empty QRU tag (see
// objects_items.h), reported as "<tag>:<name>" pairs in a status packet so
// every station listening for the roll call sees the answer, not just the one
// that asked - "?QRU?" is a general query, like "?APRS?"/"?WX?"/"?IGATE?"
// above, and general queries have no fromCall to address a reply to.
// Objects/Items are read straight from storage via objitems_load() - the same
// on-demand read objitems_service() itself does - rather than through
// objitems_request_transmit_all(), since this answer only ever reports the
// QRU tag, never a full position/status report for each element.
//
// An empty result is still answered, for the same reason "?APRSD" answers an
// empty "Directs=": "no group members configured" is the true state of a
// station with no tagged Objects/Items, and silence would be indistinguishable
// from the query never arriving.
static void respondQRU(query_source_t source) {
    char myCall[16];
    if (!resolveOwnCall(myCall, sizeof(myCall))) {
        ESP_LOGW(TAG, "?QRU? query not answered - no IGate callsign configured");
        return;
    }

    char info[64];
    int pos = snprintf(info, sizeof(info), ">QRU:");
#ifdef ENABLE_OBJECTS_ITEMS
    objitems_t set;
    objitems_load(&set); // missing/corrupt file already yields all-disabled defaults

    bool any = false;
    for (int i = 0; i < OBJITEM_COUNT && pos > 0 && (size_t)pos < sizeof(info); i++) {
        const objitem_t *b = &set.item[i];
        if (!b->enable || !b->name[0] || !b->qru[0])
            continue;

        int n = snprintf(info + pos, sizeof(info) - (size_t)pos, "%s%s:%s", any ? "," : "", b->qru, b->name);
        if (n < 0 || (size_t)pos + (size_t)n >= sizeof(info))
            break;
        pos += n;
        any = true;
    }
    if (!any && pos > 0 && (size_t)pos < sizeof(info))
        snprintf(info + pos, sizeof(info) - (size_t)pos, "none");
#else
    if (pos > 0 && (size_t)pos < sizeof(info))
        snprintf(info + pos, sizeof(info) - (size_t)pos, "none");
#endif

    char path[80];
    queryPathSuffix(source, path, sizeof(path));

    char packet[APRS_TNC2_BUF_SIZE];
    int len = snprintf(packet, sizeof(packet), "%s>%s%s:%s", myCall, QUERY_DEST, path, info);
    if (len < 0 || (size_t)len >= sizeof(packet) || len > APRS_TNC2_MAX_LEN) {
        ESP_LOGW(TAG, "?QRU? query not answered - built line too long");
        return;
    }

    txPacket(packet, (size_t)len, source);
    ESP_LOGI(TAG, "?QRU? query answered: %s", packet);
}

// "?APRST" / "?PING?" -> report the route the query itself took to get here,
// which is what lets the querying operator see which digipeaters are in play
// between the two stations. The route is the one extractRoute() read off the
// received line, '*' markers included, so a repeater that actually handled the
// frame is distinguishable from one that was merely requested.
static void respondTrace(const char *fromCall, const char *route, query_source_t source) {
    char text[APRS_MSG_TEXT_STD_MAX + 1];

    if (route == NULL || route[0] == 0)
        snprintf(text, sizeof(text), "%s via ?", fromCall);
    else
        snprintf(text, sizeof(text), "%s>%s", fromCall, route);

    txMessageTo(fromCall, text, source);
}

// ---------------------------------------------------------------------------
// Deferred execution
// ---------------------------------------------------------------------------

// Runs one queued request: this is where the answer is actually built and put
// on the air, and it only ever executes on the beacon scheduler task.
static void runRequest(const query_request_t *req) {
    switch (req->type) {
        case QUERY_TYPE_APRS:
        case QUERY_TYPE_POSITION:
            respondAPRS(req->source);
            return;
        case QUERY_TYPE_WX:
            respondWX(req->source);
            return;
        case QUERY_TYPE_IGATE:
            respondIGate(req->source);
            return;
        case QUERY_TYPE_QRU:
            respondQRU(req->source);
            return;
        case QUERY_TYPE_STATUS:
            respondStatus(req->source);
            return;
        case QUERY_TYPE_DIRECTS:
            respondDirects(req->fromCall, req->source);
            return;
        case QUERY_TYPE_HEARD:
            respondHeard(req->fromCall, req->detail, req->source);
            return;
        case QUERY_TYPE_MESSAGES:
            respondMessages(req->fromCall, req->source);
            return;
        case QUERY_TYPE_OBJECTS:
            respondObjects(req->fromCall, req->source);
            return;
        case QUERY_TYPE_TRACE:
            respondTrace(req->fromCall, req->detail, req->source);
            return;
        default:
            return;
    }
}

// Monotonic second at which the periodic capabilities beacon is next due. 0
// means "due now", which is also what disabling the beacon resets it to, so
// re-enabling transmits one straight away rather than after a full interval of
// silence.
static int64_t s_cap_next_due = 0;

uint32_t query_capabilities_service(void) {
    bool enabled;
    uint32_t interval;
    bool toRf, toInet;

    app_config_lock();
    enabled = g_config.query_cap_beacon_en && g_config.igate_en;
    interval = g_config.query_cap_interval_sec;
    toRf = g_config.query_cap_rf;
    toInet = g_config.query_cap_inet;
    app_config_unlock();

    if (!enabled || (!toRf && !toInet)) {
        s_cap_next_due = 0;
        return QUERY_CAP_IDLE_RECHECK_S;
    }

    int64_t now = sched_mono_seconds();
    if (now >= s_cap_next_due) {
        // One packet per leg: the path differs between them, so the line an
        // APRS-IS reader sees carries the TCPIP suffix while the RF one
        // carries the digipeater path, exactly as the query answer does.
        char packet[APRS_TNC2_BUF_SIZE];
        if (toRf) {
            size_t len = buildCapabilitiesPacket(QUERY_SRC_RF, packet, sizeof(packet));
            if (len > 0) {
                txPacket(packet, len, QUERY_SRC_RF);
                ESP_LOGI(TAG, "Capabilities beacon (RF): %s", packet);
            } else {
                ESP_LOGW(TAG, "Capabilities beacon not sent on RF - no IGate callsign configured, or the line did not fit");
            }
        }
        if (toInet) {
            size_t len = buildCapabilitiesPacket(QUERY_SRC_INET, packet, sizeof(packet));
            if (len > 0) {
                txPacket(packet, len, QUERY_SRC_INET);
                ESP_LOGI(TAG, "Capabilities beacon (INET): %s", packet);
            } else {
                ESP_LOGW(TAG, "Capabilities beacon not sent to APRS-IS - no IGate callsign configured, or the line did not fit");
            }
        }

        s_cap_next_due = now + (int64_t)beacon_scheduler_jitter(sched_clamp_interval(interval, QUERY_CAP_INTERVAL_S_MIN, QUERY_CAP_INTERVAL_S_DEFAULT));
    }

    int64_t rem = s_cap_next_due - now;
    if (rem < 1)
        rem = 1;
    return (uint32_t)rem;
}

void query_service(void) {
    if (s_pendingLock == NULL)
        return;

    for (;;) {
        query_request_t req;

        if (xSemaphoreTake(s_pendingLock, pdMS_TO_TICKS(50)) != pdTRUE)
            return;
        if (s_pendingCount == 0) {
            xSemaphoreGive(s_pendingLock);
            return;
        }
        req = s_pending[s_pendingHead];
        s_pendingHead = (uint8_t)((s_pendingHead + 1) % QUERY_PENDING_MAX);
        s_pendingCount--;
        xSemaphoreGive(s_pendingLock);

        // The lock is released across the build and the transmission, which
        // together can occupy the radio for the length of a burst: a task
        // receiving traffic must be able to queue the next request throughout.
        runRequest(&req);

        ESP_LOGD(TAG, "Query answered from the scheduler task, stack free: %u bytes", (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
    }
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

// Matches a query keyword at the start of `info` (already known to start with
// '?') against the table this responder implements, and reports where the
// keyword's argument begins.
//
// `directed` selects the table: a general query is one of the four APRS101
// chapter 15 defines as broadcast, while a directed query may additionally be
// any of the per-station ones.
//
// Matching is whole-keyword and first-hit-wins: the table is walked in order
// and the first entry whose full keyword matches the head of `info` is
// returned, with no attempt to prefer a longer entry further down. That rule
// constrains what may be added here - no keyword may be a prefix of another
// one, or the shorter of the pair would shadow the longer for every query.
// The set below satisfies it: no keyword is a prefix of any other, the eight
// that share the "?APRS" stem being told apart by their sixth character
// ("?APRS?" against "?APRSP", "?APRST" against its "?PING?" alias).
static bool matchQueryType(const char *info, bool directed, query_type_t *type, const char **arg) {
    static const struct {
        const char *keyword;
        query_type_t type;
        bool directedOnly;
    } table[] = {
        { "?APRS?", QUERY_TYPE_APRS, false },    { "?IGATE?", QUERY_TYPE_IGATE, false }, { "?WX?", QUERY_TYPE_WX, false },
        { "?QRU?", QUERY_TYPE_QRU, false },      { "?APRSD", QUERY_TYPE_DIRECTS, true }, { "?APRSH", QUERY_TYPE_HEARD, true },
        { "?APRSM", QUERY_TYPE_MESSAGES, true }, { "?APRSO", QUERY_TYPE_OBJECTS, true }, { "?APRSP", QUERY_TYPE_POSITION, true },
        { "?APRSS", QUERY_TYPE_STATUS, true },   { "?APRST", QUERY_TYPE_TRACE, true },   { "?PING?", QUERY_TYPE_TRACE, true },
    };

    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (table[i].directedOnly && !directed)
            continue;
        size_t kwLen = strlen(table[i].keyword);
        if (strncasecmp(info, table[i].keyword, kwLen) != 0)
            continue;
        *type = table[i].type;
        if (arg) {
            const char *p = info + kwLen;
            while (*p == ' ')
                p++;
            *arg = p;
        }
        return true;
    }
    return false;
}

// True if the query type is enabled by the operator. The three original
// general types keep their own per-type switches; "?QRU?" and the
// directed-only set share g_config.query_ext_en, since they are all answers
// about this station's own configuration (Objects/Items membership, traffic,
// heard history) and an operator either wants the station answering that
// class of question or not.
static bool queryTypeEnabled(query_type_t type) {
    switch (type) {
        case QUERY_TYPE_APRS:
            return g_config.query_aprs_en; // single-word read, benign if stale
        case QUERY_TYPE_WX:
            return g_config.query_wx_en; // single-word read, benign if stale
        case QUERY_TYPE_IGATE:
            return g_config.query_igate_en; // single-word read, benign if stale
        case QUERY_TYPE_QRU:
        case QUERY_TYPE_POSITION:
        case QUERY_TYPE_STATUS:
        case QUERY_TYPE_DIRECTS:
        case QUERY_TYPE_HEARD:
        case QUERY_TYPE_MESSAGES:
        case QUERY_TYPE_OBJECTS:
        case QUERY_TYPE_TRACE:
            return g_config.query_ext_en; // single-word read, benign if stale
        default:
            return false;
    }
}

// Queues one recognized broadcast query type for answering, gated by its own
// enable flag and the broadcast rate limiter for this type and source. Both
// gates run here, on the receiving task, so a flooded channel costs a string
// compare and a timestamp check rather than a queue slot.
//
// @p arg is the text following the keyword, as matchQueryType() reports it;
// only QUERY_TYPE_APRS inspects it, to recognize APRS101 ch.15's
// area-restricted form and stay silent when this station falls outside the
// circle it names.
static void dispatchBroadcast(query_type_t type, query_source_t source, const char *arg) {
    if (!queryTypeEnabled(type))
        return;
    if (!broadcastRateLimitPass(type, source))
        return;

    switch (type) {
        case QUERY_TYPE_APRS:
            if (!areaQueryInRange(arg))
                return;
            queueResponse(type, source, "", "");
            return;
        case QUERY_TYPE_WX:
        case QUERY_TYPE_IGATE:
        case QUERY_TYPE_QRU:
            queueResponse(type, source, "", "");
            return;
        default:
            return;
    }
}

void query_process(const char *tnc2Line, query_source_t source) {
    if (!g_config.query_en) // single-word read, benign if stale
        return;
    // A general query is broadcast traffic: the same "?APRS?" is heard on the
    // air and carried by the APRS-IS backbone, and only the operator's switch
    // for the source it arrived on decides whether this station answers it.
    if (!querySourceEnabled(source))
        return;
    if (tnc2Line == NULL)
        return;

    const char *colon = strchr(tnc2Line, ':');
    if (colon == NULL || colon[1] == 0)
        return;

    const char *info = colon + 1;
    if (info[0] != '?')
        return;

    query_type_t type;
    const char *arg = NULL;
    if (!matchQueryType(info, false, &type, &arg))
        return;

    dispatchBroadcast(type, source, arg);
}

void query_process_directed(const char *fromCall, const char *toCall, const char *text, const char *tnc2Line, query_source_t source) {
    if (!g_config.query_en || !g_config.query_directed_en) // single-word reads, benign if stale
        return;
    if (!querySourceEnabled(source))
        return;
    if (fromCall == NULL || toCall == NULL || text == NULL || text[0] != '?')
        return;

    // Snapshot the own callsign under the lock before comparing against it:
    // toCall is matched against a copy rather than g_config.aprs_mycall
    // directly, since a concurrent web save rewrites that field in place and
    // baseCallMatch() would otherwise be able to read it torn, or momentarily
    // without its NUL.
    char cfg_mycall[sizeof(g_config.aprs_mycall)];
    app_config_lock();
    memcpy(cfg_mycall, g_config.aprs_mycall, sizeof(cfg_mycall));
    app_config_unlock();
    cfg_mycall[sizeof(cfg_mycall) - 1] = 0;

    if (!baseCallMatch(toCall, cfg_mycall))
        return;

    query_type_t type;
    const char *arg = NULL;
    if (!matchQueryType(text, true, &type, &arg))
        return;

    if (!queryTypeEnabled(type))
        return;

    if (!directedRateLimitPass(fromCall))
        return;

    // Directed replies are queued the same way broadcast ones are, without
    // re-applying the broadcast rate limiter - the per-source directed limiter
    // above already governs this response. The position/status/weather/
    // capability answers go out as ordinary reports, which is what the
    // querying station is listening for anyway; the list-style answers are
    // addressed messages, since they only mean anything to the operator who
    // asked. Which of the two a type produces is decided in runRequest().
    //
    // Everything the answer needs beyond the type and the asker is carried in
    // one text field, captured now while the received line and the parsed
    // argument are still in scope.
    char detail[QUERY_DETAIL_LEN];
    detail[0] = 0;
    if (type == QUERY_TYPE_HEARD) {
        if (arg != NULL) {
            strncpy(detail, arg, sizeof(detail) - 1);
            detail[sizeof(detail) - 1] = 0;
        }
    } else if (type == QUERY_TYPE_TRACE) {
        extractRoute(tnc2Line, detail, sizeof(detail));
    }

    queueResponse(type, source, fromCall, detail);
}
