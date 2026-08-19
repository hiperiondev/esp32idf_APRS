.. _en-config-json:

=====================
Configuration Storage
=====================

The resident configuration persists to ``/storage/config.json`` on LittleFS.
This reference summarises the storage mechanics; for the field groups see
:ref:`en-configuration`.

Mechanics
=========

* **Path:** ``/storage/config.json``.
* **Loaded** with cJSON; **saved** by a streaming token-at-a-time writer.
* **Atomic save:** write ``config.json.tmp``, then rename.
* Missing or corrupt → defaults applied and immediately saved, so the file
  always exists and is consistent.
* Field names / JSON keys are kept 1:1 with the reference project, so old files
  load unchanged; unknown keys are ignored.

Other persistent files
======================

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - File
     - Contents
   * - ``/storage/config.json``
     - The resident ``app_config_t`` (system, station, Wi-Fi, IGate, digi,
       tracker, weather, GPS, modem, message).
   * - ``/storage/telemetry.json``
     - Telemetry channel-0 config (``telemetry_config_t``): analog A1–A5,
       digital B1–B8, report parameters, definition-message toggles.
   * - ``/storage/bulletins.json``
     - The five APRS bulletins (addressee identifier and group, text, RF/INET,
       interval, expiry).
   * - ``/storage/objitems.json``
     - The five APRS objects/items (name, position, symbol, course/speed,
       comment, interval, permanent flag).

All four use the same streaming writer, each under its own mutex, each with an
explicit ``setvbuf()`` to avoid a lazy large stdio-buffer allocation mid-write.

Factory reset
=============

``POST /default`` (the *factory reset* button on the System page) calls
``app_config_factory_reset()``, which wipes the configuration back to
``app_config_set_defaults()`` and persists it. It does not, by itself, remove the
separate telemetry/bulletins/objitems files — those regenerate defaults on next
access if deleted via the Storage page.
