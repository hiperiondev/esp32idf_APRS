.. _en-getting-started:

===============
Getting Started
===============

Prerequisites
=============

* **ESP-IDF v6.0 or newer** (locked/tested at **6.0.2** — see
  ``dependencies.lock``).
* An ESP32 with **≥ 4 MB flash**.
* The IDF component manager fetches ``joltwallet/littlefs``, ``espressif/cjson``
  and, via the
  ``sensors_local`` component, ``esp-idf-lib/bmp280`` and
  ``esp-idf-lib/bmp180`` (which pull in ``i2cdev`` +
  ``esp_idf_lib_helpers``) automatically.

Building and flashing
=====================

.. code-block:: bash

   . $IDF_PATH/export.sh

   cd workspace-APRS/esp32_APRS_igate

   idf.py set-target esp32          # sdkconfig already ships with target=esp32
   idf.py build
   idf.py -p /dev/ttyUSB0 flash monitor

Build in Spanish or Italian instead of English (see :ref:`en-localization`):

.. code-block:: bash

   idf.py build -DLANGUAGE=LANG_ES
   idf.py build -DLANGUAGE=LANG_IT

.. tip::

   ``sdkconfig`` ships with ``CONFIG_COMPILER_OPTIMIZATION_DEBUG`` (``-Og``)
   and assertions on. Switch to ``-Os`` if you are tight on flash.

Memory budget
=============

The ESP32 on this design has no PSRAM, so every byte of internal DRAM the
build reserves statically is a byte the heap never gets. ``sdkconfig`` is
tuned for that, and the settings below are deliberate - raising any of them
lowers the *Min free heap* figure on the dashboard.

.. list-table::
   :header-rows: 1
   :widths: 44 10 46

   * - Setting
     - Value
     - Why
   * - ``CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM``
     - 6
     - ~1.6 KB each, allocated at ``esp_wifi_init()`` and held until Wi-Fi is
       de-initialised. Six matches ``CONFIG_ESP_WIFI_RX_BA_WIN``, which is the
       floor AMPDU RX wants.
   * - ``CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM`` / ``..._TX_...``
     - 12
     - Caps the Wi-Fi driver's peak heap claim. APRS traffic is a few hundred
       bytes per minute; the throughput these buffers buy is never used.
   * - ``CONFIG_LWIP_TCP_SND_BUF_DEFAULT`` / ``CONFIG_LWIP_TCP_WND_DEFAULT``
     - 2880
     - Two MSS per direction per connection. The only sustained transfer is an
       OTA image upload, which still saturates a LAN at this window.
   * - ``max_open_sockets`` in ``web_server_start()``
     - 4
     - httpd takes this plus 3 sockets of its own out of the
       ``CONFIG_LWIP_MAX_SOCKETS`` (10) pool. The remaining 3 are what the
       APRS-IS uplink, DNS and SNTP need to stay up while someone is browsing
       the admin pages.
   * - HTTPS server, certificate bundle, Wi-Fi Enterprise
     - disabled
     - The web admin is plain HTTP and the APRS-IS uplink is plain TCP, so no
       code path ever opens a TLS session: ``CONFIG_ESP_HTTPS_SERVER_ENABLE``,
       ``CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`` and
       ``CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT`` are all off in ``sdkconfig``.
       mbedTLS itself stays enabled — Wi-Fi crypto needs it — so
       ``CONFIG_MBEDTLS_TLS_ENABLED`` and its nested ``_SERVER``/``_CLIENT``
       options remain set at their defaults; what that costs at run time is
       only the per-session record buffers
       (``CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN``/``_OUT_CONTENT_LEN``, 4096 each),
       which are allocated per TLS session and so never allocated at all here.
       HTTP Basic auth decodes its credential pair
       with a small local RFC 4648 base64 decoder
       (``components/webconfig/include/web_base64.h``) instead of
       ``mbedtls_base64_decode()``, so no component declares an mbedTLS
       dependency for that call either; ``esp_wifi``/``esp_netif``/``lwip``
       still pull mbedTLS in transitively for WPA2 crypto, which is unaffected
       by this setting.

