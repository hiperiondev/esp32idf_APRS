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

#include "app_config.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

esp_err_t page_msg_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_MESSAGE, "msg");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/msg'>");

    web_fieldset_open(req, TR_F_APRS_MESSAGING);
    web_field_checkbox(req, TR_F_ENABLE_MESSAGING, "msgEnable", g_config.msg_enable);
    web_field_text(req, TR_F_MY_CALLSIGN, "msgMycall", g_config.msg_mycall, 9);
    web_field_int(req, TR_F_PATH_BITMASK, "msgPath", g_config.msg_path);
    web_field_checkbox(req, TR_F_SEND_RECEIVE_VIA_RF, "msgRf", g_config.msg_rf);
    web_field_checkbox(req, TR_F_SEND_RECEIVE_VIA_INTERNET, "msgInet", g_config.msg_inet);
    web_field_int(req, TR_F_RETRY_COUNT, "msgRetry", g_config.msg_retry);
    web_field_int(req, TR_F_RETRY_INTERVAL_S, "msgInterval", g_config.msg_interval);
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

    g_config.msg_enable = web_form_get_bool(body, "msgEnable");
    web_form_get_call(body, "msgMycall", g_config.msg_mycall, sizeof(g_config.msg_mycall));
    g_config.msg_path = (uint8_t)web_form_get_int(body, "msgPath", g_config.msg_path);
    g_config.msg_rf = web_form_get_bool(body, "msgRf");
    g_config.msg_inet = web_form_get_bool(body, "msgInet");
    g_config.msg_retry = (uint8_t)web_form_get_int(body, "msgRetry", g_config.msg_retry);
    g_config.msg_interval = (uint16_t)web_form_get_int(body, "msgInterval", g_config.msg_interval);
    g_config.msg_encrypt = web_form_get_bool(body, "msgEncrypt");
    web_form_get(body, "msgAESKey", g_config.msg_key, sizeof(g_config.msg_key));

    app_config_save();
    web_send_saved_redirect(req, "/msg");
    return ESP_OK;
}
