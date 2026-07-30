.. _it-messaging:

==================
Messaggistica APRS
==================

Il componente ``message`` (``components/message/``) implementa la messaggistica
APRS con conferma e ritentativo, cifratura AES opzionale, e instradamento in RF
e/o APRS-IS. L'amministrazione web espone due pagine distinte: la pagina
**Message** *configura* il motore (abilitazione RF/INET, ritentativo, cifratura),
mentre la pagina **Snd/Rcv Msg** (``/msgchat``) è l'interfaccia di
casella/composizione vera e propria.

Il motore dei messaggi
======================

* **Coda in RAM.** Fino a ``MSG_QUEUE_SIZE`` (20) messaggi in uscita, ciascuno
  fino a 200 caratteri di testo, sono mantenuti in ``s_queue[]``.
* **Invia / ack / ritentativo.** ``sendAPRSMessage()`` accoda un messaggio,
  ``sendAPRSAck()`` risponde a uno ricevuto, e ``sendAPRSMessageRetry()`` —
  invocata a 1 Hz dal task di tick del servizio APRS — reinvia qualsiasi
  messaggio la cui conferma non è ancora arrivata, fino a ``msg_retry`` volte ogni
  ``msg_interval`` secondi.
* **Analisi degli entranti.** ``handleIncomingAPRS()`` analizza qualsiasi riga
  TNC2 — da RF *o* da APRS-IS — riconosce i messaggi diretti a questa stazione,
  risponde con un ack, e riconosce gli ack entranti (``ackNNN``) per pulire il
  messaggio accodato corrispondente.

Instradamento
=============

Ogni messaggio è instradato tramite una maschera di bit di canale via un gestore
TX registrato:

* ``MSG_CHANNEL_RF`` (``1 << 0``) → ``aprs_service_send_tnc2()`` (ramo RF).
* ``MSG_CHANNEL_INET`` (``1 << 1``) → ``igate_send_raw()`` (ramo APRS-IS).

``g_config.msg_rf`` e ``g_config.msg_inet`` decidono quali rami sono attivi.

L'interfaccia di chat messaggi (``/msgchat``)
=============================================

La pagina ``/msgchat`` presenta un pannello scorrevole di messaggi inviati e
ricevuti da questa stazione, un campo di indicativo di destinazione, una casella
di testo del messaggio (limitata alla lunghezza del messaggio APRS) e un pulsante
di invio. Aggiorna la sua lista di messaggi tramite ``/msgchat/list`` (un
frammento JSON). È condizionata dall'interruttore di compilazione
``ENABLE_MSG_CHAT``.

GPIO di allarme messaggio
=========================

Opzionalmente (``msg_alarm_enable``), un messaggio entrante può attivare un GPIO
(``msg_alarm_gpio``; ``-1`` = disabilitato), validato da
``message_alarm_gpio_is_valid()`` — utile per accendere un LED o far suonare un
cicalino alla ricezione di un messaggio.
