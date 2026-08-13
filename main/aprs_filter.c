// @file aprs_filter.c
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
// @brief Maps an APRS information field onto one IGATE_FILT_* bit, so the
// rf2inetFilter / inet2rfFilter bitmasks edited on the web IGATE Filter page
// can actually gate traffic.

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "aprs_coord.h"
#include "aprs_dao.h"
#include "aprs_filter.h"
#include "str_append.h"
#include "weather_telemetry.h"

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
        // encoders omit the '#', and those are telemetry too.
        //
        // Heuristic, not spec: APRS101 defines the Data Type Identifier as
        // "T#", so a bare 'T' with no '#' is not a DTI the standard assigns
        // to anything. Matching on the first byte alone therefore also claims
        // any future or vendor payload that happens to start with 'T'. The
        // trade is deliberate - the alternative is dropping the '#'-less
        // telemetry that real encoders still emit - but it is the reason a
        // packet can be classified TELEMETRY without carrying a sequence
        // number or comma-separated fields.
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
        // Mic-E: the position itself lives in the AX.25 destination field
        // (see aprs_mice_decode()), but the display symbol is carried right
        // here in the info field - byte 8 is the symbol code, byte 9 the
        // symbol table id (APRS101 Ch.10 "Mic-E Information Field"). 0x1c/
        // 0x1d are the "current/old Mic-E data (Rev 0 beta)" DTIs.
        // ------------------------------------------------------------------
        case '`':
        case '\'':
        case 0x1c:
        case 0x1d:
            if (len >= 9) {
                if (info[7] == '_')
                    return IGATE_FILT_WEATHER;
                if (info[7] == 'N' && info[8] == '/')
                    return IGATE_FILT_BUOY;
            }
            return IGATE_FILT_POSITION;

        // ------------------------------------------------------------------
        // Payload kinds with no bit of their own, gated together under
        // IGATE_FILT_OTHER: station capabilities, user-defined formats,
        // Agrelo direction finding, Maidenhead locator beacons (marked
        // obsolete by APRS101 but still heard) and the reserved map feature.
        //
        // None of the five is decoded any further, and three of them are
        // closed questions rather than pending work: '{' is the private
        // experimenter space APRS101 ch.19 reserves for formats that are
        // meaningful only to the software that defines them - this firmware
        // defines none, and that space, not the status text, is where any
        // firmware-specific diagnostic would belong if one were ever added -
        // while '%' (Agrelo direction finder) and '[' (standalone Maidenhead
        // beacon) are marked obsolete by the specification itself, the
        // locator having moved into status reports. Classifying them is what
        // an IGate owes them: an operator who ticks "Other" forwards them,
        // and a station running one of those units stays visible on the maps.
        // ------------------------------------------------------------------
        case '<':
        case '{':
        case '%':
        case '[':
        case '&':
            return IGATE_FILT_OTHER;

        // ------------------------------------------------------------------
        // Deliberately unclassified -> never relayed, whatever the mask:
        //   '}'  third-party traffic (already gated once; re-gating it is how
        //        IGate loops are born)
        //   ','  test/invalid data (APRS101 ch.20), which is not meant to
        //        leave the channel it was sent on
        //
        // Both fall through the catch-all rather than carrying a class of
        // their own, which is the same answer either way: a kind that is
        // never gated needs no bit for the operator to tick, and giving test
        // data one could only ever be used to defeat the rule above.
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

// ---------------------------------------------------------------------------
// APRS-IS server-side filter grammar validation.
//
// Reference: the "server-side filter commands" defined by aprsc / javAPRSSrvr
// (http://www.aprs-is.net/WX/, ftp filter docs). Grammar only - a term is
// "<letter>/<arg1>[/<arg2>...]", terms separated by whitespace. Numeric
// argument shape (int vs float) is checked; range/sanity of the values is
// deliberately not (that is what the "not a full filter engine" scope means).
// ---------------------------------------------------------------------------

static void set_err(char *err, size_t errSize, const char *msg) {
    if (err && errSize)
        snprintf(err, errSize, "%s", msg);
}

// Is `s` (length len) a valid signed decimal number (int or float)?
static bool is_number(const char *s, size_t len) {
    if (len == 0)
        return false;
    size_t i = 0;
    if (s[0] == '+' || s[0] == '-')
        i = 1;
    if (i >= len)
        return false;
    bool sawDigit = false, sawDot = false;
    for (; i < len; i++) {
        if (isdigit((unsigned char)s[i])) {
            sawDigit = true;
        } else if (s[i] == '.' && !sawDot) {
            sawDot = true;
        } else {
            return false;
        }
    }
    return sawDigit;
}

// Is `s` (length len) a valid unsigned integer?
static bool is_uint(const char *s, size_t len) {
    if (len == 0)
        return false;
    for (size_t i = 0; i < len; i++) {
        if (!isdigit((unsigned char)s[i]))
            return false;
    }
    return true;
}

// Split `term` (length len, no surrounding whitespace) on '/' into up to
// maxArgs sub-strings (each as {ptr,len} via outArgs/outLens). Returns the
// number of args found after the leading "<letter>/", or -1 if there is no
// '/' at all (a bare letter with no args, which every filter letter needs at
// least one of). The letter itself is *not* included in the args.
static int split_args(const char *term, size_t len, const char **outArgs, size_t *outLens, int maxArgs) {
    if (len < 2 || term[1] != '/')
        return -1;
    const char *p = term + 2;
    const char *end = term + len;
    int n = 0;
    while (p <= end && n < maxArgs) {
        const char *slash = memchr(p, '/', (size_t)(end - p));
        const char *argEnd = slash ? slash : end;
        outArgs[n] = p;
        outLens[n] = (size_t)(argEnd - p);
        n++;
        if (!slash)
            break;
        p = slash + 1;
    }
    return n;
}

