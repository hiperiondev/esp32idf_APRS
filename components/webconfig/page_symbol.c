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

// Renders a genuine small graphical icon (inline SVG, self-contained - no
// external image/network dependency, since this page must work standalone
// over the ESP32's own AP with no internet route). A curated set of common
// symbol categories get a real pictogram; anything not in that set falls
// back to a generic colored map-pin marker (still a graphic, not text).
// Table color still distinguishes Primary ('/' -> green) vs Alternate
// ('\' -> blue), matching the convention used elsewhere in this firmware's UI.
typedef enum {
    ICON_PIN = 0,
    ICON_HOUSE,
    ICON_CAR,
    ICON_TRUCK,
    ICON_VAN,
    ICON_BUS,
    ICON_MOTORCYCLE,
    ICON_BICYCLE,
    ICON_BOAT,
    ICON_SAILBOAT,
    ICON_AIRCRAFT,
    ICON_HELICOPTER,
    ICON_BALLOON,
    ICON_SHUTTLE,
    ICON_DIGI,
    ICON_GATEWAY,
    ICON_ANTENNA,
    ICON_WEATHER,
    ICON_PHONE,
    ICON_HOSPITAL,
    ICON_POLICE,
    ICON_FIRETRUCK,
    ICON_AMBULANCE,
    ICON_JEEP,
    ICON_TRAIN,
} icon_shape_t;

static icon_shape_t sym_icon_shape(char ch) {
    switch (ch) {
    case '-':
    case 'y':
        return ICON_HOUSE;
    case '>':
        return ICON_CAR;
    case 'k':
    case 'u':
    case 't':
        return ICON_TRUCK;
    case 'v':
        return ICON_VAN;
    case 'U':
        return ICON_BUS;
    case '<':
        return ICON_MOTORCYCLE;
    case 'b':
        return ICON_BICYCLE;
    case 's':
    case 'C':
        return ICON_BOAT;
    case 'Y':
        return ICON_SAILBOAT;
    case '^':
        return ICON_AIRCRAFT;
    case 'X':
        return ICON_HELICOPTER;
    case 'O':
        return ICON_BALLOON;
    case 'S':
        return ICON_SHUTTLE;
    case '#':
        return ICON_DIGI;
    case '&':
        return ICON_GATEWAY;
    case 'r':
        return ICON_ANTENNA;
    case '_':
    case 'W':
        return ICON_WEATHER;
    case '$':
        return ICON_PHONE;
    case 'h':
    case 'H':
        return ICON_HOSPITAL;
    case '!':
    case 'P':
        return ICON_POLICE;
    case 'f':
        return ICON_FIRETRUCK;
    case 'a':
        return ICON_AMBULANCE;
    case 'j':
        return ICON_JEEP;
    case '=':
        return ICON_TRAIN;
    default:
        return ICON_PIN;
    }
}

