#include <string.h>

#include "app_config.h"
#include "aprs_service.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

esp_err_t page_radio_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_RADIO_MODEM, "radio");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/radio'>");

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
    web_field_int(req, TR_F_SQUELCH_LEVEL, "rfSql", g_config.sql_level);
    web_field_int(req, TR_F_VOLUME, "rfVolume", g_config.volume);
    web_field_int(req, TR_F_ADC_ATTENUATION_0_3, "adcAtten", g_config.adc_atten);
    web_field_checkbox(req, TR_F_RF_POWER_BOOST, "rfPwr", g_config.rf_power);
    web_field_checkbox(req, TR_F_AUDIO_LOW_PASS_FILTER, "audioLPF", g_config.audio_lpf);
    web_field_int(req, TR_F_PREAMBLE_MS, "rfPreamble", g_config.preamble);
    web_field_int(req, TR_F_TX_TIME_SLOT_MS, "txTimeSlot", g_config.tx_timeslot);
    web_field_int(req, TR_F_BAND_PLAN_INDEX, "rfBand", g_config.band);
    web_fieldset_close(req);

    httpd_resp_sendstr_chunk(req, "<script>"
                                  "function loopTest(){"
                                  "var btn=document.getElementById('loopTestBtn');"
                                  "var status=document.getElementById('loopTestStatus');"
                                  "btn.disabled=true;status.style.color='';status.textContent=' " TR_LOOPTEST_RUNNING "';"
                                  "fetch('/radio/looptest').then(function(r){return r.json();}).then(function(data){"
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

    char result[220];
    bool ok = aprs_loop_test_run(result, sizeof(result));

    // JSON-escape the result text: it can echo back raw RX payload bytes on
    // a mismatch, which are not guaranteed to be JSON-safe.
    char esc[440];
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

    char out[512];
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
    // afskSetModem() only understands values 0-3.
    int afsk_modem_in = web_form_get_int(body, "afskModem", g_config.afsk_modem_type);
    if (afsk_modem_in < 0)
        afsk_modem_in = 0;
    else if (afsk_modem_in > 3)
        afsk_modem_in = 3;
    g_config.afsk_modem_type = (uint8_t)afsk_modem_in;
    g_config.sql_level = (uint8_t)web_form_get_int(body, "rfSql", g_config.sql_level);
    g_config.volume = (uint8_t)web_form_get_int(body, "rfVolume", g_config.volume);
    // adcAtten is a free-typed field (not a <select>), and afskSetADCAtten()
    // in AFSK.c only recognizes 0-4 - anything outside that range falls
    // through all of its if/else-if branches and would leave
    // cfg_adc_atten/Vref out of sync with what's saved here. Clamp so an
    // out-of-range value can never desync the two.
    int adc_atten_in = web_form_get_int(body, "adcAtten", g_config.adc_atten);
    if (adc_atten_in < 0)
        adc_atten_in = 0;
    else if (adc_atten_in > 4)
        adc_atten_in = 4;
    g_config.adc_atten = (uint8_t)adc_atten_in;
    g_config.rf_power = web_form_get_bool(body, "rfPwr");
    g_config.audio_lpf = web_form_get_bool(body, "audioLPF");
    g_config.preamble = (uint16_t)web_form_get_int(body, "rfPreamble", g_config.preamble);
    g_config.tx_timeslot = (uint16_t)web_form_get_int(body, "txTimeSlot", g_config.tx_timeslot);
    g_config.band = (uint8_t)web_form_get_int(body, "rfBand", g_config.band);

    app_config_save();
    web_send_saved_redirect(req, "/radio");
    return ESP_OK;
}
