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

#endif /* STR_APPEND_H */
