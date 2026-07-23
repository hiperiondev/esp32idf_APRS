/**
 * @file aprs_service.c
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
 * @brief APRS application layer: maps g_config onto the modem configuration,
 * installs the modem RX callback that feeds the digipeater, IGate, message,
 * lastheard and trafficlog components, provides the TNC2 transmit path and runs
 * the periodic service tick.
 */

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "afsk.h"
#include "ax25.h"
#include "esp32idf_radioamateur_modem.h"
#include "esp32idf_radioamateur_modem_config.h"
#include "modem.h"

#include "app_config.h"
#include "aprs_filter.h"
#include "aprs_service.h"
#include "beacon.h"
#include "beacon_scheduler.h"
#include "bulletins.h"
#include "objects_items.h"
#include "digirepeater.h"
#include "igate.h"
#include "lastheard.h"
#include "message.h"
#include "trafficlog.h"
#include "weather.h"

static const char *TAG = "aprs_service";

// How many frames aprs_service_send_tnc2() lets pile up in the RF TX ring
// before it starts discarding new packets instead of queuing them (see the
// comment inside that function). The ring itself has room for far more
// (FRAME_MAX_COUNT-1 slots in ax25.c), but a long backlog just means later
// packets go out stale, so this caps it well below that.
#define RF_TX_QUEUE_LIMIT 2

// ---------------------------------------------------------------------------
// Modem configuration
//
// Single mapping point from g_config to the component's modem_config_t, used
// by main.c at boot, by page_radio.c's Save (live re-apply, no reboot) and by
// the LOOP TEST below (which only overrides full_duplex).
//
// What the new component does NOT take at runtime, and where it went instead:
//   audio pins        -> compile-time MODEM_ADC_GPIO / MODEM_DAC_GPIO, set in
//                        the top-level CMakeLists.txt. Changing the audio
//                        front-end pins still requires a rebuild.
//   ADC attenuation   -> compile-time MODEM_ADC_ATTEN (ADC_ATTEN_DB_12).
//   hardware squelch  -> gone. The component gates RX on the demodulator's own
//                        DCD rather than on a squelch line, and it has no RF
//                        power-switch output either.
//   software squelch,
//   RX volume, AGC
//   gain ceiling      -> gone. The component's AGC is self-limiting and there
//                        is no RX gain trim.
// The g_config fields that used to hold all of the above (adc_gpio, dac_gpio,
// adc_sel_gpio, dac_sel_gpio, adc_atten, rf_sql_*, rf_pwr_*, rf_pd_*,
// rf_tx_gpio, rf_rx_gpio, sql_level, volume, agc_max_gain) have been REMOVED:
// they were written to /storage/config.json on every save and read by nothing.
//
// rf_ptt_gpio / rf_ptt_active are the exception and are still mapped at runtime
// (below) to modem_config_t.ptt_gpio / .ptt_active_high, validated by
// afsk_ptt_gpio_is_valid(). MODEM_PTT_GPIO / MODEM_PTT_ACTIVE_HIGH now only
// seed the factory default of those two fields (app_config_set_defaults()), so
// the CMakeLists.txt board definition stays the single source of truth for the
// PTT wiring instead of competing with a second default in app_config.c.
// ---------------------------------------------------------------------------
void aprs_service_build_modem_config(modem_config_t *cfg, bool full_duplex) {
    modem_config_t base = MODEM_DEFAULT_CONFIG();
    *cfg = base;

    // afskModem on the Radio page: 0=300 Bd, 1=1200 Bd Bell202, 2=1200 Bd V.23,
    // 3=9600 Bd G3RUH. modem_mode_t uses exactly the same numbering
    // (MODEM_MODEM_AFSK300=0 .. MODEM_MODEM_G3RUH=3), which is why this is a
    // plain cast; page_radio.c already clamps the field to 0-3.
    cfg->modem = (modem_mode_t)g_config.afsk_modem_type;

    // audioLPF fed the old afskSetModem()'s bpf argument, which that function
    // assigned straight to ModemConfig.flatAudioIn - i.e. despite the name it
    // always was the flat-audio-input flag, and modem_config_t.flat_audio is
    // the same bit. Kept as a direct mapping so the saved setting keeps its
    // existing meaning.
    cfg->flat_audio = g_config.audio_lpf;

    cfg->preamble_ms = g_config.preamble;
    cfg->slot_time_ms = g_config.tx_timeslot;
    cfg->fx25_mode = g_config.fx25_mode;
    cfg->allow_non_aprs = false;

    // PTT pin is now runtime-selectable (Radio/Modem page) instead of the
    // old compile-time-only MODEM_PTT_GPIO. afsk_ptt_gpio_is_valid() rejects
    // input-only pins (GPIO34-39), the internal flash/PSRAM pins (GPIO6-11)
    // and whichever pins are already wired to the ADC/DAC audio path
    // (MODEM_ADC_GPIO / MODEM_DAC_GPIO), so a stale or hand-edited
    // config.json can never key up an audio pin or a non-output pin.
    // -1 always means "PTT disabled" and passes validation unchanged.
    cfg->ptt_gpio = afsk_ptt_gpio_is_valid(g_config.rf_ptt_gpio) ? g_config.rf_ptt_gpio : -1;
    cfg->ptt_active_high = g_config.rf_ptt_active;

    // Half duplex for real on-air use: MODEM_DEFAULT_CONFIG() ships full
    // duplex (it targets the wire-loopback demo), which would key up over
    // anyone already transmitting. CSMA/quiet time comes from txTimeSlot.
    // The LOOP TEST passes true here because a DAC->ADC wire means the node
    // always hears its own carrier and would never see a clear channel.
    cfg->full_duplex = full_duplex;
}

