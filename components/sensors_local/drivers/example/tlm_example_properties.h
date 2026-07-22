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
    .name = "TLM Example",
    .wx = SENSOR_LOCAL_WX_NONE,
    .tlm = (sensor_local_tlm_channel_mask_t)(SENSOR_LOCAL_TLM_ANALOG_ALL | SENSOR_LOCAL_TLM_DIGITAL_ALL),
    .tlm_meta = SENSOR_LOCAL_TLM_META_NONE,
    .wx_channel_name = { 0 },
    .tlm_channel_name = {
        [0] = "A1",  /* SENSOR_LOCAL_TLM_ANALOG_A1 (bit 0) */
        [1] = "A2",  /* SENSOR_LOCAL_TLM_ANALOG_A2 (bit 1) */
        [2] = "A3",  /* SENSOR_LOCAL_TLM_ANALOG_A3 (bit 2) */
        [3] = "A4",  /* SENSOR_LOCAL_TLM_ANALOG_A4 (bit 3) */
        [4] = "A5",  /* SENSOR_LOCAL_TLM_ANALOG_A5 (bit 4) */
        [5] = "B1",  /* SENSOR_LOCAL_TLM_DIGITAL_B1 (bit 5) */
        [6] = "B2",  /* SENSOR_LOCAL_TLM_DIGITAL_B2 (bit 6) */
        [7] = "B3",  /* SENSOR_LOCAL_TLM_DIGITAL_B3 (bit 7) */
        [8] = "B4",  /* SENSOR_LOCAL_TLM_DIGITAL_B4 (bit 8) */
        [9] = "B5",  /* SENSOR_LOCAL_TLM_DIGITAL_B5 (bit 9) */
        [10] = "B6", /* SENSOR_LOCAL_TLM_DIGITAL_B6 (bit 10) */
        [11] = "B7", /* SENSOR_LOCAL_TLM_DIGITAL_B7 (bit 11) */
        [12] = "B8", /* SENSOR_LOCAL_TLM_DIGITAL_B8 (bit 12) */
    },
};

#endif /* TLM_EXAMPLE_PROPERTIES_H_ */
