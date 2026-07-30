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
#. Chiama ``sensors_local_save(&weather_telemetry_data, SENSOR_LOCAL_DATA_ALL)``,
   che percorre il registro e, per ogni driver capace, lo inizializza pigramente
   se serve e chiama il suo ``save()`` — il driver scrive direttamente in
   ``aprs_weather_report_t`` / ``aprs_telemetry_report_t``.
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

Blocco
======

Poiché un driver può aggiornare il contenitore in concorrenza mentre il beacon lo
legge, ogni accesso passa per ``weather_lock()`` / ``weather_unlock()``. Tratta
``weather_telemetry_data`` come sola-lettura al di fuori di ``weather.c``.

.. seealso::

   :ref:`it-sensor-framework` — come collegare un sensore reale (BME280, DS18B20,
   BMP180…) così che le sue letture alimentino questi campi.
