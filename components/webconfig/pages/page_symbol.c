/**
 * @file page_symbol.c
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
 * @brief APRS symbol picker page: renders the full 94-character Primary ('/') and
 * Alternate ('\\') symbol tables so a symbol table/code pair can be chosen from
 * the web admin.
 */

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

// Full APRS symbol table (94 characters, '!' 0x21 .. '~' 0x7E), one row per
// character shared by both the Primary ('/') and Alternate ('\') tables, per
// the canonical reference: https://www.aprs.org/symbols/symbols.txt
// An empty string means "no symbol defined / reserved / TBD" for that slot.
struct sym_row {
    char ch;
    const char *primary;
    const char *alternate;
};

static const struct sym_row SYM_TABLE[] = {
    { '!', "Police, Sheriff", "Emergency" },
    { '"', "Reserved", "Reserved" },
    { '#', "Digipeater (white center)", "Numbered star (green)" },
    { '$', "Phone", "Bank or ATM (green box)" },
    { '%', "DX Cluster", "" },
    { '&', "HF Gateway", "Numbered diamond" },
    { '\'', "Small aircraft (SSID 7)", "Crash site" },
    { '(', "Cloudy", "Cloudy" },
    { ')', "Available (Mic moved to m)", "" },
    { '*', "Snowmobile", "Snow" },
    { '+', "Red Cross", "Church" },
    { ',', "Boy Scouts", "Girl Scouts" },
    { '-', "House QTH (VHF)", "House (HF)" },
    { '.', "X", "Ambiguous plot (big ?)" },
    { '/', "Dot", "" },
    { '0', "# circle (obsolete)", "Numbered circle (overlay)" },
    { '1', "TBD", "" },
    { '2', "TBD", "" },
    { '3', "TBD", "" },
    { '4', "TBD", "" },
    { '5', "TBD", "" },
    { '6', "TBD", "" },
    { '7', "TBD", "" },
    { '8', "TBD", "" },
    { '9', "TBD", "Gas station (blue pump)" },
    { ':', "Fire", "Hail" },
    { ';', "Campground", "Park / Picnic area" },
    { '<', "Motorcycle (SSID 10)", "Advisory" },
    { '=', "Railroad engine", "" },
    { '>', "Car (SSID 9)", "Numbered car" },
    { '?', "Server for files", "Info kiosk (blue box, ?)" },
    { '@', "HC future predict (dot)", "Hurricane / trop. storm" },
    { 'A', "Aid station", "Numbered box" },
    { 'B', "BBS", "Blowing snow" },
    { 'C', "Canoe", "Coast guard" },
    { 'D', "", "Drizzle" },
    { 'E', "Eyeball (eye catcher)", "Smoke" },
    { 'F', "", "Freezing rain" },
    { 'G', "Grid square (6 digit)", "Snow shower" },
    { 'H', "Hotel (blue bed symbol)", "Haze" },
    { 'I', "TCP/IP", "Rain shower" },
    { 'J', "", "Lightning" },
    { 'K', "School", "Kenwood HT" },
    { 'L', "Logged-on PC user", "Lighthouse" },
    { 'M', "MacAPRS", "" },
    { 'N', "NTS station", "Navigation buoy" },
    { 'O', "Balloon (SSID 11)", "" },
    { 'P', "Police", "Parking" },
    { 'Q', "TBD", "Earthquake" },
    { 'R', "Recreational vehicle (SSID 13)", "Restaurant" },
    { 'S', "Space shuttle", "Satellite / Pacsat" },
    { 'T', "SSTV", "Thunderstorm" },
    { 'U', "Bus (SSID 2)", "Sunny" },
    { 'V', "ATV", "VORTAC nav aid" },
    { 'W', "National WX service site", "Numbered NWS site" },
    { 'X', "Helicopter (SSID 6)", "Pharmacy (Rx)" },
    { 'Y', "Yacht / sailboat (SSID 5)", "" },
    { 'Z', "WinAPRS", "" },
    { '[', "Jogger", "Wall cloud" },
    { '\\', "Triangle (DF)", "" },
    { ']', "PBBS", "" },
    { '^', "Large aircraft", "Numbered aircraft" },
    { '_', "Weather station (blue)", "Numbered WX site (green digi)" },
    { '`', "Dish antenna", "Rain" },
    { 'a', "Ambulance (SSID 1)", "ARRL, ARES, etc." },
    { 'b', "Bicycle (SSID 4)", "Blowing dust/sand" },
    { 'c', "TBD", "Numbered civil defense (RACES)" },
    { 'd', "Fire dept (dual garage)", "DX spot by callsign" },
    { 'e', "Horse (equestrian)", "Sleet" },
    { 'f', "Fire truck (SSID 3)", "Funnel cloud" },
    { 'g', "Glider", "Gale flags" },
    { 'h', "Hospital", "HAM store" },
    { 'i', "IOTA (islands on the air)", "Indoor digipeater (w/ overlay)" },
    { 'j', "Jeep (SSID 12)", "Work zone (steam shovel)" },
    { 'k', "Truck (SSID 14)", "" },
    { 'l', "Logged-on laptop", "Area locations (box, circle, etc.)" },
    { 'm', "Mic-E repeater", "Value signpost (3-digit)" },
    { 'n', "Node", "Numbered triangle" },
    { 'o', "EOC (emergency ops center)", "Small circle" },
    { 'p', "Rover (puppy)", "Partly cloudy" },
    { 'q', "Grid sq. shown above 128m", "" },
    { 'r', "Antenna / repeater", "Restrooms" },
    { 's', "Ship / power boat (SSID 8)", "Numbered ship/boat (top view)" },
    { 't', "Truck stop", "Tornado" },
    { 'u', "Truck (18-wheeler)", "Numbered truck" },
    { 'v', "Van (SSID 15)", "Numbered van" },
    { 'w', "Water station", "Flooding" },
    { 'x', "xAPRS (Unix)", "" },
    { 'y', "Yagi @ QTH", "" },
    { 'z', "", "" },
    { '{', "", "Fog" },
    { '|', "Reserved (stream switch)", "" },
    { '}', "", "" },
    { '~', "Reserved (stream switch)", "" },
};
#define SYM_TABLE_COUNT (sizeof(SYM_TABLE) / sizeof(SYM_TABLE[0]))

