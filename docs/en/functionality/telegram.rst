.. _en-telegram:

============
Telegram Bot
============

The station can run an optional Telegram bot alongside its APRS services, so
an operator can check on and lightly control the station from a phone without
opening the web admin. It is built from two components layered on top of each
other:

* ``esp_telegram_bot`` — the HTTPS transport: the bot token, the Telegram Bot
  API URLs, the TLS client and multipart upload.
* ``telegram_service`` — long polling, command dispatch, per-user/per-chat
  authorization, alerts and remote parameters, built on that transport.

``main/telegram_app.c`` is the glue between those two components and this
firmware: it owns the bot's own JSON store, brings the service up and down in
a supervised way, and publishes a diagnosis the *Telegram* web-admin page
renders as a translated sentence.

Its own configuration file
===========================

Everything the bot needs lives in ``/storage/telegram.json``, not in
``config.json``:

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Field
     - Meaning
   * - ``enabled``
     - The switch the Telegram page renders. The only key this firmware adds
       to the store; a hand-written file that omits it loads with the bot off.
   * - ``routeStationMessages``
     - The Telegram page's "Route Station messages" switch. When on, every
       APRS message addressed to an authorized user's own callsign is also
       sent to that user's own Telegram chat, whether it was received from
       the network or originated here on the *Snd/Rcv Msg* page; see `Routing
       station messages to Telegram`_ below. Absent, the same as ``enabled``,
       loads as off.
   * - ``routeBulletins``
     - The Telegram page's "Route Bulletins" switch. When on, every APRS
       bulletin this station handles - received from the network or
       transmitted by its own *Bulletins* page - is sent to every authorized
       user, to the administrator and to every allowed group chat; see
       `Routing bulletins to Telegram`_ below. Absent, the same as
       ``enabled``, loads as off.
   * - ``bulletinWindowSeconds``
     - The Telegram page's "Bulletin repeat window" field: how many seconds a
       bulletin that has been routed keeps its own repeats from being routed
       as well, 0 to 86400. 0 routes every copy. Unlike the switches above,
       an absent key loads as the 900 s default rather than as 0, because 0
       is a legal setting here and means the opposite of "leave it alone".
   * - Bot token
     - The token issued by `@BotFather <https://t.me/BotFather>`__.
   * - Administrator identifier
     - The numeric Telegram user identifier of the station's administrator.
       ``telegram_init()`` adds it to the authorized-user list itself.
   * - Mini App address
     - Optional HTTPS address of a Telegram Mini App the bot's menu button
       opens.
   * - Authorized users / allowed group chats
     - Up to 8 authorized users and 4 allowed group chats, each an
       identifier plus a display name.

Every field above is editable from the *Telegram* page, which loads the
whole structure before a save and writes it back with the form's changes
applied, so the whole configuration is also one file that can be downloaded,
edited by hand and uploaded again from the *File Storage* page
(:ref:`en-storage-ota`).

Telegram identifiers are 64-bit and signed (a supergroup identifier is a large
negative number), so both the administrator identifier and every user/chat
entry are carried as ``int64_t``, posted from the web form as text for
exactly that reason.

Supervised bring-up
====================

Turning the switch on does not call into the service directly. Instead
``telegram_app_apply_config()`` starts a small supervisor task, because a
Telegram bring-up can fail for a reason the operator has to be told apart:

#. **Network connectivity.** Checked first and cheaply, before anything else
   is allocated, so a station with no route to the internet says so at once.
#. **Root certificate.** When the transport is not built with the ESP-IDF
   certificate bundle, it needs a PEM file on the storage partition
   (``/storage/telegram_certificate.pem`` by default, up to 8 KB); a missing
   or invalid file is reported before any network attempt.
#. **Token shape.** The token must be of the form ``<digits>:<secret>``
   before it is ever sent anywhere.
#. **Heap.** A buffer, a queue or a TLS session that does not fit is
   distinguished from every other failure, since the wrong diagnosis sends
   an operator chasing memory that was never the problem.
#. **DNS and TCP.** Resolving ``api.telegram.org`` and opening the TLS
   connection to it are each checked in turn.
#. **Telegram's own answer.** A malformed but well-formed-looking token is
   only caught once the API itself rejects it.

