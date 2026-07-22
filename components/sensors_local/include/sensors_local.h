/**
 * @file sensors_local.h
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
 * @brief Local sensor sub-system: a run-time registry of dynamically loaded
 *        sensor drivers.
 *
 * Every driver is a plain struct of function pointers (::sensor_local_driver_t)
 * that is inserted into a dynamically grown pointer table (the "registry"), so
 * the core never needs a hard-coded list of the sensors that happen to be
 * compiled in. A driver can attach itself at start-up with
 * ::SENSORS_LOCAL_DRIVER_AUTOREGISTER (a C constructor that runs before
 * app_main) or be added/removed at run time with ::sensors_local_register /
 * ::sensors_local_unregister.
 *
 * Each driver exposes ONE common entry point, ::sensor_local_driver_t::save,
 * that receives a pointer to the shared ::weather_telemetry_data_t container so
 * the driver can write its freshly measured values *directly* into it. The same
 * entry serves both payload families: the ::sensor_local_data_kind_t argument
 * tells the driver whether the caller currently wants Weather data, Telemetry
 * data, or both.
 *
 * Typical flow from a beacon / service task:
 * @code
 *     weather_telemetry_data_t data = {0};
 *     aprs_weather_report_t     wx   = {0};
 *     aprs_telemetry_report_t   tlm  = {0};
 *     data.weather = &wx;             data.weather_qty          = 1;
 *     data.telemetry_report = &tlm;   data.telemetry_report_qty = 1;
 *
 *     // Ask every capable driver to fill the container in place.
 *     sensors_local_save(&data, SENSOR_LOCAL_DATA_WEATHER | SENSOR_LOCAL_DATA_TELEMETRY);
 *     // ... now encode `data` on-air ...
 * @endcode
 */

#ifndef SENSORS_LOCAL_H_
#define SENSORS_LOCAL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "weather_telemetry.h"
#include "sensor_local_properties.h"

/**
 * @brief Which payload family a driver is being asked to populate on a given
 *        call. Values are bit flags so a caller can request both at once and a
 *        driver can advertise both in ::sensor_local_driver_t::capabilities.
 */
/*
 * To add a new sensor family in the future (e.g. GPS, power/battery, air
 * quality, ...), append a new bit here:
 *
 *     SENSOR_LOCAL_DATA_GPS = 1u << 2,
 *
 * and OR it into SENSOR_LOCAL_DATA_ALL. Nothing else in the registry needs to
 * change - sensors_local_register() only requires that a driver's
 * capabilities be non-zero, and any UI page (like the Weather "Sensor
 * Mapping" table) that wants only its own kind of sensor filters the
 * registry by testing `driver->capabilities & SENSOR_LOCAL_DATA_xxx`.
 */
typedef enum {
    SENSOR_LOCAL_DATA_NONE = 0,                    /**< No payload (used as an "unsupported" sentinel). Not a valid value for a registered driver's capabilities. */
    SENSOR_LOCAL_DATA_WEATHER = 1u << 0,           /**< Populate the Weather Report slot(s) of the container. */
    SENSOR_LOCAL_DATA_TELEMETRY = 1u << 1,         /**< Populate the Telemetry Report slot(s) of the container. */
    /* -- add future sensor kinds here as additional bits, e.g.:
     * SENSOR_LOCAL_DATA_GPS = 1u << 2, */
    SENSOR_LOCAL_DATA_ALL = (1u << 0) | (1u << 1), /**< Convenience: every kind currently defined. Update this when a new kind is added above. */
} sensor_local_data_kind_t;

/* Forward declaration so the callback typedefs can take the driver itself. */
struct sensor_local_driver;
typedef struct sensor_local_driver sensor_local_driver_t;

/**
 * @brief Optional one-time bring-up of a driver (open I2C/SPI/UART, probe the
 *        chip, allocate private state, ...). Called at most once, lazily, the
 *        first time the driver is used, or explicitly from
 *        ::sensors_local_init_all.
 *
 * @param self  The driver being initialised (use @c self->ctx for state).
 * @return ESP_OK on success; any error marks the driver as failed and excludes
 *         it from ::sensors_local_save until re-registered.
 */
typedef esp_err_t (*sensor_local_init_fn_t)(sensor_local_driver_t *self);

