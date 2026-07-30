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

Timestamps are UTC
==================

Beacon timestamps are zulu/UTC (``051200z``) per the APRS spec — which is why
``time_sync.c`` pins the system clock to ``TZ=UTC0`` regardless of
``g_config.timeZone`` (the configured zone is display-only).
