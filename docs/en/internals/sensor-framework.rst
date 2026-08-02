.. _en-sensor-framework:

====================
The Sensor Framework
====================

``sensors_local`` (``components/sensors_local/``) is the run-time framework that
lets real (or simulated) hardware sensors feed the own-station Weather Report and
Telemetry subsystems, **without the core ever needing a hard-coded list** of
"the sensors this build supports". If you want to attach a BME280, a DS18B20, an
ADS1115, a soil-moisture probe, a battery-voltage divider or anything else, this
is the mechanism to use.

Why a driver framework
======================

Earlier APRS firmwares in this lineage used a fixed-size array of "sensor slots"
in ``g_config``, each described by a numeric ``type``/``port``/``address`` that
some central ``switch`` statement interpreted. Every new sensor meant editing
that switch, recompiling, and hoping the numeric IDs did not collide.

``sensors_local`` inverts this:

* The core (``sensors_local.c``) knows **nothing** about any specific sensor. It
  only holds a list of opaque "driver" structs and calls a handful of function
  pointers on them.
* Every actual sensor lives in its **own ``.c`` file** under ``drivers/`` and
  adds itself to the list **automatically at start-up**, before ``app_main()``
  even runs, via a C constructor hidden behind the
  ``SENSORS_LOCAL_DRIVER_AUTOREGISTER`` macro.

The practical result: adding a sensor is "drop a new file in ``drivers/``, list
it in the component's ``CMakeLists.txt``, rebuild" — nothing in
``sensors_local.c``, ``weather.c``, ``sensors_local.h`` or any header needs to
change.

The two payload families
========================

A driver fills in application-level fields already grouped by APRS payload type,
defined in the separate ``weather_telemetry`` component:

.. list-table::
   :header-rows: 1
   :widths: 20 26 54

   * - Family
     - Bit
     - Destination struct / consumer
   * - **Weather**
     - ``SENSOR_LOCAL_DATA_WEATHER`` (``1u<<0``)
     - ``aprs_weather_report_t`` → ``weather.c`` → on-air WX beacon
   * - **Telemetry**
     - ``SENSOR_LOCAL_DATA_TELEMETRY`` (``1u<<1``)
     - ``aprs_telemetry_report_t`` (A1–A5 + B1–B8) → ``telemetry.c`` → ``T#nnn``
       beacon
   * - *(reserved)*
     - e.g. ``SENSOR_LOCAL_DATA_GPS = 1u<<2``
     - a future struct — see below

A single driver may advertise **either or both** bits in its ``capabilities``.
``SENSOR_LOCAL_DATA_ALL`` is the OR of every bit currently defined; it is
available for a consumer that genuinely wants every family at once. ``weather.c``
does **not** use it on its 1 Hz pass: it refreshes telemetry with an aggregate
``SENSOR_LOCAL_DATA_TELEMETRY`` call and then reads each weather field
individually with ``sensors_local_save_one(..., SENSOR_LOCAL_DATA_WEATHER)``, so
that every WX field honours the driver selected for it on the Weather page.

Anatomy of a driver
===================

Every driver is one instance of ``sensor_local_driver_t``:

.. code-block:: c

   struct sensor_local_driver {
       const char *name;      // stable, unique, human-readable id
       uint32_t capabilities; // OR of WEATHER / TELEMETRY (must be non-zero)

       sensor_local_init_fn_t   init;   // optional one-time bring-up (may be NULL)
       sensor_local_save_fn_t   save;   // REQUIRED: reads the sensor
       sensor_local_deinit_fn_t deinit; // optional tear-down (may be NULL)

       const sensor_local_properties_t *properties; // which WX fields / TLM channels

       void *ctx; // driver-private state, opaque to the registry

       bool initialized; // registry-owned
       bool failed;       // registry-owned
   };

The three function-pointer roles:

* **``init(self)``** — called at most once, lazily, the first time the driver is
  needed (or eagerly at boot). Open the bus, probe the chip, allocate private
  state. Return ``ESP_OK`` on success; any other value **permanently marks the
  driver failed** and it is skipped from then on.
* **``save(self, data, kind)``** — THE common entry point, called on every
  refresh cycle (1 Hz). ``kind`` is already masked to only the bits both the
  caller wants *and* the driver advertised. The driver reads its sensor and
  writes straight into the caller-owned ``data`` container, setting each field's
  ``enabled[…]`` flag. It must tolerate an empty destination
  (``data->weather_qty == 0``) by doing nothing for that family.
* **``deinit(self)``** — optional mirror of ``init()``.

The registry
============

