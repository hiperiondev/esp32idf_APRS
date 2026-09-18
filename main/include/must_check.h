/**
 * @file must_check.h
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
 * @brief Single definition of the "the caller must look at what this returns"
 * function attribute.
 *
 * ESP-IDF's esp_attr.h carries placement and inlining attributes only, so the
 * compiler attribute is spelled out here once and shared by every header that
 * declares an operation whose failure is invisible unless the return value is
 * read - the persistence entry points, where a discarded result means the user
 * is told the write succeeded while the flash still holds the old content.
 *
 * Header-only and dependency-free: it is included from main/include, which is
 * already on the include path of every component that consumes those headers,
 * so no CMake change is needed to use it.
 */

#ifndef MUST_CHECK_H
#define MUST_CHECK_H

/**
 * @brief Marks a function whose return value a caller may not discard.
 *
 * Placed on a declaration, it makes the build emit -Wunused-result for any
 * call site that ignores the result. Applied to the persistence functions, it
 * is what keeps a future call site from silently reintroducing a "Saved"
 * response for a write that never reached flash.
 *
 * Expands to nothing on a compiler that does not implement the attribute, so
 * it never affects whether the project builds.
 */
#if defined(__GNUC__) || defined(__clang__)
#define APRS_MUST_CHECK __attribute__((warn_unused_result))
#else
#define APRS_MUST_CHECK
#endif

#endif /* MUST_CHECK_H */
