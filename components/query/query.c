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
// Recognizes the three general queries APRS101 chapter 15 defines - "?APRS?",
// "?WX?" and "?IGATE?" - in broadcast traffic, and the full directed set
// ("?APRSD", "?APRSH", "?APRSM", "?APRSO", "?APRSP", "?APRSS", "?APRST" and
// its "?PING?" alias) when addressed to this station, then transmits the
// matching response.
//
// Position, status and weather answers reuse the existing beacon builders, so
// a reply is byte-for-byte consistent with what the periodic beacons would
// send. The list-style answers (directs, heard, traceroute) are returned as
// APRS text messages addressed back to the querying station, which is the
// form chapter 15 specifies for a directed query.
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
#include "aprs_path.h"        // aprs_path_build_suffix_from_config()
#include "aprs_service.h"     // APRS_TNC2_BUF_SIZE / APRS_TNC2_MAX_LEN
#include "beacon.h"           // beacon_build_igate_position_packet() / beacon_build_igate_status_packet()
#include "beacon_scheduler.h" // beacon_scheduler_wake()
#include "igate.h"            // igate_get_stats()
#include "lastheard.h"        // lastheard_directs() / lastheard_heard_history()
#include "message.h"          // MSG_CHANNEL_RF / MSG_CHANNEL_INET / message_send_pending_to()
#include "query.h"
#include "sched_time.h" // sched_mono_seconds()
#include "weather.h"    // weather_build_report_packet()
#ifdef ENABLE_OBJECTS_ITEMS
#include "objects_items.h" // objitems_request_transmit_all()
#endif

static const char *TAG = "query";

// Same software-identifier destination call used by the message and beacon
// components, for consistency across the firmware's own-originated traffic.
#define QUERY_DEST "APE32L"

static void (*s_txHandler)(const char *packet, size_t len, uint8_t channels) = NULL;

// One "last responded" monotonic timestamp per broadcast query type, so a
// misbehaving network flooding "?APRS?" broadcasts cannot make this station
// answer more than once per g_config.query_min_interval_sec - both to save
// airtime and to avoid a feedback loop with other auto-responders.
// The first three are the general (broadcast) queries and also answerable
// when directed; everything after QUERY_TYPE_IGATE is directed-only, which is
// how APRS101 chapter 15 splits them - a station only answers those on behalf
// of the operator who addressed it.
typedef enum {
    QUERY_TYPE_APRS = 0,
    QUERY_TYPE_WX,
    QUERY_TYPE_IGATE,
    QUERY_TYPE_POSITION, // ?APRSP
    QUERY_TYPE_STATUS,   // ?APRSS
    QUERY_TYPE_DIRECTS,  // ?APRSD
    QUERY_TYPE_HEARD,    // ?APRSH
    QUERY_TYPE_MESSAGES, // ?APRSM
    QUERY_TYPE_OBJECTS,  // ?APRSO
    QUERY_TYPE_TRACE,    // ?APRST / ?PING?
    QUERY_TYPE_COUNT
} query_type_t;

static int64_t s_lastBroadcastRespondSec[QUERY_TYPE_COUNT];

// Directed queries bypass the broadcast-type limiter above (they are
// explicitly addressed to this station) but get their own, tighter
// per-source limit so a single remote station can't hammer it. Small fixed
// table rather than a hash map: directed queries are rare traffic, and a
// full table simply means the oldest source gets recycled.
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

