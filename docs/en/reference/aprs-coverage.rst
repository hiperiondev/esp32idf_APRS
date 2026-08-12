.. _en-aprs-coverage:

======================
APRS Protocol Coverage
======================

This page maps the firmware against the APRS protocol itself, chapter by
chapter, rather than against other APRS software. Where
:ref:`en-limitations` asks *"how does this project compare with Direwolf or
Xastir?"*, this page asks *"how much of the specification is on the air?"*.

The reference used throughout is the APRS Protocol Reference 1.2 — the
consolidation of the original APRS101 1.0.1 (2000), the APRS 1.1 addendum
approved in 2004, and the 1.2 additions published since — together with the
individual specification files on ``aprs.org`` for the features that only
exist there.

Legend:

* ✅ — Implemented and working
* ⚠️ — Partial / limited implementation
* ❌ — Not implemented

A ❌ is not automatically a defect. Several rows below describe formats the
specification itself marks as obsolete or "not recommended", and a few
describe conventions that need radio hardware this project does not drive.
The Notes column says which is which.

Throughout the tables, *transmit* means the station can originate the format,
and *receive* means the format is decoded well enough to feed the duplicate
cache, the range filter, Last Heard and the traffic log — not merely relayed.
A packet the firmware cannot decode is still digipeated and still gated,
because both of those paths work on the AX.25 address field.

AX.25 and channel access (ch. 3)
================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - AX.25 UI frames, control 0x03 / PID 0xF0
     - ✅
     - Full soft-modem transmit and receive on the ESP32's own ADC and DAC. Connectionless UI frames only, which is all APRS uses.
   * - Address field: destination, source and 0–8 digipeaters
     - ✅
     - Decoded and re-encoded with the has-been-repeated bit honoured. The decoder walks the address field against the frame length, so a header claiming more repeaters than the frame carries is rejected rather than read past. Path builders enforce the 8-address limit.
   * - p-persistent CSMA channel access
     - ✅
     - Carrier detect plus a configurable persistence value and TX delay, with an anti-starvation floor so a busy channel cannot block a frame forever. Forced transmissions are counted separately for channel-busy and failed-persistence draws.
   * - FX.25 forward error correction
     - ✅
     - Three modes: off, receive only, receive and transmit. All 11 correlation tags. Transmitted codeblocks stay readable by a plain AX.25 receiver.
   * - KISS / AGWPE host interface
     - ❌
     - The station cannot act as a TNC back end for external client software. Deliberate: the firmware is a complete station, not a modem peripheral.

Destination and source address fields (ch. 4)
=============================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - Software version identifier in the destination (TOCALL)
     - ⚠️
     - Every originated packet uses ``APZ32L``. That is a valid experimental TOCALL, but it is not registered in the ``aprsorg/aprs-deviceid`` list, so receiving clients show the station as a generic experimental application instead of naming the firmware.
   * - Mic-E encoded data in the destination address
     - ✅
     - Latitude digits, north/south, east/west and longitude offset are encoded on transmit and reassembled on receive together with the information-field half.
   * - Generic digipeater path in the destination SSID
     - ✅
     - Recognised by the digipeater as a legacy routing form, behind an explicit switch that is off by default so it never short-circuits an explicit path.
   * - Symbol carried in the destination address (GPSxyz / SYMxyz)
     - ✅
     - Read on receive when the information field yields no symbol of its own, in all of the ``GPSxy``, ``SPCxy``, ``SYMxy``, ``GPSCnn`` and ``GPSEnn`` forms, including the overlay character on alternate-table symbols. Mic-E packets are excluded, since their destination address carries position data. Originated traffic keeps the symbol in the information field, so nothing is written in this form.
   * - Symbol from the source address SSID (obsolete)
     - ⚠️
     - Applied on receive only as the last step of the symbol precedence chain, and only to raw NMEA packets — the one case the convention was invented for. Any other data type keeps the SSID as a station role, which is what it means today.
   * - Alternate nets
     - ❌
     - Originated traffic always uses the project TOCALL; there is no setting for an alternate-net destination address.

