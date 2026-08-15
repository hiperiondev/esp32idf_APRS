.. _it-getting-started:

===========
Primi passi
===========

Prerequisiti
============

* **ESP-IDF v6.0 o successivo** (bloccato/testato su **6.0.2** — vedi
  ``dependencies.lock``).
* Un ESP32 con **≥ 4 MB di flash**.
* Il gestore dei componenti di IDF scarica ``joltwallet/littlefs``,
  ``espressif/cjson`` e, tramite il
  componente ``sensors_local``, ``esp-idf-lib/bmp180`` (che trascina ``i2cdev`` +
  ``esp_idf_lib_helpers``) automaticamente.

Compilazione e scrittura
========================

.. code-block:: bash

   . $IDF_PATH/export.sh

   cd workspace-APRS/esp32_APRS_igate

   idf.py set-target esp32          # sdkconfig arriva già con target=esp32
   idf.py build
   idf.py -p /dev/ttyUSB0 flash monitor

Compila in spagnolo o italiano invece che in inglese (vedi :ref:`it-localization`):

.. code-block:: bash

   idf.py build -DLANGUAGE=LANG_ES
   idf.py build -DLANGUAGE=LANG_IT

.. tip::

   ``sdkconfig`` arriva con ``CONFIG_COMPILER_OPTIMIZATION_DEBUG`` (``-Og``) e le
   asserzioni attive. Passa a ``-Os`` se sei a corto di flash.

Budget di memoria
=================

L'ESP32 di questo progetto non ha PSRAM, quindi ogni byte di DRAM interna che
la build riserva staticamente è un byte che l'heap non riceve mai.
``sdkconfig`` è tarato per questo, e i valori qui sotto sono deliberati:
alzarne uno qualsiasi abbassa la cifra *Min free heap* del pannello.

.. list-table::
   :header-rows: 1
   :widths: 44 10 46

   * - Opzione
     - Valore
     - Perché
   * - ``CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM``
     - 6
     - ~1,6 KB ciascuno, allocati in ``esp_wifi_init()`` e trattenuti finché il
       Wi-Fi non viene de-inizializzato. Sei coincide con
       ``CONFIG_ESP_WIFI_RX_BA_WIN``, che è il minimo richiesto da AMPDU RX.
   * - ``CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM`` / ``..._TX_...``
     - 12
     - Limita il picco di heap richiesto dal driver Wi-Fi. Il traffico APRS è
       di qualche centinaio di byte al minuto; la banda che questi buffer
       comprano non viene mai usata.
   * - ``CONFIG_LWIP_TCP_SND_BUF_DEFAULT`` / ``CONFIG_LWIP_TCP_WND_DEFAULT``
     - 2880
     - Due MSS per direzione e per connessione. L'unico trasferimento
       prolungato è il caricamento di un'immagine OTA, che con questa finestra
       satura comunque una LAN.
   * - ``max_open_sockets`` in ``web_server_start()``
     - 4
     - httpd prende questo numero più 3 socket propri dal pool di
       ``CONFIG_LWIP_MAX_SOCKETS`` (10). I 3 rimanenti servono al collegamento
       APRS-IS, al DNS e a SNTP per restare attivi mentre qualcuno naviga le
       pagine di amministrazione.
   * - Livello TLS di mbedTLS, bundle di certificati, Wi-Fi Enterprise
     - disattivati
     - L'amministrazione web è HTTP in chiaro e il collegamento APRS-IS è TCP
       in chiaro. mbedTLS è collegato per una sola chiamata,
       ``mbedtls_base64_decode()`` nell'autenticazione HTTP Basic, che non
       dipende da ``CONFIG_MBEDTLS_TLS_ENABLED``.

.. note::

   Da ESP-IDF v6.0 il port di mbedTLS chiama ``psa_crypto_init()`` da un hook
   di avvio di sistema, quindi PSA Crypto è attivo in ogni build che collega
   mbedTLS, questa compresa. Questo, insieme alla maggiore impronta statica di
   mbedTLS 4.x, è il motivo per cui lo stesso firmware riporta meno heap
   libero sotto v6.0 rispetto a v5.2 con una configurazione per il resto
   identica.

Primo avvio
===========

