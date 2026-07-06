/*
 * digirepeater.h - APRS digipeater (WIDEn-N / TRACEn-N / RELAY / ECHO / GATE)
 * component. Pure C, ESP-IDF. Reads its callsign/SSID from g_config
 * (app_config_t, see main/include/app_config.h) so the web admin ("Digi"
 * page) is the single source of truth for configuration.
 */
#ifndef DIGIREPEATER_H
#define DIGIREPEATER_H

#include "AX25.h"

typedef struct {
    uint32_t rxPkts; // packets seen
    uint32_t txPkts; // packets digipeated (path modified, return value 2)
    uint32_t dropRx; // packets dropped (duplicate insert, filtered path, etc.)
    uint32_t erPkts; // malformed packets (too short / no path)
} digi_stats_t;

/**
 * @brief Process one received AX.25 frame through the digipeater path logic.
 *
 * @param packet Decoded frame (as produced by ax25_decode()). Modified in place
 *               when the path needs to be rewritten (new-N decrement, callsign
 *               insertion, etc).
 * @return 0  - do not repeat (drop / not for us / already relayed)
 *         1  - repeat as-is (path already carries our used call, e.g. bypass "*")
 *         2  - repeat with modified path (packet.info + rewritten header must be
 *              re-encoded and transmitted on RF)
 */
int digiProcess(AX25Msg *packet);

/**
 * @brief Snapshot of digipeater counters (used by the web admin dashboard /
 * telemetry beacon).
 */
digi_stats_t digi_get_stats(void);

/**
 * @brief Reset digipeater counters (e.g. after a telemetry beacon is sent).
 */
void digi_reset_stats(void);

#endif // DIGIREPEATER_H
