.. _it-architecture:

============
Architettura
============

Sequenza di avvio
=================

``app_main()`` viene eseguita nel task principale del sistema, il cui stack è
impostato da ``CONFIG_ESP_MAIN_TASK_STACK_SIZE`` e non è pensato per ospitare
lavoro pesante — ``esp_netif`` + ``esp_wifi`` + ``esp_http_server`` + cJSON
possono usare diversi KB di stack tra loro. Quindi ``app_main()`` fa solo le due
cose che devono precedere tutto, e poi cede il controllo a un task dedicato:

.. code-block:: text

   app_main()
    ├─ nvs_flash_init()          (cancella+ritenta su NO_FREE_PAGES / NEW_VERSION_FOUND)
    ├─ storage_init()            (monta LittleFS su /storage, auto-formatta al primo avvio)
    └─ xTaskCreate(app_task, 8192 B, prio 5)   ── e ritorna; FreeRTOS recupera il task principale

   app_task()
    ├─ app_config_load()                  ← /storage/config.json, o scrivi+carica default di fabbrica
    ├─ cpu_freq_apply()                   ← 80/160/240 MHz dalla pagina System
    ├─ net_state_init()                   ← "ancora nessun internet"
    ├─ wifi_init()                        ← AP / STA / AP+STA secondo g_config.wifi_mode
    ├─ vTaskDelay(10 ms)                  ← cedi così IDLE gira; evita un falso scatto del TWDT
    ├─ time_sync_start()                  ← arma la macchina a stati SNTP (non bloccante)
    ├─ web_server_start()                 ← esp_http_server, ~50 gestori di URI, stack da 20 KB
    ├─ (conferma immagine OTA valida se in-attesa-di-verifica)
    ├─ aprs_service_start()               ← ⚠ DEVE precedere modem_init(): installa il callback RX
    ├─ if (audio_modem_en) modem_init()   ← ⏳ SI BLOCCA ~5 s calibrando il clock reale dell'ADC (una volta per avvio)
    │      └─ aprs_service_notify_modem_ready()
    └─ vTaskDelete(NULL)                  ← restituisce lo stack da 8 KB di app_task all'heap

Due regole di ordine sono critiche e sono commentate come tali nel codice
sorgente:

#. **``aprs_service_start()`` prima di ``modem_init()``** — il modem inizia a
   consegnare frame *dall'interno di* ``modem_init()``; il callback RX deve essere
   già installato.
#. **I beacon partono prima che il modem sia pronto** — trasmettono
   immediatamente all'ingresso, quindi ``aprs_service_send_tnc2()`` scarta frame
   con un log di debug finché ``s_modemReady`` non è attivo, invece di raggiungere
   lo scrittore AX.25 prima che il livello AX.25 sia inizializzato.

Dentro ``aprs_service_start()``
===============================

.. code-block:: text

   aprs_service_start()
    ├─ trafficlog_init / lastheard_init / message_init
    ├─ message_set_tx_handler / igate_set_inet2rf_handler
    ├─ modem_set_rx_callback(on_rx_frame)
    ├─ igate_start()                 ← sempre avviato; resta inattivo quando niente richiede APRS-IS
    ├─ beacon_start() / weather_start() / bulletins_start() / objitems_start() / telemetry_start()
    ├─ beacon_scheduler_start()      ← UN task condiviso aziona tutto il TX periodico e le risposte alle query
    └─ xTaskCreate(serviceTickTask)  ← 1 Hz: refresh meteo + ritentativo messaggi + MaS sincro oraria

Mappa dei task
==============

