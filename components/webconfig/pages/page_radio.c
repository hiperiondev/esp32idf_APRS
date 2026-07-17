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

#include "esp32idf_radioamateur_modem.h"
#include "esp32idf_radioamateur_modem_config.h"
#include "afsk.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

// Renders the PTT GPIO field as a <select> restricted to pins that
// afsk_ptt_gpio_is_valid() actually accepts: output-capable ESP32 GPIOs,
// excluding whichever pins are wired to the ADC/DAC audio path
// (MODEM_ADC_GPIO / MODEM_DAC_GPIO), the input-only GPIO34-39 pads, and the
// internal-flash/PSRAM GPIO6-11 pads. This keeps a colliding or unusable pin
// from ever being selected in the UI, rather than only catching it on Save.
static void web_field_ptt_gpio(httpd_req_t *req, int8_t current) {
    // -1 = PTT disabled, always offered first regardless of validity.
    web_select_open(req, TR_F_PTT_PIN, "rfPTT");
    web_select_option(req, -1, TR_DISABLED, current == -1);
    for (int gpio = 0; gpio <= 39; gpio++) {
        if (!afsk_ptt_gpio_is_valid((int8_t)gpio))
            continue;
        char label[16];
        snprintf(label, sizeof(label), "GPIO%d", gpio);
        web_select_option(req, gpio, label, current == gpio);
    }
    web_select_close(req);
}

esp_err_t page_radio_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_RADIO_MODEM, "radio");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/radio' id='radioForm'>");

    web_fieldset_open(req, TR_F_PROTOCOL);
    web_field_checkbox(req, TR_F_FX_25_FORWARD_ERROR_CORRECTED_AX_25, "fx25Mode", g_config.fx25_mode);
    web_fieldset_close(req);

#ifdef ENABLE_RF_MODULE
    web_fieldset_open(req, TR_F_RF_MODULE);
    web_field_checkbox(req, TR_F_ENABLE_RF_MODULE, "rfEnable", g_config.rf_en);
    web_select_open(req, TR_F_MODULE_TYPE, "rfType");
    web_select_option(req, RF_SX1278, "SX1278", g_config.rf_type == RF_SX1278);
    web_select_option(req, RF_SX1276, "SX1276", g_config.rf_type == RF_SX1276);
    web_select_option(req, RF_SX1262, "SX1262", g_config.rf_type == RF_SX1262);
    web_select_option(req, RF_SX1268, "SX1268", g_config.rf_type == RF_SX1268);
    web_select_option(req, RF_SX1280, "SX1280", g_config.rf_type == RF_SX1280);
    web_select_close(req);
    web_select_open(req, TR_F_MODEM_MODE, "rfModem");
    web_select_option(req, RF_MODE_OFF, TR_F_OFF, g_config.modem_type == RF_MODE_OFF);
    web_select_option(req, RF_MODE_LoRa, "LoRa", g_config.modem_type == RF_MODE_LoRa);
    web_select_option(req, RF_MODE_G3RUH, "AFSK/G3RUH", g_config.modem_type == RF_MODE_G3RUH);
    web_select_option(req, RF_MODE_GFSK, "GFSK", g_config.modem_type == RF_MODE_GFSK);
    web_select_option(req, RF_MODE_DPRS, "D-PRS", g_config.modem_type == RF_MODE_DPRS);
    web_select_close(req);
    web_field_float(req, TR_F_RX_FREQUENCY_MHZ, "rfFreqRX", g_config.freq_rx, "0.001");
    web_field_float(req, TR_F_TX_FREQUENCY_MHZ, "rfFreqTX", g_config.freq_tx, "0.001");
    web_field_int(req, TR_F_CTCSS_DCS_RX_TONE, "rfToneRX", g_config.tone_rx);
    web_field_int(req, TR_F_CTCSS_DCS_TX_TONE, "rfToneTX", g_config.tone_tx);
    web_fieldset_close(req);