Time and position formats (ch. 6)
=================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - Latitude and longitude, uncompressed
     - ✅
     - Transmitted and parsed in the ``DDMM.mmN`` / ``DDDMM.mmW`` form, with a shared formatter so every originating service produces identical coordinates.
   * - Zulu day/hour/minute timestamp
     - ✅
     - The only form this station originates, on positions, objects, status reports and weather. The clock runs in UTC throughout, so no timezone conversion is involved.
   * - Local day/hour/minute and hour/minute/second timestamps on receive
     - ✅
     - All four forms are read: the two zulu 7-byte forms, the legacy local-time one and the month/day/hour/minute stamp of a positionless weather report. The zulu forms are resolved to absolute UTC against the clock, stepping back a day, a month or a year when the value would otherwise land in the future, and the local form is reported exactly as the sender wrote it, since the packet does not name the time zone it is in. The value is shown in the DECODED column of the traffic table, which is what tells a packet relayed with minutes of latency apart from one just heard.
   * - Position ambiguity
     - ✅
     - One to four digits blanked on transmit, station-wide. On receive the blanked minutes are parsed digit by digit and resolved to the centre of the ambiguity box, so a coarse position still range-filters sensibly instead of collapsing toward the degree boundary.
   * - Altitude
     - ✅
     - The six-digit ``/A=`` form in comments, per station role, plus the base-91 form inside Mic-E.
   * - High-precision ``!DAO!`` and datum option
     - ✅
     - Transmitted on uncompressed positions and inside the Mic-E text field, suppressed when position ambiguity is in use or the compressed layout is selected — both of which already carry a different precision claim. A received token is applied the other way round, on both of its on-air forms — the human-readable digits and the base-91 one most trackers emit — so an incoming uncompressed position is refined by up to about 18 m before the range gate measures it. A compressed report is left alone, since its base-91 fields already carry that precision.
   * - Raw NMEA position reports (``$``)
     - ✅
     - ``RMC``, ``GGA`` and ``GLL`` sentences are decoded on receive, with any two-letter talker identifier so that multi-constellation receivers are covered as well as GPS-only ones. The optional checksum is enforced when present, and a sentence reporting an invalid fix is refused, so the IGate range filter never evaluates a station on a stale coordinate. ``$GPWPL`` names a waypoint rather than the sender's own fix and is deliberately left undecoded; ``$ULTW`` is a weather record and is routed as one.
   * - Default null position
     - ❌
     - The configured coordinates are always transmitted as they stand; there is no convention for signalling "position unknown" when the operator has not set them.

Data extensions (ch. 7)
=======================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - Course and speed
     - ✅
     - In the uncompressed 7-byte slot, in the compressed two-byte form, and in Mic-E.
   * - Wind direction and speed
     - ✅
     - Occupies the same 7-byte slot in weather reports, in the positioned form, with placeholders when no sensor is mapped.
   * - Power / height / gain / directivity (PHG)
     - ✅
     - Built from watts, feet, dBi and a direction on the Station page and mirrored into the per-role beacons and objects.
   * - PHGR "probes" (beacon rate character)
     - ✅
     - The 1.2 nine-byte form ("PHGphgd" plus a beacons-per-hour rate character and its mandatory trailing slash) is transmitted whenever the IGate beacon's own interval is known, which it always is, and is parsed on receive: the rate character and slash are recognised and stripped so the comment that follows is read correctly rather than starting with a stray slash.
   * - Pre-calculated radio range (RNG)
     - ✅
     - Selectable as the data extension for any beacon role, in statute miles.
   * - Omni-DF signal strength (DFS)
     - ✅
     - Selectable as the data extension, with the S-point strength plus the same height, gain and directivity codes PHG uses.
   * - Bearing and number/range/quality (BRG/NRQ)
     - ✅
     - Available on objects and items, in the ``000/000`` form the specification requires.
   * - Data extensions on receive
     - ✅
     - The 7-byte slot of an incoming uncompressed report is parsed rather than read as the first seven characters of the comment: ``PHGphgd`` and its nine-byte PHGR form, ``RNGrrrr``, ``DFSshgd`` and ``CSE/SPD``, which is reported as wind direction and speed when the symbol is a weather station. The comment is then taken from the first byte past whichever token was found, so the nine-byte form no longer leaves a stray rate character and slash at its front.
   * - Area object descriptor
     - ✅
     - Full shape, colour and size encoding, including the rule that replaces the slash with a digit for colour values of ten and above.