Each attempt begins by taking a locked snapshot of ``telegram.json``'s
in-memory copy — token, administrator identifier, authorized users and
allowed chats — into a stack-local copy the worker then runs from for the
rest of the attempt. A save landing on the *Telegram* page while a handshake
is already tens of seconds into flight therefore never reaches the token or
tables an attempt in progress is using; it is picked up cleanly by the next
attempt instead.

Bring-up and teardown both happen off the calling task, handed to
``telegram_app_tick_1hz()`` (called once a second) which spawns a short-lived
worker only when one is actually due. That keeps the handshake-sized stack a
bring-up needs (Telegram's own guidance: around 8 KB is the practical minimum)
off both the web server's save handler and any permanent task, so flipping
the switch on the *Telegram* page never blocks the browser and never keeps
that stack allocated once the bot is simply running.

There is one worker task, not one per kind of job. A bring-up, a teardown and
the delivery of routed notifications (described further down) are all sized
for a TLS handshake, and are all carried out by the same task instead of one
task per kind, since more than one such stack at a time is more than this
board can hold alongside the service's own polling task, the web server's
task and the handshake itself. Whichever job is spawned first runs alone, and
once it is done the same task drains the notification queue before it exits,
rather than a second task being spawned for that. A bring-up or teardown that
finds the worker already running stays pending and is offered again by the
next tick, and a notification queued while the worker is busy leaves its
lines in place for the spawn the next routed line triggers, or for the drain
the running worker performs on its way out. Nothing is lost either way; the
only cost is a delay of a second or so.

While the bot runs, the same 1 Hz tick republishes its counters and notices
an uplink that went away or a polling task that has ended, so a station that
loses its connection reports "waiting for a network route" instead of an
unexplained, growing count of polling errors.

Reboot is deliberately left disabled (``allow_reboot = false``): this station
carries a transmitter and a scheduler with timed obligations, and a restart
is already available, behind the web admin's own login, from the *System*
page.

Publish-on-start is off
========================

Two conveniences the service offers — publishing its command list to
Telegram's own UI, and announcing on start-up that the bot is now running —
are both turned off (``publish_commands = false``, ``announce_start =
false``). Both would open a second TLS session the instant the polling
connection comes up, while the first one's buffers are still held; on a
station whose heap also carries the radio modem's DMA buffers that second
session does not fit, and the only visible effect would be a pair of errors
in the log at every start-up. Neither commands nor the start-up notice are
lost in any functional sense — every command works whether or not Telegram
has been told about it, and the *Telegram* page's live status already shows
an administrator that the bot is up.

One TLS session, shared by a batch
==================================

The service keeps two HTTP client handles, one for the long poll and one for
outgoing requests, and only ever holds a single live TLS session between them.
A handshake needs its record buffers in contiguous blocks that a station also
running the radio modem, the Wi-Fi stack and the web server cannot supply
twice over, so an outgoing request releases the polling session before it
opens its own, and the next polling cycle takes it back afterwards.

Releasing per message would make a station with routing on pay for that trade
continuously: every routed line would cost a handshake on the way out and
another on the way back into the poll. Sends that belong together are
therefore grouped into a *transmit batch*. The polling session is released
once, by the first message of the group, and every further message reuses the
session it opened, so a whole queue drain - a bulletin fanned out to eight
users, an administrator and four group chats included - costs one handshake
rather than one per recipient. A polling cycle falling due while a batch is
still in flight waits for it to close, up to eight seconds, before reclaiming
the connection, so a fan-out is never cut in half by the poll; past that bound
the poll takes its connection back anyway, because a send stuck on an
unresponsive socket must not stop the bot from reading its updates.

Batching applies to the routed-notification drain, to an alert cycle and to
``telegram_broadcast()``. A send issued on its own is a group of one: it
releases the polling session, opens its own and leaves it for the next poll
to reclaim.

Routing station messages to Telegram
====================================

The *Telegram* page's "Route Station messages" switch, next to the bot's own
enable switch and above the "Route Bulletins" switch described below,
delivers APRS messages to their addressee's own Telegram chat, so an operator
reads what was sent to them on their phone without opening the *Snd/Rcv Msg*
page.

Both origins are routed on the same terms. A message received over the air or
from the APRS-IS feed is routed as it is decoded, and a message this station
originates itself from the *Snd/Rcv Msg* page is routed as it is transmitted,
so sending to a callsign listed on the *Telegram* page puts the line in that
user's chat as well as on the air. What is routed in the second case is the
text that actually went out, after the sanitising the outgoing path applies,
and without the message-number suffix, which addresses the radio protocol
rather than the person reading the line. Only the first transmission is
routed: the automatic retries carry the same text to the same addressee and
would otherwise arrive as duplicates.

The addressee set is the *Telegram* page's authorized-users table and nothing
else. Each user card carries that operator's own Callsign, and a received
message is delivered to every user whose Callsign matches the message's
addressee exactly, SSID included. This station's own callsign — the *Station*
page's My Callsign — takes no part in the decision, so a message addressed to
a listed user is routed to that user whether or not this station reads the
frame on its own account, and several users sharing one base callsign under
different SSIDs each receive only what was sent to their own. An addressee
matching no user's Callsign is routed to nobody.

An ``ackNN``/``rejNN`` reply carries no readable text and is never routed, and
neither is a message addressed to a group (``ALL``, ``QST``, ``CQ`` or one of
the operator's own group names), since a group name is not a user's callsign.
Routing is independent of what this station does with the same frame
afterwards: the automatic acknowledgement, the ``/status`` alarm pulse and the
*Snd/Rcv Msg* history still apply only to messages addressed to this station's
own callsign. It is also independent of the *Message* page's own enable
switch. Telegram routing is a consumer of the received frame in its own right,
so an incoming message or bulletin is decoded and routed even on a station
that runs the bot with APRS messaging switched off; everything behind that
switch - the acceptance rule, the acknowledgement, the alarm pulse and the
chat history - stays behind it.

A routed message reaches its user as one line:

.. code-block:: text

   msg from <sender callsign> to <addressee callsign> :: <message text>

Delivery only happens when the switch is on, the bot itself is enabled, and
the bot is currently connected; a message that arrives while any of those
does not hold is simply not routed; it is not queued for later delivery once
the bot comes back. ``telegram_app_notify_station_message()`` (declared in
``telegram_app.h``) is the entry point message.c calls; it only formats the
line, looks up the users it belongs to and hands one item per user to a small
queue, since the frame-decoding path that calls it runs on the modem's own
receive task, which carries none of the stack a Telegram network call needs
for its TLS handshake. The same short-lived worker task that performs a
bring-up or a teardown drains that queue through ``telegram_send_message()``
as the last thing it does before it exits.

The queue itself is created when the configuration is applied — that is, at
start-up and on every save of the *Telegram* page — and only for a bot that is
enabled with at least one of the two routing switches on. A station that
leaves both off therefore never allocates it, while a station that turns one
on has the queue in place before the first line can be routed, so neither
notify entry point ever allocates anything on the task that calls it.

Routing bulletins to Telegram
=============================

The *Telegram* page's "Route Bulletins" switch, directly under the one above,
delivers APRS bulletins to Telegram. A bulletin is addressed to the
whole network rather than to any station, so there is no addressee to match
and no callsign to select a recipient by: every bulletin goes to every
authorized user of the users table, to the administrator when one is
configured, and to every allowed group chat. An administrator who is also
listed in the users table receives one copy, not two.

A bulletin is recognised by its addressee, per APRS101 chapter 14: ``BLN``
followed by a single digit is a general bulletin, ``BLN`` followed by a single
upper-case letter is an announcement, and either may carry a group name of up
to five further characters (``BLN1``, ``BLNA``, ``BLN1WX``). Every source
counts: a bulletin heard off the air, one that arrived from the APRS-IS feed,
and one this station transmits itself are routed alike, so the operators
reading the bot see the station's own announcements in the same chat and in
the same form as everyone else's.

A bulletin this station originates is routed by the scheduler that transmits
it, once per transmit pass rather than once per channel, so a bulletin sent
over both RF and the internet still reaches each chat once. It is routed
whether or not either transmission succeeded, because what travels to
Telegram is the announcement rather than a report on the radio, and it carries
the on-air text, no-archive marker included, rather than the stored draft. A
bulletin slot with neither RF nor Internet ticked is not transmitted at all
and so is not routed either.

A bulletin reaches its recipients as one line:

.. code-block:: text

   bulletin from <sender callsign> to <bulletin addressee> :: <bulletin text>

Bulletins repeat, which is what makes them bulletins: the originator
retransmits on a timer, every digipeater within earshot repeats what it hears,
and an igated copy comes back from APRS-IS as well. A bulletin whose sender,
addressee and text match one routed within the *Telegram* page's "Bulletin
repeat window" is therefore dropped instead of being sent again, so a periodic
bulletin reaches each chat once rather than filling it with copies of itself.
Editing the text, or a different station sending it, makes it a new bulletin
and it is routed at once. The eight most recently routed bulletins are
remembered, as a hash of those three fields and the time they were seen,
whatever the window is set to. This is also what keeps one of this station's
own bulletins to a single copy when the digipeated frame comes back within the
window: it carries the same sender, addressee and text, so the returning copy
is recognised as the repeat it is. The addressee is compared
with its trailing blanks removed, because the two entry points do not spell it
alike - the scheduler hands over the nine-character space-padded field it just
put on the air (``BLN1     ``), the frame decoder the trimmed one (``BLN1``) -
and without that the returning copy would be a different bulletin.

The window is armed by a delivery, never by an attempt. A bulletin that could
not be handed over leaves it untouched and is routed on its next transmission
instead, which matters most on the shortest interval the *Bulletins* page
allows: a bulletin repeating every 30 s against the default window would
otherwise lose its next twenty-nine transmissions to a single arming that no
delivery followed, and lose all of them while whatever blocked the first one
persists.

The window is the field directly under the switch, in seconds, from 0 to
86400 (24 h), and it defaults to 900 s. Set it longer than the interval the
bulletins heard on the channel are transmitted at, so each one reaches the
chats once per edit rather than once per transmission; a station whose
neighbours repeat their bulletins every ten minutes wants more than 600 s
here. Setting it to 0 turns the test off altogether and routes every copy,
including the ones that come back through digipeaters and from the APRS-IS
feed, which is what an operator watching retransmissions on a congested
channel wants and what nobody reading a chat does. The value lives in
``bulletinWindowSeconds`` and takes effect on save, without a reboot.

Delivery is bound by the same three conditions as a routed station message -
the switch on, the bot enabled, the bot connected - and nothing is queued for
later delivery when one of them does not hold. Which of them is missing, or
which other reason applied, is written to the log whenever it changes rather
than once per bulletin: a chat that stays quiet says why once, at the moment
the reason starts and again at the moment it ends, instead of either in
silence or on every repeat.
``telegram_app_notify_bulletin()`` (declared in ``telegram_app.h``) is the
entry point message.c calls for a received bulletin and bulletins.c for one of
this station's own, both under the same reasoning about the calling task's
stack. It queues one item for
the whole bulletin rather than one per recipient: the item carries the text
once and is fanned out by the worker task on its drain pass, which is also
where the recipient list is read, so a save landing between the two is
reflected in the delivery.

A drain is asked for whenever a line is queued, and that request is refused
while the bot's single short-lived worker task is already running a
bring-up, a teardown or another drain. The question is therefore put again at
the point where the worker hands its slot back, so a refused drain is a
deferred one rather than a lost one: a bulletin queued while the bot was
being rebuilt leaves as soon as the rebuild finishes, without waiting for a
further line that the duplicate window would have suppressed anyway.

The fan-out runs as one transmit batch, so the whole recipient list shares the
single TLS session described above rather than paying a handshake per chat,
and it is bounded by the number of recipients the store itself can name -
eight users, four group chats and the administrator - so a fan-out holds that
session for a length the firmware fixes rather than a configuration. Each pass
reports at INFO how many recipients it reached and how long it took, which is
what tells an operator reading the log that a burst of bulletins, rather than
something else, is what is spending the heap.

Built-in commands
==================

``telegram_service`` registers one shared command set on every device that
embeds it; this firmware does not add commands, sensors or remote parameters
of its own on top of it.

.. list-table::
   :header-rows: 1
   :widths: 20 12 68

   * - Command
     - Access
     - What it does
   * - ``/start``
     - anyone
     - Greets the bot and shows its entry points.
   * - ``/help``
     - anyone
     - Lists the available commands.
   * - ``/status``
     - anyone
     - Reports which of this firmware's services are on or off (IGate,
       digipeater, tracker, weather, telemetry, messaging, query responder,
       BrandMeister, GNSS receiver, AFSK modem, TX duty-cycle limiter, SNTP
       sync) and the current free heap.
   * - ``/sensors``
     - anyone
     - Reports every weather field and telemetry channel that is both
       enabled and mapped to a sensor channel, with its value or "no
       reading". Enabled fields with no sensor mapped are omitted.
   * - ``/uptime``
     - anyone
     - Reports how long the device has been up.
   * - ``/whoami``
     - anyone
     - Reports the caller's own user and chat identifiers — the values to
       hand-add to ``telegram.json``'s user or chat list.
   * - ``/menu``
     - anyone
     - Shows the button interface (Telegram inline keyboard) for the
       commands above.
   * - ``/stats``
     - anyone
     - Reports the service's own counters: updates received, commands
       handled, messages sent, rejected updates, poll errors.
   * - ``/config``, ``/get``, ``/set``
     - admin only
     - List, read and change remotely-registered parameters. This firmware
       registers none, so these list nothing at present but stay available
       for a future driver or feature to register one.
   * - ``/users``
     - admin only
     - Lists the authorized users.
   * - ``/alerts``
     - admin only
     - Enables or disables the service's push alerts to the administrator.
   * - ``/reboot``
     - admin only, disabled here
     - Present in the library; not registered by this firmware (see above).

"Admin only" means the caller's identifier must match ``admin_id`` or one of
the authorized users added with a non-default admin flag; anyone else's
attempt is counted as a rejected update rather than answered.

Who may talk to the bot
========================

Authorization is a list, and the list is closed. A sender is accepted only
when the identifier is on it — seeded from ``admin_id`` by ``telegram_init()``
and extended with the authorized users from ``telegram.json`` — or when the
service was built to run in open-access mode, which this firmware never
enables. An empty list is not an exception to that rule: it denies everyone.

That matters for a station whose token is configured before its
administrator is, which is the normal case for a cloned image or a
``telegram.json`` written once and copied to several devices. A list that
opened itself while it was empty would leave such a device answering the
first stranger who found the bot, for as long as the identifier field stayed
at 0.

Closing the list does not make the first identifier harder to find, because
discovery never depended on being let in. A private command from an
unlisted sender is refused with a reply naming that sender's own numeric
identifier, and the same number is written to the log; entering it as the
administrator identifier on the *Telegram* page is the whole bring-up. While
nobody is authorized, the page says so under the credentials and the service
notes it once in the log when polling starts, so a bot that answers nobody
is never mistaken for a bot that cannot connect.

The Telegram page
==================

``GET``/``POST /telegram`` (:ref:`en-http-routes`) exposes every field of
``telegram.json``: the enable switch, the bot token (masked, with a *show
password* toggle), the administrator identifier, the Mini App address, and
the authorized-user and allowed-group-chat tables. Below the save form, a
live status table (``GET /telegram/status``, JSON, polled every 2 seconds)
shows the coarse state, the precise reason, any untranslated detail Telegram
or the network stack returned, the bot's own username once known, its
uptime, and its counters.

The authorized-user and allowed-group-chat tables are fixed-size — up to 8
users and 4 group chats, matching ``TELEGRAM_APP_USERS_MAX`` and
``TELEGRAM_APP_CHATS_MAX`` — and each entry is rendered as a collapsible card
with an identifier field and a display-name field. An account already on the
list reads its own identifier with ``/whoami``; one that is not gets the same
number back in the refusal the bot answers any command with, and a group's
identifier comes from ``/whoami`` sent inside the group. Leaving a card's
identifier empty (or at 0) leaves
that slot unused, and a save compacts the table so a cleared slot in the
middle does not leave a gap behind.

Everything the page renders is still the same ``telegram.json`` described
above, so it can also be downloaded, edited by hand and uploaded again from
the *File Storage* page — useful for editing several entries at once, or for
restoring a known-good configuration.

.. seealso::

   :ref:`en-web-admin` — the rest of the web admin's pages and routes.

   :ref:`en-storage-ota` — how ``telegram.json`` and the certificate file are
   stored and how the File Storage page's upload/download lets an operator
   edit the parts the Telegram page does not.
