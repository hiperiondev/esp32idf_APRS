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
     - The resident ``app_config_t`` (system, station, Wi-Fi, IGate, BrandMeister, digi,
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
   * - ``/storage/telegram.json``
     - The Telegram bot's whole configuration: the enable switch, the bot
       token, the administrator identifier, the Mini App address and the
       authorized user and group chat lists.

All five use the same streaming writer, each under its own mutex, each with an
explicit ``setvbuf()`` to avoid a lazy large stdio-buffer allocation mid-write.
The ``setvbuf()`` buffer is a single static object shared by all five stores,
since the filesystem-wide writer gate keeps two saves from overlapping.

Factory reset
=============

``POST /default`` (the *factory reset* button on the System page) calls
``app_config_factory_reset()``, which wipes the configuration back to
``app_config_set_defaults()`` and persists it. It does not, by itself, remove the
separate telemetry/bulletins/objitems files — those regenerate defaults on next
access if deleted via the Storage page.

BrandMeister interconnect keys
==============================

.. list-table::
   :header-rows: 1
   :widths: 25 15 60

   * - Key
     - Type
     - Meaning
   * - ``bmEn``
     - bool
     - BrandMeister interconnect master switch. Off by default.
   * - ``bmMonitor``
     - bool
     - Intent to run the ``u/APBM*`` worldwide subscription. Forced off on load
       when ``inet2rf`` is on and ``inet2rfRangeEn`` is off, so a hand-edited
       file cannot bypass the interlock.
   * - ``bmMsgInetOnly``
     - bool
     - Route messages for BrandMeister addressees over APRS-IS only. On by
       default; can only ever remove the RF leg.
   * - ``bmGateways``
     - array of 4 strings
     - Optional entry-station callsigns for the third classifier test. A
       trailing ``*`` matches by prefix.
   * - ``inet2rfRangeEn``
     - bool
     - Enable the INET→RF range gate. Off by default.
   * - ``inet2rfRangeKm``
     - number
     - INET→RF range gate radius in km, 0 = unlimited. Clamped to
       0…20038 on load.
