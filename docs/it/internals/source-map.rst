.. _it-source-map:

=========================
Mappa del codice sorgente
=========================

Un percorso attraverso il repository, così che tu sappia dove guardare. Le
dimensioni sono approssimative. Il C di prima parte somma ~37 k righe tra
``main/`` + ``components/`` (esclusi i ``managed_components/``), di cui ~6,8 k
sono il componente del modem e ~10 k l'amministrazione web.

Disposizione del repository
===========================

.. code-block:: text

   workspace-APRS/esp32_APRS_igate/
   ├── CMakeLists.txt          ← definizione scheda (pin ADC/DAC/PTT/LED) + project()
   ├── partitions.csv          ← nvs / otadata / phy_init / ota_0 / ota_1 / storage (LittleFS)
   ├── sdkconfig               ← target=esp32, flash 4MB, partizioni personalizzate
   ├── dependencies.lock       ← idf 5.5.4, littlefs, esp-idf-lib bmp180/i2cdev/helpers
   ├── LICENSE                 ← GPL-3.0
   ├── schematics/             ← schema KiCad interfaccia radio + PCB
   │
   ├── main/                                   (l'applicazione)
   │   ├── main.c              ← app_main, avvio/riconnessione Wi-Fi, ordine di boot
   │   ├── app_config.c/.h     ← app_config_t, default di fabbrica, load/save JSON
   │   ├── storage.c           ← montaggio/formato/uso LittleFS
   │   ├── aprs_service.c/.h   ← la colla: smistamento RX, helper TX, cfg modem, stats, loop test
   │   ├── aprs_filter.c/.h    ← classificatore payload + filtri portata/prefisso/budlist/terze-parti
   │   ├── aprs_coord.c/.h     ← lat/lon ↔ testo APRS, ambiguità, estrazione simbolo
   │   ├── include/aprs_path.h ← bitmask dei preset di percorso → suffisso ",WIDE1-1,WIDE2-1"
   │   ├── include/str_append.h ← helper di append snprintf limitato, condiviso dai costruttori
   │   ├── include/json_store.h / json_escape.h ← scrittore JSON in streaming + escaping
   │   ├── include/sched_time.h ← secondi monotonici usati da ogni scheduler
   │   ├── beacon.c/.h         ← beacon posizione propria (trk / igate / digi)
   │   ├── weather.c/.h        ← report WX proprio: refresh sensors_local + beacon WX
   │   ├── telemetry.c/.h      ← telemetria propria: A1–A5 + B1–B8, beacon T#nnn + metadati
   │   ├── beacon_scheduler.c/.h ← UN task condiviso che aziona TUTTO il TX periodico + risposte alle query
   │   ├── bulletins.c/.h      ← bollettini APRS BLN1..BLN5 (bulletins.json proprio)
   │   ├── objects_items.c/.h  ← Oggetti/Item APRS (objitems.json proprio)
   │   ├── net_state.c/.h      ← flag "abbiamo davvero internet?"
   │   ├── time_sync.c/.h      ← SNTP (sempre UTC), macchina a stati non bloccante
   │   └── cpu_freq.c/.h       ← esp_pm_configure() dalla pagina System
   │
   ├── components/
   │   ├── esp32idf_radioamateur_modem/    (il modem software — il cuore del progetto)
   │   │   ├── esp32idf_radioamateur_modem.h  ← API pubblica (config, callback RX, helper TX)
   │   │   ├── include/…_config.h             ← TUTTE le costanti di scheda/DSP in compilazione
   │   │   ├── src/afsk.c                      ← ingest DMA ADC, AGC, FIR decimazione, ISR DAC, PTT
   │   │   ├── src/modem.c                     ← correlatori, DPLL, tabelle toni, DCD, calibrazione
   │   │   ├── src/ax25.c                      ← framer HDLC, NRZI, bit-stuffing, codec AX.25, coda TX
   │   │   ├── src/fx25.c, lwfec/rs.c, gf.c    ← FEC Reed–Solomon FX.25
   │   │   └── src/crc_ccit.c                  ← FCS
   │   │
   │   ├── igate/          ← client TCP APRS-IS, login, filtri, dedup, RF→INET / INET→RF
   │   ├── digirepeater/   ← logica percorso WIDEn-N / TRACEn-N / RELAY / ECHO / GATE
   │   ├── message/        ← messaggistica APRS, ack/ritentativo, AES-128-CBC + base64
   │   ├── query/          ← risponditore di query APRS (?APRS?/?WX?/?IGATE? + dirette), risposte dal task dello scheduler
   │   ├── lastheard/      ← tabella in RAM di stazioni sentite, una per nominativo → JSON dashboard
   │   ├── trafficlog/     ← anello in RAM di righe di traffico → JSON dashboard (long-poll per seq)
   │   ├── weather_telemetry/  ← solo strutture di livello protocollo (campi WX + Telemetria APRS101)
   │   ├── sensors_local/      ← IL framework di driver sensori
   │   │   ├── sensors_local.c              ← il registro dinamico
   │   │   ├── include/sensors_local.h      ← API pubblica
   │   │   ├── include/sensor_local_properties.h ← descrittore di capacità per driver
   │   │   └── drivers/<name>/              ← una cartella per driver (auto-registrato)
   │   │       ├── example/…_weather_example.c    ← scheletro WEATHER a dati casuali
   │   │       ├── example/…_telemetry_example.c  ← scheletro TELEMETRY a dati casuali
   │   │       └── bmp180/bmp180.c                ← driver I2C reale temperatura/pressione
   │   └── webconfig/      ← amministrazione esp_http_server
   │       ├── web_server.c            ← tabella delle route
   │       ├── web_common.c            ← auth, analisi form, shell HTML, helper di campo
   │       ├── pages/*.c               ← un file per pagina di amministrazione
   │       └── translations/           ← translations.h + lang_en/es/it.h
   │
   └── managed_components/                     (ottenuti dal gestore dei componenti)
       ├── joltwallet__littlefs/
       ├── esp-idf-lib__bmp180/
       ├── esp-idf-lib__i2cdev/
       └── esp-idf-lib__esp_idf_lib_helpers/

Da dove iniziare a leggere
==========================

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Se vuoi capire…
     - Inizia da…
   * - L'ordine di avvio e la disposizione dei task
     - ``main/main.c``, poi ``main/aprs_service.c``
   * - Come viene smistato un frame ricevuto
     - ``aprs_msg_callback()`` in ``main/aprs_service.c``
   * - Il DSP / perché si scelgono le frequenze di campionamento
     - ``…_modem_config.h``, poi ``src/modem.c`` / ``src/afsk.c``
   * - Gatewaying e filtraggio
     - ``components/igate/igate.c`` + ``main/aprs_filter.c``
   * - Collegare un sensore
     - ``components/sensors_local/`` e :ref:`it-sensor-framework`
   * - Lo schema di configurazione
     - ``main/include/app_config.h``
   * - Una pagina web specifica
     - il ``components/webconfig/pages/page_*.c`` corrispondente
