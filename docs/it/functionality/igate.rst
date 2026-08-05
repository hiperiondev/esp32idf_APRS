.. _it-igate:

=======================
IGate — gateway APRS-IS
=======================

Il componente ``igate`` (``components/igate/``) è un gateway Internet APRS-IS
bidirezionale completo, costruito su socket LWIP. Legge tutta la sua
configurazione da ``g_config`` (la pagina *IGate* dell'amministrazione web),
quindi l'amministrazione web è l'unica fonte di verità.

Il task client APRS-IS
======================

* **Client TCP** con riconnessione automatica. Rilegge ``g_config`` a ogni
  riconnessione, quindi la maggior parte delle modifiche dal web ha effetto dopo
  il ciclo di riconnessione successivo, senza riavvio.
* **Condizionato alla connettività reale**, non solo al fatto che "il Wi-Fi è
  attivo": interroga ``net_state_is_connected()``, che diventa vero solo con
  ``IP_EVENT_STA_GOT_IP`` e falso di nuovo alla disconnessione o in modalità
  solo-AP.
* **Riga di login:** ``user <mycall> pass <passcode> vers ESP32APRS 1.0 filter
  <filter>`` — registrata alla lettera, così che un filtro malformato sia
  visibile. Il banner del server e la riga ``# logresp … verified/unverified``
  vengono mostrati; una risposta ``unverified`` genera un avviso che nomina
  ``aprs_mycall`` / ``aprs_passcode``.
* **Validazione del filtro lato server.** Prima di essere inviato,
  ``g_config.aprs_filter`` è controllato strutturalmente da
  ``aprs_filter_validate_server_string()`` — ogni termine separato da spazi deve
  essere ``<lettera>/<argomenti>`` con il numero di argomenti corretto per quella
  lettera di filtro.
* **Uplink condiviso.** Il task è sempre in esecuzione, perché lo stesso socket è
  usato dal componente di messaggistica (``igate_send_raw()``) e dal "beacon a
  internet". Resta inattivo a basso costo quando niente lo richiede.

RF → INET (``igateProcess()``)
==============================

Ogni frame decodificato da RF che l'applicazione smista (con ``igate_en`` e
``rf2inet`` attivi) attraversa questa pipeline, in ordine. Un frame che fallisce
qualsiasi fase viene scartato, e la *ragione* è registrata su un contatore
per-ragione così che la dashboard possa mostrare "N scartati per X" invece di un
singolo aggregato opaco.

#. **Soppressione duplicati.** Il frame è controllato contro la cache dei
   duplicati condivisa (``isDuplicatePacket()``). Sia la sua profondità
   (``g_config.dup_cache_size``, ``DUP_CACHE_SIZE_MIN``..``DUP_CACHE_SIZE_MAX``
   = 4..40, predefinito 20) sia la sua finestra
   (``g_config.dup_cache_timeout_ms``, 1000..120000 ms, predefinito 30000) sono
   modificabili nella pagina *IGate* e vengono rilette a ogni ricerca, quindi
   una modifica si applica senza riavvio. L'array è sempre allocato alla
   capacità di compilazione ``DUP_CACHE_SIZE_MAX``; ``dup_cache_size`` sceglie
   solo quanta parte usarne. I duplicati sono contati a parte in
   ``dupCount``.
#. **Guardia di frame troppo corto.** I frame il cui campo info è sotto la
   lunghezza minima utilizzabile vengono scartati (``DROP_TOO_SHORT``).
#. **Filtro di token di percorso.** I frame il cui percorso porta ``RFONLY``,
   ``TCPIP``, ``qA*`` o ``NOGATE`` non vengono mai inoltrati (``DROP_PATH_TOKEN``).
#. **Regola di gate satellitare.** Un frame ripetuto tramite un gateway
   satellitare noto il cui indicativo non è marcato come usato (``*``) viene
   scartato (``DROP_SAT_NOT_USED``).
