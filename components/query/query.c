/**
 * @file query.c
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
 * @brief APRS query responder implementation: recognizes "?APRS?", "?WX?",
 * "?IGATE?" (broadcast and directed) and transmits the matching response,
 * reusing the existing IGate position / weather report builders so a reply
 * is byte-for-byte consistent with what the periodic beacons would send.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "app_config.h"
#include "aprs_path.h"    // aprs_path_build_suffix_from_config()
#include "aprs_service.h" // APRS_TNC2_BUF_SIZE / APRS_TNC2_MAX_LEN
#include "beacon.h"       // beacon_build_igate_position_packet()
#include "igate.h"        // igate_get_stats()
#include "message.h"      // MSG_CHANNEL_RF / MSG_CHANNEL_INET
#include "query.h"
#include "sched_time.h" // sched_mono_seconds()
#include "weather.h"    // weather_build_report_packet()

static const char *TAG = "query";

// Same software-identifier destination call used by the message and beacon
// components, for consistency across the firmware's own-originated traffic.
#define QUERY_DEST "APE32L"

static void (*s_txHandler)(const char *packet, size_t len, uint8_t channels) = NULL;

// One "last responded" monotonic timestamp per broadcast query type, so a
// misbehaving network flooding "?APRS?" broadcasts cannot make this station
// answer more than once per g_config.query_min_interval_sec - both to save
// airtime and to avoid a feedback loop with other auto-responders.
typedef enum { QUERY_TYPE_APRS = 0, QUERY_TYPE_WX, QUERY_TYPE_IGATE, QUERY_TYPE_COUNT } query_type_t;

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

void query_init(void) {
    memset(s_lastBroadcastRespondSec, 0, sizeof(s_lastBroadcastRespondSec));
    memset(s_directedTrack, 0, sizeof(s_directedTrack));
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

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

// Dispatches one recognized broadcast query type to its builder, gated by
// its own enable flag and the shared broadcast rate limiter.
static void dispatchBroadcast(query_type_t type) {
    switch (type) {
        case QUERY_TYPE_APRS:
            if (!g_config.query_aprs_en)
                return;
            if (!broadcastRateLimitPass(QUERY_TYPE_APRS))
                return;
            respondAPRS();
            return;
        case QUERY_TYPE_WX:
            if (!g_config.query_wx_en)
                return;
            if (!broadcastRateLimitPass(QUERY_TYPE_WX))
                return;
            respondWX();
            return;
        case QUERY_TYPE_IGATE:
            if (!g_config.query_igate_en)
                return;
            if (!broadcastRateLimitPass(QUERY_TYPE_IGATE))
                return;
            respondIGate();
            return;
        default:
            return;
    }
}

// Matches the broadcast query keyword at the start of `info` (already known
// to start with '?') against the small set this responder implements.
// Longest-prefix keywords first so "?APRS?" is not confused with a bare
// "?APRS" prefix of some other, unrecognized query string.
static bool matchBroadcastType(const char *info, query_type_t *type) {
    static const struct {
        const char *keyword;
        query_type_t type;
    } table[] = {
        { "?APRS?", QUERY_TYPE_APRS },
        { "?IGATE?", QUERY_TYPE_IGATE },
        { "?WX?", QUERY_TYPE_WX },
    };

    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        size_t kwLen = strlen(table[i].keyword);
        if (strncasecmp(info, table[i].keyword, kwLen) == 0) {
            *type = table[i].type;
            return true;
        }
    }
    return false;
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
    if (!matchBroadcastType(info, &type))
        return;

    dispatchBroadcast(type);
}

void query_process_directed(const char *fromCall, const char *toCall, const char *text) {
    if (!g_config.query_en || !g_config.query_directed_en)
        return;
    if (fromCall == NULL || toCall == NULL || text == NULL || text[0] != '?')
        return;

    if (!baseCallMatch(toCall, g_config.aprs_mycall))
        return;

    query_type_t type;
    if (!matchBroadcastType(text, &type))
        return;

    if (!directedRateLimitPass(fromCall))
        return;

    // Per the Must-have scope, directed replies are sent the same way
    // broadcast ones are (see the note in the design proposal: most
    // deployed IGates just re-broadcast the reply, since the querying
    // station is listening either way). Reuse the same enable-flag-gated
    // dispatch, without re-applying the broadcast rate limiter - the
    // directed limiter above already governs this response.
    switch (type) {
        case QUERY_TYPE_APRS:
            if (g_config.query_aprs_en)
                respondAPRS();
            return;
        case QUERY_TYPE_WX:
            if (g_config.query_wx_en)
                respondWX();
            return;
        case QUERY_TYPE_IGATE:
            if (g_config.query_igate_en)
                respondIGate();
            return;
        default:
            return;
    }
}
