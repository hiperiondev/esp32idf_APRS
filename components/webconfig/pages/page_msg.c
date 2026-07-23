/**
 * @file page_msg.c
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
 * @brief Web admin "Message" page: renders and saves the APRS messaging
 * configuration (RF/INET destinations, encryption key) and provides the message
 * send/inbox interface.
 */

#include <string.h>

#include "afsk.h" // afsk_ptt_gpio_is_valid(): hardware-capable pins for the picker
#include "app_config.h"
#include "message.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

// Renders the Message Alarm GPIO field as a <select>. The pin list itself is
// restricted to what's physically able to drive an output
// (afsk_gpio_is_output_capable(): not input-only, not internal flash/PSRAM -
// those are truly unusable and stay hidden). message_alarm_gpio_is_valid()'s
// extra "not already used by the audio front-end/PTT/BMP180" rule is still
// enforced on Save, but here it's surfaced via the shared GPIO registry
// instead: a pin taken by another feature is still listed, just disabled and
// labelled with its owner, so the user sees the whole pin map rather than
// pins vanishing. Disabled ("-1") is always offered first.
static void web_field_msg_alarm_gpio(httpd_req_t *req, int8_t current) {
    web_select_open(req, TR_F_MESSAGE_ALARM_PIN, "msgAlarmGpio");
    web_select_option(req, -1, TR_DISABLED, current == -1);
    for (int gpio = 0; gpio <= 39; gpio++) {
        if (!afsk_gpio_is_output_capable((int8_t)gpio))
            continue;
        const char *owner = web_gpio_owner_tag(gpio, "Message Alarm");
        char label[48];
        if (owner)
            snprintf(label, sizeof(label), TR_GPIO_USED_BY, gpio, owner);
        else
            snprintf(label, sizeof(label), "GPIO%d", gpio);
        web_select_option_state(req, gpio, label, current == gpio, owner != NULL);
    }
    web_select_close(req);
}

esp_err_t page_msg_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_MESSAGE, "msg");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/msg'>");

    web_fieldset_open(req, TR_F_APRS_MESSAGING);
    web_field_checkbox(req, TR_F_ENABLE_MESSAGING, "msgEnable", g_config.msg_enable);
    web_field_use_station_data(req, "msgUseStation", g_config.msg_use_station, "msgMycall", NULL, NULL, NULL);
    web_field_text(req, TR_F_MY_CALLSIGN, "msgMycall", g_config.msg_mycall, 9);
    web_field_int(req, TR_F_PATH_BITMASK, "msgPath", g_config.msg_path);
    web_field_checkbox(req, TR_F_SEND_RECEIVE_VIA_RF, "msgRf", g_config.msg_rf);
    web_field_checkbox(req, TR_F_SEND_RECEIVE_VIA_INTERNET, "msgInet", g_config.msg_inet);
    web_field_int(req, TR_F_RETRY_COUNT, "msgRetry", g_config.msg_retry);
    web_field_int(req, TR_F_RETRY_INTERVAL_S, "msgInterval", g_config.msg_interval);
    web_field_checkbox(req, TR_F_MESSAGE_ALARM_ENABLE, "msgAlarmEn", g_config.msg_alarm_enable);
    web_field_msg_alarm_gpio(req, g_config.msg_alarm_gpio);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_ENCRYPTION);
    web_field_checkbox(req, TR_F_ENCRYPT_MESSAGES_AES, "msgEncrypt", g_config.msg_encrypt);
    web_field_password(req, TR_F_AES_KEY_HEX, "msgAESKey", g_config.msg_key, 32);
    web_fieldset_close(req);

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_msg_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char body[700];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    app_config_lock();
    g_config.msg_enable = web_form_get_bool(body, "msgEnable");
    g_config.msg_use_station = web_form_get_bool(body, "msgUseStation");
    if (g_config.msg_use_station) {
        strncpy(g_config.msg_mycall, g_config.my_callsign, sizeof(g_config.msg_mycall) - 1);
        g_config.msg_mycall[sizeof(g_config.msg_mycall) - 1] = 0;
    } else {
        web_form_get_call(body, "msgMycall", g_config.msg_mycall, sizeof(g_config.msg_mycall));
    }
    g_config.msg_path = (uint8_t)web_form_get_int(body, "msgPath", g_config.msg_path);
    g_config.msg_rf = web_form_get_bool(body, "msgRf");
    g_config.msg_inet = web_form_get_bool(body, "msgInet");
    g_config.msg_retry = (uint8_t)web_form_get_int(body, "msgRetry", g_config.msg_retry);
    g_config.msg_interval = (uint16_t)web_form_get_int(body, "msgInterval", g_config.msg_interval);
    g_config.msg_encrypt = web_form_get_bool(body, "msgEncrypt");
    web_form_get(body, "msgAESKey", g_config.msg_key, sizeof(g_config.msg_key));

    // A malicious/hand-crafted POST could still send a value the <select>
    // never offers (e.g. a pin already used by the modem or sensors_local),
    // so re-validate here too instead of trusting the dropdown alone - same
    // pattern as rf_ptt_gpio in page_mod.c.
    g_config.msg_alarm_enable = web_form_get_bool(body, "msgAlarmEn");
    int8_t alarm_gpio_in = (int8_t)web_form_get_int(body, "msgAlarmGpio", g_config.msg_alarm_gpio);
    g_config.msg_alarm_gpio = message_alarm_gpio_is_valid(alarm_gpio_in) ? alarm_gpio_in : -1;

    app_config_unlock();

    app_config_save();
    message_alarm_configure(g_config.msg_alarm_enable, g_config.msg_alarm_gpio);
    web_send_saved_redirect(req, "/msg");
    return ESP_OK;
}
