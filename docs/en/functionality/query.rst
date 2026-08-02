.. _en-query:

====================
APRS Query Responder
====================

The ``query`` component (``components/query/``) answers the APRS queries defined
in APRS101 chapter 15. It recognises the **general** (broadcast) queries carried
in ordinary received traffic and the **directed** queries addressed to this
station as APRS messages, builds the matching response and hands it to the same
RF/APRS-IS TX plumbing the messaging engine uses. Everything it does is gated by
``g_config.query_en`` and the rest of the *Query* web-admin page.

Two entry points
================

* ``query_process(tnc2Line)`` — fed every decoded TNC2 line. It no-ops unless
  ``query_en`` is set and the information field starts with ``?``, which is what
  makes a line a **general** query.
* ``query_process_directed(fromCall, toCall, text, tnc2Line)`` — called by
  ``message.c``'s ``handleIncomingAPRS()`` when an addressed message's text
  starts with ``?``, so the ``:ADDRESSEE:`` parsing is not duplicated here. It
  no-ops unless ``query_en`` **and** ``query_directed_en`` are set and
  ``toCall`` matches ``g_config.aprs_mycall`` (base callsign, SSID-insensitive).

Responses are transmitted through the handler installed with
``query_set_tx_handler()``. ``aprs_service.c`` reuses the very same
``messageTxHandler()`` it gives the messaging engine, so the routing bits are the
familiar ``MSG_CHANNEL_RF`` / ``MSG_CHANNEL_INET`` pair, selected by
``g_config.query_rf`` / ``query_inet``.

General queries
===============

.. list-table::
   :header-rows: 1
   :widths: 16 16 68

   * - Query
     - Enable
     - Response
   * - ``?APRS?``
     - ``query_aprs_en``
     - This station's own position report, built with
       ``beacon_build_igate_position_packet()`` — byte-for-byte what the IGate
       position beacon would have sent.
   * - ``?WX?``
     - ``query_wx_en``
     - The latest cached Weather Report, built with
       ``weather_build_report_packet()``. Not answered if no reading has been
       cached yet or no WX/APRS callsign is configured.
   * - ``?IGATE?``
     - ``query_igate_en``
     - The Station Capabilities line APRS101 defines,
       ``<IGATE,MSG_CNT=n,LOC_CNT=n>``, built from the same ``igate_get_stats()``
       counters the dashboard reads (``MSG_CNT`` = ``txCount``, ``LOC_CNT`` =
       ``rxCount``). Silently ignored while ``igate_en`` is off.

Because position, status and weather answers reuse the existing beacon builders,
a reply can never drift from what the periodic beacons transmit.

Directed queries
================

Answered only with ``query_directed_en`` on. ``?APRSP`` and ``?APRSS`` are always
available in that set; the remaining, list-style ones additionally require
*Extended directed queries* (``query_ext_en``).

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Query
     - Response
   * - ``?APRSP``
     - Position report (same builder as ``?APRS?``).
   * - ``?APRSS``
     - Status report, byte-for-byte what the IGate status beacon would send —
       including the Maidenhead locator block when that Station-page option is
       on.
   * - ``?APRSD``
     - The stations heard **direct** (not via a digipeater), as an APRS text
       message back to the asker. Whole callsigns are dropped rather than
       truncated when the list would exceed the on-air message length.
   * - ``?APRSH <call>``
     - The 18-hour heard graph for one station: ``Hrd: h0 h1 … h17``, six counts
       per period separated by ``.``, hour 0 being the current clock hour. The
       histogram itself lives in ``components/lastheard`` (see
       ``lastheard_heard_history()`` and ``LASTHEARD_HEARD_HOURS``). Without a
       callsign argument the responder says so.
   * - ``?APRSM``
     - Re-sends this station's pending messages for the querying operator.
   * - ``?APRSO``
     - Re-announces the Objects/Items originated here. It calls
       ``objitems_request_transmit_all()``, so the elements go out from the
       **beacon scheduler** task rather than from the RX task — answering a query
       never occupies RX for the length of a transmission burst. Built without
       ``ENABLE_OBJECTS_ITEMS``, it replies ``No objects``.
   * - ``?APRST`` / ``?PING?``
     - The route the query itself took, reconstructed from the received TNC2
       line. With no line available it answers with an unknown route.

Answers of the list kind go out as APRS text messages addressed back to the
querying station, with the fixed 9-character space-padded addressee field and
**no** message number — a query answer is informational, so no ack is solicited.
The destination call used for query traffic is ``APE32L``, and the path is the
IGate page's own path bitmask.

Rate limiting
=============

Two independent limiters keep the responder from becoming an airtime problem or
half of a feedback loop with another auto-responder:

* **Broadcast limiter** — per query *type*, at most one answer per
  ``g_config.query_min_interval_sec`` (default 30 s; the *Query* page's minimum
  is 5 s). Because it is per type, a busy channel asking ``?APRS?`` cannot
  suppress a ``?WX?`` answer.
* **Directed limiter** — directed queries bypass the broadcast limiter (they are
  explicitly addressed to this station) but get their own, tighter **per-source**
  limit of ``QUERY_DIRECTED_MIN_INTERVAL_SEC`` (5 s), tracked in a fixed
  ``QUERY_DIRECTED_TRACK_MAX`` (8) entry table. Directed queries are rare
  traffic, so a full table simply recycles the oldest source.

Configuration
=============

The *Query* page (``page_query.c``) exposes, in order: **Enable**, **Send via
RF**, **Send via Internet**, the three general-query toggles (``?APRS?``,
``?WX?``, ``?IGATE?`` — the last two only appear in builds that include the
weather and IGate features), **Enable directed queries**, **Extended directed
queries**, and the **Minimum reply interval** in seconds.

.. list-table::
   :header-rows: 1
   :widths: 30 20 50

   * - JSON key
     - Default
     - Field
   * - ``queryEn``
     - ``false``
     - master enable (opt-in, like messaging)
   * - ``queryRf`` / ``queryInet``
     - ``true`` / ``false``
     - answer on RF / to APRS-IS. The Internet leg is off by default so the
       station does not answer into APRS-IS unless asked to.
   * - ``queryAprsEn`` / ``queryWxEn`` / ``queryIgateEn``
     - ``true``
     - per-general-query enables
   * - ``queryDirectedEn`` / ``queryExtEn``
     - ``true``
     - directed set / extended directed set
   * - ``queryMinInterval``
     - ``30``
     - broadcast rate limit, seconds

The whole page is gated by the ``ENABLE_QUERY`` compile-time switch.

.. note::

   ``query_init()`` must run after ``message_init()``/``igate_start()`` and
   before ``aprs_service_start()`` finishes wiring the RX dispatch chain, which
   is exactly where ``aprs_service.c`` calls it.
