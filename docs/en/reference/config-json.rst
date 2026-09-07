.. _en-config-json:

=====================
Configuration Storage
=====================

The resident configuration persists to LittleFS as **one file per web admin
functionality**, each named after the page that owns it. This reference
summarises the storage mechanics; for the field groups see
:ref:`en-configuration`.

Mechanics
=========

* **One file per functionality**, under ``/storage``: ``system.json``,
  ``station.json``, ``wireless.json``, ``radio.json``, ``igate.json``,
  ``brandmeister.json``, ``digi.json``, ``tracker.json``, ``weather.json``,
  ``gps.json``, ``message.json``, ``winlink.json``, ``query.json``. There is no
  combined configuration file.
* **Loaded** with cJSON, one file at a time; **saved** by a streaming
  token-at-a-time writer.
* **Atomic save:** write ``<name>.json.tmp``, then rename.
* **A page saves only its own file.** The *My Station* page is the exception
  that names several, because its "Use My Station Data" mirrors write into five
  other services' fields.
* Missing, empty or corrupt → that file is rewritten from the defaults during
  the load, so every functionality always has a file and the device always
  comes up on a reachable web admin.
* Out of memory → **nothing** is written and the load reports a failure. A read
  or a parse that ran out of RAM says nothing about the file's content, so it
  must never take the path above: the reader scans the text without allocating
  anything to tell the two apart, and only genuinely unparseable bytes are
  overwritten.
* Field names / JSON keys are kept 1:1 with the reference project, so an
  operator moving between the two recognises them; unknown keys are ignored and
  a key a file does not carry keeps its documented default.

Other persistent files
======================

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - File
     - Contents
   * - ``/storage/telemetry.json``
     - Telemetry channel-0 config (``telemetry_config_t``): analog A1–A5,
       digital B1–B8, report parameters, definition-message toggles.
   * - ``/storage/bulletins.json``
     - The five APRS bulletins (addressee identifier and group, text, RF/INET,
       initial interval, decay ramp, expiry).
   * - ``/storage/objitems.json``
     - The five APRS objects/items (name, position, symbol, course/speed,
       comment, interval, permanent flag).
   * - ``/storage/telegram.json``
     - The Telegram bot's whole configuration: the enable switch, the bot
       token, the administrator identifier, the Mini App address and the
       authorized user and group chat lists.
   * - ``/storage/winlink_mail.json``
     - The replies the Winlink service has sent back, oldest first. The account
       settings themselves are the ``wl*`` keys in ``winlink.json``; only the
       replies live here, so clearing them never touches the configuration.

Every store uses the same streaming writer, each under its own mutex, each with
an explicit ``setvbuf()`` to avoid a lazy large stdio-buffer allocation
mid-write. The ``setvbuf()`` buffer is a single static object shared by all of
them, since the filesystem-wide writer gate keeps two saves from overlapping.

Every one of these files is created from its defaults during bring-up if it
does not exist, so a first boot leaves a complete set on flash without the
operator visiting a single page.

Factory reset
=============

``POST /default`` (the *factory reset* button on the System page) calls
``app_config_factory_reset()``, which wipes the configuration back to
``app_config_set_defaults()`` and rewrites **every** section file. It does not,
by itself, remove the separate telemetry/bulletins/objitems/telegram files —
those regenerate defaults on next access if deleted via the Storage page.

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
