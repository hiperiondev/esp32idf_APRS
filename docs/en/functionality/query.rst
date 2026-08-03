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

* ``query_process(tnc2Line, source)`` — fed every decoded TNC2 line. It no-ops
  unless ``query_en`` is set, the switch for ``source`` is on, and the
  information field starts with ``?``, which is what makes a line a **general**
  query. Matching the keyword, applying the rate limit and queuing the request
  is all it does.
* ``query_process_directed(fromCall, toCall, text, tnc2Line, source)`` — called
  by ``message.c``'s ``handleIncomingAPRS()`` when an addressed message's text
  starts with ``?``, so the ``:ADDRESSEE:`` parsing is not duplicated here. It
  no-ops unless ``query_en`` **and** ``query_directed_en`` are set, the switch
  for ``source`` is on, and ``toCall`` matches ``g_config.aprs_mycall`` (base
  callsign, SSID-insensitive).

Source and channel
==================

Both entry points are told where the query came from — ``QUERY_SRC_RF`` for the
radio, ``QUERY_SRC_INET`` for the APRS-IS feed — and that source decides two
things.

It decides **whether the query is answered at all**: ``g_config.query_rf`` and
``query_inet`` name a source, not a destination. It also decides **where the
answer goes**: a question heard on the air is answered on the air, a question
read from the feed is answered to APRS-IS. Responses are transmitted through the
handler installed with ``query_set_tx_handler()`` — ``aprs_service.c`` reuses the
very same ``messageTxHandler()`` it gives the messaging engine — and the bitmask
handed to it always carries exactly one of ``MSG_CHANNEL_RF`` /
``MSG_CHANNEL_INET``, the one matching the source.

Pairing the two is what keeps the APRS-IS feed away from the transmitter.
``?APRS?`` is ordinary backbone traffic and an IGate sees a steady stream of it;
with the APRS-IS source switch off — the factory setting — none of it reaches
the responder, and with the switch on the answers go back to APRS-IS rather than
on the air.

Two exceptions are deliberate, because the traffic is not the answer itself.
``?APRSM`` re-sends messages this station already owes the querying operator,
routed by the Message page's own "send via" flags; ``?APRSO`` re-announces
Objects/Items, each routed by its own configuration. Only the "nothing pending"
reply and the ``No objects`` reply follow the query's source.

Where the answer is built
=========================

Neither entry point builds or transmits anything. Both match the keyword, apply
the rate limit and **record** the request: its type, the querying station, and
at most one piece of text — the callsign a ``?APRSH`` asks about, or the route a
``?APRST`` arrived by, read there and then while the received line is still in
hand. The answer itself is built and put on the air by ``query_service()``,
which the **beacon scheduler** task calls at the start of every pass; queuing a
request also calls ``beacon_scheduler_wake()``, so that pass happens right away
rather than whenever the next beacon falls due.

The reason is stack. A ``?APRS?`` answer *is* a beacon: it runs
``beacon_build_igate_position_packet()``, several of newlib's float-capable
``snprintf()``\ s, ``lat_lon_to_aprs()``, the path builder and then the whole
``aprs_service_send_tnc2()`` → ``modem_send_tnc2()`` → ``ax25_encode()`` chain,
each level stacking its own 300–450 byte buffer — precisely the call tree
``beacon_scheduler.c`` sizes its 14336-byte stack for. Queries, though, arrive
on ``modem_svc`` (RF) and ``igate_task`` (APRS-IS), whose stacks are a fraction
of that. The work therefore runs on the task whose budget covers it, not on
whichever task happened to receive the question, and the builders can grow
without those two paths needing to be re-checked.

Two things follow from where it runs. Answering never occupies an RX task for
the length of a transmission burst — the property ``?APRSO`` has always had —
and a query answer inherits the scheduler's beacon context, so
``aprs_service_send_tnc2()`` waits briefly for a full RF TX ring instead of
dropping the reply (see :ref:`en-beacons`).

The queue holds ``QUERY_PENDING_MAX`` (8) requests, oldest answered first. A
request identical to one already waiting is folded into it — every answer
reports live state at the moment it is sent, so a duplicate would only put the
same information on the air twice — and once the queue is full, further requests
are dropped with a warning rather than displacing an answer already owed to
someone.

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
       ``<IGATE,MSG_CNT=n,LOC_CNT=n>``, carrying the two figures chapter 15
       gives them. ``MSG_CNT`` is the running count of APRS message packets this
       gateway has passed in either direction (``igate_stats_t::msgCount``, not
       a tally of all gated traffic). ``LOC_CNT`` is a live figure rather than a
       running total: the number of distinct stations in the local heard list,
       from ``lastheard_station_count(true)``, counting only the rows whose most
       recent frame was heard off the air. Silently ignored while ``igate_en`` is
       off.

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
       ``objitems_request_transmit_all()``, and since it is served from the
       scheduler pass itself the elements go out later in that same pass. Built
       without ``ENABLE_OBJECTS_ITEMS``, it replies ``No objects``.
   * - ``?APRST`` / ``?PING?``
     - The route the query itself took, read off the received TNC2 line when the
       request is queued. With no line available it answers with an unknown
       route.

Answers of the list kind go out as APRS text messages addressed back to the
querying station, with the fixed 9-character space-padded addressee field and
**no** message number — a query answer is informational, so no ack is solicited.
The destination call used for query traffic is ``APE32L``, and the path is the
IGate page's own path bitmask.

Rate limiting
=============

Two independent limiters keep the responder from becoming an airtime problem or
half of a feedback loop with another auto-responder:

* **Broadcast limiter** — per query *type* **and source**, at most one answer
  per ``g_config.query_min_interval_sec`` (default 30 s; the *Query* page's
  minimum is 5 s). Because it is per type, a busy channel asking ``?APRS?``
  cannot suppress a ``?WX?`` answer; because it is also per source, a talkative
  APRS-IS feed cannot spend the allowance a question heard on the air needs.
* **Directed limiter** — directed queries bypass the broadcast limiter (they are
  explicitly addressed to this station) but get their own, tighter **per-source**
  limit of ``QUERY_DIRECTED_MIN_INTERVAL_SEC`` (5 s), tracked in a fixed
  ``QUERY_DIRECTED_TRACK_MAX`` (8) entry table. Directed queries are rare
  traffic, so a full table simply recycles the oldest source.

Configuration
=============

The *Query* page (``page_query.c``) exposes, in order: **Enable**, **Answer
queries heard on RF**, **Answer queries heard from APRS-IS**, the three
general-query toggles (``?APRS?``,
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
     - which source is answered: queries heard on RF / read from the APRS-IS
       feed. The APRS-IS source is off by default, so backbone traffic cannot
       key the transmitter out of the box.
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
