.. _en-tracker:

=======
Tracker
=======

The tracker sends this station's own position as it moves (or from a fixed
point when no receiver is fitted). It runs on the shared beacon scheduler
like every other beacon; the encoding options it offers - compressed and
Mic-E positions, SmartBeaconing, PHG, status and frequency blocks - are
described in detail in :ref:`en-beacons`. This page is the operator's view.

.. figure:: /_static/webadmin/tracker_top.png
   :alt: The Tracker page, as it opens.
   :align: center
   :width: 100%

   The *Tracker* page, as it opens.

.. figure:: /_static/webadmin/tracker_fs00.png
   :alt: Tracker — enable, Use My Station Data, transmission over RF and/or APRS-IS, the DDHHMMz timestamp and the fixed beacon interval.
   :align: center
   :width: 90%

   **Tracker** — enable, *Use My Station Data*, transmission over RF and/or APRS-IS, the ``DDHHMMz`` timestamp and the fixed beacon interval.

.. figure:: /_static/webadmin/tracker_fs01.png
   :alt: Station — callsign, SSID (9 by default, the usual SSID of a mobile), path, and the fixed position with Use GPS and Use live GPS fix.
   :align: center
   :width: 90%

   **Station** — callsign, SSID (9 by default, the usual SSID of a mobile), path, and the fixed position with *Use GPS* and *Use live GPS fix*.

The two GPS switches do different things:

* **Use GPS** copies the receiver's fix into the fixed latitude, longitude and
  altitude fields while the page is open; what is saved is a fixed position.
* **Use live GPS fix** leaves the fixed position as the fallback and makes
  every transmission read the receiver at that moment. This is what a mobile
  tracker needs, and what SmartBeaconing needs to have a speed and a course
  to work from. When the receiver has no fix, the beacon falls back to the
  fixed position and the fixed interval.

.. figure:: /_static/webadmin/tracker_fs02.png
   :alt: SmartBeaconing — the SmartBeaconing parameters: slow and fast intervals, low and high speeds, turn angle, turn slope and minimum turn time.
   :align: center
   :width: 90%

   **SmartBeaconing** — the SmartBeaconing parameters: slow and fast intervals, low and high speeds, turn angle, turn slope and minimum turn time.

.. figure:: /_static/webadmin/tracker_fs03.png
   :alt: Options — compressed position, Mic-E encoding with its position comment, altitude, PHG, the station symbol and the comment.
   :align: center
   :width: 90%

   **Options** — compressed position, Mic-E encoding with its position comment, altitude, PHG, the station symbol and the comment.

.. figure:: /_static/webadmin/tracker_fs04.png
   :alt: Status Beacon — status beacon text and interval.
   :align: center
   :width: 90%

   **Status Beacon** — status beacon text and interval.

.. figure:: /_static/webadmin/tracker_fs05.png
   :alt: Repeater Radio Parameters — frequency, duplex, offset and tone written at the start of the comment, for a station that announces the frequency it listens on.
   :align: center
   :width: 90%

   **Repeater Radio Parameters** — frequency, duplex, offset and tone written at the start of the comment, for a station that announces the frequency it listens on.
