/**
 * @file page_gnss.c
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
 * @brief Web admin "GNSS" page: renders and saves the GNSS module configuration
 * in g_config.
 */

#include "app_config.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

esp_err_t page_gnss_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_GNSS, "gnss");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/gnss'>");

    web_fieldset_open(req, TR_F_GNSS_MODULE);
    web_field_checkbox(req, TR_F_ENABLE_GNSS, "gnssEn", g_config.gnss_enable);
    web_field_int(req, TR_F_UART_CHANNEL, "gnssCH", g_config.gnss_channel);
    web_field_int(req, TR_F_PPS_GPIO_1_NONE, "gnssPPS", g_config.gnss_pps_gpio);
    web_field_text(req, TR_F_INIT_AT_NMEA_COMMAND, "gnssAT", g_config.gnss_at_command, 29);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_NMEA_TCP_PASSTHROUGH);
    web_field_int(req, TR_F_TCP_SERVER_PORT_0_OFF, "gnssTCPPort", g_config.gnss_tcp_port);
    web_field_text(req, TR_F_FORWARD_TO_HOST_OPTIONAL, "gnssTCPHost", g_config.gnss_tcp_host, 19);
    web_fieldset_close(req);

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_gnss_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char body[500];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    app_config_lock();
    g_config.gnss_enable = web_form_get_bool(body, "gnssEn");
    g_config.gnss_channel = (int8_t)web_form_get_int(body, "gnssCH", g_config.gnss_channel);
    g_config.gnss_pps_gpio = (int8_t)web_form_get_int(body, "gnssPPS", g_config.gnss_pps_gpio);
    web_form_get(body, "gnssAT", g_config.gnss_at_command, sizeof(g_config.gnss_at_command));
    g_config.gnss_tcp_port = (uint16_t)web_form_get_int(body, "gnssTCPPort", g_config.gnss_tcp_port);
    web_form_get(body, "gnssTCPHost", g_config.gnss_tcp_host, sizeof(g_config.gnss_tcp_host));

    app_config_unlock();

    app_config_save();
    web_send_saved_redirect(req, "/gnss");
    return ESP_OK;
}
