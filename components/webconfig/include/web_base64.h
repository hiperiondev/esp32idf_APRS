/**
 * @file web_base64.h
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
 * @brief Standalone RFC 4648 base64 decoder used to unwrap the credential
 * pair carried in an HTTP `Authorization: Basic` header.
 *
 * The web admin interface never terminates TLS and never parses an X.509
 * certificate: httpd_ssl and its mbedtls-based TLS stack are not part of this
 * firmware's attack surface or its dependency graph. Pulling in the mbedtls
 * component for that single call - `mbedtls_base64_decode()` - would drag the
 * full TLS/X.509/PK/ASN.1 object set into every image just to decode roughly
 * a hundred bytes of credential text, which is why that decode is implemented
 * here instead, with no dependency beyond the C standard library.
 *
 * This header is deliberately implementation-only (static inline, no .c file)
 * so it can be included wherever the decode is needed without adding a link
 * dependency.
 */

#ifndef WEB_BASE64_H
#define WEB_BASE64_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Decode one RFC 4648 base64 input octet into its 6-bit value.
 *
 * @param c Input octet to classify.
 * @return The 6-bit value of @p c if it is one of the 64 base64 alphabet
 *         characters, or -1 for any other byte (including the `=` padding
 *         character, which callers handle separately).
 */
static inline int web_base64_sextet(unsigned char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

/**
 * @brief Decode an RFC 4648 base64 string into raw bytes.
 *
 * Accepts standard (`+`/`/`) base64 with optional `=` padding. Whitespace and
 * any other byte outside the 64-character alphabet is rejected rather than
 * skipped, so malformed or truncated `Authorization` headers are reported as
 * errors instead of silently decoding to the wrong credential bytes.
 *
 * @param out Destination buffer for the decoded bytes.
 * @param out_size Total size of @p out in bytes.
 * @param out_len Set to the number of bytes written to @p out on success.
 *                Left unmodified on failure.
 * @param in Base64-encoded input; need not be NUL-terminated.
 * @param in_len Length of @p in in bytes.
 * @return 0 on success, -1 if @p in is not valid base64, and -2 if the
 *         decoded output would not fit in @p out.
 */
static inline int web_base64_decode(unsigned char *out, size_t out_size, size_t *out_len, const unsigned char *in, size_t in_len) {
    if (out == NULL || out_len == NULL || in == NULL)
        return -1;

    // A well-formed input is a multiple of 4 characters, with padding (if
    // any) only in the final group.
    if (in_len == 0 || in_len % 4 != 0)
        return -1;

    size_t written = 0;
    for (size_t i = 0; i < in_len; i += 4) {
        bool pad2 = in[i + 2] == '=';
        bool pad3 = in[i + 3] == '=';

        // '=' is only legal as a trailing run in the last group, and never in
        // the first two positions of a group.
        if (in[i] == '=' || in[i + 1] == '=' || (pad2 && !pad3) || ((pad2 || pad3) && i + 4 != in_len))
            return -1;

        int s0 = web_base64_sextet(in[i]);
        int s1 = web_base64_sextet(in[i + 1]);
        int s2 = pad2 ? 0 : web_base64_sextet(in[i + 2]);
        int s3 = pad3 ? 0 : web_base64_sextet(in[i + 3]);
        if (s0 < 0 || s1 < 0 || (!pad2 && s2 < 0) || (!pad3 && s3 < 0))
            return -1;

        size_t group_bytes = pad2 ? 1 : (pad3 ? 2 : 3);
        if (written + group_bytes > out_size)
            return -2;

        out[written++] = (unsigned char)((s0 << 2) | (s1 >> 4));
        if (!pad2)
            out[written++] = (unsigned char)((s1 << 4) | (s2 >> 2));
        if (!pad3)
            out[written++] = (unsigned char)((s2 << 6) | s3);
    }

    *out_len = written;
    return 0;
}

#endif /* WEB_BASE64_H */
