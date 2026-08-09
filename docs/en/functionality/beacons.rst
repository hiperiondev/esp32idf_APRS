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

Messaging capability is in the data type identifier
===================================================

The first byte of a position report's information field states two things at
once (APRS101 ch.6): whether a timestamp follows, and whether the station can
accept APRS messages.

.. list-table::
   :header-rows: 1
   :widths: 25 25 25 25

   * - Timestamp
     - ``msg_enable`` off
     - ``msg_enable`` on
     - Meaning
   * - No
     - ``!``
     - ``=``
     - Position, no timestamp
   * - Yes
     - ``/``
     - ``@``
     - Position with timestamp

The distinction is not decorative: it is how a receiving client decides whether
to offer its operator a *send message* action for the station. Kenwood
TH-D7/D700/D710 radios, APRSISCE/32, Xastir, YAAC and aprs.fi all read this bit,
and a station that says it cannot accept messages is displayed with no reply
path at all.

This station runs a complete messaging engine, answers directed queries and
acknowledges the messages it receives, so with *Enable messaging* on all three
position beacons say so. The identifier is picked in ``buildPositionPacket()``
from the same locked snapshot every other beacon field comes from. Objects and
items are unaffected — they carry their own ``;`` and ``)`` identifiers. Mic-E
states the same thing in a different place: its position lives in the AX.25
destination address, so it has no data type identifier to spare, and the
message-capable flag is carried by the TYPE byte (`` ` `` message capable,
``'`` one-way tracker) that follows the symbol table byte. Both layouts read
the same *Enable messaging* tick, so they cannot disagree about what this
station claims.

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

The !DAO! precision extension
==============================

*DAO extension* on the Station page is a second, station-wide precision
setting that sits next to ambiguity rather than replacing it. When enabled,
``aprs_dao_build()`` appends the WGS-84 human-readable ``!DAO!``
precision/datum extension (``aprs12/datum.txt``) to the comment of every
uncompressed position report and to the Mic-E text field, where the spec
reserves the same trailing position for it. The five bytes recover, as one
extra decimal digit per axis, the third minute digit that the plain
``DDMM.mmN``/``DDDMM.mmW`` fields round away — one order of magnitude more
precision than the uncompressed layout otherwise carries, matching this
firmware's own float latitude/longitude resolution.

Because it restores precision, ``!DAO!`` is only ever applied when
*Position ambiguity* is 0 and the layout is not the compressed one — a
station deliberately obscuring its position, or already sending
full-resolution compressed coordinates, must not have that resolution handed
back through this extension. A receiver that does not recognise the
extension simply sees five extra bytes of comment text, so it is always safe
to enable.

Maidenhead locator in status reports
====================================

*Maidenhead locator in status reports*, also station-wide on the Station page,
prefixes every status report with the grid locator of that beacon's own
position, its symbol table byte and its symbol code — the ``>IO91SX/G`` form of
APRS101 ch.16 — followed by a space and the configured status text. Receivers
that understand the form plot the station from the locator alone; the rest show
the whole thing as status text. The configured text itself is never
interpreted.

Status report length budget
===========================

APRS101 ch.16 caps a status report's information field at 70 bytes: the ``>``
DTI, an optional 7-character DHM timestamp and at most 62 characters of status
text. Everything the report can carry beyond the operator's own words is spent
out of that same budget — the timestamp, the frequency block and the Maidenhead
locator — and a full 49-character status text plus both optional blocks asks for
more than fits.

When that happens the optional blocks are dropped, in this order, until the
field fits:

#. the Maidenhead locator, which only restates a position this station already
   beacons;
#. the frequency block, the one part of the report a receiving radio can act on.

The configured status text is never shortened: it is what the report exists to
carry. If it does not fit even on its own, the whole report is refused and the
reason logged, rather than a truncated — and therefore malformed — status line
going on the air.

Frequency block
===============

When a beacon has a monitor frequency configured, its position comment and its
status report both start with the fixed 10-byte frequency field of
``freqspec.txt``, followed by the tone (``Tnnn``/``Toff``) and, for a duplex
repeater, the shift in units of 10 kHz. Which of the three forms the spec
defines is used follows from the frequency alone:

.. list-table::
   :header-rows: 1
   :widths: 26 24 50

   * - Frequency
     - Emitted
     - Form
   * - Below 100 MHz
     - ``  50.62 MHz``
     - 10 kHz form ``FFF.FF MHz``, right-justified against its space
   * - 100.000-999.999 MHz
     - ``146.520MHz``
     - 1 kHz form ``FFF.FFFMHz``
   * - Above 999.999 MHz
     - ``A96.000MHz``
     - Microwave letter designation, one letter per 100 MHz block

The letter table covers only the bands ``freqspec.txt`` enumerates: A (1200),
B (2300), C (2400), D (3400), E (5600), F (5700), G (5800), H (10100),
I (10200), J (10300), K (10400), L (10500), M (24000), N (24100) and O (24200),
each spanning its base plus 99 MHz. A frequency above 999.999 MHz outside all of
them has no 10-byte form at all, so no block is emitted and the omission is
logged — an 11-byte field would shift every byte a receiver reads after it.

Timestamps are UTC
==================

Beacon timestamps are zulu/UTC (``051200z``) per the APRS spec — which is why
``time_sync.c`` pins the system clock to ``TZ=UTC0``. No local-time offset
exists anywhere in the firmware.
