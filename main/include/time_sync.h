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
 * The system clock is always kept in UTC (TZ=UTC0), because APRS beacon
 * timestamps ("051200z" in beacon.c) must be zulu/UTC per the APRS spec. No
 * local-time offset is applied anywhere in the firmware.
 */

#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

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
 *
 * This does not spawn its own task: it just arms a small state machine that
 * time_sync_1hz() advances from the APRS service's shared 1 Hz tick, saving a
 * dedicated 4 KB stack.
 */
void time_sync_start(void);

/**
 * @brief Advance the SNTP bootstrap one step. Must be called once per second
 * (from serviceTickTask in aprs_service.c). Non-blocking - it only steps the
 * state machine armed by time_sync_start(): waits for connectivity, initializes
 * SNTP, requests the first sync and retries every 30 s until it lands, then
 * parks (esp_netif_sntp keeps the clock in sync on its own afterwards). A no-op
 * when time sync is disabled or already done.
 */
void time_sync_1hz(void);

/**
 * @brief One entry of the built-in timezone table: a display name and its
 * POSIX TZ rule string.
 */
typedef struct {
    const char *name;     /**< Display name for the System page's timezone select, leading with the signed UTC offset followed by the country (or, for
                            countries spanning more than one offset, the standard zone name), e.g. "UTC-5 Eastern Time (USA, Canada)". */
    const char *posix_tz; /**< POSIX TZ rule string, as consumed by setenv("TZ", ...) / tzset(). */
} time_sync_tz_t;

/**
 * @brief Number of entries in the built-in timezone table (see
 * time_sync_tz_name() / time_sync_format_local()).
 *
 * @return Entry count. Valid indices are 0 .. time_sync_tz_count()-1.
 */
uint8_t time_sync_tz_count(void);

/**
 * @brief Display name of one entry of the built-in timezone table, for
 * populating the System page's timezone @c <select>.
 *
 * @param idx Table index, 0 .. time_sync_tz_count()-1. Any other value is
 *            treated as 0 (UTC).
 * @return Short, human-readable zone label (e.g. "UTC-5 Eastern Time (USA,
 *         Canada)"). Static storage; never NULL.
 */
const char *time_sync_tz_name(uint8_t idx);

/**
 * @brief Render @p utc as a local date/time string for timezone table entry
 * @p idx, e.g. "2026-08-06 08:42:07".
 *
 * @details The system clock stays UTC everywhere else in the firmware (see
 * the file-level note above) - this only converts a UTC timestamp to local
 * civil time for display, without touching how any other part of the
 * firmware reads or stores time. The rendered string carries no timezone
 * name or abbreviation, only the resulting date and time, since the table
 * entry the caller selected is the sole source of the offset applied. The
 * process-wide @c TZ environment variable is borrowed for the duration of
 * the call and restored to @c UTC0 immediately afterwards, under an internal
 * lock, so a concurrent caller (or the SNTP client) never observes an
 * unexpected @c TZ value.
 *
 * @param utc      UTC timestamp to convert (as from @c time(NULL)).
 * @param idx      Timezone table index, 0 .. time_sync_tz_count()-1. Any
 *                 other value is treated as 0 (UTC).
 * @param out      Destination buffer for the formatted string.
 * @param out_size Size of @p out, in bytes.
 */
void time_sync_format_local(time_t utc, uint8_t idx, char *out, size_t out_size);

#endif // TIME_SYNC_H