// A curated shortlist of the most commonly used symbols, kept for the
// quick-pick reference at the top of the page.
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

// Renders the actual graphical APRS symbol icon for a table+code pair.
// There is no bitmap sprite sheet stored on-device, so the icon image is
// fetched from the same icon server already relied on elsewhere in this
// firmware (see fmtIcon() in page_common.c and the "sym" field produced by
// lastheard.c / trafficlog.c): aprs.dprns.com/symbols/icons/<code>-<table>.png,
// where <code> is the decimal ASCII value of the symbol character and
// <table> is 1 for Primary ('/') or 2 for Alternate ('\'). If the image
// can't be loaded (e.g. device has no internet access), onerror swaps in a
// colored, large-glyph fallback tile (green for Primary, blue for Alternate)
// showing the raw character so the row is still readable.
static void sym_icon_td(httpd_req_t *req, char table, char code) {
    char buf[600];
    const char *bg = (table == '\\') ? "#dbeafe" : "#dcfce7";
    const char *fg = (table == '\\') ? "#1d4ed8" : "#15803d";
    int table_num = (table == '\\') ? 2 : 1;
    int code_num = (int)(unsigned char)code;
    char esc = code;
    // HTML-escape the few symbol characters that are special in markup.
    const char *entity = NULL;
    if (esc == '<')
        entity = "&lt;";
    else if (esc == '>')
        entity = "&gt;";
    else if (esc == '&')
        entity = "&amp;";
    else if (esc == '"')
        entity = "&quot;";

    char glyph[8];
    if (entity)
        snprintf(glyph, sizeof(glyph), "%s", entity);
    else
        snprintf(glyph, sizeof(glyph), "%c", esc);

    snprintf(buf, sizeof(buf),
             "<td><span style='display:inline-flex;align-items:center;justify-content:center;width:34px;height:34px;"
             "border-radius:6px;background:%s;overflow:hidden'>"
             "<img src='http://aprs.dprns.com/symbols/icons/%d-%d.png' width=32 height=32 style='display:block' "
             "onerror=\"this.style.display='none';this.nextElementSibling.style.display='inline-flex'\">"
             "<span style='display:none;width:34px;height:34px;align-items:center;justify-content:center;"
             "color:%s;font-weight:700;font-size:1.1em;font-family:monospace'>%s</span>"
             "</span></td>",
             bg, code_num, table_num, fg, glyph);
    httpd_resp_sendstr_chunk(req, buf);
}

