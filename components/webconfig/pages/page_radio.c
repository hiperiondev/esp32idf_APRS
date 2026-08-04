// @file page_radio.c
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
// @brief Web admin "Radiomodem" page: renders and saves the modem configuration,
// re-applying it live without a reboot, and runs the DAC-to-ADC loopback self
// test.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "aprs_service.h"
// hal/adc_types.h for ADC_ATTEN_DB_12: the component's config header defines
// MODEM_ADC_ATTEN as that enumerator but does not include its declaration (its
// own .c files pull in the ADC driver first), so a translation unit that
// *uses* the macro has to bring the enum in itself.
#include "hal/adc_types.h"

#include "afsk.h"
#include "esp32idf_radioamateur_modem.h"
#include "esp32idf_radioamateur_modem_config.h"
#include "esp_log.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

static const char *TAG = "page_radio";

// PTT GPIO is a fixed, compile-time-only board wiring choice (MODEM_PTT_GPIO,
// an internal radiomodem feature - like the audio ADC/DAC pins above, it is
// supplied by the top-level CMakeLists.txt) and therefore has no <select>
// here. Its active level (MODEM_PTT_ACTIVE_HIGH) is likewise a
// compile-time-only board wiring choice - there is no runtime <select>
// or checkbox for it either - and is surfaced read-only in the compile-time
// audio hardware info block below. The fixed pin is surfaced through
// web_gpio_owner_tag() (see web_common.c's GPIO registry) so it correctly
// shows up as "used" - labelled "PTT" - in every other GPIO picker on this
// admin (e.g. Message Alarm).