// ---------------------------------------------------------------------------
// Dashboard statistics.
//
// These are the counters the web dashboard's STATISTICS panel actually wants
// (page_common.c's page_sidebar_info()): total RF frames decoded, total RF
// frames transmitted, frames relayed RF->INET and INET->RF, digipeated
// frames, and drop/error counts - all independent of whether the digipeater
// or IGate features are even enabled.
//
// This used to be improvised from digi_get_stats()/igate_get_stats(), whose
// counters only increment from inside digiProcess()/igateProcess() - which
// are only ever called when g_config.digi_en / g_config.igate_en are true
// (see aprs_msg_callback() below). With both features off (a very common
// RX-only/monitor setup), every counter on the dashboard stayed at 0 forever,
// regardless of how much real RF traffic the modem was decoding. Tracking
// these directly at the points where frames actually flow (on_rx_frame(),
// aprs_service_send_tnc2(), inet2rfHandler(), and inside aprs_msg_callback()
// for the digi/igate/error cases) makes the dashboard reflect reality
// whether or not either feature is turned on.
static atomic_uint_fast32_t s_statRadioRx = 0;  // frames decoded off RF (every on_rx_frame() call)
static atomic_uint_fast32_t s_statRadioTx = 0;  // frames transmitted on RF (every successful aprs_service_send_tnc2())
static atomic_uint_fast32_t s_statRf2Inet = 0;  // frames relayed from RF to APRS-IS (igateProcess() actually uplinked one)
static atomic_uint_fast32_t s_statInet2Rf = 0;  // lines relayed from APRS-IS to RF (inet2rfHandler() actually transmitted one)
static atomic_uint_fast32_t s_statDigi = 0;     // frames digipeated (path rewritten and re-transmitted)
// The two below close the gap left by the previous dashboard-statistics fix
// (see page_common.c's page_sidebar_info()): digi_get_stats()/igate_get_stats()
// are still the only place any *feature-specific* drop/error accounting
// exists, but their counters only move while digi_en/igate_en are on. For a
// monitor/RX-only setup (both features off - very common while
// characterizing modem decode performance), that left DROP/ERR pinned at 0
// forever even with plenty of real RF activity. These two are tracked at
// every point a frame is actually discarded - on the RX side in
// on_rx_frame()/aprs_msg_callback(), and on the TX side in
// aprs_service_send_tnc2() (RF TX queue full, oversized packet, modem not
// ready yet, or modem_send_tnc2() itself failing) - so they move regardless
// of which higher-level features are enabled and regardless of which
// direction the discard happens in.
static atomic_uint_fast32_t s_statDrop = 0; // frames discarded before dispatch or on the way out to RF (placeholder/invalid source callsign, modem-not-ready, TX queue full, oversized packet, etc.)
static atomic_uint_fast32_t s_statErr = 0;  // frames that failed to decode as valid APRS (UI, no-layer-3) AX.25, or that the modem itself failed to transmit (modem_send_tnc2() error)

aprs_service_stats_t aprs_service_get_stats(void) {
    aprs_service_stats_t s;
    s.radio_rx = (uint32_t)atomic_load_explicit(&s_statRadioRx, memory_order_relaxed);
    s.radio_tx = (uint32_t)atomic_load_explicit(&s_statRadioTx, memory_order_relaxed);
    s.rf2inet = (uint32_t)atomic_load_explicit(&s_statRf2Inet, memory_order_relaxed);
    s.inet2rf = (uint32_t)atomic_load_explicit(&s_statInet2Rf, memory_order_relaxed);
    s.digi = (uint32_t)atomic_load_explicit(&s_statDigi, memory_order_relaxed);
    s.drop = (uint32_t)atomic_load_explicit(&s_statDrop, memory_order_relaxed);
    s.err = (uint32_t)atomic_load_explicit(&s_statErr, memory_order_relaxed);
    return s;
}

void aprs_service_apply_modem_config(void) {
    if (!aprs_service_modem_ready())
        return;
    modem_config_t cfg;
    aprs_service_build_modem_config(&cfg, false);
    modem_set_modem(&cfg);
    ESP_LOGI(TAG, "modem re-applied: modem=%u flatAudio=%d preamble=%ums slot=%ums fx25=%u", (unsigned)cfg.modem, (int)cfg.flat_audio,
             (unsigned)cfg.preamble_ms, (unsigned)cfg.slot_time_ms, (unsigned)cfg.fx25_mode);
}

