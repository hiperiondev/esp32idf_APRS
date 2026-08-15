/**
 * @file aprs_free_text.h
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
 * @brief Single implementation of "turn an operator-typed free-text field into
 * the bytes that go on the air".
 *
 * Every packet this station originates that carries free text - a position or
 * Mic-E comment, a status text, a weather comment, an object/item comment, a
 * bulletin - has to answer the same two questions before the text reaches a
 * frame, and both answers are station-wide rather than per-service:
 *
 * 1. Which bytes must be removed? `|` and `~` are reserved by APRS 1.2 ch.13
 *    for the base-91 comment telemetry group, which this firmware emits of its
 *    own accord (telemetry_build_comment_tlm()). A `|` typed into any of these
 *    fields opens a second `|`-delimited region and every receiving decoder
 *    parses the wrong span as telemetry. str_copy_strip_reserved() does the
 *    filtering.
 *
 * 2. Does the APRS-IS no-archive marker go in front? ::APRS_NO_ARCHIVE_MARKER
 *    is the literal the operator opts into with a single station-wide checkbox
 *    (Station page, ::app_config_t::my_no_archive). It asks the databases
 *    behind APRS-IS not to store the packet; it addresses the archives rather
 *    than the gateways, so it never decides where a frame may travel.
 *
 * Answering both in one place is what this header is for: a service that calls
 * aprs_free_text_build() cannot get one of the two right and the other wrong,
 * and adding a service later cannot silently opt out of either.
 *
 * The marker is recognised anywhere in the packet, not only at the front, so
 * an originator that already puts a fixed leading token in the field (the
 * frequency block of a repeater object, for instance) may assemble that token
 * first and pass the finished string here: the marker still counts.
 *
 * Scope note. This covers the fields an operator writes and a receiver reads
 * as descriptive text. It deliberately does not cover message text (the marker
 * is not descriptive text there, it is a word the correspondent reads in the
 * body of a message addressed to them), telemetry PARM/UNIT/EQNS/BITS
 * definition packets (fixed-layout metadata with no free-text slot, and a
 * budget the definition itself needs), or query responses (which answer
 * another station's question and are not archived as own-station reports).
 *
 * The no-archive flag is taken as a parameter rather than read from
 * ::g_config here, so a caller that already holds a configuration snapshot
 * uses that snapshot, and a caller running outside app_config_lock() takes the
 * bool the same way it takes every other field it needs.
 *
 * This header is deliberately implementation-only (static inline, no .c file)
 * so it can be included from `main/` and from every component that already has
 * `main/include` on its include path, without adding a link dependency.
 */

#ifndef APRS_FREE_TEXT_H
#define APRS_FREE_TEXT_H

#include <stdbool.h>
#include <stddef.h>

#include "str_append.h"

/**
 * @brief The APRS-IS no-archive marker (APRS 1.1), asking the databases behind
 * APRS-IS not to store the packet carrying it.
 */
#define APRS_NO_ARCHIVE_MARKER "!x!"

/** @brief Length of ::APRS_NO_ARCHIVE_MARKER in bytes, excluding the NUL. */
#define APRS_NO_ARCHIVE_MARKER_LEN 3

/**
 * @brief Bytes aprs_free_text_build() may add in front of the operator's text:
 * ::APRS_NO_ARCHIVE_MARKER plus the single separating space.
 *
 * A destination buffer sized `field + APRS_NO_ARCHIVE_PREFIX_LEN + 1` carries
 * the longest possible field with the marker enabled and still has room for
 * the NUL, so enabling the marker never costs the operator four characters of
 * their own text.
 */
#define APRS_NO_ARCHIVE_PREFIX_LEN (APRS_NO_ARCHIVE_MARKER_LEN + 1)

/**
 * @brief Test whether @p src already opens with ::APRS_NO_ARCHIVE_MARKER once
 * the reserved characters are taken out.
 *
 * The comparison skips the bytes str_copy_strip_reserved() removes, so text
 * the operator typed as `|!x!` - which reaches the air as `!x!` - is
 * recognised as already marked, exactly like a plain `!x!` would be. Only the
 * leading position is examined: a marker further along is the operator's own
 * text and says nothing about whether the field opens with one.
 *
 * @param src Text as the operator stored it, before filtering. NULL is treated
 *            as an empty string.
 * @return true if the first three surviving characters are the marker.
 */
static inline bool aprs_free_text_has_no_archive_marker(const char *src) {
    if (src == NULL)
        return false;

    static const char marker[] = APRS_NO_ARCHIVE_MARKER;
    size_t matched = 0;
    for (size_t i = 0; src[i] != '\0'; i++) {
        if (str_is_reserved_char(src[i]))
            continue;
        if (src[i] != marker[matched])
            return false;
        if (marker[++matched] == '\0')
            return true;
    }
    return false;
}

/**
 * @brief Build the on-air form of one own-station free-text field.
 *
 * Copies @p src into @p dst with every reserved character removed, and, when
 * @p no_archive is set, prefixes ::APRS_NO_ARCHIVE_MARKER followed by one
 * space. The space keeps a leading word of the operator's own text from being
 * read as a continuation of the marker's closing `!`; an empty field gets the
 * bare marker with no trailing space. A field that already opens with the
 * marker is left to speak for itself, so an operator who types it by hand
 * never sends it twice.
 *
 * With @p no_archive clear this is exactly str_copy_strip_reserved().
 *
 * @p dst is always NUL-terminated. Passing a @p dst_size of 0 is a no-op;
 * passing a NULL @p src is treated as an empty string. Output longer than
 * @p dst is cut to fit, so a caller that must not lose operator text sizes
 * @p dst with ::APRS_NO_ARCHIVE_PREFIX_LEN of headroom over the stored field.
 *
 * @param src Text as the operator stored it, before filtering.
 * @param no_archive Station-wide no-archive choice
 *                   (::app_config_t::my_no_archive), as snapshotted by the
 *                   caller.
 * @param dst Destination buffer.
 * @param dst_size Total size of @p dst in bytes, including room for the NUL.
 */
static inline void aprs_free_text_build(const char *src, bool no_archive, char *dst, size_t dst_size) {
    if (dst == NULL || dst_size == 0)
        return;

    if (!no_archive || aprs_free_text_has_no_archive_marker(src)) {
        str_copy_strip_reserved(src, dst, dst_size);
        return;
    }

    size_t used = 0;
    str_append(dst, dst_size, &used, "%s", APRS_NO_ARCHIVE_MARKER);

    // The separating space is only earned by text that actually survives the
    // reserved-character filter: a field holding nothing but reserved bytes
    // reaches the air empty and must not leave a trailing space behind the
    // marker.
    bool has_text = false;
    for (size_t i = 0; src != NULL && src[i] != '\0'; i++) {
        if (!str_is_reserved_char(src[i])) {
            has_text = true;
            break;
        }
    }
    if (!has_text || used + 1 >= dst_size)
        return;

    dst[used++] = ' ';
    dst[used] = '\0';
    str_copy_strip_reserved(src, dst + used, dst_size - used);
}

#endif /* APRS_FREE_TEXT_H */
