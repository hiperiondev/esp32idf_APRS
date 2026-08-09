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
#include "aprs_filter.h"
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

// Compressed position: table[1] lat[4] lon[4] code[1] (+ optional compression
// type byte, not needed here). Standard APRS compressed-position formula.
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

static bool decode_pos(const char *pos, float *lat, float *lon) {
    if (pos == NULL || pos[0] == 0)
        return false;
    if (isdigit((unsigned char)pos[0]))
        return decode_pos_uncompressed(pos, lat, lon);
    return decode_pos_compressed(pos, lat, lon);
}

bool aprs_filter_decode_position(const char *info, const char *dst_call, float *out_lat, float *out_lon) {
    if (info == NULL || out_lat == NULL || out_lon == NULL)
        return false;

    size_t len = strlen(info);
    if (len < 1)
        return false;

    // Mirrors aprs_filter_classify_info()'s DTI dispatch so the two never
    // disagree about where the position data starts for a given payload.
    switch (info[0]) {
        case '!':
        case '=':
            if (len < 2)
                return false;
            return decode_pos(&info[1], out_lat, out_lon);

        case '/':
        case '@':
            if (len < 9)
                return false;
            return decode_pos(&info[8], out_lat, out_lon);

        case ';':
            if (len < 19)
                return false;
            return decode_pos(&info[18], out_lat, out_lon);

        case ')':
            for (size_t i = 4; i <= 10 && i < len; i++) {
                if (info[i] == '!' || info[i] == '_') {
                    if (i + 1 >= len)
                        return false;
                    return decode_pos(&info[i + 1], out_lat, out_lon);
                }
            }
            return false;

        // Mic-E's position is split between this info field and the AX.25
        // destination field; aprs_mice_decode() reassembles both halves.
        case '`':
        case '\'':
        case 0x1c:
        case 0x1d: {
            if (dst_call == NULL)
                return false;
            aprs_mice_report_t mice;
            if (!aprs_mice_decode(dst_call, info, len, &mice))
                return false;
            *out_lat = (float)mice.position.latitude_deg;
            *out_lon = (float)mice.position.longitude_deg;
            return true;
        }

        // Every other DTI either has no position or isn't worth the extra
        // parsing for a "should we push this to APRS-IS" range check.
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
