.. _en-igate:

=======================
IGate — APRS-IS gateway
=======================

The ``igate`` component (``components/igate/``) is a full bidirectional APRS-IS
Internet gateway built on LWIP sockets. It reads all of its configuration from
``g_config`` (the web admin's *IGate* page), so the web admin is the single
source of truth.

The APRS-IS client task
=======================

* **TCP client** with multiserver failover and auto-reconnect. It re-reads
  ``g_config`` on every reconnect, so web-admin changes to most IGate
  settings (enable toggles, RF/INET direction, budlist, PHG, beacon timing,
  and the rest) land as soon as the uplink loop next checks them, without a
  reboot.
* **Live identity/server/filter updates.** The login identity
  (``aprs_mycall``/``aprs_ssid``/``aprs_passcode``), the failover server list
  (``aprs_server``) and the server-side filter (``aprs_filter``) are the one
  exception: ``connectAprsIs()`` reads them only once, at connect time, and
  the uplink then holds that session open indefinitely, so on their own a
  changed passcode or a narrowed filter would otherwise sit unused until the
  link happened to drop. The *IGate* page's save handler compares the new
  values against what was saved before and, when identity or a server slot
  changed, calls ``igate_request_reconnect()`` to drop and re-open the
  session with the new values in its next login line; when only the filter
  changed, it calls ``igate_request_filter_update()`` instead, which pushes a
  ``#filter <spec>`` comment line on the already-open socket - the live
  update `aprs-is.net's filter documentation
  <https://www.aprs-is.net/javAPRSFilter.aspx>`_ describes - so the session
  is not dropped just to change the filter. Saving an unrelated IGate field
  triggers neither.
* **Gated on real connectivity**, not merely on "Wi-Fi is up": it polls
  ``net_state_is_connected()``, which becomes true only on
  ``IP_EVENT_STA_GOT_IP`` and false again on disconnect or AP-only mode.
* **Login identity.** The station logs in as its **callsign-SSID**
  (``aprs_mycall`` plus ``aprs_ssid``, e.g. ``LU3VEA-10``; the bare callsign
  when the SSID is 0 — an APRS-IS identity has no ``-0`` form). This is the
  same string ``stationIdentity()`` writes after the ``qA*`` construct on
  gated frames and the same one the IGate's own beacons carry as their source
  callsign, and all three matter to the server: `aprs-is.net's IGate details
  <https://www.aprs-is.net/IGateDetails.aspx>`_ has the server deliver a
  message from APRS-IS only to the client whose login equals the addressee
  byte for byte, so **a message addressed to this station must be addressed to
  its callsign-SSID**, and a packet is recognised as originated by the client
  — rather than tagged as relayed through a server — only when its source
  callsign equals the login. The passcode is unaffected: it is derived from
  the base callsign with the SSID stripped, so every SSID of one station
  shares one passcode.
* **Login line:** ``user <call-ssid> pass <passcode> vers esp32_APRS_igate
  <version>``, with `` filter <filter>`` appended only when a server-side
  filter is configured — the ``filter`` command takes one or more terms, so
  the clause is left out entirely rather than sent as a bare keyword. The
  name and version come from ``APRS_SOFTWARE_NAME`` /
  ``APRS_SOFTWARE_VERSION`` in ``main/include/aprs_service.h``, the latter
  being ``FIRMWARE_INFO``, so the ``vers`` clause identifies *this* firmware
  to APRS-IS server operators. The line is logged exactly as sent (minus the
  CR/LF), so a malformed filter is visible; with no filter configured a second
  line notes that the server's own default applies. The read immediately
  following login goes through the exact same line framer and packet handler
  as the steady-state RX loop below, so a server that sends its banner, the
  ``# logresp … verified/unverified`` line and the first filtered packet all
  within one read still delivers that packet to ``inet2rf`` — nothing arriving
  alongside the banner is ever dropped. The banner and ``# logresp`` lines are
  additionally surfaced as their own log lines; an ``unverified`` response
  raises a warning naming ``aprs_mycall`` / ``aprs_passcode``, and an echoed
  identity that does not match the one sent raises a warning of its own, since
  that is the failure mode that leaves messages addressed to this station
  undelivered while everything else looks healthy.
* **Server-side filter validation.** Before it is sent, ``g_config.aprs_filter``
  is checked for structural validity by
  ``aprs_filter_validate_server_string()`` — each space-separated term must be
  ``<letter>/<args>`` with the right argument count for that filter letter.
* **Shared uplink.** The task always runs, because the same socket is used by
  the message component (``igate_send_raw()``) and by "beacon to internet". It
  idles cheaply when nothing needs it.
* **Dead-link detection.** ``net_state_is_connected()`` only catches the
  station's own Wi-Fi dropping; it says nothing about the far end of an
  already-open APRS-IS socket going quiet - an idle-TCP NAT/firewall mapping
  getting evicted, a blackholed route, or a peer that stops sending without
  ever closing the connection. The RX loop tracks the timestamp of the last
  byte actually read off the socket and, if none arrives for
  ``IGATE_RX_SILENCE_US`` (90 s), logs a warning and closes the socket so the
  normal reconnect path runs. 90 s is comfortably above the ``#`` comment
  cadence servers following `aprs-is.net's connection guidance
  <https://www.aprs-is.net/Connecting.aspx>`_ send whenever the channel is
  otherwise quiet - that comment line is what keeps an idle-but-healthy link
  from ever tripping the timer - while staying short enough to recover well
  within the eviction time of a typical NAT table entry. The socket also
  carries ``SO_KEEPALIVE`` (30 s idle, 10 s interval, 3 probes) as an
  independent, lower-level backstop; it complements rather than replaces the
  RX-side timer, since a peer that keeps acknowledging TCP-level probes while
  no longer sending application data would otherwise slip past it.
* **Nagle disabled (``TCP_NODELAY``).** Set on the socket before ``connect()``,
  as `aprs-is.net's connection guidance
  <https://www.aprs-is.net/Connecting.aspx>`_ asks of any bidirectional
  client. Every outbound line — a gated RF frame, an outbound message, a
  beacon — is assembled together with its CR/LF terminator and written with a
  single ``send()`` in ``sendToAprsIs()``, so with Nagle off that one write
  reaches the wire immediately instead of waiting on an ACK or a Nagle
  timeout.

Server failover
===============

The IGate page stores ``APRS_SERVER_NUM`` (four) server slots in
``g_config.aprs_server[]``, each with its own Enable checkbox, host and port.
All slots share one login identity — callsign, SSID, passcode and filter string
are single-valued — because they represent the same station connecting to
alternative APRS-IS servers.

``connectAprsIs()`` dials the currently selected slot. Any failure — DNS
lookup, ``socket()``, ``connect()`` or sending the login line — calls
``advanceServer()``, which moves the selection to the next **enabled** slot
with circular wrap-around, and the task waits 1 second before the next attempt.
The rotation never stops: it keeps cycling through every enabled slot until one
accepts the connection.

Disabled slots are skipped on the **first** selection after boot as well, not
only after a failure: clearing a slot's checkbox takes it out of service
immediately. If no slot at all is enabled the task falls back to slot 1, so it
always has a concrete destination to attempt and log.

An established connection does not rotate the selection: if the server closes
the link, the task retries the same slot first, and only moves on when that
fresh attempt also fails.

The dashboard shows the host and port of the slot in use at that moment
(``igate_get_current_server()``), so which server a failover landed on is
visible at a glance.

Locally-originated traffic
==========================

Everything this station puts on APRS-IS itself — position and status beacons
(Tracker, IGate, Digipeater), weather reports, telemetry data and its
PARM/UNIT/EQNS/BITS definitions, bulletins, objects and items, outbound
messages and query answers — goes out through ``igate_send_raw()`` with
``TCPIP*`` as its **entire** path, and nothing else. `aprs-is.net's connection
guidance <https://www.aprs-is.net/Connecting.aspx>`_ states the rule in those
words: a packet originating from the client carries ``TCPIP*`` in the path,
nothing more and nothing less.

A digipeater path such as ``WIDE1-1,WIDE2-1`` names repeaters on the air. A
packet injected straight into APRS-IS traverses none of them, so sending that
path describes hops that never happened: a server that does not recognise the
source callsign as its own client keeps the path and tags the packet as
relayed (``,qAS,<login>``), and every consumer — aprs.fi included — then shows
the station's own beacon as if it had been repeated across the air. This is
also why the login identity above must carry the SSID: it is what tells the
server the packet is the client's own.

Every originator therefore builds **one packet per leg** rather than one packet
sent twice. The two lines are identical except for the path suffix: the RF leg
gets the digipeater selection from that beacon's own page
(``aprs_path_build_suffix()``), and the APRS-IS leg gets
``APRS_PATH_TCPIP_SUFFIX`` from ``main/include/aprs_path.h``, which is the one
place that literal is spelled. Query answers pick between the two by the
channel the question arrived on, since an answer goes back the way it came.

RF → INET (``igateProcess()``)
==============================

Every RF-decoded frame that the application dispatches (with ``igate_en`` and
``rf2inet`` on) runs through this pipeline, in order. A frame that fails any
stage is dropped, and the *reason* is recorded against a per-reason counter so
the dashboard can show "N dropped because X" rather than one opaque aggregate.

#. **Duplicate suppression.** The frame is checked against the shared
   duplicate cache (``isDuplicatePacket()``). Both its depth
   (``g_config.dup_cache_size``, ``DUP_CACHE_SIZE_MIN``..``DUP_CACHE_SIZE_MAX``
   = 4..40, default 20) and its window (``g_config.dup_cache_timeout_ms``,
   1000..120000 ms, default 30000) are editable on the *IGate* page and are
   re-read on every lookup, so a change applies without a reboot. The array is
   always allocated at the compile-time capacity ``DUP_CACHE_SIZE_MAX``;
   ``dup_cache_size`` only selects how much of it is used. Duplicates are
   counted separately in ``dupCount``.
#. **Too-short guard.** Frames whose info field is below the minimum usable
   length are dropped (``DROP_TOO_SHORT``).
#. **Path-token filter.** Frames whose path carries ``RFONLY``, ``TCPIP``,
   ``qA*`` or ``NOGATE`` are never gated (``DROP_PATH_TOKEN``).
#. **Satellite-gate rule.** A frame repeated via a known satellite gate whose
   call is not marked used (``*``) is dropped (``DROP_SAT_NOT_USED``).
#. **Third-party (``}``) unwrap.** A frame whose information field starts
   with ``}`` carries a complete inner ``SRC>DST,PATH:payload`` line of its
   own. If that inner path already carries ``TCPIP`` or ``TCPXX``, the frame
   already reached APRS-IS once and is dropped as a loop
   (``DROP_3RDPARTY_LOOP``). Otherwise the outer RF header is discarded and
   every remaining stage — payload-type filter onward — runs against the
   inner packet: its own source, destination, path and payload, exactly as
   if that station had been heard directly. This is what lets a cross-band or
   HF gateway relay a station that has no other route to the Internet.
