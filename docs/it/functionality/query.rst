.. _it-query:

===============================
Risponditore di query APRS
===============================

Il componente ``query`` (``components/query/``) risponde alle query APRS definite
nel capitolo 15 di APRS101. Riconosce le query **generali** (broadcast) che
viaggiano nel normale traffico ricevuto e le query **dirette** inviate a questa
stazione come messaggi APRS, costruisce la risposta corrispondente e la consegna
alla stessa infrastruttura di TX RF/APRS-IS usata dal motore di messaggistica.
Tutto ciò che fa è condizionato da ``g_config.query_en`` e dal resto della pagina
*Query* dell'amministrazione web.

Due punti di ingresso
=====================

* ``query_process(tnc2Line, source)`` — riceve ogni riga TNC2 decodificata. Non
  fa nulla a meno che ``query_en`` sia attivo, l'interruttore di ``source`` sia
  acceso e il campo informativo inizi con ``?``, che è ciò che rende una riga una
  query **generale**. Riconoscere la parola chiave, applicare la limitazione di
  frequenza e accodare la richiesta è tutto ciò che fa.
* ``query_process_directed(fromCall, toCall, text, tnc2Line, source)`` — chiamata
  da ``handleIncomingAPRS()`` di ``message.c`` quando il testo di un messaggio
  indirizzato inizia con ``?``, così l'analisi di ``:ADDRESSEE:`` non viene
  duplicata qui. Non fa nulla a meno che ``query_en`` **e** ``query_directed_en``
  siano attivi, l'interruttore di ``source`` sia acceso e ``toCall`` corrisponda a
  ``g_config.aprs_mycall`` (nominativo base, senza distinguere l'SSID).

Sorgente e canale
=================

Entrambi i punti di ingresso sanno da dove è arrivata la query —
``QUERY_SRC_RF`` per la radio, ``QUERY_SRC_INET`` per il flusso APRS-IS — e
quella sorgente decide due cose.

Decide **se la query viene risposta**: ``g_config.query_rf`` e ``query_inet``
nominano una sorgente, non una destinazione. E decide **dove va la risposta**:
una domanda ascoltata in onda riceve risposta in onda, una domanda letta dal
flusso riceve risposta verso APRS-IS. Le risposte sono trasmesse tramite il
gestore installato con ``query_set_tx_handler()`` — ``aprs_service.c`` riusa
esattamente lo stesso ``messageTxHandler()`` che fornisce al motore di
messaggistica — e la maschera che riceve porta sempre esattamente uno fra
``MSG_CHANNEL_RF`` / ``MSG_CHANNEL_INET``, quello corrispondente alla sorgente.

Tenere insieme le due cose è ciò che tiene il flusso APRS-IS lontano dal
trasmettitore. ``?APRS?`` è normale traffico di dorsale e un IGate ne vede un
flusso costante; con l'interruttore della sorgente APRS-IS spento — l'impostazione
di fabbrica — nulla di quel traffico raggiunge il risponditore, e con
l'interruttore acceso le risposte tornano verso APRS-IS invece di andare in onda.

Due eccezioni sono deliberate, perché quel traffico non è la risposta in sé.
``?APRSM`` ritrasmette messaggi che questa stazione già deve all'operatore che
interroga — un numero limitato di essi, vedi la tabella più sotto — instradati
dagli indicatori "invia via" della pagina Message;
``?APRSO`` riannuncia gli Oggetti/Elementi, ciascuno instradato dalla propria
configurazione. Solo la risposta "nessun messaggio in sospeso" e quella
``No objects`` seguono la sorgente della query.

Dove viene costruita la risposta
================================

Nessuno dei due punti di ingresso costruisce o trasmette qualcosa. Entrambi
riconoscono la parola chiave, applicano la limitazione di frequenza e
**registrano** la richiesta: il suo tipo, la stazione che interroga e al massimo
un testo — il nominativo su cui chiede un ``?APRSH``, oppure il percorso da cui è
arrivata una ``?APRST``, letto lì per lì mentre la riga ricevuta è ancora a
disposizione. La risposta vera e propria viene costruita e messa in aria da
``query_service()``, che il task dello **scheduler dei beacon** chiama all'inizio
di ogni passata; accodare una richiesta chiama anche
``beacon_scheduler_wake()``, così quella passata avviene subito e non quando
scade il beacon successivo.

Il motivo è lo stack. Una risposta a ``?APRS?`` *è* un beacon: esegue
``beacon_build_igate_position_packet()``, diversi ``snprintf()`` di newlib con
supporto in virgola mobile, ``lat_lon_to_aprs()``, il costruttore del path e poi
l'intera catena ``aprs_service_send_tnc2()`` → ``modem_send_tnc2()`` →
``ax25_encode()``, con ogni livello che impila il proprio buffer da 300–450 byte
— esattamente l'albero di chiamate per cui ``beacon_scheduler.c`` dimensiona il
suo stack da 14336 byte. Le query, però, arrivano su ``modem_svc`` (RF) e
``igate_task`` (APRS-IS), i cui stack sono una frazione di quello. Il lavoro gira
quindi sul task il cui budget lo copre e non su quello che ha ricevuto la
domanda, e i costruttori possono crescere senza obbligare a riverificare quei
due percorsi.

Da dove gira discendono due cose. Rispondere non occupa mai un task RX per la
durata di una raffica di trasmissione — la proprietà che ``?APRSO`` ha sempre
avuto — e la risposta eredita il contesto beacon dello scheduler, quindi
``aprs_service_send_tnc2()`` attende un istante se l'anello di TX RF è pieno
invece di scartare la risposta (vedi :ref:`it-beacons`).

La coda contiene ``QUERY_PENDING_MAX`` (8) richieste, servite dalla più vecchia.
Una richiesta identica a una già in attesa viene fusa con essa — ogni risposta
riporta lo stato vivo nel momento in cui viene inviata, quindi un duplicato
metterebbe in aria due volte la stessa informazione — e a coda piena le
richieste successive vengono scartate con un avviso anziché soppiantare una
risposta già dovuta a qualcuno.

Query generali
==============

.. list-table::
   :header-rows: 1
   :widths: 16 16 68

   * - Query
     - Abilitazione
     - Risposta
   * - ``?APRS?``
     - ``query_aprs_en``
     - Il rapporto di posizione della stazione, costruito con
       ``beacon_build_igate_position_packet()`` — byte per byte ciò che avrebbe
       inviato il beacon di posizione dell'IGate.
   * - ``?WX?``
     - ``query_wx_en``
     - L'ultimo Rapporto Meteo in cache, costruito con
       ``weather_build_report_packet()``. Non viene risposto se non c'è ancora
       una lettura in cache o non è configurato un nominativo WX/APRS.
   * - ``?IGATE?``
     - ``query_igate_en``
     - La riga di Capacità di Stazione definita da APRS101,
       ``<IGATE,MSG_CNT=n,LOC_CNT=n>``, con le due cifre che il capitolo 15
       assegna loro. ``MSG_CNT`` è il conteggio cumulativo dei pacchetti di
       messaggio APRS che questo gateway ha inoltrato in entrambe le direzioni
       (``igate_stats_t::msgCount``, non un totale di tutto il traffico
       inoltrato). ``LOC_CNT`` è una cifra viva e non un cumulativo: il numero di
       stazioni distinte nell'elenco delle ascoltate locali, da
       ``lastheard_station_count(true)``, contando solo le righe il cui frame più
       recente è stato ascoltato in onda. Ignorata in silenzio mentre
       ``igate_en`` è spento.

Poiché le risposte di posizione, stato e meteo riusano i costruttori di beacon
esistenti, una risposta non può mai divergere da ciò che trasmettono i beacon
periodici.

Query dirette
=============

Risposte solo con ``query_directed_en`` attivo. ``?APRSP`` e ``?APRSS`` sono
sempre disponibili in quell'insieme; le restanti, di tipo elenco, richiedono
inoltre *Query dirette estese* (``query_ext_en``).

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Query
     - Risposta
   * - ``?APRSP``
     - Rapporto di posizione (stesso costruttore di ``?APRS?``).
   * - ``?APRSS``
     - Rapporto di stato, byte per byte ciò che invierebbe il beacon di stato
       dell'IGate — incluso il blocco del locatore Maidenhead quando quell'opzione
       della pagina Station è attiva.
   * - ``?APRSD``
     - Le stazioni sentite **dirette** (non via digipeater), come messaggio di
       testo APRS di ritorno a chi ha chiesto. Vengono scartati nominativi interi
       anziché troncare quando l'elenco supererebbe la lunghezza del messaggio in
       onda.
   * - ``?APRSH <call>``
     - Il grafico di 18 ore di ascolto di una stazione: ``Hrd: h0 h1 … h17``, sei
       conteggi per periodo separati da ``.``, con l'ora 0 corrispondente all'ora
       di orologio corrente. L'istogramma vive in ``components/lastheard`` (vedi
       ``lastheard_heard_history()`` e ``LASTHEARD_HEARD_HOURS``). Leggere il
       grafico non è traffico: la risposta porta avanti l'istogramma fino all'ora
       di orologio corrente, così un silenzio trascorso appare come il vuoto che
       è, ma nessuna ora viene conteggiata, e la stessa stazione interrogata più
       volte riporta le stesse cifre. Senza l'argomento nominativo il
       risponditore risponde ``Usage: ?APRSH <call>`` — la formulazione tiene di
       proposito la parola chiave lontana dal primo carattere, poiché un
       messaggio il cui testo inizia con ``?`` è a sua volta una query diretta e
       un peer che esegua un risponditore leggerebbe la risposta come una nuova
       domanda.
   * - ``?APRSM``
     - Reinvia i messaggi pendenti di questa stazione per l'operatore che
       interroga, fino a ``MSG_QUERY_BURST_MAX`` (3) trame per query. Ciò che
       resta in coda mantiene il proprio stato di ritentativo ed esce secondo la
       pianificazione del motore di messaggistica, così una sola domanda non può
       tenere il trasmettitore attivo per un'intera coda.
   * - ``?APRSO``
     - Riannuncia gli Oggetti/Item originati qui. Chiama
       ``objitems_request_transmit_all()`` e, poiché viene servita all'interno
       della passata dello scheduler, gli elementi escono più avanti nella stessa
       passata. Compilato senza ``ENABLE_OBJECTS_ITEMS``, risponde
       ``No objects``.
   * - ``?APRST`` / ``?PING?``
     - Il percorso preso dalla query stessa, letto dalla riga TNC2 ricevuta al
       momento di accodare la richiesta. Senza riga disponibile risponde con
       percorso sconosciuto.

