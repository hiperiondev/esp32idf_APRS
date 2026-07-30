.. _it-getting-started:

===========
Primi passi
===========

Prerequisiti
============

* **ESP-IDF v5.1 o successivo** (bloccato/testato su **5.5.4** — vedi
  ``dependencies.lock``).
* Un ESP32 con **≥ 4 MB di flash**.
* Il gestore dei componenti di IDF scarica ``joltwallet/littlefs`` e, tramite il
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
   * - Hostname
     - ``esp32idf_APRS``
   * - Frequenza CPU
     - 160 MHz
   * - Fuso orario
     - 7.0 (l'orologio stesso è sempre UTC)
   * - Host NTP
     - ``pool.ntp.org``, ``time.google.com``, ``time.cloudflare.com``
   * - IGate
     - abilitato, ``rf2inet`` attivo, ``inet2rf`` inattivo
   * - Indicativo / SSID
     - ``NOCALL`` / 10, passcode ``-1``
   * - Host / porta APRS-IS
     - ``aprs.dprns.com`` : 14580
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
   * - FX.25
     - disattivato
   * - PTT
     - GPIO26 (la polarità è di compilazione)
   * - Messaggistica
     - abilitata, RF + INET, cifratura disattivata

.. danger::

   **Cambia** ``NOCALL`` **e imposta un passcode reale prima di trasmettere.**
   Verifica di essere autorizzato per la frequenza e il ciclo di lavoro che stai
   per attivare.
