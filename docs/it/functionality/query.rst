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

* ``query_process(tnc2Line)`` — riceve ogni riga TNC2 decodificata. Non fa nulla
  a meno che ``query_en`` sia attivo e il campo informativo inizi con ``?``, che è
  ciò che rende una riga una query **generale**.
* ``query_process_directed(fromCall, toCall, text, tnc2Line)`` — chiamata da
  ``handleIncomingAPRS()`` di ``message.c`` quando il testo di un messaggio
  indirizzato inizia con ``?``, così l'analisi di ``:ADDRESSEE:`` non viene
  duplicata qui. Non fa nulla a meno che ``query_en`` **e** ``query_directed_en``
  siano attivi e ``toCall`` corrisponda a ``g_config.aprs_mycall`` (nominativo
  base, senza distinguere l'SSID).

Le risposte sono trasmesse tramite il gestore installato con
``query_set_tx_handler()``. ``aprs_service.c`` riusa esattamente lo stesso
``messageTxHandler()`` che fornisce al motore di messaggistica, quindi i bit di
instradamento sono la nota coppia ``MSG_CHANNEL_RF`` / ``MSG_CHANNEL_INET``,
selezionata da ``g_config.query_rf`` / ``query_inet``.

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
       ``<IGATE,MSG_CNT=n,LOC_CNT=n>``, costruita dagli stessi contatori di
       ``igate_get_stats()`` letti dalla dashboard (``MSG_CNT`` = ``txCount``,
       ``LOC_CNT`` = ``rxCount``). Ignorata in silenzio mentre ``igate_en`` è
       spento.

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
       ``lastheard_heard_history()`` e ``LASTHEARD_HEARD_HOURS``). Senza
       l'argomento nominativo, il risponditore lo segnala.
   * - ``?APRSM``
     - Reinvia i messaggi pendenti di questa stazione per l'operatore che
       interroga.
   * - ``?APRSO``
     - Riannuncia gli Oggetti/Item originati qui. Chiama
       ``objitems_request_transmit_all()``, quindi gli elementi escono dal task
       dello **scheduler dei beacon** e non dal task RX — rispondere a una query
       non occupa mai l'RX per la durata di una raffica di trasmissione. Compilato
       senza ``ENABLE_OBJECTS_ITEMS``, risponde ``No objects``.
   * - ``?APRST`` / ``?PING?``
     - Il percorso preso dalla query stessa, ricostruito dalla riga TNC2 ricevuta.
       Senza riga disponibile risponde con percorso sconosciuto.

Le risposte di tipo elenco escono come messaggi di testo APRS indirizzati alla
stazione che interroga, con il campo destinatario fisso di 9 caratteri riempito
di spazi e **senza** numero di messaggio — una risposta a query è informativa,
quindi non sollecita ack. Il nominativo di destinazione usato per il traffico di
query è ``APE32L``, e il percorso è la maschera di percorso della pagina IGate.

Limitazione di frequenza
========================

Due limitatori indipendenti evitano che il risponditore diventi un problema di
tempo in onda o metà di un anello di retroazione con un altro auto-risponditore:

* **Limitatore broadcast** — per *tipo* di query, al massimo una risposta ogni
  ``g_config.query_min_interval_sec`` (30 s predefiniti; il minimo della pagina
  *Query* è 5 s). Essendo per tipo, un canale affollato che chiede ``?APRS?`` non
  può sopprimere una risposta a ``?WX?``.
* **Limitatore delle dirette** — le query dirette saltano il limitatore broadcast
  (sono esplicitamente indirizzate a questa stazione) ma hanno un proprio limite
  **per sorgente**, più stretto, di ``QUERY_DIRECTED_MIN_INTERVAL_SEC`` (5 s),
  tracciato in una tabella fissa di ``QUERY_DIRECTED_TRACK_MAX`` (8) voci. Le
  query dirette sono traffico raro, quindi una tabella piena semplicemente ricicla
  la sorgente più vecchia.

Configurazione
==============

La pagina *Query* (``page_query.c``) espone, in ordine: **Abilita**, **Invia via
RF**, **Invia via Internet**, i tre interruttori di query generale (``?APRS?``,
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
     - rispondere in RF / verso APRS-IS. La gamba Internet è spenta per
       impostazione predefinita così la stazione non risponde verso APRS-IS
       senza che sia richiesto.
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
