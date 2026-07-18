/**
 * @file translations.h
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
 * @brief Compile-time language selector.
 *
 * How it works:
 *   - app_config.h defines the numeric language codes (LANG_EN, LANG_ES, ...)
 *     and the active `LANGUAGE` macro (defaulting to LANG_EN if not set).
 *   - This header is the ONLY place that decides which lang_xx.h file gets
 *     compiled in. Based on the value of LANGUAGE, exactly one lang_xx.h is
 *     #included. All the others are never seen by the compiler/preprocessor,
 *     so only one language's strings ever end up in the firmware image.
 *   - Every translatable string in the codebase is referenced through a
 *     TR_xxx macro (e.g. TR_MENU_DASHBOARD, TR_BTN_SAVE). Each lang_xx.h
 *     defines the full set of TR_xxx macros as plain C string literals in
 *     that language.
 *
 * To add a new language:
 *   1. Copy translations/lang_en.h to translations/lang_xx.h and translate
 *      every string (keep every TR_xxx macro name identical - only the
 *      string literal contents change).
 *   2. Add a `#define LANG_XX <next_free_number>` in app_config.h.
 *   3. Add an `#elif LANGUAGE == LANG_XX` branch below that includes it.
 *   4. Build with `-DLANGUAGE=LANG_XX` (or edit the default in app_config.h).
 *
 * To add a new translatable string:
 *   1. Add a new TR_xxx definition to EVERY lang_xx.h file (missing a macro
 *      in one language causes a compile error in that language build, which
 *      is intentional - it stops untranslated strings from shipping silently).
 *   2. Use TR_xxx in the C code wherever the literal used to be.
 */

#ifndef TRANSLATIONS_H
#define TRANSLATIONS_H

#include "app_config.h" // for LANGUAGE, LANG_EN, LANG_ES, ...

#if LANGUAGE == LANG_EN
#include "lang_en.h"
#elif LANGUAGE == LANG_ES
#include "lang_es.h"
#else
#error "translations.h: unknown LANGUAGE selected. Define LANGUAGE as LANG_EN or LANG_ES in app_config.h (or add a new lang_xx.h + branch here)."
#endif

#endif // TRANSLATIONS_H
