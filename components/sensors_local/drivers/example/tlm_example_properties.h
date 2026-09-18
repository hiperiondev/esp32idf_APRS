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
 * The example driver simulates a realistic, plausible engineering value
 * for every analog (A1-A5) and digital (B1-B8) channel the caller has
 * allocated (see tlm_example_save()), so its properties advertise the
 * full analog and digital channel set. It supplies no PARM/UNIT/EQNS/
 * BITS defaults *through this descriptor* (those are configured from the
 * web admin Telemetry page instead, so @c tlm_meta is left empty per the
 * ::sensor_local_tlm_meta_mask_t contract) - however every channel's
 * intended name, engineering unit and scaling equation IS fully
 * documented below (and, in more detail, alongside
 * ::s_analog_channels / ::s_digital_channels in the .c file) so it can be
 * typed directly into that page's per-channel Name/Unit/(a,b,c)
 * calibration fields:
 *
 *   Analog channel      | Unit    | Realistic range   | EQNS (a=0, X=b*raw+c)
 *   -------------------- | ------- | ------------------ | ------------------------
 *   A1 Supply Voltage    | Volts   | 10.50 .. 14.80 V    | b=0.016863  c=10.50
 *   A2 Enclosure Temp    | DegC    | -20.0 .. 60.0 degC  | b=0.313725  c=-20.00
 *   A3 Charge Current    | Amps    | 0.00 .. 3.20 A      | b=0.012549  c=0.00
 *   A4 RF Signal (RSSI)  | dBm     | -110 .. -40 dBm     | b=0.274510  c=-110.00
 *   A5 Free Heap         | Percent | 0 .. 100 %          | b=0.392157  c=0.00
 *
 *   Digital channel        | ON label | Meaning
 *   ----------------------- | -------- | --------------------------------------
 *   B1 PTT Active            | TX       | Transmitter keyed
 *   B2 GPS Fix Valid         | FIX      | GPS has a current fix
 *   B3 SD Card Present       | OK       | SD/LittleFS log storage mounted
 *   B4 Charge Ctrl Active    | CHG      | Solar charge controller charging
 *   B5 Enclosure Door        | OPEN     | Enclosure tamper/door switch
 *   B6 Low Battery Alarm     | LOW      | Supply voltage below threshold
 *   B7 Over Temp Alarm       | HOT      | Enclosure temperature above threshold
 *   B8 Watchdog Reset        | RST      | Last boot caused by a watchdog reset
 *
 * @note b above is computed as (eng_max - eng_min) / 255, c as eng_min,
 *       i.e. the standard strict-APRS101 8-bit (0-255) linear mapping;
 *       see tlm_example_save()/tlm_example_encode_raw() for the exact
 *       floating-point derivation used at run time.
 */

#ifndef TLM_EXAMPLE_PROPERTIES_H_
#define TLM_EXAMPLE_PROPERTIES_H_

#include "sensor_local_properties.h"

/** @brief tlm-example: all 5 analog (A1-A5) + all 8 digital (B1-B8) channels. No PARM/UNIT/EQNS/BITS defaults (see documentation table above). */
static const sensor_local_properties_t tlm_example_properties = {
    .name = "TLM Example",
    .wx = SENSOR_LOCAL_WX_NONE,
    .tlm = (sensor_local_tlm_channel_mask_t)(SENSOR_LOCAL_TLM_ANALOG_ALL | SENSOR_LOCAL_TLM_DIGITAL_ALL),
    .tlm_meta = SENSOR_LOCAL_TLM_META_NONE,
    .wx_channel_name = { 0 },
    .tlm_channel_name = {
        [0] = "A1 Supply Voltage (Volts)",
        [1] = "A2 Enclosure Temp (DegC)",
        [2] = "A3 Charge Current (Amps)",
        [3] = "A4 RF Signal RSSI (dBm)",
        [4] = "A5 Free Heap (Percent)",
        [5] = "B1 PTT Active",
        [6] = "B2 GPS Fix Valid",
        [7] = "B3 SD Card Present",
        [8] = "B4 Charge Ctrl Active",
        [9] = "B5 Enclosure Door",
        [10] = "B6 Low Battery Alarm",
        [11] = "B7 Over Temp Alarm",
        [12] = "B8 Watchdog Reset",
    },
    .tlm_ana_unit = {
        [0] = "Volts",
        [1] = "DegC",
        [2] = "Amps",
        [3] = "dBm",
        [4] = "Percent",
    },
    .tlm_ana_eng_min = { [0] = 10.50f,  [1] = -20.0f, [2] = 0.00f,  [3] = -110.0f, [4] = 0.0f },
    .tlm_ana_eng_max = { [0] = 14.80f,  [1] = 60.0f,  [2] = 3.20f,  [3] = -40.0f,  [4] = 100.0f },
    .tlm_ana_eng_valid = SENSOR_LOCAL_TLM_ANALOG_ALL,
};

#endif /* TLM_EXAMPLE_PROPERTIES_H_ */
