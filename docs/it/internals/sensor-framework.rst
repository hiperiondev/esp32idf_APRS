.. _it-sensor-framework:

========================
Il framework dei sensori
========================

``sensors_local`` (``components/sensors_local/``) è il framework a runtime che
permette a sensori hardware reali (o simulati) di alimentare i sottosistemi di
Report Meteo e Telemetria della propria stazione, **senza che il nucleo necessiti
mai di una lista cablata** di "i sensori che questa build supporta". Se vuoi
collegare un BME280, un DS18B20, un ADS1115, una sonda di umidità del suolo, un
partitore di tensione di batteria o qualsiasi altra cosa, questo è il meccanismo
da usare.

Perché un framework di driver
=============================

Firmware APRS precedenti di questo lignaggio usavano un array di dimensione fissa
di "slot di sensore" in ``g_config``, ciascuno descritto da un ``type``/``port``/
``address`` numerico che qualche istruzione ``switch`` centrale interpretava. Ogni
nuovo sensore significava modificare quello switch, ricompilare, e sperare che gli
ID numerici non collidessero.

``sensors_local`` inverte questo:

* Il nucleo (``sensors_local.c``) **non sa nulla** di alcun sensore specifico.
  Mantiene solo una lista di strutture "driver" opache e chiama una manciata di
  puntatori a funzione su di esse.
* Ogni sensore reale vive nel **proprio file ``.c``** sotto ``drivers/`` e si
  aggiunge alla lista **automaticamente all'avvio**, prima ancora che
  ``app_main()`` venga eseguita, tramite un costruttore C nascosto dietro la macro
  ``SENSORS_LOCAL_DRIVER_AUTOREGISTER``.

Il risultato pratico: aggiungere un sensore è "lascia un nuovo file in
``drivers/``, elencalo nel ``CMakeLists.txt`` del componente, ricompila" — niente
in ``sensors_local.c``, ``weather.c``, ``sensors_local.h`` o alcun header deve
cambiare.

Le due famiglie di payload
==========================

Un driver riempie campi di livello applicazione già raggruppati per tipo di
payload APRS, definiti nel componente separato ``weather_telemetry``:

.. list-table::
   :header-rows: 1
   :widths: 20 26 54

   * - Famiglia
     - Bit
     - Struttura di destinazione / consumatore
   * - **Meteo**
     - ``SENSOR_LOCAL_DATA_WEATHER`` (``1u<<0``)
     - ``aprs_weather_report_t`` → ``weather.c`` → beacon WX in onda
   * - **Telemetria**
     - ``SENSOR_LOCAL_DATA_TELEMETRY`` (``1u<<1``)
     - ``aprs_telemetry_report_t`` (A1–A5 + B1–B8) → ``telemetry.c`` → beacon
       ``T#nnn``
   * - *(riservato)*
     - es. ``SENSOR_LOCAL_DATA_GPS = 1u<<2``
     - una struttura futura — vedi sotto

Un singolo driver può annunciare **l'uno o l'altro o entrambi** i bit nelle sue
``capabilities``. ``SENSOR_LOCAL_DATA_ALL`` è l'OR di ogni bit attualmente
definito; è disponibile per un consumatore che voglia davvero tutte le famiglie
insieme. ``weather.c`` **non** lo usa nella sua passata a 1 Hz: aggiorna la
telemetria con una chiamata aggregata ``SENSOR_LOCAL_DATA_TELEMETRY`` e poi legge
ogni campo meteorologico separatamente con
``sensors_local_save_one(..., SENSOR_LOCAL_DATA_WEATHER)``, così che ogni campo
WX rispetti il driver scelto per esso nella pagina Weather.

Anatomia di un driver
=====================

Ogni driver è un'istanza di ``sensor_local_driver_t``:

.. code-block:: c

   struct sensor_local_driver {
       const char *name;      // id stabile, unico, leggibile da umani
       uint32_t capabilities; // OR di WEATHER / TELEMETRY (deve essere non-zero)

       sensor_local_init_fn_t init; // avvio singolo opzionale (può essere NULL)
       sensor_local_save_fn_t save; // OBBLIGATORIO: legge il sensore

       const sensor_local_properties_t *properties; // quali campi WX / canali TLM

       void *ctx; // stato privato del driver, opaco al registro

       bool initialized; // di proprietà del registro
       bool failed;       // di proprietà del registro
   };

I due ruoli di puntatore a funzione:

