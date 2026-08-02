.. _en-web-admin:

=========
Web Admin
=========

The ``webconfig`` component (``components/webconfig/``) is an ``esp_http_server``
admin built from one file per page (``pages/*.c``), a route table
(``web_server.c``) and a set of shared helpers (``web_common.c``). It uses
**HTTP Basic auth** against ``g_config.http_username`` / ``http_password`` on
every page, wildcard URI matching, a 20 KB handler stack and LRU purge.

Why per-field helpers
=====================

HTML is emitted through small per-field helpers (``web_field_text``,
``web_field_int``, ``web_field_checkbox``, ``web_select_*``,
``web_field_symbol``, …) rather than one giant ``snprintf`` — deliberately, to
avoid ``-Werror=format-truncation`` and to keep each page readable.

The numeric helpers (``web_field_int``, ``web_field_float``) take the field's
accepted range and always emit it as the input's HTML ``min``/``max``, so every
numeric field on every page is validated by the browser before the form is
submitted. That is the first line of defence against a typo; the POST handler
still clamps what it stores, which is what holds against a crafted request.
Recurring domains (SSID, transmit interval, latitude, longitude, altitude) come
from the ``WEB_RANGE_*`` constants in ``web_common.h`` so a bound is defined
once for every page that shares it.

The pages
=========

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Page
     - What it does
   * - **Dashboard**
     - Network Status pills (Wi-Fi, APRS-IS via ``igate_is_connected()``), a
       STATISTICS panel, a LAST HEARD table with symbol icons, and a live
       traffic table (DX / PACKET / AUDIO) fed by sequence-based long polling.
   * - **Station**
     - The shared own-station identity read by every beacon, object and
       message: callsign, latitude, longitude, altitude (``g_config.my_*``).
   * - **IGate**
     - Enable, RF→INET / INET→RF, both filter bitmasks, budlist and range/prefix
       gates, callsign/SSID/passcode, host/port, server-side filter string,
       beacon on/off, position, interval, symbol picker, object, comment,
       status, PHG.
   * - **Digi**
     - Digipeater enable, callsign/SSID, auto (WIDEn-N) mode, beacon settings,
       delay, filter.
   * - **Tracker**
     - Tracker enable, callsign/SSID, fixed interval, position, symbol
       (moving/stopped), comment, compressed-position and altitude options.
   * - **Weather**
     - Enable, send-via-RF/-INET, timestamp, WX callsign/SSID/path, position,
       object name, comment, per-field *Averaged* checkboxes, and — for every
       on-air WX field — a **channel dropdown** populated live from the
       ``sensors_local`` registry and filtered by each driver's published
       capabilities. Live values via ``/wx/values``.
   * - **Telemetry**
     - Beacon/report parameters, definition-message toggles, analog A1–A5 with
       source pickers and calibration, digital B1–B8 with source pickers and
       sense. Live values via ``/tlm/values``.
   * - **Bulletins**
     - Up to five bulletins (text, RF/INET, interval, expiry).
   * - **Objects and Items**
     - Up to five objects/items (name, position, symbol, course/speed, comment,
       RF/INET, interval, permanent flag, kill).
   * - **Snd/Rcv Msg**
     - The APRS inbox/compose UI (``/msgchat``).
   * - **Message**
     - Configures the messaging engine (RF/INET enable, retry, encryption,
       alarm GPIO).
   * - **Query**
     - APRS query responder enable, RF/INET, broadcast query types
       (``?APRS?``, and where built, ``?WX?``/``?IGATE?``), directed-query
       enable, minimum reply interval (airtime/loop safety floor).
   * - **Radio / Modem**
     - FX.25 toggle; audio modem enable, modulation (300 / 1200 Bell202 / 1200
       V.23 / 9600 G3RUH), audio LPF (flat audio), preamble ms, TX time slot ms,
       TX buffers, extra PTT unkey hold; and the **LOOP TEST** button. Save
       re-applies the modem live — no reboot.
   * - **Wireless**
     - Mode (off/STA/AP/AP+STA), AP SSID/pass/channel, 5 STA slots each with its
       own Enable checkbox, TX power in dBm, plus a live scan.
   * - **System**
     - Web login, hostname, CPU frequency (applied live), NTP hosts ×3, resync
       interval, reset timeout, and the four shared path presets ``path[0..3]``.
   * - **Storage**
     - LittleFS browser: download, delete, multipart upload, usage, format.
   * - **About / Firmware**
     - Project name, version, build date/time, IDF version, running partition,
       and the **OTA Update** panel.

The dashboard statistics
========================

The statistics come from ``aprs_service_get_stats()``, tracked **independently**
of ``igate_en``/``digi_en``:

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Counter
     - Meaning
   * - ``radio_rx``
     - Every frame the modem decoded off RF.
   * - ``radio_tx``
     - Every frame successfully transmitted on RF.
   * - ``rf2inet``
     - Frames the IGate actually uplinked.
   * - ``inet2rf``
     - APRS-IS lines actually transmitted on RF.
   * - ``digi``
     - Frames digipeated (path rewritten + retransmitted).
   * - ``drop`` / ``err``
     - Frames discarded / that failed to decode, at the RX/service level.
   * - ``tx_queue_depth`` / ``tx_queue_limit``
     - The current RF TX ring backlog and the effective *TX buffers* cap, so the
       dashboard reads like the console's "n/n pending" line.

This is deliberate. With both features off (a common RX-only/monitor setup) the
dashboard would otherwise stay pinned at zero no matter how much traffic was
decoded.

Live feeds
==========

* ``/lastheard`` — the LAST HEARD table (JSON), fed from both RF and APRS-IS.
* ``/igate_traffic?since=<seq>`` — the traffic log delta (JSON). Each entry
  carries a direction tag (``RX``/``TX``/``DIGI``/``INET2RF``/``RX-IS``), the DX
  callsign, the raw packet, and the audio level in mV RMS (or −1).
* ``/dashinfo``, ``/sidebarInfo``, ``/heapinfo`` — compact live info fragments.

See :ref:`en-http-routes` for the full route table.
