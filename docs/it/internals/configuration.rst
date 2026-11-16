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

.. _it-config-module-switches:

Interruttori di modulo in compilazione
======================================

``app_config.h`` definisce un insieme di macro ``ENABLE_*``; commentarne una
rimuove il modulo dall'immagine — la sua voce di barra laterale e la sua pagina,
le rotte servite dalla pagina e tutto il codice firmware che implementa il
modulo: i suoi task, i suoi passaggi di servizio, la sua gestione nel percorso
di ricezione, i suoi beacon e le sue risposte agli altri moduli:

.. code-block:: c

   ENABLE_DASHBOARD    ENABLE_MSG_CHAT     ENABLE_BULLETINS    ENABLE_OBJECTS_ITEMS
   ENABLE_STATION      ENABLE_RADIO_MODEM  ENABLE_MESSAGE      ENABLE_IGATE
   ENABLE_BRANDMEISTER ENABLE_QUERY        ENABLE_DIGIPEATER   ENABLE_TRACKER
   ENABLE_WEATHER      ENABLE_TELEMETRY    ENABLE_GPS          ENABLE_TELEGRAM
   ENABLE_WINLINK      ENABLE_LOGS         ENABLE_SYSTEM       ENABLE_WIRELESS
   ENABLE_FILE_STORAGE ENABLE_ABOUT_FIRMWARE

Cosa lascia dietro di sé un modulo disabilitato:

