/**
 * @file json_escape.h
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
 * @brief JSON string escaping, in the two shapes the firmware needs.
 *
 * The device emits JSON in two situations and neither one uses cJSON to print
 * it: the web pages fetch small JSON documents assembled in fixed-size RAM
 * buffers (last heard, traffic log, message history), and the persisted
 * configuration files are streamed token-by-token straight to a FILE so no
 * whole-document copy ever exists in RAM. Both need the same guarantee - any
 * text an operator or an off-air packet can put into a field must come back out
 * of cJSON_Parse() unchanged, and must never be able to terminate the string
 * literal it sits in.
 *
 * json_escape() serves the buffer case and json_write_escaped() the streaming
 * case; they are the single implementation of that guarantee for every producer
 * in the firmware.
 *
 * This header is deliberately implementation-only (static inline, no .c file)
 * so it can be included from `main/` and from every component that has
 * `main/include` on its include path, without adding a link dependency.
 */

#ifndef JSON_ESCAPE_H
#define JSON_ESCAPE_H

#include <stddef.h>
#include <stdio.h>

/**
 * @brief Escape @p src into @p dst as the body of a JSON string literal
 * (without the surrounding quotes).
 *
 * Quotes and backslashes are escaped and a newline becomes "\\n"; every other
 * control character is dropped rather than escaped, because the fields this
 * builds - callsigns, paths, comments, decoded packet text - carry no
 * information in them and dropping keeps the output one byte per input byte for
 * the size accounting the callers do. Bytes at or above 0x20 are copied
 * through, including the high half of the range: APRS text is not required to
 * be UTF-8 and a byte-transparent copy is what lets a receiver reproduce
 * exactly what was heard.
 *
 * The escape never runs past @p dst: the loop stops while there is still room
 * for a two-character escape plus the terminating NUL, so a value that does not
 * fit is cut short at a character boundary and can never leave a lone backslash
 * (which would corrupt the whole document) as the last byte.
 *
 * @param src      Source text (NUL-terminated).
 * @param dst      Destination buffer, always left NUL-terminated.
 * @param dst_size Size of @p dst in bytes, including room for the NUL.
 * @return Number of characters written to @p dst, excluding the NUL.
 */
static inline size_t json_escape(const char *src, char *dst, size_t dst_size) {
    size_t di = 0;

    if (dst == NULL || dst_size == 0)
        return 0;

    if (src != NULL) {
        for (const char *p = src; *p && di + 2 < dst_size; p++) {
            unsigned char c = (unsigned char)*p;
            if (c == '"' || c == '\\') {
                dst[di++] = '\\';
                dst[di++] = (char)c;
            } else if (c == '\n') {
                dst[di++] = '\\';
                dst[di++] = 'n';
            } else if (c < 0x20) {
                continue;
            } else {
                dst[di++] = (char)c;
            }
        }
    }

    dst[di] = 0;
    return di;
}

/**
 * @brief Write @p v to @p f as a complete JSON string literal, quotes included.
 *
 * Uses the same minimal escaping cJSON's unformatted printer uses, so values
 * written this way round-trip through cJSON_Parse() when the file is loaded
 * back. Control characters with no short escape are emitted as "\\u00xx" rather
 * than dropped: these are configuration files, where a byte an operator managed
 * to store has to survive the save/load cycle intact. A NULL @p v is written as
 * an empty string, so a missing value still produces a well-formed document.
 *
 * @param f Open stream to write to.
 * @param v Text to write, or NULL.
 */
static inline void json_write_escaped(FILE *f, const char *v) {
    fputc('"', f);

    if (v != NULL) {
        for (const unsigned char *p = (const unsigned char *)v; *p; p++) {
            unsigned char ch = *p;
            switch (ch) {
                case '"':
                    fputs("\\\"", f);
                    break;
                case '\\':
                    fputs("\\\\", f);
                    break;
                case '\b':
                    fputs("\\b", f);
                    break;
                case '\f':
                    fputs("\\f", f);
                    break;
                case '\n':
                    fputs("\\n", f);
                    break;
                case '\r':
                    fputs("\\r", f);
                    break;
                case '\t':
                    fputs("\\t", f);
                    break;
                default:
                    if (ch < 0x20)
                        fprintf(f, "\\u%04x", ch);
                    else
                        fputc(ch, f);
            }
        }
    }

    fputc('"', f);
}

#endif /* JSON_ESCAPE_H */
