.. _en-limitations:

============================
Status and Known Limitations
============================

The firmware is **work in progress**. The RF transmit path, IGate, digipeater,
beacons, weather, telemetry, messaging and web admin are all functional.

Feature comparison table
=========================

The table below compares this project's implemented functionality against the
union of features found across the most popular APRS software packages
(desktop/mapping clients such as Xastir, APRSIS32 and YAAC; software TNCs such
as Direwolf and UZ7HO Soundmodem; and headless iGate/digipeater stacks such as
aprx and VP-Digi). No single package in that ecosystem implements every row —
that is normal and expected. The legend is:

* ✅ — Implemented and working
* ⚠️ — Partial / limited implementation
* ❌ — Not implemented

Modem / Layer-2
----------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Specific capability
     - Typical in popular APRS software
     - Here
     - Notes on this project's implementation
   * - AFSK 1200 Bd Bell 202 (standard VHF APRS)
     - ✅ (Direwolf, UZ7HO, VP-Digi, hardware TNCs)
     - ✅
     - Default profile; dual demodulator running in parallel for better decode probability
   * - AFSK 1200 Bd V.23
     - ⚠️ (Direwolf supports it; many clients don't)
     - ✅
     - Selectable modem profile #2; like Bell 202 it runs two demodulators in parallel
   * - AFSK 300 Bd (HF APRS)
     - ✅ (Direwolf, UZ7HO)
     - ✅
     - Selectable modem profile #0
   * - 9600 Bd G3RUH FSK
     - ✅ (Direwolf, dedicated packet TNCs)
     - ✅
     - Selectable modem profile #3
   * - HDLC framing / AX.25 UI encode-decode
     - ✅ (universal)
     - ✅
     - Full soft-modem TX/RX, in software, on ADC/DAC. ``ax25_decode()`` reads
       only the bytes of the frame it is handed: the address field is walked one
       address at a time against the frame length, so a header whose extension
       bits claim more repeaters than the frame carries is rejected instead of
       decoding whatever follows it in memory
   * - FX.25 Reed-Solomon FEC
     - ⚠️ (Direwolf yes; most hardware TNCs no)
     - ✅
     - Three modes on the Radiomodem page: off, RX only (decode FX.25, transmit
       plain AX.25) and RX+TX. Transmitted codeblocks stay backward compatible —
       a plain AX.25 receiver ignores the correlation tag and parity bytes and
       decodes the frame carried inside
   * - IL2P (alternative to FX.25)
     - ⚠️ (Direwolf only)
     - ❌
     - Not implemented
   * - KISS protocol (serial or TCP) to act as a TNC for external client software
     - ✅ (Direwolf, UZ7HO, virtually all soundmodems)
     - ❌
     - Not implemented. No serial or network KISS/AGWPE server — this project cannot act as a TNC "back end" for Xastir/APRSIS32/YAAC etc.
   * - AGWPE protocol
     - ⚠️ (Windows-centric TNCs)
     - ❌
     - Not implemented
   * - CSMA / channel-busy detection before TX
     - ✅
     - ✅
     - DCD-gated p-persistent access: configurable Persist (``csma_persist``,
       1-255), quiet time before access begins (``tx_timeslot``) and
       preamble/TXDelay, plus an eight-slot anti-starvation floor so a channel
       that never clears cannot hold a queued frame forever. The dashboard
       reports how often that floor fired, split between a busy channel and a
       clear one, as *CSMA FORCED (BUSY/PERSIST)*
   * - Long-term transmit duty-cycle ceiling
     - ⚠️ (rare outside commercial/regulated gear)
     - ✅
     - Optional (``duty_cycle_en``, off by default) ceiling of
       ``duty_cycle_pct`` percent (1-100, default 25) measured over a rolling
       10-minute window, accumulated from the estimated on-air time of every
       frame actually transmitted at the configured baud rate. Only
       non-critical traffic is held back: message traffic and digipeat repeats
       always go out. A held-back beacon is deferred, not lost - the owning
       periodic task re-offers it on its next interval - though it is counted
       as ``DROP_TX_DUTY_CYCLE`` for visibility. The dashboard shows measured
       against configured percentage as *TX DUTY CYCLE*, populated even while
       the limiter is off so the ceiling can be judged before enabling it
   * - PTT keying (VOX-free, hardware GPIO)
     - ✅
     - ✅
     - Compile-time GPIO + polarity; runtime-adjustable minimum unkey hold
   * - Built-in RF loopback/self-test tool
     - ⚠️ (rare)
     - ✅
     - "LOOP TEST" — transmits a token packet and verifies the full RX chain decodes it back, with detailed stage-by-stage diagnostics
   * - Flat/discriminator vs. de-emphasized audio input
     - ✅ (Direwolf, UZ7HO)
     - ✅
     - Tells the demodulator whether it is fed speaker audio or unfiltered
       discriminator audio; applied live on save
   * - TX queue depth control
     - ⚠️ (usually a fixed internal queue)
     - ✅
     - ``rf_tx_buffers``: how many frames may wait in the RF TX ring before new
       packets are discarded rather than queued; read on every transmit, so it
       takes effect without a reboot
   * - Minimum PTT unkey hold between frames
     - ⚠️ (TXTAIL on some TNCs)
     - ✅
     - ``ptt_min_unkey_ms``, 0-5000 ms on top of the fixed one-tick release the
       modem always applies — for radios or repeaters that need a longer
       guaranteed gap between transmissions

IGate (RF <-> APRS-IS)
------------------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Specific capability
     - Typical in popular APRS software
     - Here
     - Notes on this project's implementation
   * - RF -> APRS-IS gating
     - ✅ (universal)
     - ✅
     - Full pipeline: dedup -> too-short guard -> path-token filter -> sat-gate rule -> payload-type filter -> range gate -> prefix gate -> budlist
   * - APRS-IS -> RF gating (two-way IGate)
     - ✅ (Direwolf igate mode, aprx, VP-Digi)
     - ✅
     - Own-report echo suppression, payload-type filter, restricted third-party unwrap, budlist
   * - Duplicate-packet suppression
     - ✅
     - ✅
     - Shared cache, depth and window both web-configurable on the IGate page
       (4-40 entries, default 20; 1-120 s window, default 30 s)
   * - ``qAR``/``qAO`` Q-construct insertion
     - ✅
     - ✅
     - ``qAR`` when this IGate can gate messages back to RF for the station
       being gated; ``qAO`` otherwise (receive-only IGates, or a bidirectional
       IGate with INET→RF relay disabled)
   * - Server-side APRS-IS filter string (``r/``, ``p/``, ``t/``, ``b/``...)
     - ✅
     - ✅
     - Sent verbatim in the login line, with local grammar validation before sending
   * - Local range gate (great-circle distance)
     - ⚠️ (some, e.g. aprx ``filter``)
     - ✅
     - Haversine distance vs. "My Station," supports both compressed & uncompressed positions
   * - Local callsign prefix whitelist
     - ⚠️ (uncommon as a first-class feature)
     - ✅
     - Comma-separated prefix list (e.g., ``EA,EB,EC``)
   * - Callsign budlist (whitelist/blacklist)
     - ✅
     - ✅
     - Per-direction mode: off / whitelist / blacklist
   * - Payload-type gating (msg/status/tlm/wx/obj/item/query/buoy/position)
     - ✅ (via APRS-IS filters mostly)
     - ✅
     - Local, bitmask-based, applied on both directions independently of the server filter
   * - Third-party (``}``) packet handling / loop protection
     - ✅ (critical, often manual)
     - ✅
     - Off by default; opt-in unwrap gated behind whitelist-only mode specifically to prevent IGate loops
   * - Auto-reconnect to APRS-IS with backoff
     - ✅
     - ⚠️
     - TCP auto-reconnect, re-reads config on every reconnect, but on a fixed
       1 s retry interval (also 1 s while the device has no internet route)
       rather than an exponential backoff. Each failed attempt moves on to the
       next configured server rather than repeating the same one
   * - Passcode-based APRS-IS login
     - ✅
     - ✅
     - Standard ``user/pass/vers/filter`` login line; server verified/unverified response surfaced
   * - Multiple/failover APRS-IS servers
     - ⚠️ (some support server lists)
     - ✅
     - Four server slots (``APRS_SERVER_NUM``), each with its own Enable
       checkbox, host and port. A failed DNS lookup, connect or login advances
       to the next enabled slot and wraps circularly, retrying every second
       until one accepts; disabled slots are skipped, including on the first
       attempt after boot. All slots share the one login identity
       (callsign/SSID/passcode/filter). The dashboard names the slot in use
   * - Per-drop-reason statistics
     - ⚠️ (uncommon, usually just totals)
     - ✅
     - Named counters (``DROP_TOO_SHORT``, ``DROP_PATH_TOKEN``, ``DROP_RANGE_FILTER``, etc.)
   * - Satellite/ISS gate-call list
     - ⚠️ (aprx and some dedicated satgates)
     - ✅
     - Up to 8 satellite digipeater callsigns; a frame repeated through one of
       them without the repeated (``*``) flag set is dropped before reaching
       APRS-IS

   * - Message gating criteria (addressee/sender locality)
     - ✅ (required of a conformant IGate)
     - ✅
     - All four conditions enforced before a message read from APRS-IS reaches
       RF: addressee heard locally inside the window, sender not heard on RF,
       no ``TCPXX``/``NOGATE``/``RFONLY`` in the sender's header, addressee not
       Internet-connected. Each failure has its own drop reason
   * - Configurable heard-locally window
     - ⚠️ (often fixed)
     - ✅
     - ``igate_local_window_sec``, 60-3600 s, one hour by default
   * - Associated position after a gated message
     - ⚠️ (uncommon)
     - ✅
     - Eight-entry ring of gated-to addressees; the next position or buoy report
       seen for one of them is gated once, replacing the deprecated practice of
       replaying historical positions

