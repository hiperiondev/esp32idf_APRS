.. _it-weather:

====================
Report meteorologico
====================

Il sottosistema ``weather`` (``main/weather.c``) è un report meteorologico APRS
della propria stazione pienamente funzionante, non un'impalcatura. Possiede
l'unico contenitore condiviso ``weather_telemetry_data_t`` in cui scrive ogni
driver di sensore locale, lo aggiorna dal registro ``sensors_local`` una volta al
secondo, e periodicamente codifica e trasmette un report meteorologico APRS
standard in RF e/o APRS-IS dai campi che l'operatore ha mappato nella pagina web
*Weather* (``g_config.wx_*``).

Le tre parti mobili
===================

``weather_start()`` (chiamata una volta all'avvio) configura:

#. **Il contenitore condiviso.** ``weather_telemetry_data`` è collegato a
   memoria statica di supporto per un ``aprs_weather_report_t`` e un
   ``aprs_telemetry_report_t``.
#. **Il registro.** ``sensors_local_init()`` crea il mutex del registro e
   ``sensors_local_init_all()`` esegue l'``init()`` di ogni driver
   auto-registrato.
#. **Due callback di servizio.** ``weather_service_1hz()`` (eseguita a 1 Hz dal
   tick del servizio APRS) e ``weather_beacon_service()`` (eseguita dal
   pianificatore di beacon condiviso).

L'aggiornamento a 1 Hz
======================

``weather_service_1hz()``:

#. Pulisce i flag "abilitato" del contenitore, così che un driver che smetta di
   riportare un campo questo ciclo non lasci un valore obsoleto che sembra valido.
#. Aggiorna la famiglia telemetria con una sola chiamata aggregata,
   ``sensors_local_save(&weather_telemetry_data, SENSOR_LOCAL_DATA_TELEMETRY)``:
   i canali di telemetria non sono selezionabili per campo, quindi contribuisce
   ogni driver con capacità TELEMETRY.
#. Risolve **ogni campo meteorologico in modo indipendente** contro l'unico
   driver che l'operatore ha scelto per esso. Un campo viene campionato solo se
   è spuntato (``g_config.wx_sensor_enable[f]``) e ha un canale sorgente
   assegnato (``g_config.wx_sensor_ch[f] != SENSOR_LOCAL_CH_NONE``); la lettura
   è presa con ``sensors_local_save_one(ch, &scratch, SENSOR_LOCAL_DATA_WEATHER)``
   su un contenitore temporaneo, e solo il valore di quel campo viene copiato
   nel report vivo. Usare un contenitore temporaneo per campo è ciò che impedisce
   a un secondo driver WEATHER registrato di sovrascrivere un campo già risolto
   da un altro, ed è il motivo per cui il pacchetto in onda corrisponde sempre
   alla colonna *Channel* di ogni campo e all'anteprima *Value* live della
   pagina.
#. Accumula ogni campo marcato come *Averaged* (una casella per campo nella pagina
   Weather) in una somma/conteggio correnti.

Campi meteorologici mappabili
=============================

La lista dei campi in onda è l'insieme canonico del capitolo 12 di APRS101 più le
proposte di inondazione di APRS 1.2, enumerati da ``wx_field_id_t``:

.. list-table::
   :header-rows: 1
   :widths: 34 22 44

   * - Campo
     - Token in onda
     - Unità
   * - Direzione del vento
     - ``ddd/``
     - gradi
   * - Velocità del vento (sostenuta)
     - ``/sss``
     - mph
   * - Raffica di vento
     - ``gXXX``
     - mph
   * - Temperatura
     - ``tXXX``
     - °F
   * - Pioggia ultima ora
     - ``rXXX``
     - 1/100 poll
   * - Pioggia ultime 24 h
     - ``pXXX``
     - 1/100 poll
   * - Pioggia da mezzanotte
     - ``PXXX``
     - 1/100 poll
   * - Neve ultime 24 h
     - ``sXXX``
     - 1/10 poll (APRS 1.2)
   * - Umidità
     - ``hXX``
     - %
   * - Pressione barometrica
     - ``bXXXXX``
     - decimi di mb
   * - Luminosità
     - ``LXXX`` / ``lXXX``
     - W/m² (APRS 1.2)
   * - Altezza di inondazione (piedi)
     - ``FXXXX.X``
     - piedi (APRS 1.2)
   * - Altezza di inondazione (metri)
     - ``fXXXX.X``
     - metri (APRS 1.2)
   * - Contatore di pioggia grezzo
     - ``#XXXX``
     - conteggi della bascula, non scalati

Il contatore di pioggia grezzo è il caso a parte: è il conteggio corrente dei
ribaltamenti della bascula del pluviometro stesso, non una misura in centesimi di
pollice, e la stazione non lo azzera mai. Un ricevitore ricava la pioggia
sottraendo due rapporti, ed è questo a renderlo utile in un sito non presidiato i
cui altri campi di pioggia dipendono dal fatto che la stazione sia rimasta accesa
abbastanza a lungo da accumularli. Viene trasmesso senza scalatura, con quattro
cifre, e si riavvolge nella larghezza del campo come fa il contatore stesso.

Il beacon WX
============

``weather_beacon_service()`` trasmette ogni ``g_config.wx_interval`` secondi
(solo quando ``wx_en`` è attivo):

#. **Risolvi i campi.** Per ogni token WX in onda, legge o il valore in tempo
   reale direttamente dal contenitore, o il valore mediato accumulato
   dall'aggiornamento a 1 Hz, secondo la casella *Averaged* di quel campo — così
   che un reporter intermittente contribuisca comunque con una media ragionevole.
#. **Costruisci il pacchetto.** Rende la riga TNC2 standard
   ``!lat/lon_WIND/SPDgGUSTtTTTrRRRhHHbBBBBB…``.
#. **Trasmettilo** in RF e/o APRS-IS secondo ``wx_2rf`` / ``wx_2inet``.

Commento e identificatore software
==================================

Ogni rapporto termina con l'identificatore di tipo software / unità
meteorologica di APRS101 cap.12, ``xESP``: la lettera del tipo software seguita
dalla stringa che nomina la famiglia di sensori basata su ESP32 di questo
firmware.

La specifica definisce quell'identificatore come il token che chiude i dati
meteorologici e non definisce affatto un commento in testo libero per un
rapporto meteorologico, quindi i due non possono stare entrambi al loro posto
nominale. Questo firmware mette il commento dell'operatore
(``g_config.wx_comment``) tra i dati meteorologici e l'identificatore, per cui
l'ordine in onda è:

.. code-block:: text

   =DDMM.mmN/DDDMM.mmW_<token meteorologici><commento>xESP

In questo modo un decodificatore che legge la stringa di unità fino a fine riga
non può inglobare il commento, e uno che scorre a ritroso dalla fine trova
comunque l'identificatore dove se lo aspetta. Tutti e quattro i layout di
rapporto (oggetto, posizione con timestamp, posizione senza timestamp e senza
posizione) usano lo stesso ordine, e l'identificatore compare esattamente una
volta per rapporto.

Blocco
======

Poiché un driver può aggiornare il contenitore in concorrenza mentre il beacon lo
legge, ogni accesso passa per ``weather_lock()`` / ``weather_unlock()``. Tratta
``weather_telemetry_data`` come sola-lettura al di fuori di ``weather.c``.

.. seealso::

   :ref:`it-sensor-framework` — come collegare un sensore reale (BME280, DS18B20,
   BMP180…) così che le sue letture alimentino questi campi.
