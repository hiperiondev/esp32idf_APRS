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

Supported path schemes
======================

* **WIDEn-N** — the standard flooding alias. The hop count *N* is decremented
  and the digipeater's own callsign inserted (marked used with ``*``) when the
  alias is consumed.
* **TRACEn-N** — like WIDEn-N but every hop inserts its callsign, building an
  explicit trace of the route taken.
* **RELAY / GATE / ECHO** — the legacy generic aliases, each substituted with
  the digipeater's callsign.
* **WIDEn-N encoded in the destination SSID field** — the older convention
  where the hop count lives in the AX.25 destination address's SSID nibble is
  also recognised and handled.

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
