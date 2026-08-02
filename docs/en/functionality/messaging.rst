.. _en-messaging:

==============
APRS Messaging
==============

The ``message`` component (``components/message/``) implements APRS messaging
with acknowledgement and retry, optional AES encryption, and routing over RF
and/or APRS-IS. The web admin exposes two distinct pages: the **Message** page
*configures* the engine (RF/INET enable, retry, encryption), while the
**Snd/Rcv Msg** page (``/msgchat``) is the actual inbox/compose UI.

The message engine
==================

* **In-RAM queue.** ``s_queue[]`` holds up to ``MSG_QUEUE_SIZE`` (20) entries,
  shared by **received and outbound** messages alike — each entry carries an
  ``rxtx`` flag saying which it is. ``MSG_TEXT_MAX`` (200) is the in-memory
  storage cap for an entry's text; the on-air protocol limit is the separate
  ``APRS_MSG_TEXT_STD_MAX`` (67 characters), which is what the compose box and
  the query responder validate against, so a full ``":ADDRESSEE:text{id"``
  information field stays inside the classic 256-byte TNC2 budget.
* **Send / ack / retry.** ``sendAPRSMessage()`` queues a message,
  ``sendAPRSAck()`` replies to a received one, and ``sendAPRSMessageRetry()`` —
  ticked at 1 Hz by the APRS service's tick task — re-sends any message whose
  acknowledgement has not yet arrived, up to ``msg_retry`` times at
  ``msg_interval`` seconds.
* **Incoming parse.** ``handleIncomingAPRS()`` parses any TNC2 line — from RF
  *or* from APRS-IS — recognises messages addressed to this station, replies
  with an ack, and recognises inbound acks (``ackNNN``) to clear the matching
  queued message.

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
``/msgchat/list`` (a JSON fragment). It is gated by the ``ENABLE_MSG_CHAT``
compile-time switch.

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