Le risposte di tipo elenco escono come messaggi di testo APRS indirizzati alla
stazione che interroga, con il campo destinatario fisso di 9 caratteri riempito
di spazi e **senza** numero di messaggio — una risposta a query è informativa,
quindi non sollecita ack. Il nominativo di destinazione usato per il traffico di
query è ``APE32L``, e il percorso è la maschera di percorso della pagina IGate.

Limitazione di frequenza
========================

Due limitatori indipendenti evitano che il risponditore diventi un problema di
tempo in onda o metà di un anello di retroazione con un altro auto-risponditore:

* **Limitatore broadcast** — per *tipo* di query **e sorgente**, al massimo una
  risposta ogni ``g_config.query_min_interval_sec`` (30 s predefiniti; il minimo
  della pagina *Query* è 5 s). Essendo per tipo, un canale affollato che chiede
  ``?APRS?`` non può sopprimere una risposta a ``?WX?``; essendo anche per
  sorgente, un flusso APRS-IS loquace non può consumare la quota che serve a una
  domanda ascoltata in onda.
* **Limitatore delle dirette** — le query dirette saltano il limitatore broadcast
  (sono esplicitamente indirizzate a questa stazione) ma hanno un proprio limite
  **per sorgente**, più stretto, di ``QUERY_DIRECTED_MIN_INTERVAL_SEC`` (5 s),
  tracciato in una tabella fissa di ``QUERY_DIRECTED_TRACK_MAX`` (8) voci. Le
  query dirette sono traffico raro, quindi una tabella piena semplicemente ricicla
  la sorgente più vecchia.