esp_err_t page_radio_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_RADIO_MODEM, "radio");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/radio' id='radioForm'>");

    web_fieldset_open(req, TR_F_PROTOCOL);
    web_field_checkbox(req, TR_F_FX_25_FORWARD_ERROR_CORRECTED_AX_25, "fx25Mode", g_config.fx25_mode);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_AUDIO_AFSK);
    {
        char buf[380];
        snprintf(buf, sizeof(buf),
                 "<label style='display:flex;align-items:center;gap:10px;flex-wrap:wrap;'>"
                 "<span><input type='checkbox' name='audioModemEn' %s> " TR_F_ENABLE_AUDIO_MODEM "</span>"
                 "<button type='button' class='secondary' id='loopTestBtn' onclick='loopTest()'>" TR_BTN_LOOP_TEST "</button>"
                 "<span id='loopTestStatus'></span>"
                 "</label>",
                 g_config.audio_modem_en ? "checked" : "");
        httpd_resp_sendstr_chunk(req, buf);
    }
    web_select_open(req, TR_F_AFSK_MODULATION, "afskModem");
    web_select_option(req, 0, "300 Bd (AFSK)", g_config.afsk_modem_type == 0);
    web_select_option(req, 1, "1200 Bd (AFSK/Bell202)", g_config.afsk_modem_type == 1);
    web_select_option(req, 2, "1200 Bd (AFSK/V.23)", g_config.afsk_modem_type == 2);
    web_select_option(req, 3, "9600 Bd (G3RUH/FSK)", g_config.afsk_modem_type == 3);
    web_select_close(req);
    // Squelch level / Volume / ADC attenuation / AGC max gain are shown
    // read-only: esp32idf_radioamateur_modem has no runtime equivalent for
    // any of them. It has no software squelch (the AX.25 decoder gates on the
    // demodulator's own DCD), no RX gain trim, a self-limiting AGC, and it
    // takes the ADC attenuation and both audio pins as compile-time constants,
    // so editable inputs would save to flash and change nothing. The values
    // shown are the ones actually compiled in.
    // PTT's GPIO (MODEM_PTT_GPIO) and its active level (MODEM_PTT_ACTIVE_HIGH)
    // are both fixed, compile-time-only board wiring choices - like the
    // ADC/DAC pins - supplied by the top-level CMakeLists.txt, so neither is
    // a selectable or checkbox field. Both are folded into the same read-only
    // compile-time info block as the ADC/DAC pins below, one item per line, so
    // the operator can see the whole audio/PTT hardware picture without digging
    // through the build files.
    {
        char ptt_pin_buf[16];
        if (MODEM_PTT_GPIO >= 0)
            snprintf(ptt_pin_buf, sizeof(ptt_pin_buf), "GPIO%d", MODEM_PTT_GPIO);
        else
            snprintf(ptt_pin_buf, sizeof(ptt_pin_buf), "%s", TR_DISABLED);

        char buf[900];
        snprintf(buf, sizeof(buf), "<p style='opacity:.75'><b>" TR_RADIO_AUDIO_HW_TITLE "</b>: " TR_RADIO_AUDIO_HW_INFO TR_RADIO_AUDIO_HW_NOTE "</p>",
                 MODEM_DAC_GPIO, MODEM_ADC_GPIO, ptt_pin_buf, MODEM_PTT_ACTIVE_HIGH ? TR_ENABLED : TR_F_OFF, (int)MODEM_ADC_ATTEN, MODEM_ADC_SAMPLERATE,
                 MODEM_DAC_SAMPLERATE);
        httpd_resp_sendstr_chunk(req, buf);
    }
    web_field_checkbox(req, TR_F_AUDIO_LOW_PASS_FILTER, "audioLPF", g_config.audio_lpf);
    web_field_int(req, TR_F_PREAMBLE_MS, "rfPreamble", g_config.preamble, RF_PREAMBLE_MS_MIN, RF_PREAMBLE_MS_MAX);
    web_field_int(req, TR_F_TX_TIME_SLOT_MS, "txTimeSlot", g_config.tx_timeslot, RF_TX_TIMESLOT_MS_MIN, RF_TX_TIMESLOT_MS_MAX);
    // How many frames may queue in the RF TX ring (waiting to key up or on
    // the air right now) before a new packet is dropped instead of queued -
    // see RF_TX_BUFFERS_MIN/MAX in aprs_service.h and g_config.rf_tx_buffers
    // in aprs_service.c. Applied live on Save (no reboot): the value is read
    // straight off g_config on every transmit. The dropdown's range comes
    // from the same RF_TX_BUFFERS_MIN/MAX used to clamp the saved value
    // below, so it can never offer a choice larger than the ring can hold.
    web_select_open(req, TR_F_RF_TX_BUFFERS, "rfTxBuffers");
    for (int i = RF_TX_BUFFERS_MIN; i <= RF_TX_BUFFERS_MAX; i++) {
        char label[4];
        snprintf(label, sizeof(label), "%d", i);
        web_select_option(req, i, label, g_config.rf_tx_buffers == i);
    }
    web_select_close(req);
    // Extra minimum PTT-off (unkeyed) hold time between transmissions, in
    // milliseconds, on top of the fixed one-service-tick release holdoff the
    // modem always applies (~10 ms). Useful for radios/repeaters that need a
    // longer guaranteed unkey gap between frames than the fixed floor gives
    // (e.g. to clear a repeater's courtesy tone or squelch tail before the
    // next packet keys up). 0 disables the extra hold. Applied live on Save,
    // same as rfPreamble/txTimeSlot above - see g_config.ptt_min_unkey_ms.
    web_field_int(req, TR_F_PTT_MIN_UNKEY_MS, "pttMinUnkeyMs", g_config.ptt_min_unkey_ms, PTT_MIN_UNKEY_MS_MIN, PTT_MIN_UNKEY_MS_MAX);
    // CSMA/p-persistent channel-access probability (standard AX.25/KISS
    // "Persist"): once the channel is heard clear, the modem transmits
    // immediately with probability csmaPersist/256 on every slot and
    // otherwise waits one more txTimeSlot before rolling again. 255
    // transmits on the first clear slot every time (equivalent to plain
    // non-persistent CSMA); lower values spread contending stations'
    // key-ups further apart. Applied live on Save, same as rfPreamble/
    // txTimeSlot above - see g_config.csma_persist.
    web_field_int(req, TR_F_CSMA_PERSISTENCE, "csmaPersist", g_config.csma_persist, CSMA_PERSIST_MIN, CSMA_PERSIST_MAX);
    web_fieldset_close(req);

    httpd_resp_sendstr_chunk(req, "<script>"
                                  "function loopTest(){"
                                  "var btn=document.getElementById('loopTestBtn');"
                                  "var status=document.getElementById('loopTestStatus');"
                                  "btn.disabled=true;status.style.color='';status.textContent=' " TR_LOOPTEST_SAVING "';"
                                  // Save the current form state first, then run the test, so what's
                                  // on screen is always what gets tested rather than whatever was last
                                  // saved to flash.
                                  "var form=document.getElementById('radioForm');"
                                  "var params=new URLSearchParams(new FormData(form));"
                                  "fetch('/radio',{method:'POST',body:params}).then(function(){"
                                  "status.textContent=' " TR_LOOPTEST_RUNNING "';"
                                  // POST, not GET: this route keys the transmitter, so it is
                                  // registered POST-only and goes through the same-origin check.
                                  "return fetch('/radio/looptest',{method:'POST'});"
                                  "}).then(function(r){return r.json();}).then(function(data){"
                                  "btn.disabled=false;"
                                  "status.style.color=data.ok?'green':'red';"
                                  "status.textContent=' '+data.msg;"
                                  "}).catch(function(){btn.disabled=false;status.style.color='red';status.textContent=' " TR_LOOPTEST_FAILED "';});"
                                  "}"
                                  "</script>");

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

