/**
 * @file sensor_local_properties.h
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
 * @brief Common "properties" contract every local sensor driver must
 *        publish, declaring EXACTLY which APRS Weather/Telemetry
 *        parameters it is able to produce.
 *
 * @details
 * ::sensor_local_driver_t::capabilities (see sensors_local.h) only says
 * *what family* a driver belongs to (Weather and/or Telemetry). It says
 * nothing about *which* of the fields within that family the driver can
 * actually fill in. A BMP180 is a Weather sensor, but it can only ever
 * supply Temperature and Pressure - never Wind, Rain, Humidity, etc. This
 * header closes that gap.
 *
 * Every sensor driver (weather or telemetry) owns exactly one
 * "<sensor>_properties.h" (e.g. bmp180_properties.h, wx_example_properties.h,
 * tlm_example_properties.h) declaring a single @c static @c const
 * ::sensor_local_properties_t and pointing
 * ::sensor_local_driver_t::properties at it. This lets any consumer of the
 * registry (chiefly the Weather page's per-field "Channel" picker in
 * page_wx.c) offer only the sensors that are actually capable of a given
 * field/channel, instead of every registered driver regardless of fitness.
 *
 * Two independent bit fields are provided, mirroring the two payload
 * families of weather_telemetry.h:
 *
 *   - ::sensor_local_wx_mask_t: one bit per ::aprs_weather_sensor_id_t
 *     (Wind Direction, Wind Speed, Wind Gust, Temperature, Rain 1h, Rain
 *     24h, Snow, Humidity, Pressure, Luminosity, Flood Height ft, Flood
 *     Height m, ...) - a WEATHER sensor sets exactly the bits it can
 *     produce.
 *
 *   - ::sensor_local_tlm_mask_t: one bit per analog (A1-A5) and digital
 *     (B1-B8) telemetry channel plus flags for which of the four telemetry
 *     metadata message kinds (PARM/UNIT/EQNS/BITS) the driver can supply
 *     default values for - a TELEMETRY sensor sets exactly the analog/
 *     digital channel bits it drives.
 *
 * A driver that is Weather-only leaves @c tlm at all zero (or omits the
 * @c properties pointer's telemetry mask usage entirely); a driver that is
 * Telemetry-only leaves @c wx at all zero. A hybrid driver (rare, but
 * legal per ::sensor_local_data_kind_t) may set bits in both.
 */

#ifndef SENSOR_LOCAL_PROPERTIES_H_
#define SENSOR_LOCAL_PROPERTIES_H_

#include <stdint.h>
#include <stdio.h>

#include "weather_telemetry.h"

/* ======================================================================
 * WEATHER capability bit field
 * ====================================================================== */

/**
 * @brief One bit per mappable APRS Weather Report parameter a WEATHER
 *        sensor driver can produce, mirroring ::aprs_weather_sensor_id_t
 *        (weather_telemetry.h) one-for-one, plus a split of the single
 *        ::APRS_WX_SENSOR_WIND slot into its three independently
 *        mappable web-admin fields (Direction / Speed / Gust) to match
 *        the granularity of the Weather page's per-field "Channel"
 *        column (::wx_field_id_t in app_config.h).
 *
 * @note Bit position intentionally matches ::wx_field_id_t so a mask can
 *       be tested directly with `(mask >> field_id) & 1u` from page_wx.c
 *       without an extra lookup table.
 */