// Set true once main.c has actually called modem_init() successfully (i.e.
// the audio modem is enabled in config *and* the hardware came up since boot -
// toggling the checkbox without rebooting does not re-run modem_init).
static volatile bool s_modemReady = false;

// ---------------------------------------------------------------------------
// TX helper
//
// The old APRS_sendTNC2Pkt(raw, len) took a pointer+length; the component's
// modem_send_tnc2() takes a NUL-terminated string (it copies into a scratch
// buffer for ax25_encode(), which uses strtok on the digipeater path). Callers
// here hand us slices of larger buffers that are not always NUL-terminated at
// exactly `len`, so terminate explicitly rather than trusting the caller.
//
// Exported (beacon.c calls it too) so the pointer+length -> NUL-terminated
// conversion and the length check live in exactly one place.
// ---------------------------------------------------------------------------
bool aprs_service_send_tnc2(const char *packet, size_t len) {
    char buf[AX25_FRAME_MAX_SIZE];

    if (len == 0)
        return false;

    // Drop rather than queue when the modem was never brought up. Two ways to
    // get here: the audio modem is disabled on the Radio page, or - and this
    // is the one that bites - aprs_service_start() now runs BEFORE
    // modem_init() (it has to: it installs the RX callback), and it starts the
    // beacon tasks, which transmit immediately on entry rather than after
    // their first interval. modem_init() blocks for ~5 s measuring the ADC
    // clock, so without this a boot-time beacon would reach
    // Ax25WriteTxFrame() before Ax25Init() had run.
    if (!s_modemReady) {
        atomic_fetch_add_explicit(&s_statDrop, 1, memory_order_relaxed);
        ESP_LOGD(TAG, "modem not up, RF TX dropped: %.*s", (int)len, packet);
        return false;
    }
    // Allow a small backlog rather than discarding the moment one frame is
    // in flight: up to RF_TX_QUEUE_LIMIT frames may sit in the ring
    // (waiting to key up or on the air right now) before a new packet is
    // dropped instead of queued. Ax25WriteTxFrame() would otherwise happily
    // queue far more (FRAME_MAX_COUNT-1 slots) and, under sustained load
    // (e.g. INET2RF gating faster than the RF channel can clear), eventually
    // drop it anyway once the ring fills - capping the backlog here instead
    // keeps queued packets from going stale and makes the reason visible on
    // the serial console. This only ever touches the RF TX ring - it has no
    // effect on the separate APRS-IS socket buffer used by igate_send_raw(),
    // so a busy RF leg never blocks or drops the IGate leg of the same
    // packet.
    uint8_t pending = modem_tx_queue_depth();
    if (pending >= RF_TX_QUEUE_LIMIT) {
        atomic_fetch_add_explicit(&s_statDrop, 1, memory_order_relaxed);
        ESP_LOGW(TAG, "RF TX queue full (%u/%u pending), packet discarded: %.*s", (unsigned)pending, (unsigned)RF_TX_QUEUE_LIMIT, (int)len,
                 packet);
        return false;
    }
    if (len >= sizeof(buf)) {
        atomic_fetch_add_explicit(&s_statDrop, 1, memory_order_relaxed);
        ESP_LOGW(TAG, "TNC2 packet too long (%u bytes), dropped", (unsigned)len);
        return false;
    }
    memcpy(buf, packet, len);
    buf[len] = 0;

    esp_err_t err = modem_send_tnc2(buf);
    if (err != ESP_OK) {
        atomic_fetch_add_explicit(&s_statErr, 1, memory_order_relaxed);
        ESP_LOGW(TAG, "modem_send_tnc2() failed: %s (\"%s\")", esp_err_to_name(err), buf);
        return false;
    }
    atomic_fetch_add_explicit(&s_statRadioTx, 1, memory_order_relaxed);
    return true;
}

// ---------------------------------------------------------------------------
// AX25Msg -> TNC2 text line ("SRC-N>DST-N,PATH...:info"), shared by the digi
// re-transmit path, the igate RF->INET path (igate.c builds its own header
// internally) and message parsing (which works on TNC2 text either way).
//
// This used to be a local ax25ToTnc2(); the component now ships exactly the
// same rendering as modem_format_tnc2(), so this is a thin length-returning
// wrapper over it rather than a second copy that could drift.
// ---------------------------------------------------------------------------
static int ax25ToTnc2(const ax25_msg_t *m, char *out, size_t outMax) {
    modem_format_tnc2(m, out, outMax);
    return (int)strlen(out);
}

/**
 * @brief Single dispatch point for digipeater / igate / message, fed by
 * on_rx_frame() below for every decoded RX frame.
 */
