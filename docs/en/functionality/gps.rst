.. _en-gps:

=============
GNSS receiver
=============

A serial GNSS (GPS) module can be connected to the ESP32 to give the station a
live position, speed, course and UTC time. Any receiver that outputs standard
NMEA 0183 sentences at 9600 bit/s works (the common NEO-6M, ATGM336H and
HW-248 boards do out of the box); the firmware only listens and never sends
configuration commands to the module.

Wiring
======

.. list-table::
   :header-rows: 1
   :widths: 30 25 45

   * - Module pin
     - ESP32 pin
     - Note
   * - TX (NMEA out)
     - GPIO16 (UART2 RX)
     - the only line actually used
   * - RX
     - GPIO17 (UART2 TX)
     - reserved; nothing is ever sent
   * - VCC
     - 3V3 or 5V
     - as the module requires
   * - GND
     - GND
     -

The port, the pins and the 9600 8N1 line rate are compile-time constants in
``main/include/gps.h``. GPIO16/17 are not available on an ESP32-WROVER, where
they belong to the PSRAM; change the two pins there (see
:ref:`en-hardware`).

The GPS page
============

.. figure:: /_static/webadmin/gps_top.png
   :alt: The GPS Receiver page, as it opens.
   :align: center
   :width: 100%

   The *GPS Receiver* page, as it opens.

.. figure:: /_static/webadmin/gps_fs00.png
   :alt: GPS Receiver — the single switch the rest of the firmware tests before using the receiver. With it off the UART is not installed and the reader task does not run; moving it takes effect at once, without a reboot.
   :align: center
   :width: 90%

   **GPS Receiver** — the single switch the rest of the firmware tests before using the receiver. With it off the UART is not installed and the reader task does not run; moving it takes effect at once, without a reboot.

.. figure:: /_static/webadmin/gps_fs01.png
   :alt: Receiver Status — the Module Status badge and the link, navigation status, fix quality and fix mode.
   :align: center
   :width: 90%

   **Receiver Status** — the *Module Status* badge and the link, navigation status, fix quality and fix mode.

The badge condenses the page into one diagnosis:

* red **Disabled** - the switch is off, or the UART could not be installed;
* red **No data (check wiring)** - enabled, but no valid sentence arrived on
  GPIO16 within 10 seconds;
* amber **Searching (no fix)** - sentences arrive but the receiver has no
  navigation solution yet;
* green **Fix OK** - a valid fix is being reported.

.. figure:: /_static/webadmin/gps_fs02.png
   :alt: Position — latitude, longitude, altitude and geoid separation.
   :align: center
   :width: 90%

   **Position** — latitude, longitude, altitude and geoid separation.

A position is shown, and offered to the other pages' *Use GPS* option, only
while it comes from a current fix: as soon as the receiver reports no fix — a
``GGA`` quality of 0 or a void ``RMC`` — the station stops presenting the last
coordinates as where it is, and the *Serial Link* panel's fix age tells how
old the last fix is. A coordinate outside its range (latitude beyond 90°,
longitude beyond 180°, minutes of 60 or more) is never accepted, even from a
sentence whose checksum is good.

.. figure:: /_static/webadmin/gps_fs03.png
   :alt: Motion — ground speed, course and magnetic variation.
   :align: center
   :width: 90%

   **Motion** — ground speed, course and magnetic variation.

.. figure:: /_static/webadmin/gps_fs04.png
   :alt: Date and Time (UTC) — UTC date and time of the last fix.
   :align: center
   :width: 90%

   **Date and Time (UTC)** — UTC date and time of the last fix.

.. figure:: /_static/webadmin/gps_fs05.png
   :alt: Satellites and Accuracy — satellites used and in view and the HDOP / PDOP / VDOP figures.
   :align: center
   :width: 90%

   **Satellites and Accuracy** — satellites used and in view and the HDOP / PDOP / VDOP figures.

.. figure:: /_static/webadmin/gps_fs06.png
   :alt: Serial Link — sentences accepted and discarded (checksum or framing errors) and the age of the last sentence and of the last fix.
   :align: center
   :width: 90%

   **Serial Link** — sentences accepted and discarded (checksum or framing errors) and the age of the last sentence and of the last fix.

.. figure:: /_static/webadmin/gps_fs07.png
   :alt: Wiring (compile-time) — the compile-time wiring, shown for reference.
   :align: center
   :width: 90%

   **Wiring (compile-time)** — the compile-time wiring, shown for reference.

The live values come from ``/gps/values``, polled every second.

Who uses the fix
================

* **Use GPS** on the *Station*, *IGate*, *Digipeater*, *Tracker* and
  *Weather* pages fills that page's position fields from ``/gps/live`` while
  ticked.
* **Use live GPS fix** on the *Tracker* page reads the receiver at every
  transmission, and SmartBeaconing takes its speed and course from it.
* The Telegram bot's ``/status`` reports the receiver's switch.

Everything on this page is stored in ``/storage/gps.json``.
