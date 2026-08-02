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
     - Selectable modem profile #2
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
     - Full soft-modem TX/RX, in software, on ADC/DAC
   * - FX.25 Reed-Solomon FEC
     - ⚠️ (Direwolf yes; most hardware TNCs no)
     - ✅
     - RX-only or RX+TX modes, backward compatible with plain AX.25
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
     - TX time-slot (``tx_timeslot``) + preamble/TXDelay control
   * - PTT keying (VOX-free, hardware GPIO)
     - ✅
     - ✅
     - Compile-time GPIO + polarity; runtime-adjustable minimum unkey hold
   * - Built-in RF loopback/self-test tool
     - ⚠️ (rare)
     - ✅
     - "LOOP TEST" — transmits a token packet and verifies the full RX chain decodes it back, with detailed stage-by-stage diagnostics

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
     - 10-entry / 30 s cache, shared with the digipeater
   * - ``qAR``/``qAO`` Q-construct insertion
     - ✅
     - ✅
     - Standard ``qAR``; satellite-gate ``qAO`` form supported
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
     - ✅
     - TCP auto-reconnect, re-reads config on every reconnect
   * - Passcode-based APRS-IS login
     - ✅
     - ✅
     - Standard ``user/pass/vers/filter`` login line; server verified/unverified response surfaced
   * - Multiple/failover APRS-IS servers
     - ⚠️ (some support server lists)
     - ❌
     - Single configured host/port only
   * - Per-drop-reason statistics
     - ⚠️ (uncommon, usually just totals)
     - ✅
     - Named counters (``DROP_TOO_SHORT``, ``DROP_PATH_TOKEN``, ``DROP_RANGE_FILTER``, etc.)

Digipeater
-----------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Specific capability
     - Typical in popular APRS software
     - Here
     - Notes on this project's implementation
   * - WIDEn-N flood digipeating
     - ✅ (universal)
     - ✅
     - Hop-count decrement + own-call insertion
   * - TRACEn-N explicit-trace digipeating
     - ✅
     - ✅
     - Every hop inserts its callsign
   * - RELAY / ECHO / GATE legacy aliases
     - ✅
     - ✅
     - All substituted with digi's own callsign
   * - Destination-SSID-encoded hop count (legacy)
     - ⚠️ (older TNCs)
     - ✅
     - Recognised and handled
   * - Digipeat duplicate/ping-pong suppression
     - ✅
     - ✅
     - Own 30 s window in the shared dedup cache (``DUP_SCOPE_DIGI``), keyed on
       source and payload only, tested before any path work
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
     - Tracker page offers a compressed-position option; decoder also understands it
   * - Mic-E position encoding (TX)
     - ⚠️ (mostly mobile-tracker firmware)
     - ✅
     - Tracker beacon page offers a Mic-E option (``aprs_mice_encode()``); fixed-position only, so course/speed is always sent as "unknown" and the message code is fixed at Off Duty
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
     - Station-wide altitude field, used by beacons

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
   * - Message retry with configurable count/interval
     - ✅
     - ✅
     - ``msg_retry`` / ``msg_interval``, ticked at 1 Hz
   * - In-app chat/inbox UI
     - ✅ (Xastir, YAAC, APRSIS32)
     - ✅
     - Browser-based ``/msgchat`` page, JSON-polled
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
     - Full ch.12 field set + APRS 1.2 additions (snow, luminosity, flood)
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
     - ``value = a*x^2 + b*x + c`` per channel
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
       its own rate limiter; see the ``query`` component and the web admin's
       *Query* page
   * - Directed query set (``?APRSD``/``?APRSH``/``?APRSM``/``?APRSO``/
       ``?APRSP``/``?APRSS``/``?APRST``/``?PING?``)
     - ⚠️ (APRSISCE/32, YAAC)
     - ✅
     - Answered when *Extended directed queries* is enabled. List-style answers
       come back as APRS messages to the querying station; ``?APRSO`` queues the
       Objects/Items for the beacon scheduler rather than transmitting from the
       RX task
   * - ``?APRSH`` heard-history graph
     - ⚠️
     - ✅
     - The station keeps an 18-hour heard histogram per callsign (see
       ``components/lastheard``), so the answer is the ``Hrd: h0 h1 ... h17``
       graph APRS101 ch.15 defines, six counts per period separated by ``.``,
       hour 0 being the current clock hour
   * - Station Capabilities (``<`` DTI)
     - ✅
     - ✅
     - Emitted as the ``?IGATE?`` response
       (``<IGATE,MSG_CNT=n,LOC_CNT=n>``)

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
     - ~30 pages, HTTP Basic auth, live re-apply for most settings without reboot
   * - Live dashboard (status, counters)
     - ⚠️
     - ✅
     - Network-status pills, statistics panel, live traffic log, last-heard table (JSON long-poll)
   * - Traffic/packet log with raw frame view
     - ✅
     - ✅
     - Direction-tagged (RX/TX/DIGI/INET2RF/RX-IS), includes audio-level RMS
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
