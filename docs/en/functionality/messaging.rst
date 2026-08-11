.. _en-messaging:

==============
APRS Messaging
==============

The ``message`` component (``components/message/``) implements APRS messaging
with acknowledgement and retry, and routing over RF and/or APRS-IS. The web
admin exposes two distinct pages: the **Message** page *configures* the engine
(RF/INET enable, retry, alarm GPIO), while the
**Snd/Rcv Msg** page (``/msgchat``) is the actual inbox/compose UI.

The message engine
==================

* **In-RAM queue.** ``s_queue[]`` holds up to ``MSG_QUEUE_SIZE`` (10) entries,
  shared by **received and outbound** messages alike — each entry carries an
  ``rxtx`` flag saying which it is. The queue *is* the conversation: a message
  sent and a message received each take a slot of their own and keep it until
  newer traffic pushes them out, and once all ten slots are taken, storing the
  next message discards the oldest entry of the thread, RX or TX alike.
  ``MSG_TEXT_MAX`` (200) is the in-memory storage cap for an entry's text; the
  on-air protocol limit is the separate ``APRS_MSG_TEXT_STD_MAX`` (67
  characters), which is what the compose box and the query responder validate
  against, so a full ``":ADDRESSEE:text{id"`` information field stays inside the
  classic 256-byte TNC2 budget.
* **Conversation order.** Each stored message is stamped with an insertion
  counter (``msg_entry_t::seq``) that is assigned once and never changes, and
  that counter — not the wall clock — is what orders the thread and picks the
  entry to discard. An outbound message therefore keeps the place it was written
  in for as long as it is retried (``last_tx`` carries the retry schedule,
  ``time`` stays the moment the message was created), and the order survives a
  system-clock step such as the first NTP sync after boot.
* **Send / ack / retry.** ``sendAPRSMessage()`` queues a message,
  ``sendAPRSAck()`` replies to a received one, and ``sendAPRSMessageRetry()`` —
  ticked at 1 Hz by the APRS service's tick task — re-sends any message whose
  acknowledgement has not yet arrived, up to ``msg_retry`` times at
  ``msg_interval`` seconds.
* **Answering ``?APRSM``.** ``message_send_pending_to()`` re-sends what this
  station is holding for the querying operator without spending any of a
  message's own retries or moving its next retry, and stops after
  ``MSG_QUERY_BURST_MAX`` (3) frames — one directed query is worth a handful of
  frames, not a whole keyed-up queue. Whatever the cap leaves behind is still
  pending, so the retry pass above keeps delivering it one ``msg_interval``
  apart.
* **Incoming parse.** ``handleIncomingAPRS()`` parses any TNC2 line — from RF
  *or* from APRS-IS — recognises messages addressed to this station or to a
  message group it reads, replies with an ack for a direct message, and
  recognises inbound acks (``ackNNN``) to clear the matching queued message.
  Each accepted message is a new line of the conversation and gets its own
  slot, including messages that carry no ``{id`` at all and messages whose
  number the sending station has already used before (numbering restarts when
  that station reboots). The one line that does not take a slot is a
  retransmission the queue already holds — same sender, same message number,
  identical text, same direct/group status — which is answered with a fresh
  ack when it was a direct message, since a repeat means the sender never
  heard the first one, without appearing twice in the history. Sender
  callsigns are normalised to upper case as they are parsed, so one station
  occupies one name in the thread and an ack pairs with the outbound message
  it acknowledges.

Message groups
==============

Per APRS101 chapter 14, "Message Groups", a receiving station reads every
message addressed to ``ALL``, ``QST`` or ``CQ`` — the built-in group set — in
addition to its own callsign, plus any locally configured group name, such as
the addressee an APRS net or round-table uses instead of each participant's
own callsign. ``ALL``, ``QST`` and ``CQ`` need no configuration and are always
read; up to ``MSG_USER_GROUPS`` (3) operator-defined names are set on the
**Message** page's "Message Groups" fieldset, one text field per slot, the
same repeated-slot pattern the Bulletins page uses. A group name is compared
whole and case-insensitively, with no ``-SSID`` stripping (a group is not a
callsign).

* **Reading, never acknowledging.** A message addressed to a group is stored
  in the queue and shown in the ``/msgchat`` panel exactly like a direct
  message. It is, however, never acknowledged, never retransmitted and never
  auto-replied to, whether or not it carries a ``{id`` suffix: a group has no
  single owner to send an ``ackNN`` back, and every member reading the group
  would otherwise answer the sender at once. ``handleIncomingAPRS()`` routes
  every "send an ack" / "remember a Reply-ACK owed" / "pulse the Message
  Alarm" decision on whether the addressee matched this station's own
  callsign exactly, never on acceptance alone — an ``ack``/``rej`` line
  addressed to a group is likewise ignored, since this station never sends an
  outbound message to a group for one to acknowledge.
* **Separate history slots.** A group message and a direct message are kept
  apart in the queue even when they happen to share the same sender and the
  same APRS message number — the duplicate-detection and storage keys the
  sender's callsign and message number against the message's direct/group
  status, so the two never collide into one slot or overwrite one another.

Reply-ACK
=========

The Reply-ACK algorithm (APRS 1.1, ``aprs11/replyacks.txt``) embeds an
acknowledgement in the line number of an ordinary message, so a reply doubles as
the ack for what it replies to. It is what the addendum calls the single largest
reliability win available in APRS messaging: end-to-end acks have to survive the
return path, and over two hops a 70 % channel gives a message only about a 25 %
chance of being acknowledged.

* **Outgoing.** Every message is numbered ``{MM}`` or ``{MM}AA``, where ``MM``
  is this station's own number and ``AA`` the acknowledgement owed to the
  addressee. The suffix is built by ``buildMsgNumberSuffix()`` at the instant a
  frame is assembled — not when the message is queued — so a retry carries
  whatever is owed by then rather than what was owed when the operator wrote it.
  With nothing owed the number is ``{MM}``, whose trailing brace is what tells
  the other end that a Reply-ACK can be sent back.
* **Incoming.** A number written ``MM}AA`` is split in two. ``AA`` is matched
  against the outbound queue and marks that message acknowledged, without any
  separate ``ackNN`` ever having to arrive. ``MM`` identifies the received
  message — so the same message heard twice with two different free
  acknowledgements is still one line of the conversation — and is remembered as
  the acknowledgement now owed to that station, ready to ride out on the next
  message sent to it.
* **The ordinary ack still goes back**, quoting the identifier exactly as it
  arrived: a message numbered ``MM}AA`` is acknowledged with ``ackMM}AA``.
  Conversely, an incoming ``ackMM}AA`` is matched on ``MM`` alone, because the
  part after the brace is this station's own free acknowledgement quoted back
  and acknowledges nothing.
