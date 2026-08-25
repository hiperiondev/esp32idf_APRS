:orphan:

.. _it-index:

.. image:: /_static/welcome_logo.png
   :align: center
   :width: 300px

.. raw:: html

   <h1 style="text-align: center;">esp32idf_APRS</h1>
   <h2 style="text-align: center;">Una stazione APRS completa su un singolo ESP32<br>ESP-IDF nativo, senza Arduino.</h2>
   <p style="text-align: center;"><strong>IGate · Digipeater · Tracker · Meteo · Telemetria, con amministrazione web integrata, un modem software AFSK/FSK direttamente sul chip, collegamento ad APRS-IS, un framework di driver per sensori a runtime e aggiornamenti firmware OTA.</strong></p>

==========
Benvenuti
==========

Benvenuti nella documentazione di **esp32idf_APRS** — una stazione APRS
IGate, Digipeater, Tracker, Meteo e Telemetria nativa in ESP-IDF (C, senza
Arduino) che gira interamente su un singolo microcontrollore ESP32.

Che cos'è questo progetto, in parole semplici
================================================

La maggior parte delle stazioni APRS che si incontrano sul campo sono
costruite da due componenti separati: un computer (o una scheda a
singola board) che esegue un software APRS come Direwolf o Xastir, più un
TNC hardware o software in ascolto su una radio. ``esp32idf_APRS`` riduce
tutta questa pila — il modem audio, il motore dei pacchetti, la logica di
gateway, il digipeater e l'interfaccia dell'operatore — al firmware di un
singolo ESP32, senza PC, senza Raspberry Pi e senza scheda audio esterna
nel circuito.

Il convertitore analogico-digitale dell'ESP32 stesso ascolta l'uscita
dell'altoparlante o del discriminatore di una radio, e il firmware
demodula l'audio AFSK o FSK via software, interamente sul chip. Da lì
decodifica i pacchetti AX.25, decide se instradarli verso internet,
ritrasmetterli via RF, o entrambe le cose, e con la stessa facilità può
generare il proprio traffico: beacon di posizione, report meteo,
telemetria, messaggi, bollettini e oggetti. Tutto ciò si configura
tramite una pagina web servita dal dispositivo stesso, via Wi-Fi, dal
browser di un telefono o di un laptop — non c'è console seriale, non
serve software speciale da installare, e non serve ricompilare il
firmware per le impostazioni di uso quotidiano.

In breve: una scheda piccola ed economica, più una semplice interfaccia
audio verso una radio, diventa un IGate, digipeater e tracker APRS
completo e autonomo, che si configura da un browser e poi si lascia
funzionare.

Che cos'è l'APRS?
==================

**APRS — l'Automatic Packet Reporting System** — è un protocollo digitale
di radioamatore per lo scambio in tempo reale, da molti a molti, di
piccole informazioni tattiche: la posizione di una stazione, il suo
stato, brevi messaggi di testo, letture meteo, valori di telemetria e
annunci generici. A differenza di un collegamento dati punto-punto,
l'APRS è fondamentalmente un sistema di *diffusione* — chiunque ascolti
sulla frequenza condivisa, o osservi la rete via internet, vede il
traffico nel momento in cui avviene.

Una breve storia
------------------

L'APRS è stato creato da Bob Bruninga, nominativo **WB4APR**, ingegnere
di ricerca senior presso la United States Naval Academy. I suoi primi
esperimenti di mappatura della posizione risalgono al 1982 su un Apple
II, e già nel 1984 aveva una versione dedicata — il Connectionless
Emergency Traffic System — funzionante su un Commodore VIC-20 per
tracciare cavalli e cavalieri durante una corsa di resistenza di 100
miglia. Verso la fine degli anni '80 il software passò al PC IBM e, una
volta combinato con il protocollo di radio a pacchetto AX.25 e, più
tardi, con ricevitori GPS economici, si trasformò in un sistema generale
di reportistica tattica in tempo reale. Fu presentato formalmente alla
comunità dei radioamatori e chiamato APRS — un acronimo costruito sul
nominativo dello stesso Bruninga — in un articolo del 1992 alla
Computer Networking Conference dell'ARRL. Bruninga ha continuato a
mantenere il protocollo e il suo sito di riferimento fino alla sua
scomparsa nel 2022, dopo la quale è stata fondata la APRS Foundation per
portare avanti lo sviluppo del protocollo.

