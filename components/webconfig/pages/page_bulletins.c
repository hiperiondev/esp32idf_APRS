// @file page_bulletins.c
//
// @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
// @date 2026
// @copyright GNU General Public License v3
// @see https://github.com/hiperiondev/esp32idf_APRS
//
// @note
// This is based on other projects:
//     VP-Digi: https://github.com/sq8vps/vp-digi
//     ESP32APRS: https://github.com/nakhonthai/ESP32APRS_Audio
//     LibAPRS: https://github.com/markqvist/LibAPRS
//
//     please contact their authors for more information.
//
// @brief Web admin "Bulletins" page: edits the BULLETIN_COUNT APRS bulletins.
// Each bulletin has enable / Send via RF / Send via Internet toggles, an
// addressee identifier and group name, a length-limited message, and an
// "expire after N hours" window that auto-disables the bulletin once it
// elapses.
//
// The identifier and group together select which of the three APRS101
// chapter 14 addressee forms goes on the air - general bulletin ("BLN1"),
// group bulletin ("BLN1WX") or announcement ("BLNQ"). Both are normalized at
// transmit time by bulletins_build_addressee(), so anything typed here that
// the addressee field cannot carry is dropped rather than transmitted.
//
// Bulletins live in their own LittleFS file (/storage/bulletins.json), NOT in
// g_config - see bulletins.h. This page therefore loads/saves them through the
// bulletins_* API rather than touching g_config or app_config_save().

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bulletins.h"
#include "esp_log.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

static const char *TAG = "page_bulletins";

esp_err_t page_bulletins_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    bulletins_t set;
    bulletins_load(&set);
    // Reflect expiry in the UI: a bulletin whose deadline has passed shows up
    // unchecked (and the change is persisted so it stays that way).
    if (bulletins_apply_expiry(&set)) {
        if (!bulletins_save(&set))
            ESP_LOGW(TAG, "expired bulletins could not be written to flash");
    }

    web_send_header(req, TR_F_BULLETINS, "bulletins");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/bulletins'>");

    httpd_resp_sendstr_chunk(req, "<div id='blnWrap'>");
    for (int i = 0; i < BULLETIN_COUNT; i++) {
        const bulletin_t *b = &set.item[i];

        char legend[24];
        snprintf(legend, sizeof(legend), TR_F_BULLETIN_FMT, i + 1);

        // Collapsible accordion card: only the first bulletin starts open;
        // accordionClick() (injected below) closes any other open card
        // whenever one is clicked, so at most one bulletin is expanded at a
        // time. Reuses the shared .achan* accordion styling.
        char head[256];
        snprintf(head, sizeof(head),
                 "<div class='achan%s' id='bln%d'>"
                 "<div class='achan-head' onclick='accordionClick(\"bln\",%d,%d)'>"
                 "<span class='achan-name'>%s</span>"
                 "<span class='achan-caret'>&#9654;</span>"
                 "</div><div class='achan-body'>",
                 (i == 0) ? " open" : "", i, i, BULLETIN_COUNT, legend);
        httpd_resp_sendstr_chunk(req, head);

        char name[20];
        snprintf(name, sizeof(name), "bEn%d", i + 1);
        web_field_checkbox(req, TR_F_ENABLE, name, b->enable);
        snprintf(name, sizeof(name), "bRf%d", i + 1);
        web_field_checkbox(req, TR_F_SEND_VIA_RF, name, b->send_rf);
        snprintf(name, sizeof(name), "bInet%d", i + 1);
        web_field_checkbox(req, TR_F_SEND_VIA_INTERNET, name, b->send_inet);

        // The identifier is one character wide on air. An empty field is
        // legal and means "use this slot's own digit", which is what a
        // bulletin saved before this field existed does.
        char ident[2] = { b->ident, 0 };
        snprintf(name, sizeof(name), "bId%d", i + 1);
        web_field_text(req, TR_F_BULLETIN_ID, name, ident, 1);

        snprintf(name, sizeof(name), "bGrp%d", i + 1);
        web_field_text(req, TR_F_BULLETIN_GROUP, name, b->group, BULLETIN_GROUP_MAX);

        // web_field_text() HTML-escapes value internally, so the free-form
        // bulletin text is passed straight through here.
        snprintf(name, sizeof(name), "bMsg%d", i + 1);
        web_field_text(req, TR_F_BULLETIN_MSG, name, b->text, BULLETIN_TEXT_MAX);

        snprintf(name, sizeof(name), "bInt%d", i + 1);
        web_field_int(req, TR_F_BEACON_INTERVAL_S, name, (long)b->interval_s, WEB_RANGE_INTERVAL_S_MIN, WEB_RANGE_INTERVAL_LONG_S_MAX);

        snprintf(name, sizeof(name), "bExp%d", i + 1);
        web_field_int(req, TR_F_BULLETIN_EXPIRE, name, (long)b->expire_hours, 0, 8760);

        httpd_resp_sendstr_chunk(req, "</div></div>");
    }
    httpd_resp_sendstr_chunk(req, "</div>");

    // Generic single-open accordion helper, shared verbatim with the Objects
    // and Items page: closes every card of the given id-prefix except the
    // one just clicked (toggling it), so only one is expanded at a time.
    httpd_resp_sendstr_chunk(req, "<script>"
                                  "function accordionClick(p,i,n){"
                                  "for(var k=0;k<n;k++){"
                                  "var c=document.getElementById(p+k);"
                                  "if(!c)continue;"
                                  "if(k===i)c.classList.toggle('open');"
                                  "else c.classList.remove('open');"
                                  "}"
                                  "}"
                                  "</script>");

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

        snprintf(name, sizeof(name), "bId%d", i + 1);
        char ident[4];
        ident[0] = 0;
        web_form_get(body, name, ident, sizeof(ident));
        b->ident = ident[0]; // 0 (empty field) means "this slot's default digit"

        snprintf(name, sizeof(name), "bGrp%d", i + 1);
        char group[BULLETIN_GROUP_MAX + 1];
        group[0] = 0;
        web_form_get(body, name, group, sizeof(group));
        strncpy(b->group, group, BULLETIN_GROUP_MAX);
        b->group[BULLETIN_GROUP_MAX] = 0;

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

    // The page rendered next is built from the live settings, so the save
    // result is what decides whether the operator is told this reached flash.
    bool ok = bulletins_save(&set);
    if (!ok)
        ESP_LOGE(TAG, "bulletins could not be written to flash");

    web_send_save_result(req, ok, "/bulletins");
    return ESP_OK;
}
