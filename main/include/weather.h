/**
 * @file weather.h
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
 * @brief Own-station APRS Weather Report subsystem.
 *
 * Owns the single shared ::weather_telemetry_data_t container that every local
 * sensor driver writes into, refreshes it from the ::sensors_local registry once
 * per second, and periodically encodes and transmits a standard APRS Weather
 * Report (RF and/or APRS-IS) from the fields the operator mapped on the Weather
 * web admin page (g_config.wx_*).
 */

#ifndef WEATHER_H
#define WEATHER_H

#include <stddef.h>
#include <stdint.h>

#include "weather_telemetry.h"

/**
 * @brief The one shared weather/telemetry container for this station.
 *
 * Created and owned by weather.c. Local sensor drivers registered in the
 * ::sensors_local registry write their freshly measured values directly into
 * @c weather_telemetry_data.weather[0] (and optionally telemetry_report[0])
 * every time ::weather_refresh_now runs (once per second). The weather beacon
 * task then reads it under the module lock to build the on-air report.
 *
 * Declared here so other components (e.g. a future telemetry sender, a
 * dashboard page) can read the latest sample; treat it as read-only outside
 * weather.c and always go through ::weather_lock / ::weather_unlock when
 * reading fields that a driver may be updating concurrently.
 */
extern weather_telemetry_data_t weather_telemetry_data;

/**
 * @brief Bring up the weather subsystem: allocate the shared container's
 *        backing storage, initialise the ::sensors_local registry, and start
 *        the 1 Hz sensor-refresh task and the WX beacon task.
 *
 * Safe to call once from application start-up (after config load). The beacon
 * task idles and re-checks periodically when g_config.wx_en is false or when
 * neither wx_2rf nor wx_2inet is set, so toggling those in the web admin takes
 * effect without a reboot.
 */
void weather_start(void);

/**
 * @brief Service the WX beacon: transmit an APRS Weather Report if one is due,
 * and return the number of seconds until it next needs servicing (always >= 1).
 *
 * Uses g_config.wx_* for enable/legs/interval, keeping the same behaviour the
 * old dedicated WX beacon task had. Intended to be called only from the shared
 * beacon scheduler task (beacon_scheduler.c). The 1 Hz sensor-refresh task set
 * up by ::weather_start is unaffected and keeps running on its own.
 */
uint32_t weather_beacon_service(void);

/**
 * @brief Refresh the shared weather container from the local sensor registry.
 * Must be called at ~1 Hz. Driven by the APRS service's existing 1 Hz tick
 * (serviceTickTask), so the weather subsystem does not need its own
 * sensor-refresh task. Safe to call before any sensor exists (no-op refresh).
 */
void weather_service_1hz(void);

/** @brief Take the lock guarding ::weather_telemetry_data. */
void weather_lock(void);

/** @brief Release the lock guarding ::weather_telemetry_data. */
void weather_unlock(void);

/**
 * @brief Build an APRS Weather Report from the latest cached reading, on
 * demand, without waiting for or disturbing the periodic WX beacon's own
 * interval. Used by the query responder's "?WX?" reply (components/query).
 *
 * Resolves the shared weather container the same way weather_beacon_service()
 * does (per-field enable mask, averaging where configured) and encodes it
 * with the same builder the periodic beacon uses, so the reply is
 * byte-for-byte consistent with a normal WX beacon transmission.
 *
 * @param path    TNC2 path suffix placed between the destination address and
 *                the ':', leading comma included, chosen by the caller for
 *                the leg the line is transmitted on: the digipeater suffix
 *                aprs_path_build_suffix_from_config() builds for an RF
 *                transmission, or ::APRS_PATH_TCPIP_SUFFIX for an APRS-IS
 *                one. Pass "" for no path at all.
 * @param out     Destination buffer for the built TNC2 text line.
 * @param out_max Size of @p out in bytes; ::APRS_TNC2_BUF_SIZE is the size
 *                every other packet builder in this codebase uses.
 * @return Packet length, or 0 if nothing usable is configured (no Weather or
 *         APRS callsign set) or the built line does not fit @p out_max /
 *         APRS_TNC2_MAX_LEN.
 */
int weather_build_report_packet(const char *path, char *out, size_t out_max);

#endif // WEATHER_H