// Validate one term (no leading/trailing whitespace, len >= 1). Returns true
// on success; on failure writes a reason into err/errSize if given.
static bool validate_term(const char *term, size_t len, char *err, size_t errSize) {
    char letter = term[0];
    const char *args[8];
    size_t argLens[8];

    switch (letter) {
        // r/lat/lon/dist - range filter: exactly 3 numeric args.
        case 'r': {
            int n = split_args(term, len, args, argLens, 4);
            if (n != 3 || !is_number(args[0], argLens[0]) || !is_number(args[1], argLens[1]) || !is_number(args[2], argLens[2])) {
                set_err(err, errSize, "'r' needs exactly 3 numeric args: r/lat/lon/dist");
                return false;
            }
            return true;
        }

        // p/prefix[/prefix...] - at least one non-empty prefix.
        case 'p':
        // b/call[/call...] - budlist, at least one non-empty callsign.
        case 'b':
        // e/call[/call...] - entry station, at least one non-empty callsign.
        case 'e':
        // d/digi[/digi...] - digipeater, at least one non-empty callsign.
        case 'd':
        // g/digi[/digi...] - "seen via digi", at least one non-empty callsign.
        case 'g':
        // u/unproto[/unproto...] - unproto path, at least one non-empty value.
        case 'u':
        // s/sym[/sym...] - symbol, at least one non-empty value.
        case 's':
        // o/objname[/objname...] - object name, at least one non-empty value.
        case 'o': {
            int n = split_args(term, len, args, argLens, 8);
            if (n < 1) {
                char msg[64];
                snprintf(msg, sizeof(msg), "'%c' needs at least one '/'-separated argument", letter);
                set_err(err, errSize, msg);
                return false;
            }
            for (int i = 0; i < n; i++) {
                if (argLens[i] == 0) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "'%c' has an empty argument", letter);
                    set_err(err, errSize, msg);
                    return false;
                }
            }
            return true;
        }

        // f/call/dist - friend range: callsign + numeric distance.
        case 'f': {
            int n = split_args(term, len, args, argLens, 3);
            if (n != 2 || argLens[0] == 0 || !is_number(args[1], argLens[1])) {
                set_err(err, errSize, "'f' needs call and numeric dist: f/call/dist");
                return false;
            }
            return true;
        }

        // m/dist - my range: single numeric distance.
        case 'm': {
            int n = split_args(term, len, args, argLens, 2);
            if (n != 1 || !is_number(args[0], argLens[0])) {
                set_err(err, errSize, "'m' needs a single numeric dist: m/dist");
                return false;
            }
            return true;
        }

        // t/type[/callsign[/hops]] - type filter. type is 1+ chars, each one
        // of the ten type letters aprsc accepts - p(osition), o(bject),
        // i(tem), m(essage), q(uery), s(tatus), t(elemetry), u(ser-defined),
        // n(WS), w(eather); callsign/hops optional.
        case 't': {
            int n = split_args(term, len, args, argLens, 3);
            if (n < 1 || argLens[0] == 0) {
                set_err(err, errSize, "'t' needs a type spec: t/type[/callsign[/hops]]");
                return false;
            }
            static const char *validTypes = "poimqstunw"; // the ten type letters aprsc's t/ filter defines
            for (size_t i = 0; i < argLens[0]; i++) {
                if (strchr(validTypes, args[0][i]) == NULL) {
                    char msg[80];
                    snprintf(msg, sizeof(msg), "'t' has unknown type char '%c'", args[0][i]);
                    set_err(err, errSize, msg);
                    return false;
                }
            }
            if (n >= 3 && !is_uint(args[2], argLens[2])) {
                set_err(err, errSize, "'t' hops must be numeric: t/type/callsign/hops");
                return false;
            }
            return true;
        }

        // q/con[/ana] - q construct: 1-2 single-char flags from I,i,O,o,S,s.
        case 'q': {
            int n = split_args(term, len, args, argLens, 2);
            if (n < 1 || argLens[0] != 1 || (n == 2 && argLens[1] != 1)) {
                set_err(err, errSize, "'q' needs 1-2 single-char flags: q/con[/ana]");
                return false;
            }
            static const char *validCon = "IiOoSs";
            if (strchr(validCon, args[0][0]) == NULL || (n == 2 && strchr(validCon, args[1][0]) == NULL)) {
                set_err(err, errSize, "'q' flags must be one of I,i,O,o,S,s");
                return false;
            }
            return true;
        }

        // a/lat1/lon1/lat2/lon2 - area filter: exactly 4 numeric args.
        case 'a': {
            int n = split_args(term, len, args, argLens, 5);
            if (n != 4 || !is_number(args[0], argLens[0]) || !is_number(args[1], argLens[1]) || !is_number(args[2], argLens[2]) ||
                !is_number(args[3], argLens[3])) {
                set_err(err, errSize, "'a' needs 4 numeric args: a/lat1/lon1/lat2/lon2");
                return false;
            }
            return true;
        }

        default: {
            char msg[48];
            snprintf(msg, sizeof(msg), "unknown filter letter '%c'", letter);
            set_err(err, errSize, msg);
            return false;
        }
    }
}

bool aprs_filter_validate_server_string(const char *filter, char *err, size_t errSize) {
    if (err && errSize)
        err[0] = 0;

    if (filter == NULL || filter[0] == 0)
        return true; // empty filter is valid (server sends everything)

    const char *p = filter;
    int termIndex = 0;
    while (*p) {
        while (*p == ' ')
            p++;
        if (*p == 0)
            break;

        const char *termStart = p;
        while (*p && *p != ' ')
            p++;
        size_t termLen = (size_t)(p - termStart);
        termIndex++;

        char termBuf[40];
        size_t copyLen = termLen < sizeof(termBuf) - 1 ? termLen : sizeof(termBuf) - 1;
        memcpy(termBuf, termStart, copyLen);
        termBuf[copyLen] = 0;

        char reason[64];
        if (!validate_term(termStart, termLen, reason, sizeof(reason))) {
            if (err && errSize) {
                snprintf(err, errSize, "term %d '%s' - %s", termIndex, termBuf, reason);
            }
            return false;
        }
    }
    return true;
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
        case IGATE_FILT_OTHER:
            return "other";
        default:
            return "unknown";
    }
}

// ---------------------------------------------------------------------------
// Position decoding, for the local RF->INET range gate.
//
// `pos` points at the first byte of the position data itself (i.e. past the
// DTI, any timestamp, and any object/item name) - the exact same convention
// extract_symbol()/position_bit() above already use to disambiguate the
// symbol. The two layouts are told apart exactly the same way: uncompressed
// latitude starts with a digit; a compressed report starts with the symbol
// table byte, which is never a digit.
// ---------------------------------------------------------------------------

// Decode one base-91 digit (an ASCII byte in the range '!'..'{', i.e. 33..123).
static bool base91_digit(char c, long *out) {
    if (c < 33 || c > 123)
        return false;
    *out = c - 33;
    return true;
}

