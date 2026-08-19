/**
 * @file bme280_properties.h
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
 * @brief The two ::sensor_local_properties_t descriptors the "bme280" WEATHER
 *        driver (bme280.c) chooses between, one per chip variant it can find
 *        on the bus.
 *
 * @details
 * The Bosch BME280 is a relative-humidity + barometric-pressure + temperature
 * digital sensor. Its pin- and register-compatible sibling, the BMP280, drops
 * the humidity element and answers the very same driver with a different chip
 * ID; neither part can measure wind, rain, snow, luminosity or flood height.
 *
 * A properties descriptor is what the Weather page's per-field "Channel"
 * picker filters on, so advertising a field the fitted chip cannot produce
 * would let an operator map a Weather row to a source that never fills it -
 * the field would then simply be missing from every WX beacon, with nothing on
 * screen to say why. Both variants therefore get their own descriptor here,
 * each advertising exactly the fields bme280_drv_save() writes for that chip,
 * and the driver points ::sensor_local_driver_t::properties at the matching
 * one once bring-up has read the chip ID off the real device.
 */

#ifndef BME280_PROPERTIES_H_
#define BME280_PROPERTIES_H_

#include "sensor_local_properties.h"

/** @brief BME280 variant: Temperature + Relative Humidity + Barometric Pressure. No telemetry capability. */
static const sensor_local_properties_t bme280_properties = {
    .name = "BME280",
    .wx = (sensor_local_wx_mask_t)(SENSOR_LOCAL_WX_TEMPERATURE | SENSOR_LOCAL_WX_HUMIDITY | SENSOR_LOCAL_WX_PRESSURE),
    .tlm = SENSOR_LOCAL_TLM_NONE,
    .tlm_meta = SENSOR_LOCAL_TLM_META_NONE,
    .wx_channel_name = {
        [3] = "Temperature",
        [8] = "Humidity",
        [9] = "Pressure",
    },
    .tlm_channel_name = { 0 },
};

/** @brief BMP280 variant: Temperature + Barometric Pressure. No humidity element, no telemetry capability. */
static const sensor_local_properties_t bmp280_properties = {
    .name = "BMP280",
    .wx = (sensor_local_wx_mask_t)(SENSOR_LOCAL_WX_TEMPERATURE | SENSOR_LOCAL_WX_PRESSURE),
    .tlm = SENSOR_LOCAL_TLM_NONE,
    .tlm_meta = SENSOR_LOCAL_TLM_META_NONE,
    .wx_channel_name = {
        [3] = "Temperature",
        [9] = "Pressure",
    },
    .tlm_channel_name = { 0 },
};

#endif /* BME280_PROPERTIES_H_ */
