// @file igate.c
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
// @brief APRS-IS Internet Gateway implementation: TCP client task with login,
// multiserver failover and auto-reconnect, RF->INET gatewaying with filtering
// and duplicate suppression, INET->RF relaying, and gateway traffic statistics.

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_config.h"
#include "aprs_coord.h"
#include "aprs_filter.h"
#include "aprs_service.h"
#include "crc_ccit.h"
#include "igate.h"
#include "lastheard.h"
#include "net_state.h"
#include "str_append.h"
#include "trafficlog.h"

static const char *TAG = "igate";

// Size of a buffer holding this station's APRS-IS identity: the 9-character
// callsign field of app_config_t, the "-15" SSID suffix and the NUL, with a
// little room to spare.
#define IGATE_IDENTITY_SIZE 16

struct DupPacketCache {
    char hash[16];
    unsigned long timestamp;
    uint8_t scope; // dup_scope_t that inserted this entry; a lookup only matches its own scope
};

// Ordinary .bss, not RTC slow memory: this firmware never enters deep sleep, so
// there is nothing for RTC placement to preserve. Keeping the counters here
// leaves that scarce memory for something that needs it, and makes them reset
// on esp_restart() - which is what the dashboard should show after an OTA
// reboot, rather than totals carried over from the previous firmware image.
static igate_stats_t s_stats;
static struct DupPacketCache s_dupCache[DUP_CACHE_SIZE_MAX];
static uint8_t s_dupCacheIndex = 0;

static int s_sock = -1;
static SemaphoreHandle_t s_sockMutex;
static portMUX_TYPE s_sockMutexInitLock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;

// sendToAprsIs()/igate_send_raw() is reachable from several tasks - the IGate
// task, and the Digipeater beacon task whenever digi_loc2inet is on,
// independent of igate_en - so s_sockMutex must exist before any of them uses
// it. This helper makes mutex creation idempotent and safe to call from any
// task, the first time any of them needs it - a portMUX critical section
// (not the mutex itself, which doesn't exist yet) protects the one-time
// creation against a race between concurrent first callers. The handle is
// read and written with __atomic_load_n()/__atomic_store_n() so every caller,
// including the one outside the critical section, sees either NULL or a
// fully constructed semaphore, never a partially published pointer.
static void ensureSockMutex(void) {
    if (__atomic_load_n(&s_sockMutex, __ATOMIC_ACQUIRE))
        return;
    // Allocated before the critical section is entered, so the heap walk
    // never runs with interrupts masked; the critical section only ever
    // publishes the winning handle. A concurrent loser's candidate is freed
    // once outside the lock.
    SemaphoreHandle_t candidate = xSemaphoreCreateMutex();
    portENTER_CRITICAL(&s_sockMutexInitLock);
    if (!s_sockMutex) {
        __atomic_store_n(&s_sockMutex, candidate, __ATOMIC_RELEASE);
        candidate = NULL;
    }
    portEXIT_CRITICAL(&s_sockMutexInitLock);
    if (candidate)
        vSemaphoreDelete(candidate);
}

// Web-configured duplicate-cache size (g_config.dup_cache_size), clamped to
// DUP_CACHE_SIZE_MIN..DUP_CACHE_SIZE_MAX. Read fresh on every call rather than
// cached, so a web-admin save takes effect on the very next check without
// requiring a reconnect or reboot; clamped here (not just at load/save time)
// so an out-of-range value read back from a hand-edited config.json can never
// index past s_dupCache[].
static uint8_t dupCacheSize(void) {
    uint8_t n = g_config.dup_cache_size;
    if (n < DUP_CACHE_SIZE_MIN)
        n = DUP_CACHE_SIZE_MIN;
    else if (n > DUP_CACHE_SIZE_MAX)
        n = DUP_CACHE_SIZE_MAX;
    return n;
}

// Web-configured duplicate-suppression window, in milliseconds
// (g_config.dup_cache_timeout_ms), clamped to DUP_CACHE_TIMEOUT_MS_MIN..
// DUP_CACHE_TIMEOUT_MS_MAX. Read fresh on every call for the same live-update
// reason as dupCacheSize().
static uint32_t dupCacheTimeoutMs(void) {
    uint32_t ms = g_config.dup_cache_timeout_ms;
    if (ms < DUP_CACHE_TIMEOUT_MS_MIN)
        ms = DUP_CACHE_TIMEOUT_MS_MIN;
    else if (ms > DUP_CACHE_TIMEOUT_MS_MAX)
        ms = DUP_CACHE_TIMEOUT_MS_MAX;
    return ms;
}

// Extracts the source callsign (everything before '>') from a TNC2 text
// line into out, for use as the DX field of a trafficlog entry. Leaves out
// empty if no '>' is found within the expected callsign length.
static void tnc2SrcCallsign(const char *data, size_t len, char *out, size_t outMax) {
    out[0] = 0;
    size_t maxScan = (len < 16) ? len : 16;
    for (size_t i = 0; i < maxScan; i++) {
        if (data[i] == '>') {
            size_t n = (i < outMax - 1) ? i : outMax - 1;
            memcpy(out, data, n);
            out[n] = 0;
            return;
        }
    }
}

// Spells one station identity the single way this firmware puts a callsign on
// APRS-IS: "CALL-SSID", or the bare callsign when the SSID is 0 (an APRS-IS
// identity has no "-0" form). Every place the station names itself to the
// server goes through here - the login line, the callsign-SSID that follows
// the q construct on gated frames, and the source callsign of the frames
// themselves - so the server sees one identity from this station and not
// several.
//
// That identity is what aprsc matches a client's own traffic against
// (aprs-is.net/Connecting.aspx, IGating.aspx): an inbound message is delivered
// only when its addressee equals the login exactly, and an outbound packet is
// recognised as originated by the client, rather than relayed, only when its
// source callsign equals the login exactly. out is always NUL-terminated;
// out_size must be at least 1.
static void stationIdentity(const char *call, uint8_t ssid, char *out, size_t out_size) {
    if (ssid > 0)
        snprintf(out, out_size, "%s-%d", call, (int)ssid);
    else
        snprintf(out, out_size, "%s", call);
}

igate_stats_t igate_get_stats(void) {
    return s_stats;
}

void igate_note_drop(drop_reason_t reason) {
    if ((unsigned)reason < DROP_REASON_COUNT)
        s_stats.dropByReason[reason]++;
}

void igate_note_message_gated(void) {
    s_stats.msgCount++;
}

const char *igate_drop_reason_name(drop_reason_t reason) {
    switch (reason) {
        case DROP_DUP:
            return "duplicate";
        case DROP_TOO_SHORT:
            return "too short";
        case DROP_SRC_PLACEHOLDER:
            return "src: NOCALL/N0CALL/WIDE/TRACE/TCP";
        case DROP_NOT_APRS:
            return "not a valid APRS UI/PID 0xF0 frame";
        case DROP_PATH_TOKEN:
            return "RFONLY/TCP/qA/NOGATE";
        case DROP_3RDPARTY_LOOP:
            return "3rd-party loop (TCPIP/TCPXX)";
        case DROP_3RDPARTY_NESTED:
            return "3rd-party nested (INET->RF)";
        case DROP_3RDPARTY_NESTED_RF:
            return "3rd-party nested (RF->INET)";
        case DROP_SAT_NOT_USED:
            return "satellite not used";
        case DROP_GENERIC_QUERY:
            return "generic query (RF/INET)";
        case DROP_TYPE_FILTER:
            return "type filter";
        case DROP_RANGE_FILTER:
            return "range filter";
        case DROP_PREFIX_FILTER:
            return "prefix filter";
        case DROP_BUDLIST:
            return "callsign filter";
        case DROP_MSG_NOT_LOCAL:
            return "msg: addressee not heard locally";
        case DROP_MSG_SENDER_LOCAL:
            return "msg: sender heard on RF";
        case DROP_HEADER_FORBIDS_RF:
            return "header: TCPXX/NOGATE/RFONLY/qAX/qAZ";
        case DROP_MSG_ADDRESSEE_INET:
            return "msg: addressee on the Internet";
        case DROP_TX_FAIL:
            return "TX failed";
        case DROP_HEADER_OVERFLOW:
            return "header overflow";
        case DROP_IS_LINE_TOO_LONG:
            return "APRS-IS line too long (RF->INET)";
        case DROP_IS_RX_LINE_TOO_LONG:
            return "APRS-IS line too long (RX)";
        case DROP_PLACEHOLDER_CALL:
            return "placeholder callsign (NOCALL/MYCALL)";
        case DROP_MODEM_NOT_READY:
            return "modem not ready";
        case DROP_TX_QUEUE_FULL:
            return "RF TX queue full";
        case DROP_TX_TOO_LONG:
            return "TX packet too long";
        case DROP_TX_DUTY_CYCLE:
            return "duty-cycle ceiling reached";
        case ERR_MODEM_SEND_FAIL:
            return "modem send failed";
        case ERR_AX25_DECODE:
            return "AX.25 decode error";
        case ERR_AX25_NOT_APRS:
            return "non-APRS AX.25 traffic";
        case DROP_DIGI_MALFORMED:
            return "digi: malformed packet";
        case DROP_DIGI_PLACEHOLDER_CALL:
            return "digi: placeholder callsign (NOCALL/MYCALL)";
        case DROP_DIGI_ALREADY_USED:
            return "digi: already used";
        case DROP_DIGI_PATH_FULL:
            return "digi: path full";
        case DROP_DIGI_N_TRAPPED:
            return "digi: hop count trapped";
        case DROP_DIGI_PATH_TOKEN:
            return "digi: qA/TCP path token";
        case DROP_DIGI_DUPLICATE:
            return "digi: duplicate";
        case DROP_REASON_COUNT:
            break;
    }
    return "";
}

