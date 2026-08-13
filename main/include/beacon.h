/**
 * @file beacon.h
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
 * @brief Own-station position beacons (Tracker / IGate / Digipeater web admin
 * pages).
 *
 * Periodically builds an APRS position report from the fixed lat/lon/altitude
 * saved by each page (g_config.trk_*, g_config.igate_*, g_config.digi_*) and
 * transmits it on RF (aprs_service_send_tnc2) and/or to APRS-IS
 * (igate_send_raw), per that page's own loc2rf / loc2inet flags. This is what
 * makes the station itself show up (e.g. on aprs.fi) - the IGate/digipeater alone
 * only relay traffic they already hear, they never announce their own position on
 * their own.
 *
 * Each of the three beacons (tracker, igate, digi) runs as its own FreeRTOS task
 * with its own enable flag and interval, so they operate completely independently
 * of one another.
 *
 * GPS/live-position beaconing is not implemented here: these are fixed-station
 * beacons using each page's saved coordinates only, sent at the fixed interval
 * configured on each page. SmartBeaconing is not implemented.
 */

#ifndef BEACON_H
#define BEACON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Log the configured state of the Tracker, IGate, and Digipeater
 * beacons. The beacons themselves do not run as three separate tasks: they
 * are driven by the shared beacon scheduler (beacon_scheduler_start()), which
 * calls ::beacon_service. Safe to call once from app startup.
 */
void beacon_start(void);

/**
 * @brief Build the same APRS position report the IGate position beacon
 * transmits (g_config.igate_*), on demand, for reuse by any caller that needs
 * a byte-for-byte consistent copy of it outside the beacon's own interval -
 * currently the query responder's "?APRS?" reply (components/query).
 *
 * Snapshots every g_config.igate_* field this needs under app_config_lock(),
 * exactly like igateBeaconService() does internally, so a concurrent web save
 * cannot be observed mid-write.
 *
 * @param path    TNC2 path suffix placed between the destination address and
 *                the ':', leading comma included, chosen by the caller for
 *                the leg the line is transmitted on: the digipeater suffix
 *                aprs_path_build_suffix() builds for an RF transmission, or
 *                ::APRS_PATH_TCPIP_SUFFIX for an APRS-IS one. Pass "" for no
 *                path at all.
 * @param out     Destination buffer for the built TNC2 text line.
 * @param out_max Size of @p out in bytes; ::APRS_TNC2_BUF_SIZE is the size
 *                every other packet builder in this codebase uses.
 * @return Packet length, or 0 if nothing usable is configured (no IGate
 *         callsign set) or the built line does not fit @p out_max /
 *         APRS_TNC2_MAX_LEN.
 */
int beacon_build_igate_position_packet(const char *path, char *out, size_t out_max);

/**
 * @brief Parse an APRS "PHGphgd" Data Extension (APRS101 ch.7) from the start
 * of a received information-field slice, including the optional PHGR
 * "probes" beacon-rate character and its mandatory trailing slash (1.2
 * addition, aprs.org/aprs12/probes.txt).
 *
 * The standard form is the 7-byte "PHGphgd" token; the probe form inserts
 * one extra character - the beacons-per-hour rate, '0'-'9' then 'A' upward
 * for 10 and above - immediately followed by a mandatory '/' before the
 * free-text comment. Both forms are accepted; whichever one is present,
 * @p comment_out is left pointing at the first byte of the comment/free text
 * that follows the extension, never at a stray rate character or separator.
 *
 * @param field       NUL-terminated slice starting at the 'P' of "PHG".
 * @param phg_digits  Receives the 4-digit "phgd" code (power, height, gain,
 *                    directivity) as a NUL-terminated string; must be
 *                    >= 5 bytes.
 * @param rate_out    Receives the decoded beacons-per-hour rate when the
 *                    probe form is present, or -1 when the plain 7-byte form
 *                    is present instead. May be NULL if the caller does not
 *                    need the rate.
 * @param comment_out Receives a pointer into @p field at the first byte
 *                    after the parsed extension. May be NULL.
 * @return true if @p field begins with a well-formed "PHGphgd" token, false
 *         if it is too short or does not start with "PHG".
 */
bool beacon_parse_phg_extension(const char *field, char phg_digits[5], int *rate_out, const char **comment_out);

/**
 * @brief Build the same APRS status report the IGate status beacon transmits
 * (g_config.igate_status, plus the Maidenhead locator block when
 * g_config.status_grid_en is set), on demand - currently for the query
 * responder's "?APRSS" reply (components/query).
 *
 * Snapshots every g_config field it needs under app_config_lock(), exactly
 * like igateStatusService() does internally.
 *
 * @param path    TNC2 path suffix placed between the destination address and
 *                the ':', leading comma included, chosen by the caller for
 *                the leg the line is transmitted on: the digipeater suffix
 *                aprs_path_build_suffix() builds for an RF transmission, or
 *                ::APRS_PATH_TCPIP_SUFFIX for an APRS-IS one. Pass "" for no
 *                path at all.
 * @param out     Destination buffer for the built TNC2 text line.
 * @param out_max Size of @p out in bytes; ::APRS_TNC2_BUF_SIZE is the size
 *                every other packet builder in this codebase uses.
 * @return Packet length, or 0 if no IGate callsign or no status text is
 *         configured, or the built line does not fit @p out_max /
 *         APRS_TNC2_MAX_LEN.
 */
int beacon_build_igate_status_packet(const char *path, char *out, size_t out_max);

/**
 * @brief Service all three position beacons (Tracker / IGate / Digipeater) in
 * one pass, transmitting any that are due, and return the number of seconds
 * until the soonest one next needs servicing (always >= 1).
 *
 * Each beacon keeps its own enable flags and interval; a disabled beacon is a
 * cheap no-op that returns a short re-check interval so toggling it on in the
 * web admin still takes effect without a reboot. Intended to be called only
 * from the shared beacon scheduler task.
 */
uint32_t beacon_service(void);

#endif // BEACON_H