#. Su una partizione nuova, LittleFS si auto-formatta e ``app_config_load()``
   scrive ``/storage/config.json`` con i valori di fabbrica.
#. L'ESP32 si avvia come **AP Wi-Fi**: SSID ``esp32idf_APRS``, password
   ``esp32idf_APRS``, canale 1, WPA2-PSK, max 4 client.
#. Uniscici e naviga al dispositivo (predefinito ``http://192.168.4.1/``).
#. **Accedi:** ``admin`` / ``admin`` — cambialo nella pagina *System*.
#. In *Wireless*: scegli **Station** o **AP+STA**, spunta **Enable** in un blocco
   Client Wi-Fi, inserisci SSID/password, Salva.
#. In *IGate*: imposta il tuo **indicativo**, **SSID**, **passcode**,
   **host**/**porta** APRS-IS, filtro, coordinate, simbolo, commento.
#. In *Radio / Modem*: abilita il modem audio, scegli la modulazione, il
   preambolo, lo slot temporale TX; usa **LOOP TEST** per verificare il percorso
   audio.
#. Riavvia (o Salva — la maggior parte delle impostazioni si riapplica in tempo
   reale).

Valori di fabbrica notevoli
===========================

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Impostazione
     - Predefinito
   * - Modalità Wi-Fi
     - AP (sempre raggiungibile)
   * - SSID / password AP
     - ``esp32idf_APRS`` / ``esp32idf_APRS``
   * - Login web
     - ``admin`` / ``admin``
   * - Frequenza CPU
     - 240 MHz
   * - Orologio di sistema
     - sempre UTC (TZ=UTC0). Il selettore di fuso orario della pagina System
       (default ``UTC``) cambia solo la data/ora locale mostrata nella
       dashboard; i timestamp in onda restano zulu
   * - Host NTP
     - ``pool.ntp.org``, ``time.google.com``, ``time.cloudflare.com``
   * - IGate
     - abilitato, ``rf2inet`` attivo, ``inet2rf`` inattivo
   * - Indicativo / SSID
     - ``NOCALL`` / 10, passcode ``-1``
   * - Coordinate della stazione
     - ``0.000`` / ``0.000``, trasmesse così come sono
   * - Server APRS-IS
     - quattro slot di failover, tutti preimpostati a ``aprs.dprns.com`` :
       14580, con il solo slot 1 abilitato
   * - Elenco digipeater satellitari
     - ``RS0ISS``, ``YBOX``, ``YBSAT``, ``PSAT``, ``W3ADO``, ``BJ1SI`` (fino a 8, configurabile dal web)
   * - Cache / finestra soppressione duplicati
     - 20 voci / 30000 ms (configurabile dal web)
   * - Limitatore di duty cycle di trasmissione
     - disabilitato; tetto del 25 % di una finestra scorrevole di 10 minuti
       quando abilitato
   * - Preset di percorso 0
     - ``WIDE1-1,WIDE2-1``
   * - Digipeater
     - disabilitato, SSID 1
   * - Tracker
     - disabilitato, SSID 9
   * - Modem audio
     - abilitato, 1200 Bd Bell 202
   * - Preambolo / slot TX
     - 300 ms / 2000 ms
   * - Persistenza CSMA
     - 63 (~25 % di probabilità di trasmettere per slot libero)
   * - Buffer di TX RF
     - 1
   * - Risponditore di query
     - disabilitato; RF attivo, Internet spento, intervallo minimo di
       risposta 30 s
   * - FX.25
     - disattivato
   * - PTT
     - GPIO26 (la polarità è di compilazione)
   * - Messaggistica
     - abilitata, RF + INET, GPIO di allarme disabilitato

.. danger::

   **Cambia** ``NOCALL`` **e imposta un passcode reale prima di trasmettere.**
   Verifica di essere autorizzato per la frequenza e il ciclo di lavoro che stai
   per attivare.

   **Imposta anche le coordinate della stazione.** APRS non ha una coordinata
   di "posizione sconosciuta": una stazione che trasmette il beacon prima di
   averle impostate mette in aria 0° N / 0° E, che è una posizione reale nel
   golfo di Guinea e non una posizione assente, e ogni mappa la disegna lì.