``sensors_local.c`` implements the registry as a small, mutex-protected,
heap-growable array of driver **pointers** (never copies — your ``static``
struct's storage is what lives in the table):

.. code-block:: text

   sensors_local_init()          // create the registry mutex
   sensors_local_register(drv)   // append; rejects NULL save, empty name,
                                 //   duplicate name, or capabilities == NONE
   sensors_local_unregister(name)// remove by name, calling deinit()
   sensors_local_count()         // how many drivers are registered
   sensors_local_get(index)      // fetch by position (Weather page dropdown)
   sensors_local_find(name)      // fetch by name
   sensors_local_init_all()      // eagerly init() every driver
   sensors_local_save(data,kind) // walk the table; init() lazily, then save()
   sensors_local_save_one(i,...) // read ONE driver by index (live preview)
   sensors_local_deinit()        // deinit() + drop everything

``sensors_local_register()`` can run **before the FreeRTOS scheduler exists**,
because ``SENSORS_LOCAL_DRIVER_AUTOREGISTER`` fires from a
``__attribute__((constructor))``. At that point the registry mutex does not yet
exist — the lock/unlock helpers are no-ops while it is NULL, which is safe only
because that whole phase is single-threaded. The first real
``sensors_local_init()`` call (from ``weather_start()``) creates the mutex and
makes every subsequent access thread-safe.

One driver failing its ``init()`` or returning an error from ``save()`` is
logged and **skipped**; it never aborts the pass for the other drivers.

End-to-end data flow
====================

.. code-block:: text

   boot (before app_main)
     └─ each drivers/*.c constructor → SENSORS_LOCAL_DRIVER_AUTOREGISTER
          → sensors_local_register(&my_driver)

   weather_start()  (once, at boot)
     ├─ sensors_local_init()          ← creates the registry mutex
     ├─ sensors_local_init_all()      ← runs init() on every driver
     └─ registers weather_service_1hz() and weather_beacon_service()

   weather_service_1hz()   (1 Hz)
     ├─ clears the container's "enabled" flags
     ├─ sensors_local_save(&data, SENSOR_LOCAL_DATA_TELEMETRY)
     │    └─ each TELEMETRY-capable driver: init() lazily, then save()
     ├─ for each WX field f with wx_sensor_enable[f] and a channel assigned:
     │    └─ sensors_local_save_one(wx_sensor_ch[f], &scratch, ..._WEATHER)
     │         └─ copy just that field into the live report
     └─ folds any "Averaged" field into a running sum/count

   weather_beacon_service()   (every wx_interval s, if wx_en)
     ├─ resolve fields (live or averaged, per checkbox)
     ├─ build the "!lat/lon_WIND…" TNC2 line
     └─ transmit on RF and/or APRS-IS

The key point for anyone adding a sensor: **you never call anything from
``weather.c`` or the web admin yourself.** Registering the driver is the entire
integration; the 1 Hz refresh, the averaging, the on-air WX encoding and the
channel picker all discover it through the registry.

Adding a sensor, step by step
=============================

#. **Decide the family.** A BME280/DS18B20 is Weather; a battery divider, reed
   switch or soil probe is Telemetry (analog or digital); a combo board can be
   both.
#. **Copy a skeleton.** Copy the matching example driver
   (``drivers/example/sensor_local_weather_example.c`` or
   ``…_telemetry_example.c``) into a new folder, e.g. ``drivers/bme280/``, with
   its own ``bme280_properties.h``.
#. **Fill in ``init()``** — configure/probe the bus, read the chip-ID, allocate
   calibration storage into ``self->ctx``, return ``ESP_OK`` only when confident.
#. **Fill in ``save()``** — read the sensor, convert to the engineering units
   ``weather_telemetry.h`` documents (°F, mph, tenths of mb, hundredths of an
   inch…), write the value(s) and set the matching ``enabled[…]`` flag(s).
   Always check ``kind`` and the destination pointers first.
#. **Declare the descriptor** and ``SENSORS_LOCAL_DRIVER_AUTOREGISTER(...)`` it.
   ``name`` must be unique — it is what shows in the Weather page's dropdown.
#. **List the source** in ``components/sensors_local/CMakeLists.txt`` and
   ``idf.py build``. The component links ``WHOLE_ARCHIVE`` so the linker's
   ``--gc-sections`` cannot drop an object whose only reference is its own
   constructor.
#. **Map it on the Weather (or Telemetry) page** — your driver's name now
   appears in every relevant field's channel dropdown automatically.

Multiple instances, error handling, thread safety
=================================================

* **Multiple instances** of the same sensor type coexist: give each a distinct
  ``name`` (``bme280-indoor`` / ``bme280-outdoor``), its own ``ctx``, and its own
  address/bus/GPIO baked into that ``ctx``.
* An ``init()`` error marks the driver failed **permanently** (until
  unregister+register). A ``save()`` error is logged and skipped **for that one
  cycle only** — the next tick tries again, so an occasional bus hiccup does not
  disable the driver.
* The registry calls are all mutex-protected. A driver's own
  ``init()``/``save()``/``deinit()`` are **not** wrapped in any lock by the
  framework — if a driver's ``ctx`` is touched from more than the 1 Hz refresh
  (e.g. an ISR), the driver is responsible for its own synchronisation.

The built-in BMP180 driver
==========================

``drivers/bmp180/bmp180.c`` is a real I2C temperature/pressure driver built on
``esp-idf-lib/bmp180``. Its I2C pins are configurable by ``#define`` in
``BMP180.h`` (default GPIO21 = SDA, GPIO22 = SCL); those pins are excluded from
every GPIO selector in the web admin so they cannot be double-assigned. It
advertises Weather and writes temperature and barometric pressure. It is gated
behind ``CONFIG_SENSORS_LOCAL_BMP180_DRIVER``.

Adding a whole new sensor *kind*
================================

Weather and Telemetry are not the only families the framework can carry. To add,
say, GPS:

#. Add ``SENSOR_LOCAL_DATA_GPS = 1u << 2`` to ``sensor_local_data_kind_t`` and OR
   it into ``SENSOR_LOCAL_DATA_ALL``.
#. Add the destination struct a GPS fix lands in (to ``weather_telemetry.h``).
#. Write driver(s) whose ``capabilities`` include the new bit.
#. Filter the registry with ``driver->capabilities & SENSOR_LOCAL_DATA_GPS``
   wherever a consumer needs the new kind. The registry, ``sensors_local_save()``
   and every existing driver are completely unaffected.
