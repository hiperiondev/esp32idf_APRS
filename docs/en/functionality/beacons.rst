.. _en-beacons:

=========================
Beacons and the scheduler
=========================

Own-station beacons are what make the station itself appear on aprs.fi. The
IGate and digipeater alone only relay traffic they hear; they never announce
their own position. Three logical beacons exist — **tracker**, **igate** and
**digi** — each with its own enable flags, interval, coordinates, symbol,
comment and RF/INET routing, saved by its respective web-admin page
(``g_config.trk_*``, ``g_config.igate_*``, ``g_config.digi_*``).

The shared beacon scheduler
===========================

Earlier revisions ran the tracker, igate and digi beacons, the weather report
and the bulletins each in **its own FreeRTOS task**. Every one of those tasks
did the same thing — sleep, wake, build a packet, walk the shared (float-heavy)
TNC2/AX.25 TX chain, sleep again — and therefore each had to carry a large
stack (10–14 KB) sized for that call tree, even though they almost never run at
the same time and the half-duplex modem serialises their transmissions anyway.

The ``beacon_scheduler`` component **collapses those five tasks into one**. On
each pass it calls every subsystem's "service" function
(``beacon_service()``, ``weather_beacon_service()``, ``bulletins_service()``,
and the objects/items and telemetry services), each of which transmits whatever
is due and reports how many seconds until it next needs servicing; the scheduler
then sleeps until the soonest of them. The subsystems keep their independent
enable flags and intervals — only the task (and its stack) is shared.

Net effect: five stacks (~61 KB total) become one (~14 KB), freeing ~46 KB of
internal heap on this no-PSRAM build.

Query answers ride the same task
================================

An APRS query answer is a beacon in everything but its trigger: ``?APRS?`` and
``?APRSP`` run the position builder, ``?APRSS`` the status builder, ``?WX?`` the
weather one, and all of them end in the same float-heavy TNC2/AX.25 TX chain. So
they are answered here too. ``query_process()`` and ``query_process_directed()``,
which run on the tasks that receive traffic, only queue the request; the
scheduler calls ``query_service()`` at the start of every pass and does the
building and the transmitting on the stack sized for that call tree (see
:ref:`en-query`).

Because a query is not periodic, waiting for the next beacon to fall due would
show up as a late reply. Queuing a request therefore calls
``beacon_scheduler_wake()``, which cuts the scheduler's sleep short — the sleep
is an ``ulTaskNotifyTake()`` with the soonest due time as its timeout rather than
a plain delay. A wake raised while the task is mid-pass is latched by FreeRTOS
and taken by the next sleep, so nothing queued is slept through.

Service functions
=================

Each subsystem exposes a ``*_service()`` that:

#. Checks its own enable flags. A disabled beacon is a cheap no-op that returns
   a short re-check interval, so toggling it on in the web admin still takes
   effect without a reboot.
#. Transmits any beacon that is currently due, on RF
   (``aprs_service_send_tnc2()``) and/or to APRS-IS (``igate_send_raw()``) per
   the page's own ``loc2rf`` / ``loc2inet`` flags.
#. Returns the number of seconds (always ≥ 1) until the soonest next-due event.

``beacon_service()`` handles all three position beacons in one pass.

Anti-collision jitter
=====================

Beacon scheduling is otherwise deterministic, so multiple stations that all pick
the same round interval (e.g. WX every 600 s) tend to phase-lock and collide on
a shared RF channel — a classic APRS pathology. ``beacon_scheduler_jitter()``
spreads a beacon's due time by ± a few percent (``esp_random()``-seeded,
uniform), so own-station beacons de-correlate both from each other and from
neighbouring stations, and simultaneously-due beacons drift apart over time. The
jitter is applied to the interval used to compute a beacon's **next-due**
timestamp — not merely to the scheduler's sleep, which would leave the
underlying due-time grid deterministic and let it re-lock on the next cycle.

TX staggering within a pass
===========================

When several own-station beacons fall due together they are serviced
back-to-back in the scheduler task, far faster than a 1200 Bd frame clears the
air. With the factory-default *TX buffers = 1*, the 2nd and 3rd frames would hit
a full RF TX ring and be dropped. To prevent this, the scheduler task registers
itself via ``aprs_service_set_beacon_context()``, and **only in that task** is
``aprs_service_send_tnc2()`` permitted to wait briefly (up to 4 s) for the ring
to drain below the limit before giving up — so every due beacon eventually keys
up, while every other caller (RX/digipeat, INET→RF, message TX) keeps the
non-blocking drop-if-full behaviour and a busy RF leg never stalls RX decode or
the APRS-IS socket.

