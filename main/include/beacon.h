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
 * Periodically builds an APRS position report from each page's saved
 * lat/lon/altitude (g_config.trk_*, g_config.igate_*, g_config.digi_*) - or,
 * for the Tracker beacon with "Use live GPS fix" on, from the GNSS
 * receiver's current fix instead - and transmits it on RF
 * (aprs_service_send_tnc2) and/or to APRS-IS (igate_send_raw), per that
 * page's own loc2rf / loc2inet flags. This is what makes the station itself
 * show up (e.g. on aprs.fi) - the IGate/digipeater alone only relay traffic
 * they already hear, they never announce their own position on their own.
 *
 * The three beacons (tracker, igate, digi) each keep their own enable flag,
 * interval and next-due time, so they operate independently of one another,
 * but none of them owns a task: ::beacon_service transmits whichever are due
 * in a single pass, driven by the shared beacon scheduler
 * (beacon_scheduler.c).
 *
 * GPS/live-position beaconing is available on the Tracker beacon alone
 * ("Use live GPS fix", g_config.trk_use_live_gps): when enabled and the GNSS
 * receiver (gps.c) reports a current fix, the Tracker's position report
 * carries that live latitude/longitude/altitude/course/speed instead of the
 * page's fixed trk_lat/trk_lon/trk_alt; a disabled receiver or a momentary
 * loss of fix falls back to those fixed values for that beacon. The IGate
 * and Digipeater beacons remain fixed-station only, always sent at the fixed
 * interval configured on each page.
 *
 * With live GPS active, the Tracker beacon can additionally run
 * SmartBeaconing (g_config.trk_sb_enable, Hans-Gunnar Lundahl / HamHUD
 * algorithm): the beacon interval is interpolated between a slow (stationary)
 * and a fast (moving) rate according to current speed, and an independent
 * corner-pegging check forces an immediate beacon on a heading change past a
 * speed-scaled turn-angle/turn-slope threshold, subject to a minimum-turn-time
 * guard against re-triggering too soon. The IGate and Digipeater beacons have
 * no SmartBeaconing of their own.
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
