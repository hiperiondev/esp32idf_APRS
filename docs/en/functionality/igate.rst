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
  ``g_config`` on every reconnect, so most web-admin changes land after the
  next reconnect cycle without a reboot.
* **Gated on real connectivity**, not merely on "Wi-Fi is up": it polls
  ``net_state_is_connected()``, which becomes true only on
  ``IP_EVENT_STA_GOT_IP`` and false again on disconnect or AP-only mode.
* **Login line:** ``user <mycall> pass <passcode> vers ESP32APRS 1.0 filter
  <filter>`` — logged verbatim, so a malformed filter is visible. The server
  banner and the ``# logresp … verified/unverified`` line are surfaced; an
  ``unverified`` response raises a warning naming ``aprs_mycall`` /
  ``aprs_passcode``.
* **Server-side filter validation.** Before it is sent, ``g_config.aprs_filter``
  is checked for structural validity by
  ``aprs_filter_validate_server_string()`` — each space-separated term must be
  ``<letter>/<args>`` with the right argument count for that filter letter.
* **Shared uplink.** The task always runs, because the same socket is used by
  the message component (``igate_send_raw()``) and by "beacon to internet". It
  idles cheaply when nothing needs it.

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

A frame that survives all stages gets a ``,qAR,<mycall>-<ssid>`` header — or
``,qAO,<mycall>-<ssid>`` when this IGate cannot gate messages back to RF for
the station being gated (``aprs_service_can_gate_to_rf()``, i.e. transmit is
unavailable, ``igate_en`` is off, or ``inet2rf`` is off) — and is written to
APRS-IS. The callsign-SSID following the q construct is always this station's
own login identity, per QCON.

INET → RF (``inet2rfHandler()``)
================================

Every non-``#`` line read off the socket increments ``isRxCount`` and is handed
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
``}`` as the payload of this station's own header (``MYCALL[-SSID]>APE32L,
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
       ``DROP_BUDLIST`` and ``DROP_TX_FAIL``; the
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
(the APRS-IS pill) reads it.
