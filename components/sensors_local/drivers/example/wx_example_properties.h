/**
 * @file wx_example_properties.h
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
 * @brief ::sensor_local_properties_t descriptor for the "wx-example"
 *        WEATHER driver (sensor_local_weather_example.c).
 *
 * The example driver fabricates Wind (direction/sustained/gust),
 * Temperature, Humidity, Barometric Pressure, Rain (last hour) and
 * Luminosity on every call (see wx_example_save()); it never touches
 * Rain 24h/midnight, Snow, or Flood height. Properties mirror exactly
 * that set so it only appears as a selectable "Channel" source on the
 * Weather page for the fields it truly fabricates.
 */

#ifndef WX_EXAMPLE_PROPERTIES_H_
#define WX_EXAMPLE_PROPERTIES_H_

#include "sensor_local_properties.h"

/** @brief wx-example: Wind (dir/speed/gust), Temperature, Humidity, Pressure, Rain 1h, Luminosity. No telemetry capability. */
static const sensor_local_properties_t wx_example_properties = {
    .wx = (sensor_local_wx_mask_t)(SENSOR_LOCAL_WX_WIND_DIRECTION | SENSOR_LOCAL_WX_WIND_SPEED | SENSOR_LOCAL_WX_WIND_GUST | SENSOR_LOCAL_WX_TEMPERATURE |
                                    SENSOR_LOCAL_WX_HUMIDITY | SENSOR_LOCAL_WX_PRESSURE | SENSOR_LOCAL_WX_RAIN_1H | SENSOR_LOCAL_WX_LUMINOSITY),
    .tlm = SENSOR_LOCAL_TLM_NONE,
    .tlm_meta = SENSOR_LOCAL_TLM_META_NONE,
};

#endif /* WX_EXAMPLE_PROPERTIES_H_ */