Digipeater
-----------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Specific capability
     - Typical in popular APRS software
     - Here
     - Notes on this project's implementation
   * - WIDEn-N digipeating, New n-N Paradigm (traced)
     - ✅ (universal)
     - ✅
     - Hop-count decrement **and** own-call insertion marked used, so every hop
       of a repeated path is identifiable
   * - Configurable alias table
     - ⚠️ (varies; often a fixed list)
     - ✅
     - Four rows of {alias, max N, mode} on the Digi page; ``#`` in an alias
       matches one digit, so one row covers a family (``WIDE#``). Factory table:
       ``WIDE1`` 1 hop, ``WIDE2`` 2 hops, ``WIDE#`` 2 hops, all tracing
   * - Large-N trapping
     - ✅ (expected of every modern digipeater)
     - ✅
     - Per-alias ``Max N``; a larger hop count is clamped down to the limit
       (default) or dropped (``DROP_DIGI_N_TRAPPED``), operator's choice
   * - Fill-in (``WIDE1-1``-only) digipeater role
     - ✅
     - ✅
     - Single checkbox; restricts the station to single-hop alias rows
   * - ``SSn-N`` regional routing
     - ⚠️ (regional convention)
     - ✅
     - An ordinary alias row, typically in Flood mode with the region's own
       hop limit
   * - Untraceable (NOID) ``WIDEn-N`` flooding
     - ⚠️ (legacy behaviour)
     - ❌
     - Not produced for ``WIDEn-N``: the paradigm moved it onto the tracing
       mechanism. Flood mode exists, but only for an alias row an operator
       deliberately runs untraced
   * - ``TRACEn-N`` / ``RELAY`` / ``ECHO`` / ``GATE`` legacy aliases
     - ⚠️ (obsolete)
     - ❌
     - Abandoned as paths and not built in. An operator who still needs one for
       a legacy neighbour adds it as an ordinary alias row
   * - Destination-SSID-encoded hop count (legacy)
     - ⚠️ (older TNCs)
     - ✅
     - Off by default (*Digipeat by destination SSID*). It routes ahead of the
       alias table and on that SSID alone, so an explicit path would never be
       read; switched off, the destination SSID is left untouched and the alias
       table decides
   * - Digipeat duplicate/ping-pong suppression
     - ✅
     - ✅
     - Own window in the shared dedup cache (``DUP_SCOPE_DIGI``), keyed on
       source and payload only, tested before any path work; the window is
       ``g_config.dup_cache_timeout_ms`` (default 30 s)
   * - Callsign-based digipeat filtering (only digipeat certain sources)
     - ⚠️ (some, e.g. VP-Digi)
     - ❌
     - Not exposed as a separate digipeater-specific filter (IGate-side budlist != digi-side filter)
   * - Viscous/preemptive digipeating
     - ⚠️ (rare, advanced TNCs)
     - ❌
     - Not implemented

Tracking / Beaconing
----------------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Specific capability
     - Typical in popular APRS software
     - Here
     - Notes on this project's implementation
   * - Live GPS position input (NMEA)
     - ✅ (universal for mobile trackers)
     - ❌
     - Not implemented. Beacons are fixed-position only — there is no live-position input and no GPS-related configuration
   * - Fixed-position (base station) beaconing
     - ✅
     - ✅
     - Separate position/interval/symbol/comment per role (tracker, IGate, digi)
   * - Smart Beaconing (speed/heading-adaptive interval)
     - ✅ (mobile clients, OpenTracker)
     - ❌
     - No GPS, so not applicable
   * - Course/speed in position reports
     - ✅
     - ⚠️
     - Supported in Objects/Items, but the station's own tracker beacon has no live course/speed source (no GPS)
   * - Compressed (Base-91) position encoding
     - ✅
     - ✅
     - Per-service option on the Tracker, IGate, Digipeater and Objects/Items
       pages; the decoder understands it too. Skipped automatically when
       position ambiguity is non-zero or a data extension is in use, since the
       compressed layout has room for neither
   * - Mic-E position encoding (TX)
     - ⚠️ (mostly mobile-tracker firmware)
     - ✅
     - Tracker beacon page offers a Mic-E option (``aprs_mice_encode()``);
       fixed-position only, so course/speed is always sent as "unknown" and the
       message code is fixed at Off Duty. The information field follows the
       canonical order of ``mic-e-examples.txt``: TYPE byte, altitude,
       frequency block, comment, ``!DAO!``, and the Manufacturer/Version pair
       that identifies the firmware (the destination address carries position
       data, so the ``APxxxx`` TOCALL cannot)
   * - PHG / power-height-gain-directivity
     - ✅
     - ✅
     - Exposed on the IGate beacon page
   * - RNG / pre-calculated radio range
     - ⚠️
     - ✅
     - Selectable as the IGate beacon's data extension (``RNGrrrr``)
   * - DFS / omni-DF signal strength
     - ⚠️ (DF-specific software)
     - ✅
     - Selectable as the IGate beacon's data extension (``DFSshgd``)
   * - Position ambiguity in transmitted reports
     - ⚠️
     - ✅
     - Station-wide level 0-4 on the Station page; applies to the uncompressed
       and Mic-E layouts, and forces the uncompressed layout when non-zero
   * - Maidenhead locator in status reports
     - ⚠️
     - ✅
     - Station-wide option; emits the ``>IO91SX/G`` form of APRS101 ch.16
   * - Maidenhead locator in the AX.25 destination (``[IO91SX]``, obsolete)
     - ⚠️ (legacy software)
     - ❌
     - Marked obsolete by the spec itself; not produced
   * - Altitude in beacons
     - ✅
     - ✅
     - Per-role altitude (tracker, IGate, digipeater), each mirrored from the
       "My Station" value when *Use My Station Data* is ticked. Weather reports
       carry no altitude field
   * - Configurable digipeat path per service
     - ✅
     - ✅
     - Four shared path presets; every transmitting service (tracker, IGate,
       digipeater, weather, telemetry, messages, objects, bulletins) selects
       from them with its own bitmask

   * - Messaging-capable data type identifier (``=`` / ``@``)
     - ✅ (universal)
     - ✅
     - Picked from *Enable messaging*: ``!``/``/`` when messaging is off,
       ``=``/``@`` when it is on, so receiving clients offer a reply path. The
       Mic-E layout has no identifier to spare and states the same thing with
       its TYPE byte (`` ` `` / ``'``), read from the same tick

Messaging
----------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Specific capability
     - Typical in popular APRS software
     - Here
     - Notes on this project's implementation
   * - Addressed APRS messaging
     - ✅ (universal)
     - ✅
     - RF and/or APRS-IS routing per message
   * - Message acknowledgement (``ackNNN``)
     - ✅
     - ✅
     - Auto-ack on receipt, auto-retry until acked
   * - Reply-ACK (APRS 1.1, ``{MM}AA``)
     - ⚠️ (APRSdos, APRS+SA, Xastir, APRSIS32)
     - ✅
     - Both directions. Outgoing numbers are ``{MM}`` or ``{MM}AA``, the free
       acknowledgement being attached at the instant of transmission so a retry
       carries the latest one owed; an incoming ``AA`` clears the outbound
       message it names, and the sender's ``MM`` becomes what is owed back.
       Numbering is capped at two digits so a full identifier stays inside the
       five characters APRS101 allows
   * - Message retry with configurable count/interval
     - ✅
     - ✅
     - ``msg_retry`` / ``msg_interval``, ticked at 1 Hz
   * - In-app chat/inbox UI
     - ✅ (Xastir, YAAC, APRSIS32)
     - ✅
     - Browser-based ``/msgchat`` page, JSON-polled; one thread of sent and received messages, 5 visible, last 10 kept
   * - Message-received alert (sound/visual/GPIO)
     - ⚠️ (desktop clients: sound/popup)
     - ✅
     - GPIO-driven alert (LED/buzzer) instead of a desktop popup, appropriate for a headless device
   * - Bulk/broadcast messaging to a group
     - ⚠️ (some via bulletins instead)
     - ❌
     - Use Bulletins for broadcast; direct messaging is 1:1 only

Weather
--------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Specific capability
     - Typical in popular APRS software
     - Here
     - Notes on this project's implementation
   * - Own-station APRS Weather Report generation
     - ✅ (Xastir, aprx, many TNC firmwares w/ WX kits)
     - ✅
     - Full ch.12 field set + APRS 1.2 additions (snow, luminosity, flood); snow has no positionless-format encoding, so it is only sent on positioned/object reports
   * - Live sensor polling framework (pluggable drivers)
     - ⚠️ (uncommon as a generic framework; usually hardcoded to one WX board)
     - ✅
     - Dynamic, self-registering ``sensors_local`` driver registry; BMP180 included, extensible
   * - Per-field averaging over the report interval
     - ⚠️
     - ✅
     - Optional per-field "Averaged" checkbox
   * - Receiving/logging other stations' WX reports
     - ✅ (Xastir map overlays, aprs.fi)
     - ⚠️
     - Decoded/gated/digipeated like any packet, but there is no dedicated WX-history display in the web admin

Telemetry
----------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Specific capability
     - Typical in popular APRS software
     - Here
     - Notes on this project's implementation
   * - Own-station telemetry (``T#nnn``) generation
     - ✅ (some TNCs/clients)
     - ✅
     - 5 analog + 8 digital channels
   * - PARM/UNIT/EQNS/BITS metadata messages
     - ⚠️ (often manually configured)
     - ✅
     - Individually toggleable generation
   * - Quadratic calibration (EQNS) per analog channel
     - ⚠️
     - ✅
     - Per-channel a/b/c coefficients transmitted in the ``EQNS.`` message. The
       data report carries the raw sensor reading and the receiver applies the
       conversion — the standard APRS101 split between report and metadata. The
       per-channel raw range bounds the value put on the air, so a probe reading
       past its own scale reports the edge of its declared span instead of a
       figure no receiver can plot; an inverted or empty range declares nothing
       and is ignored
   * - Live sensor mapping per telemetry channel
     - ⚠️ (usually hardcoded, or fed from an external script)
     - ✅
     - Every analog A1-A5 and digital B1-B8 channel picks its source from the
       ``sensors_local`` registry, stored by driver name so enabling or
       disabling a driver never silently re-points a channel at another sensor
   * - Receiving/graphing others' telemetry
     - ✅ (Xastir, aprs.fi graphs)
     - ❌
     - Not implemented — no telemetry-graphing/history view for received data

Objects, Items, Bulletins, Status
------------------------------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Specific capability
     - Typical in popular APRS software
     - Here
     - Notes on this project's implementation
   * - Own-station Objects (timestamped)
     - ✅
     - ✅
     - Up to 5, RF and/or INET, with interval/decay
   * - Own-station Items (non-timestamped)
     - ✅
     - ✅
     - Same 5-slot pool, YAAC-style "permanent" flag chooses Object vs. Item
   * - "Kill" an object/item
     - ✅
     - ✅
     - Transmits kill a few extra times, then auto-disables
   * - Bulletins (``BLN1``-``BLNn``)
     - ✅
     - ✅
     - 5 slots, own text/interval/expiry, ``BLN1``-``BLN5``
   * - Status reports (own-station free text)
     - ✅
     - ✅
     - Per-role free-text status beacon (DTI ``>``, APRS101 ch.16) for tracker,
       IGate and digi, each with its own interval (``*_sts_interval``) and text
       (``*_status``); see ``main/beacon.c``
   * - Query response (``?APRS?``, ``?WX?``, etc.)
     - ⚠️
     - ✅
     - General (``?APRS?``/``?WX?``/``?IGATE?``) and directed queries, each with
       its own rate limiter. Received on the RF/APRS-IS tasks, answered from the
       beacon scheduler task. Each source has its own switch and its answers go
       back on the channel the question arrived on, with the APRS-IS source off
       by default so backbone traffic cannot key the transmitter; see
       :ref:`en-query`
   * - Directed query set (``?APRSD``/``?APRSH``/``?APRSM``/``?APRSO``/
       ``?APRSP``/``?APRSS``/``?APRST``/``?PING?``)
     - ⚠️ (APRSISCE/32, YAAC)
     - ✅
     - Answered when *Extended directed queries* is enabled. List-style answers
       come back as APRS messages to the querying station; ``?APRSO``
       re-announces the Objects/Items later in the same scheduler pass, and
       ``?APRSM`` re-sends at most ``MSG_QUERY_BURST_MAX`` (3) held messages per
       query, leaving the rest to the message retry schedule
   * - ``?APRSH`` heard-history graph
     - ⚠️
     - ✅
     - The station keeps an 18-hour heard histogram per callsign (see
       ``components/lastheard``), so the answer is the ``Hrd: h0 h1 ... h17``
       graph APRS101 ch.15 defines, six counts per period separated by ``.``,
       hour 0 being the current clock hour. The histogram belongs to the
       station's row and travels with it as the row moves to the front of the
       table, and only a received frame ever counts into it — answering the
       query rolls the graph forward to the current hour but leaves the stored
       counts alone, so a station can be asked about as often as one likes
   * - Station Capabilities (``<`` DTI)
     - ✅
     - ✅
     - Emitted as the ``?IGATE?`` response
       (``<IGATE,MSG_CNT=n,LOC_CNT=n>``), where ``MSG_CNT`` is the running count
       of APRS message packets gated in either direction and ``LOC_CNT`` the
       live number of stations currently in the local (RF-heard) list

Mapping / Visualization
--------------------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Specific capability
     - Typical in popular APRS software
     - Here
     - Notes on this project's implementation
   * - Live map of received stations
     - ✅ (Xastir, APRSIS32, YAAC, aprs.fi — central to most clients)
     - ❌
     - Not implemented. The web admin has a Last-Heard table, not a map
   * - Symbol/icon rendering per APRS symbol table
     - ✅
     - ✅
     - Symbol picker exists for configuring own beacons/objects; Last-Heard and the Traffic Log show symbol icons for position reports in both the uncompressed (``!``/``=`` and timestamped ``/``/``@``) and Base-91 compressed formats, and for Object (``;``) and Item (``)``) reports carrying either position layout (see ``aprs_extract_symbol()`` in ``main/aprs_coord.c``)
   * - Track/history playback
     - ✅ (desktop clients)
     - ❌
     - Not implemented
   * - Weather/telemetry graphing over time
     - ✅ (aprs.fi, Xastir plugins)
     - ❌
     - Not implemented

Station Management / Ops
----------------------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Specific capability
     - Typical in popular APRS software
     - Here
     - Notes on this project's implementation
   * - Web-based configuration UI
     - ⚠️ (VP-Digi and some ESP32 projects have one; most desktop clients use native GUIs instead)
     - ✅
     - 17 sidebar pages + symbol picker, HTTP Basic auth, live re-apply for most settings without reboot
   * - Live dashboard (status, counters)
     - ⚠️
     - ✅
     - Network-status pills, statistics panel, live traffic log, last-heard table (JSON long-poll)
   * - Traffic/packet log with raw frame view
     - ✅
     - ✅
     - Direction-tagged (RX/TX/DIGI/INET2RF/RX-IS), includes audio-level RMS
   * - Last-heard station table
     - ✅
     - ✅
     - One row per station rather than per packet, most-recent-first with LRU
       eviction, plus the 18-hour hourly histogram that answers ``?APRSH``.
       Callsigns are stored upper-cased and matched without regard to case, so
       the two feeds that fill the table — raw AX.25 addresses off the air and
       raw TNC2 text off APRS-IS — cannot give one station two rows
   * - Factory reset to compiled-in defaults
     - ⚠️
     - ✅
     - One button on the System page rewrites ``config.json`` with the factory
       defaults
   * - Multi-language UI
     - ⚠️ (rare; most are English-only or OS-localized)
     - ✅
     - EN/ES/IT, compile-time only — no runtime switch
   * - OTA / remote firmware update
     - ⚠️ (rare for embedded TNCs; common for consumer IoT)
     - ✅
     - Dual-partition (``ota_0``/``ota_1``) with automatic rollback on a bad image
   * - Persistent, versioned local config storage
     - ✅
     - ✅
     - LittleFS, atomic writes (``.tmp`` + rename), tolerant of unknown/missing keys
   * - File management (upload/download/browse)
     - ❌ (not applicable to most APRS software; relevant here because it's an embedded FS)
     - ✅
     - Full LittleFS browser (list/download/delete/upload/format)
   * - Wi-Fi AP/STA management with scan, TX power
     - N/A (desktop software doesn't need this)
     - ✅
     - AP/STA/AP+STA, 5 STA profiles, live scan, TX power control
   * - NTP/time sync
     - ⚠️ (desktop OS handles this; relevant for embedded)
     - ✅
     - 3 configurable NTP hosts, pinned to UTC for correct zulu timestamps
   * - CPU/performance tuning
     - N/A for desktop software
     - ✅
     - Runtime 80/160/240 MHz selection
   * - Remote/serial console access for diagnostics
     - ✅ (most TNCs have a serial console)
     - ⚠️
     - No serial console for ordinary operation (by design); diagnostics live in the web dashboard and LOOP TEST instead
   * - Multi-user / role-based access control
     - ⚠️ (rare)
     - ❌
     - Single HTTP Basic-auth username/password, no roles
