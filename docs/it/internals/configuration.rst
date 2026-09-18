.. _it-configuration:

===========================
Il motore di configurazione
===========================

Una configurazione residente
============================

Una singola istanza ``app_config_t g_config`` (``main/app_config.c`` /
``app_config.h``) è la copia viva che ogni sottosistema legge. È caricata
all'avvio e modificata campo per campo dai gestori POST del web. I suoi campi sono
raggruppati per pagina dell'amministrazione web: sistema/ora, identità "My
Station", Wi-Fi, IGate, BrandMeister, Digipeater, Tracker, Weather, GPS, il
modem AFSK, System/autenticazione HTTP, Message, Query e l'account Winlink.

I nomi dei campi e le chiavi JSON sono mantenuti **1:1** con il ``config.h``/
``config.cpp`` del progetto di riferimento originale, così che ogni valore che
l'amministrazione web mostra abbia una casa e un operatore che si sposta tra i
due progetti riconosca le chiavi.

Un file per funzionalità
========================

La struttura residente è una cosa; dove viene memorizzata è un'altra. Ogni
pagina dell'amministrazione web con impostazioni persistenti ha **un file
proprio** sotto ``/storage``, con il nome della pagina — il menu laterale è
l'indice dell'elenco dei file:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - File
     - Pagina
   * - ``system.json``
     - Sistema: clock della CPU, host SNTP e risincronizzazione, fuso orario,
       credenziali dell'amministrazione web.
   * - ``station.json``
     - My Station: nominativo, posizione, dati d'antenna PHG, ambiguità, opzioni
       del rapporto di stato, no-archive e DAO.
   * - ``wireless.json``
     - Wireless: selezione dell'interfaccia, potenza TX, canale/SSID/chiave del
       SoftAP, i cinque profili stazione.
   * - ``radio.json``
     - Radiomodem: modulazione AFSK/FSK, FX.25, preambolo, buffer TX,
       temporizzazione CSMA, tenuta del PTT, limitatore di duty cycle.
   * - ``igate.json``
     - IGate: entrambe le direzioni di gateway e i loro filtri, liste amici e
       satelliti, cache dei duplicati, filtri di distanza e prefisso, gating dei
       messaggi, i quattro slot server APRS-IS, il beacon e la sua estensione
       dati.
   * - ``brandmeister.json``
     - BrandMeister: gli interruttori di interconnessione e l'elenco dei
       nominativi gateway.
   * - ``digi.json``
     - Digipeater: la tabella degli alias e le regole di ripetizione, il beacon
       **e i quattro preset di percorso condivisi**, che si modificano in questa
       pagina.
   * - ``tracker.json``
     - Tracker: il beacon, le opzioni Mic-E e i parametri di SmartBeaconing.
   * - ``weather.json``
     - Weather: il rapporto e la mappatura dei sensori per campo.
   * - ``gps.json``
     - GPS: l'interruttore del ricevitore.
   * - ``message.json``
     - Message: il servizio di messaggistica, i tentativi, la GPIO d'allarme e i
       gruppi di messaggi.
   * - ``winlink.json``
     - Winlink: le impostazioni di account e sessione (le risposte stanno in un
       file separato, ``winlink_mail.json``).
   * - ``query.json``
     - Query: gli interruttori del risponditore e il beacon di capacità.

Ogni riga è un valore di ``app_config_section_t``, e la tabella ``SECTIONS`` in
``app_config.c`` nomina il file e le due metà del suo codec. Il gestore di
salvataggio di una pagina chiama ``app_config_save_section()`` con la propria
sezione, quindi salvare il digipeater non riscrive mai il file dell'IGate e una
scrittura fallita non può portarsi via le impostazioni di un'altra
funzionalità. La pagina *My Station* è l'unica che ne nomina diverse: i suoi
specchi "Use My Station Data" toccano campi di altri cinque servizi, quindi
passa a ``app_config_save_sections()`` ogni sezione toccata — un salvataggio
parziale lascerebbe l'identità della stazione divisa tra file che non
concordano più.

L'ordine di lettura conta esattamente in un punto: il lettore BrandMeister
riapplica l'interblocco del monitor mondiale contro il gating INET→RF che porta
il file dell'IGate, quindi la sezione IGate viene letta per prima.

