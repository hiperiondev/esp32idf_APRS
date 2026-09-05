.. _en-brandmeister:

======================================
BrandMeister interconnect (no DMR)
======================================

This station can exchange APRS traffic with the BrandMeister DMR network over
the Internet without implementing any part of DMR. No Homebrew/MMDVM link, no
OpenBridge, no BrandMeister account and no master password are involved.

Why no DMR link is needed
=========================

BrandMeister's APRS side **is an APRS-IS client**. Every BrandMeister master
runs a gateway process that logs into a public APRS-IS server with an ordinary
``user``/``pass``/``filter`` line and injects DMR-sourced position, telemetry
and message traffic as plain TNC2 lines. In the other direction it subscribes
to APRS-IS and converts what it receives into DMR text messages.

The transport this firmware needs is therefore the APRS-IS session the IGate
already holds. What the *BrandMeister* page adds is recognition, safety gating
and message routing on top of it.

Recognising BrandMeister traffic
================================

``main/include/aprs_bm.h`` is the single source of truth. Three independent
tests are applied to each line read from APRS-IS, and the first that matches
wins:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Test
     - What it matches
   * - TOCALL
     - A destination address of ``APBM`` plus exactly two characters. ``APBMxx``
       is the block allocated to BrandMeister; ``APBMnD`` is the main server
       software and ``APBMnS`` its supplementary services.
   * - DMR alias
     - A path element equal to ``DMR`` appearing before the q construct, with
       or without the used marker.
   * - Entry station
     - The callsign immediately after ``qAS``/``qAR`` matching one of the
       gateway callsigns configured on the page. A trailing ``*`` matches by
       prefix. Skipped when the list is empty, which is the factory state.