Data extensions (PHG / RNG / DFS)
=================================

The IGate position beacon can carry one of the three fixed-station APRS data
extensions in the 7-byte slot that follows the symbol code — the same slot a
moving station uses for course/speed, which is why exactly one of them is ever
emitted. *Enable data extension* on the IGate page gates the slot, and
*Extension type* picks which of the three fills it:

.. list-table::
   :header-rows: 1
   :widths: 12 18 70

   * - Type
     - On air
     - Meaning
   * - PHG
     - ``PHG5132``
     - Transmitter power, antenna height above average terrain, gain and
       directivity. Receiving software draws the resulting coverage estimate as
       a circle (or a lobe, for a directional antenna).
   * - RNG
     - ``RNG0025``
     - A single pre-calculated omnidirectional radio range in statute miles, for
       an operator who already knows their real coverage radius and would rather
       state it than have it inferred from PHG.
   * - DFS
     - ``DFS3364``
     - Omni-DF signal strength: the same height/gain/directivity codes as PHG,
       but reporting *received* signal strength in S-points instead of
       transmitted power. A strength of 0 means this station does **not** hear
       the signal, which plotting software renders as an exclusion circle rather
       than a coverage circle.

PHG uses all four sub-fields, DFS every one but transmit power, and RNG none of
them; the page disables whichever inputs the selected type does not use. Since a
disabled control does not POST, the stored values of the *other* type survive
switching back and forth.

Enabling any extension forces the uncompressed position layout. The compressed
format has no room for the 7-byte slot (APRS101 ch.9 states it does not support
PHG), so emitting those bytes inside a compressed report would simply be wrong
data, and dropping the extension to keep compression would silently lose a field
the operator explicitly enabled.

Position ambiguity
==================

*Position ambiguity* on the Station page is station-wide: it applies to all
three position beacons, because how precisely a station is willing to state
where it is, is a property of the station rather than of any one beacon. Levels
0–4 blank the least significant minute digits on air (APRS101 ch.6) — the
decimal point, the hemisphere character and the field widths never change, which
is what keeps the report parseable:

.. list-table::
   :header-rows: 1
   :widths: 10 25 25 40

   * - Level
     - Latitude
     - Longitude
     - Precision
   * - 0
     - ``4903.50N``
     - ``07201.75W``
     - Hundredths of a minute (full).
   * - 1
     - ``4903.5 N``
     - ``07201.7 W``
     - Nearest 1/10 minute.
   * - 2
     - ``4903.  N``
     - ``07201.  W``
     - Nearest minute.
   * - 3
     - ``490 .  N``
     - ``0720 .  W``
     - Nearest 10 minutes.
   * - 4
     - ``49  .  N``
     - ``072  .  W``
     - Nearest degree.

Digits are blanked, never rounded away, matching the reference decoders that
read a blanked digit as "unknown". The rounding carry still applies first, so a
coordinate that rounds up to the next degree is reported in that degree rather
than the one below it.

A non-zero level also forces the uncompressed layout, for the same class of
reason as a data extension: the compressed format has no decimal digits to
blank, so honouring a *compressed* tick alongside ambiguity would transmit the
exact position the operator asked to obscure. Mic-E, by contrast, carries
ambiguity natively and needs no such fallback.

Maidenhead locator in status reports
====================================

*Maidenhead locator in status reports*, also station-wide on the Station page,
prefixes every status report with the grid locator of that beacon's own
position, its symbol table byte and its symbol code — the ``>IO91SX/G`` form of
APRS101 ch.16 — followed by a space and the configured status text. Receivers
that understand the form plot the station from the locator alone; the rest show
the whole thing as status text. The configured text itself is never
interpreted.

Timestamps are UTC
==================

Beacon timestamps are zulu/UTC (``051200z``) per the APRS spec — which is why
``time_sync.c`` pins the system clock to ``TZ=UTC0`` regardless of
``g_config.timeZone`` (the configured zone is display-only).
