#include <stdio.h>

#include "app_config.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

// A representative subset of common APRS primary-table symbols (table char '/').
// This is a reference/quick-pick list, not the full 190-symbol table.
struct sym_entry {
    const char *code;
    const char *label;
};
static const struct sym_entry COMMON_SYMBOLS[] = {
    { "/-", TR_SYM_HOUSE_HF },   { "/>", TR_SYM_CAR },      { "/<", TR_SYM_MOTORCYCLE },      { "/b", TR_SYM_BICYCLE },  { "/k", TR_SYM_TRUCK },
    { "/v", TR_SYM_VAN },        { "/j", TR_SYM_JEEP },     { "/f", TR_SYM_FIRE_TRUCK },      { "/p", TR_SYM_POLICE },   { "/-", TR_SYM_HOUSE },
    { "/#", TR_SYM_DIGIPEATER }, { "/&", TR_SYM_GATEWAY },  { "/_", TR_SYM_WEATHER_STATION }, { "/O", TR_SYM_BALLOON },  { "/S", TR_SYM_SPACE_SHUTTLE },
    { "/Y", TR_SYM_SAILBOAT },   { "/W", TR_SYM_NWS_SITE }, { "/I", TR_SYM_TCP_IP },          { "\\>", TR_SYM_CAR_ALT }, { "\\W", TR_SYM_WX_STATION_ALT },
};
#define SYM_COUNT (sizeof(COMMON_SYMBOLS) / sizeof(COMMON_SYMBOLS[0]))

esp_err_t page_symbol_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_APRS_SYMBOLS, NULL);

    httpd_resp_sendstr_chunk(req, "<p>" TR_SYM_INTRO "</p>"
                                  "<table><tr><th>" TR_SYM_CODE "</th><th>" TR_SYM_MEANING "</th></tr>");
    for (size_t i = 0; i < SYM_COUNT; i++) {
        char row[160];
        snprintf(row, sizeof(row), "<tr><td><code>%s</code></td><td>%s</td></tr>", COMMON_SYMBOLS[i].code, COMMON_SYMBOLS[i].label);
        httpd_resp_sendstr_chunk(req, row);
    }
    httpd_resp_sendstr_chunk(req, "</table>");

    char cur[300];
    snprintf(cur, sizeof(cur),
             "<fieldset><legend>" TR_SYM_CURRENTLY_CONFIGURED "</legend>"
             "<p><b>" TR_F_IGATE ":</b> %s &nbsp; <b>" TR_DASH_DIGI_SHORT ":</b> %s &nbsp; "
             "<b>" TR_SYM_TRACKER_IDLE_MOVE_STOP "</b> %s / %s / %s</p></fieldset>",
             g_config.igate_symbol, g_config.digi_symbol, g_config.trk_symbol, g_config.trk_symmove, g_config.trk_symstop);
    httpd_resp_sendstr_chunk(req, cur);

    web_send_footer(req);
    return ESP_OK;
}