The second test is not redundant with the first. Genuine BrandMeister traffic
exists carrying the generic ``APRS`` tocall and only the DMR hop::

   PA0WCH>APRS,DMR*,qAS,PI1DMR-10:@043258h5123.03N/00526.95E(036/000
   DC6RN-9>APRS,DB0CJ,DMR*,qAR,DB0CJ:@043233h4925.11N/01152.85Ev148/000

A classifier keyed on the tocall alone would miss both. Since master build
20170909 the originating repeater callsign is also present in the digipath,
which is why the second sample carries ``DB0CJ`` ahead of the alias.

A frame decoded off the air is **never** classified as BrandMeister traffic,
whatever its path reads: the question the classifier answers is "did the
network gate this onto APRS-IS", and a frame heard on the radio did not arrive
that way.

Two ways to run it
==================

**Local BrandMeister** — the default, and what most operators actually want.
Leave the server filter as an ordinary local subscription. BrandMeister traffic
inside your radius arrives on its own, because it is ordinary APRS-IS traffic;
the classifier marks it, the LAST HEARD table shows it with a ``BM:`` prefix in
place of ``INET:``, and Internet-to-RF gating treats it like any other Internet
station. This costs no extra bandwidth and no extra airtime.

**Worldwide monitor** — opt-in. Adding ``u/APBM*`` to the IGate page's server
filter subscribes to BrandMeister traffic from the entire network.

.. warning::

   APRS-IS server filter terms are combined with OR, never with AND: a packet
   matching any one term is passed. ``u/APBM* r/lat/lon/150`` therefore asks
   for BrandMeister traffic **worldwide** *or* anything within 150 km — the
   intersection cannot be expressed to the server at all. The restriction to a
   local radius has to happen on this station, which is what the Internet-to-RF
   range filter on the IGate page does.

Because of that, enabling the monitor switch while Internet-to-RF gating is on
and the Internet-to-RF range filter is off is **refused**. The explanation sits
under the switch for as long as the precondition is unmet, not just on the page
that follows a refused save: the page re-derives the condition from the stored
configuration on every load, so it reads the same whether the operator has just
been refused, is about to be, or arrived on a station where the range filter was
turned off from the IGate page afterwards. Deriving it rather than remembering
it also means the message reaches the browser it concerns — several operators
can have the web admin open at once. The same rule is re-applied when the IGate
page turns the range filter off, and again when a ``config.json`` is loaded, so
it cannot be bypassed by editing the file by hand.

The page never edits the server filter string itself. The filter belongs to the
operator, and a page that rewrote it silently would make the IGate page
misreport what was actually sent to the server; instead the exact term to add is
shown, and the status table reports whether the running filter carries it.

Sending to BrandMeister
=======================

Private message to a DMR user
-----------------------------

Send an ordinary APRS message addressed to the callsign-SSID the user
associated with their DMR ID in SelfCare. The radio displays::

   <SENDER CALLSIGN> <message text>

A DMR delivery report comes back as a normal APRS message acknowledgement.

.. note::

   Delivery is not guaranteed and failure is silent. Each master's gateway
   applies its own pattern to the addressee of incoming messages — a
   country-scoped regular expression this station cannot see or predict. A
   message filtered out by it simply produces no acknowledgement. Absence of an
   ack is therefore not evidence of non-delivery, and the chat page does not
   present an un-acked message as delivered.

With *Send messages to BrandMeister stations over the Internet only* enabled
(the default), a message addressed to a station last heard as BrandMeister
traffic goes out over APRS-IS alone. Such a station is on the network, not on
the local channel, so every RF copy is airtime spent on a receiver that is not
there — multiplied by the retry count for as long as the message goes unacked.
This can only ever remove the RF leg: with *Send to Internet* off on the
Message page, nothing is sent at all.

Message to a DMR talkgroup
--------------------------

Use the existing *Bulletins* page. A bulletin whose identifier is ``0``–``9``
and whose group name is the 1–5 digit talkgroup ID forms the addressee
BrandMeister reads, with *Send to Internet* enabled:

.. list-table::
   :header-rows: 1
   :widths: 25 25 50

   * - Identifier
     - Group
     - Addressee sent, and where it lands
   * - ``0``
     - ``2509``
     - ``BLN02509`` → DMR talkgroup 2509

Recipients see ``<SENDER CALLSIGN> BLN02509 <message text>``. A non-numeric
group name is still a perfectly valid APRS group bulletin — it simply will not
reach a talkgroup.

Queries
-------

``?APRSP`` (position) and ``?APRSS`` (status) directed at a DMR station work
for radios with ARS/RRS/LRRP configured.

Configuration
=============

Everything on the *BrandMeister* page, which sits directly after *IGate* in
the menu:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Setting
     - Meaning
   * - Enable BrandMeister interconnect
     - Master switch. With it off no line is classified, no station is marked
       and message routing is untouched.
   * - Subscribe to worldwide BrandMeister traffic
     - Records the intent to run the ``u/APBM*`` subscription, and enforces the
       range-filter precondition described above.
   * - Send messages to BrandMeister stations over the Internet only
     - On by default. Suppresses the RF leg for a BrandMeister addressee.
   * - Gateway 1–4
     - Optional entry-station callsigns for the third classifier test. A
       trailing ``*`` matches by prefix.

The Internet-to-RF range filter lives on the *IGate* page next to its
RF-to-Internet twin, because it governs every line the feed offers the
transmitter and not only BrandMeister traffic. It applies even to a
BrandMeister line that carries no position of its own — a repeater status
broadcast, for example — since a worldwide monitor subscription carries no
geographic filter term for the range gate to stand in for; every other
INET→RF line without a position is left to its own gating rules because the
operator's ordinary server-side radius filter already keeps it local. The
*BrandMeister* page's status table reports the range filter's state.

What is deliberately not implemented
====================================

* Any DMR, Homebrew/MMDVM or OpenBridge connection.
* The BrandMeister REST API and the LastHeard stream. Both are HTTPS/WSS only,
  and this firmware implements no HTTPS/WebSocket client for either; what they
  return is DMR session metadata and network inventory, not APRS.
* Any local reproduction of a master's addressee pattern. It is per-master and
  undiscoverable from here.