// POST /radio/looptest - runs the audio ADC/DAC AFSK modem self-test (see
// aprs_loop_test_run()) and returns the result as JSON:
// {"ok":true/false,"msg":"..."}. Requires the ADC/DAC GPIOs to be wired
// together as a physical audio loopback.
//
// POST rather than GET, even though it reads like a query: the test keys the
// transmitter and puts a self-test frame on the air, which makes it a
// state-changing request. web_check_auth() runs its same-origin check on those
// only, so a GET route here would be reachable cross-origin - any page an
// authenticated admin happened to have open could key this station's radio
// with an <img src> pointing at it, because the browser attaches cached Basic
// credentials to such a request. POST also puts the route out of reach of the
// other ways a browser fetches a URL on its own (script/stylesheet loads,
// prefetch, link prerender, address-bar navigation).
esp_err_t page_radio_looptest_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    httpd_resp_set_type(req, "application/json");

    // 900, not 512: aprs_loop_test_run()'s failure messages are long - they
    // quote raw ADC min/max, RMS, AGC gain, the DCD bitmap and per-demodulator
    // levels, plus a paragraph of interpretation. They are plain prose: the
    // modem exposes no failed-frame buffer, so nothing here dumps raw bytes.
    char result[900];
    bool ok = aprs_loop_test_run(result, sizeof(result));

    // JSON-escape the result text: it can echo back raw RX payload bytes on
    // a mismatch, which are not guaranteed to be JSON-safe.
    char esc[1800];
    size_t o = 0;
    for (size_t i = 0; result[i] != 0 && o + 2 < sizeof(esc); i++) {
        unsigned char c = (unsigned char)result[i];
        if (c == '"' || c == '\\') {
            esc[o++] = '\\';
            esc[o++] = (char)c;
        } else if (c == '\n') {
            esc[o++] = '\\';
            esc[o++] = 'n';
        } else if (c < 0x20) {
            continue; // drop other control chars
        } else {
            esc[o++] = (char)c;
        }
    }
    esc[o] = 0;

    char out[2000];
    snprintf(out, sizeof(out), "{\"ok\":%s,\"msg\":\"%s\"}", ok ? "true" : "false", esc);
    httpd_resp_sendstr(req, out);
    return ESP_OK;
}

