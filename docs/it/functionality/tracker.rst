.. _it-tracker:

=======
Tracker
=======

Il tracker invia la posizione propria della stazione mentre si muove (o da un
punto fisso quando non c'è un ricevitore). Gira sullo scheduler di beacon
condiviso come tutti gli altri beacon; le opzioni di codifica che offre -
posizione compressa e Mic-E, SmartBeaconing, PHG, blocchi di stato e di
frequenza - sono descritte in dettaglio in :ref:`it-beacons`. Questa pagina è
la vista dell'operatore.

.. figure:: /_static/webadmin/tracker_top.png
   :alt: La pagina Tracker all'apertura.
   :align: center
   :width: 100%

   La pagina *Tracker* all'apertura.

.. figure:: /_static/webadmin/tracker_fs00.png
   :alt: Tracker — abilitazione, Usa i dati della mia stazione, trasmissione via RF e/o APRS-IS, il timestamp DDHHMMz e l'intervallo fisso del beacon.
   :align: center
   :width: 90%

   **Tracker** — abilitazione, *Usa i dati della mia stazione*, trasmissione via RF e/o APRS-IS, il timestamp ``DDHHMMz`` e l'intervallo fisso del beacon.

.. figure:: /_static/webadmin/tracker_fs01.png
   :alt: Stazione — nominativo, SSID (9 di default, il consueto per un mobile), percorso e la posizione fissa con Usa GPS e Usa posizione GPS in tempo reale.
   :align: center
   :width: 90%

   **Stazione** — nominativo, SSID (9 di default, il consueto per un mobile), percorso e la posizione fissa con *Usa GPS* e *Usa posizione GPS in tempo reale*.

I due interruttori GPS fanno cose diverse:

* **Usa GPS** copia la posizione del ricevitore nei campi fissi di latitudine,
  longitudine e altitudine finché la pagina è aperta; ciò che si salva è una
  posizione fissa.
* **Usa posizione GPS in tempo reale** lascia la posizione fissa come riserva e
  fa leggere il ricevitore a ogni trasmissione. È ciò che serve a un tracker
  mobile, e ciò di cui SmartBeaconing ha bisogno per avere velocità e rotta su
  cui lavorare. Quando il ricevitore non ha una posizione, il beacon torna alla
  posizione e all'intervallo fissi.

.. figure:: /_static/webadmin/tracker_fs02.png
   :alt: SmartBeaconing — i parametri di SmartBeaconing: intervalli lento e veloce, velocità bassa e alta, angolo di virata, pendenza di virata e tempo minimo tra le virate.
   :align: center
   :width: 90%

   **SmartBeaconing** — i parametri di SmartBeaconing: intervalli lento e veloce, velocità bassa e alta, angolo di virata, pendenza di virata e tempo minimo tra le virate.

.. figure:: /_static/webadmin/tracker_fs03.png
   :alt: Opzioni — posizione compressa, codifica Mic-E con il suo commento di posizione, altitudine, PHG, il simbolo della stazione e il commento.
   :align: center
   :width: 90%

   **Opzioni** — posizione compressa, codifica Mic-E con il suo commento di posizione, altitudine, PHG, il simbolo della stazione e il commento.

.. figure:: /_static/webadmin/tracker_fs04.png
   :alt: Beacon di stato — testo e intervallo del beacon di stato.
   :align: center
   :width: 90%

   **Beacon di stato** — testo e intervallo del beacon di stato.

.. figure:: /_static/webadmin/tracker_fs05.png
   :alt: Parametri Radio del Ripetitore — frequenza, duplex, offset e tono scritti all'inizio del commento, per una stazione che annuncia la frequenza su cui ascolta.
   :align: center
   :width: 90%

   **Parametri Radio del Ripetitore** — frequenza, duplex, offset e tono scritti all'inizio del commento, per una stazione che annuncia la frequenza su cui ascolta.
