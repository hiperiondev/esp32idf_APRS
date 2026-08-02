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
// @brief APRS-IS Internet Gateway implementation: TCP client task with login and
// auto-reconnect, RF->INET gatewaying with filtering and duplicate suppression,
// INET->RF relaying, and gateway traffic statistics.

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_config.h"
#include "aprs_coord.h"
#include "aprs_filter.h"
#include "crc_ccit.h"
#include "igate.h"
#include "net_state.h"
#include "str_append.h"
#include "trafficlog.h"

static const char *TAG = "igate";

struct DupPacketCache {
    char hash[16];
    unsigned long timestamp;
};

// Ordinary .bss, not RTC slow memory: this firmware never enters deep sleep, so
// there is nothing for RTC placement to preserve. Keeping the counters here
// leaves that scarce memory for something that needs it, and makes them reset
// on esp_restart() - which is what the dashboard should show after an OTA
// reboot, rather than totals carried over from the previous firmware image.
static igate_stats_t s_stats;
static struct DupPacketCache s_dupCache[DUP_PACKET_CACHE_SIZE];
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
// creation against a race between concurrent first callers.
static void ensureSockMutex(void) {
    if (s_sockMutex)
        return;
    portENTER_CRITICAL(&s_sockMutexInitLock);
    if (!s_sockMutex)
        s_sockMutex = xSemaphoreCreateMutex();
    portEXIT_CRITICAL(&s_sockMutexInitLock);
}
static volatile bool s_running;

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

igate_stats_t igate_get_stats(void) {
    return s_stats;
}

void igate_note_drop(drop_reason_t reason) {
    if ((unsigned)reason < DROP_REASON_COUNT)
        s_stats.dropByReason[reason]++;
}