.. list-table::
   :header-rows: 1
   :widths: 20 12 8 10 22 28

   * - Task
     - Stack
     - Prio
     - Core
     - Creato da
     - Ruolo
   * - ``app_task``
     - 8192 B
     - 5
     - qualsiasi
     - ``app_main``
     - avvio, poi si auto-elimina
   * - RX DSP del modem
     - 4096 B
     - 10
     - **0**
     - ``AFSK_init()``
     - drena l'anello dell'ADC, esegue i demodulatori
   * - ``modem_svc``
     - 6144 B
     - 5
     - qualsiasi
     - ``modem_init()``
     - aziona il TX, consegna i frame RX al callback
   * - ISR DMA dell'ADC
     - —
     - —
     - **0**
     - driver
     - frame di conversione → ring buffer
   * - Clock di campionamento del DAC (GPTimer, livello 3)
     - —
     - —
     - **1**
     - ``AFSK_init()``
     - un campione del DAC ogni 1/38400 s
   * - ``igate_task``
     - —
     - —
     - qualsiasi
     - ``igate_start()``
     - socket APRS-IS, login, pompaggio RX, riconnessione
   * - ``beacon_sched``
     - 14336 B
     - 4
     - qualsiasi
     - ``beacon_scheduler_start()``
     - UN task condiviso: tutto il TX periodico della propria stazione, più le
       risposte alle query APRS che gli vengono differite
   * - ``aprs_svc_tick``
     - 10240 B
     - 4
     - qualsiasi
     - ``aprs_service_start()``
     - 1 Hz: refresh meteo + ritentativo messaggi + sincro oraria
   * - ``httpd``
     - 20480 B
     - —
     - qualsiasi
     - ``web_server_start()``
     - amministrazione web
   * - ``loop_diag``
     - 3072 B
     - 7
     - qualsiasi
     - ``aprs_loop_test_run()``
     - transitorio: aggancia le diagnostiche del modem per la durata di un LOOP TEST
   * - ``esp_timer``
     - —
     - —
     - —
     - IDF
     - back-off di riconnessione Wi-Fi

Flusso dei dati
===============

.. image:: /_static/dataflow/dataflow_it.png
   :alt: Diagramma dell'architettura del flusso dei dati dell'ESP32 APRS iGate
   :align: center
   :width: 100%

Il tetto di arretrato TX RF
===========================

``aprs_service_send_tnc2()`` permette un piccolo arretrato invece di scartare
appena un frame è in volo: fino a ``g_config.rf_tx_buffers`` frame possono stare
nell'anello prima che un nuovo pacchetto venga scartato. Il valore è letto fresco
a ogni chiamata (così l'impostazione *TX buffers* si applica al prossimo
pacchetto, senza riavvio), ed è limitato a ``RF_TX_BUFFERS_MIN..RF_TX_BUFFERS_MAX``
— con il massimo derivato da ``AX25_TX_FRAME_RING_MAX``, la profondità utilizzabile
reale dell'anello, così che il livello di configurazione non possa mai accettare
un valore che l'anello non potrebbe sostenere. Solo al task del pianificatore di
beacon è permesso *attendere* che l'anello si drena (vedi :ref:`it-beacons`);
tutti gli altri chiamanti scartano immediatamente, così che un ramo RF occupato
non fermi mai la decodifica RX né il socket APRS-IS.

Costruzione delle righe TNC2
==============================

Ogni modulo che assembla una riga di testo TNC2 — ``beacon.c``, ``weather.c``,
``objects_items.c``, ``query.c`` e ``telemetry.c`` — segue la stessa
convenzione: la riga viene costruita in un buffer di dimensione
``APRS_TNC2_BUF_SIZE`` (``main/include/aprs_service.h``), e un risultato pari
o superiore a quella dimensione, oppure superiore a ``APRS_TNC2_MAX_LEN``,
viene rifiutato con un avviso nel log invece di essere trasmesso troncato. Una
riga scritta a metà è indistinguibile via etere da una ben formata, quindi
rifiutarla del tutto è l'unico esito che non consegna mai a una stazione
ricevente un rapporto plausibile ma errato. Un nuovo modulo che costruisce
righe TNC2 deve seguire la stessa convenzione.
