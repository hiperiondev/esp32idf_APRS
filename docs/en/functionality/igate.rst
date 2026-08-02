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

* **TCP client** with auto-reconnect. It re-reads ``g_config`` on every
  reconnect, so most web-admin changes land after the next reconnect cycle
  without a reboot.
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

RF → INET (``igateProcess()``)
==============================

Every RF-decoded frame that the application dispatches (with ``igate_en`` and
``rf2inet`` on) runs through this pipeline, in order. A frame that fails any
stage is dropped, and the *reason* is recorded against a per-reason counter so
the dashboard can show "N dropped because X" rather than one opaque aggregate.

#. **Duplicate suppression.** The frame is checked against a 10-entry / 30 s
   cache (``isDuplicatePacket()``). Duplicates are counted separately in
   ``dupCount``.
#. **Too-short guard.** Frames whose info field is below the minimum usable
   length are dropped (``DROP_TOO_SHORT``).
#. **Path-token filter.** Frames whose path carries ``RFONLY``, ``TCPIP``,
   ``qA*`` or ``NOGATE`` are never gated (``DROP_PATH_TOKEN``).
#. **Satellite-gate rule.** A frame repeated via a known satellite gate whose
   call is not marked used (``*``) is dropped (``DROP_SAT_NOT_USED``).
#. **Payload-type filter.** The payload is classified by
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

A frame that survives all stages gets a ``,qAR,<mycall>-<ssid>`` header — or the
``,<mycall>-<ssid>*,qAO,<object>`` satellite-gate form — and is written to
APRS-IS.

INET → RF (``inet2rfHandler()``)
================================

Every non-``#`` line read off the socket increments ``isRxCount`` and is handed
to the message engine (``handleIncomingAPRS()``) when messaging is on. It is
then considered for re-transmission on RF only if ``inet2rf`` is set, and only
after passing:

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

.. warning::

   Re-gating third-party traffic without restriction is the number-one cause of
   IGate loops. The third-party unwrap is deliberately gated behind an explicit
   opt-in *and* a whitelist for exactly this reason.

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
       ``DROP_SAT_NOT_USED``, ``DROP_TYPE_FILTER``, ``DROP_RANGE_FILTER``,
       ``DROP_PREFIX_FILTER``, ``DROP_BUDLIST`` and ``DROP_TX_FAIL``; the
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
