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
   * - Other
     - ``1<<9``
     - Station capabilities, user-defined formats, Agrelo direction finding,
       Maidenhead locator beacons and the reserved map feature — the payload
       kinds with no bit of their own, gathered here so they are gateable
       instead of silently dropped.

An unclassifiable payload — third-party (``}``) traffic above all — classifies
as ``0``, and ``aprs_filter_pass()`` never lets ``0`` through: unknown means
"do not relay". The same is true of an all-zero mask (every checkbox cleared):
the mask is a **whitelist** of allowed types, exactly as the web checkboxes
read.

Both directions use the same classifier and the same bits
(``g_config.rf2inetFilter`` for RF→INET, ``g_config.inet2rfFilter`` for
INET→RF), so the two can never drift apart.

Generic query gate (mandatory, both directions)
================================================

A payload whose first byte is ``?`` — a generic query such as ``?APRS?``,
``?WX?`` or ``?IGATE?`` — is never gated, in either direction
(``DROP_GENERIC_QUERY``). This check runs before the payload-type filter and
is **not** one of the composable ``IGATE_FILT_*`` bits: it cannot be turned
off, and no state of ``rf2inetFilter``/``inet2rfFilter`` lets a generic query
through. Relaying one would let a single RF station trigger a query-responder
reply from every APRS-IS-connected station that implements one, crediting
this station's callsign for the resulting flood via the ``qAR`` construct —
the same is true in reverse for a generic query relayed onto RF.

A **directed** query (``:CALLSIGN :?APRSD``, data type identifier ``:``) does
not start with ``?`` and is unaffected by this gate; it classifies as
``IGATE_FILT_MESSAGE`` and is subject only to the ordinary payload-type
filter below, same as any other message.

``IGATE_FILT_QUERY`` itself still exists as an ``aprs_filter_classify_info()``
/ ``aprs_filter_classify_tnc2()`` output and in ``aprs_filter_type_name()``,
for the local query responder's own accounting — but no web checkbox maps to
it, since a query that reaches the type filter has, by construction, already
survived the mandatory gate above.

Local range gate (RF→INET)
==========================

When ``rf2inet_range_en`` is on and ``rf2inet_range_km`` > 0, a packet whose
position decodes to more than that many kilometres from "My Station"
(``my_lat``/``my_lon``) is dropped. Distance is the great-circle
(``aprs_filter_haversine_km()``) between the two points. Packets whose position
cannot be decoded pass this check. ``aprs_filter_decode_position()`` supports
uncompressed (``DDMM.hhN/DDDMM.hhW``) and compressed base-91 layouts for the
DTIs that carry a position in the info field alone (``!``/``=``, ``/``/``@``,
``;`` object, ``)`` item), and Mic-E reports (`` ` ``/``'``/0x1c/0x1d), whose
position is split between the info field and the AX.25 destination address
field and is reassembled by ``aprs_mice_decode()`` before the same range
check is applied.

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


Traffic log display gate
========================

``igate_log_after_filters`` (*Log after filters* on the IGate page, off by
default) reuses the filters above as a **display** gate for the web traffic
table and the matching serial console lines: while it is on, an ``RX`` entry is
emitted only for a frame
``igate_log_accepts_frame()`` accepts (Satellite Gate List, ``rf2inetFilter``,
the RF→INET range and prefix gates, the RF→INET callsign filter) and an
``RX-IS`` entry only for a line ``igate_log_accepts_line()`` accepts
(the associated-position exception, the INET→RF range gate, the
``inet2rfFilter`` mask including the selective third-party unwrap, the INET→RF
callsign filter). The RF side shares its implementation with the gating path;
the INET→RF side applies the same checks in the same order as
``inet2rfHandler()``, follow-up positions included, so the log and the gateway
cannot disagree. Nothing about gating,
digipeating or transmitting changes and no drop counter moves: a frame left out
of the log is still handled exactly as before.