// Decode 4 consecutive base-91 digits into their combined value
// (d0*91^3 + d1*91^2 + d2*91 + d3), as used by both compressed lat and lon.
static bool base91_4(const char *p, long *out) {
    long v = 0, d;
    for (int i = 0; i < 4; i++) {
        if (!base91_digit(p[i], &d))
            return false;
        v = v * 91 + d;
    }
    *out = v;
    return true;
}

// Place value, in minutes, of each digit of a "MM.hh" minutes field, in the
// order they appear once the '.' at field index 2 is skipped: tens, units,
// tenths, hundredths.
static const float MIN_FIELD_PLACE[4] = { 10.0f, 1.0f, 0.1f, 0.01f };

// Index, within a "MM.hh" minutes field, of each of the four digits above.
static const int MIN_FIELD_DIGIT_INDEX[4] = { 0, 1, 3, 4 };

// Decode a "MM.hh" minutes field that may carry APRS position ambiguity,
// where trailing digits (starting from the hundredths place) are replaced
// with spaces. A fully blanked field decodes to the centre of its ambiguity
// box rather than to its low corner: each blanked digit is taken at its
// minimum (0) for the lower bound and at its maximum (9, except the tens
// digit whose maximum is 5 since minutes never reach 60) for the upper
// bound, and the returned value is the midpoint of the two. A field with no
// blanked digits decodes to its exact value. Only a trailing run of blanked
// digits is a well-formed ambiguous field; a blanked digit followed by a
// present one is rejected.
static bool parse_ambiguous_minutes(const char *field, float *out_min) {
    if (field[2] != '.')
        return false;

    bool blank[4];
    for (int i = 0; i < 4; i++) {
        char c = field[MIN_FIELD_DIGIT_INDEX[i]];
        if (c == ' ')
            blank[i] = true;
        else if (isdigit((unsigned char)c))
            blank[i] = false;
        else
            return false;
    }

    int firstBlank = -1;
    for (int i = 0; i < 4; i++) {
        if (blank[i]) {
            firstBlank = i;
            break;
        }
    }
    if (firstBlank >= 0) {
        for (int i = firstBlank; i < 4; i++)
            if (!blank[i])
                return false;
    }

    float lower = 0.0f, upper = 0.0f;
    for (int i = 0; i < 4; i++) {
        if (blank[i]) {
            float maxDigit = (i == 0) ? 5.0f : 9.0f;
            upper += maxDigit * MIN_FIELD_PLACE[i];
        } else {
            float digit = (float)(field[MIN_FIELD_DIGIT_INDEX[i]] - '0');
            lower += digit * MIN_FIELD_PLACE[i];
            upper += digit * MIN_FIELD_PLACE[i];
        }
    }

    *out_min = (firstBlank < 0) ? lower : (lower + upper) / 2.0f;
    return true;
}

// Uncompressed position: "DDMM.hhN/DDDMM.hhW..." - lat[8] table lon[9] code,
// same 19-byte layout extract_symbol() checks len>=19 for. The minutes
// fields may carry position ambiguity (APRS101 chapter 6): blanked digits
// are resolved to the centre of the resulting ambiguity box, since that is
// the best estimate of the true position and it feeds the range-gate
// distance check further down the pipeline.
static bool decode_pos_uncompressed(const char *pos, float *lat, float *lon) {
    size_t len = strlen(pos);
    if (len < 19)
        return false;

    char latDeg[3] = { pos[0], pos[1], 0 };
    char latMin[6] = { pos[2], pos[3], pos[4], pos[5], pos[6], 0 };
    char ns = pos[7];
    char lonDeg[4] = { pos[9], pos[10], pos[11], 0 };
    char lonMin[6] = { pos[12], pos[13], pos[14], pos[15], pos[16], 0 };
    char ew = pos[17];

    for (int i = 0; i < 2; i++)
        if (!isdigit((unsigned char)latDeg[i]))
            return false;
    for (int i = 0; i < 3; i++)
        if (!isdigit((unsigned char)lonDeg[i]))
            return false;

    float latMinVal, lonMinVal;
    if (!parse_ambiguous_minutes(latMin, &latMinVal))
        return false;
    if (!parse_ambiguous_minutes(lonMin, &lonMinVal))
        return false;

    float la = (float)atof(latDeg) + latMinVal / 60.0f;
    float lo = (float)atof(lonDeg) + lonMinVal / 60.0f;
    if (ns == 'S' || ns == 's')
        la = -la;
    if (ew == 'W' || ew == 'w')
        lo = -lo;

    *lat = la;
    *lon = lo;
    return true;
}

// Compressed position: table[1] lat[4] lon[4] code[1] cs[2] T[1]. Standard
// APRS compressed-position formula for the coordinates; the three bytes that
// follow the symbol code are read by decode_compressed_cs() below.
static bool decode_pos_compressed(const char *pos, float *lat, float *lon) {
    size_t len = strlen(pos);
    if (len < 9)
        return false;

    long latv, lonv;
    if (!base91_4(&pos[1], &latv) || !base91_4(&pos[5], &lonv))
        return false;

    *lat = 90.0f - (float)latv / 380926.0f;
    *lon = -180.0f + (float)lonv / 190463.0f;
    return true;
}

// ---------------------------------------------------------------------------
// Receive-side field decoding (see aprs_filter.h).
// ---------------------------------------------------------------------------

// Length of the position field of each layout, i.e. the offset from its first
// byte to the first byte that follows it: the 7-byte data extension slot for
// the uncompressed layout, the comment for the compressed one (whose cs/T
// bytes take the place of the extension).
#define POS_UNCOMPRESSED_LEN 19
#define POS_COMPRESSED_LEN   13

// Width of the data extension slot, and of the "PHGphgdr/" form, which is the
// one extension that is longer than the slot: APRS 1.2 appends the beacon
// rate character and a mandatory slash (aprs.org/aprs12/probes.txt).
#define EXT_SLOT_LEN 7
#define EXT_PHGR_LEN 9

// Highest antenna height code accepted from a received PHG/DFS extension.
// Height is 10 * 2^code feet, so this stands for 10485760 ft - far past any
// real antenna, and low enough that the shift stays inside uint32_t.
#define EXT_HEIGHT_CODE_MAX 20

