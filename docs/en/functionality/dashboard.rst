.. _en-dashboard:

=====================
Dashboard and traffic
=====================

The *Dashboard* is the page the web admin opens on. It configures nothing: it
is the station's live status, refreshed by the browser while the page is open,
and the one place where received, digipeated, gated and transmitted traffic
can be watched as it happens.

.. figure:: /_static/webadmin/dashboard_top.png
   :alt: The Dashboard page, as it opens.
   :align: center
   :width: 100%

   The *Dashboard* page, as it opens.

System information
==================

.. figure:: /_static/webadmin/dashboard_fs00.png
   :alt: System Info — date and time in the time zone chosen on the System page, uptime, free heap and its all-time minimum, LittleFS usage, CPU clock and the reason of the last reboot. The strip is refreshed once a second from /dashinfo.
   :align: center
   :width: 90%

   **System Info** — date and time in the time zone chosen on the *System* page, uptime, free heap and its all-time minimum, LittleFS usage, CPU clock and the reason of the last reboot. The strip is refreshed once a second from ``/dashinfo``.

The clock itself always runs in UTC; the time zone only changes how the date
is shown here (see :ref:`en-administration`). A heap minimum that keeps
falling over days is the first sign of a leak; the *Reboot reason* tells a
power cut (*Power-on*) from a watchdog or a panic.

Modes and network
=================

.. figure:: /_static/webadmin/dashboard_fs01.png
   :alt: Modes Enabled — one pill per service - IGate, digipeater, tracker and weather - green while the service is switched on.
   :align: center
   :width: 90%

   **Modes Enabled** — one pill per service - IGate, digipeater, tracker and weather - green while the service is switched on.

.. figure:: /_static/webadmin/dashboard_fs02.png
   :alt: Network Status — Wi-Fi association, the APRS-IS connection (igate_is_connected()) and whether FX.25 is enabled on receive.
   :align: center
   :width: 90%

   **Network Status** — Wi-Fi association, the APRS-IS connection (``igate_is_connected()``) and whether FX.25 is enabled on receive.

Statistics
==========

.. figure:: /_static/webadmin/dashboard_fs03.png
   :alt: STATISTICS — the station counters: frames received and transmitted on RF, gated in each direction, digipeated, dropped or undecodable, the RF transmit queue, forced CSMA key-ups and the transmit duty cycle.
   :align: center
   :width: 90%

   **STATISTICS** — the station counters: frames received and transmitted on RF, gated in each direction, digipeated, dropped or undecodable, the RF transmit queue, forced CSMA key-ups and the transmit duty cycle.

What every counter measures, and why the IGate and the modem keep separate
ones, is described in :ref:`en-web-admin` (*The dashboard statistics*).

.. figure:: /_static/webadmin/dashboard_fs04.png
   :alt: Drop Breakdown — one line per drop reason. A counter that grows tells which filter or limit is refusing traffic, without opening the console.
   :align: center
   :width: 90%

   **Drop Breakdown** — one line per drop reason. A counter that grows tells which filter or limit is refusing traffic, without opening the console.

Radio, server and Wi-Fi
=======================

.. figure:: /_static/webadmin/dashboard_fs05.png
   :alt: Radio Info — the active modem profile and FX.25 state.
   :align: center
   :width: 90%

   **Radio Info** — the active modem profile and FX.25 state.

.. figure:: /_static/webadmin/dashboard_fs06.png
   :alt: APRS-IS SERVER — the APRS-IS server currently in use, which with failover can be any of the four configured on the IGate page.
   :align: center
   :width: 90%

   **APRS-IS SERVER** — the APRS-IS server currently in use, which with failover can be any of the four configured on the *IGate* page.

.. figure:: /_static/webadmin/dashboard_fs07.png
   :alt: WiFi — Wi-Fi mode, the network the station is associated with and its signal strength.
   :align: center
   :width: 90%

   **WiFi** — Wi-Fi mode, the network the station is associated with and its signal strength.

Live traffic
============

.. figure:: /_static/webadmin/dashboard_fs08.png
   :alt: IGate Traffic — the traffic table: time since boot, type (RX, TX, DIGI, RX-IS…), symbol, station, the raw packet, what was decoded from the payload and the receive audio level in mV RMS.
   :align: center
   :width: 90%

   **IGate Traffic** — the traffic table: time since boot, type (``RX``, ``TX``, ``DIGI``, ``RX-IS``…), symbol, station, the raw packet, what was decoded from the payload and the receive audio level in mV RMS.

The table is filled by sequence-based polling of
``/igate_traffic?since=<seq>``, so nothing is lost between two polls, and a
freshly loaded page starts with the entries still held in the station's ring. *Pause* freezes the view
without stopping the station; *Clear* empties the browser's copy only. The
*AUDIO* column is the level the modem measured at the ADC pin for that frame,
which makes it the quickest way to trim the receive level (see
:ref:`en-hardware`). With *Log after filters* on the *IGate* page enabled, the
table shows only what the local filters accept.

The symbol icons are loaded by the browser from ``aprs.dprns.com``; when that
site cannot be reached the column stays empty and everything else works.

Last heard
==========

Every station heard is also kept in a per-station *Last heard* table of up to
30 entries, most recent first, with the path it was heard on, whether it came
over RF or APRS-IS, the digipeater hops it used and an 18-hour activity
history. The dashboard does not draw it: it is served as JSON on
``/lastheard`` and consumed by the message gate (*heard locally*), the
``?APRSD`` / ``?APRSH`` query answers, the INET→RF *heard on RF* gate and the
BrandMeister status count.