Due sviluppi hanno trasformato l'APRS da esperimento di nicchia nella
radio a pacchetto a servizio di radioamatore ampiamente utilizzato: il
GPS economico ha reso pratico il reporting automatico e continuo della
posizione, e la nascita di **APRS-IS** — la dorsale connessa a internet
formata da stazioni riceventi (IGate) che inoltrano il traffico RF sulla
internet pubblica — ha fatto sì che l'attività di una stazione potesse
essere vista in tutto il mondo in pochi secondi, non solo da chi si
trovava nel raggio della radio.

Uso attuale
------------

Oggi l'APRS viene utilizzato in tutto il mondo per il tracciamento di
veicoli ed escursionisti, il reporting di stazioni meteo, la
messaggistica di testo a corto raggio tra operatori, il supporto a
eventi e reti, la logistica di ricerca e soccorso, e semplicemente per
monitorare chi è attivo localmente. Il traffico viene scambiato su una
frequenza VHF condivisa in ciascuna regione (comunemente 144.390 MHz in
Nord America; altre frequenze si applicano altrove), ritrasmesso
localmente dai **digipeater**, e collegato alla rete globale
**APRS-IS** tramite gli **IGate** — gli stessi due ruoli implementati
da questo firmware. Siti aggregatori come `aprs.fi <https://aprs.fi>`__
permettono a chiunque di osservare quel traffico globale su una mappa
nel browser. Molte radio commerciali per radioamatori includono ormai
l'APRS di fabbrica, ed è cresciuto un vasto ecosistema di software open
source — trattato in dettaglio alla fine della pagina successiva —
attorno al protocollo, su piattaforme desktop, mobile ed embedded.

Come è organizzata questa documentazione
===========================================

Questa documentazione è organizzata in tre parti, ciascuna con il proprio
insieme di capitoli:

* **Funzionalità** — ciò che la stazione *fa* dal punto di vista dell'operatore:
  gateway, digipeating, beacon, messaggistica, meteo, telemetria, bollettini,
  oggetti, il bot Telegram opzionale e l'amministrazione web.
* **Capacità** — le *proprietà* del firmware trasversali alle funzioni: i
  profili del modem, il filtraggio, la localizzazione, l'archiviazione, OTA, la
  rete e il supporto hardware.
* **Interni** — come è *costruito*: la sequenza di avvio, la mappa dei task, il
  flusso dei dati, la catena di segnale DSP, il motore di configurazione e il
  registro dei sensori.

.. toctree::
   :maxdepth: 2
   :caption: Per iniziare

   hardware
   getting-started

.. toctree::
   :maxdepth: 2
   :caption: Funzionalità

   functionality/igate
   functionality/brandmeister
   functionality/digipeater
   functionality/beacons
   functionality/messaging
   functionality/query
   functionality/weather
   functionality/telemetry
   functionality/bulletins-objects
   functionality/telegram
   functionality/web-admin

.. toctree::
   :maxdepth: 2
   :caption: Capacità

   capability/modem
   capability/filtering
   capability/networking
   capability/storage-ota
   capability/localization

.. toctree::
   :maxdepth: 2
   :caption: Interni

   internals/architecture
   internals/dsp-signal-chain
   internals/configuration
   internals/sensor-framework
   internals/source-map

.. toctree::
   :maxdepth: 1
   :caption: Riferimento

   reference/config-json
   reference/http-routes
   reference/troubleshooting
   reference/limitations
   reference/aprs-coverage
   reference/credits
