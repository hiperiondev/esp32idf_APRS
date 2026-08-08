.. _en-digipeater:

==========
Digipeater
==========

The ``digirepeater`` component (``components/digirepeater/``) implements APRS
path-rewrite digipeating. Its single entry point, ``digiProcess()``, is called
from the RX dispatch when ``g_config.digi_en`` is on. It reads the
digipeater's callsign/SSID from ``g_config.digi_mycall`` / ``digi_ssid``, so the
web admin's *Digi* page is the single source of truth.

The rewrite contract
====================

``digiProcess(ax25_msg_t *packet)`` rewrites the path **in place** and returns
one of three values:

.. list-table::
   :header-rows: 1
   :widths: 12 88

   * - Return
     - Meaning
   * - ``0``
     - Do not repeat (drop / not for us / already relayed / malformed).
   * - ``1``
     - Repeat as-is — the path already carries our used call (e.g. a bypass
       ``*``); the caller re-transmits the frame unchanged.
   * - ``2``
     - Repeat with a modified path — the caller re-encodes the rewritten header
       and transmits it on RF.

When ``digiProcess()`` returns ``2``, the dispatch in ``aprs_service.c``
re-renders the frame to TNC2, calls ``aprs_service_send_tnc2()`` and, on
success, bumps the ``digi`` dashboard counter and logs a ``DIGI`` traffic
entry.

The alias table
===============

The digipeater recognises no aliases of its own. Every alias it honours is a
row of ``g_config.digi_alias``, edited under *n-N Path Aliases* on the *Digi*
page, and the row says how that alias is repeated. This is what makes the New
n-N Paradigm's local conventions — a fill-in ``WIDE1-1``, a two-hop
``WIDE2-2``, a regional ``SSn-N`` — an operator setting rather than a firmware
constant.

.. list-table::
   :header-rows: 1
   :widths: 16 12 72

   * - Field
     - Range
     - Meaning
   * - Alias
     - 6 chars
     - The repeater callsign **without** its SSID; the SSID is the hop count
       *N* and is handled separately. ``#`` matches exactly one decimal digit,
       so one row covers a family: ``WIDE#`` claims ``WIDE1`` through
       ``WIDE9``, but never ``WIDE``, ``WIDEN`` or ``WIDE12``, because the
       match also requires equal length. An empty alias disables the row.
   * - Max N
     - 1–7
     - The largest hop count honoured for this alias. A larger *N* received on
       air is *trapped*.
   * - Mode
     - Off / Trace / Flood
     - **Trace** inserts this station's callsign ahead of the remaining alias
       and marks it used, so every hop of the path can be attributed
       afterwards. **Flood** decrements the hop count and leaves no record of
       who did it. **Off** ignores the row entirely.

Rows are consulted in table order and the first match wins, so a specific
alias placed above a wildcard row keeps its own hop limit. The factory table
is ``WIDE1`` (1 hop), ``WIDE2`` (2 hops) and ``WIDE#`` (2 hops), all tracing,
with the fourth row free for a regional alias.

``WIDEn-N`` is required to trace. The paradigm moved it from the untraceable
flooding mechanism onto the tracing one precisely so that every hop of every
``WIDEn-N`` path is identifiable — which is why *Flood* is only appropriate
for a regional alias an operator deliberately runs untraced.

``RELAY``, ``GATE``, ``ECHO`` and ``TRACEn-N`` are not built in: they were
abandoned as paths and are gone from the firmware. An operator who still needs
one for a legacy neighbour adds it as an ordinary row.

Trapping and the fill-in role
=============================

A hop count above the matched row's *Max N* is trapped, and *Hop count above
Max N* chooses how: **Clamp to Max N** (the default) brings it down to the
limit and repeats the frame, **Drop the frame** refuses it outright and counts
``DROP_DIGI_N_TRAPPED``. Clamping keeps the frame moving while still stopping
it from flooding further than local conditions allow, which is why it is the
default; each additional hop multiplies the load a frame puts on the network by
roughly three.

*Fill-in digipeater (single hop only)* restricts the station to rows whose hop
limit is 1. That is the whole of the fill-in role: it lifts traffic from
neighbours who cannot reach the backbone directly and leaves everything routed
for more hops to the wide digipeaters, which is what stops a home station in a
valley from adding a redundant copy of every packet in the region.

Two frames are refused before the table is consulted at all: one that already
carries this station's callsign marked used, whatever its path still holds, and
one matching the duplicate-suppression window below.

Legacy destination-SSID routing
===============================

Before the New n-N Paradigm, some TNCs carried the hop count in the SSID nibble
of the AX.25 destination address rather than in the path. *Digipeat by
destination SSID (legacy)* on the *Digi* page (``digi_dest_ssid_en``) enables
that convention, and it is **off by default**.

Switched on, a frame whose destination SSID is 1 to 7 is repeated on the
strength of that SSID alone: the count is decremented and this station's
callsign is inserted into the path marked used, so the hop can still be
attributed afterwards. That decision is taken *ahead of* the alias table, which
is exactly why it is not the default — a frame that carries both a destination
SSID and an explicit ``WIDEn-N`` path would be repeated on the SSID and the
path the originating station asked for would never be read.

Switched off, or when the convention does not route a particular frame (a
destination SSID of 0, or of 8 to 15, which belongs to the destination address
itself; a path already carrying this station's callsign marked used; a path
already full), the frame reaches the alias table exactly as it was received,
destination SSID included.

Duplicate suppression
=====================

Before any path work is done, the digipeater checks the frame with
``isDuplicatePacketScoped(packet, DUP_SCOPE_DIGI)``. The key is built from the
source address and the information field only — never from the path — so every
copy of one transmission hashes the same however it arrived. A frame matching
one repeated within ``g_config.dup_cache_timeout_ms`` (default 30 s, editable on
the *IGate* page) is dropped, which is what stops two digipeaters inside each
other's coverage from bouncing a frame back and forth, and what absorbs an RF
echo of a frame this station just repeated.

The cache is shared with the IGate but the windows are not: entries carry the
scope that inserted them and only match lookups from that same scope. Both
consumers see the same frames from the same RX dispatch, and the digipeater
runs first, so a single shared window would let it consume every frame and make
the IGate treat all of them as duplicates.

Counters
========

The digipeater keeps no counters of its own. Everything an operator can see
about it comes from two places that both move regardless of whether
``digi_en`` or ``igate_en`` is on:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Figure
     - Where it comes from
   * - Headline ``digi`` count
     - ``aprs_service.c``, incremented at the point the rewritten frame is
       actually transmitted. It only moves while ``digi_en`` is on, because
       there is nothing to digipeat with it off.
   * - Every drop and malformed frame
     - ``igate_note_drop(DROP_DIGI_…)``, which feeds the per-reason table the
       dashboard renders as *Drop Breakdown*. Each reason is a separate row,
       so a duplicate, a full path and a placeholder callsign are told apart
       instead of being merged into one total.

.. note::

   The ``DROP_DIGI_*`` reasons are counted inside ``digiProcess()``, so they
   only move while the digipeater is running. Frames discarded before dispatch,
   or on the way out to RF, are counted at the service level in
   ``aprs_service.c`` and appear whether or not any feature is enabled.
