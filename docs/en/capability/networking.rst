.. _en-networking:

==========
Networking
==========

Wi-Fi bring-up (``main/main.c``) is one of the most heavily instrumented parts
of the firmware, because "I switched to Station mode and nothing happened" was a
recurring, silent failure in earlier revisions. Every path now logs what it did.

Wi-Fi modes
===========

``g_config.wifi_mode`` selects the interface configuration, matching the
Wireless page. The stored numbers are the ``WIFI_MODE_CFG_*`` selectors in
``main/include/app_config.h``, which every consumer uses by name:

* ``0`` = ``WIFI_MODE_CFG_OFF``
* ``1`` = ``WIFI_MODE_CFG_STA`` (station)
* ``2`` = ``WIFI_MODE_CFG_AP`` (access point) — ``WIFI_MODE_CFG_DEFAULT``, the
  factory value; the device is always reachable
* ``3`` = ``WIFI_MODE_CFG_APSTA``

``wifi_init()`` brings the SoftAP up for ``WIFI_MODE_CFG_OFF`` and for any value
above ``WIFI_MODE_CFG_MAX``, so no stored value can leave a headless station
without a web admin to fix it from.

Up to five STA profiles (``WIFI_STA_NUM = 5``) are stored, each with its own
Enable checkbox. The **first enabled entry with a non-empty SSID** is pushed to
the driver; multi-AP failover is noted as "can be added later".

Robust station connection
=========================

Several deliberate fixes make the station path reliable:

* **Connect from ``WIFI_EVENT_STA_START``, not immediately.**
  ``esp_wifi_connect()`` is only legal once the station interface has actually
  started, which the driver signals with ``WIFI_EVENT_STA_START``. Calling it
  right after ``esp_wifi_start()`` loses that race and returns
  ``ESP_ERR_WIFI_NOT_STARTED`` — no association, no disconnect event, no retry.
  The connect is issued from the STA_START handler and every attempt logs its
  result.
* **Fixed reconnect interval, armed on a timer.** Every disconnect waits the
  same ``RECONNECT_INTERVAL_MS`` (5 s) before the next ``esp_wifi_connect()``,
  armed on an ``esp_timer`` — **not** a ``vTaskDelay()`` inside the event handler, which
  would stall the shared event loop (including the very ``STA_GOT_IP`` it is
  waiting for) and, in a tight disconnect loop, starve the idle task until the
  task watchdog fired.
* **PMF capability advertised.** A zeroed ``wifi_config_t`` leaves
  ``pmf_cfg.capable = false``, and WPA3 / WPA2-with-PMF-required APs simply
  refuse such a station. The firmware sets *capable, not required*, which works
  against both old and new APs.
* **AP+STA fallback.** STA-only with nothing to join would leave the device
  unreachable, so it falls back to AP+STA and says so — the web admin stays up.
* **Diagnostic dumps.** If no STA slot is enabled with an SSID, the firmware
  dumps every slot and tells you which mistake it is ("enabled, but the SSID is
  EMPTY" vs "has an SSID, but 'Enable' is not ticked").

Disconnect reason codes are logged (they used to be discarded):

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Reason
     - Meaning
   * - 15 (4WAY_HANDSHAKE_TIMEOUT), 204 (NOT_AUTHED)
     - wrong password
   * - 201 (NO_AP_FOUND)
     - SSID not visible: wrong name, out of range, or 5 GHz-only
   * - 2 / 8 / 200
     - ordinary roaming / AP-side drops

The "do we have internet" flag
==============================

``net_state.c`` holds a single boolean that becomes true only on
``IP_EVENT_STA_GOT_IP`` and false on disconnect or AP-only mode. The IGate polls
it and waits for a **real IP** before ever attempting an APRS-IS connection —
being merely associated to an AP is not enough.

Wi-Fi scan
==========

The Wireless page's scan temporarily flips an AP-only radio to AP+STA. A
``s_staEnabled`` flag gates every automatic ``esp_wifi_connect()`` so the event
handler does not fight the scan.

TX power
========

The Wireless page's TX power (dBm) is converted ×4 to quarter-dBm for
``esp_wifi_set_max_tx_power()``. The driver refuses anything below 8 quarter-dBm,
so the accepted range is ``WIFI_TX_POWER_DBM_MIN``..``WIFI_TX_POWER_DBM_MAX``
(2-20 dBm, ``app_config.h``) rather than 0-20: a lower setting is rejected and
leaves the previous power in force, which reads as "minimum power" while being
"no change". The value is clamped in the Wireless POST handler and again in
``config_from_json()``, so the form bounds are a convenience and the stored
value is what is actually enforced.

Time sync
=========

``time_sync.c`` runs SNTP against three hosts. It is now a non-blocking state
machine folded into the 1 Hz service tick, and it pins the system clock to UTC
(``TZ=UTC0``) — the APRS spec's zulu timestamps require it.

The same file also carries a built-in table of fixed UTC offsets (no DST rules),
selected by ``g_config.timezone_idx`` from the *Time* section of the System
page. This is a **display convenience only**: ``time_sync_format_local()`` adds
the selected entry's ``utc_offset_s`` to the UTC instant and reads the result
back through ``gmtime_r()``. The conversion is pure arithmetic — it takes no
lock, allocates nothing and never writes the process-wide ``TZ``, which is set
to ``UTC0`` once during SNTP setup and never rewritten afterwards. That matters
because the dashboard polls the rendered date/time once per second for the whole
uptime of the device. The system clock, every APRS timestamp and every other
time-formatting call in the firmware (all of which use ``gmtime_r()``) remain
UTC regardless of the selection.

CPU frequency
=============

``cpu_freq.c`` applies the System page's 80/160/240 MHz selection via
``esp_pm_configure()``. Without this the setting was stored and displayed but
never changed the clock.