#. **Generic query gate.** A payload whose first byte is ``?`` (``?APRS?``,
   ``?WX?``, ``?IGATE?``, …) is dropped unconditionally
   (``DROP_GENERIC_QUERY``), regardless of ``g_config.rf2inetFilter`` or any
   other checkbox. See :ref:`en-filtering`.
#. **Payload-type filter.** The (possibly unwrapped) payload is classified by
   ``aprs_filter_classify_info()`` and tested against
   ``g_config.rf2inetFilter`` (``DROP_TYPE_FILTER``). See :ref:`en-filtering`.
#. **Local range gate.** If enabled, the packet's position is decoded and its
   great-circle (haversine) distance from "My Station" is compared against
   ``g_config.rf2inet_range_km``; too-distant packets are dropped
   (``DROP_RANGE_FILTER``). Packets whose position cannot be decoded pass this
   check.
#. **Local prefix gate.** If enabled, the source callsign must start with one
   of the comma-separated prefixes in ``g_config.rf2inet_prefixes`` (e.g.
   ``EA,EB,EC``), else it is dropped (``DROP_PREFIX_FILTER``).
#. **Budlist.** The source callsign is tested against the local
   whitelist/blacklist in ``g_config.rf2inet_budlist_mode`` (``DROP_BUDLIST``).
