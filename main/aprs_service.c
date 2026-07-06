#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "AX25.h"
#include "LibAPRSesp.h"

#include "app_config.h"
#include "aprs_service.h"
#include "beacon.h"
#include "digirepeater.h"
#include "igate.h"
#include "lastheard.h"
#include "message.h"
#include "trafficlog.h"

static const char *TAG = "aprs_service";

// ---------------------------------------------------------------------------
// AX25Msg -> TNC2 text line ("SRC-N>DST-N,PATH...:info"), shared by the digi
// re-transmit path, the igate RF->INET path (igate.c builds its own header
// internally) and message parsing (which works on TNC2 text either way).
// ---------------------------------------------------------------------------
static int ax25ToTnc2(AX25Msg *m, char *out, size_t outMax) {
    int n;
    if (m->src.ssid > 0)
        n = snprintf(out, outMax, "%s-%d>%s", m->src.call, m->src.ssid, m->dst.call);
    else
        n = snprintf(out, outMax, "%s>%s", m->src.call, m->dst.call);
    if (m->dst.ssid > 0)
        n += snprintf(&out[n], outMax - n, "-%d", m->dst.ssid);
    for (int i = 0; i < m->rpt_count; i++) {
        n += snprintf(&out[n], outMax - n, ",%s", m->rpt_list[i].call);
        if (m->rpt_list[i].ssid > 0)
            n += snprintf(&out[n], outMax - n, "-%d", m->rpt_list[i].ssid);
        if (m->rpt_flags & (1 << i))
            n += snprintf(&out[n], outMax - n, "*");
    }
    n += snprintf(&out[n], outMax - n, ":");
    size_t infoLen = m->len;
    if (infoLen > outMax - n - 1)
        infoLen = outMax - n - 1;
    memcpy(&out[n], m->info, infoLen);
    n += infoLen;
    out[n] = 0;
    return n;
}

/**
 * @brief Hook invoked by esp32_IDF_libAPRS (AX25.c) for every decoded RX
 * frame. This is the single dispatch point for digipeater / igate / message.
 */
void aprs_msg_callback(struct AX25Msg *msg) {
    char tnc2[400];
    ax25ToTnc2(msg, tnc2, sizeof(tnc2));

    // Source callsign (with SSID), used both for the LAST HEARD table and
    // as the DX field of the traffic-table entry below.
    char callsign[12];
    if (msg->src.ssid > 0)
        snprintf(callsign, sizeof(callsign), "%s-%d", msg->src.call, msg->src.ssid);
    else
        snprintf(callsign, sizeof(callsign), "%s", msg->src.call);

    // Position/object/item reports start their info field with one of
    // !=/@; the next two bytes are the symbol table and symbol code.
    // Only handle the no-timestamp position formats ('!' / '=') here;
    // '/' and '@' carry a 7-byte timestamp before the position and are
    // left unparsed (icon stays blank for those).
    char symTable = 0, symCode = 0;
    if ((msg->info[0] == '!' || msg->info[0] == '=') && msg->len >= 20) {
        symTable = msg->info[9];
        symCode = msg->info[19];
    }

    // Log every decoded RF frame so the web traffic viewer mirrors what the
    // serial console shows for RF activity, regardless of which
    // feature(s) below end up acting on it. AUDIO is the demodulated
    // signal level (mV RMS) reported by the AFSK/GFSK modem for this frame.
    trafficlog_add_pkt("RX", callsign, tnc2, (int)msg->mVrms, symTable, symCode);

    // Feed the web dashboard's "LAST HEARD" table (see components/lastheard).
    {
        char path[48] = "";
        size_t plen = 0;
        for (int i = 0; i < msg->rpt_count && plen + 1 < sizeof(path); i++) {
            plen += snprintf(&path[plen], sizeof(path) - plen, "%s%s", (i == 0) ? "" : ",", msg->rpt_list[i].call);
            if (msg->rpt_list[i].ssid > 0 && plen + 1 < sizeof(path))
                plen += snprintf(&path[plen], sizeof(path) - plen, "-%d", msg->rpt_list[i].ssid);
        }

        lastheard_add(callsign, path, true, symTable, symCode);
    }

    if (g_config.digi_en) {
        int action = digiProcess(msg);
        if (action == 2) {
            // Path was rewritten in place; re-transmit the modified frame on RF.
            int len = ax25ToTnc2(msg, tnc2, sizeof(tnc2));
            APRS_sendTNC2Pkt((const uint8_t *)tnc2, (size_t)len);
            ESP_LOGD(TAG, "DIGI TX: %s", tnc2);
            trafficlog_add_pkt("DIGI", callsign, tnc2, -1, symTable, symCode);
        }
    }

    if (g_config.igate_en && g_config.rf2inet) {
        igateProcess(msg); // builds its own qAR/qAO header and sends to APRS-IS internally
    }

    if (g_config.msg_enable) {
        ax25ToTnc2(msg, tnc2, sizeof(tnc2));
        handleIncomingAPRS(tnc2);
    }
}

