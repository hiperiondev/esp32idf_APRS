.. _it-gps:

===============
Ricevitore GNSS
===============

All'ESP32 si può collegare un modulo GNSS (GPS) seriale che dà alla stazione
posizione, velocità, rotta e ora UTC dal vivo. Va bene qualsiasi ricevitore
che emetta frasi NMEA 0183 standard a 9600 bit/s (le comuni schede NEO-6M,
ATGM336H e HW-248 lo fanno di serie); il firmware ascolta soltanto e non invia
mai comandi di configurazione al modulo.

Cablaggio
=========

.. list-table::
   :header-rows: 1
   :widths: 30 25 45

   * - Pin del modulo
     - Pin dell'ESP32
     - Nota
   * - TX (uscita NMEA)
     - GPIO16 (RX di UART2)
     - l'unica linea davvero usata
   * - RX
     - GPIO17 (TX di UART2)
     - riservato; non viene mai inviato nulla
   * - VCC
     - 3V3 o 5V
     - secondo quanto richiede il modulo
   * - GND
     - GND
     -

La porta, i pin e la velocità 9600 8N1 sono costanti di compilazione in
``main/include/gps.h``. GPIO16/17 non sono disponibili su un ESP32-WROVER, dove
appartengono alla PSRAM; lì bisogna cambiare i due pin (vedi
:ref:`it-hardware`).

La pagina GPS
=============

.. figure:: /_static/webadmin/gps_top.png
   :alt: La pagina Ricevitore GPS all'apertura.
   :align: center
   :width: 100%

   La pagina *Ricevitore GPS* all'apertura.

.. figure:: /_static/webadmin/gps_fs00.png
   :alt: Ricevitore GPS — l'unico interruttore che il resto del firmware controlla prima di usare il ricevitore. Spento, la UART non viene installata e il task di lettura non gira; la modifica ha effetto subito, senza riavvio.
   :align: center
   :width: 90%

   **Ricevitore GPS** — l'unico interruttore che il resto del firmware controlla prima di usare il ricevitore. Spento, la UART non viene installata e il task di lettura non gira; la modifica ha effetto subito, senza riavvio.

.. figure:: /_static/webadmin/gps_fs01.png
   :alt: Stato del Ricevitore — il badge Stato del Modulo e il collegamento, lo stato di navigazione, la qualità e la modalità della posizione.
   :align: center
   :width: 90%

   **Stato del Ricevitore** — il badge *Stato del Modulo* e il collegamento, lo stato di navigazione, la qualità e la modalità della posizione.

Il badge riassume la pagina in un'unica diagnosi:

* rosso **Disabilitato** - l'interruttore è spento, o la UART non è stata
  installata;
* rosso **Nessun dato (controllare il cablaggio)** - abilitato, ma nessuna
  frase valida è arrivata su GPIO16 entro 10 secondi;
* ambra **Ricerca in corso (nessuna posizione)** - le frasi arrivano ma il
  ricevitore non ha ancora una soluzione di navigazione;
* verde **Posizione OK** - viene riportata una posizione valida.

.. figure:: /_static/webadmin/gps_fs02.png
   :alt: Posizione — latitudine, longitudine, altitudine e separazione del geoide.
   :align: center
   :width: 90%

   **Posizione** — latitudine, longitudine, altitudine e separazione del geoide.

Una posizione viene mostrata, e offerta all'opzione *Usa GPS* delle altre
pagine, solo finché proviene da un fix attuale: appena il ricevitore segnala
di non avere un fix — una qualità ``GGA`` pari a 0 o un ``RMC`` non valido — la
stazione smette di presentare le ultime coordinate come la propria posizione, e
l'età del fix nel pannello *Serial Link* indica quanto è vecchio l'ultimo. Una
coordinata fuori intervallo (latitudine oltre 90°, longitudine oltre 180°,
minuti pari o superiori a 60) non viene mai accettata, anche da una frase con
checksum corretto.

.. figure:: /_static/webadmin/gps_fs03.png
   :alt: Movimento — velocità al suolo, rotta e declinazione magnetica.
   :align: center
   :width: 90%

   **Movimento** — velocità al suolo, rotta e declinazione magnetica.

.. figure:: /_static/webadmin/gps_fs04.png
   :alt: Data e Ora (UTC) — data e ora UTC dell'ultima posizione.
   :align: center
   :width: 90%

   **Data e Ora (UTC)** — data e ora UTC dell'ultima posizione.

.. figure:: /_static/webadmin/gps_fs05.png
   :alt: Satelliti e Precisione — satelliti usati e in vista e i valori HDOP / PDOP / VDOP.
   :align: center
   :width: 90%

   **Satelliti e Precisione** — satelliti usati e in vista e i valori HDOP / PDOP / VDOP.

.. figure:: /_static/webadmin/gps_fs06.png
   :alt: Collegamento Seriale — frasi accettate e scartate (errori di checksum o di formato) e l'età dell'ultima frase e dell'ultima posizione.
   :align: center
   :width: 90%

   **Collegamento Seriale** — frasi accettate e scartate (errori di checksum o di formato) e l'età dell'ultima frase e dell'ultima posizione.

.. figure:: /_static/webadmin/gps_fs07.png
   :alt: Cablaggio (tempo di compilazione) — il cablaggio di compilazione, come riferimento.
   :align: center
   :width: 90%

   **Cablaggio (tempo di compilazione)** — il cablaggio di compilazione, come riferimento.

I valori dal vivo vengono da ``/gps/values``, interrogato ogni secondo.

Chi usa la posizione
====================

* **Usa GPS** nelle pagine *Stazione*, *IGate*, *Digipeater*, *Tracker* e
  *Meteo* riempie i campi di posizione di quella pagina da ``/gps/live``
  finché è spuntato.
* **Usa posizione GPS in tempo reale** nella pagina *Tracker* legge il
  ricevitore a ogni trasmissione, e SmartBeaconing ne prende velocità e rotta.
* Il ``/status`` del bot Telegram riporta l'interruttore del ricevitore.

Tutto ciò che è in questa pagina è memorizzato in ``/storage/gps.json``.
