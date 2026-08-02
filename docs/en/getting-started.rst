.. _en-getting-started:

===============
Getting Started
===============

Prerequisites
=============

* **ESP-IDF v5.1 or newer** (locked/tested at **5.5.4** — see
  ``dependencies.lock``).
* An ESP32 with **≥ 4 MB flash**.
* The IDF component manager fetches ``joltwallet/littlefs`` and, via the
  ``sensors_local`` component, ``esp-idf-lib/bmp180`` (which pulls in
  ``i2cdev`` + ``esp_idf_lib_helpers``) automatically.

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
   * - Hostname
     - ``esp32idf_APRS``
   * - CPU frequency
     - 240 MHz
   * - Time zone
     - 0.0 (clock itself is always UTC)
   * - NTP hosts
     - ``pool.ntp.org``, ``time.google.com``, ``time.cloudflare.com``
   * - IGate
     - enabled, ``rf2inet`` on, ``inet2rf`` off
   * - Callsign / SSID
     - ``NOCALL`` / 10, passcode ``-1``
   * - APRS-IS host / port
     - ``aprs.dprns.com`` : 14580
   * - Satellite gate-call list
     - ``RS0ISS``, ``YBOX``, ``YBSAT``, ``PSAT``, ``W3ADO``, ``BJ1SI`` (up to 8, web-configurable)
   * - Duplicate-suppression cache / window
     - 20 entries / 30000 ms (web-configurable)
   * - Path preset 0
     - ``WIDE1-1,WIDE2-1``
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
     - enabled, RF + INET, encryption off

.. danger::

   **Change** ``NOCALL`` **and set a real passcode before transmitting.**
   Verify you are licensed for the frequency and duty cycle you are about to
   key up on.
