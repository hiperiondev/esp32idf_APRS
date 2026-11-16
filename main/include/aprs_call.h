/**
 * @file aprs_call.h
 *
 * @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
 * @date 2026
 * @copyright GNU General Public License v3
 * @see https://github.com/hiperiondev/esp32idf_APRS
 *
 * @brief Normalization of the callsign and digipeater-path text the operator
 * configures, so that every such field holds something that encodes to a legal
 * AX.25 address.
 *
 * An AX.25 address carries one to six upper-case letters or digits and an SSID
 * of 0 to 15. A configured callsign typed in lower case, with punctuation in it,
 * or a path preset holding characters that end a TNC2 header field (@c ':',
 * @c '>') or an SSID above 15, would otherwise reach the encoder and come out
 * as a frame no receiver accepts - or not come out at all. The three functions
 * below are applied at both points a configured value enters the firmware: the
 * web admin form handlers and the loaders of the stored JSON files, so a value
 * that reaches a transmitter is legal whichever way it got there.
 *
 * Each one rewrites its buffer in place and never makes it longer. Lower-case
 * letters become upper case and punctuation inside a callsign is left out,
 * which keeps the operator's intent ("lu3vea" is LU3VEA) wherever there is
 * one. An SSID that is not a legal number is never repaired, since any repair
 * would name a different address: the address carrying it is refused instead.
 *
 * This header is deliberately implementation-only (static inline, no .c file)
 * so it can be included from `main/` and from every component that already has
 * `main/include` on its include path, without adding a link dependency.
 */

#ifndef APRS_CALL_H
#define APRS_CALL_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define APRS_CALL_BASE_MAX 6  /**< Longest base callsign an AX.25 address carries. */
#define APRS_CALL_SSID_MAX 15 /**< Highest SSID an AX.25 address carries. */
#define APRS_CALL_PATH_MAX 8  /**< AX.25 limit on digipeater addresses in one frame. */

/**
 * @brief Upper-case form of @p c when it can be part of a base callsign, or
 * 0 when it cannot.
 *
 * @param c Character to test.
 * @return The character itself for an upper-case letter or a digit, its
 *         upper-case form for a lower-case letter, 0 for anything else.
 */
static inline char aprs_call_char(char c) {
    if (c >= 'a' && c <= 'z')
        return (char)(c - 'a' + 'A');
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        return c;
    return 0;
}

/**
 * @brief Normalize a base callsign in place: upper case, letters and digits
 * only, at most ::APRS_CALL_BASE_MAX characters.
 *
 * @details Characters that cannot appear in an address are dropped and the
 * result is cut to six characters. The SSID of every field this is used for is
 * stored separately, so a @c '-' and everything after it is dropped with the
 * rest of the punctuation - "LU3VEA-10" typed into such a field keeps its base
 * call, "LU3VEA".
 *
 * @param s NUL-terminated buffer to normalize; NULL is ignored.
 * @return true if the result is a non-empty callsign, false if nothing usable
 *         was left (the buffer is then the empty string).
 */
static inline bool aprs_call_normalize_base(char *s) {
    if (s == NULL)
        return false;

    size_t out = 0;
    for (size_t in = 0; s[in] != 0; in++) {
        if (s[in] == '-')
            break;
        char c = aprs_call_char(s[in]);
        if (c != 0 && out < APRS_CALL_BASE_MAX)
            s[out++] = c;
    }
    s[out] = 0;
    return out > 0;
}

/**
 * @brief Normalize one address token in place: a base callsign, an optional
 * @c -SSID and, when @p allow_used is true, an optional trailing @c '*'.
 *
 * @details Unlike a base-callsign field, a token is taken as written or not at
 * all: its callsign must already be one to ::APRS_CALL_BASE_MAX letters or
 * digits (lower-case letters are taken as their upper-case form). An SSID of
 * one or two digits worth 1 to ::APRS_CALL_SSID_MAX is kept; an SSID of 0 is
 * dropped together with its @c '-', since it is the same address written
 * without one. Anything else - punctuation inside the callsign, a callsign too
 * long, any other text after the @c '-' - makes the whole token unusable,
 * because no repair of it keeps the address the operator wrote: "WIDE2-16" is
 * not "WIDE2", which is a spent alias, and "X:Y" is not "XY".
 *
 * @param s          NUL-terminated buffer to normalize; NULL is ignored.
 * @param allow_used Whether a trailing @c '*' (the has-been-repeated marker of
 *                   a TNC2 path entry) is kept.
 * @return true if the token is a legal address (the buffer then holds its
 *         normalized form), false otherwise (the buffer is then the empty
 *         string).
 */
