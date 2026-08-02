.. _en-weather:

==============
Weather Report
==============

The ``weather`` subsystem (``main/weather.c``) is a fully working own-station
APRS Weather Report, not scaffolding. It owns the single shared
``weather_telemetry_data_t`` container that every local sensor driver writes
into, refreshes it from the ``sensors_local`` registry once per second, and
periodically encodes and transmits a standard APRS Weather Report on RF and/or
APRS-IS from the fields the operator mapped on the *Weather* web-admin page
(``g_config.wx_*``).

The three moving parts
======================

``weather_start()`` (called once at boot) sets up:

#. **The shared container.** ``weather_telemetry_data`` is wired to static
   backing storage for one ``aprs_weather_report_t`` and one
   ``aprs_telemetry_report_t``.
#. **The registry.** ``sensors_local_init()`` creates the registry mutex and
   ``sensors_local_init_all()`` runs each auto-registered driver's ``init()``.
#. **Two service callbacks.** ``weather_service_1hz()`` (run at 1 Hz by the
   APRS service tick) and ``weather_beacon_service()`` (run by the shared
   beacon scheduler).

The 1 Hz refresh
================

``weather_service_1hz()``:

#. Clears the "enabled" flags in the container, so a driver that stops reporting
   a field this cycle does not leave a stale value looking valid.
#. Refreshes the telemetry family with one aggregate call,
   ``sensors_local_save(&weather_telemetry_data, SENSOR_LOCAL_DATA_TELEMETRY)``:
   telemetry channels are not per-field selectable, so every TELEMETRY-capable
   driver contributes.
#. Resolves **each weather field independently** against the one driver the
   operator picked for it. A field is sampled only when it is both ticked
   (``g_config.wx_sensor_enable[f]``) and has a source channel assigned
   (``g_config.wx_sensor_ch[f] != SENSOR_LOCAL_CH_NONE``); the reading is taken
   with ``sensors_local_save_one(ch, &scratch, SENSOR_LOCAL_DATA_WEATHER)`` into
   a scratch container, and only that one field's value is copied into the live
   report. Using a scratch container per field is what stops a second registered
   WEATHER driver from overwriting a field already resolved from a different
   one, and is why the on-air packet always matches the per-field *Channel*
   column and the page's live *Value* preview.
#. Folds any field marked *Averaged* (a per-field checkbox on the Weather page)
   into a running sum/count.

Mappable weather fields
=======================

The on-air field list is the canonical APRS101 chapter 12 set plus the APRS 1.2
flood proposals, enumerated by ``wx_field_id_t``:

.. list-table::
   :header-rows: 1
   :widths: 34 22 44

   * - Field
     - On-air token
     - Unit
   * - Wind direction
     - ``ddd/``
     - degrees
   * - Wind speed (sustained)
     - ``/sss``
     - mph
   * - Wind gust
     - ``gXXX``
     - mph
   * - Temperature
     - ``tXXX``
     - °F
   * - Rain last hour
     - ``rXXX``
     - 1/100 in
   * - Rain last 24 h
     - ``pXXX``
     - 1/100 in
   * - Rain since midnight
     - ``PXXX``
     - 1/100 in
   * - Snow last 24 h
     - ``sXXX``
     - 1/10 in (APRS 1.2)
   * - Humidity
     - ``hXX``
     - %
   * - Barometric pressure
     - ``bXXXXX``
     - tenths of mb
   * - Luminosity
     - ``LXXX`` / ``lXXX``
     - W/m² (APRS 1.2)
   * - Flood height (feet)
     - ``FXXXX.X``
     - feet (APRS 1.2)
   * - Flood height (metres)
     - ``fXXXX.X``
     - metres (APRS 1.2)

The WX beacon
=============

``weather_beacon_service()`` transmits every ``g_config.wx_interval`` seconds
(only when ``wx_en`` is set):

#. **Resolve fields.** For each on-air WX token, it reads either the live value
   straight from the container or the averaged value accumulated by the 1 Hz
   refresh, depending on that field's *Averaged* checkbox — so an intermittent
   reporter still contributes a sane average.
#. **Build the packet.** It renders the standard
   ``!lat/lon_WIND/SPDgGUSTtTTTrRRRhHHbBBBBB…`` TNC2 line.
#. **Transmit** it on RF and/or APRS-IS per ``wx_2rf`` / ``wx_2inet``.

Locking
=======

Because a driver may be updating the container concurrently with the beacon
reading it, all access goes through ``weather_lock()`` / ``weather_unlock()``.
Treat ``weather_telemetry_data`` as read-only outside ``weather.c``.

.. seealso::

   :ref:`en-sensor-framework` — how to attach a real sensor (BME280, DS18B20,
   BMP180…) so its readings feed these fields.