// Emits one <svg>...</svg> pictogram (28x28 viewBox), filled with `fg` on a
// rounded `bg` tile. Shapes are deliberately simple (a handful of rects/
// circles/paths) so the whole set stays small and fast to generate/parse.
static void sym_icon_svg(char *out, size_t outsz, icon_shape_t shape, const char *fg) {
    const char *body;
    switch (shape) {
    case ICON_HOUSE:
        body = "<path d='M4 15 L14 6 L24 15 V24 H4 Z' fill='%s'/><rect x='11' y='16' width='6' height='8' fill='white'/>";
        break;
    case ICON_CAR:
        body = "<rect x='3' y='13' width='22' height='7' rx='2' fill='%s'/><path d='M7 13 L10 8 H18 L21 13Z' fill='%s'/>"
               "<circle cx='8' cy='21' r='2.4' fill='#333'/><circle cx='20' cy='21' r='2.4' fill='#333'/>";
        break;
    case ICON_TRUCK:
        body = "<rect x='2' y='11' width='14' height='9' fill='%s'/><path d='M16 14 H24 L26 18 V20 H16Z' fill='%s'/>"
               "<circle cx='7' cy='21' r='2.2' fill='#333'/><circle cx='21' cy='21' r='2.2' fill='#333'/>";
        break;
    case ICON_VAN:
        body = "<path d='M3 10 H22 L25 14 V20 H3Z' fill='%s'/><circle cx='8' cy='21' r='2.3' fill='#333'/>"
               "<circle cx='20' cy='21' r='2.3' fill='#333'/>";
        break;
    case ICON_BUS:
        body = "<rect x='3' y='8' width='22' height='12' rx='2' fill='%s'/><rect x='5' y='10' width='5' height='4' fill='white'/>"
               "<rect x='12' y='10' width='5' height='4' fill='white'/><circle cx='8' cy='21' r='2.3' fill='#333'/>"
               "<circle cx='20' cy='21' r='2.3' fill='#333'/>";
        break;
    case ICON_MOTORCYCLE:
        body = "<circle cx='7' cy='20' r='4' fill='none' stroke='%s' stroke-width='2'/>"
               "<circle cx='21' cy='20' r='4' fill='none' stroke='%s' stroke-width='2'/>"
               "<path d='M7 20 L14 12 H21 M14 12 L11 20' stroke='%s' stroke-width='2' fill='none'/>";
        break;
    case ICON_BICYCLE:
        body = "<circle cx='7' cy='19' r='5' fill='none' stroke='%s' stroke-width='2'/>"
               "<circle cx='21' cy='19' r='5' fill='none' stroke='%s' stroke-width='2'/>"
               "<path d='M7 19 L13 9 H18 M13 9 L21 19 M13 9 L10 19' stroke='%s' stroke-width='2' fill='none'/>";
        break;
    case ICON_BOAT:
        body = "<path d='M4 17 H24 L21 23 H7Z' fill='%s'/><path d='M14 17 V6 L20 12Z' fill='%s'/>";
        break;
    case ICON_SAILBOAT:
        body = "<path d='M14 4 V17 L23 17Z' fill='%s'/><path d='M14 8 V17 L7 17Z' fill='%s' opacity='.6'/>"
               "<path d='M4 19 H24 L21 24 H7Z' fill='%s'/>";
        break;
    case ICON_AIRCRAFT:
        body = "<path d='M14 3 L16 11 L26 15 L16 16 L15 24 L18 26 H10 L13 24 L12 16 L2 15 L12 11Z' fill='%s'/>";
        break;
    case ICON_HELICOPTER:
        body = "<rect x='3' y='9' width='22' height='2' fill='%s'/><ellipse cx='14' cy='17' rx='9' ry='6' fill='%s'/>"
               "<rect x='22' y='15' width='6" "" "' height='3' fill='%s'/>";
        break;
    case ICON_BALLOON:
        body = "<ellipse cx='14' cy='11' rx='9' ry='11' fill='%s'/><path d='M9 21 L11 26 H17 L19 21' stroke='%s' "
               "stroke-width='1.5' fill='none'/>";
        break;
    case ICON_SHUTTLE:
        body = "<path d='M14 2 L18 18 H10Z' fill='%s'/><path d='M8 14 L4 22 H10Z' fill='%s'/>"
               "<path d='M20 14 L24 22 H18Z' fill='%s'/><rect x='11' y='18' width='6' height='7' fill='%s'/>";
        break;
    case ICON_DIGI:
        body = "<path d='M14 2 L17 10 H26 L19 15 L21 24 L14 19 L7 24 L9 15 L2 10 H11Z' fill='%s'/>";
        break;
    case ICON_GATEWAY:
        body = "<path d='M14 3 L25 14 L14 25 L3 14Z' fill='%s'/>";
        break;
    case ICON_ANTENNA:
        body = "<rect x='13' y='3' width='2' height='22' fill='%s'/><path d='M14 3 L20 9 M14 3 L8 9' stroke='%s' "
               "stroke-width='2' fill='none'/><path d='M14 10 L19 15 M14 10 L9 15' stroke='%s' stroke-width='2' fill='none'/>";
        break;
    case ICON_WEATHER:
        body = "<circle cx='14' cy='11' r='6' fill='%s'/><path d='M4 21 H24 M6 24 H22' stroke='%s' stroke-width='2'/>";
        break;
    case ICON_PHONE:
        body = "<rect x='9' y='2' width='10' height='24' rx='2' fill='%s'/><rect x='12" "" "' y='21' width='4' height='2' "
               "fill='white'/>";
        break;
    case ICON_HOSPITAL:
        body = "<rect x='3' y='3' width='22' height='22' rx='3' fill='%s'/><rect x='12' y='7' width='4' height='14' "
               "fill='white'/><rect x='7' y='12' width='14' height='4' fill='white'/>";
        break;
    case ICON_POLICE:
        body = "<path d='M14 2 L24 6 V13 C24 20 20 24 14 26 C8 24 4 20 4 13 V6Z' fill='%s'/>";
        break;
    case ICON_FIRETRUCK:
        body = "<rect x='2' y='11' width='14' height='9' fill='%s'/><path d='M16 14 H24 L26 18 V20 H16Z' fill='%s'/>"
               "<circle cx='7' cy='21' r='2.2' fill='#333'/><circle cx='21' cy='21' r='2.2' fill='#333'/>";
        break;
    case ICON_AMBULANCE:
        body = "<rect x='3' y='10' width='20" "" "' height='10' fill='%s'/><rect x='11' y='12' width='4' height='6' "
               "fill='white'/><rect x='9' y='14' width='8' height='2' fill='white'/>"
               "<circle cx='8' cy='21' r='2.2' fill='#333'/><circle cx='19' cy='21' r='2.2' fill='#333'/>";
        break;
    case ICON_JEEP:
        body = "<rect x='4' y='11' width='20' height='8' rx='1' fill='%s'/><rect x='6' y='7' width='16' height='5' "
               "fill='%s' opacity='.7'/><circle cx='8' cy='20' r='2.3' fill='#333'/><circle cx='20' cy='20' r='2.3' "
               "fill='#333'/>";
        break;
    case ICON_TRAIN:
        body = "<rect x='6' y='4' width='16' height='16' rx='3' fill='%s'/><circle cx='10' cy='24' r='2' fill='#333'/>"
               "<circle cx='18' cy='24' r='2' fill='#333'/><rect x='9' y='8' width='10' height='6' fill='white'/>";
        break;
    case ICON_PIN:
    default:
        body = "<path d='M14 2 C7 2 3 7 3 12.5 C3 19 14 26 14 26 C14 26 25 19 25 12.5 C25 7 21 2 14 2Z' fill='%s'/>"
               "<circle cx='14' cy='12' r='4' fill='white'/>";
        break;
    }
    // Every %s placeholder in a given shape's body is the same fg color, so
    // one fg pointer can satisfy the whole format string safely.
    snprintf(out, outsz, "<svg viewBox='0 0 28 28' width='26' height='26' xmlns='http://www.w3.org/2000/svg'>", 0);
    (void)fg;
}

