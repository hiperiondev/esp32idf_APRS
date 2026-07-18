/**
 * @file page_mqtt.c
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
 * @brief Web admin "MQTT" page: renders and saves the MQTT broker configuration
 * in g_config.
 */

#include "app_config.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

esp_err_t page_mqtt_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_MQTT, "mqtt");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/mqtt'>");

    web_fieldset_open(req, TR_F_MQTT_BROKER);
    web_field_checkbox(req, TR_F_ENABLE_MQTT, "mqttEnable", g_config.en_mqtt);
    web_field_text(req, TR_F_HOST, "mqttHost", g_config.mqtt_host, 62);
    web_field_int(req, TR_F_PORT, "mqttPort", g_config.mqtt_port);
    web_field_text(req, TR_F_USERNAME, "mqttUser", g_config.mqtt_user, 31);
    web_field_password(req, TR_F_PASSWORD, "mqttPass", g_config.mqtt_pass, 62);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_TOPICS);
    web_field_text(req, TR_F_PUBLISH_TOPIC, "mqttTopic", g_config.mqtt_topic, 62);
    web_field_text(req, TR_F_SUBSCRIBE_TOPIC, "mqttSub", g_config.mqtt_subscribe, 62);
    web_field_int(req, TR_F_PUBLISH_FLAGS_BITMASK, "mqttTopicFlag", g_config.mqtt_topic_flag);
    web_field_int(req, TR_F_SUBSCRIBE_FLAGS_BITMASK, "mqttSubFlag", g_config.mqtt_subscribe_flag);
    web_fieldset_close(req);

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_mqtt_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char body[900];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    g_config.en_mqtt = web_form_get_bool(body, "mqttEnable");
    web_form_get(body, "mqttHost", g_config.mqtt_host, sizeof(g_config.mqtt_host));
    g_config.mqtt_port = (uint16_t)web_form_get_int(body, "mqttPort", g_config.mqtt_port);
    web_form_get(body, "mqttUser", g_config.mqtt_user, sizeof(g_config.mqtt_user));
    web_form_get(body, "mqttPass", g_config.mqtt_pass, sizeof(g_config.mqtt_pass));
    web_form_get(body, "mqttTopic", g_config.mqtt_topic, sizeof(g_config.mqtt_topic));
    web_form_get(body, "mqttSub", g_config.mqtt_subscribe, sizeof(g_config.mqtt_subscribe));
    g_config.mqtt_topic_flag = (uint16_t)web_form_get_int(body, "mqttTopicFlag", g_config.mqtt_topic_flag);
    g_config.mqtt_subscribe_flag = (uint16_t)web_form_get_int(body, "mqttSubFlag", g_config.mqtt_subscribe_flag);

    app_config_save();
    web_send_saved_redirect(req, "/mqtt");
    return ESP_OK;
}