// Timestamps carry no year, and often no month or day either, so they are
// resolved against the current clock. A stamp that lands further ahead than
// this belongs to the previous day, month or year rather than to the future.
#define TIME_FUTURE_SLACK_SEC (12 * 3600)

// Below this epoch the clock has not been set yet (no SNTP sync since boot),
// so an absolute UTC value cannot be derived from a partial timestamp.
#define TIME_CLOCK_VALID_EPOCH 1600000000

// Decodes the three bytes that follow the symbol code of a compressed report
// (APRS101 chapter 9). The pair means one of three things, and the
// compression type byte is what says which: a GGA NMEA source puts an
// altitude there, the reserved first-byte value 90 ('{') introduces a
// pre-calculated radio range, and anything else is course and speed. A space
// in the first byte means the sender put nothing there at all.
static void decode_compressed_cs(const char *cs, aprs_rx_report_t *r) {
    if (cs[0] == ' ')
        return;

    int c = (int)(unsigned char)cs[0] - APRS_COMPRESSED_BASE91_OFFSET;
    int s = (int)(unsigned char)cs[1] - APRS_COMPRESSED_BASE91_OFFSET;
    int t = (int)(unsigned char)cs[2] - APRS_COMPRESSED_BASE91_OFFSET;
    if (c < 0 || c > APRS_COMPRESSED_CS_RANGE_MARKER || s < 0 || s > APRS_COMPRESSED_CS_DIGIT_MAX || t < 0)
        return;

    if (((t & APRS_COMPRESSED_T_NMEA_MASK) >> APRS_COMPRESSED_T_NMEA_SHIFT) == APRS_COMPRESSED_T_NMEA_GGA) {
        r->has_altitude = true;
        r->altitude_ft = powf(1.002f, (float)(c * 91 + s));
        return;
    }

    if (c == APRS_COMPRESSED_CS_RANGE_MARKER) {
        r->has_range = true;
        r->range_miles = 2.0f * powf(1.08f, (float)s);
        return;
    }

    r->has_course_speed = true;
    r->course_deg = (uint16_t)((c * 4) % 360);
    r->speed_kt = powf(1.08f, (float)s) - 1.0f;
}

// Decodes the antenna height/gain/directivity codes shared by the "PHGphgd"
// and "DFSshgd" extensions. Height is the APRS code table's 10 * 2^h feet,
// gain is the dB value itself, and directivity is 0 (omni) through 8.
static bool decode_hgd(const char *p, aprs_rx_report_t *r) {
    if (p[0] < '0' || p[1] < '0' || p[1] > '9' || p[2] < '0' || p[2] > '8')
        return false;

    int h = p[0] - '0';
    if (h > EXT_HEIGHT_CODE_MAX)
        return false;

    r->phg_height_ft = 10u << h;
    r->phg_gain_db = (uint8_t)(p[1] - '0');
    r->phg_dir = (uint8_t)(p[2] - '0');
    return true;
}

// Decodes one PHGR "probes" rate character (aprs.org/aprs12/probes.txt) into
// its beacons-per-hour value: '0'-'9' map to 0-9, 'A' upward to 10 and above.
// Returns APRS_RX_PHG_RATE_NONE for any other byte, which is how an ordinary
// comment byte sitting in that position is told apart from a real rate.
static int16_t decode_phg_rate(char c) {
    if (c >= '0' && c <= '9')
        return (int16_t)(c - '0');
    if (c >= 'A' && c <= 'Z')
        return (int16_t)(10 + (c - 'A'));
    return APRS_RX_PHG_RATE_NONE;
}

// True if the three bytes at p are a course or speed group of a "CSE/SPD"
// extension: three digits, or one of the two "not available" spellings the
// specification allows in their place.
static bool is_cse_spd_group(const char *p) {
    if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2]))
        return true;
    return !strncmp(p, "...", 3) || !strncmp(p, "VVV", 3);
}

// Reads the three digits of a course or speed group; the "not available"
// spellings read as zero, which the caller treats as unknown.
static uint16_t cse_spd_value(const char *p) {
    if (!isdigit((unsigned char)p[0]))
        return 0;
    return (uint16_t)((p[0] - '0') * 100 + (p[1] - '0') * 10 + (p[2] - '0'));
}

// Parses the 7-byte data extension slot that follows the symbol code of an
// uncompressed report (APRS101 chapter 7). Returns how many bytes the
// extension occupies, so the caller knows where the comment starts: 0 when
// the slot holds ordinary comment text, 7 for the standard forms, or 9 for
// the APRS 1.2 "PHGphgdr/" form, whose rate character and mandatory slash sit
// past the end of the slot.
//
// The course/speed layout is also what a weather station puts there, where it
// means wind direction and wind speed instead; the symbol code is what tells
// the two apart, so it decides which of the two extension kinds is reported.
static size_t parse_data_extension(const char *slot, size_t avail, char sym_code, aprs_rx_report_t *r) {
    if (avail < EXT_SLOT_LEN)
        return 0;

    if (!strncmp(slot, "PHG", 3)) {
        if (slot[3] < '0' || slot[3] > '9' || !decode_hgd(&slot[4], r))
            return 0;
        int p = slot[3] - '0';
        r->ext = APRS_RX_EXT_PHG;
        r->phg_power_w = (uint16_t)(p * p);
        if (avail >= EXT_PHGR_LEN && slot[8] == '/') {
            int16_t rate = decode_phg_rate(slot[7]);
            if (rate != APRS_RX_PHG_RATE_NONE) {
                r->phg_rate_per_hour = rate;
                return EXT_PHGR_LEN;
            }
        }
        return EXT_SLOT_LEN;
    }

    if (!strncmp(slot, "RNG", 3)) {
        for (int i = 3; i < EXT_SLOT_LEN; i++)
            if (!isdigit((unsigned char)slot[i]))
                return 0;
        r->ext = APRS_RX_EXT_RNG;
        r->has_range = true;
        r->range_miles = (float)((slot[3] - '0') * 1000 + (slot[4] - '0') * 100 + (slot[5] - '0') * 10 + (slot[6] - '0'));
        return EXT_SLOT_LEN;
    }

    if (!strncmp(slot, "DFS", 3)) {
        if (slot[3] < '0' || slot[3] > '9' || !decode_hgd(&slot[4], r))
            return 0;
        r->ext = APRS_RX_EXT_DFS;
        r->dfs_strength = (uint8_t)(slot[3] - '0');
        return EXT_SLOT_LEN;
    }

    if (slot[3] == '/' && is_cse_spd_group(&slot[0]) && is_cse_spd_group(&slot[4])) {
        uint16_t course = cse_spd_value(&slot[0]);
        uint16_t speed = cse_spd_value(&slot[4]);
        r->ext = (sym_code == '_') ? APRS_RX_EXT_WIND : APRS_RX_EXT_CSE_SPD;
        // Course 360 is due north and 0 means "unknown", so a zero course
        // with a zero speed is a station saying it is not moving rather than
        // one heading north at a standstill.
        if (course != 0 || speed != 0) {
            r->has_course_speed = true;
            r->course_deg = (uint16_t)(course % 360);
            r->speed_kt = (float)speed;
        }
        return EXT_SLOT_LEN;
    }

    return 0;
}