#. **APRS-IS line-length limit.** Once the ``qAR``/``qAO`` header is built, its
   length plus the CR/LF-stripped info field is checked against the 512-byte
   APRS-IS line limit (``aprs-is.net/Connecting.aspx``, expressed as
   ``APRS_IS_LINE_MAX`` = 510 usable bytes). A frame that would not fit is
   dropped whole, with a warning naming its length, rather than sent as a
   truncated fragment (``DROP_IS_LINE_TOO_LONG``).

A frame that survives all stages gets a ``,qAR,<mycall>-<ssid>`` or
``,qAO,<mycall>-<ssid>`` header and is written to APRS-IS. Per QCON the
construct describes the **station being gated**, not the gateway: ``qAO``
marks a station this IGate would not deliver a message to, and downstream
consumers (message routers, the "messageable" indication on APRS-IS map
sites) read it that way. ``qConstructFor()`` therefore chooses ``qAR`` only
when both hold:

* this station can gate messages to RF at all
  (``aprs_service_can_gate_to_rf()``: transmit available, ``igate_en`` on,
  ``inet2rf`` on), and
* the gated station has **not** been seen on APRS-IS within
  ``igate_local_window_sec`` — the same condition ``messageGatePass()``
  applies to an addressee in the INET → RF direction, since an
  Internet-connected station already has anything addressed to it.

