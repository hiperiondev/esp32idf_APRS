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
Station", Wi-Fi, IGate, Digipeater, Tracker, Weather, il modem AFSK,
System/autenticazione HTTP, e Message e Query.

I nomi dei campi e le chiavi JSON sono mantenuti **1:1** con il ``config.h``/
``config.cpp`` del progetto di riferimento originale, così che ogni valore che
l'amministrazione web mostra abbia una casa e i vecchi file ``config.json`` si
carichino senza modifiche.

.. note::

   La configurazione di telemetria, bollettini e oggetti/item deliberatamente
   **non** vive in ``g_config``. Persiste nei propri file LittleFS
   (``/storage/telemetry.json``, ``bulletins.json``, ``objitems.json``) per
   mantenere piccola la configurazione residente — e quindi ogni salvataggio di
   ``config.json``.

Caricamento e salvataggio
=========================

* **Caricato** con **cJSON**. Se il file manca o è corrotto, si applicano i
  valori predefiniti **e si salvano immediatamente**, così che il file esista
  sempre e sia coerente.
* **Salvato** da un piccolo scrittore JSON token per token (``jw_t``/``jadd_*``)
  che scorre direttamente nel file, evitando la doppia allocazione di heap che
  necessiterebbero un albero cJSON completo più il suo buffer serializzato. Un
  buffer statico di ``setvbuf()`` è installato subito dopo ``fopen()`` così che
  newlib non allochi pigramente un grande buffer stdio a metà scrittura.
* **Atomico**: scrive ``/storage/config.json.tmp``, poi rinomina.

API pubblica: ``app_config_set_defaults()``, ``app_config_load()``,
``app_config_save()``, ``app_config_factory_reset()``, e l'istanza viva
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
   ENABLE_DIGIPEATER   ENABLE_TRACKER      ENABLE_WEATHER      ENABLE_TELEMETRY
   ENABLE_SYSTEM       ENABLE_WIRELESS     ENABLE_FILE_STORAGE ENABLE_ABOUT_FIRMWARE
   ENABLE_QUERY

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

I flag di attivazione dei servizi sono un insieme di bit **distinto**, sui
servizi della stazione e non sui preset di percorso; coincidono solo nella
larghezza:

.. code-block:: text

   ACTIVATE_OFF 0 · TRACKER 1<<0 · IGATE 1<<1 · DIGI 1<<2 · WX 1<<3
   ACTIVATE_TELEMETRY 1<<4 · QUERY 1<<5 · STATUS 1<<6 · WIFI 1<<7

I bit del filtro dell'IGate (condivisi da ``rf2inetFilter`` e ``inet2rfFilter``):

.. code-block:: text

   MESSAGE 1<<0 · STATUS 1<<1 · TELEMETRY 1<<2 · WEATHER 1<<3 · OBJECT 1<<4
   ITEM 1<<5 · QUERY 1<<6 · BUOY 1<<7 · POSITION 1<<8