#. **Unwrap di terze parti (``}``).** Un frame il cui campo informativo inizia
   con ``}`` porta una propria riga interna completa
   ``SRC>DST,PATH:payload``. Se quel percorso interno porta già ``TCPIP`` o
   ``TCPXX``, il frame ha già raggiunto APRS-IS una volta e viene scartato
   come loop (``DROP_3RDPARTY_LOOP``). Altrimenti l'intestazione RF esterna
   viene scartata del tutto e tutte le fasi restanti — dal filtro per tipo di
   payload in poi — vengono eseguite contro il pacchetto interno: la sua
   propria origine, destinazione, percorso e payload, esattamente come se
   quella stazione fosse stata ascoltata direttamente. Questo è ciò che
   permette a un gateway cross-band o HF di ritrasmettere una stazione che
   non ha altra via verso Internet.
#. **Gate di query generica.** Un payload il cui primo byte è ``?``
   (``?APRS?``, ``?WX?``, ``?IGATE?``, …) viene scartato incondizionatamente
   (``DROP_GENERIC_QUERY``), indipendentemente da ``g_config.rf2inetFilter``
   o da qualsiasi altra casella. Vedi :ref:`it-filtering`.
#. **Filtro per tipo di payload.** Il payload (eventualmente spacchettato) è
   classificato da ``aprs_filter_classify_info()`` e testato contro
   ``g_config.rf2inetFilter`` (``DROP_TYPE_FILTER``). Vedi :ref:`it-filtering`.
#. **Gate di portata locale.** Se abilitato, la posizione del pacchetto è
   decodificata e la sua distanza sul cerchio massimo (haversine) da "My Station"
   è confrontata con ``g_config.rf2inet_range_km``; i pacchetti troppo distanti
   vengono scartati (``DROP_RANGE_FILTER``). I pacchetti la cui posizione non può
   essere decodificata passano questo controllo.
#. **Gate di prefisso locale.** Se abilitato, l'indicativo di origine deve
   iniziare con uno dei prefissi separati da virgole in
   ``g_config.rf2inet_prefixes`` (es. ``EA,EB,EC``), altrimenti viene scartato
   (``DROP_PREFIX_FILTER``).
#. **Budlist.** L'indicativo di origine è testato contro la whitelist/blacklist
   locale in ``g_config.rf2inet_budlist_mode`` (``DROP_BUDLIST``).

Un frame che sopravvive a tutte le fasi riceve un'intestazione
``,qAR,<mycall>-<ssid>`` — o la forma di gate satellitare
``,<mycall>-<ssid>*,qAO,<object>`` — ed è scritto su APRS-IS.

INET → RF (``inet2rfHandler()``)
================================

Ogni riga diversa da ``#`` letta dal socket incrementa ``isRxCount`` ed è
consegnata al motore di messaggistica (``handleIncomingAPRS()``) quando la
messaggistica è attiva. È poi considerata per la ritrasmissione in RF solo se
``inet2rf`` è impostato, e solo dopo aver superato:

#. **Gate di query generica.** Una riga il cui payload inizia con ``?`` viene
   scartata incondizionatamente (``DROP_GENERIC_QUERY``), indipendentemente
   da ``g_config.inet2rfFilter`` o da qualsiasi altra casella — l'immagine
   speculare del gate di query generica RF→INET sopra, verificata prima di
   ogni altra fase seguente. Vedi :ref:`it-filtering`.
#. **Soppressione dell'eco dei report propri.** Ogni report che questa stazione
   carica con il suo flag ``*_2inet`` viene rimandato indietro come eco
   direttamente dal server APRS-IS. ``inet_line_is_own_report()`` riconosce quegli
   echi (confrontando l'indicativo base di origine contro ogni indicativo di
   report della propria stazione) e non li reinoltra mai in RF. I report propri
   raggiungono RF esclusivamente tramite i loro flag "Send via RF" (``*_2rf``).
#. **Filtro per tipo di payload.** La riga è classificata da
   ``aprs_filter_classify_tnc2()`` e testata contro ``g_config.inet2rfFilter``.
#. **Unwrap selettivo di terze parti (opzionale).** Il traffico di terze parti
   (``}``) — la classica fonte di loop IGate — classifica come 0 e non viene mai
   ritrasmesso di default. Con ``inet2rf_3rdparty_unwrap_en`` attivo **e**
   ``inet2rf_budlist_mode == BUDLIST_WHITELIST``, un livello di incapsulamento
   ``}`` può essere spacchettato e il pacchetto interno riclassificato e
   ritrasmesso, ma *solo* quando l'origine del pacchetto interno è essa stessa
   nella whitelist. Non è mai un interruttore generale di "ritrasmetti tutto il
   traffico di terze parti".
