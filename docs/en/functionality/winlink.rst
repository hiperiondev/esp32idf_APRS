.. _en-winlink:

===============================
Winlink Radio E-mail (APRSLink)
===============================

`APRSLink <https://winlink.org/APRSLink>`__ is the CMS-hosted gateway between
APRS and the Winlink global radio e-mail system. It is reached with ordinary
APRS text messages addressed to a service callsign — ``WLNK-1`` — and it
answers with APRS text messages back. There is no separate protocol, no session
layer and no framing of its own: an APRSLink session looks, on the air, exactly
like a conversation with a very literal-minded human operator.

Two different things are usually meant by "a Winlink gateway", and this station
does both. They are independent of each other and are configured separately, so
it is worth being clear about which is which before touching the *Winlink*
page.

Two roles
=========

**Client.** This station runs its own session, reads and writes its own mail,
and keeps the service's replies where the web admin can show them. This is the
``winlink`` component, the *Winlink Account* fieldset and the session terminal.
It needs a Winlink account and its password.

**Gateway.** A neighbouring station on the local RF channel addresses the
service itself. Its commands travel to APRS-IS through this station's ordinary
RF→INET path, and the service's answers come back through the INET→RF path.
Nothing of yours is involved: the neighbour's own callsign opens its own
mailbox and its own password answers its own login challenge. This role is
almost entirely the IGate doing what it already does, and its whole
configuration is the one switch in the *Gateway for Local Stations* fieldset.

If your goal is to let the people around you use Winlink, the gateway role is
the one that matters and the client role is optional. If your goal is to read
your own mail from the station, it is the other way round.

The client
==========

Identity and the mailbox
------------------------

The service keys the mailbox on the **base callsign** of whoever sends the
command, with any ``-SSID`` suffix ignored, so the account reached is always
``<BASECALL>@winlink.org``. The callsign the outgoing frame actually carries is
the messaging service's own (``msgMycall``, the *Message* page), which is why
*Use the Message service callsign* is on by default; the separate field exists
only for a station whose Winlink account is under a different callsign.

Sessions
--------

Sending any command to the service opens a session. When the account has secure
login switched on, the service answers with a challenge of the form
``Login [NNN]``, where each digit is a 1-based character position in the
password. The answer is those characters plus three arbitrary ones, in any
order — so the password itself never travels on the air, and an answer that is
observed on the channel reveals only three of its characters and not where they
came from. When secure login is off there is no challenge at all and the
service simply answers the command; the client accepts that just as readily and
goes straight to command mode.

A session expires on the service's side after about two hours.
``wlSessionMaxMin`` (110 minutes by default) sits below that, so this station
gives a session up slightly before the service does rather than discovering it
is gone by sending a command into it.

Pacing
------

One command is outstanding at a time. The next one leaves only after the
service has acknowledged its predecessor, and a further three seconds of quiet
are required after any reply before the next exchange starts. That is
deliberately unhurried: it anchors every step of the session to something the
service actually said rather than to a local timer, and it stops a session from
becoming a burst of frames on a shared channel. A command that goes
unacknowledged is retransmitted twice and then the session is abandoned, with
the reason shown on the page.

Commands
--------

The whole APRSLink command set is text, so the *Command* field takes any of it
directly. The commands that come up most often are:

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Command
     - Meaning
   * - ``L``
     - List the pending messages. The service returns up to five.
   * - ``R<n>``
     - Read message *n* as it appeared in the listing.
   * - ``Y<n>``
     - Start a reply to message *n*.
   * - ``F<n> <addressee>``
     - Forward message *n* to another address.
   * - ``K<n>``
     - Delete message *n*.
   * - ``SP <addressee> <subject>``
     - Start a message. Everything sent after it is body text.
   * - ``/EX``
     - End the body and hand the message to the service.
   * - ``A <alias>=<address>``
     - Create or update an alias. ``A <alias>=`` deletes it, ``AL`` lists them.
   * - ``B``
     - Log off.
   * - ``?``
     - Help. ``?L`` gives help for one command.

The *Start message* / *Add line* / *Send message* buttons drive the ``SP`` …
``/EX`` sequence, which is the only one whose order matters, and do nothing the
command field could not also do by hand.

Where the traffic goes
----------------------

``wlInetOnly`` is on by default and keeps this station's own Winlink traffic
off the air. The reasoning is that this station *is* an IGate: a command it
transmits occupies the local channel with traffic no station on that channel
needs to hear, and the service's answer arrives over the same Internet link
either way. The switch can only ever remove the RF leg — with messaging's own
*Send/Receive via Internet* off there is nothing to fall back on, and the
command goes out on RF as before, which is exactly what a station without an
APRS-IS uplink wants.

Being told about waiting mail
-----------------------------

APRSLink watches the comments of the position reports it sees for the word
``winlink`` and uses it to decide which stations to notify, unprompted, when
mail is waiting for them. *Announce this station as a Winlink reader* appends
that word to the beacon comment. It is appended rather than substituted and
only while it still fits, so a comment already filling the field keeps every
character you wrote — and the page says so if there is no room left.

*Check for mail every* is the other half of the same idea and does not depend
on the service noticing anything: it opens a session and sends ``L`` on a fixed
interval. It only ever runs from idle, so it never interrupts a session that is
doing something.

Stored replies
--------------

Everything the service sends back is kept in ``/storage/winlink.json``, oldest
first, up to the most recent 24 replies. Clearing them from the page removes
that file and nothing else — the account settings live in ``config.json`` and
are untouched.

The gateway
===========

An IGate does not put every message it reads from APRS-IS on the air. The
message gate (``main/aprs_service.c``, ``messageGatePass()``) requires four
things of a message before it will be transmitted:

* the addressee was heard on RF within ``igateLocalWindow`` seconds;
* it was heard over no more than ``igateMsgMaxHops`` digipeater hops;
* the addressee was **not** heard on APRS-IS within the same window;
* the sender was not itself heard on RF.

A reply from the Winlink service satisfies the first, second and fourth
conditions naturally. The third is the problem: this station gates the
neighbour's own command up to APRS-IS, the server echoes it back on the feed,
and that echo can be enough to make the neighbour look Internet-connected — at
which point the reply it is waiting for is dropped as ``DROP_MSG_ADDRESSEE_INET``
and the session simply stalls with nothing obviously wrong.

*Let the service's answers reach local stations on RF* (``wlGateExempt``, on by
default) lifts that one condition, and only for messages whose sender is the
configured service callsign. The premise the rule rests on — that an addressee
on APRS-IS can read the message there for itself — is false by construction for
an APRSLink reply: it exists only because the addressee asked this station's
gateway for it, and it has exactly one delivery path. Every other condition,
the header checks that keep ``TCPXX``/``NOGATE``/``RFONLY`` traffic off the air,
and the bulletin/broadcast drop all still apply unchanged.

The three IGate settings the gate consults are shown read-only on the *Winlink*
page next to that switch, so all four inputs to the decision can be seen at
once. They are edited on the *IGate* page. ``igateLocalWindow`` in particular
is worth a look: the service can take a minute or more to answer a read, and a
window shorter than that round trip will drop the answer for the first reason
rather than the third.

Configuration
=============

Every setting is a ``wl*`` key in ``config.json`` — see
:ref:`en-config-json`. The password is stored there in the clear, exactly as
the APRS-IS passcode already is; anyone who can read the storage partition can
read both.
