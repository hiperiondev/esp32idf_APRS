#include "afsk.h"
#include "app_config.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

// Renders the PTT GPIO field as a <select> restricted to pins that
// afsk_ptt_gpio_is_valid() actually accepts: output-capable ESP32 GPIOs,
// excluding the ones already wired to the ADC/DAC audio path
// (MODEM_ADC_GPIO / MODEM_DAC_GPIO), the input-only GPIO34-39 pads, and the
// internal-flash/PSRAM GPIO6-11 pads. This makes it impossible to select a
// colliding or unusable pin from the web UI in the first place, rather than
// only catching it on Save.
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

esp_err_t page_mod_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_MOD_GPIO_ASSIGNMENT, "mod");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/mod'>"
                                  "<p style='color:var(--sub);font-size:12px'>" TR_MOD_NOTE "</p>");

    web_fieldset_open(req, TR_F_RF_MODULE_GPIO);
    web_field_int(req, TR_F_TX_PIN, "rfTx", g_config.rf_tx_gpio);
    web_field_int(req, TR_F_RX_PIN, "rfRx", g_config.rf_rx_gpio);
    // These two pins configure the optional external RF module only. The
    // audio ADC/DAC modem (esp32idf_radioamateur_modem) still takes its
    // ADC/DAC pins as compile-time constants - MODEM_ADC_GPIO / MODEM_DAC_GPIO
    // in the top-level CMakeLists.txt - and ignores rfTx/rfRx above; the
    // previous component (esp32_IDF_libAPRS) took them at runtime. See the
    // read-only summary on the Radio / Modem page.
    //
    // PTT, however, IS applied at runtime now (aprs_service_build_modem_config()
    // maps rfPTT/rfPTTAct into modem_config_t.ptt_gpio/.ptt_active_high on
    // every boot and on every live re-apply). The dropdown below is built
    // from afsk_ptt_gpio_is_valid(), so it only ever offers output-capable
    // GPIOs that don't collide with MODEM_ADC_GPIO/MODEM_DAC_GPIO, keeping a
    // bad pin from being selected in the first place rather than merely
    // rejecting it on Save.
    web_field_int(req, TR_F_SQUELCH_PIN, "rfSQL", g_config.rf_sql_gpio);
    web_field_checkbox(req, TR_F_SQUELCH_ACTIVE_HIGH, "rfSQLAct", g_config.rf_sql_active);
    web_field_int(req, TR_F_POWER_DOWN_PIN, "rfPD", g_config.rf_pd_gpio);
    web_field_checkbox(req, TR_F_PD_ACTIVE_HIGH, "rfPDAct", g_config.rf_pd_active);
    web_field_int(req, TR_F_RF_POWER_SWITCH_PIN, "rfPWR", g_config.rf_pwr_gpio);
    web_field_checkbox(req, TR_F_PWR_ACTIVE_HIGH, "rfPWRAct", g_config.rf_pwr_active);
    web_field_ptt_gpio(req, g_config.rf_ptt_gpio);
    web_field_checkbox(req, TR_F_PTT_ACTIVE_HIGH, "rfPTTAct", g_config.rf_ptt_active);
    web_field_int(req, TR_F_ADC_DC_OFFSET, "adcOffset", g_config.adc_dc_offset);
    web_field_int(req, TR_F_SERIAL_BAUDRATE, "rfBaudrate", g_config.rf_baudrate);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_I2C_BUS_0);
    web_field_checkbox(req, TR_F_ENABLE, "i2cEn", g_config.i2c_enable);
    web_field_int(req, TR_F_SDA_PIN, "i2cSDA", g_config.i2c_sda_pin);
    web_field_int(req, TR_F_SCK_PIN, "i2cSCK", g_config.i2c_sck_pin);
    web_field_int(req, TR_F_FREQUENCY_HZ, "i2cFreq", g_config.i2c_freq);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_I2C_BUS_1);
    web_field_checkbox(req, TR_F_ENABLE, "i2c1En", g_config.i2c1_enable);
    web_field_int(req, TR_F_SDA_PIN, "i2c1SDA", g_config.i2c1_sda_pin);
    web_field_int(req, TR_F_SCK_PIN, "i2c1SCK", g_config.i2c1_sck_pin);
    web_field_int(req, TR_F_FREQUENCY_HZ, "i2c1Freq", g_config.i2c1_freq);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_1_WIRE);
    web_field_checkbox(req, TR_F_ENABLE, "oneWireEn", g_config.onewire_enable);
    web_field_int(req, TR_F_DATA_PIN, "oneWireIO", g_config.onewire_gpio);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_UART_0);
    web_field_checkbox(req, TR_F_ENABLE, "uart0En", g_config.uart0_enable);
    web_field_int(req, TR_F_BAUDRATE, "uart0BR", g_config.uart0_baudrate);
    web_field_int(req, TR_F_TX_PIN, "uart0TX", g_config.uart0_tx_gpio);
    web_field_int(req, TR_F_RX_PIN, "uart0RX", g_config.uart0_rx_gpio);
    web_field_int(req, TR_F_RTS_PIN, "uart0RTS", g_config.uart0_rts_gpio);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_UART_1);
    web_field_checkbox(req, TR_F_ENABLE, "uart1En", g_config.uart1_enable);
    web_field_int(req, TR_F_BAUDRATE, "uart1BR", g_config.uart1_baudrate);
    web_field_int(req, TR_F_TX_PIN, "uart1TX", g_config.uart1_tx_gpio);
    web_field_int(req, TR_F_RX_PIN, "uart1RX", g_config.uart1_rx_gpio);
    web_field_int(req, TR_F_RTS_PIN, "uart1RTS", g_config.uart1_rts_gpio);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_UART_2);
    web_field_checkbox(req, TR_F_ENABLE, "uart2En", g_config.uart2_enable);
    web_field_int(req, TR_F_BAUDRATE, "uart2BR", g_config.uart2_baudrate);
    web_field_int(req, TR_F_TX_PIN, "uart2TX", g_config.uart2_tx_gpio);
    web_field_int(req, TR_F_RX_PIN, "uart2RX", g_config.uart2_rx_gpio);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_MODBUS);
    web_field_checkbox(req, TR_F_ENABLE, "modbusEn", g_config.modbus_enable);
    web_field_int(req, TR_F_SLAVE_ADDRESS, "modbusAddr", g_config.modbus_address);
    web_field_int(req, TR_F_UART_CHANNEL, "modbusCh", g_config.modbus_channel);
    web_field_int(req, TR_F_DE_RE_PIN, "modbusDE", g_config.modbus_de_gpio);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_PULSE_COUNTERS);
    web_field_checkbox(req, TR_F_COUNTER_0_ENABLE, "cnt0En", g_config.counter0_enable);
    web_field_checkbox(req, TR_F_COUNTER_0_ACTIVE_HIGH, "cnt0Act", g_config.counter0_active);
    web_field_int(req, TR_F_COUNTER_0_PIN, "cnt0IO", g_config.counter0_gpio);
    web_field_checkbox(req, TR_F_COUNTER_1_ENABLE, "cnt1En", g_config.counter1_enable);
    web_field_checkbox(req, TR_F_COUNTER_1_ACTIVE_HIGH, "cnt1Act", g_config.counter1_active);
    web_field_int(req, TR_F_COUNTER_1_PIN, "cnt1IO", g_config.counter1_gpio);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_EXTERNAL_TNC);
    web_field_checkbox(req, TR_F_ENABLE, "extTNCEn", g_config.ext_tnc_enable);
    web_field_int(req, TR_F_UART_CHANNEL, "extTNCCh", g_config.ext_tnc_channel);
    web_field_int(req, TR_F_MODE, "extTNCMode", g_config.ext_tnc_mode);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_POWER_CONTROL);
    web_field_checkbox(req, TR_F_ENABLE_POWER_MANAGEMENT, "pwrEn", g_config.pwr_en);
    web_field_int(req, TR_F_MODE, "pwrMode", g_config.pwr_mode);
    web_field_int(req, TR_F_SLEEP_INTERVAL_S, "pwrSleep", g_config.pwr_sleep_interval);
    web_field_int(req, TR_F_STANDBY_DELAY_S, "pwrStanby", g_config.pwr_stanby_delay);
    web_field_int(req, TR_F_SLEEP_ACTIVATION_SOURCE, "pwrSleepAct", g_config.pwr_sleep_activate);
    web_field_int(req, TR_F_CONTROL_PIN, "pwrIO", g_config.pwr_gpio);
    web_field_checkbox(req, TR_F_ACTIVE_HIGH, "pwrIOAct", g_config.pwr_active);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_PPP_GSM_MODEM);
    web_field_checkbox(req, TR_F_ENABLE, "pppEn", g_config.ppp_enable);
    web_field_text(req, TR_F_APN, "pppAPN", g_config.ppp_apn, 31);
    web_field_text(req, TR_F_SIM_PIN, "pppPin", g_config.ppp_pin, 7);
    web_field_int(req, TR_F_TX_PIN, "pppTX", g_config.ppp_tx_gpio);
    web_field_int(req, TR_F_RX_PIN, "pppRX", g_config.ppp_rx_gpio);
    web_field_int(req, TR_F_RTS_PIN, "pppRTS", g_config.ppp_rts_gpio);
    web_field_int(req, TR_F_CTS_PIN, "pppCTS", g_config.ppp_cts_gpio);
    web_field_int(req, TR_F_DTR_PIN, "pppDTR", g_config.ppp_dtr_gpio);
    web_field_int(req, TR_F_RI_PIN, "pppRI", g_config.ppp_ri_gpio);
    web_field_int(req, TR_F_RESET_PIN, "pppRST", g_config.ppp_rst_gpio);
    web_field_checkbox(req, TR_F_RESET_ACTIVE_HIGH, "pppRSTAct", g_config.ppp_rst_active);
    web_field_int(req, TR_F_RESET_DELAY_MS, "pppRSTDelay", g_config.ppp_rst_delay);
    web_field_int(req, TR_F_POWER_PIN, "pppPWR", g_config.ppp_pwr_gpio);
    web_field_checkbox(req, TR_F_POWER_ACTIVE_HIGH, "pppPWRAct", g_config.ppp_pwr_active);
    web_field_int(req, TR_F_SERIAL_CHANNEL, "pppSerial", g_config.ppp_serial);
    web_field_int(req, TR_F_SERIAL_BAUDRATE, "pppSerialBaudrate", g_config.ppp_serial_baudrate);
    web_field_int(req, TR_F_MODEM_MODEL_ID, "pppModel", g_config.ppp_model);
    web_field_checkbox(req, TR_F_HARDWARE_FLOW_CONTROL, "pppFlow", g_config.ppp_flow_ctrl);
    web_field_checkbox(req, TR_F_GNSS_PASSTHROUGH_VIA_MODEM, "pppGNSS", g_config.ppp_gnss);
    web_field_checkbox(req, TR_F_NAT_NAPT_SHARE_DATA_TO_WIFI_CLIENTS, "pppNAPT", g_config.ppp_napt);
    web_fieldset_close(req);

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_mod_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char *body = malloc(3000);
    if (!body) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    if (web_read_body(req, body, 3000) < 0) {
        free(body);
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    g_config.rf_tx_gpio = (int8_t)web_form_get_int(body, "rfTx", g_config.rf_tx_gpio);
    g_config.rf_rx_gpio = (int8_t)web_form_get_int(body, "rfRx", g_config.rf_rx_gpio);
    g_config.rf_sql_gpio = (int8_t)web_form_get_int(body, "rfSQL", g_config.rf_sql_gpio);
    g_config.rf_sql_active = web_form_get_bool(body, "rfSQLAct");
    g_config.rf_pd_gpio = (int8_t)web_form_get_int(body, "rfPD", g_config.rf_pd_gpio);
    g_config.rf_pd_active = web_form_get_bool(body, "rfPDAct");
    g_config.rf_pwr_gpio = (int8_t)web_form_get_int(body, "rfPWR", g_config.rf_pwr_gpio);
    g_config.rf_pwr_active = web_form_get_bool(body, "rfPWRAct");
    // A malicious/hand-crafted POST could still send a value the <select>
    // never offers (e.g. an ADC/DAC pin or an input-only one), so re-validate
    // here too instead of trusting the dropdown alone.
    int8_t ptt_gpio = (int8_t)web_form_get_int(body, "rfPTT", g_config.rf_ptt_gpio);
    g_config.rf_ptt_gpio = afsk_ptt_gpio_is_valid(ptt_gpio) ? ptt_gpio : -1;
    g_config.rf_ptt_active = web_form_get_bool(body, "rfPTTAct");
    g_config.adc_dc_offset = (uint16_t)web_form_get_int(body, "adcOffset", g_config.adc_dc_offset);
    g_config.rf_baudrate = (unsigned long)web_form_get_int(body, "rfBaudrate", g_config.rf_baudrate);

    g_config.i2c_enable = web_form_get_bool(body, "i2cEn");
    g_config.i2c_sda_pin = (int8_t)web_form_get_int(body, "i2cSDA", g_config.i2c_sda_pin);
    g_config.i2c_sck_pin = (int8_t)web_form_get_int(body, "i2cSCK", g_config.i2c_sck_pin);
    g_config.i2c_freq = (uint32_t)web_form_get_int(body, "i2cFreq", g_config.i2c_freq);
    g_config.i2c1_enable = web_form_get_bool(body, "i2c1En");
    g_config.i2c1_sda_pin = (int8_t)web_form_get_int(body, "i2c1SDA", g_config.i2c1_sda_pin);
    g_config.i2c1_sck_pin = (int8_t)web_form_get_int(body, "i2c1SCK", g_config.i2c1_sck_pin);
    g_config.i2c1_freq = (uint32_t)web_form_get_int(body, "i2c1Freq", g_config.i2c1_freq);

    g_config.onewire_enable = web_form_get_bool(body, "oneWireEn");
    g_config.onewire_gpio = (int8_t)web_form_get_int(body, "oneWireIO", g_config.onewire_gpio);

    g_config.uart0_enable = web_form_get_bool(body, "uart0En");
    g_config.uart0_baudrate = (unsigned long)web_form_get_int(body, "uart0BR", g_config.uart0_baudrate);
    g_config.uart0_tx_gpio = (int8_t)web_form_get_int(body, "uart0TX", g_config.uart0_tx_gpio);
    g_config.uart0_rx_gpio = (int8_t)web_form_get_int(body, "uart0RX", g_config.uart0_rx_gpio);
    g_config.uart0_rts_gpio = (int8_t)web_form_get_int(body, "uart0RTS", g_config.uart0_rts_gpio);
    g_config.uart1_enable = web_form_get_bool(body, "uart1En");
    g_config.uart1_baudrate = (unsigned long)web_form_get_int(body, "uart1BR", g_config.uart1_baudrate);
    g_config.uart1_tx_gpio = (int8_t)web_form_get_int(body, "uart1TX", g_config.uart1_tx_gpio);
    g_config.uart1_rx_gpio = (int8_t)web_form_get_int(body, "uart1RX", g_config.uart1_rx_gpio);
    g_config.uart1_rts_gpio = (int8_t)web_form_get_int(body, "uart1RTS", g_config.uart1_rts_gpio);
    g_config.uart2_enable = web_form_get_bool(body, "uart2En");
    g_config.uart2_baudrate = (unsigned long)web_form_get_int(body, "uart2BR", g_config.uart2_baudrate);
    g_config.uart2_tx_gpio = (int8_t)web_form_get_int(body, "uart2TX", g_config.uart2_tx_gpio);
    g_config.uart2_rx_gpio = (int8_t)web_form_get_int(body, "uart2RX", g_config.uart2_rx_gpio);

    g_config.modbus_enable = web_form_get_bool(body, "modbusEn");
    g_config.modbus_address = (uint8_t)web_form_get_int(body, "modbusAddr", g_config.modbus_address);
    g_config.modbus_channel = (int8_t)web_form_get_int(body, "modbusCh", g_config.modbus_channel);
    g_config.modbus_de_gpio = (int8_t)web_form_get_int(body, "modbusDE", g_config.modbus_de_gpio);

    g_config.counter0_enable = web_form_get_bool(body, "cnt0En");
    g_config.counter0_active = web_form_get_bool(body, "cnt0Act");
    g_config.counter0_gpio = (int8_t)web_form_get_int(body, "cnt0IO", g_config.counter0_gpio);
    g_config.counter1_enable = web_form_get_bool(body, "cnt1En");
    g_config.counter1_active = web_form_get_bool(body, "cnt1Act");
    g_config.counter1_gpio = (int8_t)web_form_get_int(body, "cnt1IO", g_config.counter1_gpio);

    g_config.ext_tnc_enable = web_form_get_bool(body, "extTNCEn");
    g_config.ext_tnc_channel = (int8_t)web_form_get_int(body, "extTNCCh", g_config.ext_tnc_channel);
    g_config.ext_tnc_mode = (int8_t)web_form_get_int(body, "extTNCMode", g_config.ext_tnc_mode);

    g_config.pwr_en = web_form_get_bool(body, "pwrEn");
    g_config.pwr_mode = (uint8_t)web_form_get_int(body, "pwrMode", g_config.pwr_mode);
    g_config.pwr_sleep_interval = (uint16_t)web_form_get_int(body, "pwrSleep", g_config.pwr_sleep_interval);
    g_config.pwr_stanby_delay = (uint16_t)web_form_get_int(body, "pwrStanby", g_config.pwr_stanby_delay);
    g_config.pwr_sleep_activate = (uint8_t)web_form_get_int(body, "pwrSleepAct", g_config.pwr_sleep_activate);
    g_config.pwr_gpio = (int8_t)web_form_get_int(body, "pwrIO", g_config.pwr_gpio);
    g_config.pwr_active = web_form_get_bool(body, "pwrIOAct");

    g_config.ppp_enable = web_form_get_bool(body, "pppEn");
    web_form_get(body, "pppAPN", g_config.ppp_apn, sizeof(g_config.ppp_apn));
    web_form_get(body, "pppPin", g_config.ppp_pin, sizeof(g_config.ppp_pin));
    g_config.ppp_tx_gpio = (int8_t)web_form_get_int(body, "pppTX", g_config.ppp_tx_gpio);
    g_config.ppp_rx_gpio = (int8_t)web_form_get_int(body, "pppRX", g_config.ppp_rx_gpio);
    g_config.ppp_rts_gpio = (int8_t)web_form_get_int(body, "pppRTS", g_config.ppp_rts_gpio);
    g_config.ppp_cts_gpio = (int8_t)web_form_get_int(body, "pppCTS", g_config.ppp_cts_gpio);
    g_config.ppp_dtr_gpio = (int8_t)web_form_get_int(body, "pppDTR", g_config.ppp_dtr_gpio);
    g_config.ppp_ri_gpio = (int8_t)web_form_get_int(body, "pppRI", g_config.ppp_ri_gpio);
    g_config.ppp_rst_gpio = (int8_t)web_form_get_int(body, "pppRST", g_config.ppp_rst_gpio);
    g_config.ppp_rst_active = web_form_get_bool(body, "pppRSTAct");
    g_config.ppp_rst_delay = (uint16_t)web_form_get_int(body, "pppRSTDelay", g_config.ppp_rst_delay);
    g_config.ppp_pwr_gpio = (int8_t)web_form_get_int(body, "pppPWR", g_config.ppp_pwr_gpio);
    g_config.ppp_pwr_active = web_form_get_bool(body, "pppPWRAct");
    g_config.ppp_serial = (uint8_t)web_form_get_int(body, "pppSerial", g_config.ppp_serial);
    g_config.ppp_serial_baudrate = (unsigned long)web_form_get_int(body, "pppSerialBaudrate", g_config.ppp_serial_baudrate);
    g_config.ppp_model = (uint8_t)web_form_get_int(body, "pppModel", g_config.ppp_model);
    g_config.ppp_flow_ctrl = web_form_get_bool(body, "pppFlow");
    g_config.ppp_gnss = web_form_get_bool(body, "pppGNSS");
    g_config.ppp_napt = web_form_get_bool(body, "pppNAPT");

    free(body);
    app_config_save();
    web_send_saved_redirect(req, "/mod");
    return ESP_OK;
}
