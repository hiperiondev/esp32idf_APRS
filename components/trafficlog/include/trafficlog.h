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

// Must be called once (e.g. from app_main / aprs_service_start) before the
// first trafficlog_add(). Safe to call more than once.
void trafficlog_init(void);

// Adds a printf-style formatted line to the ring buffer. Truncates silently
// if the formatted line is longer than the internal per-line buffer. This
// is the generic/legacy form: it fills the entry's message ("m") field and
// leaves DX/PACKET blank and AUDIO unset (-1), e.g. for connection-status
// lines ("Connected to APRS-IS ...").
void trafficlog_add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Adds a structured packet-traffic entry, used to populate the DX / PACKET
// / AUDIO columns of the dashboard traffic table:
//   dir       - short direction/type tag, e.g. "RX", "TX", "DIGI", "RX-IS"
//   dx        - station callsign this entry is associated with (may be "")
//   packet    - raw TNC2 packet text (may be "")
//   audio_mv  - demodulated audio level in mV RMS, or -1 if not available
//   sym_table - APRS symbol table byte ('/' or '\\'), or 0 if unknown
//   sym_code  - APRS symbol code byte, or 0 if unknown
// The message ("m") field is derived automatically as "<dir>: <packet>" so
// legacy/plain-text consumers keep working unchanged.
void trafficlog_add_pkt(const char *dir, const char *dx, const char *packet, int audio_mv, char sym_table, char sym_code);

// Serializes every buffered entry with seq > since_seq as a JSON object:
//   {"seq":<latest_seq>,"items":[
//     {"t":<ms_since_boot>,"m":"<line>","d":"<dir>","dx":"<callsign>",
//      "pkt":"<packet>","au":<audio_mv|-1>,"sym":"<code>-<table>|"}, ...
//   ]}
// "sym" mirrors the encoding used by lastheard_dump_json() (aprs.dprns.com
// icon naming: "<symbol_code>-<1_or_2>"), or "" when no symbol is known.
// into out (NUL-terminated, truncated to fit out_size). Returns the number
// of bytes written (excluding the NUL), or 0 on error.
size_t trafficlog_dump_json(uint32_t since_seq, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif // TRAFFICLOG_H
