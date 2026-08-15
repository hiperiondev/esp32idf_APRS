/**
 * @file str_append.h
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
 * @brief Bounded, saturating string builder used wherever a fixed-size buffer
 * is filled by a sequence of formatted appends.
 *
 * snprintf() returns the length it *would* have written, not the length it
 * actually wrote. Accumulating that return value directly makes the running
 * offset exceed the buffer size as soon as one append is truncated, and the
 * next `sizeof(buf) - offset` underflows to a huge size_t, so the following
 * snprintf() is told it may write past the end of the buffer. str_append()
 * removes that whole class of defect: the offset is clamped at every step, so
 * it can never exceed `buf_size - 1`, and once the buffer is full every
 * further append is a no-op. A builder can therefore chain any number of
 * appends and test for truncation once, at the end.
 *
 * Also here: str_copy_strip_reserved() for copying operator-editable free
 * text while dropping the bytes APRS101 reserves elsewhere in the frame,
 * str_is_reserved_char() as the definition of which bytes those are, and
 * str_copy_utf8_safe() for truncating 8-bit-clean text to a byte budget
 * without splitting a UTF-8 character across the cut.
 *
 * This header is deliberately implementation-only (static inline, no .c file)
 * so it can be included from `main/` and from every component that already has
 * `main/include` on its include path, without adding a link dependency.
 */

#ifndef STR_APPEND_H
#define STR_APPEND_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Append printf-formatted text to a fixed-size buffer, saturating
 * instead of overflowing.
 *
 * On entry `*used` is the number of characters already in @p buf (excluding
 * the terminating NUL); on return it is updated to the new length. @p buf is
 * always left NUL-terminated and `*used` is always left at or below
 * `buf_size - 1`, whatever the arguments are, so the pair stays valid input
 * for the next call.
 *
 * When the formatted text does not fit, as much of it as there is room for is
 * written, `*used` saturates at `buf_size - 1` and false is returned. Every
 * subsequent call then does nothing and returns false as well, which is what
 * makes it safe to run a whole build loop and check the outcome only once
 * (see str_append_truncated()).
 *
 * @param buf Destination buffer.
 * @param buf_size Total size of @p buf in bytes, including room for the NUL.
 * @param used In/out running length of @p buf, excluding the NUL.
 * @param fmt printf-style format string.
 * @return true if the whole formatted text fit, false if it was truncated
 * (or if the buffer was already full, or the arguments are unusable).
 */
static inline bool str_append(char *buf, size_t buf_size, size_t *used, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

static inline bool str_append(char *buf, size_t buf_size, size_t *used, const char *fmt, ...) {
    if (buf == NULL || used == NULL || buf_size == 0)
        return false;

    // Already full (or handed a bogus offset): terminate defensively, pin the
    // offset to the last writable index and refuse to touch anything else.
    if (*used >= buf_size - 1) {
        *used = buf_size - 1;
        buf[*used] = '\0';
        return false;
    }

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *used, buf_size - *used, fmt, ap);
    va_end(ap);

    // Encoding error: vsnprintf() leaves the buffer contents unspecified past
    // the current offset, so cut the string back to what was valid before.
    if (n < 0) {
        buf[*used] = '\0';
        return false;
    }

    // Truncated: vsnprintf() has already NUL-terminated at the cap, so only
    // the offset needs to be moved to the saturated position.
    if ((size_t)n >= buf_size - *used) {
        *used = buf_size - 1;
        return false;
    }

    *used += (size_t)n;
    return true;
}

/**
 * @brief Test whether a buffer built with str_append() ran out of room.
 *
 * Reports on the saturation state left behind by str_append(), so a builder
 * that ignores the per-call return value can still make one decision about the
 * finished string (drop the frame, count a statistic, log a warning).
 *
 * Output that happens to fill the buffer exactly is reported as truncated too.
 * That is intentional: the one-byte difference is not distinguishable after
 * the fact, and treating a buffer filled to the brim as suspect is the safe
 * way to be wrong.
 *
 * @param used Running length left by the last str_append() call.
 * @param buf_size Total size of the buffer in bytes.
 * @return true if the buffer is full and output may have been cut short.
 */
static inline bool str_append_truncated(size_t used, size_t buf_size) {
    return buf_size == 0 || used >= buf_size - 1;
}

/**
 * @brief Test whether @p c is one of the characters str_copy_strip_reserved()
 * removes from operator-editable free text.
 *
 * Published so a caller that has to reason about the filtered form of a string
 * without building it - deciding whether the text survives the filter at all,
 * or comparing a prefix against what will reach the air - asks the same
 * question the filter itself asks, from the same definition.
 *
 * @param c Byte to inspect.
 * @return true if @p c is reserved and will be dropped.
 */
static inline bool str_is_reserved_char(char c) {
    return c == '|' || c == '~';
}

/**
 * @brief Copy @p src into @p dst, dropping every `|` and `~` along the way.
 *
 * APRS101 chapter 13 (see also `he.fi/doc/aprs-base91-comment-telemetry.txt`)
 * reserves both characters for the base-91 comment telemetry group: `|`
 * delimits it and `~` is reserved alongside it. Any operator-editable
 * free-text field that can end up on the air next to a telemetry group this
 * firmware appends of its own accord - a status text, a position or Mic-E
 * comment, an object/item comment, a bulletin - must not be able to
 * introduce a second `|`-delimited region, or every receiving decoder ends
 * up parsing the wrong one.
 *
 * `{` is deliberately left untouched here: it is legal inside these fields
 * (it is the compressed-position radio-range marker), unlike inside message
 * text, where it doubles as the message-number delimiter and is stripped
 * separately, on top of this filter, by the message component.
 *
 * @p dst is always NUL-terminated. Passing a @p dst_size of 0 is a no-op;
 * passing a NULL @p src is treated as an empty string.
 *
 * @param src Source string to filter and copy from.
 * @param dst Destination buffer.
 * @param dst_size Total size of @p dst in bytes, including room for the NUL.
 */
