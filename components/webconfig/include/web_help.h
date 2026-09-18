/**
 * @file web_help.h
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
 * @brief Contextual help registry for the web admin: the label-to-help-text
 * table behind the little orange question mark that closes every option label,
 * and the renderer that emits one.
 *
 * Every option the form-field emitters in web_common.h render ends with a small
 * orange circled question mark. Resting the pointer on it - or giving it focus
 * from the keyboard, or tapping it on a touch screen - opens a balloon holding
 * a short explanation of what that option does. The marker holds no state of its
 * own, so an ordinary page load leaves every balloon closed again.
 *
 * Whether a balloon is open is decided by the stylesheet alone, from the
 * marker's own @c :hover and @c :focus. Where an open balloon is drawn is not:
 * it is a fixed-position layer over the whole page, so that it is painted whole
 * above every card, accordion and table frame rather than being clipped by the
 * one it was opened from, and the shared script emitted by web_send_footer() is
 * what measures the marker and hands that layer its coordinates.
 *
 * The help text of an option is looked up by its label rather than passed down
 * through every page. A page therefore keeps calling web_field_int(),
 * web_select_open() and the rest exactly as it did before, and the help arrives
 * together with the label - which also means the text for a label that several
 * pages share is written once and reads the same wherever it appears.
 * ::web_help_for_label() performs that lookup against the table in web_help.c,
 * which pairs each @c TR_xxx label macro with its @c TR_H_xxx help macro. A
 * label assembled at run time (a numbered alias row, a payload-type filter
 * checkbox) has no exact entry in that table; those call sites pass their help
 * explicitly, through the @c _h() emitter variants declared in web_common.h.
 *
 * Both the labels and the help strings live in the @c lang_xx.h translation
 * tables, so the balloon speaks the language the firmware was built for, like
 * every other string in the admin.
 */

#ifndef WEB_HELP_H
#define WEB_HELP_H

#include <stddef.h>

/**
 * @brief Longest help text, in BYTES, that ::web_help_markup() renders in full.
 *
 * The unit is bytes rather than characters because the translation tables are
 * UTF-8, so an accented Spanish or Italian character spends two of them. A
 * longer string is cut on a character boundary, never mid-sequence, so what
 * reaches the browser is always valid UTF-8.
 *
 * The bound is what keeps the help a help: a balloon is read at a glance, over
 * the control it explains, and a paragraph in it would be neither read nor
 * placeable beside a field on a phone. Anything needing more room than this
 * belongs in the documentation, which the option's page can point at instead.
 */
#define WEB_HELP_MAX_BYTES 253

/**
 * @brief Size of the buffer ::web_help_markup() escapes the help text into,
 * before wrapping it in markup.
 *
 * Six bytes per source byte: that is the worst the HTML escape can do, and it
 * is reached by a text made entirely of double quotes. web_help.c asserts at
 * compile time that this stays equal to six times ::WEB_HELP_MAX_BYTES, which
 * is what lets the value be written here as the plain literal that a @c printf
 * precision needs.
 */
#define WEB_HELP_ESCAPED_MAX 1518

/**
 * @brief Size, in bytes, of the buffer ::web_help_markup() writes into.
 *
 * ::WEB_HELP_ESCAPED_MAX of escaped text plus the fixed markup that wraps it
 * and the NUL, with headroom. Every emitter in web_common.c declares its help
 * buffer with exactly this size, so no single helper can clip a help string
 * the others render whole. web_help.c asserts at compile time that the
 * headroom really does cover the markup it emits.
 */
#define WEB_HELP_MARKUP_MAX 1742

/** @cond INTERNAL */
#define WEB_HELP_STRINGIFY_(x) #x
#define WEB_HELP_STRINGIFY(x)  WEB_HELP_STRINGIFY_(x)
/** @endcond */

/**
 * @brief Conversion specifier for a help slot: a @c %s bounded by
 * ::WEB_HELP_MARKUP_MAX, spliced into an emitter's format string.
 *
 * The bound has to reach the compiler as a literal precision, for the same
 * reason @c WEB_LABEL_FMT does in web_common.h: writing the precision as a
 * runtime @c "%.*s" argument makes GCC assume the slot can emit up to
 * @c INT_MAX bytes, which loses every @c -Wformat-truncation guarantee these
 * emitters are built to keep.
 */
#define WEB_HELP_FMT "%." WEB_HELP_STRINGIFY(WEB_HELP_MARKUP_MAX) "s"

/**
 * @brief Look up the help text registered for an option label.
 *
 * The table is searched for an entry whose label matches @p label exactly. A
 * label assembled at run time (for instance "Alias 2", built from a format
 * string and a row index) has no exact entry and yields NULL; those call sites
 * pass their help explicitly instead.
 *
 * @param label Option label as handed to the form-field emitter, or NULL.
 * @return The help text for that label, or NULL when the label has no entry.
 */
const char *web_help_for_label(const char *label);

/**
 * @brief Render the help marker for one option into @p dst.
 *
 * Writes the complete markup of the question mark and the balloon it opens: a
 * focusable @c span carrying the @c hlp class, with the HTML-escaped help text
 * inside a nested @c hlp-box. The classes are styled by web_handle_css(), which
 * is what makes the marker a circle, colours it orange, reveals the balloon on
 * hover, on focus and on tap, and lifts it out of the page flow so no card,
 * accordion or table frame can cut it short; the footer script then places that
 * balloon over the marker.
 *
 * A NULL or empty @p help writes an empty string, so an option with no
 * registered help simply renders without a marker rather than with an empty
 * balloon.
 *
 * @param dst      Destination buffer; ::WEB_HELP_MARKUP_MAX bytes is always
 *                 enough, whatever the help text.
 * @param dst_size Size of @p dst, in bytes.
 * @param help     Help text, or NULL for none.
 */
void web_help_markup(char *dst, size_t dst_size, const char *help);

#endif // WEB_HELP_H