const char *igate_drop_reason_name(drop_reason_t reason) {
    switch (reason) {
        case DROP_DUP:
            return "duplicate";
        case DROP_TOO_SHORT:
            return "too short";
        case DROP_PATH_TOKEN:
            return "RFONLY/TCP/qA/NOGATE";
        case DROP_SAT_NOT_USED:
            return "satellite not used";
        case DROP_TYPE_FILTER:
            return "type filter";
        case DROP_RANGE_FILTER:
            return "range filter";
        case DROP_PREFIX_FILTER:
            return "prefix filter";
        case DROP_BUDLIST:
            return "callsign filter";
        case DROP_TX_FAIL:
            return "TX failed";
        case DROP_HEADER_OVERFLOW:
            return "header overflow";
        case DROP_PLACEHOLDER_CALL:
            return "placeholder callsign (NOCALL/MYCALL)";
        case DROP_MODEM_NOT_READY:
            return "modem not ready";
        case DROP_TX_QUEUE_FULL:
            return "RF TX queue full";
        case DROP_TX_TOO_LONG:
            return "TX packet too long";
        case ERR_MODEM_SEND_FAIL:
            return "modem send failed";
        case ERR_AX25_DECODE:
            return "AX.25 decode error";
        case DROP_DIGI_MALFORMED:
            return "digi: malformed packet";
        case DROP_DIGI_PLACEHOLDER_CALL:
            return "digi: placeholder callsign (NOCALL/MYCALL)";
        case DROP_DIGI_ALREADY_USED:
            return "digi: already used";
        case DROP_DIGI_PATH_FULL:
            return "digi: path full";
        case DROP_DIGI_NO_PATH:
            return "digi: no usable path";
        case DROP_DIGI_PATH_TOKEN:
            return "digi: qA/TCP path token";
        case DROP_PERSISTENCE_MISSED:
            return "persistence check missed";
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
// Seeds the key with the source callsign, source SSID and payload length,
// then mixes in two CRC-CCITT digests of the *entire* info field - one
// computed forward, one computed backward - so that any byte anywhere in
// the payload (not just an early prefix) changes the resulting hash. Two
// frames only produce the same hash if they genuinely share source,
// length, and byte-for-byte payload content.
static void packetHash(ax25_msg_t *packet, char *hash) {
    int n = snprintf(hash, 16, "%s%d%d", packet->src.call, packet->src.ssid, (int)packet->len);
    if (n < 0)
        n = 0;

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
    for (uint8_t i = 0; i < DUP_PACKET_CACHE_SIZE; i++) {
        if (s_dupCache[i].timestamp > 0 && (now - s_dupCache[i].timestamp) > DUP_PACKET_TIMEOUT_MS) {
            s_dupCache[i].timestamp = 0;
        }
    }
}

bool isDuplicatePacket(ax25_msg_t *packet) {
    char hash[16] = { 0 };
    packetHash(packet, hash);

    unsigned long now = (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    clearExpiredDuplicates();

    for (uint8_t i = 0; i < DUP_PACKET_CACHE_SIZE; i++) {
        // hash is fixed-width binary data (post-XOR bytes may legally be NUL
        // partway through), not a C string, so compare it with memcmp() over
        // the full 16 bytes rather than strncmp(), which stops at the first
        // embedded NUL and could otherwise call two different hashes equal.
        if (s_dupCache[i].timestamp > 0 && memcmp(s_dupCache[i].hash, hash, 16) == 0) {
            ESP_LOGD(TAG, "Duplicate packet detected");
            return true;
        }
    }

    memcpy(s_dupCache[s_dupCacheIndex].hash, hash, 16);
    s_dupCache[s_dupCacheIndex].timestamp = now;
    s_dupCacheIndex = (s_dupCacheIndex + 1) % DUP_PACKET_CACHE_SIZE;
    return false;
}

// ---------------------------------------------------------------------------
// RF -> INET
// ---------------------------------------------------------------------------
static bool sendToAprsIs(const uint8_t *data, size_t len) {
    bool ok = false;
    ensureSockMutex();
    xSemaphoreTake(s_sockMutex, portMAX_DELAY);
    if (s_sock >= 0) {
        if (send(s_sock, data, len, 0) == (ssize_t)len && send(s_sock, "\r\n", 2, 0) == 2) {
            ok = true;
        }
    }
    xSemaphoreGive(s_sockMutex);

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
        trafficlog_add_pkt("TX", dx, pkt, -1, 0, 0);
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
        trafficlog_add_pkt("TX-FAIL", dx, pkt, -1, 0, 0);
    }
    return ok;
}

bool igate_send_raw(const char *line, size_t len) {
    return sendToAprsIs((const uint8_t *)line, len);
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
    static const struct {
        const char *call;
        size_t len;
    } satGates[] = {
        { "RS0ISS", 6 }, { "YBOX", 4 }, { "YBSAT", 5 }, { "PSAT", 4 }, { "W3ADO", 5 }, { "BJ1SI", 5 },
    };
    for (idx = 0; idx < packet->rpt_count; idx++) {
        for (size_t s = 0; s < sizeof(satGates) / sizeof(satGates[0]); s++) {
            if (!strncmp(packet->rpt_list[idx].call, satGates[s].call, satGates[s].len)) {
                if (!AX25_REPEATED(packet, idx)) {
                    s_stats.dropByReason[DROP_SAT_NOT_USED]++;
                    return 0;
                }
            }
        }
    }

    // [IGATE] Filter (RF->INET): payload-type whitelist configured on the web
    // IGATE Filter page ("RF -> INET" fieldset). g_config.rf2inetFilter was
    // computed from the checkboxes and persisted (page_igate.c) but nothing
    // ever consulted it here, so every checkbox combination - including
    // "everything unchecked" - gated identically (all payload types passed
    // through, unfiltered). Only the INET->RF half of this same feature
    // (aprs_service.c, g_config.inet2rfFilter) was actually enforced. Apply
    // the same classify+whitelist check symmetrically on this side, using the
    // AX.25 info field directly (NUL-terminated copy; packet->info is not
    // itself NUL-terminated).
    char info[AX25_FRAME_MAX_SIZE + 1];
    {
        size_t n = packet->len < AX25_FRAME_MAX_SIZE ? packet->len : AX25_FRAME_MAX_SIZE;
        memcpy(info, packet->info, n);
        info[n] = 0;
        uint16_t type = aprs_filter_classify_info(info);
        if (!aprs_filter_pass(g_config.rf2inetFilter, type)) {
            ESP_LOGD(TAG, "RF2INET filtered (%s, mask=0x%03X): %.*s", aprs_filter_type_name(type), (unsigned)g_config.rf2inetFilter, (int)packet->len, info);
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

        if (rangeEn && rangeKm > 0.0f) {
            float plat, plon;
            if (aprs_filter_decode_position(info, packet->dst.call, &plat, &plon)) {
                float d = aprs_filter_haversine_km(ownLat, ownLon, plat, plon);
                if (d > rangeKm) {
                    ESP_LOGD(TAG, "RF2INET range-filtered (%.1f km > %.1f km): %s", d, rangeKm, packet->src.call);
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

        if (prefixEn && !aprs_filter_prefix_match(packet->src.call, prefixes)) {
            ESP_LOGD(TAG, "RF2INET prefix-filtered (%s not in \"%s\")", packet->src.call, prefixes);
            s_stats.dropByReason[DROP_PREFIX_FILTER]++;
            return 0;
        }
    }

    // Local callsign whitelist/blacklist (RF->INET direction): independent of
    // - and composes with (AND semantics) - the type filter just above. Keyed
    // on the source callsign only (SSID stripped inside the helper), same as
    // aprs_service.c's INET->RF handler below applies it on its own side.
    if (!aprs_filter_budlist_pass(g_config.rf2inet_budlist_mode, packet->src.call)) {
        ESP_LOGD(TAG, "RF2INET budlist-filtered (mode=%d): %s", (int)g_config.rf2inet_budlist_mode, packet->src.call);
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
        if (packet->rpt_flags & (1 << i))
            str_append(header, sizeof(header), &headerLen, "*");
    }

    // Snapshot the own-station identity used to build the qAR/qAO header. This
    // runs on the modem RX task; a concurrent web save could otherwise rewrite
    // aprs_mycall/igate_object mid-snprintf.
    char cfg_mycall[10];
    char cfg_object[10];
    uint8_t cfg_ssid;
    app_config_lock();
    memcpy(cfg_mycall, g_config.aprs_mycall, sizeof(cfg_mycall));
    memcpy(cfg_object, g_config.igate_object, sizeof(cfg_object));
    cfg_ssid = g_config.aprs_ssid;
    app_config_unlock();

    if (strlen(cfg_object) >= 3) {
        if (cfg_ssid > 0)
            str_append(header, sizeof(header), &headerLen, ",%s-%d*,qAO,%s", cfg_mycall, cfg_ssid, cfg_object);
        else
            str_append(header, sizeof(header), &headerLen, ",%s*,qAO,%s", cfg_mycall, cfg_object);
    } else {
        if (cfg_ssid > 0)
            str_append(header, sizeof(header), &headerLen, ",qAR,%s-%d", cfg_mycall, cfg_ssid);
        else
            str_append(header, sizeof(header), &headerLen, ",qAR,%s", cfg_mycall);
    }

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

    uint8_t frame[500];
    size_t fpos = 0;
    memcpy(&frame[fpos], header, headerLen);
    fpos += headerLen;

    // copy info field, stripping CR/LF, bounded to the frame buffer
    for (size_t i = 0; i < packet->len && fpos < sizeof(frame); i++) {
        uint8_t c = packet->info[i];
        if (c == '\r' || c == '\n')
            continue;
        frame[fpos++] = c;
    }

    if (sendToAprsIs(frame, fpos)) {
        s_stats.txCount++;
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
    app_config_lock();
    memcpy(cfg_host, g_config.aprs_host, sizeof(cfg_host));
    memcpy(cfg_mycall, g_config.aprs_mycall, sizeof(cfg_mycall));
    memcpy(cfg_passcode, g_config.aprs_passcode, sizeof(cfg_passcode));
    memcpy(cfg_filter, g_config.aprs_filter, sizeof(cfg_filter));
    cfg_port = g_config.aprs_port;
    cfg_ssid = g_config.aprs_ssid;
    app_config_unlock();
    (void)cfg_ssid;

    // Each memcpy above copies the full field width, so termination depends on
    // what the config loader stored. Force it here: everything below - the
    // sanitizing loop, the "%s" conversions in the login line, getaddrinfo() -
    // treats these as C strings, and a field filled edge to edge would send
    // all of them reading past the end of the local buffer.
    cfg_host[sizeof(cfg_host) - 1] = 0;
    cfg_mycall[sizeof(cfg_mycall) - 1] = 0;
    cfg_passcode[sizeof(cfg_passcode) - 1] = 0;
    cfg_filter[sizeof(cfg_filter) - 1] = 0;

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
    char *const login_fields[] = { cfg_mycall, cfg_passcode, cfg_filter };
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
        ESP_LOGW(TAG, "DNS lookup failed for %s", cfg_host);
        return false;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
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

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGW(TAG, "Connect to %s:%u failed: errno %d", cfg_host, (unsigned)cfg_port, errno);
        close(sock);
        freeaddrinfo(res);
        return false;
    }
    freeaddrinfo(res);

    char login[160];
    int n = snprintf(login, sizeof(login), "user %s pass %s vers ESP32APRS 1.0 filter %s\r\n", cfg_mycall, cfg_passcode, cfg_filter[0] ? cfg_filter : "");
    // Log exactly what we're sending (minus the trailing \r\n) so a bad
    // filter string (e.g. wrong filter letter, malformed args) is visible
    // in the logs instead of silently resulting in zero RX traffic.
    ESP_LOGI(TAG, "APRS-IS login: user %s pass %s vers ESP32APRS 1.0 filter %s", cfg_mycall, cfg_passcode,
             cfg_filter[0] ? cfg_filter : "(none - server default, usually nothing)");
    if (send(sock, login, n, 0) != n) {
        close(sock);
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
    } else {
        ESP_LOGW(TAG, "No banner/login response received from APRS-IS server within timeout");
    }

    ensureSockMutex();
    xSemaphoreTake(s_sockMutex, portMAX_DELAY);
    s_sock = sock;
    xSemaphoreGive(s_sockMutex);
    ESP_LOGI(TAG, "Connected to APRS-IS %s:%u as %s", cfg_host, (unsigned)cfg_port, cfg_mycall);
    trafficlog_add("Connected to APRS-IS %s:%u as %s", cfg_host, (unsigned)cfg_port, cfg_mycall);
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
    char line[512];
    size_t linePos = 0;
    bool waitingLogged = false;

    while (s_running) {
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
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            linePos = 0;
            sock = socketSnapshot();
            if (sock < 0)
                continue; // closed again between the connect and this read
        }

        char buf[256];
        int r = recv(sock, buf, sizeof(buf), 0);
        if (r > 0) {
            for (int i = 0; i < r; i++) {
                char c = buf[i];
                if (c == '\n' || c == '\r') {
                    if (linePos > 0) {
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
                                char symTable = 0, symCode = 0;
                                const char *colon = strchr(line, ':');
                                if (colon) {
                                    const char *info = colon + 1;
                                    size_t infoLen = strlen(info);
                                    aprs_extract_symbol(info, infoLen, &symTable, &symCode);
                                }

                                trafficlog_add_pkt("RX-IS", dx, line, -1, symTable, symCode);
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
                }
            }
        } else if (r == 0) {
            ESP_LOGW(TAG, "APRS-IS connection closed by server");
            trafficlog_add("APRS-IS connection closed by server");
            closeSocket();
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ETIMEDOUT) {
            ESP_LOGW(TAG, "recv() error errno %d", errno);
            closeSocket();
        }
        // EAGAIN/timeout: just loop, gives the "igate_en toggled off" check a chance to run.
    }

    closeSocket();
    s_task = NULL;
    vTaskDelete(NULL);
}

void igate_start(void) {
    if (s_task != NULL)
        return; // already running
    ensureSockMutex();
    s_running = true;
    xTaskCreate(igateTask, "igate_task", 6144, NULL, 5, &s_task);
}

void igate_stop(void) {
    s_running = false;
    closeSocket();
}