// Days from the Unix epoch to the first day of the given civil month, for a
// proleptic Gregorian calendar. Leap years and month lengths fall out of the
// era arithmetic rather than out of a table, and the result is exact for any
// year a time_t can hold. This is what converts a broken-down UTC date into
// an epoch value here: the C library's own inverse of gmtime_r() is not part
// of the standard and is absent from this toolchain's headers.
static long days_from_civil_month(int year, int month) {
    int y = year - (month <= 2 ? 1 : 0);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);                                  // year within the 400-year era, 0-399
    unsigned doy = (unsigned)((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5); // day of the March-based year
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                      // day within the era

    return (long)era * 146097L + (long)doe - 719468L;
}

// Converts a UTC date and time to an epoch value. The day is passed as an
// ordinary day of month but may sit outside the month's real length, and the
// month may sit outside 1-12: both are folded into place first, which is what
// lets the caller step one field back and land on the previous day, month or
// year without knowing how long any of them are.
static time_t utc_from_fields(int year, int month, int day, int hour, int minute, int second) {
    while (month < 1) {
        month += 12;
        year--;
    }
    while (month > 12) {
        month -= 12;
        year++;
    }

    long days = days_from_civil_month(year, month) + (long)day - 1;
    return (time_t)days * 86400 + (time_t)hour * 3600 + (time_t)minute * 60 + (time_t)second;
}

// Resolves a timestamp that carries no year - and, depending on its format,
// no month or day either - into absolute UTC, by filling the missing fields
// from the current clock. A value that would land further ahead than
// TIME_FUTURE_SLACK_SEC belongs to the previous period instead, which is what
// makes a stamp read correctly across a day, month or year boundary. Returns
// 0 when the clock has not been set yet, since there is nothing to resolve
// against.
static time_t resolve_utc(int month, int day, int hour, int minute, int second, bool has_month, bool has_day) {
    time_t now = time(NULL);
    if (now < (time_t)TIME_CLOCK_VALID_EPOCH)
        return 0;

    struct tm ref;
    gmtime_r(&now, &ref);

    int year = ref.tm_year + 1900;
    int mon = has_month ? month : ref.tm_mon + 1;
    int mday = has_day ? day : ref.tm_mday;

    time_t v = utc_from_fields(year, mon, mday, hour, minute, second);
    if (v > now + TIME_FUTURE_SLACK_SEC) {
        // Stepping the next coarser field back by one is all it takes to land
        // on the previous period; utc_from_fields() folds the result back into
        // a real calendar date, month lengths and leap years included.
        if (has_month)
            year -= 1;
        else if (has_day)
            mon -= 1;
        else
            mday -= 1;
        v = utc_from_fields(year, mon, mday, hour, minute, second);
    }

    return v;
}

// Reads a 7-byte timestamp field, the form every position report and object
// uses (APRS101 chapter 6): six digits followed by an indicator byte that
// says how to read them - 'z' for day/hours/minutes UTC, '/' for the same in
// the sender's local time, 'h' for hours/minutes/seconds UTC.
static bool decode_timestamp7(const char *p, size_t avail, aprs_rx_report_t *r) {
    if (avail < 7)
        return false;
    for (int i = 0; i < 6; i++)
        if (!isdigit((unsigned char)p[i]))
            return false;

    int a = (p[0] - '0') * 10 + (p[1] - '0');
    int b = (p[2] - '0') * 10 + (p[3] - '0');
    int c = (p[4] - '0') * 10 + (p[5] - '0');

    switch (p[6]) {
        case 'z':
        case '/':
            if (a < 1 || a > 31 || b > 23 || c > 59)
                return false;
            r->has_time = true;
            r->time_is_zulu = (p[6] == 'z');
            r->time_day = (uint8_t)a;
            r->time_hour = (uint8_t)b;
            r->time_minute = (uint8_t)c;
            // Local time is stated in a time zone the packet does not name,
            // so it is reported as the sender wrote it and never converted.
            if (r->time_is_zulu)
                r->time_utc = resolve_utc(0, a, b, c, 0, false, true);
            return true;

        case 'h':
            if (a > 23 || b > 59 || c > 59)
                return false;
            r->has_time = true;
            r->time_is_zulu = true;
            r->time_hour = (uint8_t)a;
            r->time_minute = (uint8_t)b;
            r->time_second = (uint8_t)c;
            r->time_utc = resolve_utc(0, 0, a, b, c, false, false);
            return true;

        default:
            return false;
    }
}

// Reads the 8-digit month/day/hours/minutes timestamp, the form a positionless
// weather report uses (APRS101 chapter 12). It carries no indicator byte and
// is always UTC.
static bool decode_timestamp_mdhm(const char *p, size_t avail, aprs_rx_report_t *r) {
    if (avail < 8)
        return false;
    for (int i = 0; i < 8; i++)
        if (!isdigit((unsigned char)p[i]))
            return false;

    int month = (p[0] - '0') * 10 + (p[1] - '0');
    int day = (p[2] - '0') * 10 + (p[3] - '0');
    int hour = (p[4] - '0') * 10 + (p[5] - '0');
    int minute = (p[6] - '0') * 10 + (p[7] - '0');
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59)
        return false;

    r->has_time = true;
    r->time_is_zulu = true;
    r->time_day = (uint8_t)day;
    r->time_hour = (uint8_t)hour;
    r->time_minute = (uint8_t)minute;
    r->time_utc = resolve_utc(month, day, hour, minute, 0, true, true);
    return true;
}