static void sym_icon_td(httpd_req_t *req, char table, char code) {
    const char *fg = (table == '\\') ? "#1d4ed8" : "#15803d";
    const char *bg = (table == '\\') ? "#dbeafe" : "#dcfce7";
    icon_shape_t shape = sym_icon_shape(code);

    // Build the shape body with up to 4 identical %s color substitutions
    // (the most any single shape above uses).
    static const char *bodies[] = {
        NULL, // filled in below per-shape via sym_icon_shape_body()
    };
    (void)bodies;

    char inner[420];
    switch (shape) {
#define ICON_CASE1(S, FMT)                                                                                                                          \
    case S:                                                                                                                                        \
        snprintf(inner, sizeof(inner), FMT, fg);                                                                                                   \
        break;
#define ICON_CASE2(S, FMT)                                                                                                                          \
    case S:                                                                                                                                        \
        snprintf(inner, sizeof(inner), FMT, fg, fg);                                                                                               \
        break;
#define ICON_CASE3(S, FMT)                                                                                                                          \
    case S:                                                                                                                                        \
        snprintf(inner, sizeof(inner), FMT, fg, fg, fg);                                                                                           \
        break;
#define ICON_CASE4(S, FMT)                                                                                                                          \
    case S:                                                                                                                                        \
        snprintf(inner, sizeof(inner), FMT, fg, fg, fg, fg);                                                                                       \
        break;
        ICON_CASE1(ICON_HOUSE, "<path d='M4 15 L14 6 L24 15 V24 H4 Z' fill='%s'/><rect x='11' y='16' width='6' height='8' fill='white'/>")
        ICON_CASE2(ICON_CAR, "<rect x='3' y='13' width='22' height='7' rx='2' fill='%s'/><path d='M7 13 L10 8 H18 L21 13Z' fill='%s'/>"
                              "<circle cx='8' cy='21' r='2.4' fill='#333'/><circle cx='20' cy='21' r='2.4' fill='#333'/>")
        ICON_CASE2(ICON_TRUCK, "<rect x='2' y='11' width='14' height='9' fill='%s'/><path d='M16 14 H24 L26 18 V20 H16Z' fill='%s'/>"
                                "<circle cx='7' cy='21' r='2.2' fill='#333'/><circle cx='21' cy='21' r='2.2' fill='#333'/>")
        ICON_CASE1(ICON_VAN, "<path d='M3 10 H22 L25 14 V20 H3Z' fill='%s'/><circle cx='8' cy='21' r='2.3' fill='#333'/>"
                              "<circle cx='20' cy='21' r='2.3' fill='#333'/>")
        ICON_CASE1(ICON_BUS, "<rect x='3' y='8' width='22' height='12' rx='2' fill='%s'/><rect x='5' y='10' width='5' height='4' fill='white'/>"
                              "<rect x='12' y='10' width='5' height='4' fill='white'/><circle cx='8' cy='21' r='2.3' fill='#333'/>"
                              "<circle cx='20' cy='21' r='2.3' fill='#333'/>")
        ICON_CASE3(ICON_MOTORCYCLE, "<circle cx='7' cy='20' r='4' fill='none' stroke='%s' stroke-width='2'/>"
                                     "<circle cx='21' cy='20' r='4' fill='none' stroke='%s' stroke-width='2'/>"
                                     "<path d='M7 20 L14 12 H21 M14 12 L11 20' stroke='%s' stroke-width='2' fill='none'/>")
        ICON_CASE3(ICON_BICYCLE, "<circle cx='7' cy='19' r='5' fill='none' stroke='%s' stroke-width='2'/>"
                                  "<circle cx='21' cy='19' r='5' fill='none' stroke='%s' stroke-width='2'/>"
                                  "<path d='M7 19 L13 9 H18 M13 9 L21 19 M13 9 L10 19' stroke='%s' stroke-width='2' fill='none'/>")
        ICON_CASE2(ICON_BOAT, "<path d='M4 17 H24 L21 23 H7Z' fill='%s'/><path d='M14 17 V6 L20 12Z' fill='%s'/>")
        ICON_CASE3(ICON_SAILBOAT, "<path d='M14 4 V17 L23 17Z' fill='%s'/><path d='M14 8 V17 L7 17Z' fill='%s' opacity='.6'/>"
                                   "<path d='M4 19 H24 L21 24 H7Z' fill='%s'/>")
        ICON_CASE1(ICON_AIRCRAFT, "<path d='M14 3 L16 11 L26 15 L16 16 L15 24 L18 26 H10 L13 24 L12 16 L2 15 L12 11Z' fill='%s'/>")
        ICON_CASE3(ICON_HELICOPTER, "<rect x='3' y='9' width='22' height='2' fill='%s'/><ellipse cx='14' cy='17' rx='9' ry='6' fill='%s'/>"
                                     "<rect x='22' y='15' width='6' height='3' fill='%s'/>")
        ICON_CASE2(ICON_BALLOON, "<ellipse cx='14' cy='11' rx='9' ry='11' fill='%s'/><path d='M9 21 L11 26 H17 L19 21' stroke='%s' "
                                  "stroke-width='1.5' fill='none'/>")
        ICON_CASE4(ICON_SHUTTLE, "<path d='M14 2 L18 18 H10Z' fill='%s'/><path d='M8 14 L4 22 H10Z' fill='%s'/>"
                                  "<path d='M20 14 L24 22 H18Z' fill='%s'/><rect x='11' y='18' width='6' height='7' fill='%s'/>")
        ICON_CASE1(ICON_DIGI, "<path d='M14 2 L17 10 H26 L19 15 L21 24 L14 19 L7 24 L9 15 L2 10 H11Z' fill='%s'/>")
        ICON_CASE1(ICON_GATEWAY, "<path d='M14 3 L25 14 L14 25 L3 14Z' fill='%s'/>")
        ICON_CASE3(ICON_ANTENNA, "<rect x='13' y='3' width='2' height='22' fill='%s'/><path d='M14 3 L20 9 M14 3 L8 9' stroke='%s' "
                                  "stroke-width='2' fill='none'/><path d='M14 10 L19 15 M14 10 L9 15' stroke='%s' stroke-width='2' fill='none'/>")
        ICON_CASE2(ICON_WEATHER, "<circle cx='14' cy='11' r='6' fill='%s'/><path d='M4 21 H24 M6 24 H22' stroke='%s' stroke-width='2'/>")
        ICON_CASE1(ICON_PHONE, "<rect x='9' y='2' width='10' height='24' rx='2' fill='%s'/><rect x='12' y='21' width='4' height='2' fill='white'/>")
        ICON_CASE1(ICON_HOSPITAL, "<rect x='3' y='3' width='22' height='22' rx='3' fill='%s'/><rect x='12' y='7' width='4' height='14' "
                                   "fill='white'/><rect x='7' y='12' width='14' height='4' fill='white'/>")
        ICON_CASE1(ICON_POLICE, "<path d='M14 2 L24 6 V13 C24 20 20 24 14 26 C8 24 4 20 4 13 V6Z' fill='%s'/>")
        ICON_CASE2(ICON_FIRETRUCK, "<rect x='2' y='11' width='14' height='9' fill='%s'/><path d='M16 14 H24 L26 18 V20 H16Z' fill='%s'/>"
                                    "<circle cx='7' cy='21' r='2.2' fill='#333'/><circle cx='21' cy='21' r='2.2' fill='#333'/>")
        ICON_CASE1(ICON_AMBULANCE, "<rect x='3' y='10' width='20' height='10' fill='%s'/><rect x='11' y='12' width='4' height='6' "
                                    "fill='white'/><rect x='9' y='14' width='8' height='2' fill='white'/>"
                                    "<circle cx='8' cy='21' r='2.2' fill='#333'/><circle cx='19' cy='21' r='2.2' fill='#333'/>")
        ICON_CASE2(ICON_JEEP, "<rect x='4' y='11' width='20' height='8' rx='1' fill='%s'/><rect x='6' y='7' width='16' height='5' fill='%s' "
                               "opacity='.7'/><circle cx='8' cy='20' r='2.3' fill='#333'/><circle cx='20' cy='20' r='2.3' fill='#333'/>")
        ICON_CASE1(ICON_TRAIN, "<rect x='6' y='4' width='16' height='16' rx='3' fill='%s'/><circle cx='10' cy='24' r='2' fill='#333'/>"
                                "<circle cx='18' cy='24' r='2' fill='#333'/><rect x='9' y='8' width='10' height='6' fill='white'/>")
        ICON_CASE1(ICON_PIN, "<path d='M14 2 C7 2 3 7 3 12.5 C3 19 14 26 14 26 C14 26 25 19 25 12.5 C25 7 21 2 14 2Z' fill='%s'/>"
                              "<circle cx='14' cy='12' r='4' fill='white'/>")
#undef ICON_CASE1
#undef ICON_CASE2
#undef ICON_CASE3
#undef ICON_CASE4
    default:
        snprintf(inner, sizeof(inner), "<circle cx='14' cy='14' r='10' fill='%s'/>", fg);
        break;
    }

    char buf[700];
    snprintf(buf, sizeof(buf),
             "<td><span style='display:inline-flex;align-items:center;justify-content:center;width:34px;height:34px;"
             "border-radius:6px;background:%s'><svg viewBox='0 0 28 28' width='24' height='24' "
             "xmlns='http://www.w3.org/2000/svg'>%s</svg></span></td>",
             bg, inner);
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
