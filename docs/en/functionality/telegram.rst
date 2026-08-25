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
     - Reports every enabled weather field (value and mapped sensor channel,
       or "no reading"/"no sensor mapped") and every enabled telemetry
       channel.
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
