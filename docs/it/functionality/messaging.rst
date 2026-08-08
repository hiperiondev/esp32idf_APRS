.. _it-messaging:

==================
Messaggistica APRS
==================

Il componente ``message`` (``components/message/``) implementa la messaggistica
APRS con conferma e ritentativo, e instradamento in RF e/o APRS-IS.
L'amministrazione web espone due pagine distinte: la pagina **Message**
*configura* il motore (abilitazione RF/INET, ritentativo, GPIO di allarme),
mentre la pagina **Snd/Rcv Msg** (``/msgchat``) è l'interfaccia di
casella/composizione vera e propria.

Il motore dei messaggi
======================

* **Coda in RAM.** ``s_queue[]`` contiene fino a ``MSG_QUEUE_SIZE`` (10) voci,
  condivise dai messaggi **ricevuti e in uscita** allo stesso modo — ogni voce
  porta un flag ``rxtx`` che dice quale sia. La coda *è* la conversazione: un
  messaggio inviato e uno ricevuto occupano ciascuno una propria casella e la
  mantengono finché traffico più recente non li spinge fuori, e una volta
  occupate tutte e dieci le caselle, memorizzare il messaggio successivo scarta
  la voce più vecchia del filo, RX o TX che sia. ``MSG_TEXT_MAX`` (200) è il
  tetto di memorizzazione in memoria del testo di una voce; il limite di
  protocollo in onda è il separato ``APRS_MSG_TEXT_STD_MAX`` (67 caratteri),
  contro cui validano la casella di composizione e il risponditore di query,
  così che un campo informativo ``":ADDRESSEE:testo{id"`` completo resti entro
  il classico budget TNC2 di 256 byte.
* **Ordine della conversazione.** Ogni messaggio memorizzato porta un contatore
  di inserimento (``msg_entry_t::seq``) assegnato una sola volta e mai
  modificato, ed è quel contatore — non l'orologio di sistema — a ordinare il
  filo e a scegliere la voce da scartare. Un messaggio in uscita mantiene quindi
  il posto in cui è stato scritto per tutto il tempo in cui viene ritentato
  (``last_tx`` porta la pianificazione dei ritentativi, ``time`` resta il
  momento della creazione), e l'ordine sopravvive a un salto dell'orologio di
  sistema come la prima sincronizzazione NTP dopo l'avvio.
* **Invia / ack / ritentativo.** ``sendAPRSMessage()`` accoda un messaggio,
  ``sendAPRSAck()`` risponde a uno ricevuto, e ``sendAPRSMessageRetry()`` —
  invocata a 1 Hz dal task di tick del servizio APRS — reinvia qualsiasi
  messaggio la cui conferma non è ancora arrivata, fino a ``msg_retry`` volte ogni
  ``msg_interval`` secondi.
* **Risposta a ``?APRSM``.** ``message_send_pending_to()`` ritrasmette ciò che
  questa stazione trattiene per l'operatore che interroga, senza consumare
  nessuno dei ritentativi propri del messaggio né spostarne il successivo, e si
  ferma dopo ``MSG_QUERY_BURST_MAX`` (3) trame: una query diretta vale una
  manciata di trame, non un'intera coda con il trasmettitore attivo. Ciò che il
  limite lascia fuori resta pendente, quindi la passata di ritentativi qui sopra
  continua a consegnarlo a distanza di un ``msg_interval``.
