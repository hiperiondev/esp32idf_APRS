/**
 * @file aprs_bm.h
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
 * @brief BrandMeister traffic classifier for TNC2 lines read from APRS-IS.
 *
 * @details BrandMeister's APRS side is an APRS-IS client, not a protocol of
 * its own: every BrandMeister master runs an @c APRSGate process that logs
 * into a public APRS-IS server with an ordinary @c user/pass/filter line and
 * injects DMR-sourced position, telemetry and message traffic as plain TNC2
 * lines. Nothing in this firmware speaks DMR, Homebrew/MMDVM or OpenBridge,
 * and nothing here needs to: recognising BrandMeister traffic is a matter of
 * reading the header of a line the IGate has already received.
 *
 * This header is the single source of truth for what "a BrandMeister packet"
 * means in this firmware. It is header-only and dependency-free - no
 * allocation, no ESP-IDF includes - so the web pages, the IGate component and
 * the service layer can all apply the same rule without pulling in anything
 * else, in the same spirit as @c str_append.h and @c aprs_df.h.
 *
 * Three independent tests are applied, in the order below, and the first one
 * that matches is reported:
 *
 *   1. **TOCALL** - the destination address is @c APBM followed by exactly two
 *      characters. @c APBMxx is the block allocated to BrandMeister in
 *      aprs.org/aprs11/tocalls.txt; @c APBMnD is the main server software and
 *      @c APBMnS its supplementary services (bm-rpt2aprs). Example:
 *      @code LZ1CCM-9>APBM1D,LZ1CCM,DMR*,qAR,LZ1CCM:!4239.00N/02321.00E> @endcode
 *
 *   2. **DMR digipeater alias** - a path element equal to @c DMR appears
 *      before the q construct. This test is not redundant with the first one:
 *      real BrandMeister traffic exists carrying the generic @c APRS tocall
 *      and only the DMR hop, so a classifier keyed on the tocall alone misses
 *      it. Examples, both genuine:
 *      @code PA0WCH>APRS,DMR*,qAS,PI1DMR-10:@043258h5123.03N/00526.95E( @endcode
 *      @code DC6RN-9>APRS,DB0CJ,DMR*,qAR,DB0CJ:@043233h4925.11N/01152.85Ev @endcode
 *      The "has been repeated" marker @c '*' is accepted but not required: a
 *      master that injects the alias unmarked would otherwise go unrecognised,
 *      and @c DMR is not an alias any RF digipeater claims.
 *
 *   3. **Entry station** - the callsign immediately following the q construct
 *      (@c qAS for a frame the master gated itself, @c qAR for one gated
 *      through a connected repeater) matches one of the operator-supplied
 *      gateway callsigns. A stored callsign ending in @c '*' matches by
 *      prefix. This test is skipped entirely when the list is empty, which is
 *      the factory state: it exists for an operator who wants only their own
 *      national master's traffic tagged, and is never needed for the feature
 *      to work.
 *
 * Everything here operates on the header of the line - the part before the
 * first @c ':' - and stops at ::APRS_BM_LINE_SCAN_MAX bytes, so a line with no
 * information field, or one longer than any APRS-IS line may legally be, is
 * bounded rather than scanned to wherever the next NUL happens to fall.
 */

#ifndef APRS_BM_H
#define APRS_BM_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/**
 * @brief Destination-address prefix allocated to BrandMeister.
 *
 * Followed by exactly two more characters to form the six-character TOCALL
 * (@c APBMnD, @c APBMnS).
 */
#define APRS_BM_TOCALL_PREFIX "APBM"

/** @brief Full length of a BrandMeister TOCALL: ::APRS_BM_TOCALL_PREFIX plus two characters. */
#define APRS_BM_TOCALL_LEN 6

/** @brief Digipeater alias BrandMeister masters insert in the path of gated traffic. */
#define APRS_BM_DIGI_ALIAS "DMR"

