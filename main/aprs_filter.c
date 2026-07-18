/**
 * @file aprs_filter.c
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
 * @brief Maps an APRS information field onto one IGATE_FILT_* bit, so the
 * rf2inetFilter / inet2rfFilter bitmasks edited on the web IGATE Filter page
 * can actually gate traffic.
 */

#include <ctype.h>
#include <string.h>

#include "app_config.h"
#include "aprs_filter.h"

// ---------------------------------------------------------------------------
// Symbol extraction.
//
// Only needed to disambiguate the position-ish DTIs: '!' '=' '/' '@' ';' ')'
// all carry a position, and whether that position is a plain position report,
// a weather report or a buoy is encoded in the symbol, not in the DTI.
//
// `pos` must point at the first byte of the position data itself (i.e. past
// the DTI, past any timestamp, past an object/item name). Two layouts exist:
//
//   uncompressed: DDMM.hhN/DDDMM.hhW$   -> lat[8] table lon[9] code
//   compressed:   /YYYYXXXX$csT        -> table lat[4] lon[4] code
//
// They are told apart exactly as the spec says: uncompressed latitude starts
// with a digit, a compressed report starts with the symbol table byte, which
// is never a digit ('/', '\' or an overlay A-Z / a-j).
// ---------------------------------------------------------------------------
static bool extract_symbol(const char *pos, char *table, char *code) {
    size_t len = strlen(pos);

    if (isdigit((unsigned char)pos[0])) {
        if (len < 19)
            return false;
        *table = pos[8];
        *code = pos[18];
        return true;
    }

    if (len < 10)
        return false;
    *table = pos[0];
    *code = pos[9];
    return true;
}

// Classify a position-carrying payload once its symbol is known. Weather
// stations always use symbol code '_' (with any table/overlay); buoys use
// code 'N' on the primary table ('/N' in the APRS symbol chart). Everything
// else is a plain position report.
static uint16_t position_bit(const char *pos) {
    char table = 0, code = 0;

    if (extract_symbol(pos, &table, &code)) {
        if (code == '_')
            return IGATE_FILT_WEATHER;
        if (code == 'N' && table == '/')
            return IGATE_FILT_BUOY;
    }

    return IGATE_FILT_POSITION;
}

// Same, but for the object/item DTIs, which keep their own filter bit unless
// the symbol says the object *is* a weather report or a buoy. Gating a weather
// object under OBJECT rather than WEATHER would make the WEATHER checkbox lie
// for any station that reports weather as an object (a common WX gateway
// pattern), so the symbol wins here too.
static uint16_t object_bit(const char *pos, uint16_t default_bit) {
    char table = 0, code = 0;

    if (extract_symbol(pos, &table, &code)) {
        if (code == '_')
            return IGATE_FILT_WEATHER;
        if (code == 'N' && table == '/')
            return IGATE_FILT_BUOY;
    }

    return default_bit;
}

