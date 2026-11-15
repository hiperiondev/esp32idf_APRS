.. _en-administration:

======================
Station administration
======================

Five pages look after the device rather than the APRS services: *Wireless*,
*System*, *File Storage*, *Firmware* and *Logs*.

Wireless
========

.. figure:: /_static/webadmin/wireless_top.png
   :alt: The Wireless page, as it opens.
   :align: center
   :width: 100%

   The *Wireless* page, as it opens.

.. figure:: /_static/webadmin/wireless_fs00.png
   :alt: WiFi Mode — Wi-Fi mode - off, client (STA), access point (AP) or both - and the transmit power, up to 20 dBm.
   :align: center
   :width: 90%

   **WiFi Mode** — Wi-Fi mode - off, client (STA), access point (AP) or both - and the transmit power, up to 20 dBm.

.. figure:: /_static/webadmin/wireless_fs01.png
   :alt: Access Point (AP) — the station's own access point: network name, password and channel (1 to 13).
   :align: center
   :width: 90%

   **Access Point (AP)** — the station's own access point: network name, password and channel (1 to 13).

.. figure:: /_static/webadmin/wireless_fs02.png
   :alt: WiFi Client — one of the five client slots, each with its own Enable switch, network name and password.
   :align: center
   :width: 90%

   **WiFi Client** — one of the five client slots, each with its own *Enable* switch, network name and password.

The slots are alternative sets of credentials, not a failover list: at boot
the first enabled slot with a network name is the one used, so moving a unit
between a home network and a phone's hotspot is a matter of ticking a
different slot. *AP + STA* keeps the access point up while the client is
connected, which is the safe choice while setting a station up: the web admin
stays reachable on ``192.168.4.1`` even when the client credentials are
wrong. The *WIFI SCAN* button above the slots searches for networks in range and
offers their names as suggestions in every SSID field. Network settings
are described in :ref:`en-networking`.

System
======

.. figure:: /_static/webadmin/system_top.png
   :alt: The System page, as it opens.
   :align: center
   :width: 100%

   The *System* page, as it opens.

.. figure:: /_static/webadmin/system_fs00.png
   :alt: Chip — chip model, cores, revision and flash size, and the CPU clock (80, 160 or 240 MHz), applied immediately and on every boot.
   :align: center
   :width: 90%

   **Chip** — chip model, cores, revision and flash size, and the CPU clock (80, 160 or 240 MHz), applied immediately and on every boot.

The audio modem needs 240 MHz: at a lower clock the receive chain cannot keep
up with the samples.

.. figure:: /_static/webadmin/system_fs01.png
   :alt: Web Admin Login — the administrator account (admin / admin from the factory). Change it before the station is reachable from any network you do not control.
   :align: center
   :width: 90%

   **Web Admin Login** — the administrator account (``admin`` / ``admin`` from the factory). Change it before the station is reachable from any network you do not control.

.. figure:: /_static/webadmin/system_fs02.png
   :alt: Web Read-Only Login — an optional second account that can see every page but change nothing.
   :align: center
   :width: 90%

   **Web Read-Only Login** — an optional second account that can see every page but change nothing.

The two roles are described in :ref:`en-web-admin`.

.. figure:: /_static/webadmin/system_fs03.png
   :alt: Time — NTP time synchronisation: enable, three servers, resync interval and the time zone used for the dashboard's date.
   :align: center
   :width: 90%

   **Time** — NTP time synchronisation: enable, three servers, resync interval and the time zone used for the dashboard's date.

Every timestamp the station sends on air is UTC whatever the time zone. The
bottom of the page holds the **factory reset** button, which restores every
setting to its default and reboots.

File storage
============

.. figure:: /_static/webadmin/storage_main.png
   :alt: The LittleFS browser: usage, upload, format and one row per file with Download and Delete.
   :align: center
   :width: 90%

   The LittleFS browser: usage, upload, format and one row per file with *Download* and *Delete*.

Each service keeps its settings in its own JSON file, so a single page can be
backed up, restored or copied to another station by downloading and uploading
its file. The Telegram root certificate is uploaded here as well. See
:ref:`en-storage-ota`.

Firmware and OTA
================

.. figure:: /_static/webadmin/about_top.png
   :alt: The Firmware page, as it opens.
   :align: center
   :width: 100%

   The *Firmware* page, as it opens.

.. figure:: /_static/webadmin/about_fs00.png
   :alt: Firmware — project, version, build date, ESP-IDF version and the running partition.
   :align: center
   :width: 90%

   **Firmware** — project, version, build date, ESP-IDF version and the running partition.

.. figure:: /_static/webadmin/about_fs01.png
   :alt: OTA Update — the OTA update: choose a .bin and Upload & Flash.
   :align: center
   :width: 90%

   **OTA Update** — the OTA update: choose a ``.bin`` and *Upload & Flash*.

The image is written to the inactive slot while the station keeps running, and
the station switches over only once it is complete and verified. An image that
fails to come up is rolled back automatically on the next restart. Only the
first flash has to be done over USB.

Console logs
============

.. figure:: /_static/webadmin/logs_fs00.png
   :alt: Console Log — the serial console mirrored in the browser: Start / Stop and the last 50 lines.
   :align: center
   :width: 90%

   **Console Log** — the serial console mirrored in the browser: *Start* / *Stop* and the last 50 lines.

The page shows what the station prints on its serial port, so a station on a
mast can be diagnosed without a cable. Nothing is recorded or written to
flash; the capture stops when the page is left. Besides the per-service lines
(received frames, beacons, APRS-IS), a normal build prints a heap line every
10 seconds and the stack headroom of every task once an hour; both, and the
optional per-heap and allocation brackets, are selected in ``menuconfig`` →
*APRS heap instrumentation*. They are the first thing to look at when a
station misbehaves after hours of running (see :ref:`en-troubleshooting`).
