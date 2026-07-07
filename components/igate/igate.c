#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_config.h"
#include "igate.h"
#include "net_state.h"
#include "trafficlog.h"

static const char *TAG = "igate";

struct DupPacketCache {
    char hash[16];
    unsigned long timestamp;
};

static RTC_DATA_ATTR igate_stats_t s_stats;
static struct DupPacketCache s_dupCache[DUP_PACKET_CACHE_SIZE];
static uint8_t s_dupCacheIndex = 0;

static int s_sock = -1;
static SemaphoreHandle_t s_sockMutex;
static portMUX_TYPE s_sockMutexInitLock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;

// s_sockMutex used to be created only in igate_start(), i.e. only when the
// IGate itself is enabled. But sendToAprsIs()/igate_send_raw() is also
// reachable from the Digipeater beacon task whenever digi_loc2inet is on,
// independent of igate_en. If the IGate was never started, that path took
// xSemaphoreTake() on a NULL handle and hit the FreeRTOS "pxQueue" assert.
// This helper makes mutex creation idempotent and safe to call from any
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
void igate_reset_stats(void) {
    memset(&s_stats, 0, sizeof(s_stats));
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
// Duplicate detection (unchanged algorithm from the original firmware)
// ---------------------------------------------------------------------------
static void packetHash(AX25Msg *packet, char *hash) {
    int n = snprintf(hash, 16, "%s%d%d", packet->src.call, packet->src.ssid, (int)packet->len);
    if (n < 0)
        n = 0;
    int mix = packet->len < 16 ? (int)packet->len : 16;
    for (int i = 0; i < mix; i++) {
        hash[i % 15] ^= packet->info[i];
    }
}

void clearExpiredDuplicates(void) {
    unsigned long now = (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    for (uint8_t i = 0; i < DUP_PACKET_CACHE_SIZE; i++) {
        if (s_dupCache[i].timestamp > 0 && (now - s_dupCache[i].timestamp) > DUP_PACKET_TIMEOUT_MS) {
            s_dupCache[i].timestamp = 0;
        }
    }
}

bool isDuplicatePacket(AX25Msg *packet) {
    char hash[16] = { 0 };
    packetHash(packet, hash);

    unsigned long now = (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    clearExpiredDuplicates();

    for (uint8_t i = 0; i < DUP_PACKET_CACHE_SIZE; i++) {
        if (s_dupCache[i].timestamp > 0 && strncmp(s_dupCache[i].hash, hash, 16) == 0) {
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
        ESP_LOGI(TAG, "APRS-IS TX: %.*s", (int)len, (const char *)data);
        trafficlog_add_pkt("TX", dx, pkt, -1, 0, 0);
    } else {
        ESP_LOGW(TAG, "APRS-IS TX failed (not connected?): %.*s", (int)len, (const char *)data);
        trafficlog_add_pkt("TX-FAIL", dx, pkt, -1, 0, 0);
    }
    return ok;
}

bool igate_send_raw(const char *line, size_t len) {
    return sendToAprsIs((const uint8_t *)line, len);
}

int igateProcess(AX25Msg *packet) {
    int idx;

    if (!g_config.igate_en || !g_config.rf2inet)
        return 0;

    if (isDuplicatePacket(packet)) {
        s_stats.dupCount++;
        return 0;
    }

    if (packet->len < 2) {
        s_stats.dropCount++;
        return 0;
    }

    for (idx = 0; idx < packet->rpt_count; idx++) {
        if (!strncmp(packet->rpt_list[idx].call, "RFONLY", 6) || !strncmp(packet->rpt_list[idx].call, "TCPIP", 5) ||
            !strncmp(packet->rpt_list[idx].call, "qA", 2) || !strncmp(packet->rpt_list[idx].call, "NOGATE", 6)) {
            s_stats.dropCount++;
            return 0;
        }
    }

    // Only gate satellite-repeated frames if the satellite's call is marked used ('*')
    static const struct {
        const char *call;
        size_t len;
    } satGates[] = {
        { "RS0ISS", 6 }, { "YBOX", 4 }, { "YBSAT", 5 }, { "PSAT", 4 }, { "W3ADO", 5 }, { "BJ1SI", 5 },
    };
    for (idx = 0; idx < packet->rpt_count; idx++) {
        for (size_t s = 0; s < sizeof(satGates) / sizeof(satGates[0]); s++) {
            if (!strncmp(packet->rpt_list[idx].call, satGates[s].call, satGates[s].len)) {
                if (strchr(&packet->rpt_list[idx].call[satGates[s].len - 1], '*') == NULL) {
                    s_stats.dropCount++;
                    return 0;
                }
            }
        }
    }

    char header[300];
    int headerLen;

    if (packet->src.ssid > 0)
        headerLen = snprintf(header, sizeof(header), "%s-%d>%s", packet->src.call, packet->src.ssid, packet->dst.call);
    else
        headerLen = snprintf(header, sizeof(header), "%s>%s", packet->src.call, packet->dst.call);

    if (packet->dst.ssid > 0)
        headerLen += snprintf(&header[headerLen], sizeof(header) - headerLen, "-%d", packet->dst.ssid);

    for (int i = 0; i < packet->rpt_count; i++) {
        headerLen += snprintf(&header[headerLen], sizeof(header) - headerLen, ",%s", packet->rpt_list[i].call);
        if (packet->rpt_list[i].ssid > 0)
            headerLen += snprintf(&header[headerLen], sizeof(header) - headerLen, "-%d", packet->rpt_list[i].ssid);
        if (packet->rpt_flags & (1 << i))
            headerLen += snprintf(&header[headerLen], sizeof(header) - headerLen, "*");
    }

    if (strlen(g_config.igate_object) >= 3) {
        if (g_config.aprs_ssid > 0)
            headerLen +=
                snprintf(&header[headerLen], sizeof(header) - headerLen, ",%s-%d*,qAO,%s", g_config.aprs_mycall, g_config.aprs_ssid, g_config.igate_object);
        else
            headerLen += snprintf(&header[headerLen], sizeof(header) - headerLen, ",%s*,qAO,%s", g_config.aprs_mycall, g_config.igate_object);
    } else {
        if (g_config.aprs_ssid > 0)
            headerLen += snprintf(&header[headerLen], sizeof(header) - headerLen, ",qAR,%s-%d", g_config.aprs_mycall, g_config.aprs_ssid);
        else
            headerLen += snprintf(&header[headerLen], sizeof(header) - headerLen, ",qAR,%s", g_config.aprs_mycall);
    }

    headerLen += snprintf(&header[headerLen], sizeof(header) - headerLen, ":");
    if (headerLen < 0 || (size_t)headerLen >= sizeof(header)) {
        s_stats.dropCount++;
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
    struct addrinfo hints = { 0 };
    struct addrinfo *res = NULL;
    char portStr[8];
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portStr, sizeof(portStr), "%u", (unsigned)g_config.aprs_port);

    if (getaddrinfo(g_config.aprs_host, portStr, &hints, &res) != 0 || res == NULL) {
        ESP_LOGW(TAG, "DNS lookup failed for %s", g_config.aprs_host);
        return false;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        return false;
    }

    struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGW(TAG, "Connect to %s:%u failed: errno %d", g_config.aprs_host, (unsigned)g_config.aprs_port, errno);
        close(sock);
        freeaddrinfo(res);
        return false;
    }
    freeaddrinfo(res);

    char login[160];
    int n = snprintf(login, sizeof(login), "user %s pass %s vers ESP32APRS 1.0 filter %s\r\n", g_config.aprs_mycall, g_config.aprs_passcode,
                     g_config.aprs_filter[0] ? g_config.aprs_filter : "");
    // Log exactly what we're sending (minus the trailing \r\n) so a bad
    // filter string (e.g. wrong filter letter, malformed args) is visible
    // in the logs instead of silently resulting in zero RX traffic.
    ESP_LOGI(TAG, "APRS-IS login: user %s pass %s vers ESP32APRS 1.0 filter %s", g_config.aprs_mycall, g_config.aprs_passcode,
             g_config.aprs_filter[0] ? g_config.aprs_filter : "(none - server default, usually nothing)");
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
    ESP_LOGI(TAG, "Connected to APRS-IS %s:%u as %s", g_config.aprs_host, (unsigned)g_config.aprs_port, g_config.aprs_mycall);
    trafficlog_add("Connected to APRS-IS %s:%u as %s", g_config.aprs_host, (unsigned)g_config.aprs_port, g_config.aprs_mycall);
    return true;
}

// The APRS-IS TCP uplink is a single shared resource used not only by the
// IGate itself (rf2inet/inet2rf) but also by the Digipeater's "beacon to
// internet" option and by outbound messages sent over the internet channel.
// Previously the uplink task only ran when igate_en was on, and was only
// ever started once at boot based on whatever igate_en happened to be at
// that moment - so enabling digi_loc2inet (or msg_inet) with IGate itself
// left disabled meant this task never existed, the socket was never
// connected, and every send silently failed forever. Likewise, toggling
// igate_en via the web UI and saving had no effect until reboot, since
// nothing re-evaluated it at runtime. Gating on this helper instead makes
// the uplink come up/go down live, from a task that's always running.
static bool igateUplinkNeeded(void) {
    return g_config.igate_en || g_config.digi_loc2inet || g_config.msg_inet;
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

        if (s_sock < 0) {
            if (!connectAprsIs()) {
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            linePos = 0;
        }

        char buf[256];
        int r = recv(s_sock, buf, sizeof(buf), 0);
        if (r > 0) {
            for (int i = 0; i < r; i++) {
                char c = buf[i];
                if (c == '\n' || c == '\r') {
                    if (linePos > 0) {
                        line[linePos] = 0;
                        if (line[0] != '#') { // '#' = server comment/keepalive
                            // Information-level log of every message received
                            // from APRS-IS, regardless of whether inet2rf is
                            // enabled below, so all igate traffic is visible.
                            ESP_LOGI(TAG, "APRS-IS RX: %s", line);
                            {
                                char dx[16];
                                tnc2SrcCallsign(line, strlen(line), dx, sizeof(dx));

                                // Position/object/item reports start their info
                                // field (right after the first ':') with one of
                                // !=/@; the next two bytes are the symbol table
                                // and symbol code. Only the no-timestamp formats
                                // ('!'/'=') are handled here, same as elsewhere.
                                char symTable = 0, symCode = 0;
                                const char *colon = strchr(line, ':');
                                if (colon) {
                                    const char *info = colon + 1;
                                    size_t infoLen = strlen(info);
                                    if ((info[0] == '!' || info[0] == '=') && infoLen >= 20) {
                                        symTable = info[9];
                                        symCode = info[19];
                                    }
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
