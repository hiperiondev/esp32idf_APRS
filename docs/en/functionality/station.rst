.. _en-station:

============================
Station identity and symbols
============================

The *Station* page holds the identity every service shares: the callsign, the
position and the station-wide on-air options. Each service page (IGate,
digipeater, tracker, weather, messaging) has its own callsign, SSID and
position, and a *Use My Station Data* switch that copies them from here, so a
station is normally configured once on this page.

.. figure:: /_static/webadmin/station_top.png
   :alt: The Station page, as it opens.
   :align: center
   :width: 100%

   The *Station* page, as it opens.

The Station fieldset
====================

.. figure:: /_static/webadmin/station_fs00.png
   :alt: Station — callsign, latitude, longitude and altitude, with Use GPS, followed by the options applied to every report the station sends.
   :align: center
   :width: 90%

   **Station** — callsign, latitude, longitude and altitude, with *Use GPS*, followed by the options applied to every report the station sends.

* **Use GPS** greys out the three position fields and fills them once a second
  from the GNSS receiver (``/gps/live``) while it is ticked, rounded to the
  precision the fields accept. It needs the receiver enabled on the *GPS*
  page.
* **Position ambiguity** blanks the last digits of every position this station
  sends, from full precision down to about 60 nautical miles.
* **Maidenhead locator in status reports** prefixes status texts with the
  station's grid square.
* **Zulu timestamp in status reports** prefixes status texts with a ``DDHHMMz``
  timestamp.
* **DAO precision extension** appends the ``!DAO!`` field that carries one
  more digit of latitude and longitude.
* **Request APRS-IS not to archive** adds the ``!x!`` marker.
* **Beam heading** and **ERP** close status reports with the meteor-scatter
  heading/power suffix.

How each of these is encoded is described in :ref:`en-beacons`.

The PHG fieldset
================

.. figure:: /_static/webadmin/station_fs01.png
   :alt: PHG — transmitter power, antenna gain, height and directivity of the station, and the resulting PHG text.
   :align: center
   :width: 90%

   **PHG** — transmitter power, antenna gain, height and directivity of the station, and the resulting ``PHG`` text.

The IGate and digipeater pages use these values when their *Data Extension* is
set to PHG with *Use My Station Data*.

The symbol picker
=================

Every page that sends a position has a *Station Symbol* row: the icon, the
symbol table and the symbol code. The ``…`` link next to it opens the
*APRS Symbols* page, a full chart of both tables.

.. figure:: /_static/webadmin/symbol_top.png
   :alt: The APRS Symbols page, as it opens.
   :align: center
   :width: 100%

   The *APRS Symbols* page, as it opens.

.. figure:: /_static/webadmin/symbol_fs00.png
   :alt: Quick Pick — the most used symbols, one click away.
   :align: center
   :width: 90%

   **Quick Pick** — the most used symbols, one click away.

.. figure:: /_static/webadmin/symbol_fs01.png
   :alt: Currently configured symbols — the symbol each service currently transmits.
   :align: center
   :width: 90%

   **Currently configured symbols** — the symbol each service currently transmits.

Below these the page lists the whole primary (``/``) and alternate (``\``)
tables. Choosing a symbol writes the table and code back into the page the
picker was opened from; nothing is saved until that page is saved. As on the
dashboard, the icons are drawn from ``aprs.dprns.com`` and fall back to the
plain symbol character when that site cannot be reached.