uint16_t aprs_filter_classify_info(const char *info) {
    if (info == NULL)
        return 0;

    size_t len = strlen(info);
    if (len < 1)
        return 0;

    switch (info[0]) {
        // ------------------------------------------------------------------
        // Message. ":ADDRESSEE:text" - the addressee field is fixed at 9
        // characters, so the second ':' is always at info[10]. Telemetry
        // metadata (PARM./UNIT./EQNS./BITS.) travels as an APRS message but is
        // telemetry as far as a user reading the checkboxes is concerned:
        // gating it under MESSAGE would let telemetry definitions through with
        // TELEMETRY unchecked, and would strand the definitions (making the
        // T# reports unreadable) with MESSAGE unchecked instead.
        // Bulletins (":BLNn     :") stay MESSAGE.
        // ------------------------------------------------------------------
        case ':':
            if (len >= 16 && info[10] == ':') {
                const char *text = &info[11];
                if (!strncmp(text, "PARM.", 5) || !strncmp(text, "UNIT.", 5) || !strncmp(text, "EQNS.", 5) || !strncmp(text, "BITS.", 5))
                    return IGATE_FILT_TELEMETRY;
            }
            return IGATE_FILT_MESSAGE;

        // Status report.
        case '>':
            return IGATE_FILT_STATUS;

        // Query ("?APRS?", "?WX?", directed queries ...).
        case '?':
            return IGATE_FILT_QUERY;

        // Telemetry report: "T#005,199,000,255,073,123,01101001". Older
        // encoders omit the '#'; both are telemetry.
        case 'T':
            return IGATE_FILT_TELEMETRY;

        // Positionless weather report ("_10090556c220s004g005t077...").
        case '_':
            return IGATE_FILT_WEATHER;

        // Peet Bros U-II / U-I weather in the "#"/"*" formats.
        case '#':
        case '*':
            return IGATE_FILT_WEATHER;

        // Raw GPS / Ultimeter. "$ULTW..." is an Ultimeter weather station;
        // anything else is a raw NMEA sentence, i.e. a position.
        case '$':
            if (len >= 5 && !strncmp(info, "$ULTW", 5))
                return IGATE_FILT_WEATHER;
            return IGATE_FILT_POSITION;

        // ------------------------------------------------------------------
        // Position, no timestamp: "!"/"=" + position data.
        // ------------------------------------------------------------------
        case '!':
        case '=':
            if (len < 2)
                return 0;
            return position_bit(&info[1]);

        // ------------------------------------------------------------------
        // Position with timestamp: "/"/"@" + 7-byte timestamp + position data.
        // ------------------------------------------------------------------
        case '/':
        case '@':
            if (len < 9)
                return 0;
            return position_bit(&info[8]);

        // ------------------------------------------------------------------
        // Object: ";NAME_____*DDHHMMz" + position data. The name is a fixed
        // 9-character field, the live/killed flag is one byte, the timestamp
        // is 7 bytes -> the position starts at info[18].
        // ------------------------------------------------------------------
        case ';':
            if (len < 19)
                return 0;
            return object_bit(&info[18], IGATE_FILT_OBJECT);

        // ------------------------------------------------------------------
        // Item: ")NAME!" + position data. Unlike an object, the name is 3-9
        // bytes and variable length, terminated by '!' (live) or '_' (killed),
        // and no timestamp follows.
        // ------------------------------------------------------------------
        case ')': {
            for (size_t i = 4; i <= 10 && i < len; i++) {
                if (info[i] == '!' || info[i] == '_') {
                    if (i + 1 >= len)
                        return 0;
                    return object_bit(&info[i + 1], IGATE_FILT_ITEM);
                }
            }
            return 0;
        }

        // ------------------------------------------------------------------
        // Mic-E: the position lives in the AX.25 destination field, so nothing
        // here can be inspected further - but it is unambiguously a position.
        // 0x1c/0x1d are the "current/old Mic-E data (Rev 0 beta)" DTIs.
        // ------------------------------------------------------------------
        case '`':
        case '\'':
        case 0x1c:
        case 0x1d:
            return IGATE_FILT_POSITION;

        // ------------------------------------------------------------------
        // Deliberately unclassified -> never relayed, whatever the mask:
        //   '}'  third-party traffic (already gated once; re-gating it is how
        //        IGate loops are born)
        //   '<'  station capabilities   ')' handled above
        //   '{'  user-defined           ','  test/invalid data
        // ------------------------------------------------------------------
        default:
            return 0;
    }
}

uint16_t aprs_filter_classify_tnc2(const char *line) {
    if (line == NULL)
        return 0;

    // The information field starts after the first ':'. A TNC2 header
    // ("SRC-N>DST,PATH,qAR,SERVER") can never contain one, so the first ':'
    // is always the separator - including for messages, whose payload
    // contains further ':' bytes of its own.
    const char *colon = strchr(line, ':');
    if (colon == NULL || colon[1] == 0)
        return 0;

    return aprs_filter_classify_info(colon + 1);
}

const char *aprs_filter_type_name(uint16_t type) {
    switch (type) {
        case IGATE_FILT_MESSAGE:
            return "message";
        case IGATE_FILT_STATUS:
            return "status";
        case IGATE_FILT_TELEMETRY:
            return "telemetry";
        case IGATE_FILT_WEATHER:
            return "weather";
        case IGATE_FILT_OBJECT:
            return "object";
        case IGATE_FILT_ITEM:
            return "item";
        case IGATE_FILT_QUERY:
            return "query";
        case IGATE_FILT_BUOY:
            return "buoy";
        case IGATE_FILT_POSITION:
            return "position";
        default:
            return "unknown";
    }
}
