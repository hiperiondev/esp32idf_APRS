.. _en-beacons:

=========================
Beacons and the scheduler
=========================

Own-station beacons are what make the station itself appear on aprs.fi. The
IGate and digipeater alone only relay traffic they hear; they never announce
their own position. Three logical beacons exist — **tracker**, **igate** and
**digi** — each with its own enable flags, interval, coordinates, symbol,
comment and RF/INET routing, saved by its respective web-admin page
(``g_config.trk_*``, ``g_config.igate_*``, ``g_config.digi_*``). Each
beacon's comment and status text has ``|`` and ``~`` filtered out at
transmission time, before it reaches the on-air packet — both characters are
reserved for the base-91 comment telemetry group (:ref:`en-telemetry`); the
saved text itself is left exactly as the operator entered it.

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

A beacon enabled on both legs is built **twice**, once per leg, and the two
lines differ in exactly one place: the path. The radio transmission carries the
digipeater selection made on that beacon's own page; the APRS-IS transmission
carries ``TCPIP*`` and nothing else, which is what `aprs-is.net's connection
guidance <https://www.aprs-is.net/Connecting.aspx>`_ requires of a packet
originating from the client — a ``WIDEn-N`` alias sent there would describe
repeaters the packet never passed through. Both lines come from the same
builder and the same locked configuration snapshot, so nothing else about them
can drift apart, and each leg is logged from what it actually did rather than
from a single unconditional line.

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

Data extensions (PHG / RNG / DFS / DF)
======================================

The IGate and digipeater position beacons can each carry one of the
fixed-station APRS data extensions in the 7-byte slot that follows the symbol
code — the same slot a moving station uses for course/speed, which is why
exactly one of them is ever emitted. *Enable data extension* on the IGate or
Digi page gates that role's slot, and *Extension type* picks which one fills
it. The two roles hold their own settings, so an IGate and a digipeater running
on one station under different SSIDs can publish different coverage:

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
   * - DF
     - ``000/000/270/735``
     - The DF report of APRS101 ch.8: the bearing to a signal, followed by the
       NRQ triplet that qualifies it — hits per sampling period (``N``, where 0
       says the triplet carries no meaning), the range code (``R``, standing for
       2\ :sup:`R` miles) and the bearing accuracy (``Q``, 9 being better than
       one degree). It is the form the chapter describes for a
       direction-finding station reporting its own fix. These beacons are fixed
       stations with no course/speed source, so the leading pair is the
       ``000/000`` the specification uses to say exactly that. The same encoder
       builds the token for objects and items, where it reports a fix taken on
       someone else.

The DF report is the one extension with a symbol requirement of its own. Its
token is fifteen bytes where the slot is seven, and chapter 8 states that the
bearing and NRQ are only meaningful when the report carries the DF symbol —
symbol table ``/`` and symbol code ``\``. A receiver that sees any other symbol
has no reason to look past the slot: it reads ``000/000`` as an ordinary
course/speed pair and takes ``/270/735`` as the first eight characters of the
comment field. So DF is transmitted only with that symbol pair; with any other
one the slot is left empty, the log names the symbol that suppressed the
report, and the page shows a note beside the extension type as soon as
the two disagree. The same rule applies to objects and items, and on receive:
an incoming DF continuation is always stepped over so it never lands in the
comment, but its bearing is only read when the sender's symbol is the DF
symbol.

PHG uses all four sub-fields, DFS every one but transmit power, and RNG and DF
none of them — DF has its own bearing and NRQ inputs instead; the page disables
whichever inputs the selected type does not use. Since a disabled control does
not POST, the stored values of the *other* types survive switching back and
forth.

Enabling PHG, DFS or a DF report that the symbol allows forces the uncompressed
position layout. The compressed
format has no room for the 7-byte slot (APRS101 ch.9 states it does not support
PHG), and a DF report is wider than the slot still, so emitting those bytes
inside a compressed report would simply be wrong data, and dropping the
extension to keep compression would lose a field the operator explicitly
enabled. The firmware logs a warning naming which of the two settings gave way,
rather than leaving it to be discovered off the air. A DF report the symbol
suppresses puts no bytes in the slot, so it does not cost the beacon its
compression.

Position ambiguity is a separate matter and travels with any of them: it blanks
decimal digits of the uncompressed layout, which keeps its extension slot, so
neither setting has to give way to the other.

