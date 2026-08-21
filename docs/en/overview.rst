:orphan:

.. _en-overview:

========
Overview
========

What this is
============

``esp32idf_APRS`` is an ESP-IDF **v6.x** project (tested and locked at IDF
**6.0.2**) that turns a bare ESP32 DevKit plus a cheap audio interface into a
complete, self-contained APRS station. Everything runs on the ESP32 itself —
there is no Arduino core, no ``String``, no PlatformIO, and no external DSP
library. The whole signal chain, from the correlator demodulator through the
DPLL bit recovery, NRZI, HDLC framer, AX.25 codec and Reed–Solomon FX.25
forward-error-correction, executes on the microcontroller using only the
SAR-ADC in continuous/DMA mode, the DAC, and a general-purpose timer.

In one sentence: the firmware

* **demodulates** AFSK/FSK audio from a radio's speaker or discriminator
  output on **ADC1**,
* **decodes** HDLC/AX.25 (optionally FX.25 forward-error-corrected) frames,
* **gates** them to APRS-IS over Wi-Fi (``qAR``/``qAO``),
* **digipeats** them back on RF (WIDEn-N, via an operator-configurable alias
  table),
* **beacons** its own position, weather and telemetry,
* **modulates** and transmits frames back out through the ESP32's **8-bit
  DAC**, keying the radio via a PTT GPIO,
* and is configured entirely through an **HTTP web admin** served by the
  device itself — no serial console, no recompilation for ordinary settings.

Feature matrix
==============

.. list-table::
   :header-rows: 1
   :widths: 40 12 48

   * - Area
     - Status
     - Notes
   * - AFSK 1200 Bd Bell 202 (standard APRS)
     - ✅
     - dual demodulator, default profile
   * - AFSK 1200 Bd ITU V.23 (1300/2100 Hz)
     - ✅
     -
   * - AFSK 300 Bd (1600/1800 Hz)
     - ✅
     - HF-style
   * - G3RUH FSK 9600 Bd
     - ✅
     - needs flat/discriminator audio
   * - HDLC / AX.25 UI frame RX+TX
     - ✅
     - ``AX25_FRAME_MAX_SIZE = 329``
   * - FX.25 (RS FEC over AX.25)
     - ✅
     - RX-only / RX+TX modes
   * - PTT keying (compile-time GPIO + polarity)
     - ✅
     - validated GPIO; runtime **minimum-unkey** hold time
   * - CSMA / TX time-slot / p-persistence / TXDelay preamble
     - ✅
     - ``preamble``, ``tx_timeslot``, ``csma_persist``
   * - DCD (data carrier detect)
     - ✅
     - demodulator-derived; no hardware squelch input
   * - APRS-IS IGate RF→INET
     - ✅
     - filters, dedup, ``qAR``/``qAO``
   * - APRS-IS IGate INET→RF
     - ✅
     - payload-type gating + budlist + third-party unwrap opt-in
   * - Local RF→INET range gate & prefix gate
     - ✅
     - haversine distance + callsign-prefix whitelist
   * - Callsign whitelist / blacklist (budlist)
     - ✅
     - per-direction, composes (AND) with the type filters
   * - Satellite/ISS digipeater gate-call list
     - ✅
     - up to 8 entries, web-configurable (IGate page), no rebuild needed
   * - Duplicate-suppression cache size & window
     - ✅
     - web-configurable (IGate page), shared by IGate and Digipeater
   * - Digipeater
     - ✅
     - WIDEn-N via a configurable alias table, dup-suppression; legacy
       ``RELAY``/``ECHO``/``GATE``/``TRACEn-N`` aliases are not built in but
       can be added as ordinary alias rows
   * - Own-station APRS Objects / Items
     - ✅
     - up to 5, RF and/or INET, interval decay + kill-repeats
   * - APRS bulletins (BLN1..BLN5)
     - ✅
     - up to 5, RF and/or INET, per-bulletin expiry
   * - APRS message chat UI (``/msgchat``)
     - ✅
     - inbox/compose page over the messaging engine
   * - APRS query responder (APRS101 ch.15)
     - ✅
     - general ``?APRS?``/``?WX?``/``?IGATE?`` + the directed set
       (``?APRSD``/``?APRSH``/``?APRSM``/``?APRSO``/``?APRSP``/``?APRSS``/
       ``?APRST``/``?PING?``), per-type and per-source rate limits
   * - Fixed-position beacons (tracker / igate / digi)
     - ✅
     - one shared beacon-scheduler task
   * - APRS messaging + ack/retry
     - ✅
     - RF and/or INET
   * - Web admin (HTTP Basic auth)
     - ✅
     - 18 sidebar pages + symbol picker, live dashboard
   * - Live traffic log + last-heard table
     - ✅
     - JSON long-poll (``?since=<seq>``)
   * - LittleFS storage, upload/download/delete/format
     - ✅
     - 512 KB partition
   * - SNTP time sync (3 hosts)
     - ✅
     - clock always kept in UTC
   * - CPU frequency control (80/160/240 MHz)
     - ✅
     - ``esp_pm_configure()``
   * - Wi-Fi AP / STA / AP+STA, scan, TX power
     - ✅
     - 5 STA slots (first enabled one is used)
   * - Localization (EN / ES / IT)
     - ✅
     - compile-time, one language per image
   * - OTA update
     - ✅
     - ``ota_0``/``ota_1`` slots, auto-rollback on boot failure
   * - Own-station APRS Weather Report
     - ✅
     - 1 Hz sensor refresh, optional averaging, on-air WX beacon
   * - Local sensor driver framework (``sensors_local``)
     - ✅
     - dynamic run-time registry, auto-registering drivers
   * - APRS Telemetry on-air encode/beacon
     - ✅
     - analog A1–A5 + digital B1–B8, ``T#nnn`` report + metadata

