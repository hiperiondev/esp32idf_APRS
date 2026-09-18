/**
 * @file trafficlog.h
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
 * @brief Small in-RAM ring buffer that mirrors the same "traffic" lines the
 * firmware already prints on the serial console (APRS-IS TX/RX, RF RX,
 * digipeated frames, INET->RF frames, ...) so the web UI can show a live feed
 * without needing a serial cable.
 *
 * Each entry carries both a free-form printf-style message (for backward
 * compatibility / connection-status style log lines) and, when available, the
 * structured fields shown by the reference esp32idf_APRS dashboard's traffic
 * table: DX (the station/callsign the entry is associated with), PACKET (the raw
 * TNC2 packet text) and AUDIO (the demodulated audio level in mV RMS, or -1 when
 * not applicable/available, e.g. for TX-only or APRS-IS-only lines).
 *
 * Thread-safe: trafficlog_add() / trafficlog_add_pkt() may be called from any
 * task (radio RX callback, igate task, digi processing, etc). Entries are
 * timestamped with esp_timer_get_time() (microseconds since boot) and tagged with
 * an ever-increasing sequence number so a web client can long-poll only the lines
 * it hasn't seen yet ("?since=<seq>").
 */

#ifndef TRAFFICLOG_H
#define TRAFFICLOG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the traffic-log ring buffer. Must be called once (e.g.
 * from app_main / aprs_service_start()) before the first trafficlog_add().
 * Safe to call more than once.
 */
void trafficlog_init(void);

/**
 * @brief Add a printf-style formatted line to the ring buffer.
 *
 * Truncates silently if the formatted line is longer than the internal
 * per-line buffer. This is the generic form: it fills the entry's
 * message ("m") field and leaves DX/PACKET blank and AUDIO unset (-1), e.g.
 * for connection-status lines ("Connected to APRS-IS ...").
 *
 * @param fmt printf-style format string, followed by its arguments.
 */
void trafficlog_add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief Add a structured packet-traffic entry, populating the DX / PACKET /
 * AUDIO columns of the dashboard traffic table.
 *
 * The message ("m") field is derived automatically as "<dir>: <packet>" so
 * plain-text consumers work unchanged.
 *
 * @param dir       Short direction/type tag, e.g. "RX", "TX", "DIGI", "RX-IS".
 * @param dx        Station callsign this entry is associated with (may be "").
 * @param packet    Raw TNC2 packet text (may be "").
 * @param decoded   One-line summary of the fields read out of the payload -
 *                  timestamp, course, speed, altitude, radio range, PHG -
 *                  as produced by aprs_filter_format_report(). NULL or ""
 *                  leaves the DECODED column of the entry empty, which is
 *                  what a payload carrying none of those fields yields.
 * @param audio_mv  Demodulated audio level in mV RMS, or -1 if not available.
 * @param sym_table APRS symbol table byte ('/' or '\\'), or 0 if unknown.
 * @param sym_code  APRS symbol code byte, or 0 if unknown.
 */
void trafficlog_add_pkt(const char *dir, const char *dx, const char *packet, const char *decoded, int audio_mv, char sym_table, char sym_code);

/**
 * @brief Upper bound, in bytes, on what trafficlog_next_json() writes for a
 * single entry, terminating NUL included.
 *
 * @details A caller streaming the feed sizes its per-entry buffer with this
 * constant and is then guaranteed that no entry is ever cut short or dropped
 * for want of room, however long the packet text is and however many of its
 * characters need escaping. The value is derived from the private field widths
 * of a ring entry and is checked against them by a static assertion in
 * trafficlog.c, so it cannot fall behind if those widths change.
 */
#define TRAFFICLOG_JSON_ENTRY_MAX 960

/**
 * @brief Sequence number of the newest entry currently in the ring buffer.
 *
 * @details Intended as the upper bound a caller passes to
 * trafficlog_next_json() as @c max_seq, so one pass over the feed reports a
 * fixed, self-consistent range even while the radio and network tasks keep
 * adding entries behind it: anything stored after the bound was taken is simply
 * picked up by the next pass.
 *
 * The counter is a single naturally-aligned word, so this reads it without
 * taking the ring's lock. The value can therefore be one entry behind an
 * insertion that is in progress on another task, which is exactly the same
 * outcome as an insertion landing a microsecond later, and never exposes a
 * half-filled entry: trafficlog_next_json() does take the lock, and the task
 * filling an entry holds it until every field is written.
 *
 * @return Sequence number of the newest entry, or 0 if none has been stored
 *         since boot.
 */
uint32_t trafficlog_latest_seq(void);

/**
 * @brief Serialize the oldest buffered entry whose sequence number lies in the
 * half-open range (@p after_seq, @p max_seq] as one JSON object, for a caller
 * streaming the feed one entry per chunk.
 *
 * @details The object has the form
 * @c {"t":<ms_since_boot>,"m":"<line>","d":"<dir>","dx":"<callsign>","pkt":"<packet>","au":<audio_mv|-1>,"sym":"<code>-<table>"},
 * with no leading or trailing separator - the caller supplies the commas and
 * the enclosing array. "sym" mirrors the encoding used by
 * lastheard_dump_json() ("<symbol_code>-<1_or_2>"), or "" when no symbol is
 * known. The result is NUL-terminated.
 *
 * Call it in a loop, seeding @p after_seq with the sequence number the client
 * last saw and feeding the value returned through @p out_seq back in as
 * @p after_seq on the next iteration, until it returns 0. Because a returned
 * entry always has a strictly greater sequence number than @p after_seq, that
 * loop is guaranteed to terminate. Entries the ring evicts part-way through the
 * walk are skipped rather than repeated or misordered, since the walk is driven
 * by sequence number and not by ring position.
 *
 * Nothing is formatted while the ring's lock is held: the selected entry is
 * copied out first, so a long response cannot delay the radio or network tasks
 * that add entries.
 *
 * @param after_seq Exclusive lower bound: only an entry with a greater sequence
 *                  number is considered. Pass 0 to start from the oldest entry
 *                  still buffered.
 * @param max_seq   Inclusive upper bound, normally from trafficlog_latest_seq().
 * @param out       Destination buffer.
 * @param out_size  Size of @p out, in bytes. Must be at least
 *                  ::TRAFFICLOG_JSON_ENTRY_MAX; anything smaller writes nothing
 *                  and returns 0, rather than emitting a truncated object.
 * @param out_seq   Receives the sequence number of the entry that was written,
 *                  or @p after_seq unchanged when none was. May be NULL.
 * @return Number of bytes written (excluding the NUL), or 0 when no entry is
 *         left in range, the arguments are unusable, or the ring's lock could
 *         not be taken in time.
 */
size_t trafficlog_next_json(uint32_t after_seq, uint32_t max_seq, char *out, size_t out_size, uint32_t *out_seq);

#ifdef __cplusplus
}
#endif

#endif // TRAFFICLOG_H