.. note::

   Le pagine senza impostazioni persistenti proprie — Dashboard, Snd/Rcv Msg,
   Console Logs, File Storage, About — non hanno file. Quattro sottosistemi
   mantengono strutture proprie invece di campi di ``app_config_t``, e
   possiedono direttamente i loro file: ``/storage/telemetry.json``,
   ``bulletins.json``, ``objitems.json`` e ``telegram.json``. Il motivo è la
   dimensione — quelle tabelle ingrandirebbero significativamente la struttura
   residente — ma il risultato è la stessa regola: una funzionalità, un file.

Caricamento e salvataggio
=========================

* **Prima i predefiniti, una volta sola.** ``app_config_load()`` riempie
  l'intera struttura da ``app_config_set_defaults()`` prima di aprire il primo
  file, poi legge ogni sezione sopra di essa. Ogni lettore di sezione prende il
  valore di ripiego di ogni chiave dal campo stesso che sta per sovrascrivere,
  quindi una chiave che un file non porta — e una sezione il cui file non esiste
  affatto — conserva il suo predefinito documentato. Questo tiene anche una
  seconda ``app_config_t`` fuori dallo stack del task che carica, che ha già
  accanto a sé l'albero cJSON di una sezione vivo nell'heap.
* **Caricato** con **cJSON**, un file alla volta, quindi il picco di
  allocazione è la sezione più grande e non l'intera configurazione.
* **Mancante, vuoto o corrotto** → quella sezione viene riscritta dai valori
  predefiniti prima che il caricamento ritorni. È questo a garantire che ogni
  funzionalità abbia un file in flash dal primo avvio in poi, senza che un
  operatore debba mai visitare la sua pagina.
* **Senza memoria** → non viene scritto proprio nulla e l'intero caricamento
  segnala l'errore. ``cJSON_Parse()`` restituisce ``NULL`` sia per un file
  corrotto sia per un'allocazione fallita, quindi ``json_store_read()``
  riesamina il testo con un controllo che non alloca nulla prima di decidere
  quale dei due sia avvenuto; confonderli cancellerebbe una configurazione
  valida durante un avvio carico. Riscrivere le sezioni genuinamente assenti
  mentre un'altra è ancora non letta sarebbe anche peggio — persisterebbe una
  configurazione assemblata metà da flash e metà dai predefiniti — quindi la
  passata non scrive nulla e ``main.c`` riprova una volta prima di ripiegare
  sull'insieme di fabbrica.
* **Salvato** da un piccolo scrittore JSON token per token (``jw_t``/``jadd_*``)
  che scorre direttamente nel file, evitando la doppia allocazione di heap che
  necessiterebbero un albero cJSON completo più il suo buffer serializzato. Un
  buffer statico di ``setvbuf()`` è installato subito dopo ``fopen()`` così che
  newlib non allochi pigramente un grande buffer stdio a metà scrittura.
* **Atomico**, per file: scrive ``<nome>.json.tmp``, poi rinomina. Un
  salvataggio di più sezioni prende una sola volta il gate di scrittura
  dell'intero filesystem attorno all'intera tornata, e tenta ogni file
  selezionato anche dopo che uno è fallito, così un filesystem pieno non lascia
  gli altri con impostazioni che l'operatore ha già sostituito.

API pubblica: ``app_config_set_defaults()``, ``app_config_load()``,
``app_config_save_section()``, ``app_config_save_sections()``,
``app_config_save()`` (tutte le sezioni), ``app_config_section_path()``,
``app_config_factory_reset()``, e l'istanza viva
``extern app_config_t g_config``.

Concorrenza: il lock di configurazione
======================================