Design philosophy
=================

Several deliberate architectural decisions recur throughout the codebase and
are worth internalising up front:

**One resident configuration, one live copy.**
   A single ``app_config_t g_config`` instance is the source of truth every
   subsystem reads. It persists to ``/storage/config.json``. Subsystems never
   duplicate configuration state; they read ``g_config`` directly. Two
   subsystems that need larger, page-specific state of their own keep it in
   separate LittleFS files instead of bloating ``g_config``: telemetry
   (``/storage/telemetry.json``), bulletins (``/storage/bulletins.json``) and
   objects/items (``/storage/objitems.json``).

**Compile-time board wiring, runtime everything-else.**
   The three audio pins (ADC, DAC, PTT), the PTT polarity, the ADC
   attenuation and the sample rates are *compile-time constants* set in the
   top-level ``CMakeLists.txt``. They are physical wiring choices, so they are
   not exposed on the web admin. Everything an operator legitimately tunes
   without rewiring — modulation profile, preamble, time-slot, FX.25 mode,
   filters, callsigns, intervals — is runtime-editable and, in most cases,
   applied live without a reboot.

**Statistics that reflect reality, not configuration.**
   The dashboard counters (RF RX/TX, RF→INET, INET→RF, digi, drop, error) are
   tracked at the points where frames actually flow, independent of whether the
   IGate or digipeater features are enabled — so a pure RX-only monitor setup
   still shows real decode activity instead of a wall of zeros.

**Fail loud, fail safe.**
   The Wi-Fi bring-up path is heavily instrumented: disconnect reason codes are
   logged, an STA-only device with nothing to join falls back to AP+STA so the
   web admin stays reachable, and reconnects use a growing back-off armed on a
   timer rather than a blocking delay inside the event loop.

Lineage and credits
===================

The project and its modem component are by **Emiliano Augusto González
(LU3VEA)**. The soft-modem's DSP lineage traces to three earlier projects:
**VP-Digi** (SQ8VPS), **ESP32APRS_Audio** (nakhonthai) and **LibAPRS**
(Mark Qvist). The configuration schema, web-admin layout and dashboard
semantics follow the reference **ESP32APRS** project so that existing
``config.json`` files and operator expectations carry over. See
:ref:`en-credits` for full attribution and licensing.

The firmware is licensed under the **GNU General Public License v3.0**.

.. warning::

   **Amateur radio disclaimer.** Transmitting on amateur radio frequencies
   requires a valid licence for your country and band. Set a real callsign —
   the default is ``NOCALL`` — use a legitimate APRS-IS passcode, respect your
   local band plan and digipeating conventions (``WIDE1-1,WIDE2-1`` is *not*
   always appropriate), and do not gate ``NOGATE``/``RFONLY`` traffic. You are
   responsible for everything this device transmits.

How this compares to popular APRS software
============================================

APRS is normally assembled from separate pieces of software, each covering
one part of the job: a soundcard TNC/modem, a client with a map and a
messaging UI, and sometimes a dedicated digipeater or IGate program running
on a PC or single-board computer. The table below lines up the most widely
used packages against ``esp32idf_APRS`` feature by feature, to make clear
what a single ESP32 running this firmware does and does not replace.

The comparison covers **Direwolf** (WB2OSZ — the de-facto standard
software soundcard TNC/modem, digipeater and IGate for Linux/Windows/macOS),
**Xastir** (a mature, highly configurable X11/Linux desktop client with
extensive mapping), **YAAC** ("Yet Another APRS Client", KA2DDO — a
cross-platform Java client with a modern UI), **APRSIS32 / UI-View**
(Windows-only desktop clients, historically dominant, UI-View now
unmaintained/legacy), and **APRSdroid** (the common Android mobile client).
Most of these are commonly paired together — e.g. Direwolf as the modem/TNC
feeding Xastir or YAAC as the client — rather than used entirely alone;
``esp32idf_APRS`` is unusual in providing the equivalent of the modem, the
gateway/digipeater logic *and* the operator interface in one single-board
firmware image.