static void aprs_msg_callback(ax25_msg_t *msg) {
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
    ESP_LOGI(TAG, "RX: %s", tnc2);
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

    // Placeholder/invalid source callsign check (NOCALL = radio not
    // configured, MYCALL = misconfigured/uninitialized digipeater config
    // sentinel used elsewhere in this codebase). This mirrors the same
    // check digiProcess() does internally, but runs unconditionally here so
    // it (and the dashboard's DROP counter) means something even with
    // digi_en off. A frame from either sentinel is never useful to
    // digipeat, gate, or otherwise act on, so skip the rest of the
    // dispatch chain for it.
    if (!strncmp(msg->src.call, "NOCALL", 6) || !strncmp(msg->src.call, "MYCALL", 6)) {
        atomic_fetch_add_explicit(&s_statDrop, 1, memory_order_relaxed);
        ESP_LOGD(TAG, "RX dropped, placeholder source callsign: %s", tnc2);
        return;
    }

    if (g_config.digi_en) {
        int action = digiProcess(msg);
        if (action == 2) {
            // Path was rewritten in place; re-transmit the modified frame on RF.
            int len = ax25ToTnc2(msg, tnc2, sizeof(tnc2));
            if (aprs_service_send_tnc2(tnc2, (size_t)len)) {
                atomic_fetch_add_explicit(&s_statDigi, 1, memory_order_relaxed);
                ESP_LOGD(TAG, "DIGI TX: %s", tnc2);
                trafficlog_add_pkt("DIGI", callsign, tnc2, -1, symTable, symCode);
            }
        }
    }

    if (g_config.igate_en && g_config.rf2inet) {
        if (igateProcess(msg)) // builds its own qAR/qAO header and sends to APRS-IS internally
            atomic_fetch_add_explicit(&s_statRf2Inet, 1, memory_order_relaxed);
    }

    if (g_config.msg_enable) {
        ax25ToTnc2(msg, tnc2, sizeof(tnc2));
        handleIncomingAPRS(tnc2);
    }
}

// ---------------------------------------------------------------------------
// Component RX callback.
//
// The old component decoded frames itself and invoked a global ax25_callback_t
// (_hook) with a ready-made AX25Msg. The new one hands back the raw AX.25
// bytes and leaves the decode to us, so this does the ax25_decode() the old
// LibAPRS.c used to do internally, then dispatches exactly as before.
//
// Runs on the component's "modem_svc" task, whose stack the component sizes at
// 6144 bytes - enough for the ax25_msg_t below (~700 B) plus the rest of the
// dispatch chain, but worth remembering before adding anything large here.
//
// s_rxHook replaces the old `extern ax25_callback_t _hook` the LOOP TEST used
// to reach into the component and swap out. The indirection is ours now: the
// component's callback stays installed for the life of the process and only
// this pointer moves.
// ---------------------------------------------------------------------------
typedef void (*aprs_rx_hook_t)(ax25_msg_t *msg);
static volatile aprs_rx_hook_t s_rxHook = aprs_msg_callback;

static void on_rx_frame(const modem_rx_frame_t *f, void *ctx) {
    (void)ctx;
    ax25_msg_t msg;

    atomic_fetch_add_explicit(&s_statRadioRx, 1, memory_order_relaxed);

    memset(&msg, 0, sizeof(msg));
    if (!ax25_decode((uint8_t *)f->frame, f->len, f->mVrms, &msg)) {
        // Not a decodable APRS (UI, no-layer-3) frame - corrupted frame off
        // RF, or legitimate non-APRS AX.25 traffic. Either way msg.info/len
        // were never populated, so there is nothing safe to dispatch;
        // just count it and stop here. This is the only decode-failure
        // signal available (ax25_decode() itself has no error side
        // channel besides its return value), and - unlike the
        // digi/igate-only drop/error counters below it in the dashboard -
        // it is tracked here unconditionally, regardless of digi_en/igate_en.
        atomic_fetch_add_explicit(&s_statErr, 1, memory_order_relaxed);
        ESP_LOGD(TAG, "RX decode error (ctrl/pid not APRS UI), %u bytes, %u mVrms", (unsigned)f->len, (unsigned)f->mVrms);
        return;
    }

    aprs_rx_hook_t hook = s_rxHook;
    if (hook)
        hook(&msg);
}

// Case-insensitive compare of a TNC2 source base callsign (SSID already
// stripped, given as ptr+len) against one configured own-station call. An
// empty configured call never matches, so report callsigns the operator left
// blank can't accidentally swallow foreign traffic.
static bool base_call_equals(const char *src, size_t srcLen, const char *cfg) {
    if (!cfg || cfg[0] == 0)
        return false;
    if (strlen(cfg) != srcLen)
        return false;
    for (size_t i = 0; i < srcLen; i++) {
        char a = src[i], b = cfg[i];
        if (a >= 'a' && a <= 'z')
            a -= 32;
        if (b >= 'a' && b <= 'z')
            b -= 32;
        if (a != b)
            return false;
    }
    return true;
}

