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
 * GPS/live-position and SmartBeaconing are not implemented here: these are
 * fixed-station beacons using each page's saved coordinates only.
 */

#ifndef BEACON_H
#define BEACON_H

#include <stdint.h>

/**
 * @brief Log the configured state of the Tracker, IGate, and Digipeater
 * beacons. The beacons themselves no longer run as three separate tasks: they
 * are driven by the shared beacon scheduler (beacon_scheduler_start()), which
 * calls ::beacon_service. Safe to call once from app startup.
 */
void beacon_start(void);

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
