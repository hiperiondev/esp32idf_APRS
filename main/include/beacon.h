/*
 * beacon.h - own-station position beacons (Tracker / IGate / Digipeater web
 * admin pages).
 *
 * Periodically builds an APRS position report from the fixed lat/lon/altitude
 * saved by each page (g_config.trk_*, g_config.igate_*, g_config.digi_*) and
 * transmits it on RF (APRS_sendTNC2Pkt) and/or to APRS-IS (igate_send_raw),
 * per that page's own loc2rf / loc2inet flags. This is what makes the
 * station itself show up (e.g. on aprs.fi) - the IGate/digipeater alone only
 * relay traffic they already hear, they never announce their own position on
 * their own.
 *
 * Each of the three beacons (tracker, igate, digi) runs as its own FreeRTOS
 * task with its own enable flag and interval, so they operate completely
 * independently of one another.
 *
 * GPS/live-position and SmartBeaconing are not implemented here: these are
 * fixed-station beacons using each page's saved coordinates only.
 */
#ifndef BEACON_H
#define BEACON_H

/**
 * @brief Start the Tracker, IGate, and Digipeater beacon tasks. Each task is
 * a no-op-ish idle loop if its own enable flag (g_config.trk_en /
 * g_config.igate_en + igate_bcn / g_config.digi_en + digi_bcn) is false, or
 * if both its own loc2rf/loc2inet flags are false - it just idles and
 * re-checks periodically, so toggling these on later in the web admin takes
 * effect without a reboot). Safe to call once from app startup.
 */
void beacon_start(void);

#endif // BEACON_H