// True if this APRS-IS line's SOURCE callsign (base call, SSID ignored) is one
// of THIS station's own report callsigns. Every report we upload to APRS-IS via
// its *_2inet flag is echoed straight back to us by the server; this lets
// inet2rfHandler() recognise those echoes so it never re-gates our own reports
// from INET back to RF. Our reports reach RF exclusively through their own
// "Send via RF" (*_2rf) flags in weather.c / beacon.c - the IGATE INET->RF
// filter is for foreign internet traffic only, never our own.
static bool inet_line_is_own_report(const char *line) {
    // Source call is everything up to the first '-' (SSID) or '>' (path).
    size_t srcLen = 0;
    while (line[srcLen] && line[srcLen] != '-' && line[srcLen] != '>')
        srcLen++;
    if (srcLen == 0)
        return false;

    // Every callsign any local report can transmit under. Blank entries are
    // skipped inside base_call_equals(), and reports that fall back to
    // aprs_mycall are covered by that entry.
    const char *calls[] = {
        g_config.aprs_mycall, g_config.my_callsign, g_config.trk_mycall,  g_config.digi_mycall,
        g_config.wx_mycall,   g_config.tlm0_mycall, g_config.tlm1_mycall, g_config.msg_mycall,
    };
    for (size_t i = 0; i < sizeof(calls) / sizeof(calls[0]); i++) {
        if (base_call_equals(line, srcLen, calls[i]))
            return true;
    }
    return false;
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
        // Never gate our OWN reports from INET back to RF. After we upload a
        // beacon / weather / telemetry / message report to APRS-IS (via its
        // *_2inet flag) the server echoes it right back to us; without this
        // guard the IGATE INET->RF filter below would decide whether our own
        // report is re-transmitted on RF, so turning a type OFF in that filter
        // would silence our own report on the air. Our reports must reach RF
        // ONLY through their own "Send via RF" (*_2rf) flags (weather.c /
        // beacon.c); this filter governs foreign internet traffic exclusively.
        // Re-gating our own echo would also double-transmit it (once directly
        // via *_2rf, once here) and is a classic IGate feedback-loop source.
        if (inet_line_is_own_report(line)) {
            ESP_LOGD(TAG, "INET2RF: own report echoed by APRS-IS, not re-gated (its *_2rf flag governs RF): %s", line);
            return;
        }

        // g_config.inet2rfFilter is a whitelist of payload types (the IGATE
        // Filter fieldset on the /igate page): classify the line and drop it
        // unless its bit is set. Unclassifiable payloads - third-party
        // traffic in particular, which is exactly how an IGate loop starts -
        // classify as 0 and never pass.
        //
        // Dropped lines are logged at debug level only: the RX-IS entry
        // igate.c already added to the traffic ring covers them, and an
        // unfiltered APRS-IS feed would otherwise flush the ring with lines
        // that never went anywhere.
        uint16_t type = aprs_filter_classify_tnc2(line);
        if (!aprs_filter_pass(g_config.inet2rfFilter, type)) {
            ESP_LOGD(TAG, "INET2RF filtered (%s, mask=0x%03X): %s", aprs_filter_type_name(type), (unsigned)g_config.inet2rfFilter, line);
            return;
        }

        if (aprs_service_send_tnc2(line, strlen(line))) {
            atomic_fetch_add_explicit(&s_statInet2Rf, 1, memory_order_relaxed);
            ESP_LOGD(TAG, "INET2RF TX: %s", line);
            trafficlog_add_pkt("INET2RF", callsign, line, -1, symTable, symCode);
        }
    }
}

// ---------------------------------------------------------------------------
// Outbound message TX: message.c hands us a built TNC2 packet + channel mask.
// ---------------------------------------------------------------------------
static void messageTxHandler(const char *packet, size_t len, uint8_t channels) {
    if (channels & MSG_CHANNEL_RF)
        aprs_service_send_tnc2(packet, len);
    if (channels & MSG_CHANNEL_INET)
        igate_send_raw(packet, len);
}

// The old afskPollTask() is gone: AFSK_Poll()/APRS_poll() were the previous
// component's "the application must pump the DSP and drain the frame queue"
// contract. esp32idf_radioamateur_modem owns both - AFSK_init() starts its own
// pinned RX DSP task, and modem_init() starts the "modem_svc" task that drives
// AFSK_ServiceTx()/Ax25TransmitCheck() and drains RX frames into the callback.
// Calling AFSK_Poll() from here would now race that task over the same FIFO.
// (The old AFSK_Poll(false, g_config.rf_power) also carried the RF power-switch
// pin; the new component has no such output, and rf_power/band were dead
// config with no consumer anywhere in the app, so both were removed - see
// the Radio/Modem page's Save handler and app_config.c.)

