/**
 * @file page_bulletins.c
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
 * @brief Web admin "Bulletins" page: edits the BULLETIN_COUNT APRS bulletins
 * (BLN1..BLN5). Each bulletin has enable / Send via RF / Send via Internet
 * toggles, a length-limited message, and an "expire after N hours" window that
 * auto-disables the bulletin once it elapses.
 *
 * Bulletins live in their own LittleFS file (/storage/bulletins.json), NOT in
 * g_config - see bulletins.h. This page therefore loads/saves them through the
 * bulletins_* API rather than touching g_config or app_config_save().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bulletins.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

esp_err_t page_bulletins_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    bulletins_t set;
    bulletins_load(&set);
    // Reflect expiry in the UI: a bulletin whose deadline has passed shows up
    // unchecked (and the change is persisted so it stays that way).
    if (bulletins_apply_expiry(&set))
        bulletins_save(&set);

    web_send_header(req, TR_F_BULLETINS, "bulletins");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/bulletins'>");

    for (int i = 0; i < BULLETIN_COUNT; i++) {
        const bulletin_t *b = &set.item[i];

        char legend[24];
        snprintf(legend, sizeof(legend), TR_F_BULLETIN_FMT, i + 1);
        web_fieldset_open(req, legend);

        char name[20];
        snprintf(name, sizeof(name), "bEn%d", i + 1);
        web_field_checkbox(req, TR_F_ENABLE, name, b->enable);
        snprintf(name, sizeof(name), "bRf%d", i + 1);
        web_field_checkbox(req, TR_F_SEND_VIA_RF, name, b->send_rf);
        snprintf(name, sizeof(name), "bInet%d", i + 1);
        web_field_checkbox(req, TR_F_SEND_VIA_INTERNET, name, b->send_inet);

        // HTML-escape the free-form text so a quote/angle bracket in the
        // bulletin can't break out of the value='...' attribute.
        char esc[BULLETIN_TEXT_MAX * 6 + 1];
        web_html_attr_escape(b->text, esc, sizeof(esc));
        snprintf(name, sizeof(name), "bMsg%d", i + 1);
        web_field_text(req, TR_F_BULLETIN_MSG, name, esc, BULLETIN_TEXT_MAX);

        snprintf(name, sizeof(name), "bInt%d", i + 1);
        web_field_int(req, TR_F_BEACON_INTERVAL_S, name, (long)b->interval_s);

        snprintf(name, sizeof(name), "bExp%d", i + 1);
        web_field_int(req, TR_F_BULLETIN_EXPIRE, name, (long)b->expire_hours);

        web_fieldset_close(req);
    }

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_bulletins_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    char *body = malloc(WEBCONFIG_POST_BUF_BULLETINS);
    if (!body) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    if (web_read_body(req, body, WEBCONFIG_POST_BUF_BULLETINS) < 0) {
        free(body);
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    bulletins_t set;
    // Start from the stored set so any field not present in the form keeps its
    // value; the form overwrites everything it does carry.
    bulletins_load(&set);

    for (int i = 0; i < BULLETIN_COUNT; i++) {
        bulletin_t *b = &set.item[i];
        char name[20];

        snprintf(name, sizeof(name), "bEn%d", i + 1);
        b->enable = web_form_get_bool(body, name);
        snprintf(name, sizeof(name), "bRf%d", i + 1);
        b->send_rf = web_form_get_bool(body, name);
        snprintf(name, sizeof(name), "bInet%d", i + 1);
        b->send_inet = web_form_get_bool(body, name);

        snprintf(name, sizeof(name), "bMsg%d", i + 1);
        char text[BULLETIN_TEXT_MAX + 1];
        text[0] = 0;
        web_form_get(body, name, text, sizeof(text)); // URL-decoded, clamped to buffer
        strncpy(b->text, text, BULLETIN_TEXT_MAX);
        b->text[BULLETIN_TEXT_MAX] = 0;

        snprintf(name, sizeof(name), "bInt%d", i + 1);
        int interval = web_form_get_int(body, name, (int)b->interval_s);
        if (interval < 0)
            interval = 0;
        b->interval_s = (uint32_t)interval;

        snprintf(name, sizeof(name), "bExp%d", i + 1);
        int hours = web_form_get_int(body, name, (int)b->expire_hours);
        if (hours < 0)
            hours = 0;
        b->expire_hours = (uint32_t)hours;
    }

    free(body);

    // (Re)arm expiry deadlines from the moment of save, then persist to
    // LittleFS. Saving is what restarts each bulletin's expiry window.
    bulletins_arm_expiry(&set);
    bulletins_save(&set);

    web_send_saved_redirect(req, "/bulletins");
    return ESP_OK;
}
