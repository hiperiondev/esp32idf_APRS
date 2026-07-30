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

Before repeating, the digipeater checks the frame against the same
duplicate-detection cache the IGate uses (``isDuplicatePacket()``), so a frame
already digipeated within the suppression window is not re-transmitted — the
classic defence against digipeater ping-pong.

Counters
========

``digi_get_stats()`` returns a ``digi_stats_t``:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Counter
     - Meaning
   * - ``rxPkts``
     - Packets seen by the digipeater.
   * - ``txPkts``
     - Packets digipeated (path modified, ``digiProcess()`` returned ``2``).
   * - ``dropRx``
     - Packets dropped (duplicate, filtered path, not for us, already relayed).
   * - ``erPkts``
     - Malformed packets (too short / no path).

.. note::

   These per-feature counters only move while ``digi_en`` is on. The dashboard's
   headline ``digi`` counter is tracked separately in ``aprs_service.c`` at the
   point the rewritten frame is actually transmitted, so it reflects reality
   whether or not other features are enabled.