* **Numbering.** Outgoing numbers run 1 to ``MSG_ID_MAX`` (99) and wrap, never
  reaching 0. Two digits are what keeps a full ``{MM}AA`` identifier inside the
  five characters APRS101 chapter 14 allows, and only ``MSG_QUEUE_SIZE``
  messages are ever outstanding at once.
* **State.** The acknowledgement owed is kept per correspondent, for
  ``MSG_REPLY_ACK_STATIONS`` (5) stations, the least recently updated entry being
  reused beyond that. A station that loses its entry simply loses the free ride:
  the ordinary ack was already sent to it when its message arrived.

The whole mechanism is transparent to software that does not implement it, which
reads ``{MM}AA`` as an ordinary message identifier and acknowledges it whole.

Routing
=======

Each message is routed by a channel bitmask via a registered TX handler:

* ``MSG_CHANNEL_RF`` (``1 << 0``) → ``aprs_service_send_tnc2()`` (RF leg).
* ``MSG_CHANNEL_INET`` (``1 << 1``) → ``igate_send_raw()`` (APRS-IS leg).

``g_config.msg_rf`` and ``g_config.msg_inet`` decide which legs are active.

The message-chat UI (``/msgchat``)
==================================

The ``/msgchat`` page presents a scrolling panel of sent and received messages
for this station, a destination-callsign field, a message-text box (capped at
the APRS message length) and a Send button. It refreshes its message list via
``/msgchat/list`` (a JSON fragment), which returns the whole stored thread,
oldest first. It is gated by the ``ENABLE_MSG_CHAT`` compile-time switch.

The panel reads as one conversation, sent and received messages interleaved in
the order they happened with the newest at the bottom. Its script measures the
message bubbles once they are laid out and sizes the panel to the newest
``MSGCHAT_VISIBLE_MESSAGES`` (5) of them, so five messages are visible without
scrolling and the rest of the stored thread — up to ``MSG_QUEUE_SIZE`` (10)
messages — is one scroll back. The measurement is repeated when the window is
resized, because narrower text wraps over more lines and makes the bubbles
taller. Scrolling follows the conversation only while the operator is already at
the bottom of it, so an arriving message never yanks the panel away from older
lines being read.

Both numbers are compile-time constants: ``MSGCHAT_VISIBLE_MESSAGES`` in
``page_msgchat.c`` decides only how tall the panel is, while ``MSG_QUEUE_SIZE``
in ``message.h`` decides how much conversation the firmware keeps.

.. seealso::

   :ref:`en-query` — the query responder shares this component's TX handler and
   is reached from ``handleIncomingAPRS()`` whenever an addressed message's text
   starts with ``?``.

Message alarm GPIO
==================

Optionally (``msg_alarm_enable``), an incoming message can drive a GPIO
(``msg_alarm_gpio``; ``-1`` = disabled), validated by
``message_alarm_gpio_is_valid()`` — useful to light an LED or sound a buzzer on
message receipt.