/** @brief Number of operator-supplied BrandMeister gateway callsigns (test 3). */
#define APRS_BM_GATEWAYS_MAX 4

/** @brief Stored size of one gateway callsign, NUL included: callsign plus SSID, or a prefix ending in '*'. */
#define APRS_BM_GATEWAY_LEN 10

/**
 * @brief Longest stretch of a line this classifier ever reads.
 *
 * An APRS-IS line is capped at 512 bytes by the server protocol, and the
 * header this classifier cares about is a small fraction of that. The bound is
 * what keeps a caller that hands over a buffer with no @c ':' and no NUL from
 * running off the end.
 */
#define APRS_BM_LINE_SCAN_MAX 512

/**
 * @brief APRS-IS server-side filter term that subscribes to worldwide
 * BrandMeister traffic.
 *
 * @details Used by the web page to tell the operator what to add to the IGate
 * filter for the "BrandMeister monitor" mode, and to report whether the
 * running filter already carries it. This firmware never edits the operator's
 * filter string on its own: the filter is theirs to set, and a page that
 * silently rewrote it would misreport what was sent to the server.
 *
 * @warning APRS-IS server filter terms are OR'd, never AND'd - a packet
 * matching any term is passed. Subscribing to this term therefore subscribes
 * to BrandMeister traffic *worldwide*, and a range restriction cannot be
 * expressed alongside it server-side. That is why the INET->RF range gate is
 * a precondition for gating this traffic to the transmitter.
 */
#define APRS_BM_MONITOR_FILTER_TERM "u/APBM*"

/**
 * @brief Which of the three tests recognised a line as BrandMeister traffic.
 */
typedef enum {
    APRS_BM_MATCH_NONE = 0, /**< Not BrandMeister traffic, or the line has no usable header. */
    APRS_BM_MATCH_TOCALL,   /**< Destination address is an @c APBMxx TOCALL (test 1). */
    APRS_BM_MATCH_DMR_HOP,  /**< Path carries the @c DMR digipeater alias ahead of the q construct (test 2). */
    APRS_BM_MATCH_GATEWAY,  /**< Entry station after the q construct is a configured BrandMeister gateway (test 3). */
} aprs_bm_match_t;

/**
 * @brief Length of the line's header - everything before the first ':' -
 * bounded by ::APRS_BM_LINE_SCAN_MAX.
 *
 * @param line NUL-terminated TNC2 line, or NULL.
 *
 * @return Number of header bytes, 0 when @p line is NULL or empty. A line with
 *         no ':' inside the bound reports the bounded length, so the caller
 *         still scans a fixed amount rather than nothing.
 */
static inline size_t aprs_bm_header_len(const char *line) {
    if (line == NULL)
        return 0;
    size_t i = 0;
    while (i < APRS_BM_LINE_SCAN_MAX && line[i] != 0 && line[i] != ':')
        i++;
    return i;
}

/**
 * @brief Case-insensitive comparison of a non-terminated field against a
 * NUL-terminated literal.
 *
 * @param field  Start of the field; not NUL-terminated.
 * @param len    Field length in bytes.
 * @param want   NUL-terminated string to compare against.
 *
 * @return true when the field is exactly @p want, ignoring case.
 */
static inline bool aprs_bm_ci_equal_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'a' && ca <= 'z')
            ca = (char)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z')
            cb = (char)(cb - 'a' + 'A');
        if (ca != cb)
            return false;
    }
    return true;
}

/**
 * @brief Case-insensitive comparison of a non-terminated field against a
 * NUL-terminated literal.
 *
 * @param field  Start of the field; not NUL-terminated.
 * @param len    Field length in bytes.
 * @param want   NUL-terminated string to compare against.
 *
 * @return true when the field is exactly @p want, ignoring case.
 */
static inline bool aprs_bm_field_equals(const char *field, size_t len, const char *want) {
    if (strlen(want) != len)
        return false;
    return aprs_bm_ci_equal_n(field, want, len);
}

