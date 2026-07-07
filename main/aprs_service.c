#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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

// AFSK_Poll() pulls samples out of the ADC ring buffer and feeds them to the
// demodulator/HDLC bit parser, and also drives the DAC-ISR TX bit clock
// indirectly. It must be called frequently (every few ms) or RX demodulation
// and TX framing never execute even though the modem hardware is running.
static void afskPollTask(void *arg) {
    while (1) {
        AFSK_Poll(false, g_config.rf_power);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void serviceTickTask(void *arg) {
    while (1) {
        if (g_config.msg_enable)
            sendAPRSMessageRetry();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ---------------------------------------------------------------------------
// Audio ADC/DAC AFSK modem "LOOP TEST" (Radio/Modem webconfig page).
// ---------------------------------------------------------------------------

// _hook is the AX.25 RX callback esp32_IDF_libAPRS invokes for every decoded
// frame (normally aprs_msg_callback, wired up in LibAPRS.c). We borrow it for
// the duration of the test so a self-generated test frame is never mistaken
// for real RF traffic (digipeated, sent to APRS-IS, etc.), then put it back.
extern ax25_callback_t _hook;

#define LOOP_TEST_TIMEOUT_MS 4000

static SemaphoreHandle_t s_loopTestSem = NULL;
static volatile bool s_loopTestActive = false;
static volatile bool s_loopTestGotFrame = false;
static char s_loopTestToken[8];
static char s_loopTestRxInfo[128];
static uint16_t s_loopTestRxMVrms = 0;

// Set true once main.c has actually called APRS_init() (i.e. the audio
// modem is enabled in config *and* the hardware has been brought up since
// boot - toggling the checkbox without rebooting does not re-run APRS_init).
static volatile bool s_modemReady = false;

void aprs_service_notify_modem_ready(void) {
    s_modemReady = true;
}

// AX.25 RX hook installed only while a loop test is in flight.
static void loopTestRxHook(struct AX25Msg *msg) {
    size_t n = msg->len < sizeof(s_loopTestRxInfo) - 1 ? msg->len : sizeof(s_loopTestRxInfo) - 1;
    memcpy(s_loopTestRxInfo, msg->info, n);
    s_loopTestRxInfo[n] = 0;
    s_loopTestRxMVrms = msg->mVrms;
    s_loopTestGotFrame = true;
    xSemaphoreGive(s_loopTestSem);
}

bool aprs_loop_test_run(char *msg, size_t msg_len) {
    if (!s_modemReady || !g_config.audio_modem_en) {
        snprintf(msg, msg_len,
                 "Audio ADC/DAC modem is not enabled/initialized. Enable \"Enable audio ADC/DAC modem\" above, save, and reboot the device first.");
        ESP_LOGW(TAG, "Loop test: %s", msg);
        return false;
    }

    if (s_loopTestActive) {
        snprintf(msg, msg_len, "A loop test is already running - please wait for it to finish.");
        ESP_LOGW(TAG, "Loop test: %s", msg);
        return false;
    }
    s_loopTestActive = true;

    if (!s_loopTestSem)
        s_loopTestSem = xSemaphoreCreateBinary();
    else
        xSemaphoreTake(s_loopTestSem, 0); // drain any stale/leftover give

    // Unique token per run, so we only accept *this* frame as a pass, not
    // some coincidental leftover/duplicate packet.
    uint32_t token = esp_random() & 0xFFFFFF;
    snprintf(s_loopTestToken, sizeof(s_loopTestToken), "%06lX", (unsigned long)token);

    char tnc2[48];
    int n = snprintf(tnc2, sizeof(tnc2), "SELFTST>APLT1T:>LOOPTEST %s", s_loopTestToken);

    s_loopTestGotFrame = false;
    ax25_callback_t savedHook = _hook;
    _hook = loopTestRxHook;

    AFSK_resetAdcDiag();

    ESP_LOGI(TAG, "Loop test: TX %s", tnc2);
    APRS_sendTNC2Pkt((const uint8_t *)tnc2, (size_t)n);

    bool signaled = (xSemaphoreTake(s_loopTestSem, pdMS_TO_TICKS(LOOP_TEST_TIMEOUT_MS)) == pdTRUE);

    // Always restore the real RX hook before doing anything else, so a
    // failed/timed-out test doesn't leave real RX frames stuck being
    // swallowed by the test hook.
    _hook = savedHook;
    s_loopTestActive = false;

    if (!signaled || !s_loopTestGotFrame) {
        int16_t adcMin, adcMax;
        AFSK_getAdcDiag(&adcMin, &adcMax);
        int adcSwing = (int)adcMax - (int)adcMin;
        if (adcMin > adcMax) {
            // ISR never ran at all - min/max never updated from their initial
            // sentinel values.
            snprintf(msg, msg_len,
                     "FAIL: no packet was received back within %d ms, and the ADC never captured a single sample. "
                     "The ADC continuous driver/timer isn't running - this points at an init failure, not a wiring "
                     "or level problem.",
                     LOOP_TEST_TIMEOUT_MS);
        } else if (adcSwing < 50) {
            // ISR ran, but the raw ADC code barely moved - the ADC is not
            // seeing the DAC's tone at all (flat/near-DC line). Report the
            // calibrated voltage, not just the raw code: a reading pinned
            // near VDD (~3.0-3.3V) points at a miswire/short rather than a
            // units/scale bug in the demodulator path.
            int mv = AFSK_adcRawToMv(adcMax);
            snprintf(msg, msg_len,
                     "FAIL: no packet was received back within %d ms. The ADC is sampling (raw code stayed within "
                     "%d-%d, a %d-count swing, ~%d mV), but is not seeing any audio tone - check that GPIO33 (ADC in) "
                     "and GPIO25 (DAC out) are actually wired together and both grounds are common.",
                     LOOP_TEST_TIMEOUT_MS, adcMin, adcMax, adcSwing, mv);
        } else {
            // ISR ran and saw a real signal swing, but the modem still
            // couldn't decode its own transmission - level/samplerate/filter
            // problem rather than a wiring problem.
            snprintf(msg, msg_len,
                     "FAIL: no packet was received back within %d ms, even though the ADC did see a signal (raw "
                     "code swung %d-%d, a %d-count range). The wiring is good; the modem itself isn't "
                     "demodulating it - check the squelch/volume settings, ADC attenuation, and modem type/baud "
                     "rate configuration.",
                     LOOP_TEST_TIMEOUT_MS, adcMin, adcMax, adcSwing);
        }
        ESP_LOGW(TAG, "Loop test: %s", msg);
        return false;
    }

    char expected[24];
    snprintf(expected, sizeof(expected), ">LOOPTEST %s", s_loopTestToken);

    if (strstr(s_loopTestRxInfo, expected) != NULL) {
        snprintf(msg, msg_len, "PASS: sent \"%s\" and correctly decoded it back (RX level %u mV RMS). The AFSK modem works correctly.",
                 tnc2 + (strchr(tnc2, ':') - tnc2) + 1, (unsigned)s_loopTestRxMVrms);
        ESP_LOGI(TAG, "Loop test: %s", msg);
        return true;
    }

    snprintf(msg, msg_len,
             "FAIL: a packet was received back, but its content did not match what was sent (got \"%s\"). Check for audio "
             "distortion, clipping, or an incorrect ADC/DAC loopback wiring.",
             s_loopTestRxInfo);
    ESP_LOGW(TAG, "Loop test: %s", msg);
    return false;
}

void aprs_service_start(void) {
    trafficlog_init();
    lastheard_init();
    message_init();
    message_set_tx_handler(messageTxHandler);
    igate_set_inet2rf_handler(inet2rfHandler);

    // Always start the uplink task: it now idles itself (socket closed,
    // fast retry loop) whenever nothing needs APRS-IS, and comes up as soon
    // as igate_en, digi_loc2inet, or msg_inet is turned on - including via
    // a runtime web UI save, with no reboot required.
    igate_start();

    beacon_start();

    // Higher priority than the 1s housekeeping tick below: this one is
    // timing-sensitive (AFSK bit/sample timing depends on it running often).
    xTaskCreate(afskPollTask, "afsk_poll", 3072, NULL, 6, NULL);
    xTaskCreate(serviceTickTask, "aprs_svc_tick", 3072, NULL, 4, NULL);

    ESP_LOGI(TAG, "APRS service started (digi=%d igate=%d msg=%d)", g_config.digi_en, g_config.igate_en, g_config.msg_enable);
}
