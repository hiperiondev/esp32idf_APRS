/**
 * @file logcapture.h
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
 * @brief On-demand mirror of the serial console log into a small in-RAM ring
 * buffer, so the web admin's *Logs* page can show what a serial cable would
 * show without one being attached.
 *
 * @details
 * This lives in the webconfig component because the Logs page is its only
 * consumer: page_logs.c is the sole caller of everything declared here.
 *
 * The mirror is installed as an @c esp_log_set_vprintf() hook. The hook always
 * forwards to the console writer it replaced, so the serial output is
 * unchanged whether the mirror is capturing or not; when it is capturing it
 * additionally formats the same text, strips the ANSI colour sequences
 * ESP-IDF adds, splits it into lines and stores those lines in the ring.
 *
 * Capture is off by default and costs nothing while off: the ring is
 * allocated by logcapture_start() and released by logcapture_stop(), so the
 * ::LOGCAPTURE_CAPACITY x (::LOGCAPTURE_LINE_MAX + 1) bytes it occupies are
 * only held while an operator is actually watching the page. The hook itself
 * is installed on the first logcapture_start() and left in place afterwards,
 * because a task can be inside it at the moment capture is switched off and
 * uninstalling would race with that.
 *
 * Lines are tagged with an ever-increasing sequence number, so a web client
 * polls with "?since=<seq>" and receives only what it has not seen yet, the
 * same way the dashboard's traffic feed works.
 *
 * Capture is also self-limiting: it stops on its own after
 * ::LOGCAPTURE_IDLE_TIMEOUT_S without a logcapture_touch(), so a browser tab
 * that is closed, put to sleep or navigated away from never leaves the station
 * mirroring its log into a ring nobody reads.
 *
 * Thread safety: the hook may run on any task, and everything it touches is
 * held under a mutex it takes without blocking - a line that arrives while
 * another task holds the ring is dropped rather than delaying the task that
 * logged it. logcapture_start(), logcapture_stop(), logcapture_touch() and
 * logcapture_next_json() are meant to be called from the HTTP server task.
 */

#ifndef LOGCAPTURE_H
#define LOGCAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Longest line stored, in characters, terminating NUL excluded.
 *
 * @details A console line longer than this is continued on the next ring
 * entry rather than cut short, so no log text is lost to the limit; what the
 * limit bounds is the width of one entry, which is what the page renders as
 * one row.
 */
#define LOGCAPTURE_LINE_MAX 255

/**
 * @brief Number of lines kept in the ring, which is also the number of rows
 * the *Logs* page window holds.
 */
#define LOGCAPTURE_CAPACITY 50

/**
 * @brief Seconds without a logcapture_touch() after which capture stops on
 * its own.
 *
 * @details The page polls for new lines about once a second, so this is
 * several missed polls: long enough that a slow answer or a momentarily busy
 * station does not interrupt a running capture, short enough that a tab
 * closed without warning releases the ring promptly.
 */
#define LOGCAPTURE_IDLE_TIMEOUT_S 10

/**
 * @brief Upper bound, in bytes, on what logcapture_next_json() writes for a
 * single line, terminating NUL included.
 *
 * @details A caller streaming the feed sizes its per-line buffer with this
 * constant and is then guaranteed that no line is ever cut short or dropped
 * for want of room, however many of its characters need escaping. The value
 * covers the two enclosing quotes plus a worst case of two output characters
 * per stored character, and is checked against ::LOGCAPTURE_LINE_MAX by a
 * static assertion in logcapture.c.
 */
#define LOGCAPTURE_JSON_LINE_MAX 520

/**
 * @brief Begin mirroring the console log into the ring.
 *
 * @details Allocates the ring, installs the @c esp_log_set_vprintf() hook the
 * first time it is called, discards anything left from a previous capture and
 * arms the idle timeout. Calling it while capture is already running only
 * rearms the timeout, so a page reloaded twice does not accumulate state.
 *
 * @return @c true when capture is running on return, @c false when the ring
 *         could not be allocated, in which case nothing was installed and the
 *         console output is untouched.
 */
bool logcapture_start(void);

/**
 * @brief Stop mirroring and release the ring.
 *
 * @details Safe to call when capture is not running. The hook stays installed
 * and keeps forwarding to the console writer, so the serial log is unaffected
 * either way.
 */
void logcapture_stop(void);

/**
 * @brief Whether the mirror is currently capturing.
 *
 * @return @c true while lines are being stored.
 */
bool logcapture_is_running(void);

/**
 * @brief Rearm the idle timeout.
 *
 * @details Called by the endpoint that hands buffered lines to the browser,
 * so capture lives exactly as long as somebody keeps reading it. Does nothing
 * when capture is not running.
 */
void logcapture_touch(void);

/**
 * @brief Sequence number of the newest line currently in the ring.
 *
 * @details Intended as the upper bound a caller passes to
 * logcapture_next_json() as @p max_seq, so one pass over the feed reports a
 * fixed, self-consistent range even while other tasks keep logging behind it:
 * anything stored after the bound was taken is picked up by the next pass.
 *
 * @return Sequence number of the newest line, or 0 when the ring is empty or
 *         capture is not running.
 */
uint32_t logcapture_latest_seq(void);

/**
 * @brief Serialize the oldest buffered line whose sequence number lies in the
 * half-open range (@p after_seq, @p max_seq] as one JSON string, for a caller
 * streaming the feed one line per chunk.
 *
 * @details The result is a complete, quoted and escaped JSON string with no
 * leading or trailing separator - the caller supplies the commas and the
 * enclosing array - and is NUL-terminated.
 *
 * Call it in a loop, seeding @p after_seq with the sequence number the client
 * last saw and feeding the value returned through @p out_seq back in as
 * @p after_seq on the next iteration, until it returns 0. Because a returned
 * line always has a strictly greater sequence number than @p after_seq, that
 * loop is guaranteed to terminate. Lines the ring evicts part-way through the
 * walk are skipped rather than repeated or misordered, since the walk is
 * driven by sequence number and not by ring position.
 *
 * Nothing is escaped while the ring's lock is held: the selected line is
 * copied out first, so a long response cannot delay a task that is trying to
 * log.
 *
 * @param after_seq Exclusive lower bound: only a line with a greater sequence
 *                  number is considered. Pass 0 to start from the oldest line
 *                  still buffered.
 * @param max_seq   Inclusive upper bound, normally from
 *                  logcapture_latest_seq().
 * @param out       Destination buffer.
 * @param out_size  Size of @p out, in bytes. Must be at least
 *                  ::LOGCAPTURE_JSON_LINE_MAX; anything smaller writes nothing
 *                  and returns 0, rather than emitting a truncated string.
 * @param out_seq   Receives the sequence number of the line that was written,
 *                  or @p after_seq unchanged when none was. May be NULL.
 * @return Number of bytes written (excluding the NUL), or 0 when no line is
 *         left in range, the arguments are unusable, or the ring's lock could
 *         not be taken in time.
 */
size_t logcapture_next_json(uint32_t after_seq, uint32_t max_seq, char *out, size_t out_size, uint32_t *out_seq);

#ifdef __cplusplus
}
#endif

#endif // LOGCAPTURE_H