typedef enum {
    SENSOR_LOCAL_WX_WIND_DIRECTION = 1u << 0,  /**< Wind Direction ("ddd/"). */
    SENSOR_LOCAL_WX_WIND_SPEED = 1u << 1,      /**< Wind Speed, sustained ("/sss"). */
    SENSOR_LOCAL_WX_WIND_GUST = 1u << 2,       /**< Wind Gust, peak ("gXXX"). */
    SENSOR_LOCAL_WX_TEMPERATURE = 1u << 3,     /**< Temperature, deg F ("tXXX"). */
    SENSOR_LOCAL_WX_RAIN_1H = 1u << 4,         /**< Rain, last 1 hour ("rXXX"). */
    SENSOR_LOCAL_WX_RAIN_24H = 1u << 5,        /**< Rain, last 24 hours ("pXXX"). */
    SENSOR_LOCAL_WX_RAIN_MIDNIGHT = 1u << 6,   /**< Rain since local midnight ("PXXX"). */
    SENSOR_LOCAL_WX_SNOW = 1u << 7,            /**< Snow, last 24 hours ("sXXX", APRS 1.2). */
    SENSOR_LOCAL_WX_HUMIDITY = 1u << 8,        /**< Relative Humidity ("hXX"). */
    SENSOR_LOCAL_WX_PRESSURE = 1u << 9,        /**< Barometric Pressure ("bXXXXX"). */
    SENSOR_LOCAL_WX_LUMINOSITY = 1u << 10,     /**< Luminosity, solar radiation ("LXXX"/"lXXX", APRS 1.2). */
    SENSOR_LOCAL_WX_FLOOD_HEIGHT_FT = 1u << 11,/**< Flood/water gauge height, feet ("FXXXX.X", APRS 1.2). */
    SENSOR_LOCAL_WX_FLOOD_HEIGHT_M = 1u << 12, /**< Flood/water gauge height, meters ("fXXXX.X", APRS 1.2). */

    SENSOR_LOCAL_WX_NONE = 0,                  /**< No weather parameter (telemetry-only or uninitialised driver). */
    SENSOR_LOCAL_WX_ALL = (1u << 13) - 1u,     /**< Convenience: every weather bit currently defined. */
} sensor_local_wx_mask_t;

/* ======================================================================
 * TELEMETRY capability bit field
 * ====================================================================== */

/**
 * @brief One bit per APRS Telemetry analog (A1-A5) and digital (B1-B8)
 *        channel a TELEMETRY sensor driver can drive, matching
 *        ::aprs_telemetry_analog_channel_id_t / ::aprs_telemetry_digital_channel_id_t
 *        (weather_telemetry.h) one-for-one, so a mask can be tested with
 *        `(mask >> APRS_TLM_ANALOG_Ax) & 1u` / `(mask >> (8 + APRS_TLM_DIGITAL_Bx)) & 1u`.
 */
typedef enum {
    /* --- Analog channels A1..A5 (bits 0..4) --- */
    SENSOR_LOCAL_TLM_ANALOG_A1 = 1u << 0,
    SENSOR_LOCAL_TLM_ANALOG_A2 = 1u << 1,
    SENSOR_LOCAL_TLM_ANALOG_A3 = 1u << 2,
    SENSOR_LOCAL_TLM_ANALOG_A4 = 1u << 3,
    SENSOR_LOCAL_TLM_ANALOG_A5 = 1u << 4,
    SENSOR_LOCAL_TLM_ANALOG_ALL = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4),

    /* --- Digital (binary) channels B1..B8 (bits 5..12) --- */
    SENSOR_LOCAL_TLM_DIGITAL_B1 = 1u << 5,
    SENSOR_LOCAL_TLM_DIGITAL_B2 = 1u << 6,
    SENSOR_LOCAL_TLM_DIGITAL_B3 = 1u << 7,
    SENSOR_LOCAL_TLM_DIGITAL_B4 = 1u << 8,
    SENSOR_LOCAL_TLM_DIGITAL_B5 = 1u << 9,
    SENSOR_LOCAL_TLM_DIGITAL_B6 = 1u << 10,
    SENSOR_LOCAL_TLM_DIGITAL_B7 = 1u << 11,
    SENSOR_LOCAL_TLM_DIGITAL_B8 = 1u << 12,
    SENSOR_LOCAL_TLM_DIGITAL_ALL = (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 10) | (1u << 11) | (1u << 12),

    SENSOR_LOCAL_TLM_NONE = 0,
    SENSOR_LOCAL_TLM_ALL = SENSOR_LOCAL_TLM_ANALOG_ALL | SENSOR_LOCAL_TLM_DIGITAL_ALL,
} sensor_local_tlm_channel_mask_t;

/**
 * @brief Which of the four APRS Telemetry metadata message kinds
 *        (APRS101 Chapter 13: "PARM.", "UNIT.", "EQNS.", "BITS.") a
 *        TELEMETRY sensor driver can supply built-in default values for
 *        (channel name/label, engineering unit, quadratic scaling
 *        coefficients, and digital bit-sense/project text respectively -
 *        see ::aprs_telemetry_metadata_t). A driver may leave this at 0
 *        if it relies entirely on the web-admin-configured PARM/UNIT/
 *        EQNS/BITS values instead of supplying its own.
 */