- **Le sue impostazioni.** ``app_config_t`` conserva ogni campo e lo strato di
  persistenza ogni sezione, quindi il file salvato del modulo resta intatto in
  flash e viene ritrovato da una compilazione successiva che lo riabilita.
  Niente al di fuori del modulo agisce su quelle impostazioni mentre è escluso,
  e ogni pagina che ne scrive anche una (per esempio My Station, che replica
  l'identità della stazione in ogni servizio) scrive solo quelle dei moduli
  compilati.
- **Codice che un altro modulo abilitato usa ancora.** Se ne va solo la parte
  del modulo disabilitato. I pezzi condivisi sono selezionati da macro derivate
  ``BUILD_*``, mai impostate a mano, ognuna definita finché almeno uno dei suoi
  utenti è nella compilazione:

  ======================== ====================================================
  ``BUILD_APRS_IS``        Client APRS-IS (connessione, login, failover dei
                           server, pompa delle righe ricevute,
                           ``igate_send_raw()``). Utenti: IGate, BrandMeister,
                           Message, Query, Digipeater, Tracker, Weather,
                           Telemetry, Bulletins, Objects/Items.
  ``BUILD_DUP_CACHE``      Cache di soppressione dei duplicati. Utenti: IGate,
                           Digipeater.
  ``BUILD_PATH_PRESETS``   I quattro percorsi predefiniti condivisi. Utenti:
                           ogni modulo che trasmette attraverso di essi.
  ``BUILD_MSG_PARSER``     Il parser di ricezione ``:ADDRESSEE:``. Utenti:
                           Message, Query (query dirette), Telegram
                           (instradamento).
  ``BUILD_SENSORS``        Avvio del registro dei sensori locali. Utenti:
                           Weather, Telemetry.
  ``BUILD_BEACONS``        Costruttori dei beacon di posizione e di stato.
                           Utenti: IGate, Digipeater, Tracker.
  ======================== ====================================================

- **Impostazioni che un altro modulo abilitato usa ancora.** Una pagina che
  modifica anche un'impostazione condivisa resta, ridotta a quell'impostazione:
  senza ``ENABLE_IGATE`` la pagina IGate diventa la pagina *APRS-IS*
  (nominativo/SSID di login, passcode, filtro, server e, con il digipeater,
  soppressione dei duplicati); senza ``ENABLE_DIGIPEATER`` la pagina
  Digipeater diventa la pagina *Percorsi* (i quattro percorsi predefiniti).

Due interruttori dipendono da un altro, e la compilazione si ferma con un
``#error`` che nomina la coppia se la dipendenza è rotta: ``ENABLE_MSG_CHAT`` e
``ENABLE_WINLINK`` richiedono entrambi ``ENABLE_MESSAGE``.

I moduli solo pagina — ``ENABLE_DASHBOARD``, ``ENABLE_STATION``,
``ENABLE_RADIO_MODEM``, ``ENABLE_LOGS``, ``ENABLE_SYSTEM``,
``ENABLE_WIRELESS``, ``ENABLE_FILE_STORAGE`` e ``ENABLE_ABOUT_FIRMWARE`` —
tolgono uno strumento di amministrazione, non una funzione della stazione: il
modem, il WiFi, la sincronizzazione oraria e il file system che configurano
funzionano sempre con le impostazioni salvate. Con ogni pagina se ne va lo
strumento che c'è dietro: il log del traffico RX/TX e le statistiche dal vivo
(Dashboard), i test di loop, livello, cattura e TX (Radiomodem), la cattura dei
log (Logs), il ripristino di fabbrica (System), la scansione WiFi (Wireless), il
gestore di file (File Storage) e il caricamento OTA (About).

Altri effetti da conoscere:

- Senza ``ENABLE_IGATE`` ogni riga APRS-IS è passata al gestore di righe (non
  c'è un interruttore Internet→RF da consultare), quindi la messaggistica, il
  risponditore di query, l'instradamento Telegram e last heard continuano a
  leggere il feed.
- La sessione APRS-IS si apre quando la gamba Internet di qualsiasi modulo
  compilato la richiede, non solo per l'IGate: telemetria, bollettini e
  oggetti/item, le cui impostazioni vivono in file propri, comunicano il loro
  bisogno con ``igate_uplink_demand()``.
- Un beacon annuncia "capace di messaggistica" solo con ``ENABLE_MESSAGE``
  compilato e attivo; il risponditore risponde a ``?APRS?``, ``?IGATE?``,
  ``?APRSS`` e ``?APRSP`` solo con ``ENABLE_IGATE`` e a ``?WX?`` solo con
  ``ENABLE_WEATHER``; SmartBeaconing e ogni controllo "Usa GPS" esistono solo
  con ``ENABLE_GPS``.
- I pin della UART GNSS restano riservati nei selettori GPIO senza
  ``ENABLE_GPS``: sono cablaggio della scheda, come i pin I2C dei sensori.

**Non** c'è interruttore ``ENABLE_SENSORS``: il framework ``sensors_local`` non ha
disabilitazione in compilazione ed è sempre compilato (i suoi singoli driver sono
condizionati dalle proprie opzioni Kconfig ``CONFIG_SENSORS_LOCAL_*_DRIVER``).

.. _it-config-allow-show-password:

Rivelare i segreti memorizzati: ``ALLOW_SHOW_PASSWORD``
=======================================================

L'ultima voce dello stesso blocco non è un selettore di pagina ma un flag di
politica, e per questo è scritta come valore invece che impostata commentando la
riga:

.. code-block:: c

   #define ALLOW_SHOW_PASSWORD 0

Governa ogni segreto che l'interfaccia di amministrazione modifica — la password
dell'admin web, le passphrase WiFi dell'AP e dei client, il passcode APRS-IS, la
password Winlink e il token del bot Telegram — attraverso i tre helper di
``web_common``: ``web_password_value()`` scrive il campo,
``web_form_get_password()`` lo rilegge e ``web_password_toggle()`` scrive la
casella *Mostra password*.

A ``1`` le pagine si comportano come hanno sempre fatto: ogni campo arriva
precompilato con il segreto memorizzato e porta una casella *Mostra password*
che trasforma l'input mascherato in uno a testo semplice.

A ``0`` — il valore predefinito — nessun segreto lascia mai il dispositivo
attraverso una pagina di amministrazione. Un campo che ha un segreto dietro di
sé è reso con il segnaposto fisso ``WEB_PASSWORD_MASK`` (``*****``); uno che non
ce l'ha è reso vuoto, così un segreto non impostato si legge ancora come tale.
La casella non viene emessa affatto, e nemmeno lo script ``togglePwd()`` di
``web_send_footer()`` né la regola ``.pwd-show`` del foglio di stile, quindi una
build compilata in questo modo non porta nessuna delle tre.

La modifica non cambia. Nel ``POST`` un campo che porta ancora il segnaposto
significa «lascia stare questo segreto» e il valore memorizzato è conservato
intatto mentre il resto della pagina si salva normalmente; qualsiasi altro
valore lo sostituisce, stringa vuota compresa, così un segreto si cambia e si
cancella esattamente come prima. L'unica cosa che questo costa è il letterale
``*****``, che non può essere memorizzato come segreto finché il flag è ``0``
perché è indistinguibile da un campo non toccato.

Un dettaglio utile leggendo la pagina Wireless: i suoi campi passphrase portano
``minlength='8'``, più lungo del segnaposto, e non per distrazione —
``minlength`` vale solo una volta che il campo è stato modificato a mano, così un
modulo non toccato invia il suo segnaposto di cinque caratteri senza protestare
mentre una passphrase davvero digitata resta soggetta all'intera lunghezza WPA2.

Origine delle icone dei simboli: ``WEB_SYMBOL_ICON_BASE``
=========================================================

Il firmware non contiene uno sprite dei simboli APRS. Le icone della tabella del
traffico della dashboard, il selettore dei simboli e ogni campo simbolo sono
immagini che il browser dell'amministratore carica da ``WEB_SYMBOL_ICON_BASE``
(``web_common.h``), un URL di directory con file ``<codice>-<tabella>.png`` —
``<codice>`` il valore ASCII decimale del carattere del simbolo, ``<tabella>``
1 per la tabella primaria e 2 per quella alternativa. Il valore predefinito è
``//aprs.dprns.com/symbols/icons/``. È relativo allo schema, quindi le icone
seguono lo schema della pagina di amministrazione stessa e vengono richieste
via HTTPS quando l'amministrazione sta dietro un proxy che termina TLS, invece
di essere bloccate come contenuto misto.

La definizione è protetta da ``#ifndef``, così una compilazione che non debba
far contattare al browser una terza parte — o che replichi le icone su un
server locale — la sostituisce con una definizione di compilazione, per
esempio nel ``CMakeLists.txt`` di primo livello:

.. code-block:: cmake

   idf_build_set_property(COMPILE_DEFINITIONS "WEB_SYMBOL_ICON_BASE=\"//192.168.1.10/icons/\"" APPEND)

Un'icona che non può essere caricata viene nascosta, e il selettore dei simboli
mostra al suo posto il carattere del simbolo.

Preset di percorso e maschere di bit
====================================

Ogni servizio (tracker / igate / digi / wx / …) memorizza una **maschera di
bit**, non una stringa di percorso. Il bit *N* seleziona ``g_config.path[N]``,
uno dei quattro preset modificati nella pagina *Digi*; ognuno viene memorizzato
nella forma in cui va in onda, in maiuscolo e con ogni voce un nominativo e un
SSID legali (vedi :ref:`it-web-admin`).
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