bool igate_is_connected(void) {
    bool connected = false;
    ensureSockMutex();
    if (xSemaphoreTake(s_sockMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        connected = (s_sock >= 0);
        xSemaphoreGive(s_sockMutex);
    }
    return connected;
}

// ---------------------------------------------------------------------------
// Duplicate detection
// ---------------------------------------------------------------------------
// @brief Build a 16-byte dedup key for a decoded frame.
//
// Zero-initializes the full 16-byte key, then seeds it with the source
// callsign, source SSID and payload length, and finally mixes in two
// CRC-CCITT digests of the *entire* info field - one computed forward, one
// computed backward - so that any byte anywhere in the payload (not just an
// early prefix) changes the resulting hash. Two frames only produce the
// same hash if they genuinely share source, length, and byte-for-byte
// payload content.
static void packetHash(const ax25_msg_t *packet, char *hash) {
    memset(hash, 0, 16);
    snprintf(hash, 16, "%s%d%d", packet->src.call, packet->src.ssid, (int)packet->len);

    uint16_t crcFwd = CRC_CCIT_INIT_VAL;
    for (size_t i = 0; i < packet->len; i++)
        crcFwd = update_crc_ccit(packet->info[i], crcFwd);

    uint16_t crcRev = CRC_CCIT_INIT_VAL;
    for (size_t i = packet->len; i-- > 0;)
        crcRev = update_crc_ccit(packet->info[i], crcRev);

    uint8_t mix[4] = { (uint8_t)(crcFwd >> 8), (uint8_t)crcFwd, (uint8_t)(crcRev >> 8), (uint8_t)crcRev };
    for (int i = 0; i < 16; i++)
        hash[i] ^= (char)mix[i % 4];
}

void clearExpiredDuplicates(void) {
    unsigned long now = (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint32_t timeoutMs = dupCacheTimeoutMs();
    for (uint8_t i = 0; i < dupCacheSize(); i++) {
        if (s_dupCache[i].timestamp > 0 && (now - s_dupCache[i].timestamp) > timeoutMs) {
            s_dupCache[i].timestamp = 0;
        }
    }
}

bool isDuplicatePacketScoped(ax25_msg_t *packet, dup_scope_t scope) {
    if ((unsigned)scope >= DUP_SCOPE_COUNT)
        return false;

    char hash[16] = { 0 };
    packetHash(packet, hash);

    unsigned long now = (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    clearExpiredDuplicates();

    uint8_t cacheSize = dupCacheSize();
    for (uint8_t i = 0; i < cacheSize; i++) {
        // hash is fixed-width binary data (post-XOR bytes may legally be NUL
        // partway through), not a C string, so compare it with memcmp() over
        // the full 16 bytes rather than strncmp(), which stops at the first
        // embedded NUL and could otherwise call two different hashes equal.
        // The scope test keeps the IGate and digipeater windows independent:
        // both see the same frame, and matching across scopes would let the
        // first one to run hide it from the second.
        if (s_dupCache[i].timestamp > 0 && s_dupCache[i].scope == (uint8_t)scope && memcmp(s_dupCache[i].hash, hash, 16) == 0) {
            ESP_LOGD(TAG, "Duplicate packet detected (scope %d)", (int)scope);
            return true;
        }
    }

    // Clamp the insertion index too: a runtime shrink of dup_cache_size
    // (web-admin save) could otherwise leave s_dupCacheIndex pointing past
    // the now-smaller window, walking indices the lookup above never scans.
    if (s_dupCacheIndex >= cacheSize)
        s_dupCacheIndex = 0;
    memcpy(s_dupCache[s_dupCacheIndex].hash, hash, 16);
    s_dupCache[s_dupCacheIndex].timestamp = now;
    s_dupCache[s_dupCacheIndex].scope = (uint8_t)scope;
    s_dupCacheIndex = (s_dupCacheIndex + 1) % cacheSize;
    return false;
}

bool isDuplicatePacket(ax25_msg_t *packet) {
    return isDuplicatePacketScoped(packet, DUP_SCOPE_IGATE);
}

// APRS-IS line limit (aprs-is.net/Connecting.aspx): no line, including its
// CR/LF terminator, may exceed 512 bytes. Expressed as the usable payload
// (510 bytes) plus the two-byte terminator, so every buffer sized from this
// constant states its budget in the same terms as the rule it enforces.
#define APRS_IS_LINE_MAX (510)

// Longest line, including the CRLF terminator, that sendToAprsIs() will
// assemble and send in one write. Matches the APRS-IS line limit exactly, so
// a line that fits here is guaranteed acceptable to the server and a line
// that does not is refused rather than sent truncated.
#define SEND_TO_APRSIS_BUF_SIZE (APRS_IS_LINE_MAX + 2)

// ---------------------------------------------------------------------------
// RF -> INET
// ---------------------------------------------------------------------------
static bool sendToAprsIs(const uint8_t *data, size_t len) {
    bool ok = false;

    // The line and its CRLF terminator are assembled into one local buffer
    // and handed to a single send(). APRS-IS is line-oriented, so the server
    // cannot act on a line until the terminator lands: sending it separately
    // would let TCP hold the two-byte CRLF back waiting to coalesce with a
    // further write, adding a full round trip to every gated frame and
    // outbound message. A single send() also means the socket can never be
    // left holding a line whose payload went out but whose terminator did
    // not. The buffer is sized well above every known caller's longest line
    // (the RF->INET gateway path builds at most a 500-byte frame); a line
    // that still would not fit is refused rather than sent truncated.
    uint8_t line[SEND_TO_APRSIS_BUF_SIZE];
    if (len + 2 <= sizeof(line)) {
        memcpy(line, data, len);
        line[len] = '\r';
        line[len + 1] = '\n';
        size_t lineLen = len + 2;

        ensureSockMutex();
        xSemaphoreTake(s_sockMutex, portMAX_DELAY);
        if (s_sock >= 0) {
            if (send(s_sock, line, lineLen, 0) == (ssize_t)lineLen) {
                ok = true;
            }
        }
        xSemaphoreGive(s_sockMutex);
    }

    // Information-level log of every APRS-IS TX (RF->INET gated frames and
    // outbound messages alike go through here) so the igate traffic is
    // visible at the default log level, not just with verbose/debug logging.
    char dx[16];
    tnc2SrcCallsign((const char *)data, len, dx, sizeof(dx));

    char pkt[128];
    size_t pktLen = (len < sizeof(pkt) - 1) ? len : sizeof(pkt) - 1;
    memcpy(pkt, data, pktLen);
    pkt[pktLen] = 0;

    if (ok) {
        // Counts every successful APRS-IS TX regardless of caller (gatewayed
        // RF frames, outbound messages, digi "beacon to internet"), unlike
        // s_stats.txCount below which is only bumped by igateProcess() for
        // the RF->INET gatewaying path specifically.
        s_stats.isTxCount++;
        ESP_LOGI(TAG, "APRS-IS TX: %.*s", (int)len, (const char *)data);
        trafficlog_add_pkt("TX", dx, pkt, "", -1, 0, 0);
    } else {
        // Every failed APRS-IS TX (socket not connected, or the write
        // itself failed) counts as a drop here, at the single choke point
        // all callers funnel through - igate_send_raw() (beacons, outbound
        // messages) and igateProcess()'s own RF2INET gatewaying path alike.
        // Counting the failure here keeps a beacon or message that never made
        // it to APRS-IS (e.g. "not connected yet") visible on the dashboard's
        // DROP/ERR tally, which reads igate_stats_total_drop().
        s_stats.dropByReason[DROP_TX_FAIL]++;
        ESP_LOGW(TAG, "APRS-IS TX failed (not connected?): %.*s", (int)len, (const char *)data);
        trafficlog_add_pkt("TX-FAIL", dx, pkt, "", -1, 0, 0);
    }
    return ok;
}

bool igate_send_raw(const char *line, size_t len) {
    return sendToAprsIs((const uint8_t *)line, len);
}

// Third-party ('}') unwrap for the RF->INET direction.
//
// A third-party payload is itself a complete TNC2-style line prefixed with
// '}': "}SRC>DST,PATH:payload". A station relaying such a frame on RF (a
// cross-band or HF gateway with no other route to the Internet, for
// instance) is asking for the wrapped station to be gated under its own
// identity, not under the relaying station's.
//
// If the inner header already carries a TCPIP/TCPXX q-construct token, the
// packet has already been on APRS-IS once; gating it again is how IGate
// loops are made, so the caller must drop it instead. If the inner payload
// is itself third-party-wrapped (a second '}' immediately after the inner
// header's ':'), unwrapping only one layer would hand the caller a payload
// that still starts with '}' and gate it under the wrong identity, so the
// caller must reject it outright instead of unwrapping it further or
// passing it on for the type filter to catch incidentally. Otherwise this
// parses the inner "SRC>DST,PATH:" header up to (and excluding) the outer
// '}' and the trailing ':', so the caller can gate the wrapped packet under
// its own source/destination/path exactly as if it had been heard directly.
//
// info must be NUL-terminated and start with '}'. On success, *innerInfo
// points at the first byte of the inner payload (still inside info, not a
// copy) and *innerInfoLen is its length up to the terminating NUL.
static bool thirdPartyUnwrap(const char *info, char *outSrc, size_t outSrcMax, char *outDst, size_t outDstMax, char *outPath, size_t outPathMax,
                             const char **innerInfo, size_t *innerInfoLen, bool *isLoop, bool *isNested) {
    const char *gt = strchr(info + 1, '>');
    const char *colon = strchr(info + 1, ':');
    if (!gt || !colon || colon <= gt)
        return false;

    size_t srcLen = (size_t)(gt - (info + 1));
    if (srcLen >= outSrcMax)
        srcLen = outSrcMax - 1;
    memcpy(outSrc, info + 1, srcLen);
    outSrc[srcLen] = 0;

    const char *dstStart = gt + 1;
    const char *pathStart = dstStart;
    while (pathStart < colon && *pathStart != ',')
        pathStart++;
    size_t dstLen = (size_t)(pathStart - dstStart);
    if (dstLen >= outDstMax)
        dstLen = outDstMax - 1;
    memcpy(outDst, dstStart, dstLen);
    outDst[dstLen] = 0;

    if (outSrc[0] == 0 || outDst[0] == 0)
        return false;

    outPath[0] = 0;
    size_t pathLen = 0;
    if (pathStart < colon && *pathStart == ',') {
        pathLen = (size_t)(colon - (pathStart + 1));
        if (pathLen >= outPathMax)
            pathLen = outPathMax - 1;
        memcpy(outPath, pathStart + 1, pathLen);
        outPath[pathLen] = 0;
    }

    // The loop guard: an inner header that already carries TCPIP/TCPXX means
    // this frame already reached APRS-IS once, via whichever station wrapped
    // it. Gating it again would feed it back to the server a second time.
    *isLoop = (strstr(outPath, "TCPIP") != NULL) || (strstr(outPath, "TCPXX") != NULL);

    // The nesting guard: a payload that starts with another '}' right after
    // the inner header's ':' is wrapped more than one level deep. Unwrapping
    // only this outer layer would leave a still-'}'-prefixed payload for the
    // caller to gate under this layer's source/destination/path, which is
    // wrong, so nesting is rejected here rather than relying on a still-
    // wrapped payload being unclassifiable further downstream.
    *isNested = (colon[1] == '}');

    *innerInfo = colon + 1;
    *innerInfoLen = strlen(colon + 1);
    return true;
}

// APRS-IS basic RX-IGate source-callsign blacklist (aprx "PROTOCOLS",
// derived from IGateDetails.aspx): none of these prefixes ever identifies a
// real originating station, so a frame carrying one as its source must never
// be gated onto APRS-IS under this station's qAR, however it got onto RF.
static const char *const kSrcPlaceholderPrefixes[] = { "NOCALL", "N0CALL", "WIDE", "TRACE", "TCP" };

static bool isSrcPlaceholder(const char *call) {
    for (size_t i = 0; i < sizeof(kSrcPlaceholderPrefixes) / sizeof(kSrcPlaceholderPrefixes[0]); i++) {
        size_t n = strlen(kSrcPlaceholderPrefixes[i]);
        if (!strncmp(call, kSrcPlaceholderPrefixes[i], n))
            return true;
    }
    return false;
}

// Pick the q construct for one packet gated from RF to APRS-IS.
//
// QCON defines qAR as a packet placed on APRS-IS by an IGate that heard it on
// RF, and qAO as one from an IGate that cannot gate messages to the station it
// heard, so the choice describes the station being gated and not the gateway:
// message routers and the "messageable" indication on APRS-IS map viewers read
// it as "a message for this station can be delivered through this IGate". A
// receive-only gateway therefore answers qAO for everything, and a
// bidirectional one still answers qAO for each station it would decline to
// deliver to.
//
// What it would decline is fixed by the message gate in the INET->RF
// direction (messageGatePass() in aprs_service.c): the addressee must have
// been heard on RF and must not be Internet-connected. The first half holds by
// construction here - this station is the source of a frame just decoded off
// the air - so only the second half is left to test, over the same last-heard
// window the message gate itself uses. A source that cannot be identified is
// answered qAO as well, since nothing can be promised about delivering to it.
static const char *qConstructFor(const char *srcCall, uint16_t localWindowSec) {
    if (!aprs_service_can_gate_to_rf())
        return "qAO";
    if (srcCall == NULL || srcCall[0] == 0)
        return "qAO";
    if (lastheard_heard_inet_within(srcCall, localWindowSec))
        return "qAO";
    return "qAR";
}

int igateProcess(ax25_msg_t *packet) {
    int idx;

    if (!g_config.igate_en || !g_config.rf2inet)
        return 0;

    // Count every packet considered for gatewaying, regardless of the
    // outcome below (dup/dropped/sent). Without this, s_stats.rxCount stays
    // permanently at 0 and the dashboard's STATISTICS panel (RF2INET / part
    // of PACKET RX) never updates.
    s_stats.rxCount++;

    if (isSrcPlaceholder(packet->src.call)) {
        s_stats.dropByReason[DROP_SRC_PLACEHOLDER]++;
        return 0;
    }

    // IGating.aspx requires every frame gated onto APRS-IS to be a valid
    // AX.25 UI frame with PID 0xF0 (no layer 3), regardless of what the
    // modem's own allowNonAprs RX setting let through to get here: that
    // setting only controls local RX/monitor handling and must never leak
    // non-APRS traffic onto APRS-IS.
    if (packet->ctrl != AX25_CTRL_UI || packet->pid != AX25_PID_NOLAYER3) {
        s_stats.dropByReason[DROP_NOT_APRS]++;
        return 0;
    }

    if (isDuplicatePacket(packet)) {
        s_stats.dupCount++;
        return 0;
    }

    if (packet->len < 2) {
        s_stats.dropByReason[DROP_TOO_SHORT]++;
        return 0;
    }

    for (idx = 0; idx < packet->rpt_count; idx++) {
        if (!strncmp(packet->rpt_list[idx].call, "RFONLY", 6) || !strncmp(packet->rpt_list[idx].call, "TCP", 3) ||
            !strncmp(packet->rpt_list[idx].call, "qA", 2) || !strncmp(packet->rpt_list[idx].call, "NOGATE", 6)) {
            s_stats.dropByReason[DROP_PATH_TOKEN]++;
            return 0;
        }
    }

    // Only gate frames routed through a satellite digipeater when that
    // satellite address is actually marked as used, i.e. the AX.25 "has been
    // repeated" H-bit of the address is set. A frame carrying the satellite in
    // its path without that bit was merely addressed to the bird, never
    // relayed by it, so it must not reach APRS-IS.
    //
    // The H-bit is decoded by ax25_decode() into the packet->rpt_flags bitmap
    // (one bit per rpt_list entry) and is read through the AX25_REPEATED()
    // accessor; rpt_list[].call holds only the bare NUL-terminated callsign.
    // The '*' marker is a TNC2 text convention and is emitted from rpt_flags
    // when the header line is rendered further down this function.
    // Satellite/ISS digipeater gate-call list: web-configurable (IGate page,
    // parallel to the callsign whitelist/blacklist), defaults to the
    // firmware's previous fixed 6-entry set. Snapshotted under the config
    // lock, same as rf2inet_prefixes/rf2inet_range_* below - this runs on the
    // modem RX task, and a concurrent web save could otherwise rewrite the
    // list mid-check.
    char satGates[IGATE_SATGATE_MAX][10];
    app_config_lock();
    memcpy(satGates, g_config.satgate, sizeof(satGates));
    app_config_unlock();
    // The copy above takes the full field width of every row, so termination
    // depends on what the config loader stored. Force it: everything
    // downstream treats each row as a C string, and a row filled edge to
    // edge would send it reading past the end of that row.
    for (size_t s = 0; s < IGATE_SATGATE_MAX; s++)
        satGates[s][sizeof(satGates[s]) - 1] = 0;

    for (idx = 0; idx < packet->rpt_count; idx++) {
        for (size_t s = 0; s < IGATE_SATGATE_MAX; s++) {
            size_t satLen = strlen(satGates[s]);
            if (satLen == 0)
                continue; // empty slot: skip
            if (!strncmp(packet->rpt_list[idx].call, satGates[s], satLen)) {
                if (!AX25_REPEATED(packet, idx)) {
                    s_stats.dropByReason[DROP_SAT_NOT_USED]++;
                    return 0;
                }
            }
        }
    }

    // The classifier and the third-party unwrap below both take a C string
    // while packet->info is a raw AX.25 field with no terminator of its own,
    // hence the bounded copy into a local.
    char info[AX25_FRAME_MAX_SIZE + 1];
    {
        size_t n = packet->len < AX25_FRAME_MAX_SIZE ? packet->len : AX25_FRAME_MAX_SIZE;
        memcpy(info, packet->info, n);
        info[n] = 0;
    }

    // Third-party ('}') traffic heard on RF: unwrap it here, before the type
    // filter, so the wrapped station is gated under its own identity rather
    // than being dropped outright (the type filter classifies '}' as 0,
    // which never passes aprs_filter_pass()) or gated under the relaying
    // station's callsign. A frame whose inner header already shows TCPIP/
    // TCPXX has already reached APRS-IS once and is dropped as a loop
    // instead of being gated a second time. A frame whose unwrapped payload
    // is itself third-party-wrapped is dropped as nested rather than gated
    // under this layer's identity or passed on still '}'-prefixed. Everything
    // below this block (type filter, range/prefix filter, budlist, header
    // build, payload copy) then runs against the effective src/dst/path/info
    // - the inner packet's own, if unwrapping applied, or the frame's own
    // otherwise - so a single code path handles both cases.
    char effSrc[12] = { 0 };
    char effDst[12] = { 0 };
    char effPath[200] = { 0 };
    const char *effInfo = info;
    size_t effInfoLen = packet->len < AX25_FRAME_MAX_SIZE ? packet->len : AX25_FRAME_MAX_SIZE;
    bool thirdParty = false;

    if (info[0] == '}') {
        const char *innerInfo;
        size_t innerInfoLen;
        bool isLoop;
        bool isNested;
        if (!thirdPartyUnwrap(info, effSrc, sizeof(effSrc), effDst, sizeof(effDst), effPath, sizeof(effPath), &innerInfo, &innerInfoLen, &isLoop, &isNested)) {
            s_stats.dropByReason[DROP_TOO_SHORT]++;
            return 0;
        }
        if (isLoop) {
            s_stats.dropByReason[DROP_3RDPARTY_LOOP]++;
            return 0;
        }
        if (isNested) {
            s_stats.dropByReason[DROP_3RDPARTY_NESTED_RF]++;
            return 0;
        }
        effInfo = innerInfo;
        effInfoLen = innerInfoLen;
        thirdParty = true;
    }

    // Generic queries ("?APRS?", "?WX?", "?IGATE?", ...) are never gated to
    // APRS-IS: a single RF station sending one can otherwise trigger a query
    // responder reply from every APRS-IS-connected station that implements
    // one, flooding the network with this station's callsign attributed as
    // the source via the qAR construct. This check is unconditional and
    // independent of g_config.rf2inetFilter, so it cannot be defeated by any
    // checkbox state. A directed query (":CALLSIGN :?APRSD", data type ':')
    // does not start with '?' and is unaffected; it is still subject to the
    // ordinary type filter below.
    if (effInfoLen > 0 && effInfo[0] == '?') {
        s_stats.dropByReason[DROP_GENERIC_QUERY]++;
        return 0;
    }

    // [IGATE] Filter (RF->INET): g_config.rf2inetFilter is a whitelist of
    // payload types, one bit per type, built from the checkboxes in the
    // "RF -> INET" fieldset of the web IGate page. Classify the effective
    // info field and drop it unless its bit is set. This is the RF side of
    // the pair aprs_service.c applies to g_config.inet2rfFilter for INET->RF
    // traffic: both halves classify with the same aprs_filter helpers and
    // test with the same aprs_filter_pass(), so a type turned off gates
    // identically whichever direction it travels in.
    {
        uint16_t type = aprs_filter_classify_info(effInfo);
        if (!aprs_filter_pass(g_config.rf2inetFilter, type)) {
            ESP_LOGD(TAG, "RF2INET filtered (%s, mask=0x%03X): %.*s", aprs_filter_type_name(type), (unsigned)g_config.rf2inetFilter, (int)effInfoLen, effInfo);
            s_stats.dropByReason[DROP_TYPE_FILTER]++;
            return 0;
        }
    }

    // Local range/prefix gate (RF->INET direction): independent of - and
    // composed with (AND semantics) - the type filter just above, exactly
    // like the budlist check below it. Neither knob has anything to do with
    // g_config.aprs_filter (the APRS-IS *server-side* filter, which only
    // governs what the server sends INTO this client, INET->RF); these gate
    // what this client chooses to push OUT to APRS-IS from RF.
    {
        bool rangeEn, prefixEn;
        float rangeKm, ownLat, ownLon;
        char prefixes[sizeof(g_config.rf2inet_prefixes)];

        // Snapshot under the config lock: this runs on the modem RX task, and
        // a concurrent web save could otherwise rewrite these mid-check.
        app_config_lock();
        rangeEn = g_config.rf2inet_range_en;
        rangeKm = g_config.rf2inet_range_km;
        ownLat = g_config.my_lat;
        ownLon = g_config.my_lon;
        prefixEn = g_config.rf2inet_prefix_en;
        memcpy(prefixes, g_config.rf2inet_prefixes, sizeof(prefixes));
        app_config_unlock();
        // The copy above takes the full field width, so termination depends
        // on what the config loader stored. Force it: everything downstream
        // treats this as a C string, and a field filled edge to edge would
        // send it reading past the end of the local buffer.
        prefixes[sizeof(prefixes) - 1] = 0;

        // The effective destination call feeds the Mic-E decode path inside
        // aprs_filter_decode_position() (the position lives in the AX.25
        // destination field for that payload type); a third-party inner
        // packet never carries Mic-E in its unwrapped text form, so this is
        // only exact for the non-unwrapped case and simply doesn't decode
        // for the other, same as any other undecodable position above.
        if (rangeEn && rangeKm > 0.0f) {
            float plat, plon;
            if (aprs_filter_decode_position(effInfo, thirdParty ? effDst : packet->dst.call, &plat, &plon)) {
                float d = aprs_filter_haversine_km(ownLat, ownLon, plat, plon);
                if (d > rangeKm) {
                    ESP_LOGD(TAG, "RF2INET range-filtered (%.1f km > %.1f km): %s", d, rangeKm, thirdParty ? effSrc : packet->src.call);
                    s_stats.dropByReason[DROP_RANGE_FILTER]++;
                    return 0;
                }
            }
            // Position couldn't be decoded (e.g. a non-position payload
            // type that still passed the type filter above, like a message
            // or status report): distance can't be evaluated, so this gate
            // simply doesn't apply - fall through rather than guessing/
            // dropping.
        }

        if (prefixEn && !aprs_filter_prefix_match(thirdParty ? effSrc : packet->src.call, prefixes)) {
            ESP_LOGD(TAG, "RF2INET prefix-filtered (%s not in \"%s\")", thirdParty ? effSrc : packet->src.call, prefixes);
            s_stats.dropByReason[DROP_PREFIX_FILTER]++;
            return 0;
        }
    }

    // Local callsign whitelist/blacklist (RF->INET direction): independent of
    // - and composes with (AND semantics) - the type filter just above. Keyed
    // on the effective source callsign only (SSID stripped inside the
    // helper), same as aprs_service.c's INET->RF handler below applies it on
    // its own side.
    if (!aprs_filter_budlist_pass(g_config.rf2inet_budlist_mode, thirdParty ? effSrc : packet->src.call)) {
        ESP_LOGD(TAG, "RF2INET budlist-filtered (mode=%d): %s", (int)g_config.rf2inet_budlist_mode, thirdParty ? effSrc : packet->src.call);
        s_stats.dropByReason[DROP_BUDLIST]++;
        return 0;
    }

    // Every append below is clamped by str_append(), so headerLen can never
    // walk past the end of header[] however long the decoded repeater path is.
    // That matters because the overflow verdict is only usable once the whole
    // header has been assembled (the q-construct suffix is appended last), so
    // the check has to be able to run safely *after* the writes rather than
    // gate each one.
    char header[300];
    size_t headerLen = 0;

    if (thirdParty) {
        // The outer RF header is discarded entirely: the gated frame is
        // built from the inner packet's own source, destination and path,
        // exactly as if the wrapped station had been heard directly.
        str_append(header, sizeof(header), &headerLen, "%s>%s", effSrc, effDst);
        if (effPath[0])
            str_append(header, sizeof(header), &headerLen, ",%s", effPath);
    } else {
        if (packet->src.ssid > 0)
            str_append(header, sizeof(header), &headerLen, "%s-%d>%s", packet->src.call, packet->src.ssid, packet->dst.call);
        else
            str_append(header, sizeof(header), &headerLen, "%s>%s", packet->src.call, packet->dst.call);

        if (packet->dst.ssid > 0)
            str_append(header, sizeof(header), &headerLen, "-%d", packet->dst.ssid);

        for (int i = 0; i < packet->rpt_count; i++) {
            str_append(header, sizeof(header), &headerLen, ",%s", packet->rpt_list[i].call);
            if (packet->rpt_list[i].ssid > 0)
                str_append(header, sizeof(header), &headerLen, "-%d", packet->rpt_list[i].ssid);
            if (AX25_REPEATED(packet, i))
                str_append(header, sizeof(header), &headerLen, "*");
        }
    }

    // Snapshot the own-station identity that goes into the qAR/qAO header,
    // together with the last-heard window the construct is decided over. This
    // runs on the modem RX task; a concurrent web save could otherwise rewrite
    // aprs_mycall mid-snprintf or move the window between the two reads.
    char cfg_mycall[10];
    uint8_t cfg_ssid;
    uint16_t cfg_window;
    app_config_lock();
    memcpy(cfg_mycall, g_config.aprs_mycall, sizeof(cfg_mycall));
    cfg_ssid = g_config.aprs_ssid;
    cfg_window = g_config.igate_local_window_sec;
    app_config_unlock();
    // The copy above takes the full field width, so termination depends on
    // what the config loader stored. Force it: everything downstream treats
    // this as a C string, and a field filled edge to edge would send it
    // reading past the end of the local buffer.
    cfg_mycall[sizeof(cfg_mycall) - 1] = 0;

    // The station the q construct describes is the one carried in the header
    // just built: the inner station for an unwrapped third-party frame, the
    // frame's own source otherwise, in the "CALL-SSID" spelling the last-heard
    // table is keyed by.
    char qSrc[12];
    if (thirdParty)
        snprintf(qSrc, sizeof(qSrc), "%s", effSrc);
    else
        stationIdentity(packet->src.call, packet->src.ssid, qSrc, sizeof(qSrc));

    // The callsign-SSID that follows the q construct is always this station's
    // own login identity - never a cosmetic label - and no other part of the
    // path is touched. It is spelled by the same helper connectAprsIs() logs
    // in with, so the two cannot disagree.
    char identity[IGATE_IDENTITY_SIZE];
    stationIdentity(cfg_mycall, cfg_ssid, identity, sizeof(identity));

    const char *qConstruct = qConstructFor(qSrc, cfg_window);
    str_append(header, sizeof(header), &headerLen, ",%s,%s", qConstruct, identity);

    if (!str_append(header, sizeof(header), &headerLen, ":")) {
        // Header buffer exhausted (an excessively long repeater path) - a rare
        // formatting edge case, tracked under its own explicit reason rather
        // than a generic/opaque "other" bucket. A header that lost even one
        // character is unusable: the q-construct and the ":" separator are the
        // last things written, so a truncated one would be gated to APRS-IS
        // with a mangled path. Drop it instead.
        s_stats.dropByReason[DROP_HEADER_OVERFLOW]++;
        return 0;
    }

    // The effective info field's CR/LF-stripped length is computed before any
    // copying, so the frame can be sized and refused up front - the gated
    // line must never reach APRS-IS or RF as a silently truncated half-frame.
    size_t strippedInfoLen = 0;
    for (size_t i = 0; i < effInfoLen; i++) {
        uint8_t c = (uint8_t)effInfo[i];
        if (c != '\r' && c != '\n')
            strippedInfoLen++;
    }

    if (headerLen + strippedInfoLen > APRS_IS_LINE_MAX) {
        // The assembled line would exceed the APRS-IS 512-byte line limit
        // (headerLen + strippedInfoLen + CRLF > 512). Refusing here, before
        // any bytes are copied, keeps this call site consistent with every
        // beacon builder in the project: a frame that does not fit is
        // dropped whole, never gated as a mangled fragment.
        ESP_LOGW(TAG, "RF2INET frame too long for APRS-IS (%u bytes, limit %u) - dropped", (unsigned)(headerLen + strippedInfoLen), (unsigned)APRS_IS_LINE_MAX);
        s_stats.dropByReason[DROP_IS_LINE_TOO_LONG]++;
        return 0;
    }

    uint8_t frame[APRS_IS_LINE_MAX];
    size_t fpos = 0;
    memcpy(&frame[fpos], header, headerLen);
    fpos += headerLen;

    // Copy the effective info field, stripping CR/LF - the inner payload for
    // an unwrapped third-party frame (the outer '}' data type identifier is
    // left behind with the discarded outer header), or the frame's own info
    // field otherwise. The length check above guarantees every byte fits.
    for (size_t i = 0; i < effInfoLen; i++) {
        uint8_t c = (uint8_t)effInfo[i];
        if (c == '\r' || c == '\n')
            continue;
        frame[fpos++] = c;
    }

    if (sendToAprsIs(frame, fpos)) {
        s_stats.txCount++;
        // An APRS message carries the ':' data type identifier in the first
        // byte of the information field, and messages are what MSG_CNT counts.
        if (effInfoLen > 0 && effInfo[0] == ':')
            s_stats.msgCount++;
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// APRS-IS TCP client task (login + RX pump). INET -> RF frames are handed off
// via the optional callback registered with igate_set_inet2rf_handler().
// ---------------------------------------------------------------------------
static void (*s_inet2rfHandler)(const char *line) = NULL;

void igate_set_inet2rf_handler(void (*handler)(const char *line)) {
    s_inet2rfHandler = handler;
}

// Read the current APRS-IS descriptor under the mutex that guards it, so the
// uplink task never observes s_sock while closeSocket() or connectAprsIs() is
// part-way through changing it. The blocking recv() below is then made on the
// returned copy: holding the mutex across a recv() with a multi-second timeout
// would block every APRS-IS transmitter for that whole window, so the value is
// snapshotted instead. A descriptor closed right after the snapshot makes that
// recv() fail, which the loop already handles by reconnecting.
static int socketSnapshot(void) {
    ensureSockMutex();
    int sock = -1;
    xSemaphoreTake(s_sockMutex, portMAX_DELAY);
    sock = s_sock;
    xSemaphoreGive(s_sockMutex);
    return sock;
}

static void closeSocket(void) {
    ensureSockMutex();
    xSemaphoreTake(s_sockMutex, portMAX_DELAY);
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
    xSemaphoreGive(s_sockMutex);
}

// Set by igate_request_reconnect() (typically the web-admin POST handler,
// off the igate task) and consumed by igateTask() alongside the silence
// timer, so a changed login identity or server slot takes effect on the very
// next loop iteration instead of waiting for the link to drop on its own.
// Plain bool: every write is 0/1 from a single call site pattern (set here,
// clear in igateTask()), so the usual read/clear race costs at worst one
// extra reconnect, never a missed one.
static volatile bool s_reconnectRequested = false;

// Set by igate_request_filter_update() and consumed by igateTask() to push
// g_config.aprs_filter to an already-open session via the APRS-IS live
// filter-update comment line, without dropping the socket. Same race
// tolerance as s_reconnectRequested.
static volatile bool s_filterUpdateRequested = false;

void igate_request_reconnect(void) {
    s_reconnectRequested = true;
}

void igate_request_filter_update(void) {
    s_filterUpdateRequested = true;
}

// Index into g_config.aprs_server of the slot the next connectAprsIs() call
// will try. Advanced by advanceServer() every time a connection attempt
// fails, so the rotation keeps moving forward across calls instead of
// hammering the same failed server; wraps back to the first enabled slot
// after the last one, giving the failover an endless round-robin.
static uint8_t s_serverIdx = 0;

// esp_timer_get_time() timestamp (microseconds) at which the currently (or
// most recently) published socket completed login in connectAprsIs(). Used
// by the RX loop to tell a server that accepts the login and then drops the
// session almost immediately - a full server, one in maintenance, a load
// balancer with no live backend - apart from a normal link that simply drops
// after a long, healthy run. Only the former should trigger a failover.
static int64_t s_sessionStartUs = 0;

// esp_timer_get_time() timestamp (microseconds) of the most recent byte
// actually read off the APRS-IS socket, set alongside s_sessionStartUs when
// a session starts and refreshed on every recv() that returns data -
// including server comment lines starting with '#', since a quiet channel
// with nothing to gate still produces those on a healthy link. This is the
// only signal that distinguishes a socket that is merely idle from one whose
// far end has gone silent: unlike net_state_is_connected(), which only
// catches the station's own Wi-Fi dropping, this catches the link staying
// nominally "connected" while nothing is actually flowing - an evicted NAT
// mapping, a blackholed route, or a peer that hung without a FIN.
static int64_t s_lastRxUs = 0;

// A session shorter than this is treated as the connected server rejecting
// or failing to sustain the link rather than a transient network blip, and
// triggers an immediate failover to the next enabled slot. Long enough that
// a normal link dropping after hours of healthy use stays pinned to its
// server, short enough that a server which accepts and immediately closes
// is not retried for minutes before the rotation moves on.
#define IGATE_MIN_SESSION_US (60LL * 1000000LL)

// Longest stretch the RX loop tolerates with nothing at all read off the
// socket before treating the link as dead and forcing a reconnect. Servers
// following aprs-is.net's connection guidance send a '#' comment line on a
// cadence well under a minute whenever there is no other traffic, so this
// margin is comfortably above that cadence while staying short enough to
// recover well within the eviction time of a typical NAT/firewall's idle-TCP
// table entry.
#define IGATE_RX_SILENCE_US (90LL * 1000000LL)

// Moves s_serverIdx to the next enabled slot, wrapping circularly through
// g_config.aprs_server. Disabled slots are skipped entirely rather than
// pausing the rotation on them. If every slot is disabled, s_serverIdx is
// simply left where it was: currentServer() below falls back to slot 0 in
// that case so the IGate still has a destination to attempt and log.
static void advanceServer(void) {
    app_config_lock();
    for (uint8_t step = 0; step < APRS_SERVER_NUM; step++) {
        s_serverIdx = (uint8_t)((s_serverIdx + 1) % APRS_SERVER_NUM);
        if (g_config.aprs_server[s_serverIdx].enable)
            break;
    }
    app_config_unlock();
}

// Called right after a post-login session ends (server closed the connection
// or recv() failed). A session shorter than IGATE_MIN_SESSION_US means the
// server accepted the login and then dropped the link almost immediately, so
// this rotates to the next enabled slot exactly like a connection-establishment
// failure does; a session that ran longer than that is left pinned to its
// current slot, since a healthy link dropping after a long run is not a
// reason to abandon an otherwise-working server.
static void failoverIfShortSession(void) {
    int64_t elapsed = esp_timer_get_time() - s_sessionStartUs;
    if (elapsed < IGATE_MIN_SESSION_US) {
        ESP_LOGW(TAG, "APRS-IS session lasted %lld ms, failing over to next APRS-IS server", (long long)(elapsed / 1000));
        advanceServer();
    }
}

// Copies the server slot connectAprsIs() should try right now into *host/
// *port, under the config lock. Falls back to slot 0 whenever s_serverIdx
// itself is disabled (e.g. every slot got disabled from the web UI after the
// rotation had already selected one of them), so the IGate always has a
// concrete destination to dial instead of silently doing nothing. The copy
// is bounded by both the source field size and hostSize, so a caller buffer
// larger than the stored field never reads past the end of the config slot,
// and a caller buffer smaller than the stored field truncates instead of
// overflowing. host is always left NUL-terminated within hostSize, and a
// hostSize of 0 is a no-op.
static void currentServer(char *host, size_t hostSize, uint16_t *port) {
    if (hostSize == 0)
        return;
    app_config_lock();
    uint8_t idx = g_config.aprs_server[s_serverIdx].enable ? s_serverIdx : 0;
    size_t n = sizeof(g_config.aprs_server[idx].host);
    if (n > hostSize)
        n = hostSize;
    memcpy(host, g_config.aprs_server[idx].host, n);
    *port = g_config.aprs_server[idx].port;
    app_config_unlock();
    host[hostSize - 1] = 0;
}

static bool connectAprsIs(void) {
    // Snapshot everything this function needs from g_config up front, under the
    // config lock, so the web task rewriting these strings during a settings
    // save can't tear a value out from under the DNS lookup / login below (this
    // runs on the long-lived igate task, entirely asynchronous to saves). The
    // lock is released immediately; all network I/O uses these local copies.
    char cfg_host[20];
    char cfg_mycall[10];
    char cfg_passcode[6];
    char cfg_filter[30];
    uint16_t cfg_port;
    uint8_t cfg_ssid;
    currentServer(cfg_host, sizeof(cfg_host), &cfg_port);
    app_config_lock();
    memcpy(cfg_mycall, g_config.aprs_mycall, sizeof(cfg_mycall));
    memcpy(cfg_passcode, g_config.aprs_passcode, sizeof(cfg_passcode));
    memcpy(cfg_filter, g_config.aprs_filter, sizeof(cfg_filter));
    cfg_ssid = g_config.aprs_ssid;
    app_config_unlock();

    // cfg_host is already NUL-terminated by currentServer(). The three
    // memcpy() calls above copy the full field width, so termination for
    // those depends on what the config loader stored. Force it here for all
    // of them: everything below - the sanitizing loop, the "%s" conversions
    // in the login line, getaddrinfo() - treats these as C strings, and a
    // field filled edge to edge would send all of them reading past the end
    // of the local buffer.
    cfg_host[sizeof(cfg_host) - 1] = 0;
    cfg_mycall[sizeof(cfg_mycall) - 1] = 0;
    cfg_passcode[sizeof(cfg_passcode) - 1] = 0;
    cfg_filter[sizeof(cfg_filter) - 1] = 0;

    // The identity this station logs in under is its callsign-SSID, the same
    // string stationIdentity() puts after the q construct on gated frames and
    // the same one its beacons carry as their source callsign. The server
    // compares all three: aprsc delivers a message from APRS-IS only to the
    // client whose login equals the addressee byte for byte
    // (aprs-is.net/IGateDetails.aspx), and treats a packet as originated by
    // the client - rewriting its path to TCPIP* rather than tagging it as
    // relayed through a server - only when its source callsign equals the
    // login. An identity that differs by an SSID is a different station to
    // the server, so this station would never receive a message addressed to
    // the SSID it beacons under. The passcode is no obstacle: it is derived
    // from the base callsign with the SSID stripped, which is why every
    // callsign-SSID of one station shares one passcode.
    char cfg_identity[IGATE_IDENTITY_SIZE];
    stationIdentity(cfg_mycall, cfg_ssid, cfg_identity, sizeof(cfg_identity));

    // APRS-IS is a line-oriented protocol: the login below is a single raw
    // "user <call> pass <code> vers ... filter <spec>\r\n" line, and every one
    // of those three fields is free-form user input (web "IGate" page) that
    // lands in it verbatim. An embedded CR/LF in any of them - reachable via
    // the form's percent-encoding, e.g. "%0D%0A", or via a config.json written
    // outside the web UI - would inject additional attacker-controlled lines
    // into the session right after login, a classic protocol/command-injection
    // gap. Sanitizing all three here, at the single point where the login line
    // is built, is cheaper and more durable than reasoning about which field
    // validators each entry path happens to pass through: none of a callsign,
    // a numeric passcode or a server-side filter spec can legitimately contain
    // a line break, so replacing one with a space costs nothing.
    char *const login_fields[] = { cfg_identity, cfg_passcode, cfg_filter };
    for (size_t f = 0; f < sizeof(login_fields) / sizeof(login_fields[0]); f++) {
        for (char *p = login_fields[f]; *p; p++) {
            if (*p == '\r' || *p == '\n')
                *p = ' ';
        }
    }

    // The web UI (page_igate.c) validates this string's grammar at save
    // time, but the field is a raw g_config value that could also have been
    // edited outside the web UI (NVS import, direct flash, future config
    // tools). Re-check here and log - not block, the string is still sent
    // verbatim - so a bad filter shows up as an explicit log line instead of
    // only as unexplained "no RX traffic".
    {
        char reason[100];
        if (!aprs_filter_validate_server_string(cfg_filter, reason, sizeof(reason))) {
            ESP_LOGW(TAG, "APRS-IS filter string may be malformed: %s", reason);
        }
    }

    struct addrinfo hints = { 0 };
    struct addrinfo *res = NULL;
    char portStr[8];
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portStr, sizeof(portStr), "%u", (unsigned)cfg_port);

    if (getaddrinfo(cfg_host, portStr, &hints, &res) != 0 || res == NULL) {
        ESP_LOGW(TAG, "DNS lookup failed for %s, failing over to next APRS-IS server", cfg_host);
        advanceServer();
        return false;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        advanceServer();
        return false;
    }

    // recv() timeout stays at 10 s (the RX loop uses that cadence to re-check
    // the igate_en toggle). send() gets a tighter 3 s timeout: sendToAprsIs()
    // holds s_sockMutex across send(), so a stalled uplink would otherwise
    // block every other sender (digi loc2inet beacon, igate_send_raw) and
    // igate_is_connected() for the full timeout. APRS-IS frames are tiny, so
    // 3 s is ample for a healthy link and caps the worst-case stall.
    struct timeval rcv_tv = { .tv_sec = 10, .tv_usec = 0 };
    struct timeval snd_tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &rcv_tv, sizeof(rcv_tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &snd_tv, sizeof(snd_tv));

    // APRS-IS is bidirectional on this station (RF->INET gatewaying, outbound
    // messages and beacons all flow out over the same link that receives INET
    // traffic), and aprs-is.net/Connecting.aspx asks bidirectional clients to
    // disable the Nagle algorithm so outgoing lines are not delayed waiting to
    // coalesce with further writes. Every APRS-IS line here is already
    // assembled into a single send() before it reaches the wire, so disabling
    // Nagle lets that single write leave immediately instead of waiting on an
    // ACK or a Nagle timeout.
    int noDelay = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));

    // Backstop for the application-level silence timer below, not a
    // substitute for it: lwIP's own probes catch a peer that stops
    // acknowledging at the TCP layer (a dead route, a vanished NAT mapping)
    // even faster than IGATE_RX_SILENCE_US would, and cost nothing to leave
    // on. They do not, on their own, catch a peer that keeps acknowledging
    // but simply stops sending application data, which is why the RX loop's
    // own s_lastRxUs check still runs regardless of this.
    int keepAlive = 1;
    int keepIdleSec = 30;
    int keepIntervalSec = 10;
    int keepCount = 3;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(keepAlive));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdleSec, sizeof(keepIdleSec));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepIntervalSec, sizeof(keepIntervalSec));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(keepCount));

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGW(TAG, "Connect to %s:%u failed: errno %d, failing over to next APRS-IS server", cfg_host, (unsigned)cfg_port, errno);
        close(sock);
        freeaddrinfo(res);
        advanceServer();
        return false;
    }
    freeaddrinfo(res);

    // The login line is assembled once and then both logged and sent from this
    // one buffer, so what the logs show can never drift from what the server
    // actually received. The mandatory part identifies the login and the
    // software behind it (APRS_SOFTWARE_NAME/APRS_SOFTWARE_VERSION name this
    // firmware, which is what lets a server operator reach its author). The
    // "filter" command takes one or more filter terms, so the whole clause is
    // appended only when there is a term to send rather than leaving the
    // server a bare keyword to make sense of.
    char login[160];
    size_t loginLen = 0;
    str_append(login, sizeof(login), &loginLen, "user %s pass %s vers %s %s", cfg_identity, cfg_passcode, APRS_SOFTWARE_NAME, APRS_SOFTWARE_VERSION);
    if (cfg_filter[0])
        str_append(login, sizeof(login), &loginLen, " filter %s", cfg_filter);

    // Log the line as sent, minus the CR/LF that terminates it, so a bad
    // filter string (e.g. wrong filter letter, malformed args) is visible in
    // the logs instead of silently resulting in zero RX traffic. With no
    // filter configured the server applies its own default, which is worth a
    // line of its own since the sent line then says nothing about filtering.
    ESP_LOGI(TAG, "APRS-IS login: %s", login);
    if (!cfg_filter[0])
        ESP_LOGI(TAG, "No APRS-IS filter configured - the server applies its default, usually nothing");

    // str_append() clamps, so the only way the terminator does not fit is a
    // login line longer than the buffer, which would reach the server as a
    // partial - and therefore unparseable - command. Treat that as a failed
    // attempt on this server rather than sending it.
    if (!str_append(login, sizeof(login), &loginLen, "\r\n")) {
        ESP_LOGW(TAG, "APRS-IS login line too long to send, failing over to next APRS-IS server");
        close(sock);
        advanceServer();
        return false;
    }

    if (send(sock, login, loginLen, 0) != (int)loginLen) {
        close(sock);
        advanceServer();
        return false;
    }

    // Read the server's immediate response. javAPRSSrvr/aprsc reply with a
    // "# ... server ..." banner followed by a "# logresp CALL verified/unverified, server ..."
    // line right after login. Surfacing this tells the user right away if
    // their passcode or filter was rejected, rather than them having to
    // infer it later from a total absence of "APRS-IS RX:" lines.
    char resp[200];
    int rlen = recv(sock, resp, sizeof(resp) - 1, 0);
    if (rlen > 0) {
        resp[rlen] = 0;
        ESP_LOGI(TAG, "APRS-IS server banner: %s", resp);
        if (strstr(resp, "unverified")) {
            ESP_LOGW(TAG, "APRS-IS login unverified - check aprs_mycall/aprs_passcode");
        }
        // The server echoes the identity it accepted in its "# logresp <call>
        // ..." line. Comparing it against what was sent turns a silent
        // mismatch - the one failure mode that leaves messages addressed to
        // this station undelivered while everything else looks healthy - into
        // a log line an operator can act on.
        const char *logresp = strstr(resp, "logresp ");
        if (logresp != NULL) {
            logresp += 8;
            size_t idLen = strlen(cfg_identity);
            if (strncmp(logresp, cfg_identity, idLen) != 0 || (logresp[idLen] != ' ' && logresp[idLen] != ',')) {
                ESP_LOGW(TAG, "APRS-IS server accepted a different identity than the one sent (%s) - messages addressed to this station may not arrive",
                         cfg_identity);
            }
        }
    } else {
        ESP_LOGW(TAG, "No banner/login response received from APRS-IS server within timeout");
    }

    ensureSockMutex();
    xSemaphoreTake(s_sockMutex, portMAX_DELAY);
    s_sock = sock;
    s_sessionStartUs = esp_timer_get_time();
    // Seeded here, not left at 0, so a server that accepts the login and then
    // sends nothing further is caught by the same silence timer as a link
    // that goes quiet mid-session - the RX loop's very first pass already has
    // a meaningful "last heard from" instant to measure against.
    s_lastRxUs = s_sessionStartUs;
    xSemaphoreGive(s_sockMutex);
    ESP_LOGI(TAG, "Connected to APRS-IS %s:%u as %s", cfg_host, (unsigned)cfg_port, cfg_identity);
    trafficlog_add("Connected to APRS-IS %s:%u as %s", cfg_host, (unsigned)cfg_port, cfg_identity);
    return true;
}