// ---------------------------------------------------------------------------
// INET -> RF: called by igate.c for every non-comment line read from APRS-IS.
// ---------------------------------------------------------------------------
static void inet2rfHandler(const char *line) {
    // Feed the dashboard LAST HEARD table from APRS-IS too, not just RF, so
    // there's something to see while verifying the IGate uplink even before
    // the local radio has decoded anything. TNC2 line looks like
    // "SRC-N>DST,PATH,qAR,SERVER:info" - split at '>' for the callsign and
    // at ':' for the via/path, matching what aprs_msg_callback does for RF.
    char callsign[12] = "";
    char symTable = 0, symCode = 0;
    {
        const char *gt = strchr(line, '>');
        const char *colon = strchr(line, ':');
        if (gt && colon && colon > gt) {
            size_t callLen = (size_t)(gt - line);
            if (callLen >= sizeof(callsign))
                callLen = sizeof(callsign) - 1;
            memcpy(callsign, line, callLen);
            callsign[callLen] = 0;

            char path[48];
            size_t pathLen = (size_t)(colon - gt - 1);
            if (pathLen >= sizeof(path))
                pathLen = sizeof(path) - 1;
            memcpy(path, gt + 1, pathLen);
            path[pathLen] = 0;

            const char *info = colon + 1;
            size_t infoLen = strlen(info);
            if ((info[0] == '!' || info[0] == '=') && infoLen >= 20) {
                symTable = info[9];
                symCode = info[19];
            }

            lastheard_add(callsign, path, false, symTable, symCode);
        }
    }

    if (g_config.msg_enable)
        handleIncomingAPRS(line);

    if (g_config.inet2rf) {
        // NOTE: g_config.inet2rfFilter (object/item/message/etc bitmask) is not
        // yet applied here; add a filter check before this call if selective
        // gating is required.
        APRS_sendTNC2Pkt((const uint8_t *)line, strlen(line));
        ESP_LOGD(TAG, "INET2RF TX: %s", line);
        trafficlog_add_pkt("INET2RF", callsign, line, -1, symTable, symCode);
    }
}

// ---------------------------------------------------------------------------
// Outbound message TX: message.c hands us a built TNC2 packet + channel mask.
// ---------------------------------------------------------------------------
static void messageTxHandler(const char *packet, size_t len, uint8_t channels) {
    if (channels & MSG_CHANNEL_RF)
        APRS_sendTNC2Pkt((const uint8_t *)packet, len);
    if (channels & MSG_CHANNEL_INET)
        igate_send_raw(packet, len);
}

static void serviceTickTask(void *arg) {
    while (1) {
        if (g_config.msg_enable)
            sendAPRSMessageRetry();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void aprs_service_start(void) {
    trafficlog_init();
    lastheard_init();
    message_init();
    message_set_tx_handler(messageTxHandler);
    igate_set_inet2rf_handler(inet2rfHandler);

    if (g_config.igate_en)
        igate_start();

    beacon_start();

    xTaskCreate(serviceTickTask, "aprs_svc_tick", 3072, NULL, 4, NULL);

    ESP_LOGI(TAG, "APRS service started (digi=%d igate=%d msg=%d)", g_config.digi_en, g_config.igate_en, g_config.msg_enable);
}