RNG is the exception, because the compressed format carries a pre-calculated
radio range natively: the two ``cs`` bytes hold ``{`` followed by a range digit,
decoded as ``2 × 1.08^s`` miles. A beacon that has RNG selected and compression
ticked therefore stays compressed, with the range folded into those two bytes
and no ``RNGrrrr`` token in the information field. The compressed form quantises
the range to a step of about 8 per cent and starts at a floor of 2 miles, so a
range set below that is transmitted as 2.

PHG is the extension a digipeater is expected to publish. Chapter 7 introduces
it as the way a station states the coverage circle its neighbours reason about
when they choose a path, and mapping clients draw that circle for digipeaters
first — which is why the Digi page offers the same four types the IGate page
does, on its own settings, rather than leaving the role's slot empty.

The Tracker beacon carries PHG and nothing else, switched on with *Include PHG
data extension* on its own page. There are no sub-fields to fill in there: the
four values are the station's own antenna data, edited once in the PHG block on
the Station page. A tracker beaconing in Mic-E keeps the token — Mic-E has no
7-byte slot after a symbol code, but APRS 1.2 states that its text field may
carry an ordinary position comment field, PHG included, and that is where the
token goes: after the frequency block, so a radio still auto-tunes from the
leading bytes, and before the operator's comment.

Compressed altitude
===================

A compressed position report has no comment token for altitude, but it does not
need one. The same two ``cs`` bytes that carry course/speed or a radio range
carry an altitude when the type byte names GGA as the NMEA source, decoded as
``1.002^(c × 91 + s)`` feet. A beacon with *Include altitude* and *Compress
position* both ticked uses that form, and the ``/A=`` token is left out of the
comment so the altitude is stated once — nine bytes of comment saved for
nothing, at a step of about 0.2 per cent.

The two bytes hold one thing at a time, so a beacon that also has RNG selected
gives them to the range: the range has no other place to go, while altitude
still has ``/A=`` to fall back on, and that is what such a beacon emits.

Numeric overlays in a compressed report
=======================================

A symbol overlay is written in the table position of the symbol pair, and APRS
1.2 ch.21 allows it to be a letter ``A``-``Z`` or a digit ``0``-``9`` — the
numeric ones are how a digipeater advertises its routing policy, the "numbered
circle" of the alternate table. The compressed layout cannot carry the digit
itself: the first byte of a position field is exactly what tells a receiver
which of the two layouts it is reading, and a leading digit means uncompressed.

A numeric overlay therefore travels as the matching lower-case letter, ``a`` for
``0`` through ``j`` for ``9``, and a receiver maps it back to the digit. The
firmware applies that mapping when it builds the field, so the overlay is
configured once, as the digit, and the *Compress position* tick changes nothing
about how it is entered or how it plots.

Both bytes of the pair are also bounded on the way in — on the form and again
when the configuration file is read — because neither is cosmetic: a table
identifier outside ``/``, ``\``, ``A``-``Z`` and ``0``-``9`` falls back to the
primary table, and a code outside the printable range falls back to ``&``.

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

Mic-E position comment
======================

A Mic-E report carries, in the A/B/C bits of its destination address, one of
fifteen *position comments*: the seven standard values M0 *Off Duty*, M1 *En
Route*, M2 *In Service*, M3 *Returning*, M4 *Committed*, M5 *Special* and M6
*Priority*; seven locally defined custom values C0–C6; and *Emergency*, which
is the pattern with all three bits clear. Every Kenwood and Yaesu APRS radio
puts this on a front-panel menu, and receiving clients display it next to the
station.

*Mic-E position comment*, on the Tracker page, selects among the fourteen
standard and custom values. It applies only when *Mic-E position encoding* is
on, since no other layout has a field for it, and the factory default is M0
*Off Duty* — the conventional value for a station that does not move, which is
what this firmware beacons.

Emergency is deliberately absent from that list. Transmitting it asks other
operators, and in some regions dispatchers, to respond to a real emergency;
that is not something a settings page should be able to arm with one mis-click
and then leave armed for every beacon afterwards. On receive it is handled in
full: see :ref:`en-filtering` for the decoder, and the traffic log
for the warning line a received emergency produces.

Mic-E status text
==================

The free-text tail of the Mic-E information field — everything after the
frequency block, the PHG/data-extension token and the altitude field —
carries whatever the operator entered as the beacon's comment, byte for byte.
The one exception is the very first byte of that tail: APRS12c ch.10 reserves
a leading ``,`` or ``0x1d`` for the (now obsolete) Mic-E Telemetry Data
sub-format, so a comment that happens to start with either byte would be
misread as telemetry rather than as text. ``aprs_mice_encode()`` guards
against this by inserting a single space ahead of either character before the
comment is appended; a comment starting with any other byte reaches the air
unchanged. The inserted space carries no information of its own and a
receiving client displays it as an ordinary leading space in the comment.

Beam heading and ERP in status reports
======================================

A status report may end with two characters after a ``^``: the beam heading in
units of ten degrees, and a code standing for the effective radiated power.
Meteor-scatter operating is what the pair exists for — the two figures a
correspondent needs in order to know whether a burst is worth waiting for — and
APRS101 ch.16 fixes it as the *last* field of the status text, which is the only
place it can be recognised.

Both halves are set on the Station page and apply station-wide, like the
Maidenhead option: *Beam heading in status reports* steps in ten degrees from 0
to 350, and *ERP in status reports* offers the specification's own table, 10 W
through 7290 W in the steps that follow the square of the code digit. A heading
with no power, or a power with no heading, says nothing, so the block is emitted
only when both are set — leaving either on *Off* is what a station that does not
work meteor scatter does, and it then transmits exactly the status report it
transmitted before.

The status information field is capped at 63 bytes, and the assembly drops its
optional blocks in order until it fits: the leading field first, then the
frequency block. The beam/ERP pair is never dropped. It is three bytes, and a
station transmitting a status report during a meteor-scatter schedule is
transmitting it for those three bytes.

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
read a blanked digit as "unknown". The degree carry described above still
applies first, so a coordinate whose minutes compute to a full 60.00 because
of floating-point error at the top of a degree is reported in the next degree
rather than the one below it.

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
``DDMM.mmN``/``DDDMM.mmW`` fields truncate away — one order of magnitude more
precision than the uncompressed layout otherwise carries, matching this
firmware's own float latitude/longitude resolution. Both the base field and
this extra digit are derived from the same truncation of the minutes value,
so appending it always recovers a position at least as close to the true one
as the base field alone.

Because it restores precision, ``!DAO!`` is only ever applied when
*Position ambiguity* is 0 and the layout is not the compressed one — a
station deliberately obscuring its position, or already sending
full-resolution compressed coordinates, must not have that resolution handed
back through this extension. A receiver that does not recognise the
extension simply sees five extra bytes of comment text, so it is always safe
to enable.

The !x! no-archive marker
==========================

*Request APRS-IS not to archive my packets* on the Station page is a
station-wide, off-by-default privacy setting. When enabled, every own-station
free-text field is prefixed with the APRS-IS no-archive marker ``!x!``,
followed by a space and then the operator's own text, if any. The marker is
addressed to the databases behind APRS-IS rather than to any gateway: it asks
them not to store the packet, but does not withhold it from RF or from
APRS-IS itself, and a receiver that does not recognise it simply sees three
extra bytes of comment text.

The fields it reaches are:

* Tracker, IGate and Digipeater position comments,
* Tracker, IGate and Digipeater status texts,
* the weather report comment, in all four of its forms (object, positioned
  with and without a timestamp, and positionless),
* object and item comments,
* bulletin and announcement text.

Three kinds of packet are deliberately left out. Message text is not
descriptive text about this station: it is a word the correspondent reads in
the body of a message addressed to them. Telemetry ``PARM``/``UNIT``/``EQNS``/
``BITS`` definition packets are fixed-layout metadata with no free-text slot,
and a budget the definition itself needs. Query responses answer another
station's question rather than reporting this station's own position or
condition.

All of these fields are assembled by one shared builder,
``aprs_free_text_build()`` in ``main/include/aprs_free_text.h``, which applies
the marker and strips the characters APRS reserves for the base-91 comment
telemetry group (``|`` and ``~``) in the same place. A field the operator has
already typed the marker into is left alone, so the marker is never sent
twice.

This setting only affects packets this station originates. A packet this
station relays, whether IGate-to-RF, RF-to-IGate or digipeated, is passed
through unchanged — the marker, if the originating station already put one
there, travels with it either way, since relaying never rewrites payload
bytes.

Maidenhead locator in status reports
====================================

*Maidenhead locator in status reports*, also station-wide on the Station page,
prefixes every status report with the grid locator of that beacon's own
position, its symbol table byte and its symbol code — the ``>IO91SX/G`` form of
APRS101 ch.16 — followed by a space and the configured status text. Receivers
that understand the form plot the station from the locator alone; the rest show
the whole thing as status text. The configured text itself is never
interpreted. The locator is always the fixed 6-character field in upper case.

APRS101 ch.16 allows only one leading field in a status report's information
field: either the DHM timestamp or the Maidenhead locator, never both — a
receiver reads whatever immediately follows the ``>`` DTI as the locator, so a
timestamp in that position would be misread as one. When both *Status
timestamp* and *Maidenhead locator in status reports* are enabled on the same
beacon, the locator takes precedence and the timestamp is left out of that
beacon's status reports, since the locator carries the station's position,
which the timestamp does not.

Status report length budget
===========================

APRS101 ch.16 caps a status report's information field at 63 bytes: the ``>``
DTI, followed either by an optional 7-character DHM timestamp and at most 55
characters of status text, or by no timestamp and at most 62 characters of
status text. Everything the report can carry beyond the operator's own words
is spent out of that same budget — the leading field (the timestamp, or the
Maidenhead locator when it takes precedence) and the frequency block — and a
full 49-character status text plus both optional blocks asks for more than
fits.

When that happens the optional blocks are dropped, in this order, until the
field fits:

#. the leading field (the Maidenhead locator, or the timestamp when no locator
   is in use), which only restates information this station already beacons
   elsewhere — its position or the current time;
#. the frequency block, the one part of the report a receiving radio can act on.

The configured status text is never shortened: it is what the report exists to
carry. If it does not fit even on its own, the whole report is refused and the
reason logged, rather than a truncated — and therefore malformed — status line
going on the air.

The separating space before the status text belongs to the block that precedes
it, so it is dropped along with that block: a report left with neither a leading
field nor a frequency block — because none was configured, or because the budget
above took both away — reads ``>My status text``, with the operator's words
immediately after the ``>`` DTI, which is the form APRS101 ch.16 defines. The
space only ever appears between two things that are both present.

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