* **Analisi degli entranti.** ``handleIncomingAPRS()`` analizza qualsiasi riga
  TNC2 — da RF *o* da APRS-IS — riconosce i messaggi diretti a questa stazione,
  risponde con un ack, e riconosce gli ack entranti (``ackNNN``) per pulire il
  messaggio accodato corrispondente. Ogni messaggio accettato è una nuova riga
  della conversazione e riceve una propria casella, compresi quelli che non
  portano alcun ``{id`` e quelli il cui numero la stazione mittente ha già usato
  in precedenza (la numerazione riparte quando quella stazione si riavvia).
  L'unica riga che non occupa una casella è una ritrasmissione che la coda già
  contiene — stesso mittente, stesso numero di messaggio, testo identico — a cui
  si risponde con un ack nuovo, perché un ripetuto significa che il mittente non
  ha mai sentito il primo, senza comparire due volte nella cronologia. Gli
  indicativi di origine sono normalizzati in maiuscolo durante l'analisi, così
  una stazione occupa un solo nome nel filo e un ack si abbina al messaggio in
  uscita che conferma.

Reply-ACK
=========

L'algoritmo Reply-ACK (APRS 1.1, ``aprs11/replyacks.txt``) incorpora una
conferma nel numero di riga di un messaggio ordinario, così che una risposta
funga anche da ack di ciò a cui risponde. È quella che l'addendum chiama la
maggiore vincita di affidabilità disponibile nella messaggistica APRS: gli ack
da estremo a estremo devono sopravvivere al percorso di ritorno, e su due hop un
canale al 70 % dà a un messaggio solo circa il 25 % di probabilità di essere
confermato.

* **In uscita.** Ogni messaggio è numerato ``{MM}`` o ``{MM}AA``, dove ``MM`` è
  il numero proprio di questa stazione e ``AA`` la conferma dovuta al
  destinatario. Il suffisso è costruito da ``buildMsgNumberSuffix()`` nell'istante
  in cui la trama viene composta — non quando il messaggio viene accodato —
  quindi una riprova porta ciò che è dovuto in quel momento e non ciò che era
  dovuto quando l'operatore l'ha scritto. Senza nulla da confermare il numero è
  ``{MM}``, e la sua parentesi finale è ciò che dice all'altro capo che può
  rispondere con un Reply-ACK.
* **In arrivo.** Un numero scritto ``MM}AA`` viene diviso in due. ``AA`` viene
  confrontato con la coda in uscita e marca quel messaggio come confermato, senza
  che debba arrivare alcun ``ackNN`` separato. ``MM`` identifica il messaggio
  ricevuto — così lo stesso messaggio sentito due volte con due conferme gratuite
  diverse resta una sola riga della conversazione — ed è memorizzato come la
  conferma ora dovuta a quella stazione, pronta a partire con il prossimo
  messaggio inviato ad essa.
* **L'ack ordinario torna comunque indietro**, citando l'identificatore esattamente
  come è arrivato: un messaggio numerato ``MM}AA`` è confermato con ``ackMM}AA``.
  Al contrario, un ``ackMM}AA`` in arrivo è abbinato solo su ``MM``, perché la
  parte dopo la parentesi è la conferma gratuita di questa stessa stazione
  rimandata indietro e non conferma nulla.
* **Numerazione.** I numeri in uscita vanno da 1 a ``MSG_ID_MAX`` (99) e
  ricominciano, senza mai passare per 0. Due cifre sono ciò che mantiene un
  identificatore ``{MM}AA`` completo entro i cinque caratteri ammessi dal
  capitolo 14 di APRS101, e non ci sono mai più di ``MSG_QUEUE_SIZE`` messaggi
  pendenti alla volta.
* **Stato.** La conferma dovuta è tenuta per corrispondente, per
  ``MSG_REPLY_ACK_STATIONS`` (5) stazioni, riutilizzando oltre quel numero la voce
  aggiornata meno di recente. Una stazione che perde la sua voce perde soltanto
  il passaggio gratuito: l'ack ordinario le è già stato inviato quando è arrivato
  il suo messaggio.

L'intero meccanismo è trasparente per il software che non lo implementa, che
legge ``{MM}AA`` come un identificatore di messaggio ordinario e lo conferma per
intero.

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
frammento JSON), che restituisce l'intero filo memorizzato, dal più vecchio al
più recente. È condizionata dall'interruttore di compilazione
``ENABLE_MSG_CHAT``.

Il pannello si legge come una sola conversazione, con i messaggi inviati e
ricevuti alternati nell'ordine in cui sono avvenuti e il più recente in basso.
Il suo script misura le bolle dei messaggi una volta impaginate e dimensiona il
pannello sulle ``MSGCHAT_VISIBLE_MESSAGES`` (5) più recenti, così cinque
messaggi sono visibili senza scorrere e il resto del filo memorizzato — fino a
``MSG_QUEUE_SIZE`` (10) messaggi — dista uno scorrimento. La misura viene
ripetuta quando la finestra cambia dimensione, perché un testo più stretto va a
capo su più righe e rende le bolle più alte. Lo scorrimento segue la
conversazione solo finché l'operatore è già in fondo ad essa, così un messaggio
in arrivo non strappa mai il pannello dalle righe più vecchie che si stanno
leggendo.

Entrambi i numeri sono costanti di compilazione: ``MSGCHAT_VISIBLE_MESSAGES`` in
``page_msgchat.c`` decide soltanto quanto è alto il pannello, mentre
``MSG_QUEUE_SIZE`` in ``message.h`` decide quanta conversazione conserva il
firmware.

.. seealso::

   :ref:`it-query` — il risponditore di query condivide il gestore di TX di questo
   componente ed è raggiunto da ``handleIncomingAPRS()`` quando il testo di un
   messaggio indirizzato inizia con ``?``.

GPIO di allarme messaggio
=========================

Opzionalmente (``msg_alarm_enable``), un messaggio entrante può attivare un GPIO
(``msg_alarm_gpio``; ``-1`` = disabilitato), validato da
``message_alarm_gpio_is_valid()`` — utile per accendere un LED o far suonare un
cicalino alla ricezione di un messaggio.