``g_config`` è scritto campo per campo dai gestori POST del web (un singolo
salvataggio di impostazioni riscrive molti campi, diversi dei quali
stringhe/array, uno a uno) mentre task di lunga durata (costruttori di beacon,
login dell'IGate, digipeater, messaggio, meteo, risponditore di query) leggono
quegli stessi campi. Un
lettore che campiona una stringa a metà ``strcpy`` può vedere un valore rotto o
transitoriamente senza terminatore NUL e uscire dalla fine del buffer.
``app_config_lock()`` / ``app_config_unlock()`` serializzano quei due lati.

È un rigoroso **lock di foglia**: è mantenuto solo abbastanza a lungo da copiare i
campi necessari in locali — mai attraverso una chiamata bloccante, I/O,
trasmissione o altro lock. I campi scalari (a singola parola) sono atomici a
livello di parola su questo MCU e possono essere letti senza lock. È distinto dal
mutex di salvataggio interno (mantenuto per tutta la serializzazione su flash).

Interruttori di modulo in compilazione
======================================

``app_config.h`` definisce un insieme di macro ``ENABLE_*``; commentarne una
rimuove la sua voce di barra laterale e la sua pagina dall'immagine:

.. code-block:: c

   ENABLE_DASHBOARD    ENABLE_MSG_CHAT     ENABLE_BULLETINS    ENABLE_OBJECTS_ITEMS
   ENABLE_STATION      ENABLE_RADIO_MODEM  ENABLE_MESSAGE      ENABLE_IGATE
   ENABLE_BRANDMEISTER ENABLE_QUERY        ENABLE_DIGIPEATER   ENABLE_TRACKER
   ENABLE_WEATHER      ENABLE_TELEMETRY    ENABLE_GPS          ENABLE_TELEGRAM
   ENABLE_WINLINK      ENABLE_LOGS         ENABLE_SYSTEM       ENABLE_WIRELESS
   ENABLE_FILE_STORAGE ENABLE_ABOUT_FIRMWARE

**Non** c'è interruttore ``ENABLE_SENSORS``: il framework ``sensors_local`` non ha
disabilitazione in compilazione ed è sempre compilato (i suoi singoli driver sono
condizionati dalle proprie opzioni Kconfig ``CONFIG_SENSORS_LOCAL_*_DRIVER``).

Preset di percorso e maschere di bit
====================================

Ogni servizio (tracker / igate / digi / wx / …) memorizza una **maschera di
bit**, non una stringa di percorso. Il bit *N* seleziona ``g_config.path[N]``,
uno dei quattro preset a testo libero modificati nella pagina *Digi*.
``aprs_path_build_suffix()`` concatena ogni slot selezionato non vuoto; gli slot
selezionati-ma-vuoti sono saltati. È condivisa dai beacon, dal meteo, dalla
telemetria, dai messaggi e dalle risposte alle query, e applica il limite AX.25
di 8 vie al momento della trasmissione, così una configurazione arrivata al
dispositivo senza passare da un modulo web non può mettere in onda un percorso
troppo lungo.

Gli Oggetti/Elementi sono l'unico servizio che non unisce gli slot tra loro: il
loro instradamento proporzionale invia **un** preset per trasmissione e ruota
sulla selezione, quindi ``objitem_paths()`` costruisce la lista da sé. Lì il
limite di salti vale per preset e non sull'intera selezione, ed è contato con la
stessa ``app_config_path_hop_count()`` usata dal costruttore condiviso e dal
taglio al salvataggio: un preset che da solo supera il limite viene escluso dalla
rotazione.

Ogni selettore parte selezionando il preset 0 e nient'altro
(``PATH_PRESET_MASK_DEFAULT``), perché ``g_config.path[0]`` è l'unico slot con
una stringa di fabbrica (``WIDE1-1,WIDE2-1``) e un bit che punta a uno slot
vuoto manderebbe il beacon con la sola destinazione.

I bit del filtro dell'IGate (condivisi da ``rf2inetFilter`` e ``inet2rfFilter``):

.. code-block:: text

   MESSAGE 1<<0 · STATUS 1<<1 · TELEMETRY 1<<2 · WEATHER 1<<3 · OBJECT 1<<4
   ITEM 1<<5 · QUERY 1<<6 · BUOY 1<<7 · POSITION 1<<8 · OTHER 1<<9

``IGATE_FILT_OTHER`` è l'unico bit che copre più tipi di payload insieme —
capacità di stazione, formati definiti dall'utente, radiogoniometria Agrelo,
beacon di locatore Maidenhead e la funzione mappa riservata — ed è per questo
che i fieldset *Filter* mostrano nove caselle per dieci bit. Nemmeno
``IGATE_FILT_QUERY`` ha una casella propria: le interrogazioni sono classificate
perché il codice di gating possa nominarle, ma l'operatore le governa dalla
pagina Query. Il traffico di terze parti e i dati di prova restano fuori da ogni
bit e non sono mai ritrasmessi sulla base della maschera.