static void render_full_symbol_table(httpd_req_t *req, char table_char, const char *legend) {
    char hdr[256];
    snprintf(hdr, sizeof(hdr),
             "<fieldset><legend>%s</legend>"
             "<table><tr><th>%s</th><th>%s</th><th>%s</th></tr>",
             legend, TR_SYM_ICON, TR_SYM_CODE, TR_SYM_MEANING);
    httpd_resp_sendstr_chunk(req, hdr);

    for (size_t i = 0; i < SYM_TABLE_COUNT; i++) {
        const char *meaning = (table_char == '\\') ? SYM_TABLE[i].alternate : SYM_TABLE[i].primary;
        char code[8];
        char cch = SYM_TABLE[i].ch;
        const char *ent = NULL;
        if (cch == '<')
            ent = "&lt;";
        else if (cch == '>')
            ent = "&gt;";
        else if (cch == '&')
            ent = "&amp;";
        else if (cch == '"')
            ent = "&quot;";
        if (ent)
            snprintf(code, sizeof(code), "%c%s", table_char == '\\' ? '\\' : '/', ent);
        else
            snprintf(code, sizeof(code), "%c%c", table_char == '\\' ? '\\' : '/', cch);

        httpd_resp_sendstr_chunk(req, "<tr>");
        sym_icon_td(req, table_char, cch);
        char row[220];
        snprintf(row, sizeof(row), "<td><code>%s</code></td><td>%s</td></tr>", code, (meaning && meaning[0]) ? meaning : "&mdash;");
        httpd_resp_sendstr_chunk(req, row);
    }
    httpd_resp_sendstr_chunk(req, "</table></fieldset>");
}

esp_err_t page_symbol_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_APRS_SYMBOLS, NULL);

    httpd_resp_sendstr_chunk(req, "<p>" TR_SYM_INTRO "</p>");

    // Quick-pick shortlist (with icon tiles added).
    {
        char hdr[256];
        snprintf(hdr, sizeof(hdr), "<fieldset><legend>%s</legend><table><tr><th>%s</th><th>%s</th><th>%s</th></tr>", TR_SYM_QUICK_PICK,
                 TR_SYM_ICON, TR_SYM_CODE, TR_SYM_MEANING);
        httpd_resp_sendstr_chunk(req, hdr);
        for (size_t i = 0; i < SYM_COUNT; i++) {
            char table = COMMON_SYMBOLS[i].code[0];
            char code_ch = COMMON_SYMBOLS[i].code[1];
            httpd_resp_sendstr_chunk(req, "<tr>");
            sym_icon_td(req, table, code_ch);
            char row[220];
            snprintf(row, sizeof(row), "<td><code>%s</code></td><td>%s</td></tr>", COMMON_SYMBOLS[i].code, COMMON_SYMBOLS[i].label);
            httpd_resp_sendstr_chunk(req, row);
        }
        httpd_resp_sendstr_chunk(req, "</table></fieldset>");
    }

    // Full 94-symbol Primary and Alternate tables.
    render_full_symbol_table(req, '/', TR_SYM_PRIMARY_TABLE);
    render_full_symbol_table(req, '\\', TR_SYM_ALTERNATE_TABLE);

    // Currently configured symbols, now shown with icon tiles too.
    httpd_resp_sendstr_chunk(req, "<fieldset><legend>" TR_SYM_CURRENTLY_CONFIGURED "</legend><table>");
    {
        struct {
            const char *label;
            const char *sym;
        } cur[] = {
            { TR_F_IGATE, g_config.igate_symbol },
            { TR_DASH_DIGI_SHORT, g_config.digi_symbol },
            { TR_SYM_TRACKER_IDLE, g_config.trk_symbol },
            { TR_SYM_TRACKER_MOVE, g_config.trk_symmove },
            { TR_SYM_TRACKER_STOP, g_config.trk_symstop },
        };
        for (size_t i = 0; i < sizeof(cur) / sizeof(cur[0]); i++) {
            char t = cur[i].sym[0] ? cur[i].sym[0] : '/';
            char c = cur[i].sym[1] ? cur[i].sym[1] : ' ';
            httpd_resp_sendstr_chunk(req, "<tr>");
            sym_icon_td(req, t, c);
            char row[160];
            snprintf(row, sizeof(row), "<td><b>%s</b></td><td><code>%s</code></td></tr>", cur[i].label, cur[i].sym);
            httpd_resp_sendstr_chunk(req, row);
        }
    }
    httpd_resp_sendstr_chunk(req, "</table></fieldset>");

    web_send_footer(req);
    return ESP_OK;
}
