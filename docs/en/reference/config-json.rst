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
       initial interval, decay ramp, expiry).
   * - ``/storage/objitems.json``
     - The five APRS objects/items (name, position, symbol, course/speed,
       comment, interval, permanent flag).
   * - ``/storage/winlink.json``
     - The replies the Winlink service has sent back, oldest first. The account
       settings themselves are ``wl*`` keys in ``config.json``; only the
       replies live here, so clearing them never touches the configuration.
   * - ``/storage/telegram.json``
     - The Telegram bot's whole configuration: the enable switch, the bot
       token, the administrator identifier, the Mini App address and the
       authorized user and group chat lists.

All six use the same streaming writer, each under its own mutex, each with an
explicit ``setvbuf()`` to avoid a lazy large stdio-buffer allocation mid-write.
The ``setvbuf()`` buffer is a single static object shared by all six stores,
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

Winlink (APRSLink) keys
=======================

.. list-table::
   :header-rows: 1
   :widths: 25 15 60

   * - Key
     - Type
     - Meaning
   * - ``wlEnable``
     - bool
     - Winlink client master switch. Off by default: the client needs an
       account and a password before it can do anything.
   * - ``wlServiceCall``
     - string
     - Callsign of the APRSLink service. ``WLNK-1`` by default; an empty value
       loads as that default.
   * - ``wlPassword``
     - string
     - Winlink account password, up to 16 characters. Never transmitted: a
       login challenge names character positions and only those characters are
       sent back.
   * - ``wlUseMsgCall``
     - bool
     - Use ``msgMycall`` as the Winlink identity. On by default, because that
       callsign is what the outgoing frame carries and therefore what the
       service sees.
   * - ``wlMyCall``
     - string
     - Winlink identity when ``wlUseMsgCall`` is off. The service keys the
       mailbox on its base callsign, without the SSID.
   * - ``wlAutoLogin``
     - bool
     - Open a session by itself when a command is queued while idle. On by
       default.
   * - ``wlSessionMaxMin``
     - number
     - Local session lifetime in minutes, 5…180, 110 by default. Kept below the
       service's own two-hour expiry so this station gives a session up first.
       Clamped on load.
   * - ``wlPollMin``
     - number
     - Minutes between unprompted listings of pending mail, 0…1440. 0 never
       asks, which is the default. Clamped on load.
   * - ``wlCommentEn``
     - bool
     - Append the Winlink notification marker to the beacon comment, so the
       service knows this station reads its mail. Off by default.
   * - ``wlInetOnly``
     - bool
     - Keep this station's own Winlink traffic off the air while it has an
       APRS-IS uplink. On by default; can only ever remove the RF leg.
   * - ``wlGateExempt``
     - bool
     - Let a reply from the service reach RF even when its addressee is also
       seen on APRS-IS. On by default; lifts that one message-gating condition
       and no other, and only for ``wlServiceCall``.