static inline void str_copy_strip_reserved(const char *src, char *dst, size_t dst_size) {
    if (dst == NULL || dst_size == 0)
        return;

    size_t out = 0;
    for (size_t i = 0; src != NULL && src[i] != '\0' && out < dst_size - 1; i++) {
        char c = src[i];
        if (str_is_reserved_char(c))
            continue;
        dst[out++] = c;
    }
    dst[out] = '\0';
}

/**
 * @brief Number of bytes a UTF-8 continuation byte contributes to, i.e. 0 for
 * a byte that is not a UTF-8 lead byte.
 *
 * A lead byte's top bits announce how many bytes the character occupies: one
 * of ``0xC0``, ``0xE0`` or ``0xF0`` matched against @p c (after masking away
 * the low bits each pattern leaves free) says two, three or four bytes; any
 * other value - a plain ASCII byte, a continuation byte (``10xxxxxx``), or an
 * invalid ``0xF8``-``0xFF`` byte - is not the start of a multi-byte sequence
 * and reports 0.
 *
 * @param c Byte to inspect.
 * @return Total length in bytes of the sequence @p c would start (2-4), or 0
 *         if @p c is not a UTF-8 lead byte.
 */
static inline int utf8_seq_len(unsigned char c) {
    if ((c & 0xE0) == 0xC0)
        return 2;
    if ((c & 0xF0) == 0xE0)
        return 3;
    if ((c & 0xF8) == 0xF0)
        return 4;
    return 0;
}

/**
 * @brief Copy @p src into @p dst, truncating to at most @p dst_size - 1 bytes
 * without splitting a UTF-8 multi-byte character across the cut.
 *
 * A plain byte-count truncation (``strncpy()``, a bounds-checked loop, ...)
 * can land its cut in the middle of a multi-byte UTF-8 sequence whenever the
 * natural cut point falls inside one, leaving an incomplete, invalid sequence
 * at the end of @p dst. For 8-bit-clean text that is passed through unchanged
 * end to end - exactly what this firmware does with packet text, per
 * ``aprs.org/aprs12/utf-8.txt`` - that invalid tail byte or byte pair goes out
 * on the air as-is, which this function avoids: whenever the byte immediately
 * after the would-be cut is a UTF-8 continuation byte (``10xxxxxx``), the cut
 * is walked back to the start of that character instead, so @p dst always
 * ends on a complete character (or on a run of raw 8-bit bytes that never
 * looked like UTF-8 to begin with, which are left exactly as truncation would
 * have cut them - this function only ever moves the cut earlier, never
 * later, so it never grows the output past @p dst_size).
 *
 * This is a plain byte-oriented safeguard, not a validator: text that is not
 * UTF-8 at all (or that is already malformed before truncation) is copied and
 * cut on the same rule and is not rejected or repaired.
 *
 * @p dst is always NUL-terminated. Passing a @p dst_size of 0 is a no-op;
 * passing a NULL @p src is treated as an empty string.
 *
 * @param src Source string to copy from.
 * @param dst Destination buffer.
 * @param dst_size Total size of @p dst in bytes, including room for the NUL.
 */
static inline void str_copy_utf8_safe(const char *src, char *dst, size_t dst_size) {
    if (dst == NULL || dst_size == 0)
        return;

    if (src == NULL)
        src = "";

    size_t srclen = strlen(src);
    size_t cap = dst_size - 1;
    size_t n = srclen < cap ? srclen : cap;

    // The whole string already fits: no cut is being made, so there is no
    // boundary to protect and the byte immediately after n (if any) is simply
    // the character that did not fit, never a continuation of one that did.
    if (n == srclen) {
        memcpy(dst, src, n);
        dst[n] = '\0';
        return;
    }

    // n bytes were kept and src[n] is the first byte left out. If src[n] is a
    // UTF-8 continuation byte, the character it belongs to started somewhere
    // inside [0, n) and is being split by this cut; walk n back to that
    // character's lead byte so the kept bytes end on a complete character.
    // Continuation bytes only ever run 1-3 deep (the longest UTF-8 sequence is
    // four bytes, one lead plus three continuations), so this loop always
    // terminates well before n reaches 0.
    if ((unsigned char)src[n] >= 0x80 && (unsigned char)src[n] < 0xC0) {
        size_t back = n;
        while (back > 0 && (unsigned char)src[back] >= 0x80 && (unsigned char)src[back] < 0xC0)
            back--;
        // Only step back if what is left at `back` is actually a lead byte
        // whose declared length reaches past n - i.e. this really is a
        // sequence that n was about to split. Otherwise `back` landed on a
        // stray continuation byte with no valid lead before it (malformed
        // input to start with), and the original cut at n is left alone
        // rather than discarding good bytes on its account.
        int seqlen = utf8_seq_len((unsigned char)src[back]);
        if (seqlen > 0 && back + (size_t)seqlen > n)
            n = back;
    }

    memcpy(dst, src, n);
    dst[n] = '\0';
}

#endif /* STR_APPEND_H */
