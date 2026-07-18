/**
 * @file time_sync.h
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
 * @brief SNTP time sync, configured entirely from the persisted System web admin
 * settings (g_config.synctime / g_config.ntp_host[0..2]).
 *
 * The system clock is always kept in UTC (TZ=UTC0) regardless of any timezone the
 * user has configured elsewhere, because APRS beacon timestamps ("051200z" in
 * beacon.c) must be zulu/UTC per the APRS spec - g_config.timeZone is left
 * untouched here for any future local-time display use, it is simply not applied
 * to the system clock.
 */

#ifndef TIME_SYNC_H
#define TIME_SYNC_H

/**
 * @brief Start (or skip, per g_config.synctime) the SNTP client against
 * all 3 configured hosts in g_config.ntp_host[0..2] (empty slots are
 * skipped; if all 3 are empty, falls back to pool.ntp.org). All non-empty
 * hosts are registered with esp_netif's SNTP wrapper at once, which tries
 * them in turn, so a single unreachable/blocked host doesn't stall the
 * whole sync. Non-blocking: kicks off the sync and returns immediately,
 * then keeps re-syncing periodically in the background (via the SNTP
 * component's own internal task) for as long as the device has network
 * access. Call once from app startup, after wifi_init().
 *
 * Periodic resync is requested every 10 s; lwip's sntp module enforces a
 * hard 15 s floor (SNTP_UPDATE_DELAY) on top of any polling library's
 * server's own rate-limit etiquette, so the effective interval is 15 s.
 * Polling a public pool.ntp.org server faster than that is against its usage
 * policy and can get your IP rate-limited/blocked - point ntp_host at a
 * local/private NTP server if you need a tighter interval than that.
 */
void time_sync_start(void);

#endif // TIME_SYNC_H
