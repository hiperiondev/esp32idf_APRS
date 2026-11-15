.. _it-station:

=================================
Identità della stazione e simboli
=================================

La pagina *Stazione* contiene l'identità condivisa da tutti i servizi: il
nominativo, la posizione e le opzioni della stazione che valgono per tutto ciò
che va in aria. Ogni pagina di servizio (IGate, digipeater, tracker, meteo,
messaggistica) ha il proprio nominativo, SSID e posizione, e un interruttore
*Usa i dati della mia stazione* che li copia da qui, quindi normalmente una
stazione si configura una volta sola in questa pagina.

.. figure:: /_static/webadmin/station_top.png
   :alt: La pagina Stazione all'apertura.
   :align: center
   :width: 100%

   La pagina *Stazione* all'apertura.

Il gruppo Stazione
==================

.. figure:: /_static/webadmin/station_fs00.png
   :alt: Stazione — nominativo, latitudine, longitudine e altitudine, con Usa GPS, seguiti dalle opzioni applicate a ogni rapporto inviato dalla stazione.
   :align: center
   :width: 90%

   **Stazione** — nominativo, latitudine, longitudine e altitudine, con *Usa GPS*, seguiti dalle opzioni applicate a ogni rapporto inviato dalla stazione.

* **Usa GPS** disabilita i tre campi di posizione e li riempie una volta al
  secondo dal ricevitore GNSS (``/gps/live``) finché è spuntato, arrotondati
  alla precisione accettata dai campi. Richiede il ricevitore abilitato nella
  pagina *GPS*.
* **Ambiguita di posizione** cancella le ultime cifre di ogni posizione
  inviata dalla stazione, dalla precisione completa fino a circa 60 miglia
  nautiche.
* **Localizzatore Maidenhead nei rapporti di stato** antepone il quadrato
  della stazione ai testi di stato.
* **Timestamp zulu nei rapporti di stato** antepone un timestamp ``DDHHMMz``
  ai testi di stato.
* **Estensione di precisione DAO nei rapporti di posizione** aggiunge il campo
  ``!DAO!``, che porta una cifra in più di latitudine e longitudine.
* **Richiedi ad APRS-IS di non archiviare i miei pacchetti** aggiunge il
  marcatore ``!x!``.
* **Direzione antenna** ed **ERP** nei rapporti di stato chiudono i rapporti
  di stato con il suffisso di direzione e potenza per il meteor scatter.

Come viene codificato ciascuno è descritto in :ref:`it-beacons`.

Il gruppo PHG
=============

.. figure:: /_static/webadmin/station_fs01.png
   :alt: PHG — potenza del trasmettitore, guadagno, altezza e direttività dell'antenna della stazione, e il testo PHG risultante.
   :align: center
   :width: 90%

   **PHG** — potenza del trasmettitore, guadagno, altezza e direttività dell'antenna della stazione, e il testo ``PHG`` risultante.

Le pagine IGate e digipeater usano questi valori quando la loro *Estensione
dati* è PHG con *Usa i dati della mia stazione*.

Il selettore di simboli
=======================

Ogni pagina che invia una posizione ha una riga *Simbolo stazione*: l'icona,
la tabella e il codice del simbolo. Il collegamento ``…`` accanto apre la
pagina *Simboli APRS*, con le due tabelle complete.

.. figure:: /_static/webadmin/symbol_top.png
   :alt: La pagina Simboli APRS all'apertura.
   :align: center
   :width: 100%

   La pagina *Simboli APRS* all'apertura.

.. figure:: /_static/webadmin/symbol_fs00.png
   :alt: Selezione rapida — i simboli più usati, a un clic.
   :align: center
   :width: 90%

   **Selezione rapida** — i simboli più usati, a un clic.

.. figure:: /_static/webadmin/symbol_fs01.png
   :alt: Simboli attualmente configurati — il simbolo che ciascun servizio trasmette in questo momento.
   :align: center
   :width: 90%

   **Simboli attualmente configurati** — il simbolo che ciascun servizio trasmette in questo momento.

Più sotto la pagina elenca le tabelle primaria (``/``) e alternativa (``\``)
complete. Scegliere un simbolo scrive tabella e codice nella pagina da cui il
selettore è stato aperto; nulla viene salvato finché non si salva quella
pagina. Come nel pannello, le icone provengono da ``aprs.dprns.com`` e, se il
sito non è raggiungibile, viene mostrato il carattere del simbolo.