typedef enum {
    SENSOR_LOCAL_TLM_META_NONE = 0,
    SENSOR_LOCAL_TLM_META_PARM = 1u << 0, /**< Driver can supply default PARM. (parameter name) text. */
    SENSOR_LOCAL_TLM_META_UNIT = 1u << 1, /**< Driver can supply default UNIT. (engineering unit / bit label) text. */
    SENSOR_LOCAL_TLM_META_EQNS = 1u << 2, /**< Driver can supply default EQNS. (a/b/c scaling coefficients). */
    SENSOR_LOCAL_TLM_META_BITS = 1u << 3, /**< Driver can supply default BITS. (bit-sense polarity / project name) text. */
    SENSOR_LOCAL_TLM_META_ALL = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3),
} sensor_local_tlm_meta_mask_t;

/* ======================================================================
 * Human-readable name tables
 * ====================================================================== */

/**
 * @brief Number of ::sensor_local_wx_mask_t field slots, i.e. the highest
 *        bit position used (::SENSOR_LOCAL_WX_FLOOD_HEIGHT_M, bit 12) plus
 *        one. Used to size ::sensor_local_properties_t::wx_channel_name.
 */
#define SENSOR_LOCAL_WX_FIELD_COUNT 13u

/**
 * @brief Number of ::sensor_local_tlm_channel_mask_t channel slots (5
 *        analog + 8 digital = 13). Used to size
 *        ::sensor_local_properties_t::tlm_channel_name.
 */
#define SENSOR_LOCAL_TLM_CHANNEL_COUNT 13u

/* ======================================================================
 * Common per-driver properties descriptor
 * ====================================================================== */

/**
 * @brief One instance per sensor driver, declared in that driver's own
 *        "<sensor>_properties.h", describing exactly which Weather and/or
 *        Telemetry parameters it is able to produce.
 *
 * @note This descriptor is intentionally separate from
 *       ::sensor_local_driver_t::capabilities: @c capabilities is the
 *       coarse family selector (Weather vs Telemetry, tested by
 *       sensors_local_save()'s dispatch loop); @c sensor_local_properties_t
 *       is the fine-grained, per-field/per-channel selector used by UI
 *       code (the Weather page's "Channel" column) to decide whether a
 *       given driver is a sane choice for a given field.
 *
 * @note Every driver's properties descriptor MUST also supply human
 *       readable text: @c name identifies the sensor itself, and
 *       @c wx_channel_name / @c tlm_channel_name give a short label for
 *       every individual Weather field / Telemetry channel the driver
 *       advertises via @c wx / @c tlm. These text names are what the
 *       Weather page's per-field "Channel" <select> shows to the user
 *       (see wx_channel_select() in page_wx.c), instead of just the bare
 *       driver name repeated on every row. A slot that the driver does
 *       NOT advertise (its @c wx / @c tlm bit is 0) may leave the
 *       corresponding name entry NULL; slots that ARE advertised should
 *       always have a non-NULL, non-empty name.
 */
typedef struct {
    const char *name;                      /**< Human-readable sensor name (e.g. "BMP180", "WX Example"). Must not be NULL. */
    sensor_local_wx_mask_t wx;             /**< Weather parameter(s) this driver can produce; ::SENSOR_LOCAL_WX_NONE if not a weather sensor. */
    sensor_local_tlm_channel_mask_t tlm;   /**< Telemetry analog/digital channel(s) this driver drives; ::SENSOR_LOCAL_TLM_NONE if not a telemetry sensor. */
    sensor_local_tlm_meta_mask_t tlm_meta; /**< Telemetry metadata (PARM/UNIT/EQNS/BITS) kinds this driver can supply defaults for. */
    /**
     * @brief Human-readable label for every Weather field bit, indexed the
     *        same way as ::sensor_local_wx_mask_t (bit N -> array index N,
     *        e.g. [3] labels ::SENSOR_LOCAL_WX_TEMPERATURE). Only indices
     *        whose bit is set in @c wx need be populated.
     */
    const char *wx_channel_name[SENSOR_LOCAL_WX_FIELD_COUNT];
    /**
     * @brief Human-readable label for every Telemetry analog/digital
     *        channel bit, indexed the same way as
     *        ::sensor_local_tlm_channel_mask_t (bit N -> array index N,
     *        e.g. [0] labels ::SENSOR_LOCAL_TLM_ANALOG_A1, [5] labels
     *        ::SENSOR_LOCAL_TLM_DIGITAL_B1). Only indices whose bit is set
     *        in @c tlm need be populated.
     */
    const char *tlm_channel_name[SENSOR_LOCAL_TLM_CHANNEL_COUNT];
} sensor_local_properties_t;