// Reads the "/A=aaaaaa" altitude token APRS101 chapter 6 allows anywhere in a
// position comment: six digits of feet above mean sea level, which may be
// negative for a position below it (leading '-' in place of a digit).
static void parse_altitude_token(const char *comment, aprs_rx_report_t *r) {
    const char *p = strstr(comment, "/A=");
    if (p == NULL)
        return;

    p += 3;
    bool negative = (p[0] == '-');
    long value = 0;
    for (int i = 0; i < 6; i++) {
        char c = (i == 0 && negative) ? '0' : p[i];
        if (!isdigit((unsigned char)c))
            return;
        value = value * 10 + (c - '0');
    }

    r->has_altitude = true;
    r->altitude_ft = negative ? -(float)value : (float)value;
}

// Applies a "!DAO!" extension found in the comment to an uncompressed
// position, recovering the decimal minute digit the two-decimal on-air field
// rounds away. The token states a magnitude, so it refines a coordinate away
// from zero on both hemispheres. Compressed reports are left alone: their
// base-91 fields already carry that precision, and the specification does not
// pair the two forms.
static void apply_dao(const char *comment, aprs_rx_report_t *r) {
    float latExtra = 0.0f, lonExtra = 0.0f;
    if (!aprs_dao_parse(comment, strlen(comment), &latExtra, &lonExtra))
        return;

    r->lat += (r->lat < 0.0f ? -latExtra : latExtra) / 60.0f;
    r->lon += (r->lon < 0.0f ? -lonExtra : lonExtra) / 60.0f;
    r->dao_refined = true;
}

// Decodes a position field and everything that hangs off it: the coordinates
// themselves, the compressed cs/T bytes or the uncompressed 7-byte data
// extension slot, and then the comment's "/A=" altitude and "!DAO!" tokens.
// `pos` points at the first byte of the position data, the same convention
// extract_symbol() uses. Returns false when the coordinates do not decode.
static bool decode_position_field(const char *pos, aprs_rx_report_t *r) {
    if (pos == NULL || pos[0] == 0)
        return false;

    size_t len = strlen(pos);
    bool compressed = !isdigit((unsigned char)pos[0]);
    size_t fieldLen = compressed ? POS_COMPRESSED_LEN : POS_UNCOMPRESSED_LEN;

    if (compressed) {
        if (!decode_pos_compressed(pos, &r->lat, &r->lon))
            return false;
    } else {
        if (!decode_pos_uncompressed(pos, &r->lat, &r->lon))
            return false;
    }
    r->has_position = true;

    if (len < fieldLen)
        return true;

    const char *comment = pos + fieldLen;
    if (compressed) {
        // The three bytes of the compressed layout that the uncompressed one
        // spends on the extension slot: two data bytes and the compression
        // type byte that says what they mean.
        decode_compressed_cs(&pos[10], r);
    } else {
        comment += parse_data_extension(pos + POS_UNCOMPRESSED_LEN, len - POS_UNCOMPRESSED_LEN, pos[18], r);
    }

    parse_altitude_token(comment, r);
    if (!compressed)
        apply_dao(comment, r);

    return true;
}

bool aprs_filter_decode_report(const char *info, const char *dst_call, aprs_rx_report_t *out) {
    if (out == NULL)
        return false;

    memset(out, 0, sizeof(*out));
    out->phg_rate_per_hour = APRS_RX_PHG_RATE_NONE;

    if (info == NULL)
        return false;

    size_t len = strlen(info);
    if (len < 1)
        return false;

    // Mirrors aprs_filter_classify_info()'s DTI dispatch so the two never
    // disagree about where the timestamp and the position data start for a
    // given payload.
    switch (info[0]) {
        case '!':
        case '=':
            if (len < 2)
                return false;
            return decode_position_field(&info[1], out);

        case '/':
        case '@':
            if (len < 9)
                return false;
            decode_timestamp7(&info[1], len - 1, out);
            return decode_position_field(&info[8], out) || out->has_time;

        case ';':
            if (len < 19)
                return false;
            decode_timestamp7(&info[11], len - 11, out);
            return decode_position_field(&info[18], out) || out->has_time;

        // Positionless weather report: an 8-digit MDHM timestamp and weather
        // values, whose decoding is out of scope here (see limitations).
        case '_':
            return decode_timestamp_mdhm(&info[1], len - 1, out);

        // Raw NMEA sentence, whose coordinates are degrees-and-minutes text
        // in the receiver's own wire format rather than an APRS position
        // field. "$ULTW..." is an Ultimeter weather record and carries no
        // position; it is refused by the sentence-identifier check inside
        // the decoder like any other unsupported sentence.
        case '$':
            out->has_position = aprs_nmea_decode_position(info, len, &out->lat, &out->lon);
            return out->has_position;

        case ')':
            for (size_t i = 4; i <= 10 && i < len; i++) {
                if (info[i] == '!' || info[i] == '_') {
                    if (i + 1 >= len)
                        return false;
                    return decode_position_field(&info[i + 1], out);
                }
            }
            return false;

        // Mic-E's position is split between this info field and the AX.25
        // destination field; aprs_mice_decode() reassembles both halves and
        // returns the course, speed and altitude it also carries, so the
        // fields below come from there rather than from a second parse.
        case '`':
        case '\'':
        case 0x1c:
        case 0x1d: {
            if (dst_call == NULL)
                return false;
            aprs_mice_report_t mice;
            if (!aprs_mice_decode(dst_call, info, len, &mice))
                return false;

            out->has_position = true;
            out->lat = (float)mice.position.latitude_deg;
            out->lon = (float)mice.position.longitude_deg;
            if (!mice.course_speed.is_unknown) {
                out->has_course_speed = true;
                out->course_deg = (uint16_t)(mice.course_speed.course_deg % 360);
                out->speed_kt = (float)mice.course_speed.speed_knots;
            }
            if (mice.position.has_altitude) {
                out->has_altitude = true;
                out->altitude_ft = (float)mice.position.altitude_ft;
            }
            if (mice.has_status_text) {
                // The status text is an ordinary position comment: it may
                // carry a data extension of its own and a !DAO! refinement.
                size_t textLen = strlen(mice.status_text);
                parse_data_extension(mice.status_text, textLen, 0, out);
                apply_dao(mice.status_text, out);
            }
            return true;
        }

        // Every other DTI either has no position or isn't worth the extra
        // parsing for a "should we push this to APRS-IS" range check.
        default:
            return false;
    }
}

