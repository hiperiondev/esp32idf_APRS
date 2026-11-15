.. _it-sensors:

==============
Sensori locali
==============

I rapporti meteo e la telemetria hanno bisogno di numeri da inviare, e quei
numeri vengono da sensori collegati all'ESP32 stesso. Il firmware include
driver per i comuni sensori ambientali I2C; ciascuno si registra da solo
all'avvio e compare come scelta nelle colonne *Canale* delle pagine *Meteo* e
*Telemetria*. Come funziona il registro, e come aggiungere un proprio driver,
è descritto in :ref:`it-sensor-framework`.

Driver disponibili
==================

.. list-table::
   :header-rows: 1
   :widths: 20 30 22 28

   * - Driver
     - Misura
     - Default
     - Opzione Kconfig
   * - ``bme280``
     - temperatura, umidità, pressione (BME280); temperatura e pressione
       (BMP280)
     - **attivo**
     - ``SENSORS_LOCAL_BME280_DRIVER``
   * - ``bmp180``
     - temperatura, pressione
     - spento
     - ``SENSORS_LOCAL_BMP180_DRIVER``
   * - ``wx-example``
     - valori meteo casuali, senza hardware
     - spento
     - ``SENSORS_LOCAL_WEATHER_EXAMPLE_DRIVER``
   * - ``tlm-example``
     - valori di telemetria casuali, senza hardware
     - spento
     - ``SENSORS_LOCAL_TELEMETRY_EXAMPLE_DRIVER``

I driver si scelgono con ``idf.py menuconfig`` → *Sensors Local*. Il driver
``bme280`` legge l'ID del chip all'avvio: un BME280 offre temperatura,
umidità e pressione, un BMP280 (lo stesso componente senza l'elemento di
umidità) solo temperatura e pressione. I due driver reali possono essere
attivi insieme; ogni chip risponde al proprio indirizzo I2C.

I due driver di esempio servono per i test al banco delle pagine e dei
codificatori. Una volta assegnati a un canale i loro valori sono
indistinguibili da letture reali e **vengono trasmessi in aria**, quindi vanno
lasciati spenti su una stazione collegata a una radio.

Cablaggio
=========

Tutti i sensori condividono un unico bus I2C:

.. list-table::
   :header-rows: 1
   :widths: 30 30 40

   * - Segnale
     - Pin dell'ESP32
     - Nota
   * - SDA
     - GPIO21
     - ``SENSORS_LOCAL_I2C_SDA_GPIO``
   * - SCL
     - GPIO22
     - ``SENSORS_LOCAL_I2C_SCL_GPIO``
   * - VCC / GND
     - 3V3 / GND
     -

I pin e la porta del bus sono definiti solo in
``components/sensors_local/include/sensors_local_i2c.h``, e l'amministrazione
web li segna come occupati (*Sensor I2C*) nei suoi selettori di GPIO, come
quello del pin di allarme messaggi, così non possono essere assegnati due
volte. Il BME280/BMP280 è atteso all'indirizzo 0x76 (``BME280.h``); un modulo
con SDO al positivo risponde su 0x77 e richiede di cambiare quella costante.

Assegnare i sensori ai rapporti
===============================

.. figure:: /_static/webadmin/wx_fs02.png
   :alt: Mappatura sensori (abilita / media / canale sorgente) — la tabella di assegnazione della pagina Meteo. Ogni campo WX sceglie il canale sensore da cui viene letto; l'elenco offre solo i driver in grado di produrre quel campo, e la colonna Valore mostra la lettura dal vivo.
   :align: center
   :width: 90%

   **Mappatura sensori (abilita / media / canale sorgente)** — la tabella di assegnazione della pagina Meteo. Ogni campo WX sceglie il canale sensore da cui viene letto; l'elenco offre solo i driver in grado di produrre quel campo, e la colonna *Valore* mostra la lettura dal vivo.

.. figure:: /_static/webadmin/tlm_fs03.png
   :alt: Canali analogici (A1-A5) — la pagina Telemetria offre gli stessi selettori per i canali analogici e digitali; con il solo BME280 attivo non esiste nessuna sorgente di telemetria e gli elenchi restano su (nessuno).
   :align: center
   :width: 90%

   **Canali analogici (A1-A5)** — la pagina Telemetria offre gli stessi selettori per i canali analogici e digitali; con il solo BME280 attivo non esiste nessuna sorgente di telemetria e gli elenchi restano su *(nessuno)*.

Le letture sono aggiornate una volta al secondo; per un campo meteo spuntato
nella colonna *Media* il beacon invia la media di quelle letture invece
dell'ultima. L'assegnazione è memorizzata per nome del driver, quindi
abilitare o disabilitare un altro driver non sposta le assegnazioni esistenti.
Il comando ``/sensors`` del bot Telegram elenca ogni campo assegnato con il
suo valore attuale.
