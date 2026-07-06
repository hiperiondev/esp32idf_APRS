#include "app_config.h"
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
    web_field_int(req, TR_F_SQUELCH_LEVEL, "rfSql", g_config.sql_level);
    web_field_int(req, TR_F_VOLUME, "rfVolume", g_config.volume);
    web_field_checkbox(req, TR_F_RF_POWER_BOOST, "rfPwr", g_config.rf_power);
    web_field_checkbox(req, TR_F_AUDIO_LOW_PASS_FILTER, "audioLPF", g_config.audio_lpf);
    web_field_int(req, TR_F_PREAMBLE_MS, "rfPreamble", g_config.preamble);
    web_field_int(req, TR_F_TX_TIME_SLOT_MS, "txTimeSlot", g_config.tx_timeslot);
    web_field_int(req, TR_F_BAND_PLAN_INDEX, "rfBand", g_config.band);
    web_fieldset_close(req);

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
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

    g_config.sql_level = (uint8_t)web_form_get_int(body, "rfSql", g_config.sql_level);
    g_config.volume = (uint8_t)web_form_get_int(body, "rfVolume", g_config.volume);
    g_config.rf_power = web_form_get_bool(body, "rfPwr");
    g_config.audio_lpf = web_form_get_bool(body, "audioLPF");
    g_config.preamble = (uint16_t)web_form_get_int(body, "rfPreamble", g_config.preamble);
    g_config.tx_timeslot = (uint16_t)web_form_get_int(body, "txTimeSlot", g_config.tx_timeslot);
    g_config.band = (uint8_t)web_form_get_int(body, "rfBand", g_config.band);

    app_config_save();
    web_send_saved_redirect(req, "/radio");
    return ESP_OK;
}
