/**
 * @file aprs_path.h
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
 * @brief Single implementation of "resolve a path bitmask into the comma-joined
 * TNC2 path suffix a transmitter appends to its destination call".
 *
 * Every service that originates traffic (position/status beacons, weather,
 * telemetry, messages) selects its digipeater path the same way: a bitmask over
 * the four shared presets in ::g_config.path[0..3], where bit N selects preset
 * N and a selected-but-empty slot contributes nothing. The resulting suffix is
 * ",PRESET" per selected slot, in ascending bit order, appended straight after
 * the destination call ("MYCALL>APE32L,WIDE1-1,WIDE2-1:...").
 *
 * The AX.25 8-via limit is enforced here, on the transmit side. The webconfig
 * POST handlers already clamp the bitmask at save time
 * (app_config_path_mask_clamp()), but a configuration can also reach a
 * transmitter without ever passing through a form - a hand-edited or imported
 * config.json, or a file dropped on the device from the Storage page - so the
 * limit is checked again at the point the path is actually built. Hop counting
 * uses app_config_path_hop_count(), the same function the save-time clamp uses,
 * so the two enforcement points cannot disagree: a preset slot may itself list
 * several comma-separated hops, which is why counting set bits is not enough.
 *
 * This header is deliberately implementation-only (static inline, no .c file)
 * so it can be included from `main/` and from every component that already has
 * `main/include` on its include path, without adding a link dependency.
 */

#ifndef APRS_PATH_H
#define APRS_PATH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"

#include "app_config.h"
#include "str_append.h"

#define APRS_PATH_PRESET_COUNT 4  /**< Number of shared path presets (::g_config.path). */
#define APRS_PATH_PRESET_SIZE  72 /**< Size of one preset slot, including the NUL. */
#define APRS_PATH_MAX_HOPS     8  /**< AX.25 limit on via (digipeater) addresses in one frame. */

#define APRS_PATH_LOG_TAG "aprs_path"

/**
 * @brief Build the TNC2 path suffix selected by @p path_bitmask.
 *
 * @param path_bitmask Bitmask over @p path_preset (bit N selects preset N).
 * @param path_preset  The four shared path presets, i.e. a snapshot of
 *                     ::g_config.path[0..3] taken while holding
 *                     app_config_lock(). Never read from ::g_config directly,
 *                     so a concurrent web save cannot be observed mid-strcpy.
 * @param out          Destination buffer, always left NUL-terminated (empty
 *                     when the bitmask selects nothing).
 * @param out_max      Size of @p out in bytes, including room for the NUL.
 * @return true if the whole selection was emitted; false if it was cut short,
 *         either by the 8-via limit or by @p out running out of room.
 */
static inline bool aprs_path_build_suffix(uint8_t path_bitmask, const char path_preset[APRS_PATH_PRESET_COUNT][APRS_PATH_PRESET_SIZE], char *out,
                                          size_t out_max) {
    if (out == NULL || out_max == 0)
        return false;

    out[0] = 0;
    if (path_bitmask == 0)
        return true;

    size_t used = 0;
    uint8_t hops_used = 0;
    for (int bit = 0; bit < APRS_PATH_PRESET_COUNT; bit++) {
        if (!(path_bitmask & (1u << bit)))
            continue;
        if (!path_preset[bit][0])
            continue; // bit selected but that preset slot isn't configured

        // Hops this slot adds, counted the same way the save-time clamp counts
        // them: a single-bit mask asks app_config_path_hop_count() for exactly
        // this preset's own comma-separated hop count.
        uint8_t preset_hops = app_config_path_hop_count((uint8_t)(1u << bit), path_preset);
        if (hops_used + preset_hops > APRS_PATH_MAX_HOPS) {
            ESP_LOGW(APRS_PATH_LOG_TAG, "path bitmask 0x%02X exceeds AX.25 %d-hop limit, truncating at preset %d", path_bitmask, APRS_PATH_MAX_HOPS, bit + 1);
            return false; // stop adding presets, keep what is already built
        }

        if (!str_append(out, out_max, &used, ",%s", path_preset[bit]))
            return false; // out is full and still valid/terminated

        hops_used = (uint8_t)(hops_used + preset_hops);
    }

    return true;
}

/**
 * @brief Build the TNC2 path suffix selected by @p path_bitmask, taking the
 * preset snapshot from ::g_config for the caller.
 *
 * Convenience wrapper for the services that do not already hold a snapshot of
 * the presets: it copies ::g_config.path[0..3] under app_config_lock() and then
 * builds from that copy.
 *
 * @param path_bitmask Bitmask over ::g_config.path[0..3].
 * @param out          Destination buffer, always left NUL-terminated.
 * @param out_max      Size of @p out in bytes, including room for the NUL.
 * @return Same as aprs_path_build_suffix().
 */
static inline bool aprs_path_build_suffix_from_config(uint8_t path_bitmask, char *out, size_t out_max) {
    char path_preset[APRS_PATH_PRESET_COUNT][APRS_PATH_PRESET_SIZE];

    app_config_lock();
    memcpy(path_preset, g_config.path, sizeof(path_preset));
    app_config_unlock();

    return aprs_path_build_suffix(path_bitmask, path_preset, out, out_max);
}

#endif /* APRS_PATH_H */