.. list-table::
   :header-rows: 1
   :widths: 26 15 12 12 12 12 27

   * - Feature
     - esp32idf_APRS
     - Direwolf
     - Xastir
     - YAAC
     - APRSIS32 / UI-View
     - Notes
   * - Runs standalone, no host PC
     - ✅
     - ❌ (needs a host OS)
     - ❌
     - ❌
     - ❌
     - This project's core differentiator: modem + logic + UI on one MCU.
   * - Software AFSK/FSK soundcard modem
     - ✅ (on-chip ADC/DAC)
     - ✅ (PC sound card)
     - ➖ (usually via Direwolf)
     - ➖ (usually via Direwolf/AGW)
     - ➖ (via TNC or AGW)
     - Only Direwolf and this project *are* the modem; the others consume one.
   * - Hardware TNC / KISS support
     - ❌
     - ✅
     - ✅
     - ✅
     - ✅
     - This firmware is its own modem; it does not speak to an external TNC.
   * - AX.25 UI frame RX/TX
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - Baseline for all APRS software.
   * - FX.25 (Reed–Solomon FEC)
     - ✅
     - ✅
     - ❌
     - ➖ (client-side only)
     - ❌
     - Direwolf and this project both encode/decode FX.25 directly.
   * - IGate (RF → APRS-IS)
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - Common to nearly every APRS package.
   * - IGate (APRS-IS → RF, "two-way")
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - Usually gated by local/duplicate/type filters everywhere.
   * - Digipeater (WIDEn-N / TRACEn-N)
     - ✅
     - ✅
     - ✅
     - ✅
     - ➖ (limited)
     - 
   * - Built-in web admin UI
     - ✅
     - ❌ (config file + optional 3rd-party web UIs)
     - ❌ (native X11 GUI)
     - ❌ (native Java Swing GUI)
     - ❌ (native Windows GUI)
     - This firmware is the only one configured purely through a browser.
   * - Live map display
     - ❌
     - ❌ (text/log only)
     - ✅ (extensive)
     - ✅
     - ✅
     - Deliberately out of scope — this is a station, not a mapping client.
   * - GPS-based live tracking
     - ❌ (fixed-position only)
     - ➖ (via connected GPS/tracker)
     - ✅
     - ✅
     - ✅
     - Noted as a known limitation; see :ref:`en-limitations`.
   * - Position beaconing (fixed station)
     - ✅
     - ✅ (as config'd)
     - ✅
     - ✅
     - ✅
     - 
   * - APRS messaging (chat, ack/retry)
     - ✅ (web chat UI)
     - ➖ (via connected client)
     - ✅
     - ✅
     - ✅
     - 
   * - Bulletins / announcements
     - ✅
     - ➖ (relays, doesn't compose)
     - ✅
     - ✅
     - ✅
     - 
   * - Objects / items
     - ✅ (up to 5, own-station)
     - ➖ (relays, doesn't compose)
     - ✅
     - ✅
     - ✅
     - 
   * - Weather station reporting
     - ✅ (native sensor framework)
     - ➖ (via external WX software)
     - ✅ (via external feed)
     - ✅ (via external feed)
     - ✅ (via external feed)
     - This firmware reads sensors and encodes WX reports itself, on-chip.
   * - Telemetry (analog/digital channels)
     - ✅ (A1–A5, B1–B8, EQNS/UNIT/BITS)
     - ❌
     - ➖ (display only)
     - ➖ (display only)
     - ➖ (display only)
     - 
   * - APRStt (DTMF-to-APRS gateway)
     - ❌
     - ✅
     - ❌
     - ❌
     - ❌
     - Not implemented in this project.
   * - OTA firmware / software update
     - ✅ (dual OTA slot, auto-rollback)
     - ➖ (OS package manager)
     - ➖ (OS package manager)
     - ➖ (OS package manager)
     - ➖ (manual installer)
     - "OTA" here is specific to the embedded firmware-update model.
   * - Multi-language UI
     - ✅ (EN/ES/IT, compile-time)
     - ❌ (English only)
     - ➖ (partial translations)
     - ❌ (English only)
     - ❌ (English only)
     - 
   * - Cost / hardware footprint
     - Single ~US$5–10 ESP32 board + audio interface
     - PC/RPi + sound card + radio
     - PC/RPi + TNC + radio
     - PC/phone + TNC + radio
     - Windows PC + TNC + radio
     - 

Legend: ✅ implemented / native · ➖ partial, or available only via another
program in the chain · ❌ not implemented / not applicable.

**What this project deliberately does implement**, matching the core of what
a full desktop APRS station provides: the modem itself, AX.25/FX.25 framing,
two-way IGate, digipeating, beacons, messaging, bulletins,
objects, weather and telemetry, all reachable from a self-hosted web UI with
no companion PC software.

**What this project deliberately does not implement**: it has no map
display and no GPS-based mobile tracking (fixed-position beacons only, see
:ref:`en-limitations`), and it does not include an APRStt DTMF-to-APRS
gateway. These are intentionally out of scope for a headless, browser
configured embedded station — a companion mapping client such as YAAC,
Xastir or `aprs.fi <https://aprs.fi>`__ remains the natural way to
*visualise* the traffic this firmware generates and relays.