#endif

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
    // Squelch level / Volume / ADC attenuation / AGC max gain used to be
    // editable here and were pushed into the old esp32_IDF_libAPRS component
    // at runtime (afskSetSquelchLevel/afskSetVolume/afskSetAgcMaxGain, plus
    // adc_atten via aprs_modem_config_t). esp32idf_radioamateur_modem has no
    // equivalent for any of them: it has no software squelch (the AX.25
    // decoder gates on the demodulator's own DCD), no RX gain trim, a
    // self-limiting AGC, and it takes the ADC attenuation and both audio pins
    // as compile-time constants. Leaving the inputs on the page would have
    // meant four controls that save to flash and change nothing - the exact
    // failure this page's Save handler was previously fixed to avoid - so
    // they are shown read-only, sourced from the values actually compiled in.
    {
        char buf[420];
        snprintf(buf, sizeof(buf),
                 "<p style='opacity:.75'><b>Audio hardware (compile-time)</b>: "
                 "DAC out GPIO%d, ADC in GPIO%d, ADC attenuation %d, "
                 "ADC %d Hz / DAC %d Hz.<br>"
                 "Set these in the top-level CMakeLists.txt (MODEM_* build definitions) and rebuild. "
                 "The modem's AGC is automatic and it has no software squelch or RX volume control.</p>",
                 MODEM_DAC_GPIO, MODEM_ADC_GPIO, (int)MODEM_ADC_ATTEN, MODEM_ADC_SAMPLERATE, MODEM_DAC_SAMPLERATE);
        httpd_resp_sendstr_chunk(req, buf);
    }
    // Unlike the ADC/DAC pins above, PTT IS applied at runtime:
    // aprs_service_build_modem_config() maps rfPTT/rfPTTAct into
    // modem_config_t.ptt_gpio/.ptt_active_high on every boot and on every
    // live re-apply (Save, below). The dropdown is built from
    // afsk_ptt_gpio_is_valid(), so it only ever offers output-capable GPIOs
    // that don't collide with MODEM_ADC_GPIO/MODEM_DAC_GPIO - a bad pin can't
    // be selected in the first place, rather than merely being rejected on
    // Save.
    web_field_ptt_gpio(req, g_config.rf_ptt_gpio);
    web_field_checkbox(req, TR_F_PTT_ACTIVE_HIGH, "rfPTTAct", g_config.rf_ptt_active);
    web_field_checkbox(req, TR_F_AUDIO_LOW_PASS_FILTER, "audioLPF", g_config.audio_lpf);
    web_field_int(req, TR_F_PREAMBLE_MS, "rfPreamble", g_config.preamble);
    web_field_int(req, TR_F_TX_TIME_SLOT_MS, "txTimeSlot", g_config.tx_timeslot);
    web_fieldset_close(req);

    httpd_resp_sendstr_chunk(req, "<script>"
                                  "function loopTest(){"
                                  "var btn=document.getElementById('loopTestBtn');"
                                  "var status=document.getElementById('loopTestStatus');"
                                  "btn.disabled=true;status.style.color='';status.textContent=' " TR_LOOPTEST_SAVING "';"
                                  // Loop test previously ran against whatever was last *saved* to flash,
                                  // silently ignoring any field the user had just edited but not yet
                                  // submitted via the page's Save button - e.g. typing a new squelch
                                  // value and clicking "Run Loop Test" tested the OLD squelch level with
                                  // no indication anything was stale. Save the current form state first,
                                  // then run the test, so what's on screen is always what gets tested.
                                  "var form=document.getElementById('radioForm');"
                                  "var params=new URLSearchParams(new FormData(form));"
                                  "fetch('/radio',{method:'POST',body:params}).then(function(){"
                                  "status.textContent=' " TR_LOOPTEST_RUNNING "';"
                                  "return fetch('/radio/looptest');"
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

// GET /radio/looptest - runs the audio ADC/DAC AFSK modem self-test (see
// aprs_loop_test_run()) and returns the result as JSON:
// {"ok":true/false,"msg":"..."}. Requires the ADC/DAC GPIOs to be wired
// together as a physical audio loopback.
esp_err_t page_radio_looptest_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    httpd_resp_set_type(req, "application/json");

    // 900, not 512: aprs_loop_test_run()'s failure messages are long - they
    // quote raw ADC min/max, RMS, AGC gain, the DCD bitmap and per-demodulator
    // levels, plus a paragraph of interpretation. (The old raw-bytes hex dump
    // of a CRC-failed frame is gone with the previous component's
    // Ax25GetFailedFrame(); the replacement exposes no such buffer.)
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

    g_config.fx25_mode = web_form_get_bool(body, "fx25Mode") ? 1 : 0;

#ifdef ENABLE_RF_MODULE
    g_config.rf_en = web_form_get_bool(body, "rfEnable");
    g_config.rf_type = (uint8_t)web_form_get_int(body, "rfType", g_config.rf_type);
    g_config.modem_type = (uint8_t)web_form_get_int(body, "rfModem", g_config.modem_type);

    g_config.freq_rx = web_form_get_float(body, "rfFreqRX", g_config.freq_rx);
    g_config.freq_tx = web_form_get_float(body, "rfFreqTX", g_config.freq_tx);
    g_config.tone_rx = web_form_get_int(body, "rfToneRX", g_config.tone_rx);
    g_config.tone_tx = web_form_get_int(body, "rfToneTX", g_config.tone_tx);
#else
    g_config.rf_en = false;
#endif

    g_config.audio_modem_en = web_form_get_bool(body, "audioModemEn");
    // afskModem selects the AFSK software modem modulation (300/1200/1200 V.23/9600 Bd)
    // used for both RX and TX on the audio ADC/DAC modem. It is independent of the
    // optional RF module's own modem mode (rfModem, above) - clamp defensively since
    // modem_mode_t only defines values 0-3 (AFSK300/BELL202/V23/G3RUH).
    int afsk_modem_in = web_form_get_int(body, "afskModem", g_config.afsk_modem_type);
    if (afsk_modem_in < 0)
        afsk_modem_in = 0;
    else if (afsk_modem_in > 3)
        afsk_modem_in = 3;
    g_config.afsk_modem_type = (uint8_t)afsk_modem_in;
    // rfSql / rfVolume / adcAtten / agcMaxGain are no longer posted by the form
    // (see the read-only note in page_radio_get()). The g_config fields are
    // deliberately left untouched rather than deleted, so an existing
    // config.json round-trips unchanged through app_config_save() below and a
    // future component that can honour them finds the values still there.
    g_config.audio_lpf = web_form_get_bool(body, "audioLPF");
    // A malicious/hand-crafted POST could still send a value the <select>
    // never offers (e.g. an ADC/DAC pin or an input-only one), so re-validate
    // here too instead of trusting the dropdown alone.
    int8_t ptt_gpio_in = (int8_t)web_form_get_int(body, "rfPTT", g_config.rf_ptt_gpio);
    g_config.rf_ptt_gpio = afsk_ptt_gpio_is_valid(ptt_gpio_in) ? ptt_gpio_in : -1;
    g_config.rf_ptt_active = web_form_get_bool(body, "rfPTTAct");
    g_config.preamble = (uint16_t)web_form_get_int(body, "rfPreamble", g_config.preamble);
    g_config.tx_timeslot = (uint16_t)web_form_get_int(body, "txTimeSlot", g_config.tx_timeslot);

    app_config_save();

    // Push the settings that the new component *can* take at runtime into the
    // running modem, so Save (and the loop test's auto-save, which POSTs this
    // form before running) takes effect without a reboot: modulation, preamble,
    // time slot, flat-audio flag and FX.25 mode all go through
    // modem_set_modem(). This is the successor to the old
    // afskSetSquelchLevel()/afskSetVolume()/afskSetAgcMaxGain() block - it
    // covers strictly more of the page than that did.
    //
    // audioModemEn still needs a reboot: modem_init() only runs at boot, from
    // main.c, and this no-ops until it has.
    aprs_service_apply_modem_config();

    web_send_saved_redirect(req, "/radio");
    return ESP_OK;
}