bool aprs_filter_decode_position(const char *info, const char *dst_call, float *out_lat, float *out_lon) {
    if (out_lat == NULL || out_lon == NULL)
        return false;

    aprs_rx_report_t report;
    if (!aprs_filter_decode_report(info, dst_call, &report) || !report.has_position)
        return false;

    *out_lat = report.lat;
    *out_lon = report.lon;
    return true;
}

size_t aprs_filter_format_report(const aprs_rx_report_t *report, char *out, size_t out_size) {
    if (out == NULL || out_size == 0)
        return 0;

    out[0] = 0;
    if (report == NULL)
        return 0;

    size_t used = 0;

    if (report->has_time) {
        // A day of 0 is what marks the hours/minutes/seconds form: the two
        // formats that do carry a day never state it as zero.
        if (report->time_day == 0)
            str_append(out, out_size, &used, "%02u:%02u:%02uZ", report->time_hour, report->time_minute, report->time_second);
        else
            str_append(out, out_size, &used, "%02u%02u%02u%c", report->time_day, report->time_hour, report->time_minute, report->time_is_zulu ? 'Z' : 'L');
    }

    if (report->has_course_speed) {
        const char *label = (report->ext == APRS_RX_EXT_WIND) ? "WIND" : "CSE";
        str_append(out, out_size, &used, "%s%s %03u SPD %.0fkt", used > 0 ? " " : "", label, report->course_deg, (double)report->speed_kt);
    }

    if (report->has_altitude)
        str_append(out, out_size, &used, "%sALT %.0fft", used > 0 ? " " : "", (double)report->altitude_ft);

    if (report->has_range)
        str_append(out, out_size, &used, "%sRNG %.0fmi", used > 0 ? " " : "", (double)report->range_miles);

    if (report->ext == APRS_RX_EXT_PHG) {
        str_append(out, out_size, &used, "%sPHG %uW %luft %udB", used > 0 ? " " : "", (unsigned)report->phg_power_w, (unsigned long)report->phg_height_ft,
                   (unsigned)report->phg_gain_db);
        if (report->phg_rate_per_hour != APRS_RX_PHG_RATE_NONE)
            str_append(out, out_size, &used, " %u/h", (unsigned)report->phg_rate_per_hour);
    } else if (report->ext == APRS_RX_EXT_DFS) {
        str_append(out, out_size, &used, "%sDFS S%u %luft %udB", used > 0 ? " " : "", (unsigned)report->dfs_strength, (unsigned long)report->phg_height_ft,
                   (unsigned)report->phg_gain_db);
    }

    if (report->dao_refined)
        str_append(out, out_size, &used, "%sDAO", used > 0 ? " " : "");

    return used;
}

bool aprs_filter_mice_message(const char *dst_call, const char *info, size_t len, const char **out_name, bool *out_emergency) {
    if (dst_call == NULL || info == NULL || len == 0)
        return false;

    // The four Mic-E data type identifiers, the same set the position decode
    // above dispatches on: current and old data, in their printable and
    // control-character forms.
    switch ((unsigned char)info[0]) {
        case '`':
        case '\'':
        case 0x1c:
        case 0x1d:
            break;
        default:
            return false;
    }

    aprs_mice_report_t mice;
    if (!aprs_mice_decode(dst_call, info, len, &mice))
        return false;

    if (out_name != NULL)
        *out_name = aprs_mice_message_name(mice.message_code, mice.is_custom_message);
    if (out_emergency != NULL)
        *out_emergency = (mice.message_code == APRS_MICE_MSG_EMERGENCY);

    return true;
}

// ---------------------------------------------------------------------------
// Bracketed comment-field alert codes (aprs.org/aprs12/EmergencyCode.txt).
// ---------------------------------------------------------------------------

// The fourteen bracketed strings the proposal defines, in the order it lists
// them: the nine primary designators that mirror the Mic-E status bits (with
// Emergency the one that is raised as an actual alert, and TestAlarm its
// deliberately-not-alerting test twin), followed by the five CENTER/ZOOM
// triggers of the "additional proposal" already implemented by APRS+SA and
// Xastir. Every token is fully bracketed by '!', so none of them is a prefix
// of another and the order they are checked in does not affect the result.
typedef struct {
    const char *token; // Bracketed on-air form, e.g. "!EMERGENCY!".
    const char *name;  // Operator-visible English name.
    bool emergency;    // True only for the one code this station alerts on.
} comment_alert_entry_t;

static const comment_alert_entry_t COMMENT_ALERTS[] = {
    { "!EMERGENCY!", "Emergency", true },   { "!TESTALARM!", "Test Alarm", false },      { "!PRIORITY!", "Priority", false },
    { "!SPECIAL!", "Special", false },      { "!COMMITTED!", "Committed", false },       { "!RETURNING!", "Returning", false },
    { "!INSERVICE!", "In Service", false }, { "!ENROUTE!", "En Route", false },          { "!OFF-DUTY!", "Off Duty", false },
    { "!WXALARM!", "WX Alarm", false },     { "!WARNING!", "Warning", false },           { "!ALARM!", "Alarm", false },
    { "!ALERT!", "Alert", false },          { "!EM!", "Emergency (short form)", false },
};

#define COMMENT_ALERT_COUNT (sizeof(COMMENT_ALERTS) / sizeof(COMMENT_ALERTS[0]))

// Number of bytes from the first byte of a position field to the first byte
// of the free-text comment that follows it, the same layout
// decode_position_field() walks, but computed without needing the position
// itself to decode: only the byte counts matter here, so a malformed
// coordinate still yields the comment that follows it. `pos` points at the
// first byte of the position data (extract_symbol()'s convention); `len` is
// the number of bytes available from `pos` onward, which need not be
// NUL-terminated.
static size_t comment_offset(const char *pos, size_t len) {
    bool compressed = !isdigit((unsigned char)pos[0]);
    size_t fieldLen = compressed ? POS_COMPRESSED_LEN : POS_UNCOMPRESSED_LEN;

    if (len < fieldLen)
        return len;

    if (compressed)
        return fieldLen;

    // parse_data_extension() only needs a report to write its decoded fields
    // into; this scratch one is discarded - only its return value, the
    // number of bytes consumed, is used to step past a PHG/DFS/RNG/CSE-SPD
    // extension when one is present.
    aprs_rx_report_t scratch;
    memset(&scratch, 0, sizeof(scratch));
    return fieldLen + parse_data_extension(pos + POS_UNCOMPRESSED_LEN, len - POS_UNCOMPRESSED_LEN, pos[18], &scratch);
}

