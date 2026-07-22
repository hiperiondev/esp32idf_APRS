/**
 * @file bmp180_properties.h
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
 * @brief ::sensor_local_properties_t descriptor for the "bmp180" WEATHER
 *        driver (bmp180.c).
 *
 * The Bosch BMP180 is a barometric-pressure + temperature-only digital
 * sensor: it physically cannot measure wind, rain, humidity, snow,
 * luminosity or flood height. Its properties therefore advertise exactly
 * the two Weather fields bmp180_drv_save() actually writes
 * (::APRS_WX_SENSOR_TEMPERATURE and ::APRS_WX_SENSOR_BAROMETRIC_PRESSURE),
 * so the Weather page's per-field "Channel" picker only offers "bmp180"
 * as a source for Temperature and Pressure - never for Wind Direction,
 * Rain 1h, Humidity, etc.
 */

#ifndef BMP180_PROPERTIES_H_
#define BMP180_PROPERTIES_H_

#include "sensor_local_properties.h"

/** @brief bmp180: Temperature + Barometric Pressure only. No telemetry capability. */
static const sensor_local_properties_t bmp180_properties = {
    .wx = (sensor_local_wx_mask_t)(SENSOR_LOCAL_WX_TEMPERATURE | SENSOR_LOCAL_WX_PRESSURE),
    .tlm = SENSOR_LOCAL_TLM_NONE,
    .tlm_meta = SENSOR_LOCAL_TLM_META_NONE,
};

#endif /* BMP180_PROPERTIES_H_ */