static inline bool aprs_call_normalize_token(char *s, bool allow_used) {
    if (s == NULL)
        return false;

    const char *dash = strchr(s, '-');
    const char *star = allow_used ? strchr(s, '*') : NULL;
    const char *call_end = dash ? dash : (star ? star : s + strlen(s));

    // The callsign part, before any '-' or '*', is checked character by
    // character before anything is rewritten.
    size_t call_len = (size_t)(call_end - s);
    bool call_ok = call_len >= 1 && call_len <= APRS_CALL_BASE_MAX;
    for (const char *p = s; call_ok && p < call_end; p++)
        call_ok = aprs_call_char(*p) != 0;
    if (!call_ok || (star != NULL && star[1] != 0) || (star != NULL && dash != NULL && star < dash)) {
        s[0] = 0;
        return false;
    }
    bool used = star != NULL;

    unsigned ssid = 0;
    if (dash != NULL) {
        size_t digits = 0;
        const char *p = dash + 1;
        for (; *p >= '0' && *p <= '9'; p++) {
            ssid = ssid * 10 + (unsigned)(*p - '0');
            if (++digits > 2)
                break;
        }
        bool tail_ok = (*p == 0) || (*p == '*' && allow_used && p[1] == 0);
        if (digits == 0 || digits > 2 || ssid > APRS_CALL_SSID_MAX || !tail_ok) {
            s[0] = 0;
            return false;
        }
    }

    if (!aprs_call_normalize_base(s))
        return false;

    size_t n = strlen(s);
    if (ssid >= 1) {
        s[n++] = '-';
        if (ssid >= 10)
            s[n++] = (char)('0' + ssid / 10);
        s[n++] = (char)('0' + ssid % 10);
    }
    if (used)
        s[n++] = '*';
    s[n] = 0;
    return true;
}

/**
 * @brief Normalize a comma-separated digipeater path in place.
 *
 * @details Each entry is normalized by aprs_call_normalize_token() with the
 * has-been-repeated marker allowed; an entry that leaves no callsign is
 * dropped together with its comma, and at most ::APRS_CALL_PATH_MAX entries are
 * kept. Spaces around the commas are tolerated. The result is the plain
 * "WIDE1-1,WIDE2-1" form a TNC2 header carries, or the empty string.
 *
 * @param s    NUL-terminated buffer to normalize; NULL is ignored.
 * @param size Size of @p s, in bytes.
 * @return Number of path entries kept.
 */
static inline int aprs_call_normalize_path(char *s, size_t size) {
    if (s == NULL || size == 0)
        return 0;

    // Every token is normalized in a scratch copy and appended at the write
    // position, which never overtakes the read position: a normalized token is
    // never longer than the text it was read from plus its comma.
    size_t out = 0;
    int kept = 0;
    const char *p = s;
    while (*p != 0 && kept < APRS_CALL_PATH_MAX) {
        const char *end = strchr(p, ',');
        size_t len = (end != NULL) ? (size_t)(end - p) : strlen(p);

        char tok[16];
        size_t t = 0;
        for (size_t i = 0; i < len && t < sizeof(tok) - 1; i++) {
            if (p[i] != ' ')
                tok[t++] = p[i];
        }
        tok[t] = 0;

        size_t next = (size_t)(p - s) + len + ((end != NULL) ? 1 : 0);
        if (aprs_call_normalize_token(tok, true)) {
            size_t tl = strlen(tok);
            if (out + (kept > 0 ? 1 : 0) + tl < size) {
                if (kept > 0)
                    s[out++] = ',';
                memmove(&s[out], tok, tl);
                out += tl;
                kept++;
            }
        }
        p = s + next;
    }
    s[out] = 0;
    return kept;
}

#endif /* APRS_CALL_H */
