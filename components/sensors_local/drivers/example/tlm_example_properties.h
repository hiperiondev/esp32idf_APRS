/**
 * @file tlm_example_properties.h
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
 * @brief ::sensor_local_properties_t descriptor for the "tlm-example"
 *        TELEMETRY driver (sensor_local_telemetry_example.c).
 *
 * The example driver fabricates a value for every analog (A1-A5) and
 * digital (B1-B8) channel the caller has allocated (see
 * tlm_example_save()), so its properties advertise the full analog and
 * digital channel set. It supplies no PARM/UNIT/EQNS/BITS metadata
 * defaults of its own (those are configured from the web admin Telemetry
 * page instead), so @c tlm_meta is left empty.
 */

#ifndef TLM_EXAMPLE_PROPERTIES_H_
#define TLM_EXAMPLE_PROPERTIES_H_

#include "sensor_local_properties.h"

/** @brief tlm-example: all 5 analog (A1-A5) + all 8 digital (B1-B8) channels. No PARM/UNIT/EQNS/BITS defaults. */
static const sensor_local_properties_t tlm_example_properties = {
    .wx = SENSOR_LOCAL_WX_NONE,
    .tlm = (sensor_local_tlm_channel_mask_t)(SENSOR_LOCAL_TLM_ANALOG_ALL | SENSOR_LOCAL_TLM_DIGITAL_ALL),
    .tlm_meta = SENSOR_LOCAL_TLM_META_NONE,
};

#endif /* TLM_EXAMPLE_PROPERTIES_H_ */