// The APRS-IS TCP uplink is a single shared resource used not only by the
// IGate itself (rf2inet/inet2rf) but also by the Digipeater's and Tracker's
// "beacon to internet" options and by outbound messages sent over the
// internet channel. The uplink task always runs and gates on this helper, so
// the uplink comes up or goes down live as any of these settings is toggled
// from the web UI, without a reboot.
//
// trk_loc2inet and igate_loc2inet are the Tracker/IGate pages' own "beacon via
// internet" checkboxes (see beacon.c); each is a standalone setting independent
// of igate_en. They are included here so the uplink activates for every case
// that actually needs to send over APRS-IS, not just the ones that also flip
// igate_en/msg_inet.
static bool igateUplinkNeeded(void) {
    return g_config.igate_en || g_config.digi_loc2inet || g_config.msg_inet || g_config.trk_loc2inet || g_config.igate_loc2inet;
}

static void igateTask(void *arg) {
    // Sized to hold the longest line APRS-IS may legally send (the 512-byte
    // limit's usable payload) plus the NUL terminator this buffer is always
    // kept holding, so a full-length line is never itself the overflow case.
    char line[APRS_IS_LINE_MAX + 1];
    size_t linePos = 0;
    // Set for the remainder of an over-length line once linePos has already
    // saturated the buffer, so every further byte up to the next terminator
    // is consumed without being written anywhere, and the line is discarded
    // as a whole rather than processed as a truncated fragment.
    bool discarding = false;
    bool waitingLogged = false;

    for (;;) {
        if (!igateUplinkNeeded()) {
            closeSocket();
            waitingLogged = false;
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        // IGate must not start (or must drop back off) until we actually
        // have an internet route, not merely "WiFi is configured". While
        // offline, keep polling at a fast 1 s interval so we connect to
        // APRS-IS as soon as possible once the network comes up.
        if (!net_state_is_connected()) {
            closeSocket();
            if (!waitingLogged) {
                ESP_LOGW(TAG, "No internet connection yet - IGate waiting, retrying every 1 s");
                waitingLogged = true;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (waitingLogged) {
            ESP_LOGI(TAG, "Internet connection available - starting IGate / APRS-IS connection");
            waitingLogged = false;
        }

        int sock = socketSnapshot();
        if (sock < 0) {
            if (!connectAprsIs()) {
                // Failover: connectAprsIs() has already advanced s_serverIdx
                // to the next server in the rotation on failure, so this
                // fixed 1 s wait is the interval between successive attempts
                // against that circular list of servers, not a single-server
                // backoff - the retry keeps cycling through every enabled
                // slot forever until one of them accepts the connection.
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            linePos = 0;
            discarding = false;
            sock = socketSnapshot();
            if (sock < 0)
                continue; // closed again between the connect and this read
        }

        // Dead-link detection: a socket can stay open with recv() returning
        // nothing but EAGAIN forever - an evicted NAT mapping, a blackholed
        // route, a peer that hung without sending a FIN - none of which
        // net_state_is_connected() can see, since Wi-Fi itself stays
        // associated throughout. A healthy link never goes this long without
        // at least a server '#' comment, so exceeding IGATE_RX_SILENCE_US
        // means the other end is no longer actually feeding this station;
        // closing here hands off to the same reconnect path used for every
        // other kind of drop.
        if (esp_timer_get_time() - s_lastRxUs > IGATE_RX_SILENCE_US) {
            ESP_LOGW(TAG, "No data received from APRS-IS for over %lld s, reconnecting", (long long)(IGATE_RX_SILENCE_US / 1000000LL));
            trafficlog_add("APRS-IS link silent for over %lld s, reconnecting", (long long)(IGATE_RX_SILENCE_US / 1000000LL));
            closeSocket();
            continue;
        }

        // A changed login identity or server list (igate_request_reconnect(),
        // typically called from the web-admin POST handler right after
        // g_config is updated) must not wait for the link to drop on its own,
        // so it is treated exactly like the silence timer above: close here
        // and fall into the normal reconnect path, which rebuilds the login
        // line from the now-current g_config. Cleared unconditionally so a
        // request arriving between the read and the clear is not lost - it is
        // instead honoured on the very next iteration. A pending filter-only
        // update is dropped here rather than sent twice: the reconnect below
        // logs in with g_config.aprs_filter already current.
        if (s_reconnectRequested) {
            s_reconnectRequested = false;
            s_filterUpdateRequested = false;
            ESP_LOGI(TAG, "APRS-IS identity/server settings changed, reconnecting");
            trafficlog_add("APRS-IS identity/server settings changed, reconnecting");
            closeSocket();
            continue;
        }

        // A filter-only change (igate_request_filter_update()) is pushed to
        // the already-open session with APRS-IS's live filter-update comment
        // line: aprs-is.net/javAPRSFilter.aspx documents the filter command
        // as also acceptable "as a separate comment line", so the session
        // survives the change instead of being torn down. Read under the
        // config lock so a save that is still in progress cannot hand this a
        // half-written string. Cleared before sending so a request that
        // arrives while this send is in flight is not lost.
        if (s_filterUpdateRequested) {
            s_filterUpdateRequested = false;
            char newFilter[30];
            app_config_lock();
            memcpy(newFilter, g_config.aprs_filter, sizeof(newFilter));
            app_config_unlock();
            newFilter[sizeof(newFilter) - 1] = 0;
            if (newFilter[0]) {
                char cmd[40];
                size_t cmdLen = 0;
                str_append(cmd, sizeof(cmd), &cmdLen, "#filter %s", newFilter);
                sendToAprsIs((const uint8_t *)cmd, cmdLen);
                ESP_LOGI(TAG, "APRS-IS filter updated live: %s", newFilter);
                trafficlog_add("APRS-IS filter updated live: %s", newFilter);
            }
        }

        char buf[256];
        int r = recv(sock, buf, sizeof(buf), 0);
        if (r > 0) {
            s_lastRxUs = esp_timer_get_time();
            for (int i = 0; i < r; i++) {
                char c = buf[i];
                if (c == '\n' || c == '\r') {
                    if (discarding) {
                        // The overflowing line ends here: drop it whole and
                        // resynchronise on the next line's first byte,
                        // rather than handing anything from it downstream.
                        discarding = false;
                        linePos = 0;
                    } else if (linePos > 0) {
                        line[linePos] = 0;
                        if (line[0] != '#') { // '#' = server comment/keepalive
                            // Counts every packet received from APRS-IS,
                            // regardless of whether inet2rf is enabled or the
                            // line ends up relayed to RF below - this is the
                            // total internet RX traffic, not just what was
                            // actually transmitted on RF.
                            s_stats.isRxCount++;
                            // Information-level log of every message received
                            // from APRS-IS, regardless of whether inet2rf is
                            // enabled below, so all igate traffic is visible.
                            ESP_LOGI(TAG, "APRS-IS RX: %s", line);
                            {
                                char dx[16];
                                tnc2SrcCallsign(line, strlen(line), dx, sizeof(dx));

                                // Position/object/item reports start their info
                                // field (right after the first ':') with one of
                                // !=/@; the symbol table and symbol code follow
                                // the latitude/longitude fields. Handles both
                                // no-timestamp ('!'/'=') and timestamped
                                // ('/'/'@') formats - see aprs_extract_symbol().
                                // A payload with no symbol of its own falls
                                // back to the destination address and then
                                // the source SSID of the TNC2 header, in the
                                // precedence order of APRS101 chapter 21.
                                char symTable = 0, symCode = 0;
                                char decoded[APRS_RX_DECODED_BUF_SIZE] = "";
                                const char *colon = strchr(line, ':');
                                if (colon) {
                                    const char *info = colon + 1;
                                    size_t infoLen = strlen(info);
                                    if (!aprs_extract_symbol(info, infoLen, &symTable, &symCode) && infoLen > 0)
                                        aprs_symbol_from_tnc2_header(line, info[0], &symTable, &symCode);

                                    // Same decode as the RF path, and the one
                                    // place where it earns more than display:
                                    // a packet relayed through APRS-IS reaches
                                    // this station later than it was sent, so
                                    // its own timestamp is the only statement
                                    // of when the sender was where it says.
                                    char destCall[10];
                                    aprs_rx_report_t report;
                                    const char *dest = aprs_tnc2_dest_call(line, destCall, sizeof(destCall)) ? destCall : NULL;
                                    if (aprs_filter_decode_report(info, dest, &report))
                                        aprs_filter_format_report(&report, decoded, sizeof(decoded));
                                }

                                trafficlog_add_pkt("RX-IS", dx, line, decoded, -1, symTable, symCode);

                                // Mic-E position comment (APRS101 ch.10),
                                // carried in the destination address and so
                                // absent from the packet text logged above.
                                // The internet feed reaches this station
                                // through its own server-side filter, which
                                // is a local one, so an emergency arriving
                                // here is as close by as one heard on the
                                // radio and is raised the same way.
                                if (colon) {
                                    const char *info = colon + 1;
                                    char destCall[10];
                                    const char *miceMsg = NULL;
                                    bool miceEmergency = false;
                                    if (aprs_tnc2_dest_call(line, destCall, sizeof(destCall)) &&
                                        aprs_filter_mice_message(destCall, info, strlen(info), &miceMsg, &miceEmergency)) {
                                        if (miceEmergency) {
                                            ESP_LOGW(TAG, "Mic-E EMERGENCY from %s (APRS-IS)", dx);
                                            trafficlog_add("Mic-E EMERGENCY from %s (APRS-IS)", dx);
                                        } else {
                                            ESP_LOGI(TAG, "Mic-E position comment from %s (APRS-IS): %s", dx, miceMsg);
                                        }
                                    }

                                    // Bracketed comment-field alert code
                                    // (aprs.org/aprs12/EmergencyCode.txt), the
                                    // non-Mic-E equivalent of the check above.
                                    // Same reasoning as the Mic-E case applies
                                    // here: the internet feed's own filter is
                                    // local, so an emergency arriving this way
                                    // is raised the same as one heard on radio.
                                    const char *alertName = NULL;
                                    bool alertEmergency = false;
                                    if (aprs_filter_comment_alert(info, strlen(info), &alertName, &alertEmergency)) {
                                        if (alertEmergency) {
                                            ESP_LOGW(TAG, "EMERGENCY from %s (APRS-IS)", dx);
                                            trafficlog_add("EMERGENCY from %s (APRS-IS)", dx);
                                        } else {
                                            ESP_LOGI(TAG, "Comment alert from %s (APRS-IS): %s", dx, alertName);
                                        }
                                    }
                                }
                            }
                            if (g_config.inet2rf && s_inet2rfHandler)
                                s_inet2rfHandler(line);
                        } else {
                            ESP_LOGD(TAG, "APRS-IS keepalive: %s", line);
                        }
                        linePos = 0;
                    }
                } else if (linePos < sizeof(line) - 1) {
                    line[linePos++] = c;
                } else if (!discarding) {
                    // The line has exceeded the APRS-IS 512-byte limit:
                    // every byte received so far is unusable as a whole
                    // line, so stop accumulating and discard the rest up to
                    // the next terminator instead of processing a truncated
                    // remainder as if it were a complete packet.
                    discarding = true;
                    linePos = 0;
                    s_stats.dropByReason[DROP_IS_RX_LINE_TOO_LONG]++;
                    ESP_LOGW(TAG, "APRS-IS RX line exceeded %u bytes - discarding until next line", (unsigned)sizeof(line));
                }
            }
        } else if (r == 0) {
            ESP_LOGW(TAG, "APRS-IS connection closed by server");
            trafficlog_add("APRS-IS connection closed by server");
            failoverIfShortSession();
            closeSocket();
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ETIMEDOUT) {
            ESP_LOGW(TAG, "recv() error errno %d", errno);
            failoverIfShortSession();
            closeSocket();
        }
        // EAGAIN/timeout: just loop, gives the "igate_en toggled off" check a chance to run.
    }
}

void igate_get_current_server(char *host, size_t hostLen, uint16_t *port) {
    currentServer(host, hostLen, port);
}

// The uplink task is started once and runs for the lifetime of the firmware.
// Enabling or disabling any of the settings that need APRS-IS does not stop
// it: igateUplinkNeeded() is re-evaluated on every pass, so the task simply
// closes the socket and idles until one of them is turned back on. s_task is
// therefore only ever set, and guards against a second task being created.
void igate_start(void) {
    if (s_task != NULL)
        return; // already running
    ensureSockMutex();
    xTaskCreate(igateTask, "igate_task", 6144, NULL, 5, &s_task);
}
