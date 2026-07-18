/*
 * lastheard.h - small in-RAM ring buffer of decoded RF stations, used to
 * feed the "LAST HEARD" table on the web dashboard.
 *
 * Every decoded AX.25 frame recorded here also increments a per-callsign
 * packet counter so the dashboard can show how many times each station has
 * been heard, the same way the reference project's "PACKET" column does.
 *
 * Thread-safe: lastheard_add() may be called from any task (radio RX
 * callback). Entries are timestamped with time(NULL) (wall clock, once NTP
 * has synced) so the web client can render a human time-of-day.
 */
#ifndef LASTHEARD_H
#define LASTHEARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Must be called once (e.g. from aprs_service_start) before the first
// lastheard_add(). Safe to call more than once.
void lastheard_init(void);

/**
 * @brief Record one heard station.
 *
 * @param callsign   Source callsign, e.g. "HS5TQA-7" (already includes SSID
 *                    if non-zero).
 * @param path       Digipeat path as shown after the source call, e.g.
 *                    "WIDE1-1" or "DIRECT" (no leading/trailing comma).
 * @param via_rf     true if heard on RF, false if it only arrived via
 *                    APRS-IS (INET). Rendered as the "RF:"/"INET:" prefix on
 *                    the path column, matching the reference dashboard.
 * @param sym_table  APRS symbol table byte ('/' or '\' or overlay char), 0 if
 *                    unknown/not a position packet.
 * @param sym_code   APRS symbol code byte, 0 if unknown/not a position packet.
 */
void lastheard_add(const char *callsign, const char *path, bool via_rf, char sym_table, char sym_code);

// Serializes the buffered stations (most recent first) as a JSON array:
//   [{"time":"HH:MM:SS","call":"HS5TQA-7","path":"RF: WIDE1-1",
//     "sym":"91-1","packets":3}, ...]
// into out (NUL-terminated, truncated to fit out_size). Returns the number
// of bytes written (excluding the NUL), or 0 on error.
size_t lastheard_dump_json(char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif // LASTHEARD_H
