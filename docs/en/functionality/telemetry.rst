.. _en-telemetry:

=========
Telemetry
=========

The ``telemetry`` subsystem (``main/telemetry.c``) gathers analog and digital
channels from the ``sensors_local`` registry and beacons a standard APRS
Telemetry Data Report (``T#nnn``) on RF and/or APRS-IS, together with the
PARM/UNIT/EQNS/BITS metadata messages that label those channels for receiving
stations. It mirrors the pattern the weather subsystem uses, but for telemetry.

Separate storage
================

Unlike most settings, telemetry configuration deliberately does **not** live in
``g_config``/``config.json``. It persists to its own small LittleFS file,
``/storage/telemetry.json``, the same way bulletins and objects/items keep
their own files. On first boot, or whenever the file is missing, an empty
default set is created so ``/storage/telemetry.json`` always exists once the
subsystem has started. The full schema is ``telemetry_config_t``
(``main/include/telemetry.h``).

Channels
========

Per APRS101 chapter 13, a telemetry report carries:

* **5 analog channels** ``A1``–``A5`` (``TLM_CH = 5``).
* **8 digital bits** ``B1``–``B8`` (``TLM_BIT_NUM = 8``).

Each analog channel has an enable flag, a source sensor-channel index
(``tlm_ana_channel[]``, ``0xFF`` = none), a quadratic calibration
(``value = a·x² + b·x + c``), an expected raw-input range that bounds the
transmitted value, and a decimal-place count. Each digital bit has an enable flag, a source channel, a sense
(Normal / Inverted), per-bit RF/INET routing, and an operator-facing label used
in the BITS message.

What goes on-air
================

``build_tlm_data_packet()`` (in ``telemetry.c``) resolves each mapped channel
from the registry (via ``sensors_local_save_one()``) once per second and
encodes the periodic data report:

.. code-block:: text

   T#sss,a1,a2,a3,a4,a5,bbbbbbbb

The analog fields carry the **raw** sensor reading, clamped to the channel's
declared raw range and written with the per-channel field width and decimals;
the eight ``b`` characters are the digital bits. The calibration is *not*
applied here — APRS101 splits report from metadata, so the ``EQNS.`` message
carries the a/b/c coefficients and each receiving station recovers the
engineering value itself. The report **never** carries channel names — per the
APRS spec, names, units and equations travel separately.

The declared raw range defaults to 0–1023, a 10-bit ADC span, and applies to
both field widths. A channel whose source reads outside that span needs its
range widened, otherwise the transmitted value is pinned to the edge of the
declared span. A range that is inverted or empty declares nothing and is
ignored, leaving only the bound the chosen field width imposes (000–999 for
the three-digit form, none for the free-decimal form).

The metadata messages
=====================

At a slower cadence (``info_interval``), the module emits the definition
messages as APRS messages addressed back to the station itself:

.. code-block:: text

   :MYCALL   :PARM.<analog names>,<bit names>
   :MYCALL   :UNIT.<analog units>,<bit on-state labels>
   :MYCALL   :EQNS.<a,b,c per analog channel>
   :MYCALL   :BITS.<bit sense bitmap>,<project title>

Generation of each is individually toggleable (``gen_parm``, ``gen_unit``,
``gen_eqns``, ``gen_bits``).

A line that does not fit — longer than ``APRS_TNC2_MAX_LEN``, the longest text
the modem can encode into an AX.25 frame — is refused rather than truncated,
and neither leg transmits it. That matters most for the definition messages: a
coefficient cut mid-number leaves an ``EQNS.`` line every receiver still reads
as well formed, and each of them then applies a different calibration to this
station's raw readings for as long as that definition stands. The warning in
the log names the field to shorten.

Report parameters
=================

The configuration also carries APRS101 chapter-13 framing options: a free-text
digipeater path (``report_path``), destination TOCALL (``tocall``),
auto-incrementing sequence number (``auto_seq``), analog field width
(``field_width``), an option to omit unused trailing channels
(``omit_trailing``), a trailing free-text comment (``trail_comment``), and the
number of analog/digital channels actually sent (``analog_count`` /
``digital_count``).

``report_path`` applies to the radio transmission only. The APRS-IS
transmission of a telemetry report - data and definitions alike - carries
``TCPIP*`` as its whole path, as `aprs-is.net's connection guidance
<https://www.aprs-is.net/Connecting.aspx>`_ requires of a client's own
traffic, so each enabled leg is built as its own line. The definition Messages
go out on the air with no via path, direct, as they always have.

Setting ``field_width`` to 3 zero-pads each analog value to three digits,
000-999 - the range APRS 1.2 allows for this field, extended from the
original APRS101 000-255 window. A channel whose receiving station still
expects the older 0-255 range can be kept inside it by setting that channel's
``ana_raw_min``/``ana_raw_max`` accordingly.

Comment telemetry (APRS 1.2 base-91)
=====================================

Alongside the ``T#nnn`` report, the *Comment Telemetry* option
(``comment_telemetry`` / ``cmtTlm``) makes ``telemetry_build_comment_tlm()``
append a second, compact encoding of the same sample to a station's position
comment:

.. code-block:: text

   |ss1122|

The group opens and closes with ``|``. The first base-91 pair is the sequence
number; each following pair is one analog channel, in order (``A1`` first). A
final pair may carry the whole 8-bit digital bank as a single number, its least
significant bit being ``B1`` and its eighth bit ``B8``.

This is not a beacon of its own. It rides inside the position comment of
whichever beacon — Tracker, IGate or Digipeater — is currently transmitting
under the callsign/SSID configured on the *Telemetry* page; a position beacon
running under any other callsign/SSID never gets the group appended, since a
receiving station would otherwise read it as that other station's own
telemetry. Status reports, objects and items never carry it: only a position
report identifies a single reporting station unambiguously enough for the
group to mean anything.

The sequence number is the same counter the ``T#nnn`` report uses, taken from
the same snapshot of channel values, so the two never disagree about which
sample they describe. Base-91 encoding gives that counter a 0-8280 window
(91×91 values), wrapping independently of the report's own 0-999 decimal
field.

Each analog pair is only emitted for a channel that is both enabled and
currently resolved from the sensor registry, and only as long as every
channel before it in the A1-A5 order was too: the group has no per-pair
channel identifier, so a receiving station recovers each value's channel
purely from its position in the sequence. The encoder stops at the first gap
rather than skip it, keeping the group an unbroken prefix of A1, A2, ... An.

APRS 1.2 requires the extension to carry the sequence counter *and* at least one
channel, so a station with no analog channel currently enabled and resolved
emits no group at all rather than a bare ``|ss|``. An empty group is a form a
strict parser is entitled to reject, and it would spend four bytes of comment
budget on every beacon while carrying nothing.

The digital pair is legal only after all five analog pairs — with a shorter
group in front of it, a receiver would read it as the next analog channel — so
it is emitted only when every analog channel resolved *and* the digital bank is
routed with at least one channel configured. The group is a single string
appended to a position report that goes out over whichever legs that beacon
uses, so it has no per-leg form of its own: a digital channel travels whenever
the bank and the channel are routed to either leg. A channel that must stay off
the air entirely is disabled on the *Telemetry* page rather than unrouted.

A group that would not fit the telemetry station's own ``telemetry_build_comment_tlm()``
output buffer is dropped rather than truncated — a truncated base-91 pair
decodes to a wrong value, not a missing one. Once resolved, the group's bytes
(and the trailing ``!DAO!`` extension's, if enabled) are reserved ahead of the
operator's own comment text, so a position report whose comment would
otherwise overflow the field truncates the *comment*, never the telemetry
group or the DAO extension that follows it.

Because ``|`` delimits this group and ``~`` is reserved alongside it, neither
character may appear anywhere else in the same information field without a
receiver misreading which region is the telemetry group. Every operator
comment and status text this firmware transmits — Tracker, IGate and
Digipeater position/Mic-E comments, status text, the weather report comment,
and object/item and bulletin text — has both characters filtered out before it
goes on the air,
regardless of whether that particular beacon carries a comment telemetry
group itself. ``{`` is left untouched in these fields, since it is the
compressed-position radio-range marker rather than a telemetry delimiter.

A CR or LF is a separate concern from the telemetry delimiters above: APRS-IS
and the internal AX.25 TNC2 text form are both line-oriented and neither
escapes an embedded line break, so either byte is stripped from every
operator-editable field at the point it is stored — as it is decoded from a
web form POST, and as it is loaded from a hand-edited ``config.json`` — rather
than only when the field is rendered onto the air. This keeps a line break
out of every consumer of the stored text, not only the beacon builders this
chapter covers.

Within the position report's text field the emission order is fixed:
frequency block (if any), operator comment, comment telemetry group, then
``!DAO!`` (if enabled) — matching APRS101 chapter 13 and the DAO extension's
own placement rule (``aprs12/datum.txt``). This order holds for both the
uncompressed layout and Mic-E.

Web page pickers
================

The *Telemetry* page (``page_tlm.c``) populates a *Source* dropdown for each
analog channel and a *Channel* dropdown for each digital bit from the live
``sensors_local`` registry, filtered by each driver's advertised telemetry
channels. Live per-channel values are shown via ``/tlm/values``. See
:ref:`en-sensor-framework`.