* **``init(self)``** — chiamato al più una volta, pigramente, la prima volta che
  il driver è necessario (o ansiosamente all'avvio). Apre il bus, sonda il chip,
  alloca stato privato. Restituisce ``ESP_OK`` in caso di successo; qualsiasi
  altro valore **marca il driver come fallito permanentemente** e viene saltato da
  quel punto in poi.
* **``save(self, data, kind)``** — IL punto di ingresso comune, chiamato a ogni
  ciclo di aggiornamento (1 Hz). ``kind`` è già mascherato solo ai bit che sia il
  chiamante vuole *sia* il driver ha annunciato. Il driver legge il suo sensore e
  scrive direttamente nel contenitore ``data`` di proprietà del chiamante,
  attivando il flag ``enabled[…]`` di ogni campo. Deve tollerare una destinazione
  vuota (``data->weather_qty == 0``) non facendo niente per quella famiglia.

Deliberatamente non esiste un callback di smontaggio: il firmware non rimuove
mai un driver, quindi uno slot per la pulizia prometterebbe solo lavoro che non
potrebbe mai essere eseguito.

Il registro
===========

``sensors_local.c`` implementa il registro come un piccolo array protetto da
mutex, espandibile su heap, di **puntatori** a driver (mai copie — la memoria
della tua struttura ``static`` è ciò che vive nella tabella):

.. code-block:: text

   sensors_local_init()          // crea il mutex del registro
   sensors_local_register(drv)   // aggiunge; rifiuta save NULL, nome vuoto,
                                 //   nome duplicato, o capabilities == NONE
   sensors_local_count()         // quanti driver registrati
   sensors_local_get(index)      // ottieni per posizione (menu pagina Weather)
   sensors_local_init_all()      // init() ansiosamente ogni driver
   sensors_local_save(data,kind) // percorre la tabella; init() pigro, poi save()
   sensors_local_save_one(i,...) // legge UN driver per indice (anteprima live)

``sensors_local_register()`` può essere eseguito **prima che esista il
pianificatore di FreeRTOS**, perché ``SENSORS_LOCAL_DRIVER_AUTOREGISTER`` scatta
da un ``__attribute__((constructor))``. A quel punto il mutex del registro non
esiste ancora — gli helper di lock/unlock sono no-op mentre è NULL, ciò che è
sicuro solo perché tutta quella fase è a singolo thread. La prima vera chiamata a
``sensors_local_init()`` (da ``weather_start()``) crea il mutex e rende
thread-safe ogni accesso successivo.

Che un driver fallisca il suo ``init()`` o restituisca un errore da ``save()`` è
registrato e **saltato**; non aborta mai la passata per gli altri driver.

Flusso dei dati end-to-end
==========================

.. code-block:: text

   avvio (prima di app_main)
     └─ costruttore di ogni drivers/*.c → SENSORS_LOCAL_DRIVER_AUTOREGISTER
          → sensors_local_register(&my_driver)

   weather_start()  (una volta, all'avvio)
     ├─ sensors_local_init()          ← crea il mutex del registro
     ├─ sensors_local_init_all()      ← esegue init() su ogni driver
     └─ registra weather_service_1hz() e weather_beacon_service()

   weather_service_1hz()   (1 Hz)
     ├─ pulisce i flag "enabled" del contenitore
     ├─ sensors_local_save(&data, SENSOR_LOCAL_DATA_TELEMETRY)
     │    └─ ogni driver con capacità TELEMETRY: init() pigro, poi save()
     ├─ per ogni campo WX f con wx_sensor_enable[f] e canale assegnato:
     │    └─ sensors_local_save_one(wx_sensor_ch[f], &scratch, ..._WEATHER)
     │         └─ copia solo quel campo nel report vivo
     └─ accumula ogni campo "Averaged" in una somma/conteggio correnti

   weather_beacon_service()   (ogni wx_interval s, se wx_en)
     ├─ risolve i campi (live o mediato, secondo casella)
     ├─ costruisce la riga TNC2 "!lat/lon_WIND…"
     └─ trasmette in RF e/o APRS-IS

Il punto chiave per chiunque aggiunga un sensore: **non chiami mai tu stesso nulla
di ``weather.c`` o dell'amministrazione web.** Registrare il driver è tutta
l'integrazione; l'aggiornamento a 1 Hz, la media, la codifica WX in onda e il
selettore di canale lo scoprono tutto attraverso il registro.

Aggiungere un sensore, passo dopo passo
=======================================

#. **Decidi la famiglia.** Un BME280/DS18B20 è Meteo; un partitore di batteria,
   interruttore reed o sonda del suolo è Telemetria (analogica o digitale); una
   scheda combo può essere entrambe.
#. **Copia uno scheletro.** Copia il driver di esempio corrispondente
   (``drivers/example/sensor_local_weather_example.c`` o
   ``…_telemetry_example.c``) in una nuova cartella, es. ``drivers/bme280/``, con
   il proprio ``bme280_properties.h``.
#. **Riempi ``init()``** — configura/sonda il bus, leggi il chip-ID, alloca
   memoria di calibrazione in ``self->ctx``, restituisci ``ESP_OK`` solo quando sei
   sicuro.
#. **Riempi ``save()``** — leggi il sensore, converti nelle unità ingegneristiche
   che ``weather_telemetry.h`` documenta (°F, mph, decimi di mb, centesimi di
   pollice…), scrivi il/i valore(i) e attiva il/i flag ``enabled[…]``
   corrispondente(i). Controlla sempre prima ``kind`` e i puntatori di
   destinazione.
#. **Dichiara il descrittore** e ``SENSORS_LOCAL_DRIVER_AUTOREGISTER(...)`` sopra
   di esso. ``name`` deve essere unico — è ciò che appare nel menu a tendina della
   pagina Weather.
#. **Elenca il sorgente** in ``components/sensors_local/CMakeLists.txt`` e
   ``idf.py build``. Il componente collega con ``WHOLE_ARCHIVE`` così che il
   ``--gc-sections`` del linker non possa scartare un oggetto il cui unico
   riferimento è il proprio costruttore.
#. **Mappalo nella pagina Weather (o Telemetry)** — il nome del tuo driver ora
   appare automaticamente nel menu a tendina di canale di ogni campo rilevante.

Istanze multiple, gestione errori, thread safety
================================================

* **Istanze multiple** dello stesso tipo di sensore coesistono: dai a ciascuna un
  ``name`` distinto (``bme280-indoor`` / ``bme280-outdoor``), il proprio ``ctx``,
  e il proprio indirizzo/bus/GPIO incorporato in quel ``ctx``.
* Un errore di ``init()`` marca il driver come fallito **permanentemente** (fino a
  disregistra+registra). Un errore di ``save()`` è registrato e saltato **solo per
  quel ciclo** — il tick successivo lo riprova, così che un singhiozzo occasionale
  del bus non disabiliti il driver.
* Le chiamate al registro sono tutte protette da mutex.
  L'``init()``/``save()`` proprio di un driver **non** è avvolto dal
  framework in alcun lock — se il ``ctx`` di un driver è toccato da qualcosa di
  diverso dall'aggiornamento a 1 Hz (es. una ISR), il driver è responsabile della
  propria sincronizzazione.

Il driver BMP180 integrato
==========================

``drivers/bmp180/bmp180.c`` è un driver I2C di temperatura/pressione reale
costruito su ``esp-idf-lib/bmp180``. I suoi pin I2C sono configurabili tramite
``#define`` in ``BMP180.h`` (predefinito GPIO21 = SDA, GPIO22 = SCL); quei pin
sono esclusi da ogni selettore GPIO dell'amministrazione web così che non possano
essere doppio-assegnati. Annuncia Meteo e scrive temperatura e pressione
barometrica. È condizionato dietro ``CONFIG_SENSORS_LOCAL_BMP180_DRIVER``.

Aggiungere una *classe* di sensore completamente nuova
======================================================

Meteo e Telemetria non sono le uniche famiglie che il framework può portare. Per
aggiungere, diciamo, GPS:

#. Aggiungi ``SENSOR_LOCAL_DATA_GPS = 1u << 2`` a ``sensor_local_data_kind_t`` e
   aggiungilo in OR a ``SENSOR_LOCAL_DATA_ALL``.
#. Aggiungi la struttura di destinazione in cui atterra un fix GPS (a
   ``weather_telemetry.h``).
#. Scrivi driver le cui ``capabilities`` includano il nuovo bit.
#. Filtra il registro con ``driver->capabilities & SENSOR_LOCAL_DATA_GPS`` ovunque
   un consumatore necessiti della nuova classe. Il registro,
   ``sensors_local_save()`` e ogni driver esistente restano completamente
   inalterati.