#. **Budlist.** L'indicativo di origine (che qui può portare un ``-SSID``) è
   testato contro ``g_config.inet2rf_budlist_mode``.

Una riga che supera tutte le fasi non viene mai trasmessa in RF con la sua
intestazione APRS-IS intatta. ``build_thirdparty_frame()`` scarta del tutto
quell'intestazione e avvolge l'originale ``SRC>DST`` e il campo informativo,
inalterati, dietro un ``}`` come payload dell'intestazione propria di questa
stazione (``MYCALL[-SSID]>APE32L,<percorso igate>:}SRC>DST,TCPIP,
MYCALL[-SSID]*:info``) — la forma di terze parti richiesta dalla
specifica APRS per il traffico ritrasmesso. Questo mantiene i costrutti
``qA`` e un ``TCPIP`` non incapsulato fuori dall'etere, e permette a
qualsiasi altro IGate che ascolti il pacchetto di riconoscerlo come già
ritrasmesso invece di rimandarlo indietro.

.. warning::

   Reinoltrare il traffico di terze parti senza restrizioni è la causa numero uno
   di loop IGate. L'unwrap di terze parti è deliberatamente condizionato a
   un'opzione esplicita *e* a una whitelist proprio per questa ragione.

Contatori e ragioni di scarto
=============================

Lo snapshot ``igate_stats_t`` (``igate_get_stats()``) porta:

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Contatore
     - Significato
   * - ``rxCount``
     - Frame considerati per l'inoltro (RF→INET).
   * - ``txCount``
     - Frame effettivamente inviati ad APRS-IS come risultato dell'inoltro.
   * - ``msgCount``
     - Pacchetti di messaggio APRS (identificatore di tipo dato ``:``) inoltrati
       in entrambe le direzioni — RF→INET da ``igateProcess()``, INET→RF da
       ``igate_note_message_gated()`` da ``aprs_service.c``. È la cifra
       ``MSG_CNT`` riportata dalla risposta a ``?IGATE?``, quindi conta solo i
       messaggi e non il resto del traffico inoltrato.
   * - ``dupCount``
     - Frame duplicati soppressi.
   * - ``isRxCount``
     - **Tutte** le righe lette dal socket (soprainsieme di ciò che raggiunge il
       gestore INET→RF).
   * - ``isTxCount``
     - **Tutte** le scritture sul socket: frame inoltrati, messaggi in uscita e
       invii "beacon a internet" del digi allo stesso modo.
   * - ``dropByReason[]``
     - Contatori di scarto per-ragione, indicizzati da ``drop_reason_t``. Le
       fasi RF→INET sopra coprono ``DROP_TOO_SHORT``, ``DROP_PATH_TOKEN``,
       ``DROP_SAT_NOT_USED``, ``DROP_3RDPARTY_LOOP``, ``DROP_GENERIC_QUERY``,
       ``DROP_TYPE_FILTER``, ``DROP_RANGE_FILTER``, ``DROP_PREFIX_FILTER``,
       ``DROP_BUDLIST`` e ``DROP_TX_FAIL``; l'array
       porta anche ragioni incrementate altrove nel firmware (percorso TX RF,
       digipeater, decodifica AX.25) — vedere ``drop_reason_t`` in
       ``components/igate/include/igate.h`` per l'elenco completo e
       autorevole. Non esiste una ragione generica/opaca di "altro": ogni
       scarto è attribuito a una causa specifica e nominata.
       ``igate_stats_total_drop()`` somma le ragioni non di errore;
       ``igate_stats_total_err()`` somma separatamente le due ragioni di
       errore di decodifica/invio.

``igate_note_drop()`` è esposto così che altri componenti che condividono gli
stessi concetti di filtraggio — attualmente il gestore INET→RF di
``aprs_service.c``, per i suoi controlli di filtro-tipo e budlist — contribuiscano
alla stessa suddivisione per-ragione.

Indicatore di connettività
==========================

``igate_is_connected()`` è vero mentre il socket TCP APRS-IS è aperto, con login
effettuato e con il lettore di righe RX in funzione. Il pannello *Network Status*
della dashboard web (la pillola APRS-IS) lo legge.