Configurazione
==============

La pagina *Query* (``page_query.c``) espone, in ordine: **Abilita**,
**Rispondere alle interrogazioni ricevute in RF**, **Rispondere alle
interrogazioni ricevute da APRS-IS**, i tre interruttori di query generale (``?APRS?``,
``?WX?``, ``?IGATE?`` — gli ultimi due appaiono solo nelle build che includono le
funzioni meteo e IGate), **Abilita query dirette**, **Query dirette estese**, e
l'**Intervallo minimo di risposta** in secondi.

.. list-table::
   :header-rows: 1
   :widths: 30 20 50

   * - Chiave JSON
     - Predefinito
     - Campo
   * - ``queryEn``
     - ``false``
     - abilitazione principale (opt-in, come la messaggistica)
   * - ``queryRf`` / ``queryInet``
     - ``true`` / ``false``
     - quale sorgente viene risposta: query ascoltate in RF / lette dal flusso
       APRS-IS. La sorgente APRS-IS è spenta per impostazione predefinita, così
       il traffico di dorsale non può attivare il trasmettitore appena installato.
   * - ``queryAprsEn`` / ``queryWxEn`` / ``queryIgateEn``
     - ``true``
     - abilitazioni per singola query generale
   * - ``queryDirectedEn`` / ``queryExtEn``
     - ``true``
     - insieme diretto / insieme diretto esteso
   * - ``queryMinInterval``
     - ``30``
     - limite di frequenza broadcast, in secondi

L'intera pagina è condizionata dall'interruttore di compilazione ``ENABLE_QUERY``.

.. note::

   ``query_init()`` deve essere eseguita dopo ``message_init()``/``igate_start()``
   e prima che ``aprs_service_start()`` finisca di cablare la catena di dispatch
   RX, che è esattamente dove la chiama ``aprs_service.c``.