/**
 * @brief THE common driver entry point. Reads the sensor and writes the decoded
 *        values *directly* into @p data.
 *
 * The driver must honour @p kind: when only ::SENSOR_LOCAL_DATA_WEATHER is set
 * it should touch @c data->weather[...] (and set the relevant
 * @c enabled[] flags); when only ::SENSOR_LOCAL_DATA_TELEMETRY is set it should
 * touch @c data->telemetry_report[...]; when both bits are set it may fill both.
 * It must never write a family it did not advertise in
 * ::sensor_local_driver_t::capabilities, and must tolerate a container whose
 * matching array is empty (e.g. @c weather_qty == 0) by simply doing nothing
 * for that family.
 *
 * @param self  The driver being invoked (private state in @c self->ctx).
 * @param data  Caller-owned container to modify in place. Never NULL.
 * @param kind  Bit mask of the payload families the caller wants right now.
 * @return ESP_OK if the (requested & supported) values were written; an error
 *         code if the sensor could not be read this cycle. A single driver's
 *         error does not abort the whole ::sensors_local_save pass.
 */
typedef esp_err_t (*sensor_local_save_fn_t)(sensor_local_driver_t *self, weather_telemetry_data_t *data, sensor_local_data_kind_t kind);

/**
 * @brief Optional tear-down, mirroring ::sensor_local_init_fn_t. Called from
 *        ::sensors_local_unregister and ::sensors_local_deinit.
 */
typedef void (*sensor_local_deinit_fn_t)(sensor_local_driver_t *self);

/**
 * @brief A single sensor driver: a named bundle of function pointers plus a
 *        private context. Instances are what live in the dynamic registry.
 *
 * @note A driver descriptor is normally a @c static instance owned by the
 *       driver's own translation unit; the registry stores a *pointer* to it
 *       and never copies or frees it. Run-time/heap drivers are equally valid
 *       as long as their storage outlives their registration.
 */
struct sensor_local_driver {
    const char *name;      /**< Stable, unique, human-readable id (e.g. "bme280", "ads1115-batt"). */
    uint32_t capabilities; /**< REQUIRED, must be non-zero: OR of ::sensor_local_data_kind_t bits this
                             *   driver can produce (e.g. ::SENSOR_LOCAL_DATA_WEATHER,
                             *   ::SENSOR_LOCAL_DATA_TELEMETRY, or both). Every driver must declare at
                             *   least one kind at registration time so consumers (like the Weather
                             *   page's channel picker) can tell weather sensors apart from telemetry
                             *   (or any future kind) instead of listing every registered sensor
                             *   regardless of type. ::sensors_local_register rejects drivers that leave
                             *   this at ::SENSOR_LOCAL_DATA_NONE. */

    sensor_local_init_fn_t init;     /**< Optional bring-up (may be NULL). */
    sensor_local_save_fn_t save;     /**< REQUIRED common entry that fills ::weather_telemetry_data_t. */
    sensor_local_deinit_fn_t deinit; /**< Optional tear-down (may be NULL). */

    /**
     * @brief Pointer to this driver's fine-grained capability descriptor,
     *        declared in its own "<sensor>_properties.h" (see
     *        sensor_local_properties.h). Tells consumers of the registry
     *        (chiefly the Weather page's per-field "Channel" picker)
     *        exactly which Weather parameters and/or Telemetry channels
     *        this driver can produce - unlike @c capabilities above,
     *        which only says the coarse family (Weather/Telemetry).
     *        May be NULL for a legacy driver that has not been migrated
     *        to publish a properties descriptor yet; NULL is treated as
     *        "no fields advertised" (i.e. the driver will not appear as a
     *        choice on any per-field channel picker that filters by
     *        properties), so every in-tree driver should set this.
     */
    const sensor_local_properties_t *properties;

    void *ctx; /**< Driver-private state, opaque to the registry. */

    /* --- fields owned/managed by the registry; do not touch from a driver --- */
    bool initialized; /**< Set once init() has succeeded (or when there is no init()). */
    bool failed;      /**< Set if init() returned an error; such drivers are skipped. */
};

/**
 * @brief Initialise the registry (creates the internal lock). Safe to call more
 *        than once. Not strictly required before ::sensors_local_register (that
 *        works even during C-constructor start-up), but must be called once the
 *        FreeRTOS scheduler is up so subsequent registry access is thread-safe.
 */
esp_err_t sensors_local_init(void);

/**
 * @brief Add a driver to the dynamic registry.
 *
 * @param driver  Caller-owned descriptor whose storage must outlive the
 *                registration. Its @c save pointer must be non-NULL, its
 *                @c name unique among currently registered drivers, and its
 *                @c capabilities non-zero (it must declare at least
 *                ::SENSOR_LOCAL_DATA_WEATHER and/or ::SENSOR_LOCAL_DATA_TELEMETRY,
 *                or any future ::sensor_local_data_kind_t bit) so callers can
 *                later tell what type of sensor it is.
 * @return ESP_OK on success; ESP_ERR_INVALID_ARG for a malformed descriptor
 *         (including one with @c capabilities == ::SENSOR_LOCAL_DATA_NONE);
 *         ESP_ERR_INVALID_STATE if @c name is already registered;
 *         ESP_ERR_NO_MEM if the registry could not grow.
 */