esp_err_t page_radio_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char body[1200];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    // These two are parsed here and stored below, inside app_config_lock(),
    // together with the rest of the form: every write to g_config on this page
    // happens under the lock, so the whole page lands as one update as far as
    // any concurrent reader is concerned.
    bool fx25_mode_in = web_form_get_bool(body, "fx25Mode");
    bool audio_modem_en_in = web_form_get_bool(body, "audioModemEn");

    // afskModem selects the AFSK software modem modulation (300/1200/1200 V.23/9600 Bd)
    // used for both RX and TX on the audio ADC/DAC modem - clamp defensively since
    // modem_mode_t only defines values 0-3 (AFSK300/BELL202/V23/G3RUH).
    int afsk_modem_in = web_form_get_int(body, "afskModem", g_config.afsk_modem_type);
    if (afsk_modem_in < 0)
        afsk_modem_in = 0;
    else if (afsk_modem_in > 3)
        afsk_modem_in = 3;
    app_config_lock();
    g_config.fx25_mode = fx25_mode_in ? 1 : 0;
    g_config.audio_modem_en = audio_modem_en_in;
    g_config.afsk_modem_type = (uint8_t)afsk_modem_in;
    // rfSql / rfVolume / adcAtten / agcMaxGain are not posted by the form
    // (see the read-only note in page_radio_get()); there are no g_config
    // fields behind them. The compiled-in values are displayed read-only.
    g_config.audio_lpf = web_form_get_bool(body, "audioLPF");
    // rfPTT (PTT GPIO) and rfPTTAct (PTT active-high) are not posted by the
    // form: both are fixed at compile time (MODEM_PTT_GPIO and
    // MODEM_PTT_ACTIVE_HIGH) and can't be changed from here.
    // TXDelay/preamble length (RF_PREAMBLE_MS_MIN..RF_PREAMBLE_MS_MAX ms,
    // default 300) - clamp defensively, same reasoning as afskModem above,
    // because an out-of-range value from a malformed POST should never be
    // stored. Nothing downstream would catch it either:
    // aprs_service_build_modem_config() copies this straight into the modem
    // configuration and Ax25TxDelay() turns it into a flag-byte count, so at
    // 1200 Bd an unclamped 65535 would put roughly a minute of continuous
    // carrier ahead of every single frame - a station that holds a shared VHF
    // APRS channel open indefinitely. Bounds come from aprs_service.h so the
    // form's min/max, this clamp and the flash-load clamp cannot drift apart.
    int preamble_in = web_form_get_int(body, "rfPreamble", g_config.preamble);
    if (preamble_in < RF_PREAMBLE_MS_MIN)
        preamble_in = RF_PREAMBLE_MS_MIN;
    else if (preamble_in > RF_PREAMBLE_MS_MAX)
        preamble_in = RF_PREAMBLE_MS_MAX;
    g_config.preamble = (uint16_t)preamble_in;

    // CSMA slot time (RF_TX_TIMESLOT_MS_MIN..RF_TX_TIMESLOT_MS_MAX ms,
    // default 2000) - clamp defensively, same reasoning as rfPreamble above.
    // This is how long the modem waits before rolling the p-persistence dice
    // again on a busy channel, so an out-of-range value stalls this station's
    // own transmissions rather than the channel's.
    int tx_timeslot_in = web_form_get_int(body, "txTimeSlot", g_config.tx_timeslot);
    if (tx_timeslot_in < RF_TX_TIMESLOT_MS_MIN)
        tx_timeslot_in = RF_TX_TIMESLOT_MS_MIN;
    else if (tx_timeslot_in > RF_TX_TIMESLOT_MS_MAX)
        tx_timeslot_in = RF_TX_TIMESLOT_MS_MAX;
    g_config.tx_timeslot = (uint16_t)tx_timeslot_in;

    // RF TX ring backlog limit (RF_TX_BUFFERS_MIN..RF_TX_BUFFERS_MAX frames,
    // default 1) - clamp defensively, same as afskModem above, since this
    // drives an array-free bound check in aprs_service_send_tnc2() rather
    // than an array index, but an out-of-range value from a malformed POST
    // should still never be stored. Bounds come from aprs_service.h so this
    // can never drift from the TX ring's real usable depth (see the comment
    // there on AX25_TX_FRAME_RING_MAX).
    int rf_tx_buffers_in = web_form_get_int(body, "rfTxBuffers", g_config.rf_tx_buffers);
    if (rf_tx_buffers_in < RF_TX_BUFFERS_MIN)
        rf_tx_buffers_in = RF_TX_BUFFERS_MIN;
    else if (rf_tx_buffers_in > RF_TX_BUFFERS_MAX)
        rf_tx_buffers_in = RF_TX_BUFFERS_MAX;
    g_config.rf_tx_buffers = (uint8_t)rf_tx_buffers_in;

    // Extra minimum PTT-off hold time between transmissions
    // (PTT_MIN_UNKEY_MS_MIN..PTT_MIN_UNKEY_MS_MAX ms, default 0 = disabled) -
    // clamp defensively against a malformed POST,
    // same reasoning as rf_tx_buffers above. See g_config.ptt_min_unkey_ms
    // and Ax25Config.minUnkeyTime in ax25.c for how this is enforced.
    int ptt_min_unkey_in = web_form_get_int(body, "pttMinUnkeyMs", g_config.ptt_min_unkey_ms);
    if (ptt_min_unkey_in < PTT_MIN_UNKEY_MS_MIN)
        ptt_min_unkey_in = PTT_MIN_UNKEY_MS_MIN;
    else if (ptt_min_unkey_in > PTT_MIN_UNKEY_MS_MAX)
        ptt_min_unkey_in = PTT_MIN_UNKEY_MS_MAX;
    g_config.ptt_min_unkey_ms = (uint16_t)ptt_min_unkey_in;

    // CSMA/p-persistent transmit probability (CSMA_PERSIST_MIN..
    // CSMA_PERSIST_MAX, default 63 = ~25%
    // chance per clear slot) - clamp defensively against a malformed POST,
    // same reasoning as rf_tx_buffers above. 0 is refused (it would key up
    // never, i.e. transmission would be permanently suppressed) rather than
    // silently coerced to a working value, so a bad input floors at
    // CSMA_PERSIST_MIN (lowest non-zero transmit probability) instead of
    // hiding the mistake.
    // See g_config.csma_persist and Ax25Config.persist in ax25.c for how
    // this is enforced.
    int csma_persist_in = web_form_get_int(body, "csmaPersist", g_config.csma_persist);
    if (csma_persist_in < CSMA_PERSIST_MIN)
        csma_persist_in = CSMA_PERSIST_MIN;
    else if (csma_persist_in > CSMA_PERSIST_MAX)
        csma_persist_in = CSMA_PERSIST_MAX;
    g_config.csma_persist = (uint8_t)csma_persist_in;

    app_config_unlock();

    // The page rendered next is built from the live settings, so the save
    // result is what decides whether the operator is told this reached flash.
    bool ok = app_config_save();
    if (!ok)
        ESP_LOGE(TAG, "radio/modem settings could not be written to flash");

    // Push every setting the modem accepts at runtime into the running modem,
    // so Save (and the loop test's auto-save, which POSTs this form before
    // running) takes effect without a reboot: modulation, preamble, time slot,
    // CSMA persistence, flat-audio flag, FX.25 mode and the PTT minimum unkey
    // time all go through modem_set_modem().
    //
    // rfTxBuffers needs no propagation into the modem component at all:
    // aprs_service_send_tnc2() (above modem.c, in this same binary) reads
    // g_config.rf_tx_buffers straight off g_config on every call, so it is
    // already live the instant app_config_unlock() runs, before this
    // function is even reached.
    //
    // audioModemEn still needs a reboot: modem_init() only runs at boot, from
    // main.c, and this no-ops until it has.
    aprs_service_apply_modem_config();

    web_send_save_result(req, ok, "/radio");
    return ESP_OK;
}