/**
 * @brief Does a path element match one stored gateway callsign?
 *
 * @param field Start of the path element; not NUL-terminated.
 * @param len   Element length in bytes.
 * @param want  Stored callsign. A trailing '*' turns the comparison into a
 *              prefix test; "*" on its own matches any entry station.
 *
 * @return true on a match, false for an empty stored callsign.
 */
static inline bool aprs_bm_gateway_matches(const char *field, size_t len, const char *want) {
    if (want == NULL)
        return false;
    // Bounded by hand rather than with strnlen(): that one is POSIX, and this
    // header is included by translation units that do not set a feature-test
    // macro for it.
    size_t wlen = 0;
    while (wlen < APRS_BM_GATEWAY_LEN && want[wlen] != 0)
        wlen++;
    if (wlen == 0)
        return false;

    if (want[wlen - 1] == '*') {
        size_t plen = wlen - 1;
        if (plen == 0)
            return true;
        if (len < plen)
            return false;
        return aprs_bm_ci_equal_n(field, want, plen);
    }

    return len == wlen && aprs_bm_ci_equal_n(field, want, wlen);
}

/**
 * @brief Test 1: is the line's destination address a BrandMeister TOCALL?
 *
 * @param line NUL-terminated TNC2 line, or NULL.
 *
 * @return true when the destination is exactly ::APRS_BM_TOCALL_LEN characters
 *         beginning with ::APRS_BM_TOCALL_PREFIX.
 */
static inline bool aprs_bm_tocall_matches(const char *line) {
    size_t hdr = aprs_bm_header_len(line);
    if (hdr == 0)
        return false;

    // The destination runs from the '>' to the first ',' of the path, or to
    // the end of the header when the line carries no path at all.
    size_t gt = 0;
    while (gt < hdr && line[gt] != '>')
        gt++;
    if (gt >= hdr)
        return false;

    size_t start = gt + 1;
    size_t end = start;
    while (end < hdr && line[end] != ',')
        end++;

    size_t len = end - start;
    if (len != APRS_BM_TOCALL_LEN)
        return false;
    return aprs_bm_field_equals(line + start, sizeof(APRS_BM_TOCALL_PREFIX) - 1, APRS_BM_TOCALL_PREFIX);
}

/**
 * @brief Does a path element name a q construct ("qAR", "qAS", "qAO", ...)?
 *
 * @param field Start of the path element; not NUL-terminated.
 * @param len   Element length in bytes.
 *
 * @return true for any three-character element beginning with @c qA, in either
 *         case.
 */
static inline bool aprs_bm_is_q_construct(const char *field, size_t len) {
    if (len != 3)
        return false;
    char c0 = field[0];
    char c1 = field[1];
    if (c0 == 'Q')
        c0 = 'q';
    if (c1 == 'a')
        c1 = 'A';
    return c0 == 'q' && c1 == 'A';
}

/**
 * @brief Classify one TNC2 line.
 *
 * @param line           NUL-terminated TNC2 line as read from APRS-IS
 *                       ("SRC>DST,PATH,qXX,ENTRY:info"), or NULL.
 * @param gateways       Operator-supplied gateway callsigns for test 3, or
 *                       NULL to skip that test. A stored entry ending in '*'
 *                       matches by prefix; an empty entry is skipped.
 * @param gateway_count  Number of usable rows in @p gateways.
 *
 * @return The first test that matched, or ::APRS_BM_MATCH_NONE.
 */