Everything else gets ``qAO``, so a receive-only IGate sends ``qAO`` for every
packet. The callsign-SSID following the q construct is always this station's
own login identity.

INET → RF (``inet2rfHandler()``)
================================

Every line read off the socket is first checked against the 512-byte APRS-IS
line limit as it is accumulated. A line that exceeds it is discarded in full —
every further byte up to the next terminator is consumed without being stored,
so the framer resynchronises cleanly on the following line instead of handing
a truncated fragment downstream — and counted under
``DROP_IS_RX_LINE_TOO_LONG``.

Every non-``#`` line within the limit increments ``isRxCount`` and is handed
to the message engine (``handleIncomingAPRS()``) when messaging is on. It is
then considered for re-transmission on RF only if ``inet2rf`` is set, and only
after passing:

#. **Generic query gate.** A line whose payload starts with ``?`` is dropped
   unconditionally (``DROP_GENERIC_QUERY``), regardless of
   ``g_config.inet2rfFilter`` or any other checkbox — the mirror image of the
   RF→INET generic query gate above, and checked before every other stage
   below. See :ref:`en-filtering`.
#. **Own-report echo suppression.** Every report this station uploads with its
   ``*_2inet`` flag is echoed straight back by the APRS-IS server.
   ``inet_line_is_own_report()`` recognises those echoes (by matching the source
   base callsign against every own-station report callsign) and never re-gates
   them back to RF. Own reports reach RF exclusively through their own "Send via
   RF" (``*_2rf``) flags.
#. **Payload-type filter.** The line is classified by
   ``aprs_filter_classify_tnc2()`` and tested against
   ``g_config.inet2rfFilter``.
#. **Selective third-party unwrap (opt-in).** Third-party (``}``) traffic — the
   classic IGate-loop source — classifies as 0 and is never relayed by default.
   With ``inet2rf_3rdparty_unwrap_en`` on **and** ``inet2rf_budlist_mode ==
   BUDLIST_WHITELIST``, one level of ``}`` wrapping may be unwrapped and the
   inner packet re-classified and relayed, but *only* when the inner packet's
   source is itself on the whitelist. This is never a general "relay all
   third-party" switch.
#. **Budlist.** The source callsign (which may carry a ``-SSID`` here) is tested
   against ``g_config.inet2rf_budlist_mode``.
#. **Message gating.** Applies to the ``MESSAGE`` type only; the other types
   are relayed at the sysop's discretion, which the type filter and the budlist
   above already express. See below.

A line that survives all stages is never keyed onto RF with its APRS-IS
header intact. ``build_thirdparty_frame()`` discards that header entirely and
wraps the original ``SRC>DST`` and information field, unmodified, behind a
``}`` as the payload of this station's own header (``MYCALL[-SSID]>APE32I,
<igate path>:}SRC>DST,TCPIP,MYCALL[-SSID]*:info``) - the third-party form the
APRS spec requires for gatewayed traffic. This keeps ``qA`` constructs and a
bare ``TCPIP`` off the air, and lets every other IGate that hears the frame
recognise it as already gated instead of gating it back.

.. warning::

   Re-gating third-party traffic without restriction is the number-one cause of
   IGate loops. The third-party unwrap is deliberately gated behind an explicit
   opt-in *and* a whitelist for exactly this reason.

Message gating
==============

An IGate sits on a very large data stream and must not gate indiscriminately.
With ``igate_msg_gate_en`` on (the factory default), an APRS message read from
APRS-IS is put on the air only when **all four** conditions hold at once:

.. list-table::
   :header-rows: 1
   :widths: 46 54

   * - Condition
     - Drop reason when it fails
   * - The sender's header carries none of ``TCPXX``, ``NOGATE``, ``RFONLY``
     - ``DROP_MSG_NOGATE``
   * - The addressee was heard on RF inside ``igate_local_window_sec``
     - ``DROP_MSG_NOT_LOCAL``
   * - The addressee is not itself Internet-connected
     - ``DROP_MSG_ADDRESSEE_INET``
   * - The sender was **not** heard on RF inside the same window
     - ``DROP_MSG_SENDER_LOCAL``

