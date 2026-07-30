.. _en-filtering:

=========
Filtering
=========

The firmware applies several independent, composable filters to decide which
packets cross between RF and APRS-IS. They live mainly in ``main/aprs_filter.c``
and are used by both the IGate (``components/igate/igate.c``) and the INET→RF
handler (``main/aprs_service.c``). All of them **compose with AND semantics**: a
packet must pass every filter that applies to its direction.

.. important::

   These local filters are entirely separate from the **APRS-IS server-side
   filter string** (``g_config.aprs_filter``), which is free text forwarded
   verbatim in the login line and applied by the APRS-IS server to what it sends
   *into* the client. The local filters below decide what the client pushes
   *out*, and what it re-transmits on RF.

Payload-type classification
===========================

``aprs_filter_classify_tnc2()`` / ``aprs_filter_classify_info()`` decide which
single ``IGATE_FILT_*`` bit a packet belongs to, working on the APRS data type
identifier (DTI) and — where the DTI alone is ambiguous — on the symbol the
report carries (``_`` → weather, ``/N`` → buoy):

.. list-table::
   :header-rows: 1
   :widths: 30 20 50

   * - Type
     - Bit
     - Notes
   * - Message
     - ``1<<0``
     - APRS messages
   * - Status
     - ``1<<1``
     - status reports
   * - Telemetry
     - ``1<<2``
     - telemetry reports
   * - Weather
     - ``1<<3``
     - position report with ``_`` symbol
   * - Object
     - ``1<<4``
     -
   * - Item
     - ``1<<5``
     -
   * - Query
     - ``1<<6``
     -
   * - Buoy
     - ``1<<7``
     - position report with ``/N`` symbol
   * - Position
     - ``1<<8``
     - plain position report

An unclassifiable payload — third-party (``}``) traffic above all — classifies
as ``0``, and ``aprs_filter_pass()`` never lets ``0`` through: unknown means
"do not relay". The same is true of an all-zero mask (every checkbox cleared):
the mask is a **whitelist** of allowed types, exactly as the web checkboxes
read.

Both directions use the same classifier and the same bits
(``g_config.rf2inetFilter`` for RF→INET, ``g_config.inet2rfFilter`` for
INET→RF), so the two can never drift apart.

Local range gate (RF→INET)
==========================

When ``rf2inet_range_en`` is on and ``rf2inet_range_km`` > 0, a packet whose
position decodes to more than that many kilometres from "My Station"
(``my_lat``/``my_lon``) is dropped. Distance is the great-circle
(``aprs_filter_haversine_km()``) between the two points. Packets whose position
cannot be decoded pass this check. ``aprs_filter_decode_position()`` supports
both uncompressed (``DDMM.hhN/DDDMM.hhW``) and compressed base-91 layouts for
the DTIs that carry a position in the info field (``!``/``=``, ``/``/``@``,
``;`` object, ``)`` item). Mic-E reports carry position in the AX.25
destination field and are not decodable from the info field alone.

Local prefix gate (RF→INET)
===========================

When ``rf2inet_prefix_en`` is on, the source callsign must start with one of the
comma-separated prefixes in ``rf2inet_prefixes`` (e.g. ``EA,EB,EC``).
Case-insensitive; whitespace around entries is ignored. A callsign matches if it
starts with any listed prefix (``aprs_filter_prefix_match()``).

Callsign whitelist / blacklist (budlist)
========================================

A shared callsign list (``g_config.budlist[]``, base call, no SSID) with a
**per-direction mode**:

* ``BUDLIST_OFF`` — the callsign filter is disabled for this direction.
* ``BUDLIST_WHITELIST`` — only callsigns in the list are allowed through.
* ``BUDLIST_BLACKLIST`` — callsigns in the list are blocked; everyone else
  passes.

``aprs_filter_budlist_pass()`` compares case-insensitively and strips any
``-SSID`` suffix internally, so both RF (base call only) and INET (may carry
``-SSID``) callers pass their callsign straight through.

Selective third-party unwrap (INET→RF only)
===========================================

Third-party (``}``) payloads classify as ``0`` and never pass by default —
re-gating them without restriction is the number-one cause of IGate loops. When
``inet2rf_3rdparty_unwrap_en`` is on **and** ``inet2rf_budlist_mode ==
BUDLIST_WHITELIST``, ``aprs_filter_classify_thirdparty_inner()`` evaluates the
payload *inside* one level of ``}`` wrapping so the caller can relay it — but
only after verifying the inner packet's source is itself on the whitelist. It is
never a general "relay all third-party" switch.

Server-side filter validation
=============================

``aprs_filter_validate_server_string()`` checks the *grammar* of
``g_config.aprs_filter`` before it is sent: each space-separated term must be
``<letter>/<args>`` with a known filter letter and the right argument shape for
that letter (``r`` needs exactly 3 numeric args, ``p`` needs at least one
prefix, …). It validates structure only, not whether the coordinate/distance
values are sensible.