// Rate-limit gate for broadcast queries: true if it is fine to respond now
// (and, as a side effect, stamps the current time so the next check within
// g_config.query_min_interval_sec fails).
static bool broadcastRateLimitPass(query_type_t type) {
    int64_t now = sched_mono_seconds();
    uint16_t minInterval = g_config.query_min_interval_sec;
    if (minInterval == 0)
        minInterval = 30; // sanity floor, matches the webconfig page's own default

    if (now - s_lastBroadcastRespondSec[type] < (int64_t)minInterval)
        return false;

    s_lastBroadcastRespondSec[type] = now;
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

// Records one answer for query_service() to build and transmit from the beacon
// scheduler task, and wakes that task so the answer goes out on its next pass
// rather than whenever the periodic beacons happen to need servicing.
//
// A request identical to one already waiting is collapsed into it: every
// answer is built from live state (position, traffic counters, the heard
// table) at the moment it is sent, so a second copy would put the same
// information on the air twice for one operator's benefit.
static void queueResponse(query_type_t type, const char *fromCall, const char *detail) {
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
        if (r->type == type && strcasecmp(r->fromCall, call) == 0 && strcmp(r->detail, arg) == 0) {
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

// Transmits an already-built TNC2 line on RF and/or INET per
// g_config.query_rf / g_config.query_inet, via the same channel-bitmask
// contract message_set_tx_handler() defines.
static void txPacket(const char *packet, size_t len) {
    if (!s_txHandler) {
        ESP_LOGW(TAG, "No TX handler registered, dropping: %s", packet);
        return;
    }
    uint8_t channels = 0;
    if (g_config.query_rf)
        channels |= MSG_CHANNEL_RF;
    if (g_config.query_inet)
        channels |= MSG_CHANNEL_INET;
    if (channels == 0)
        return;
    s_txHandler(packet, len, channels);
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
// has nothing to acknowledge.
static void txMessageTo(const char *toCall, const char *text) {
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
    aprs_path_build_suffix_from_config(g_config.igate_path, path, sizeof(path));

    char packet[APRS_TNC2_BUF_SIZE];
    int len = snprintf(packet, sizeof(packet), "%s>%s%s::%s:%s", myCall, QUERY_DEST, path, addr, text);
    if (len < 0 || (size_t)len >= sizeof(packet) || len > APRS_TNC2_MAX_LEN) {
        ESP_LOGW(TAG, "Query answer to %s not sent - built line too long", toCall);
        return;
    }

    txPacket(packet, (size_t)len);
    ESP_LOGI(TAG, "Query answered to %s: %s", toCall, packet);
}

// ---------------------------------------------------------------------------
// Response builders
// ---------------------------------------------------------------------------

// "?APRS?" -> the station's own position/status packet, byte-for-byte the
// same the IGate position beacon would send.
static void respondAPRS(void) {
    char packet[APRS_TNC2_BUF_SIZE];
    int len = beacon_build_igate_position_packet(packet, sizeof(packet));
    if (len <= 0) {
        ESP_LOGW(TAG, "?APRS? query not answered - no IGate callsign/position configured, or the line did not fit");
        return;
    }
    txPacket(packet, (size_t)len);
    ESP_LOGI(TAG, "?APRS? query answered: %s", packet);
}

// "?WX?" -> the latest cached weather report, if ENABLE_WEATHER and a
// report exists.
static void respondWX(void) {
    char packet[APRS_TNC2_BUF_SIZE];
    int len = weather_build_report_packet(packet, sizeof(packet));
    if (len <= 0) {
        ESP_LOGW(TAG, "?WX? query not answered - no Weather/APRS callsign configured, no reading cached yet, or the line did not fit");
        return;
    }
    txPacket(packet, (size_t)len);
    ESP_LOGI(TAG, "?WX? query answered: %s", packet);
}

// "?IGATE?" -> the "<IGATE,MSG_CNT=n,LOC_CNT=n>" capability/status line
// APRS101 ch.15 defines, built from the same traffic counters the dashboard
// reads (igate_get_stats()). MSG_CNT is the count of messages gatewayed
// RF->INET this session (the closest existing counter to "messages this
// IGate has handled"); LOC_CNT is the count of stations this IGate has
// heard locally and gatewayed (txCount), matching how reference
// implementations (aprsc, javAPRSSrvr) report their own local heard-count.
static void respondIGate(void) {
    if (!g_config.igate_en) {
        ESP_LOGD(TAG, "?IGATE? query ignored - IGate service is disabled");
        return;
    }

    igate_stats_t stats = igate_get_stats();

    char callField[16];
    app_config_lock();
    {
        if (g_config.aprs_ssid > 0)
            snprintf(callField, sizeof(callField), "%s-%d", g_config.aprs_mycall, (int)g_config.aprs_ssid);
        else
            snprintf(callField, sizeof(callField), "%s", g_config.aprs_mycall);
    }
    app_config_unlock();

    if (!callField[0]) {
        ESP_LOGW(TAG, "?IGATE? query not answered - no IGate callsign configured");
        return;
    }

    char path[80];
    aprs_path_build_suffix_from_config(g_config.igate_path, path, sizeof(path));

    char info[64];
    snprintf(info, sizeof(info), "<IGATE,MSG_CNT=%u,LOC_CNT=%u>", (unsigned)stats.txCount, (unsigned)stats.rxCount);

    char packet[APRS_TNC2_BUF_SIZE];
    int n = snprintf(packet, sizeof(packet), "%s>%s%s:%s", callField, QUERY_DEST, path, info);
    if (n < 0 || (size_t)n >= sizeof(packet) || n > APRS_TNC2_MAX_LEN) {
        ESP_LOGW(TAG, "?IGATE? query not answered - built line too long");
        return;
    }

    txPacket(packet, (size_t)n);
    ESP_LOGI(TAG, "?IGATE? query answered: %s", packet);
}

// "?APRSS" -> the station's own status report, byte-for-byte the same the
// IGate status beacon would send (including the Maidenhead locator block when
// that option is on).
static void respondStatus(void) {
    char packet[APRS_TNC2_BUF_SIZE];
    int len = beacon_build_igate_status_packet(packet, sizeof(packet));
    if (len <= 0) {
        ESP_LOGW(TAG, "?APRSS query not answered - no IGate callsign/status text configured, or the line did not fit");
        return;
    }
    txPacket(packet, (size_t)len);
    ESP_LOGI(TAG, "?APRSS query answered: %s", packet);
}

// "?APRSD" -> the list of stations heard here without a digipeater in
// between, as the "Directs=" message APRS101 ch.15 defines. An empty list is
// still answered: "heard nobody directly" is the true state of a station that
// has just booted or sits in a quiet area, and silence would be
// indistinguishable from the query never arriving.
static void respondDirects(const char *fromCall) {
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

    txMessageTo(fromCall, text);
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
static void respondHeard(const char *fromCall, const char *arg) {
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
        snprintf(text, sizeof(text), "?APRSH needs a callsign");
        txMessageTo(fromCall, text);
        return;
    }

    uint16_t hourly[LASTHEARD_HEARD_HOURS];
    if (!lastheard_heard_history(target, hourly)) {
        snprintf(text, sizeof(text), "%s not heard", target);
        txMessageTo(fromCall, text);
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
    txMessageTo(fromCall, text);
}

// "?APRSM" -> re-send any messages this station is still holding for the
// querying operator. Nothing pending is reported explicitly, for the same
// reason an empty "Directs=" is sent: the operator asked a question and an
// answer of "none" is information.
static void respondMessages(const char *fromCall) {
    int sent = message_send_pending_to(fromCall);
    if (sent == 0)
        txMessageTo(fromCall, "No messages pending");
}

// "?APRSO" -> re-announce the Objects/Items this station originates. The
// elements go out from the beacon scheduler task (see
// objitems_request_transmit_all()), not from here, so answering a query never
// occupies the radio RX task for the length of a transmission burst.
static void respondObjects(const char *fromCall) {
#ifdef ENABLE_OBJECTS_ITEMS
    objitems_request_transmit_all();
    ESP_LOGI(TAG, "?APRSO query from %s: all Objects/Items queued for transmission", fromCall);
#else
    txMessageTo(fromCall, "No objects");
#endif
}

// "?APRST" / "?PING?" -> report the route the query itself took to get here,
// which is what lets the querying operator see which digipeaters are in play
// between the two stations. The route is the one extractRoute() read off the
// received line, '*' markers included, so a repeater that actually handled the
// frame is distinguishable from one that was merely requested.
static void respondTrace(const char *fromCall, const char *route) {
    char text[APRS_MSG_TEXT_STD_MAX + 1];

    if (route == NULL || route[0] == 0)
        snprintf(text, sizeof(text), "%s via ?", fromCall);
    else
        snprintf(text, sizeof(text), "%s>%s", fromCall, route);

    txMessageTo(fromCall, text);
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
            respondAPRS();
            return;
        case QUERY_TYPE_WX:
            respondWX();
            return;
        case QUERY_TYPE_IGATE:
            respondIGate();
            return;
        case QUERY_TYPE_STATUS:
            respondStatus();
            return;
        case QUERY_TYPE_DIRECTS:
            respondDirects(req->fromCall);
            return;
        case QUERY_TYPE_HEARD:
            respondHeard(req->fromCall, req->detail);
            return;
        case QUERY_TYPE_MESSAGES:
            respondMessages(req->fromCall);
            return;
        case QUERY_TYPE_OBJECTS:
            respondObjects(req->fromCall);
            return;
        case QUERY_TYPE_TRACE:
            respondTrace(req->fromCall, req->detail);
            return;
        default:
            return;
    }
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
// `directed` selects the table: a general query is one of the three APRS101
// chapter 15 defines as broadcast, while a directed query may additionally be
// any of the per-station ones. Keywords are matched whole, so "?APRS?" and
// "?APRSP" cannot be confused with one another, and the longest-matching
// entry wins for the two that share a prefix ("?APRST" and its "?PING?"
// alias are distinct strings, but "?APRS?" is a prefix of nothing else here).
static bool matchQueryType(const char *info, bool directed, query_type_t *type, const char **arg) {
    static const struct {
        const char *keyword;
        query_type_t type;
        bool directedOnly;
    } table[] = {
        { "?APRS?", QUERY_TYPE_APRS, false },   { "?IGATE?", QUERY_TYPE_IGATE, false },  { "?WX?", QUERY_TYPE_WX, false },
        { "?APRSD", QUERY_TYPE_DIRECTS, true }, { "?APRSH", QUERY_TYPE_HEARD, true },    { "?APRSM", QUERY_TYPE_MESSAGES, true },
        { "?APRSO", QUERY_TYPE_OBJECTS, true }, { "?APRSP", QUERY_TYPE_POSITION, true }, { "?APRSS", QUERY_TYPE_STATUS, true },
        { "?APRST", QUERY_TYPE_TRACE, true },   { "?PING?", QUERY_TYPE_TRACE, true },
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

// True if the query type is enabled by the operator. The three general types
// keep their own per-type switches; the directed-only set shares
// g_config.query_ext_en, since they are all answers about this station's own
// traffic and an operator either wants the station answering that class of
// question or not.
static bool queryTypeEnabled(query_type_t type) {
    switch (type) {
        case QUERY_TYPE_APRS:
            return g_config.query_aprs_en;
        case QUERY_TYPE_WX:
            return g_config.query_wx_en;
        case QUERY_TYPE_IGATE:
            return g_config.query_igate_en;
        case QUERY_TYPE_POSITION:
        case QUERY_TYPE_STATUS:
        case QUERY_TYPE_DIRECTS:
        case QUERY_TYPE_HEARD:
        case QUERY_TYPE_MESSAGES:
        case QUERY_TYPE_OBJECTS:
        case QUERY_TYPE_TRACE:
            return g_config.query_ext_en;
        default:
            return false;
    }
}

// Queues one recognized broadcast query type for answering, gated by its own
// enable flag and the shared broadcast rate limiter. Both gates run here, on
// the receiving task, so a flooded channel costs a string compare and a
// timestamp check rather than a queue slot.
static void dispatchBroadcast(query_type_t type) {
    if (!queryTypeEnabled(type))
        return;
    if (!broadcastRateLimitPass(type))
        return;

    switch (type) {
        case QUERY_TYPE_APRS:
        case QUERY_TYPE_WX:
        case QUERY_TYPE_IGATE:
            queueResponse(type, "", "");
            return;
        default:
            return;
    }
}

void query_process(const char *tnc2Line) {
    if (!g_config.query_en)
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
    if (!matchQueryType(info, false, &type, NULL))
        return;

    dispatchBroadcast(type);
}

void query_process_directed(const char *fromCall, const char *toCall, const char *text, const char *tnc2Line) {
    if (!g_config.query_en || !g_config.query_directed_en)
        return;
    if (fromCall == NULL || toCall == NULL || text == NULL || text[0] != '?')
        return;

    if (!baseCallMatch(toCall, g_config.aprs_mycall))
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

    queueResponse(type, fromCall, detail);
}