.. note::

   From ESP-IDF v6.0 the mbedTLS port calls ``psa_crypto_init()`` from a
   system startup hook, so PSA Crypto is live in every build that links
   mbedTLS, including this one, regardless of ``CONFIG_MBEDTLS_TLS_ENABLED``.
   That, together with mbedTLS 4.x's larger static footprint, is why the same
   firmware reports a lower free heap under v6.0 than it did under v5.2 with
   an otherwise identical configuration.

First boot
==========

#. On a fresh partition, LittleFS auto-formats and ``app_config_load()`` writes
   ``/storage/config.json`` from factory defaults.
#. The ESP32 comes up as a **Wi-Fi AP**: SSID ``esp32idf_APRS``, password
   ``esp32idf_APRS``, channel 1, WPA2-PSK, max 4 clients.
#. Join it and browse to the device (default ``http://192.168.4.1/``).
#. **Log in:** ``admin`` / ``admin`` — change this on the *System* page.
#. On *Wireless*: pick **Station** or **AP+STA**, tick **Enable** in a Wi-Fi
   Client block, enter SSID/password, Save.
#. On *IGate*: set your **callsign**, **SSID**, **passcode**, APRS-IS
   **host**/**port**, filter, coordinates, symbol, comment.
#. On *Radio / Modem*: enable the audio modem, pick the modulation, preamble,
   TX time slot; use **LOOP TEST** to verify the audio path.
#. Reboot (or Save — most settings re-apply live).

Notable factory defaults
========================

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Setting
     - Default
   * - Wi-Fi mode
     - AP (always reachable)
   * - AP SSID / pass
     - ``esp32idf_APRS`` / ``esp32idf_APRS``
   * - Web login
     - ``admin`` / ``admin``
   * - CPU frequency
     - 240 MHz
   * - System clock
     - always UTC (TZ=UTC0). The System page's timezone selector (default
       ``UTC``) only changes the local date/time shown on the dashboard; on-air
       timestamps stay zulu
   * - NTP hosts
     - ``pool.ntp.org``, ``time.google.com``, ``time.cloudflare.com``
   * - IGate
     - enabled, ``rf2inet`` on, ``inet2rf`` off
   * - Callsign / SSID
     - ``NOCALL`` / 10, passcode ``-1``
   * - Station coordinates
     - ``0.000`` / ``0.000``; every position beacon and status-report grid
       locator is withheld while a role's coordinates are still at this
       default
   * - APRS-IS servers
     - four failover slots, all preset to ``aprs.dprns.com`` : 14580, with only
       slot 1 enabled
   * - Satellite gate-call list
     - ``RS0ISS``, ``YBOX``, ``YBSAT``, ``PSAT``, ``W3ADO``, ``BJ1SI`` (up to 8, web-configurable)
   * - Duplicate-suppression cache / window
     - 20 entries / 30000 ms (web-configurable)
   * - Transmit duty-cycle limiter
     - disabled; ceiling 25 % of a rolling 10-minute window when enabled
   * - Path preset 0
     - ``WIDE1-1,WIDE2-1``
   * - Path selection
     - preset 0, for the IGate, digipeater, tracker and weather beacons alike
   * - Digipeater
     - disabled, SSID 1
   * - Tracker
     - disabled, SSID 9
   * - Audio modem
     - enabled, 1200 Bd Bell 202
   * - Preamble / TX slot
     - 300 ms / 2000 ms
   * - CSMA persistence
     - 63 (~25 % transmit chance per clear slot)
   * - RF TX buffers
     - 1
   * - Query responder
     - disabled; RF on, Internet off, minimum reply interval 30 s
   * - FX.25
     - off
   * - PTT
     - GPIO26 (polarity is compile-time)
   * - Messaging
     - enabled, RF + INET, alarm GPIO disabled

.. danger::

   **Change** ``NOCALL`` **and set a real passcode before transmitting.**
   Verify you are licensed for the frequency and duty cycle you are about to
   key up on.

   **Set the station coordinates too.** APRS has no "position unknown"
   coordinate, so this firmware treats the factory-default ``0.000`` /
   ``0.000`` pair — Null Island, not a real amateur station site — as "not
   yet configured" and withholds the Tracker, IGate and Digipeater position
   beacons (and the Maidenhead locator of their status reports) rather than
   putting a false fix in the Gulf of Guinea on the air. A beacon whose
   coordinates are still unset stays silent instead of transmitting; check
   the log if it does not seem to be sending anything.
