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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_config.h" // wx_field_id_t
#include "weather_telemetry.h"

/**
 * @brief The one shared weather/telemetry container for this station.
 *
 * Created and owned by weather.c. Local sensor drivers registered in the
 * ::sensors_local registry write their freshly measured values directly into
 * @c weather_telemetry_data.weather[0] (and optionally telemetry_report[0])
 * every time ::weather_service_1hz runs (once per second). The WX beacon then
 * reads it under the module lock to build the on-air report.
 *
 * Declared here so other components (e.g. a future telemetry sender, a
 * dashboard page) can read the latest sample; treat it as read-only outside
 * weather.c and always go through ::weather_lock / ::weather_unlock when
 * reading fields that a driver may be updating concurrently.
 */
extern weather_telemetry_data_t weather_telemetry_data;

/**
 * @brief Bring up the weather subsystem: wire the shared container to its
 *        backing storage, create the module lock, and initialise the
 *        ::sensors_local registry together with every auto-registered driver.
 *
 * Creates no task of its own. The WX beacon is serviced by the shared beacon
 * scheduler through ::weather_beacon_service, and the sensor refresh by the
 * APRS service tick through ::weather_service_1hz.
 *
 * Safe to call once from application start-up (after config load). Both
 * services re-read g_config on every pass, so toggling g_config.wx_en,
 * wx_2rf or wx_2inet in the web admin takes effect without a reboot.
 */
void weather_start(void);

/**
 * @brief Service the WX beacon: transmit an APRS Weather Report if one is due,
 * and return the number of seconds until it next needs servicing (always >= 1).
 *
 * Uses g_config.wx_* for enable/legs/interval. Intended to be called only from
 * the shared beacon scheduler task (beacon_scheduler.c); the 1 Hz sensor
 * refresh is driven separately by ::weather_service_1hz.
 * @return Seconds until this beacon next needs servicing, always >= 1.
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

/**
 * @name Single-field read-out
 *
 * Read one weather field of the shared container without going through the
 * on-air encoder, for the callers that present readings to a person instead
 * of transmitting them (the Telegram bot's `/sensors` answer).
 *
 * The three helpers split what such a caller needs: the name of the field,
 * whether the container currently carries it and with which value, and how
 * that value reads in International System units. Keeping them here rather
 * than in the caller is what stops the field tables from being copied a third
 * time - weather.c already owns the one that decides presence and value for
 * the encoder, and these expose it.
 * @{
 */

/**
 * @brief Fixed English name of one weather field.
 *
 * Untranslated on purpose: the web admin has its own translated table
 * (::wx_field_id_t indexed, in the webconfig pages), while this is what a
 * caller uses when it has no locale to work from.
 *
 * @param field Field to name.
 * @return The field's name, or "" when @p field is out of range. Valid for the
 *         program's lifetime.
 */
const char *weather_field_label(wx_field_id_t field);

/**
 * @brief Read one field of the shared weather container.
 *
 * Takes ::weather_lock internally, so the caller must not already hold it.
 *
 * @param[in]  field     Field to read.
 * @param[out] out_value Receives the field's value in the units the APRS
 *                       Weather Report format itself uses on air (degrees
 *                       Fahrenheit, miles per hour, hundredths of an inch,
 *                       tenths of a millibar - see weather_telemetry.h).
 *                       Untouched when the field is not present.
 * @return true when the container currently carries this field, i.e. when its
 *         selected sensor reported it on the last refresh; false otherwise,
 *         including for an out-of-range @p field.
 */
bool weather_field_snapshot(wx_field_id_t field, double *out_value);

/**
 * @brief Render one field's value as human-readable text in International
 *        System units, measurement unit included (e.g. "21.3 C", "1013.2 hPa").
 *
 * The conversion is the display side of ::weather_field_snapshot: on-air units
 * are what the container holds because that is what the encoder needs, and no
 * caller showing a reading to a person wants them.
 *
 * @param[in]  field   Field @p value belongs to; it selects both the
 *                     conversion and the unit shown.
 * @param[in]  value   Value as returned by ::weather_field_snapshot.
 * @param[out] out     Destination buffer, always NUL-terminated. 24 bytes hold
 *                     every field's rendering.
 * @param[in]  out_max Size of @p out in bytes; nothing is written when it is 0.
 */
void weather_field_format(wx_field_id_t field, double value, char *out, size_t out_max);
/** @} */

#endif // WEATHER_H