Position and DF report formats (ch. 8)
======================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - All four position data type identifiers
     - ✅
     - Transmit and receive. The choice between the messaging-capable and non-messaging identifiers follows the station's actual messaging setting rather than whether a timestamp happens to be present.
   * - Comment field
     - ✅
     - Carried on every originated position, with the frequency block, ``!DAO!`` and comment telemetry reserving their bytes before the free text is allowed to fill the field, so a long comment truncates instead of dropping an extension.
   * - DF report format
     - ⚠️
     - The bearing and NRQ fields are produced on objects and items, which covers reporting a fix on someone else. There is no dedicated DF report role for the station's own beacon.

Compressed position reports (ch. 9)
===================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - Base-91 compressed latitude and longitude
     - ✅
     - Selectable per service — tracker, IGate, digipeater and objects — and decoded on receive. Compression is automatically suppressed when a PHG or DF extension or position ambiguity is in use, since none of those survives the compressed layout; a pre-calculated radio range is the one extension that does, and folds into the two-byte field instead.
   * - Compressed course/speed and the compression type byte
     - ✅
     - A moving tracker encodes course and speed into the two-byte field and sets the type byte to compressed course/speed with a current fix; a station with nothing to report sends the three-space "no data" encoding. The field quantises course to a 4 degree step and speed to a step of about 8 per cent, and the encoder keeps both bytes inside the range the course/speed form owns, so a token is never read back as the radio range form that shares those two bytes. On receive the three bytes are read back the same way: the type byte decides between altitude, radio range and course/speed, so a moving station shows its heading and speed instead of a bare coordinate.
   * - Compressed pre-calculated radio range
     - ✅
     - A beacon whose data extension is the pre-calculated radio range folds it into the two-byte field — the reserved ``{`` marker followed by the range digit — rather than falling back to the uncompressed layout, so a coverage circle travels with a compressed position and no ``RNGrrrr`` token is left in the information field. The field quantises the range to a step of about 8 per cent, from a floor of 2 miles.
   * - Compressed altitude
     - ✅
     - A compressed beacon carrying an altitude puts it in the same two bytes, with the type byte naming GGA as the source, which is what selects that reading of them. The ``/A=`` comment token is left out when it does, so the altitude is stated once and costs nothing rather than nine bytes. A radio range keeps the two bytes when both are configured, since it has nowhere else to go and altitude still has the comment form to fall back on. The step is about 0.2 per cent.

Mic-E data format (ch. 10)
==========================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - Mic-E transmit and receive
     - ✅
     - Complete encoder and decoder covering the destination-address half, longitude, speed and course, and both the current and old data type identifiers.
   * - Type code and manufacturer identifier
     - ✅
     - The type byte follows the symbol table byte and reflects the station's messaging capability; the manufacturer and version pair closes the text field. Without these a Mic-E beacon is anonymous to every client, since the destination address is carrying position and cannot also identify the firmware.
   * - Altitude, frequency block and ``!DAO!`` in the Mic-E text field
     - ✅
     - Altitude leads the text field and shifts the comment rather than replacing it, and the frequency block and datum extension are emitted in the canonical order the specification's own examples show.
   * - Position comment codes (Off Duty, En Route … Emergency)
     - ✅
     - The receiver decodes all fifteen values, including the custom set and the all-zero Emergency pattern, and correctly reports a mixed standard/custom pattern as undefined. The Tracker page selects any of the fourteen standard and custom values for transmission. Emergency is deliberately absent from that list: it asks other operators to respond to a real emergency, which is not something a settings page should arm with one mis-click and leave armed for every beacon afterwards.
   * - Emergency indication
     - ⚠️
     - A Mic-E emergency received on radio or from APRS-IS raises a warning-level log line and its own entry in the traffic log, next to the packet that carried it. The other fourteen position comments are logged at information level, since the value lives in the destination address and is otherwise invisible in the packet text. The bracketed comment-field forms (``!EMERGENCY!``, ``!WXALARM!`` and the rest of that proposed set) that let a non-Mic-E station raise the same signal are not recognised, so a station declaring an emergency that way passes through as ordinary traffic today. Low frequency of occurrence, high consequence when it does occur; classifying those forms alongside the Mic-E case is worth doing regardless of how rarely the field sees a real one.
   * - Speeds above 670 knots
     - ✅
     - The 1.2 extension is applied on both sides, so a frame digipeated through a space station reports its orbital velocity rather than a clipped one. That scale is quantised in steps of 112 knots and has a gap between 671 and 781 knots that the published rule itself leaves unrepresentable; below 671 knots the field stays exact to the knot.
   * - PHG inside the Mic-E text field
     - ✅
     - The data extension is written into the Mic-E text, behind the frequency block and ahead of the operator's comment, so a station beaconing in Mic-E advertises its coverage the way the 1.2 addition allows. The Tracker page carries the switch; its four sub-fields are the station's own antenna data.
   * - Mic-E telemetry
     - ❌
     - Deliberately absent: version 1.2 deprecates this format in favour of the manufacturer type codes and the base-91 comment telemetry, both of which this firmware implements.

Object and item reports (ch. 11)
================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - Object reports
     - ✅
     - Five configurable slots, nine-character padded names, live and killed states, uncompressed or compressed position, with the whole set transmitted and received.
   * - Item reports
     - ✅
     - Variable three to nine character names with the live and killed markers. Note that the specification marks the item format as not recommended on RF; objects are the better choice for anything long-lived.
   * - Killing an object or item
     - ✅
     - A killed object is retransmitted a few times as a kill report before its enable flag is cleared, so receivers actually see the withdrawal instead of the object simply ageing out of their lists.
   * - Permanent objects
     - ✅
     - The fixed all-ones pseudo-timestamp is emitted for an object marked permanent, which is what a standing repeater or landmark object needs.
   * - Area objects
     - ✅
     - Shape, colour, line width and size, on the same object slots.
   * - Signpost objects and items
     - ✅
     - Up to three characters of signpost text in braces, on the signpost symbol.
   * - Proportional pathing for objects
     - ✅
     - One path preset per transmission, rotating between them, so a long-lived object does not flood every hop on every cycle. Each preset is hop-counted and dropped from the rotation if it exceeds the AX.25 limit.

Weather reports (ch. 12)
========================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - Complete weather report, with and without timestamp
     - ✅
     - The recommended form, with the weather symbol, the wind data extension and the full weather data block. The message-capable data type identifier follows the station's messaging setting, as it does for ordinary positions.
   * - Weather report as an object
     - ✅
     - A named weather object at a position other than the station's own.
   * - Positionless weather report
     - ⚠️
     - Emitted when the station has no coordinates configured, with the month/day/hour/minute timestamp the format requires. Snow is omitted from this form on purpose, because the letter it would use is already wind speed there; the firmware logs when that drops a configured reading. The specification marks this format as not recommended.
   * - Mandatory weather fields
     - ✅
     - Wind direction, wind speed, gust and temperature are always emitted, falling back to the dot placeholders when no sensor is mapped, so the report stays a valid weather frame rather than a truncated one.
   * - Rain, humidity and barometric pressure
     - ✅
     - Rain over the last hour, last 24 hours and since midnight; humidity with the two-zero encoding for 100 %; pressure in tenths of a millibar in the full six-character field the 1.2 revision confirms.
   * - Luminosity and snowfall
     - ✅
     - Luminosity uses the upper-case letter below 1000 W/m² and the lower-case one above it; snowfall uses the fractional form below ten inches and the zero-padded integer form above.
   * - Water gauge / flood height
     - ✅
     - Both the foot and metre forms from the 1.2 water gauge proposal, at tenth resolution, unpadded as that document's own example shows.
   * - Software type and weather unit identifiers
     - ⚠️
     - Both are emitted, as the last token of the information field so a strict parser does not absorb the operator's comment into the unit string. The unit code is free-form and fine; the single software-type character is one the specification does not allocate.
   * - Raw rain counter
     - ✅
     - The gauge's own running count, four digits after a ``#``, transmitted unscaled and never reset by the station so a receiver can difference two reports. It is a row of the sensor mapping table like every other weather field, emitted only when a driver is mapped to it.
   * - Radiation and voltage fields
     - ❌
     - The two fields proposed for 1.2 have no weather token of their own and travel as telemetry analog channels instead, which is where the sensor framework routes them.
   * - Raw Peet Bros and Ultimeter weather reports
     - ⚠️
     - Recognised as weather by the gating classifier so they route correctly, but the raw payloads are never decoded into readings. The specification says senders should convert to the complete format anyway, so this is receive-side breadth rather than a transmit gap.
   * - Storm data
     - ❌
     - The data model for tropical cyclone reports exists in the headers, but nothing encodes or decodes the on-air form. A station of this kind has no source for that information — it comes from weather services — so relaying such packets untouched, which is what happens today, is the sensible behaviour.

Telemetry data (ch. 13)
=======================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - Telemetry report
     - ✅
     - Sequence number, five analog channels and the eight-bit digital bank, with the channels mapped to local sensor drivers by name so enabling or disabling a driver cannot silently re-point them.
   * - Extended 000–999 analog range
     - ✅
     - The three-digit field accepts the full 1.2 range rather than the original 000–255 window, with out-of-range readings clamped rather than wrapped. A per-channel raw minimum and maximum is applied first, so an operator can keep values inside whatever window their receiving software understands.
   * - On-air parameter definitions
     - ✅
     - All four definition messages — names, units and labels, equation coefficients and bit sense with project title — sent as addressed messages, as the specification requires. The report carries the raw value and the receiver applies the coefficients.
   * - Base-91 comment telemetry
     - ✅
     - The sequence counter, the analog channels and the eight-bit digital bank are encoded into the pipe-delimited group. Because that group is positional — the nth pair *is* channel n, with no per-pair identifier — the encoder stops at the first channel that is disabled or unresolved rather than leaving a gap that would shift every later channel one slot, and the digital pair is appended only behind a full set of five analog pairs, which is the only place the specification allows it. A station with no channel to report emits no group at all, since the extension must carry the counter and at least one channel.
   * - Alternative sequence number form
     - ❌
     - The three-letter sequence identifier some encoders use in place of a number is neither produced nor specially recognised on receive.

Messages, bulletins and announcements (ch. 14)
==============================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - Text messages with acknowledgement and rejection
     - ✅
     - Nine-character addressee field, acknowledgement and rejection matching restricted to the one to five alphanumeric characters the specification allows, and a retry timer for unacknowledged outgoing messages.
   * - Reply-ACK
     - ✅
     - All seven rules of the algorithm, including building the suffix at the instant of transmission and keeping a small per-station table of owed acknowledgements. This is what makes a message exchange flow at conversation speed instead of two packets per turn.
   * - Message number format
     - ⚠️
     - Incoming numbers of any legal length are matched. Outgoing messages number themselves with two digits, wrapping at 99, so that a Reply-ACK suffix still fits inside the five characters the specification allows for the whole identifier.
   * - Message groups
     - ✅
     - Messages addressed to the built-in group names ``ALL``, ``QST`` and ``CQ``, or to any of the operator-defined group names, are matched case-insensitively alongside the station's own callsign, across all SSIDs. A group message stays distinct from a direct message of the same text and is displayed but never acknowledged, since a group has no single owner to answer for it.
   * - OBJECT-in-MSG and ITEM-in-MSG
     - ❌
     - The two 1.2 proposals that carry a full object or item report inside a message payload, for a station that cannot digipeat the ordinary object/item packet, are not recognised as a class. This station has no map to plot one on and neither originates nor needs the workaround; a message using either form still reaches the operator as plain text.
   * - UTF-8 text encoding
     - ⚠️
     - Message and other free-text fields are 8-bit-clean end to end — nothing here re-encodes or rejects a non-ASCII byte, which is the specification's own recommendation (``aprs.org/aprs12/utf-8.txt``). What is not yet guaranteed is that a length-driven truncation always lands on a character boundary rather than through the middle of one; the outgoing message path enforces this, the rest of the free-text fields do not yet.
   * - General bulletins, announcements and group bulletins
     - ✅
     - Five configurable slots with the correct addressee forms for all three: the digit identifier for bulletins, the letter identifier for announcements, and the group name suffix for group bulletins. Each slot has its own expiry.
   * - Bulletin transmit cadence
     - ⚠️
     - Bulletins go out on a fixed interval with an expiry time. The specification recommends a decaying schedule instead — frequent at first, then tapering over hours — which puts less load on a shared channel for the same effect.
   * - National Weather Service bulletins
     - ❌
     - Bulletins addressed to the weather service prefixes are handled as ordinary messages rather than recognised as a class of their own. They are relayed correctly and never acknowledged, because the addressee is not this station, so the practical effect is limited to how they are labelled in the UI.
   * - NTS radiograms
     - ❌
     - The line-prefix convention is not parsed. The specification explicitly says an application need not understand it, since the lines are ordinary messages and read correctly as plain text — which is what happens here.

Station capabilities, queries and responses (ch. 15)
====================================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - General queries
     - ✅
     - The all-stations query, including the optional latitude, longitude and radius footprint, plus the weather, IGate and QRU broadcast queries. A query arriving from the internet is answered on the internet and one arriving on RF is answered on RF, so an internet query can never key the transmitter.
   * - Directed station queries
     - ✅
     - The complete set: position, status, stations heard direct, heard-station history, outstanding messages, objects and route trace, including the ping alias for the trace query. Answers are built by the beacon scheduler rather than by the receiving task, so a burst of queries cannot overflow a receive stack.
   * - Heard-station history
     - ✅
     - The eight-hour histogram the specification asks for, kept per station and carried with the station when its row moves to the front of the table.
   * - Station capabilities packet
     - ⚠️
     - Sent in reply to the IGate query with the gateway token and the message and local-station counts, where the counts mean what the specification says rather than raw frame totals. The capability model is open-ended, so the station could also advertise its digipeater, weather and telemetry roles; it does not.

Status reports (ch. 16)
=======================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - Status report with and without timestamp
     - ✅
     - Per-role status text with an optional zulu timestamp.
   * - Status text length budget
     - ✅
     - The 62-character limit is enforced by discarding optional blocks in a defined order — locator first, then the frequency block — and the operator's own text is never touched. If it still does not fit, the report is refused rather than truncated into a malformed frame.
   * - Status report with Maidenhead grid locator
     - ⚠️
     - The four or six character locator and its symbol are produced, but two placement rules are not met: the specification requires the locator to follow the data type identifier immediately, and forbids combining it with a timestamp. Here the timestamp and the frequency block can both precede it, so a strict receiver will not recognise the locator form.
   * - Beam heading and effective radiated power
     - ✅
     - The two characters close the status text after a ``^``, from a station-wide heading and power set on the Station page. The heading steps in ten degrees and the power is matched to the nearest entry of the specification's table, which runs from 10 to 7290 watts. Both halves have to be set for the block to appear, and it is the one block the length budget never drops — a station running meteor scatter is sending the report for those three bytes.

Network tunnelling and third-party traffic (ch. 17)
===================================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - Third-party frames
     - ✅
     - Built when relaying internet traffic to RF and unwrapped on the way in, with the inner station's callsign used for duplicate suppression and Last Heard rather than the gateway's. A frame that will not fit is refused instead of truncated.
   * - Path tokens that forbid gating
     - ✅
     - The classic no-gate tokens and the internet-only q-constructs are all honoured, and only in the header where they are meaningful — a packet whose comment text happens to contain one of these words is not mistaken for a packet routed with it.
   * - No-archive marker
     - ❌
     - The ``!x!`` string that asks the databases behind APRS-IS not to store a packet is neither written into the station's own beacons nor given any special treatment when relaying. It addresses the archives rather than the gateways, so it never decides where a frame may travel; relayed packets keep it because the payload is passed through byte for byte.
   * - IGate-to-RF path report
     - ❌
     - The experimental ``{IP-`` wrapper that would let this station announce, over APRS-IS, the AX.25 path it used to gate a packet onto RF is not generated. Unlike most of the other 1.2 proposals in this table, the station is squarely in this one's scope: it is a bidirectional IGate and does gate to RF. It remains a proposal rather than a ratified addition, has seen thin adoption among IGates in general, and would add a second transmit on every gated packet at the cost of channel time this deliberately lightweight design does not spend elsewhere. Revisit if the proposal is ever ratified or an operator asks for it.

Frequency specification (ch. 18)
================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - Frequency block in positions, objects and status
     - ✅
     - All three fixed ten-byte forms are produced — the ten-kilohertz form below 100 MHz, the plain form in the VHF and UHF range, and the letter-prefixed form for the microwave bands — working in whole kilohertz so no digit drifts, and refusing to emit anything that is not exactly ten bytes.
   * - Tone, offset and range
     - ✅
     - The three-digit CTCSS tone, the offset in units of ten kilohertz, and the two-digit coverage range in miles or kilometers, in the order the frequency specification defines. Range is an Objects/Items sub-field (a fixed repeater's own advertised coverage), not a per-service tracker/IGate/digipeater setting.
   * - Narrowband tone, DCS code and split TX/RX frequency
     - ❌
     - Three further optional sub-fields the specification defines are not built: the lower-case narrowband-modulation flag, the DCS code that can stand in for the CTCSS tone, and the split transmit/receive frequency form. None of the three is a defect in the block this station transmits, which stays well-formed and auto-tunable without them - they are capability gaps, declined because the firmware has no narrowband/DCS radio setting to report and objitem_t models a single monitor frequency rather than independent TX/RX ones.
   * - Frequency and QSY requests inside messages
     - ❌
     - The proposed message forms that request or command a change of operating frequency are not generated or acted on. They are 1.2 proposals with thin field deployment.

User-defined and other packet types (ch. 19–20)
===============================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - User-defined data format
     - ❌
     - The private extension space the specification reserves for experimenters is unused. It would be the correct home for any firmware-specific diagnostic the project wanted to put on the air, instead of overloading status text.
   * - Invalid data and test data packets
     - ❌
     - The test-data identifier is not recognised as its own class, so such packets fall through to the catch-all path. They should never be gated to the internet.
   * - Agrelo direction-finding format
     - ❌
     - The bearing-and-quality format from the standalone direction finder is not decoded. Very few units remain in service.
   * - Maidenhead grid locator beacon
     - ❌
     - The standalone locator beacon identifier is not decoded. The specification itself marks it obsolete; the locator lives in status reports now.
   * - Reserved identifiers (map feature, shelter data, space weather)
     - ❌
     - Reserved by the specification and never defined further, so there is nothing to implement.

None of the five identifiers above carries a payload this firmware decodes, but
all five are classified for gating under one shared filter bit, the "Other"
checkbox of the IGate Filter page, so a packet of any of those kinds can be
forwarded to APRS-IS instead of being dropped whatever the operator ticks.
Third-party traffic and test data stay deliberately unclassified and are never
relayed: re-gating third-party traffic is how IGate loops start, and test data
is not meant to leave the channel it was sent on.

Symbols (ch. 21)
================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - Primary and alternate symbol tables
     - ✅
     - A visual picker in the web admin covers both tables, with a per-role symbol for the tracker, IGate, digipeater, weather station and each object.
   * - Overlay characters
     - ✅
     - An overlay character can be placed in the table position for the symbols that accept one, which is how a digipeater advertises its own routing policy on the map.
   * - Symbol precedence
     - ⚠️
     - Only the information-field symbol is ever read, so the precedence question does not arise in practice — but it also means the fallback sources the rule describes are never consulted for a packet that carries no symbol there.
   * - SSID conventions
     - ✅
     - The recommended role SSIDs are the factory defaults for each service, and every SSID field is range-checked on both the form and the configuration loader.

Digipeating and the New-N paradigm
==================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - New-N traced digipeating
     - ✅
     - An operator-defined alias table with a wildcard form, a maximum hop count per row and a trace or fill mode per row. The legacy aliases the New-N paradigm replaced are deliberately not honoured.
   * - Fill-in (home) digipeater role
     - ✅
     - A single switch restricts the station to single-hop rows, which is the correct configuration for a home digipeater serving stations that cannot reach the wide-area one.
   * - Hop-count trapping
     - ✅
     - A hop count above the matched row's maximum is either clamped down and repeated or dropped, at the operator's choice, with the drop counted under its own reason on the dashboard.
   * - Duplicate suppression
     - ✅
     - A shared cache with a configurable depth and time window, used by both the digipeater and the gateway so a frame cannot be repeated by one path after the other has already seen it.
   * - Preemptive digipeating
     - ✅
     - Off by default and selectable in two indicator modes: the addresses jumped over are either kept and marked used, or discarded so that only what is still to be done goes out. The scan runs from the first unused address to the end of the path and claims only a fixed identity, so both exclusions the proposal states - generic n-N aliases, and an alias written with a hop count - are enforced by construction.
   * - Legacy destination-SSID routing
     - ✅
     - Available behind a switch that is off by default. When it is off, a packet using that convention falls through to the explicit path logic instead of being dropped.
   * - RR-bit precedence and operator-present signalling
     - ❌
     - The proposals that repurpose the reserved bits of the SSID octet are not implemented in either direction. The marking mode of preemptive digipeating sets the low reserved bit on the addresses it skips, but the frame is re-encoded from its TNC2 rendering, which does not carry those bits onto the channel.

APRS-IS gatewaying
==================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - APRS feature
     - Status
     - Notes
   * - Login and software identification
     - ✅
     - The login line carries the callsign, passcode, software name and version, and the server-side filter only when one is configured — a bare filter keyword with no argument is never sent.
   * - RF to internet gatewaying
     - ✅
     - Per-type gating checkboxes, a range filter around the station, callsign prefix and blacklist filters, and a satellite gateway list for stations heard through space digipeaters.
   * - Per-station q-construct
     - ✅
     - The gateway chooses between the two receive constructs per source station rather than with one global flag, so a station this gateway would not deliver a message to is marked as such — which is exactly what the construct means.
   * - Internet to RF gatewaying
     - ✅
     - A configurable mask decides which categories may cross, and messages additionally have to satisfy the three conditions the gateway documentation sets out, followed by the position packet of the addressed station.
   * - Server-side filter validation
     - ✅
     - The filter string is checked for grammar before it is sent — term shape and argument type — while the range and sanity of the values are left to the server, which is authoritative.
   * - Server failover
     - ⚠️
     - Four server slots with automatic reconnection. Rotation happens on a failure to establish a session; a server that accepts a connection and then drops it is retried rather than skipped, so a server in maintenance can hold the station until it recovers.

Summary
=======

Counting the rows above: the station implements the whole of the position,
object, message, query and status core of the protocol, both directions, plus
the two network roles (digipeater and IGate) and the post-2000 additions that
matter most in daily traffic — compressed positions, ``!DAO!``, Mic-E with
device identification, the frequency field, Reply-ACK, base-91 comment
telemetry and the New-N paradigm.

The gaps cluster in three places, and they are worth stating plainly:

* **Receive-side breadth.** The station transmits more formats than it
  decodes. Peet Bros and Ultimeter raw weather records are recognised as a
  *category* — enough to route them through the gating filters — but their
  contents are never parsed. For an IGate this shows up as stations that pass
  the type filter but whose measurements are unavailable locally.
* **Post-2004 proposals.** The RR-bit signalling proposal is absent, though
  PHGR probes are supported. These are genuine specification
  additions, not folklore, but their deployment in the field is uneven.
  Preemptive digipeating, the most consequential of the group, is implemented
  and off by default.
* **Formats with no local source.** Storm data, NWS bulletins and NTS
  radiograms are transport-only concerns for a station of this kind: the
  firmware has no source of that information. They are listed for
  completeness, and the sensible behaviour — relaying them untouched — is
  already what happens.

None of the ❌ rows prevent the station from operating as a fully conformant
IGate, digipeater, tracker, weather or telemetry station.
