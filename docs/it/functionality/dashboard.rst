.. _it-dashboard:

===================
Pannello e traffico
===================

Il *Pannello* è la pagina con cui si apre l'amministrazione web. Non configura
nulla: è lo stato della stazione dal vivo, aggiornato dal browser finché la
pagina è aperta, e l'unico punto in cui si vede in tempo reale il traffico
ricevuto, ripetuto, inoltrato dal gateway e trasmesso.

.. figure:: /_static/webadmin/dashboard_top.png
   :alt: La pagina Pannello all'apertura.
   :align: center
   :width: 100%

   La pagina *Pannello* all'apertura.

Informazioni di sistema
=======================

.. figure:: /_static/webadmin/dashboard_fs00.png
   :alt: Informazioni sistema — data e ora nel fuso orario scelto nella pagina Sistema, tempo di attività, heap libero e il suo minimo storico, uso di LittleFS, frequenza della CPU e motivo dell'ultimo riavvio. La striscia è aggiornata una volta al secondo da /dashinfo.
   :align: center
   :width: 90%

   **Informazioni sistema** — data e ora nel fuso orario scelto nella pagina *Sistema*, tempo di attività, heap libero e il suo minimo storico, uso di LittleFS, frequenza della CPU e motivo dell'ultimo riavvio. La striscia è aggiornata una volta al secondo da ``/dashinfo``.

L'orologio funziona sempre in UTC; il fuso orario cambia solo come la data
viene mostrata qui (vedi :ref:`it-administration`). Un minimo di heap che
scende giorno dopo giorno è il primo segnale di una perdita di memoria; il
*Motivo riavvio* distingue un'interruzione di alimentazione (*Power-on*) da un
watchdog o da un panic.

Modalità e rete
===============

.. figure:: /_static/webadmin/dashboard_fs01.png
   :alt: Modalità abilitate — una pillola per servizio - IGate, digipeater, tracker e meteo - verde finché il servizio è attivo.
   :align: center
   :width: 90%

   **Modalità abilitate** — una pillola per servizio - IGate, digipeater, tracker e meteo - verde finché il servizio è attivo.

.. figure:: /_static/webadmin/dashboard_fs02.png
   :alt: Stato rete — associazione Wi-Fi, connessione ad APRS-IS (igate_is_connected()) e se FX.25 è abilitato in ricezione.
   :align: center
   :width: 90%

   **Stato rete** — associazione Wi-Fi, connessione ad APRS-IS (``igate_is_connected()``) e se FX.25 è abilitato in ricezione.

Statistiche
===========

.. figure:: /_static/webadmin/dashboard_fs03.png
   :alt: STATISTICHE — i contatori della stazione: trame ricevute e trasmesse via RF, inoltrate in ciascuna direzione, ripetute, scartate o non decodificabili, la coda di trasmissione RF, le trasmissioni CSMA forzate e il ciclo di lavoro di trasmissione.
   :align: center
   :width: 90%

   **STATISTICHE** — i contatori della stazione: trame ricevute e trasmesse via RF, inoltrate in ciascuna direzione, ripetute, scartate o non decodificabili, la coda di trasmissione RF, le trasmissioni CSMA forzate e il ciclo di lavoro di trasmissione.

Cosa misura ciascun contatore, e perché IGate e modem hanno contatori
separati, è descritto in :ref:`it-web-admin` (*Le statistiche della dashboard*).

.. figure:: /_static/webadmin/dashboard_fs04.png
   :alt: Dettaglio Scarti — una riga per motivo di scarto. Un contatore che cresce dice quale filtro o limite sta rifiutando il traffico, senza aprire la console.
   :align: center
   :width: 90%

   **Dettaglio Scarti** — una riga per motivo di scarto. Un contatore che cresce dice quale filtro o limite sta rifiutando il traffico, senza aprire la console.

Radio, server e Wi-Fi
=====================

.. figure:: /_static/webadmin/dashboard_fs05.png
   :alt: Info Radio — il profilo di modem attivo e lo stato di FX.25.
   :align: center
   :width: 90%

   **Info Radio** — il profilo di modem attivo e lo stato di FX.25.

.. figure:: /_static/webadmin/dashboard_fs06.png
   :alt: SERVER APRS-IS — il server APRS-IS in uso, che con il failover può essere uno qualsiasi dei quattro configurati nella pagina IGate.
   :align: center
   :width: 90%

   **SERVER APRS-IS** — il server APRS-IS in uso, che con il failover può essere uno qualsiasi dei quattro configurati nella pagina *IGate*.

.. figure:: /_static/webadmin/dashboard_fs07.png
   :alt: WiFi — modalità Wi-Fi, la rete a cui la stazione è associata e la sua intensità di segnale.
   :align: center
   :width: 90%

   **WiFi** — modalità Wi-Fi, la rete a cui la stazione è associata e la sua intensità di segnale.

Traffico dal vivo
=================

.. figure:: /_static/webadmin/dashboard_fs08.png
   :alt: Traffico IGate — la tabella del traffico: tempo dall'avvio, tipo (RX, TX, DIGI, RX-IS…), simbolo, stazione, il pacchetto grezzo, ciò che è stato decodificato dal payload e il livello audio ricevuto in mV RMS.
   :align: center
   :width: 90%

   **Traffico IGate** — la tabella del traffico: tempo dall'avvio, tipo (``RX``, ``TX``, ``DIGI``, ``RX-IS``…), simbolo, stazione, il pacchetto grezzo, ciò che è stato decodificato dal payload e il livello audio ricevuto in mV RMS.

La tabella si riempie interrogando ``/igate_traffic?since=<seq>`` per numero di
sequenza, quindi nulla si perde tra due interrogazioni, e una pagina appena
caricata parte dalle voci ancora presenti nell'anello della stazione. *Pausa*
congela la vista senza fermare la stazione; *Cancella* svuota solo la copia
del browser. La colonna *AUDIO* è il livello che il modem ha misurato sul pin
dell'ADC per quella trama, ed è il modo più rapido per regolare il livello di
ricezione (vedi :ref:`it-hardware`). Con *Registra dopo i filtri* attivo
nella pagina *IGate*, la tabella mostra solo ciò che i filtri locali
accettano.

Le icone dei simboli sono caricate dal browser da ``aprs.dprns.com``; se quel
sito non è raggiungibile la colonna resta vuota e tutto il resto funziona.

Ultimi ascoltati
================

Ogni stazione ascoltata è conservata anche in una tabella *Last heard* per
stazione di al massimo 30 voci, la più recente per prima, con il percorso da
cui è arrivata, se via RF o via APRS-IS, i salti di digipeater usati e uno
storico di attività di 18 ore. Il pannello non la disegna: è servita come JSON
su ``/lastheard`` e la usano il gateway dei messaggi (*sentito localmente*),
le risposte a ``?APRSD`` / ``?APRSH``, il filtro INET→RF *sentito via RF* e il
conteggio di stato di BrandMeister.