/**
 * @brief Format the human-readable label for a given Weather field as
 *        "<sensor name> <channel name>" into @p out, e.g. "BMP180
 *        Temperature". If the field has no dedicated channel-name entry,
 *        @p out is just the sensor name; if @p props itself is NULL,
 *        @p out is "?".
 *
 * @param props      Pointer to the driver's ::sensor_local_properties_t (may be NULL).
 * @param field_bit  One ::sensor_local_wx_mask_t bit (e.g. ::SENSOR_LOCAL_WX_TEMPERATURE).
 * @param out        Destination buffer.
 * @param out_size   Size of @p out in bytes.
 */
static inline void sensor_local_properties_wx_label(const sensor_local_properties_t *props, sensor_local_wx_mask_t field_bit, char *out, size_t out_size) {
    if (out == NULL || out_size == 0)
        return;
    if (props == NULL) {
        snprintf(out, out_size, "?");
        return;
    }
    const char *sensor_name = (props->name != NULL) ? props->name : "?";
    unsigned idx = 0;
    unsigned bit = (unsigned)field_bit;
    while (bit > 1u) {
        bit >>= 1;
        idx++;
    }
    const char *chan_name = (idx < SENSOR_LOCAL_WX_FIELD_COUNT) ? props->wx_channel_name[idx] : NULL;
    if (chan_name != NULL)
        snprintf(out, out_size, "%s %s", sensor_name, chan_name);
    else
        snprintf(out, out_size, "%s", sensor_name);
}

/**
 * @brief Format the human-readable label for a given Telemetry
 *        analog/digital channel as "<sensor name> <channel name>" into
 *        @p out, e.g. "TLM Example A1". If the channel has no dedicated
 *        channel-name entry, @p out is just the sensor name; if @p props
 *        itself is NULL, @p out is "?".
 *
 * @param props       Pointer to the driver's ::sensor_local_properties_t (may be NULL).
 * @param channel_bit One ::sensor_local_tlm_channel_mask_t bit (e.g. ::SENSOR_LOCAL_TLM_ANALOG_A1).
 * @param out         Destination buffer.
 * @param out_size    Size of @p out in bytes.
 */
static inline void sensor_local_properties_tlm_label(const sensor_local_properties_t *props, sensor_local_tlm_channel_mask_t channel_bit, char *out,
                                                       size_t out_size) {
    if (out == NULL || out_size == 0)
        return;
    if (props == NULL) {
        snprintf(out, out_size, "?");
        return;
    }
    const char *sensor_name = (props->name != NULL) ? props->name : "?";
    unsigned idx = 0;
    unsigned bit = (unsigned)channel_bit;
    while (bit > 1u) {
        bit >>= 1;
        idx++;
    }
    const char *chan_name = (idx < SENSOR_LOCAL_TLM_CHANNEL_COUNT) ? props->tlm_channel_name[idx] : NULL;
    if (chan_name != NULL)
        snprintf(out, out_size, "%s %s", sensor_name, chan_name);
    else
        snprintf(out, out_size, "%s", sensor_name);
}

/**
 * @brief Test whether a driver's properties advertise a given Weather
 *        field, matching that field's ::wx_field_id_t bit position
 *        one-for-one (see the note on ::sensor_local_wx_mask_t).
 *
 * @param props      Pointer to the driver's ::sensor_local_properties_t (may be NULL).
 * @param field_bit  One ::sensor_local_wx_mask_t bit to test (e.g. ::SENSOR_LOCAL_WX_TEMPERATURE).
 * @return true if @p props is non-NULL and has @p field_bit set.
 */
static inline bool sensor_local_properties_has_wx(const sensor_local_properties_t *props, sensor_local_wx_mask_t field_bit) {
    return (props != NULL) && ((props->wx & field_bit) != 0);
}

/**
 * @brief Test whether a driver's properties advertise a given Telemetry
 *        analog or digital channel.
 *
 * @param props       Pointer to the driver's ::sensor_local_properties_t (may be NULL).
 * @param channel_bit One ::sensor_local_tlm_channel_mask_t bit to test (e.g. ::SENSOR_LOCAL_TLM_ANALOG_A1).
 * @return true if @p props is non-NULL and has @p channel_bit set.
 */
static inline bool sensor_local_properties_has_tlm(const sensor_local_properties_t *props, sensor_local_tlm_channel_mask_t channel_bit) {
    return (props != NULL) && ((props->tlm & channel_bit) != 0);
}

#endif /* SENSOR_LOCAL_PROPERTIES_H_ */