Each failure has its own reason so the dashboard's *Drop Breakdown* says which
condition stopped a message — the single most-asked IGate support question.
Only the header is searched for the ``TCPXX``/``NOGATE``/``RFONLY`` tokens, so
a message whose *text* mentions one is not mistaken for one routed with it.

The locality tests read ``lastheard_heard_rf_within()`` and
``lastheard_heard_inet_within()``, which keep a separate stamp per channel: a
station can be both locally audible and Internet-connected, and each condition
tests its own. A frame heard off the air also counts as an Internet sighting
when its path carries ``TCPIP`` or ``TCPXX`` — the on-air signature of a packet
that has already passed through a gateway.

*Heard-locally window (s)* is ``igate_local_window_sec``, 60–3600 s, one hour by
default, which is the upper bound the APRS-IS IGate design notes recommend.

Turning message gating off transmits **every** message the type filter allows,
to addressees anywhere in the world, whether or not anything on the local
channel can hear them.

Associated position
===================

Rather than replaying a station's historical position reports, the gateway
notes the stations it has gated a message **to** — an eight-entry ring — and
forwards the next plain position or buoy report it sees for each of them,
whatever the type filter says, so the local operator has something to plot for
the far end of the conversation. The slot is released by that one report, which
is what makes it a follow-up rather than a subscription; a weather or object
report is gated under its own type bit, on its own merits.

Counters and drop reasons
=========================

The ``igate_stats_t`` snapshot (``igate_get_stats()``) carries:

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Counter
     - Meaning
   * - ``rxCount``
     - Frames considered for gatewaying (RF→INET).
   * - ``txCount``
     - Frames actually sent to APRS-IS as a result of gatewaying.
   * - ``msgCount``
     - APRS message packets (``:`` data type identifier) gated in either
       direction — RF→INET by ``igateProcess()``, INET→RF by
       ``igate_note_message_gated()`` from ``aprs_service.c``. This is the
       ``MSG_CNT`` figure the ``?IGATE?`` answer reports, so it counts messages
       only and not the rest of the gated traffic.
   * - ``dupCount``
     - Duplicate frames suppressed.
   * - ``isRxCount``
     - **All** lines read off the socket (superset of what reaches the INET→RF
       handler).
   * - ``isTxCount``
     - **All** socket writes: gatewayed frames, outbound messages, and digi
       "beacon to internet" sends alike.
   * - ``dropByReason[]``
     - Per-reason drop counters, indexed by ``drop_reason_t``. The RF→INET
       stages above cover ``DROP_TOO_SHORT``, ``DROP_PATH_TOKEN``,
       ``DROP_SAT_NOT_USED``, ``DROP_3RDPARTY_LOOP``, ``DROP_GENERIC_QUERY``,
       ``DROP_TYPE_FILTER``, ``DROP_RANGE_FILTER``, ``DROP_PREFIX_FILTER``,
       ``DROP_BUDLIST``, ``DROP_IS_LINE_TOO_LONG`` and ``DROP_TX_FAIL``; the
       RX line reader above covers ``DROP_IS_RX_LINE_TOO_LONG``. The
       array also carries reasons bumped elsewhere in the firmware (RF TX
       path, digipeater, AX.25 decode) — see ``drop_reason_t`` in
       ``components/igate/include/igate.h`` for the complete, authoritative
       list. There is no generic/opaque catch-all reason: every drop is
       attributed to a specific named cause. ``igate_stats_total_drop()`` sums
       the non-error reasons; ``igate_stats_total_err()`` sums the two
       decode/send error reasons separately.

``igate_note_drop()`` is exposed so other components sharing the same filtering
concepts — currently ``aprs_service.c``'s INET→RF handler, for its type-filter
and budlist checks — contribute to the same per-reason breakdown.

Connectivity indicator
======================

``igate_is_connected()`` is true while the APRS-IS TCP socket is open, logged
in and pumping the RX line reader. The web dashboard's *Network Status* panel
(the APRS-IS pill) reads it. Because the RX loop closes the socket as soon as
dead-link detection trips (see above), this also reports false for the whole
interval between a silently dropped link and the next successful re-login,
rather than continuing to show "connected" against a socket that has stopped
delivering anything.
