/**
 * @file wifi_sta.h
 *
 * @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
 * @date 2026
 * @copyright GNU General Public License v3
 * @see https://github.com/hiperiondev/esp32idf_APRS
 *
 * @brief Coordination between the WiFi station's automatic reconnection
 * (main.c) and the access-point scan the Wireless page runs.
 *
 * The station walks the enabled WiFi Client slots in order: each one gets a
 * few attempts and, when it does not connect, the next enabled slot is
 * selected, wrapping around, so every configured network keeps being tried
 * until one lets the station in. An attempt in progress keeps the driver from
 * scanning, so a scan is bracketed by the two calls below: the first holds
 * the reconnection and stops an association under way, the second releases
 * the hold and makes the next attempt at once.
 */

#ifndef WIFI_STA_H
#define WIFI_STA_H

#include <stdbool.h>

/**
 * @brief Hold the station's automatic reconnection for the length of a scan.
 *
 * @details Stops the pending reconnect timer and, when the station is not
 * connected, cancels the association it is attempting, which would otherwise
 * make esp_wifi_scan_start() fail. A station that is connected keeps its link:
 * the driver scans alongside it. The disconnect a cancelled association
 * produces is not counted as a failed attempt on the current slot. Every call
 * must be followed by wifi_sta_scan_end().
 *
 * @return true if an association attempt was cancelled, false otherwise.
 */
bool wifi_sta_scan_begin(void);

/**
 * @brief Release the hold taken by wifi_sta_scan_begin().
 *
 * @details When the station is enabled and not connected, the next connection
 * attempt is made immediately rather than after the usual reconnect interval.
 */
void wifi_sta_scan_end(void);

#endif // WIFI_STA_H