static void serviceTickTask(void *arg) {
    while (1) {
        // 1 Hz weather sensor refresh, folded in here instead of running its
        // own wx_sensor_task (saves that task's stack). weather_start() has
        // already run by the time this task is created, so the shared container
        // and sensor registry are ready.
        weather_service_1hz();

        if (g_config.msg_enable)
            sendAPRSMessageRetry();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ---------------------------------------------------------------------------
// Audio ADC/DAC AFSK modem "LOOP TEST" (Radio/Modem webconfig page).
// ---------------------------------------------------------------------------

#define LOOP_TEST_TIMEOUT_MS 4000

static SemaphoreHandle_t s_loopTestSem = NULL;
static volatile bool s_loopTestActive = false;
static volatile bool s_loopTestGotFrame = false;
static char s_loopTestToken[8];
static char s_loopTestRxInfo[128];
static uint16_t s_loopTestRxMVrms = 0;

void aprs_service_notify_modem_ready(void) {
    s_modemReady = true;
}

bool aprs_service_modem_ready(void) {
    return s_modemReady;
}

// ---------------------------------------------------------------------------
// Loop-test diagnostics.
//
// The old component exposed purpose-built latching diagnostics for this
// (AFSK_getAdcDiag/AFSK_getSquelchDiag/AFSK_getAgcDiag/Modem_getDcdDiag*/
// Ax25GetFrameDiag/Ax25GetFailedFrame). The new one exposes instantaneous
// getters instead - afskGetRms(), afskGetAgcGain(), afskGetDcOffset(),
// ModemDcdState(), Ax25GetRxStage(), ModemGetSignalLevel() - plus a passive
// raw-sample tap, afskDiagCaptureRaw(), that reads straight out of the
// conversion ISR without disturbing the live RX task.
//
// So the latching is done here instead: a monitor task samples those getters
// throughout the test window and records the peaks/high-water marks the
// failure messages need. This keeps every distinction the old diagnostics drew
// (ADC dead vs. no tone vs. tone but no lock vs. lock but no frame) with two
// exceptions, noted where they are reported:
//   - there is no software squelch, so no "squelch never opened" case;
//   - there are no CRC-failure counters, so the furthest HDLC RX stage reached
//     stands in for the old "N frame attempts seen, all failed CRC".
// ---------------------------------------------------------------------------
#define LOOP_DIAG_RAW_SAMPLES 512

typedef struct {
    volatile bool stop;
    // Raw ADC min/max, captured mid-preamble via the passive ISR tap.
    int16_t rawMin;
    int16_t rawMax;
    int rawCount;
    // High-water marks sampled across the whole test window.
    uint16_t mVrmsPeak;
    float agcGainPeak;
    uint8_t dcdLatch; // OR of ModemDcdState() - one bit per demodulator
    uint8_t rxStageMax[MODEM_MAX_DEMODULATOR_COUNT];
    uint32_t adcSamplesStart;
    uint32_t adcSamplesEnd;
    volatile TaskHandle_t task;
} loop_diag_t;

static loop_diag_t s_diag;

static void loopDiagTask(void *arg) {
    loop_diag_t *d = (loop_diag_t *)arg;
    static int16_t raw[LOOP_DIAG_RAW_SAMPLES];

    // Wait out the start of the preamble so the capture lands on real tone
    // rather than on the idle line, then take one passive snapshot.
    vTaskDelay(pdMS_TO_TICKS(50));
    d->rawCount = afskDiagCaptureRaw(raw, LOOP_DIAG_RAW_SAMPLES, 500);
    for (int i = 0; i < d->rawCount; i++) {
        if (raw[i] < d->rawMin)
            d->rawMin = raw[i];
        if (raw[i] > d->rawMax)
            d->rawMax = raw[i];
    }

    while (!d->stop) {
        uint16_t rms = afskGetRms();
        if (rms > d->mVrmsPeak)
            d->mVrmsPeak = rms;

        float gain = afskGetAgcGain();
        if (gain > d->agcGainPeak)
            d->agcGainPeak = gain;

        d->dcdLatch |= ModemDcdState();

        uint8_t demods = ModemGetDemodulatorCount();
        if (demods > MODEM_MAX_DEMODULATOR_COUNT)
            demods = MODEM_MAX_DEMODULATOR_COUNT;
        for (uint8_t i = 0; i < demods; i++) {
            uint8_t stage = (uint8_t)Ax25GetRxStage(i);
            if (stage > d->rxStageMax[i])
                d->rxStageMax[i] = stage;
        }

        d->adcSamplesEnd = afskGetAdcSampleCount();
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    d->task = NULL;
    vTaskDelete(NULL);
}

static void loopDiagStart(void) {
    memset(&s_diag, 0, sizeof(s_diag));
    s_diag.rawMin = INT16_MAX;
    s_diag.rawMax = INT16_MIN;
    s_diag.adcSamplesStart = afskGetAdcSampleCount();
    s_diag.adcSamplesEnd = s_diag.adcSamplesStart;
    s_diag.stop = false;
    if (xTaskCreate(loopDiagTask, "loop_diag", 3072, &s_diag, 7, (TaskHandle_t *)&s_diag.task) != pdPASS) {
        s_diag.task = NULL;
        ESP_LOGW(TAG, "Loop test: could not start the diagnostics task - a failure will be reported without detail");
    }
}

static void loopDiagStop(void) {
    s_diag.stop = true;
    // The task deletes itself; give it a few of its 2 ms polls to notice.
    for (int i = 0; i < 20 && s_diag.task != NULL; i++)
        vTaskDelay(pdMS_TO_TICKS(5));
}

// AX.25 RX hook installed only while a loop test is in flight, so a
// self-generated test frame is never mistaken for real RF traffic
// (digipeated, sent to APRS-IS, etc.).
static void loopTestRxHook(ax25_msg_t *msg) {
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
    s_rxHook = loopTestRxHook;

    // A DAC->ADC wire means the node always hears its own carrier, so in the
    // half-duplex config normal operation uses, Ax25TransmitCheck()'s CSMA
    // would never find a clear channel and the test frame would never be
    // keyed. Switch to full duplex for the duration and put the configured
    // mode back afterwards, whatever the outcome.
    modem_config_t testCfg;
    aprs_service_build_modem_config(&testCfg, true);
    modem_set_modem(&testCfg);

    loopDiagStart();

    ESP_LOGI(TAG, "Loop test: TX %s", tnc2);
    if (!aprs_service_send_tnc2(tnc2, (size_t)n))
        ESP_LOGW(TAG, "Loop test: TX frame was discarded, test will time out");

    bool signaled = (xSemaphoreTake(s_loopTestSem, pdMS_TO_TICKS(LOOP_TEST_TIMEOUT_MS)) == pdTRUE);

    loopDiagStop();

    // Always restore the real RX hook and the configured duplex mode before
    // doing anything else, so a failed/timed-out test doesn't leave real RX
    // frames being swallowed by the test hook or the radio keying over other
    // stations.
    s_rxHook = aprs_msg_callback;
    aprs_service_apply_modem_config();
    s_loopTestActive = false;

    if (!signaled || !s_loopTestGotFrame) {
        int adcSwing = (s_diag.rawCount > 0) ? ((int)s_diag.rawMax - (int)s_diag.rawMin) : 0;
        bool adcAlive = (s_diag.adcSamplesEnd != s_diag.adcSamplesStart);

        if (!adcAlive || s_diag.rawCount == 0) {
            // The sample counter never moved / the ISR tap produced nothing -
            // the ADC continuous driver isn't running at all. That's an init
            // failure, not a wiring or level problem.
            snprintf(msg, msg_len,
                     "FAIL: no packet was received back within %d ms, and the ADC never delivered a single sample "
                     "(%d captured, sample counter stuck at %lu). The ADC continuous driver/timer isn't running - "
                     "this points at an init failure, not a wiring or level problem.",
                     LOOP_TEST_TIMEOUT_MS, s_diag.rawCount, (unsigned long)s_diag.adcSamplesStart);
        } else if (adcSwing < 50) {
            // The ADC is sampling, but the raw code barely moved - it is not
            // seeing the DAC's tone at all (flat/near-DC line). Report the DC
            // offset the component tracks (already in mV) alongside the raw
            // codes: a line pinned near VDD points at a miswire/short rather
            // than a units/scale bug in the demodulator path.
            snprintf(msg, msg_len,
                     "FAIL: no packet was received back within %d ms. The ADC is sampling (raw code stayed within "
                     "%d-%d, a %d-count swing; DC offset ~%d mV), but is not seeing any audio tone - check that "
                     "GPIO%d (ADC in) and GPIO%d (DAC out) are actually wired together and both grounds are common.",
                     LOOP_TEST_TIMEOUT_MS, s_diag.rawMin, s_diag.rawMax, adcSwing, afskGetDcOffset(), MODEM_ADC_GPIO, MODEM_DAC_GPIO);
        } else if (s_diag.dcdLatch == 0) {
            // A real signal swing reached the ADC but no demodulator's PLL
            // ever asserted DCD, i.e. none of them locked onto the tones.
            //
            // Note: there is no "software squelch never opened" case to
            // separate out any more. The old component gated MODEM_DECODE()
            // behind an mVrms squelch (rfSql) and this branch had to tell
            // "squelch shut the decoder out" apart from "decoder ran but
            // didn't lock". This component has no such gate - every sample
            // reaches the demodulator and DCD is the only lock indicator - so
            // the rfSql-related failure text is gone with it.
            int8_t peak0 = 0, valley0 = 0, peak1 = 0, valley1 = 0;
            uint8_t level0 = 0, level1 = 0;
            ModemGetSignalLevel(0, &peak0, &valley0, &level0);
            if (ModemGetDemodulatorCount() > 1)
                ModemGetSignalLevel(1, &peak1, &valley1, &level1);

            snprintf(msg, msg_len,
                     "FAIL: no packet was received back within %d ms. The ADC saw a real signal (raw code swung "
                     "%d-%d, a %d-count range; RMS peaked at %u mV), so the demodulator did receive samples, but no "
                     "demodulator's correlator/PLL ever locked onto the tones. Per-demodulator: demod0 "
                     "(prefilter=%d, audioLPF/flatAudio=%s) level=%u%%, demod1 level=%u%% (AGC peak gain %.2fx). %s",
                     LOOP_TEST_TIMEOUT_MS, s_diag.rawMin, s_diag.rawMax, adcSwing, (unsigned)s_diag.mVrmsPeak, (int)ModemGetFilterType(0),
                     g_config.audio_lpf ? "on" : "off", (unsigned)level0, (unsigned)level1, (double)s_diag.agcGainPeak,
                     (s_diag.agcGainPeak <= 1.05f)
                         ? "The AGC gain never rose above unity, so the correlator saw the same tiny raw signal as "
                           "the ADC - check the AGC path rather than the baud rate."
                         : "Check that the AFSK modulation/baud rate on this page matches what was transmitted, and "
                           "try toggling the audio low-pass filter (a direct DAC->ADC loop never passes through a "
                           "real radio's deemphasis network).");
        } else {
            // A demodulator locked, but no valid AX.25 frame with the expected
            // token came back within the timeout. "Locked" only means enough
            // correctly-timed symbol transitions were seen - a much lower bar
            // than "every bit in a ~20-byte frame was clean". Report how far
            // the HDLC state machine got: reaching RX_STAGE_FRAME means flags
            // were found and bytes were being assembled, so the framing works
            // and the bits themselves are dirty (the old component's
            // "attempts seen, all failed CRC"); never getting past
            // RX_STAGE_FLAG/IDLE means bit-sync never produced a frame at all.
            uint8_t stageMax = 0;
            for (int i = 0; i < MODEM_MAX_DEMODULATOR_COUNT; i++)
                if (s_diag.rxStageMax[i] > stageMax)
                    stageMax = s_diag.rxStageMax[i];

            snprintf(msg, msg_len,
                     "FAIL: no packet was received back within %d ms, even though the demodulator's PLL locked onto "
                     "the tones (DCD bitmap 0x%02X, RMS peaked at %u mV, AGC peak gain %.2fx). Furthest HDLC receive "
                     "stage reached: %u (0=idle, 1=flag seen, 2=assembling a frame). %s",
                     LOOP_TEST_TIMEOUT_MS, (unsigned)s_diag.dcdLatch, (unsigned)s_diag.mVrmsPeak, (double)s_diag.agcGainPeak, (unsigned)stageMax,
                     (stageMax < (uint8_t)RX_STAGE_FRAME)
                         ? "No HDLC flag ever led into frame data - the bit-sync/framing state machine isn't "
                           "starting a frame at all, which points at a deeper bit-recovery bug rather than noise on "
                           "individual bits."
                         : "The receiver did start assembling frames but none passed the CRC check - the signal is "
                           "clean enough to fake a brief DCD lock but not clean enough to get an entire ~20-byte "
                           "frame bit-perfect. Check for a marginal signal level/SNR rather than a "
                           "baud-rate/modem-type mismatch.");
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
    message_alarm_configure(g_config.msg_alarm_enable, g_config.msg_alarm_gpio);
    message_set_tx_handler(messageTxHandler);
    igate_set_inet2rf_handler(inet2rfHandler);

    // Install the RX callback before main.c calls modem_init(): the component
    // starts its service task inside modem_init() and can deliver a frame the
    // moment it does.
    modem_set_rx_callback(on_rx_frame, NULL);

    // Always start the uplink task: it now idles itself (socket closed,
    // fast retry loop) whenever nothing needs APRS-IS, and comes up as soon
    // as igate_en, digi_loc2inet, or msg_inet is turned on - including via
    // a runtime web UI save, with no reboot required.
    igate_start();

    beacon_start();

    // Own-station weather: creates the shared weather_telemetry_data container,
    // refreshes it from the local sensor drivers at 1 Hz, and beacons an APRS
    // Weather Report at wx_interval.
    weather_start();

    // Periodic APRS bulletins (BLN1..BLN5), configured on the "Bulletins" web
    // admin page and persisted in their own LittleFS file (not g_config). The
    // task also enforces per-bulletin expiry.
#ifdef ENABLE_BULLETINS
    bulletins_start();
#endif

    // Periodic APRS Objects/Items, configured on the "Objects and Items" web
    // admin page and persisted in their own LittleFS file (not g_config). The
    // shared scheduler drives their transmission and kill retransmissions.
#ifdef ENABLE_OBJECTS_ITEMS
    objitems_start();
#endif

    // Single shared task that drives all of the above periodic transmissions
    // (tracker/igate/digi beacons, WX report, bulletins). Started last, after
    // beacon_start()/weather_start()/bulletins_start() have set up the state
    // its service functions read. This replaces the five separate beacon/
    // bulletin/WX tasks (~61 KB of stacks) with one (~14 KB), reclaiming heap.
    beacon_scheduler_start();

    // No afsk_poll task any more - the component runs its own RX DSP and TX
    // service tasks (see the note above serviceTickTask). Only the 1 Hz
    // housekeeping tick is ours.
    // sendAPRSMessageRetry() walks the same TX chain as the beacon tasks
    // (aprs_service_send_tnc2 -> modem_send_tnc2 -> modem_build_frame_tnc2 ->
    // ax25_encode/hdlcFrame), which stacks several ~300-450 byte buffers per
    // level. Give it the same stack budget as those tasks (see
    // BEACON_TASK_STACK_BYTES in beacon.c) instead of the old 3072, which was
    // sized as if this task only did housekeeping.
    xTaskCreate(serviceTickTask, "aprs_svc_tick", 10240, NULL, 4, NULL);

    ESP_LOGI(TAG, "APRS service started (digi=%d igate=%d msg=%d)", g_config.digi_en, g_config.igate_en, g_config.msg_enable);
}