esp_err_t sensors_local_register(sensor_local_driver_t *driver);

/**
 * @brief Remove a driver by name, calling its deinit() if present.
 * @return ESP_OK on success; ESP_ERR_NOT_FOUND if no such driver.
 */
esp_err_t sensors_local_unregister(const char *name);

/** @brief Number of drivers currently in the registry. */
size_t sensors_local_count(void);

/** @brief Fetch a driver by position (0..count-1), or NULL if out of range. */
sensor_local_driver_t *sensors_local_get(size_t index);

/** @brief Fetch a driver by name, or NULL if not found. */
sensor_local_driver_t *sensors_local_find(const char *name);

/**
 * @brief Eagerly run init() on every not-yet-initialised driver. Optional;
 *        ::sensors_local_save also initialises lazily. Drivers whose init()
 *        fails are flagged and skipped, they do not abort the pass.
 * @return ESP_OK if all drivers initialised, ESP_FAIL if at least one failed.
 */
esp_err_t sensors_local_init_all(void);

/**
 * @brief THE common aggregate entry: walk the registry and let every capable,
 *        healthy driver write its values directly into @p data.
 *
 * A driver is invoked only if @c (driver->capabilities & kind) is non-zero.
 * Drivers are lazily init()'d on first use. One driver returning an error is
 * logged and skipped; it never prevents the others from running.
 *
 * @param data  Caller-owned container to be filled in place. Must be non-NULL,
 *              with whichever of @c weather / @c telemetry_report arrays the
 *              caller wants populated already allocated and their @c *_qty set.
 * @param kind  Bit mask of the payload families to gather this cycle.
 * @return ESP_OK if the pass completed (even if some drivers were skipped);
 *         ESP_ERR_INVALID_ARG if @p data is NULL.
 */
esp_err_t sensors_local_save(weather_telemetry_data_t *data, sensor_local_data_kind_t kind);

/**
 * @brief Empty the registry, calling deinit() on each driver. Does not free the
 *        driver descriptors themselves (the registry never owned them).
 */
void sensors_local_deinit(void);

/**
 * @brief Read exactly ONE driver by registry index, lazily initialising it if
 *        needed, and let it write straight into @p data - the single-channel
 *        counterpart of ::sensors_local_save (which asks every capable driver
 *        at once). Intended for on-demand "live preview" callers such as the
 *        Weather page's Sensor Mapping table, which needs the current reading
 *        of whichever one channel is selected in a given row, not an
 *        aggregate pass over the whole registry.
 *
 * @param index  Registry position (0..sensors_local_count()-1), i.e. the same
 *               index used as a <select> option value on the Weather/Telemetry
 *               channel pickers.
 * @param data   Caller-owned container to be filled in place, same contract as
 *               ::sensors_local_save (arrays/qty must already be set up).
 * @param kind   Bit mask of payload families requested; only the bits also
 *               advertised in the driver's @c capabilities are actually asked
 *               for.
 * @return ESP_OK if the driver was found, (lazily) initialised and asked to
 *         save; ESP_ERR_NOT_FOUND if @p index is out of range or the slot is
 *         NULL; ESP_ERR_INVALID_ARG if @p data is NULL or @p kind is
 *         ::SENSOR_LOCAL_DATA_NONE; whatever the driver's own init()/save()
 *         returned otherwise.
 */
esp_err_t sensors_local_save_one(size_t index, weather_telemetry_data_t *data, sensor_local_data_kind_t kind);


/**
 * @brief Auto-register a statically defined driver descriptor at program
 *        start-up, before app_main, with no edit to the core.
 *
 * Place this at file scope in a driver .c after defining a
 * @c static sensor_local_driver_t. The generated constructor simply calls
 * ::sensors_local_register, so the driver "loads itself dynamically" into the
 * registry:
 * @code
 *     static sensor_local_driver_t my_drv = { .name = "bme280",
 *                                             .capabilities = SENSOR_LOCAL_DATA_WEATHER,
 *                                             .save = my_save };
 *     SENSORS_LOCAL_DRIVER_AUTOREGISTER(my_drv);
 * @endcode
 *
 * @param drv_sym  The identifier of the @c static ::sensor_local_driver_t.
 */
#define SENSORS_LOCAL_DRIVER_AUTOREGISTER(drv_sym)                                                                                                             \
    __attribute__((constructor)) static void _sensors_local_autoreg_##drv_sym(void) {                                                                          \
        (void)sensors_local_register(&(drv_sym));                                                                                                              \
    }

#endif /* SENSORS_LOCAL_H_ */