static inline aprs_bm_match_t aprs_bm_classify(const char *line, const char gateways[][APRS_BM_GATEWAY_LEN], size_t gateway_count) {
    size_t hdr = aprs_bm_header_len(line);
    if (hdr == 0)
        return APRS_BM_MATCH_NONE;

    if (aprs_bm_tocall_matches(line))
        return APRS_BM_MATCH_TOCALL;

    // Walk the path elements once, left to right. The q construct ends the
    // part of the path a digipeater alias can appear in - everything after it
    // was written by APRS-IS itself - so test 2 stops there, and test 3 reads
    // the single element that follows it.
    size_t pos = 0;
    while (pos < hdr && line[pos] != '>')
        pos++;
    if (pos >= hdr)
        return APRS_BM_MATCH_NONE;
    pos++; // destination
    while (pos < hdr && line[pos] != ',')
        pos++;

    bool seen_q = false;
    while (pos < hdr) {
        pos++; // step over the ','
        size_t start = pos;
        while (pos < hdr && line[pos] != ',')
            pos++;
        size_t len = pos - start;
        if (len == 0)
            continue;

        if (seen_q) {
            // The element right after the q construct is the entry station.
            if (gateways != NULL) {
                for (size_t g = 0; g < gateway_count; g++) {
                    if (aprs_bm_gateway_matches(line + start, len, gateways[g]))
                        return APRS_BM_MATCH_GATEWAY;
                }
            }
            return APRS_BM_MATCH_NONE; // only the first element after the q construct is the entry station
        }

        if (aprs_bm_is_q_construct(line + start, len)) {
            seen_q = true;
            continue;
        }

        // Test 2: the alias, with or without the "has been repeated" marker.
        size_t alias_len = len;
        if (alias_len > 0 && line[start + alias_len - 1] == '*')
            alias_len--;
        if (aprs_bm_field_equals(line + start, alias_len, APRS_BM_DIGI_ALIAS))
            return APRS_BM_MATCH_DMR_HOP;
    }

    return APRS_BM_MATCH_NONE;
}

/**
 * @brief Convenience predicate over ::aprs_bm_classify().
 *
 * @param line           NUL-terminated TNC2 line, or NULL.
 * @param gateways       Gateway callsign list for test 3, or NULL.
 * @param gateway_count  Number of usable rows in @p gateways.
 *
 * @return true when the line came from the BrandMeister network.
 */
static inline bool aprs_bm_is_bm_line(const char *line, const char gateways[][APRS_BM_GATEWAY_LEN], size_t gateway_count) {
    return aprs_bm_classify(line, gateways, gateway_count) != APRS_BM_MATCH_NONE;
}

/**
 * @brief Short, untranslated name of a match, for logs and the traffic view.
 *
 * @param m Match reported by ::aprs_bm_classify().
 *
 * @return A static string; never NULL.
 */
static inline const char *aprs_bm_match_name(aprs_bm_match_t m) {
    switch (m) {
        case APRS_BM_MATCH_TOCALL:
            return "tocall";
        case APRS_BM_MATCH_DMR_HOP:
            return "DMR hop";
        case APRS_BM_MATCH_GATEWAY:
            return "gateway";
        case APRS_BM_MATCH_NONE:
        default:
            return "none";
    }
}

/**
 * @brief Does a filter string already carry the worldwide BrandMeister
 * subscription term?
 *
 * @details Compares whole space-separated terms rather than searching for a
 * substring, so a term such as @c "u/APBM*ABC" is not mistaken for
 * ::APRS_BM_MONITOR_FILTER_TERM.
 *
 * @param filter NUL-terminated APRS-IS server filter string, or NULL.
 *
 * @return true when one of the terms is exactly the monitor term.
 */
static inline bool aprs_bm_filter_has_monitor_term(const char *filter) {
    if (filter == NULL)
        return false;
    const char *p = filter;
    while (*p) {
        while (*p == ' ')
            p++;
        if (*p == 0)
            break;
        const char *start = p;
        while (*p && *p != ' ')
            p++;
        if (aprs_bm_field_equals(start, (size_t)(p - start), APRS_BM_MONITOR_FILTER_TERM))
            return true;
    }
    return false;
}

#endif /* APRS_BM_H */