// Matches the first `commentLen` bytes at `comment` (not necessarily
// NUL-terminated) against the COMMENT_ALERTS table and reports what it
// found. Per the proposal the code is expected to be the first bytes of the
// comment, so this checks a leading match rather than searching the whole
// field - a station's free-text comment is otherwise free to contain '!'
// bytes of its own without being misread as an alert.
static bool match_comment_alert(const char *comment, size_t commentLen, const char **out_name, bool *out_emergency) {
    for (size_t i = 0; i < COMMENT_ALERT_COUNT; i++) {
        size_t tokLen = strlen(COMMENT_ALERTS[i].token);
        if (commentLen >= tokLen && !strncmp(comment, COMMENT_ALERTS[i].token, tokLen)) {
            if (out_name != NULL)
                *out_name = COMMENT_ALERTS[i].name;
            if (out_emergency != NULL)
                *out_emergency = COMMENT_ALERTS[i].emergency;
            return true;
        }
    }
    return false;
}

// Runs comment_offset()+match_comment_alert() for a position field starting
// at info[posStart], with commentLen accordingly derived from len.
static bool comment_alert_from_pos(const char *info, size_t len, size_t posStart, const char **out_name, bool *out_emergency) {
    const char *pos = &info[posStart];
    size_t posLen = len - posStart;
    size_t off = comment_offset(pos, posLen);
    return match_comment_alert(pos + off, posLen - off, out_name, out_emergency);
}

bool aprs_filter_comment_alert(const char *info, size_t len, const char **out_name, bool *out_emergency) {
    if (info == NULL || len < 1)
        return false;

    switch (info[0]) {
        case '!':
        case '=':
            if (len < 2)
                return false;
            return comment_alert_from_pos(info, len, 1, out_name, out_emergency);

        case '/':
        case '@':
            if (len < 9)
                return false;
            return comment_alert_from_pos(info, len, 8, out_name, out_emergency);

        case ';':
            if (len < 19)
                return false;
            return comment_alert_from_pos(info, len, 18, out_name, out_emergency);

        case ')':
            for (size_t i = 4; i <= 10 && i < len; i++) {
                if (info[i] == '!' || info[i] == '_') {
                    if (i + 1 >= len)
                        return false;
                    return comment_alert_from_pos(info, len, i + 1, out_name, out_emergency);
                }
            }
            return false;

        default:
            return false;
    }
}

float aprs_filter_haversine_km(float lat1, float lon1, float lat2, float lon2) {
    static const float EARTH_RADIUS_KM = 6371.0f;
    static const float DEG2RAD = 0.017453293f; // pi / 180

    float dLat = (lat2 - lat1) * DEG2RAD;
    float dLon = (lon2 - lon1) * DEG2RAD;
    float a = sinf(dLat / 2.0f) * sinf(dLat / 2.0f) + cosf(lat1 * DEG2RAD) * cosf(lat2 * DEG2RAD) * sinf(dLon / 2.0f) * sinf(dLon / 2.0f);
    float c = 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
    return EARTH_RADIUS_KM * c;
}

bool aprs_filter_prefix_match(const char *call, const char *prefixes_csv) {
    if (call == NULL || call[0] == 0 || prefixes_csv == NULL || prefixes_csv[0] == 0)
        return false;

    const char *p = prefixes_csv;
    while (*p) {
        while (*p == ' ' || *p == ',')
            p++;
        if (*p == 0)
            break;

        const char *start = p;
        while (*p && *p != ',')
            p++;
        const char *end = p;
        while (end > start && end[-1] == ' ')
            end--;
        size_t plen = (size_t)(end - start);

        if (plen > 0) {
            bool match = true;
            for (size_t i = 0; i < plen; i++) {
                char cc = call[i];
                if (cc >= 'a' && cc <= 'z')
                    cc -= 32;
                char pc = start[i];
                if (pc >= 'a' && pc <= 'z')
                    pc -= 32;
                if (cc == 0 || cc != pc) {
                    match = false;
                    break;
                }
            }
            if (match)
                return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Selective third-party ('}') unwrap.
// ---------------------------------------------------------------------------
uint16_t aprs_filter_classify_thirdparty_inner(const char *info) {
    if (info == NULL || info[0] != '}')
        return 0;

    // "}SRC>DST,PATH:payload" - the third-party payload is itself a complete
    // TNC2-style line; find its ':' the same way aprs_filter_classify_tnc2()
    // finds the outer one, then classify what follows like any other packet.
    const char *colon = strchr(info + 1, ':');
    if (!colon || colon[1] == 0)
        return 0;

    return aprs_filter_classify_info(colon + 1);
}

// ---------------------------------------------------------------------------
// Local callsign whitelist/blacklist ("budlist").
// ---------------------------------------------------------------------------

// Case-insensitive compare of two NUL-terminated base callsigns (no SSID).
static bool call_base_equals(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z')
            ca -= 32;
        if (cb >= 'a' && cb <= 'z')
            cb -= 32;
        if (ca != cb)
            return false;
        a++;
        b++;
    }
    return *a == *b; // both must end together
}

// True if `call` (base call, optionally with a trailing "-SSID" that is
// stripped here before comparing) matches one of the non-empty entries in
// g_config.budlist[]. Callers on both sides (RF's packet->src.call, which
// never carries an SSID, and INET's TNC2-derived callsign, which may) can
// pass their callsign straight through unmodified.
static bool budlist_contains(const char *call) {
    if (call == NULL || call[0] == 0)
        return false;

    char base[10];
    size_t n = 0;
    while (call[n] && call[n] != '-' && n < sizeof(base) - 1) {
        base[n] = call[n];
        n++;
    }
    base[n] = 0;
    if (n == 0)
        return false;

    for (int i = 0; i < IGATE_BUDLIST_MAX; i++) {
        const char *entry = g_config.budlist[i];
        if (entry[0] == 0)
            continue;
        if (call_base_equals(base, entry))
            return true;
    }
    return false;
}

bool aprs_filter_budlist_pass(budlist_mode_t mode, const char *call) {
    if (mode == BUDLIST_OFF)
        return true;
    bool in_list = budlist_contains(call);
    return mode == BUDLIST_WHITELIST ? in_list : !in_list;
}
