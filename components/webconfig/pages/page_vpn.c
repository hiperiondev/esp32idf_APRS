/**
 * @file page_vpn.c
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
 * @brief Web admin "VPN (WireGuard)" page: renders and saves the WireGuard tunnel
 * configuration in g_config.
 */

#include "app_config.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

esp_err_t page_vpn_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_VPN_WIREGUARD, "vpn");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/vpn'>");

    web_fieldset_open(req, TR_F_WIREGUARD_TUNNEL);
    web_field_checkbox(req, TR_F_ENABLE_VPN, "vpnEn", g_config.vpn);
    web_field_int(req, TR_F_LOCAL_UDP_PORT, "vpnPort", g_config.wg_port);
    web_field_text(req, TR_F_LOCAL_TUNNEL_ADDRESS, "vpnLocal", g_config.wg_local_address, 15);
    web_field_text(req, TR_F_NETMASK, "vpnNetmark", g_config.wg_netmask_address, 15);
    web_field_text(req, TR_F_GATEWAY, "vpnGW", g_config.wg_gw_address, 15);
    web_field_text(req, TR_F_PEER_ENDPOINT_HOST_PORT, "vpnPeer", g_config.wg_peer_address, 31);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_F_KEYS);
    web_field_password(req, TR_F_PRIVATE_KEY, "vpnPriKey", g_config.wg_private_key, 44);
    web_field_text(req, TR_F_PEER_PUBLIC_KEY, "vpnPubKey", g_config.wg_public_key, 44);
    web_fieldset_close(req);

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_vpn_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char body[900];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    g_config.vpn = web_form_get_bool(body, "vpnEn");
    g_config.wg_port = (uint16_t)web_form_get_int(body, "vpnPort", g_config.wg_port);
    web_form_get(body, "vpnLocal", g_config.wg_local_address, sizeof(g_config.wg_local_address));
    web_form_get(body, "vpnNetmark", g_config.wg_netmask_address, sizeof(g_config.wg_netmask_address));
    web_form_get(body, "vpnGW", g_config.wg_gw_address, sizeof(g_config.wg_gw_address));
    web_form_get(body, "vpnPeer", g_config.wg_peer_address, sizeof(g_config.wg_peer_address));
    web_form_get(body, "vpnPriKey", g_config.wg_private_key, sizeof(g_config.wg_private_key));
    web_form_get(body, "vpnPubKey", g_config.wg_public_key, sizeof(g_config.wg_public_key));

    app_config_save();
    web_send_saved_redirect(req, "/vpn");
    return ESP_OK;
}
