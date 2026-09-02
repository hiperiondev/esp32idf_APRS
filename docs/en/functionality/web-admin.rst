.. _en-web-admin:

=========
Web Admin
=========

The ``webconfig`` component (``components/webconfig/``) is an ``esp_http_server``
admin built from one file per page (``pages/*.c``), a route table
(``web_server.c``) and a set of shared helpers (``web_common.c``). It uses
**HTTP Basic auth** against ``g_config.http_username`` / ``http_password`` on
every page — the single exception being the static ``/style.css``, which carries
no configuration or traffic data — plus wildcard URI matching, a 20 KB handler
stack and LRU purge.

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
       traffic table (DX / PACKET / DECODED / AUDIO) fed by sequence-based long
       polling. DECODED holds what was read out of the payload itself - the
       packet's own timestamp, course, speed, altitude, radio range, PHG or
       DFS, and the bearing and NRQ of a DF report - and is empty for a payload
       that carries none of them. The column holds a whole summary line
       whatever the payload: the ring sizes its field from the same constant
       the formatter writes into, so a report that fills every field is shown
       entire rather than cut short.
   * - **Station**
     - The shared own-station identity read by every beacon, object and
       message: callsign, latitude, longitude, altitude (``g_config.my_*``),
       plus the station-wide on-air options - position ambiguity, the
       Maidenhead locator prefix for status reports, and the meteor-scatter
       beam heading and ERP that close them. Position can be typed in or
       taken live from the GNSS receiver via *Use GPS*, which disables the
       three fields and fills them from ``GET /gps/live`` once a second while
       checked, rounding latitude and longitude to 4 decimal places and
       altitude to 1 decimal place so the filled values always pass the
       fields' own validation.
   * - **IGate**
     - Enable, RF→INET / INET→RF, both filter bitmasks, budlist and range/prefix
       gates, callsign/SSID/passcode, four *APRS-IS Server* fieldsets (each an
       Enable checkbox plus host and port, used as a failover rotation),
       server-side filter string, the *Log after filters* switch that narrows the
       traffic table and the serial console to what the local filters accept,
       nine payload-type checkboxes per direction (the ninth, *Other*, covers
       station capabilities, user-defined formats, Agrelo direction finding,
       Maidenhead locator beacons and the reserved map feature),
       beacon on/off, position, interval, symbol picker, object, comment,
       status, PHG. *Message Gating* holds the INET→RF message criteria switch,
       the heard-locally window and the addressee hop limit. Position can be
       typed in, mirrored from *Use My Station Data* or taken live from the
       GNSS receiver via *Use GPS*; the three are mutually exclusive.
   * - **BrandMeister**
     - Interconnect enable switch, the worldwide monitor subscription, the
       Internet-only message routing switch for BrandMeister addressees, and
       four optional gateway callsigns. The monitor switch is refused while
       INET→RF gating is on and the INET→RF range filter on the *IGate* page is
       off, since APRS-IS filter terms are OR'd and nothing else would stand
       between a worldwide feed and the transmitter. A read-only status table
       reports the interconnect state, whether ``u/APBM*`` is present in the
       server filter, the range filter's setting and how many BrandMeister
       stations are currently in LAST HEARD. No DMR connection of any kind is
       involved; see :ref:`en-brandmeister`.
   * - **Digi**
     - Digipeater enable, callsign/SSID and beacon settings (position, symbol,
       interval, comment, status, path). *Data Extension* selects what the
       position beacon carries in the slot after the symbol code — PHG, RNG,
       DFS or a DF report — with the same sub-fields and the same *Use My
       Station Data* mirror the *IGate* page offers. *n-N Path Aliases* holds the four
       {alias, max N, mode} rows the digipeater repeats by, the fill-in-only
       switch, the choice of what to do with a trapped hop count and the
       *Digipeat by destination SSID (legacy)* switch, off by default. It also
       carries the four shared path presets ``path[0..3]`` that every
       transmitting service selects from. The duplicate-suppression window is a
       single control, on the *IGate* page. Position can also be taken live
       from the GNSS receiver via *Use GPS*, mutually exclusive with *Use My
       Station Data*.
   * - **Tracker**
     - Tracker enable, callsign/SSID, fixed interval, position, station symbol,
       comment, compressed-position, Mic-E-position (with its position-comment
       selector), PHG and altitude options. The fixed position can be typed in,
       mirrored from *Use My Station Data* or taken live from the GNSS
       receiver via *Use GPS*; the three are mutually exclusive. *Use live GPS
       fix* is separate from all three: it leaves the fixed position as the
       fallback and has each transmission read the receiver instead. The
       *SmartBeaconing* fieldset (slow/fast interval, low/high speed, turn
       angle, turn slope, minimum turn time) makes that interval
       speed-adaptive; it needs the live fix to have anything to work from.
       A status-beacon fieldset (interval and text) and a frequency/repeater
       fieldset (frequency, duplex, offset, tone) close the page.
   * - **Weather**
     - Enable, send-via-RF/-INET, timestamp, WX callsign/SSID/path, position,
       object name, comment, per-field *Averaged* checkboxes, and — for every
       on-air WX field — a **channel dropdown** populated live from the
       ``sensors_local`` registry and filtered by each driver's published
       capabilities. Live values via ``/wx/values``. Position can be typed in,
       mirrored from *Use My Station Data* or taken live from the GNSS
       receiver via *Use GPS*; the three are mutually exclusive.
   * - **Telemetry**
     - Beacon/report parameters, definition-message toggles, analog A1–A5 with
       source pickers and calibration, digital B1–B8 with source pickers and
       sense. Live values via ``/tlm/values``.
   * - **GPS**
     - *Enable GPS Receiver* is the single switch the rest of the firmware
       tests before using anything the module reports; with it off the UART is
       not installed at all and the reader task does not run. Moving it takes
       effect immediately, without a reboot. Below it, a read-only live view of
       the receiver, led by a colour-coded *Module Status* badge that turns a
       page of numbers into a single diagnosis: red *Disabled* when the switch
       is off or the UART failed to come up, red *No data (check wiring)* when
       the module is enabled but nothing has arrived on the receive pin within
       the link timeout, amber *Searching (no fix)* once sentences are
       arriving but no valid navigation solution has been reported yet, and
       green *Fix OK* once one has. Below the badge, link state, navigation
       status, fix quality and 2D/3D mode, position, altitude and geoid
       separation, ground speed, course and magnetic variation, UTC date and
       time, satellites used and in view, HDOP/PDOP/VDOP, the accepted and
       discarded sentence counters and the age of the last sentence and fix.
       The serial port and its pins are compile-time board wiring and are
       shown as text. Live values via ``/gps/values``, polled every second.
       The plain-numeric counterpart, ``/gps/live``, is what every other
       page's *Use GPS* checkbox polls to auto-fill its own position/motion
       fields (Station, IGate, Digi, Tracker, Weather); each page's script
       rounds the polled latitude, longitude and altitude to the precision
       its own fields accept (4 decimal places for position, 1 decimal place
       for altitude) before writing them in.
   * - **Telegram**
     - *Enable Telegram Bot* switches the whole subsystem; with it off nothing
       connects to Telegram and no polling task runs, and moving it takes
       effect immediately without a reboot. Below it the bot token (rendered as
       a password field, with the same show/hide control the IGate passcode
       uses) and the administrator's numeric identifier, which is carried as a
       64-bit value and posted as text because today's Telegram user
       identifiers no longer fit in 32 bits. Below that, the Mini App address
       and the fixed-size authorized-user and allowed-group-chat tables (up to
       8 users and 4 group chats, each an identifier plus a display name).
       Everything on this page is stored in ``/storage/telegram.json``, not in
       ``config.json``, so it can also be downloaded and uploaded from the File
       Storage page. The status
       table below the form reports where the connection stands and, when it
       stands still, exactly which step failed and what to do about it - the
       settings file is missing or unparseable, the token is empty or not of
       the form ``<numbers>:<secret>``, the root certificate is not on the
       storage partition, there is no route to the Internet yet, the heap could
       not supply a TLS session, or Telegram itself answered and refused, in
       which case its own error code and wording are shown untranslated. Live
       values via ``/telegram/status``, polled every two seconds. The bot the
       page brings up answers ``/status`` with every service's on/off switch -
       IGate, digipeater, tracker, weather, telemetry, messaging, query
       responder, BrandMeister, GNSS receiver, AFSK modem, TX duty-cycle
       limiter and SNTP time sync - and ``/sensors`` with every weather field
       and telemetry channel enabled on the Weather and Telemetry pages, each
       with the sensor driver it is mapped to and its current reading. Both
       answers are built from the configuration as it stands when the command
       arrives, so a save takes effect on the next command without a restart.
   * - **Winlink**
     - The station's two Winlink roles, on one page. *Winlink Account* holds
       what a session of its own needs: the APRSLink service callsign, the
       identity the mailbox is keyed on (the base callsign, without its SSID),
       the password a login challenge is answered from, and the switches that
       decide whether a session opens by itself, how long it may live, whether
       its traffic is kept off the air and whether the beacon comment announces
       this station as a Winlink reader. The password is rendered as a password
       field with the same show/hide control the IGate passcode uses, and is
       never transmitted: a challenge names three character positions and only
       those characters are sent back. *Gateway for Local Stations* holds the
       single setting of the other role, relaying a neighbour's own session,
       together with a read-only view of the three IGate settings that decide
       the same question, so all four inputs to the message gate can be seen at
       once. Below the form, the session terminal: where the session stands and
       how long it has left, buttons for logging in and out and for listing
       mail, a free-form command field that takes the whole APRSLink command
       set, a three-step helper for writing a message, and the replies the
       service has sent back. Live values via ``/winlink/status`` and
       ``/winlink/list``, polled every three seconds; actions are posted to
       ``/winlink/cmd``, which is POST because it keys the transmitter.
   * - **Logs**
     - A viewer for the serial console, so what the station prints can be read
       without a cable attached. There is nothing to configure: one button,
       which reads *Start* while nothing is being captured and *Stop* while
       something is, and one window below it holding the last 50 lines. A
       console line longer than 255 characters is continued on the next row
       rather than cut, and the window scrolls in both directions - vertically
       because it holds more rows than fit on screen, horizontally because
       each line is kept whole. Pressing *Start* installs a mirror on the log
       writer; the serial output itself is unchanged either way, and the ring
       the mirror fills is allocated only while a capture is running.
       Capturing never outlives the page: arriving at it (or reloading it)
       stops anything left running, so the button always comes up in its
       *Start* state; leaving it stops the capture from the browser; and a tab
       that is closed, put to sleep or cut off mid-session says nothing at
       all, which is why the mirror also stops itself once nothing has read it
       for ten seconds. Nothing is written to flash and nothing is recorded -
       only what arrives while the window is open is shown. Live lines via
       ``/logs/read``, polled every second.
   * - **Bulletins**
     - Up to five bulletins (addressee identifier and group, text, RF/INET,
       initial interval, decay ramp, expiry).
   * - **Objects and Items**
     - Up to five objects/items (name, position, symbol, course/speed, comment,
       RF/INET, interval, permanent flag, kill).
   * - **Snd/Rcv Msg**
     - The APRS inbox/compose UI (``/msgchat``): one thread of sent and
       received messages, five visible at a time and ten kept.
   * - **Message**
     - Configures the messaging engine (RF/INET enable, retry, digipeat path,
       alarm GPIO).
   * - **Query**
     - APRS query responder enable, which source is answered (RF / APRS-IS —
       an answer always goes back on the channel the question arrived on),
       general query types (``?APRS?``,
       and where built, ``?WX?``/``?IGATE?``), directed-query enable, extended
       directed query set, minimum reply interval (airtime/loop safety floor),
       and the periodic station-capabilities beacon: enable, interval, RF and
       APRS-IS channel selection, and any extra capability tokens to append.
   * - **Radio / Modem**
     - FX.25 mode (off / RX only / RX+TX); audio modem enable, modulation (300 /
       1200 Bell202 / 1200 V.23 / 9600 G3RUH), audio LPF (flat audio), preamble
       ms, TX time slot ms, TX buffers, extra PTT unkey hold, CSMA persistence,
       and the long-term duty-cycle limiter (enable plus ceiling percentage);
       and the **LOOP TEST** button. Save re-applies the modem live — no reboot.
   * - **Wireless**
     - Mode (off/STA/AP/AP+STA), AP SSID/pass/channel, 5 STA slots each with its
       own Enable checkbox, TX power in dBm, plus a live scan.
   * - **System**
     - Web login, CPU frequency (applied live) and a *Time* section: NTP
       enable, NTP hosts ×3, resync interval, and a timezone selector that sets
       the local date/time shown on the dashboard (the clock itself stays UTC).
       Also the factory-reset button.
   * - **Storage**
     - LittleFS browser: download, delete, multipart upload, usage, format.
   * - **About / Firmware**
     - Project name, version, build date/time, IDF version, running partition,
       and the **OTA Update** panel.

.. note::

   Every control on these pages drives runtime behaviour: a setting that reaches
   ``config.json`` is read by the service that owns it. The digipeater always
   handles WIDEn-N and repeats without an added delay, so neither is offered as
   an option.

   Duplicate suppression has exactly one pair of controls, *Dup cache size*
   (``dupCacheSize``) and *Dup cache timeout* (``dupCacheTimeoutMs``) on the
   *IGate* page, and they govern the digipeater as well as the IGate: both
   services share the one cache in ``components/igate``, each with its own
   scope.

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
   * - ``csma_busy_forced`` / ``csma_persist_forced``
     - How often the eight-slot anti-starvation floor forced a transmission,
       split by whether any slot saw the channel busy or every slot found it
       clear. Shown as *CSMA FORCED (BUSY/PERSIST)*. These are transmissions,
       not drops.
   * - ``tx_duty_cycle_pct`` / ``duty_cycle_limit_pct``
     - Measured transmit duty cycle over the rolling 10-minute window against
       the configured ceiling, as *TX DUTY CYCLE*. The limit reads ``0`` when
       the limiter is off, while the measured figure is populated either way.

This is deliberate. With both features off (a common RX-only/monitor setup) the
dashboard would otherwise stay pinned at zero no matter how much traffic was
decoded.

Live feeds
==========

* ``/lastheard`` — the LAST HEARD table (JSON), fed from both RF and APRS-IS. A
  station last heard before NTP synced carries an empty ``time`` field: the
  clock was still counting from the epoch when the frame arrived, so there is no
  time of day to state and none is invented.
* ``/igate_traffic?since=<seq>`` — the traffic log delta (JSON). Each entry
  carries a direction tag (``RX``/``TX``/``DIGI``/``INET2RF``/``RX-IS``), the DX
  callsign, the raw packet, the decoded-fields summary (``dec``, empty when the
  payload carries none), and the audio level in mV RMS (or −1). With *Log after
  filters* set on the IGate page, the ``RX`` and ``RX-IS`` entries cover only the
  traffic this station's own filters accept — see :ref:`en-igate`. The body is
  streamed one entry per HTTP chunk, so a client that is far behind still gets
  every buffered line: the response has no size cap and the firmware never
  assembles the whole document in RAM. The ``seq`` it reports back is the
  sequence number of the last entry actually delivered, so the cursor can only
  advance past lines the client has received; a cursor ahead of the ring — the
  device rebooted, and numbering restarted at 1 — resends from the oldest entry
  still buffered.
* ``/dashinfo``, ``/sidebarInfo``, ``/heapinfo`` — compact live info fragments.

See :ref:`en-http-routes` for the full route table.

Login lockout policy
=====================

``web_check_auth()`` tracks failed Basic Auth attempts per source IPv4 address
in a small fixed-size table (``components/webconfig/web_common.c``). Only a
request that actually presented credentials and was rejected — a malformed
Basic payload, or a wrong username/password — counts as a failure. A request
with no ``Authorization`` header at all, or one that isn't ``Basic``, is the
challenge half of the HTTP Basic handshake that every browser performs on its
own, and is answered with a ``401`` without being charged against the budget;
this is what lets the dashboard's authenticated pollers (``/dashinfo``,
``/sidebarInfo``, ``/heapinfo``, ``/lastheard``, ``/igate_traffic``) sit behind
a fresh login page without ever tripping a lockout on their own.

After 5 consecutive rejected credentials from the same source, that source is
locked out and every further request gets ``429 Too Many Requests`` with a
``Retry-After`` header instead of a ``401``, for a window that starts at 5 s
and doubles on each further rejected attempt while still locked out, capped at
300 s. A window that elapses without a successful login rearms one failure
below the threshold rather than resuming from the accumulated count, so a
client that keeps retrying the same stale credentials after every window
expires re-triggers only the base 5 s lockout each time, instead of ratcheting
straight back to the 300 s cap. A successful login clears the source's slot
entirely.

Same-origin protection (CSRF)
==============================

``web_check_auth()`` also enforces a same-origin check on every ``HTTP_POST``
request, independently of whether ``g_config.http_username`` is set. The
check confirms the request's ``Origin`` header (falling back to ``Referer``)
names this device's own ``Host`` before anything else runs, and fails closed:
a request with neither header, or with a mismatching one, is rejected with
``403 Forbidden`` regardless of any credentials it carries.

This is deliberately independent of Basic Auth. Clearing the username on the
System page is a supported way to run the admin UI without a password, but
it only removes the login prompt — it does not
relax the same-origin requirement, because a browser-borne cross-site
request is a threat with or without a configured password: with no password
set there is no credential to steal, but the attacker's page can still make
the operator's own browser submit a state-changing request on their behalf.
Every state-changing route (``/ota_update``, ``/format``, ``/upload``,
``/delete``, ``/msgchat``, and every settings page's save handler) is
registered ``HTTP_POST`` for exactly this reason; no registered ``GET`` route
has a side effect, so this check never has to run against ordinary
navigation, bookmarks, or a typed-in URL.

.. seealso::

   :ref:`en-telegram` — the Telegram bot subsystem behind the *Telegram* page:
   its own configuration file, its supervised bring-up, and its built-in
   command set.
