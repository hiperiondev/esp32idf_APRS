.. _it-administration:

==============================
Amministrazione della stazione
==============================

Cinque pagine si occupano del dispositivo più che dei servizi APRS: *Senza
fili*, *Sistema*, *Archiviazione file*, *Firmware* e *Registri*.

Senza fili
==========

.. figure:: /_static/webadmin/wireless_top.png
   :alt: La pagina Senza fili all'apertura.
   :align: center
   :width: 100%

   La pagina *Senza fili* all'apertura.

.. figure:: /_static/webadmin/wireless_fs00.png
   :alt: Modalità WiFi — modalità Wi-Fi - spento, client (STA), punto di accesso (AP) o entrambi - e la potenza di trasmissione, fino a 20 dBm.
   :align: center
   :width: 90%

   **Modalità WiFi** — modalità Wi-Fi - spento, client (STA), punto di accesso (AP) o entrambi - e la potenza di trasmissione, fino a 20 dBm.

.. figure:: /_static/webadmin/wireless_fs01.png
   :alt: Punto di accesso (AP) — il punto di accesso della stazione: nome della rete, password e canale (da 1 a 13).
   :align: center
   :width: 90%

   **Punto di accesso (AP)** — il punto di accesso della stazione: nome della rete, password e canale (da 1 a 13).

.. figure:: /_static/webadmin/wireless_fs02.png
   :alt: Client WiFi — uno dei cinque slot client, ciascuno con il proprio interruttore Abilita, nome della rete e password.
   :align: center
   :width: 90%

   **Client WiFi** — uno dei cinque slot client, ciascuno con il proprio interruttore *Abilita*, nome della rete e password.

Gli slot sono insiemi di credenziali alternativi, non un elenco di failover:
all'avvio si usa il primo slot abilitato che abbia un nome di rete, quindi
spostare un dispositivo dalla rete di casa all'hotspot di un telefono è
questione di spuntare un altro slot. *AP + STA* mantiene attivo il punto di
accesso mentre il client è connesso, ed è la scelta sicura mentre si configura
una stazione: l'amministrazione web resta raggiungibile su ``192.168.4.1``
anche se le credenziali del client sono sbagliate. Il pulsante *SCANSIONA
WIFI* sopra gli slot cerca le reti nel raggio e ne propone i nomi come
suggerimenti in ogni campo SSID. La rete è descritta in :ref:`it-networking`.

Sistema
=======

.. figure:: /_static/webadmin/system_top.png
   :alt: La pagina Sistema all'apertura.
   :align: center
   :width: 100%

   La pagina *Sistema* all'apertura.

.. figure:: /_static/webadmin/system_fs00.png
   :alt: Chip — modello del chip, core, revisione e dimensione della flash, e la frequenza della CPU (80, 160 o 240 MHz), applicata subito e a ogni avvio.
   :align: center
   :width: 90%

   **Chip** — modello del chip, core, revisione e dimensione della flash, e la frequenza della CPU (80, 160 o 240 MHz), applicata subito e a ogni avvio.

Il modem audio richiede 240 MHz: a una frequenza più bassa la catena di
ricezione non riesce a stare dietro ai campioni.

.. figure:: /_static/webadmin/system_fs01.png
   :alt: Accesso amministrazione web — l'account amministratore (admin / admin di fabbrica). Cambialo prima che la stazione sia raggiungibile da una rete che non controlli.
   :align: center
   :width: 90%

   **Accesso amministrazione web** — l'account amministratore (``admin`` / ``admin`` di fabbrica). Cambialo prima che la stazione sia raggiungibile da una rete che non controlli.

.. figure:: /_static/webadmin/system_fs02.png
   :alt: Accesso web in sola lettura — un secondo account facoltativo che può vedere tutte le pagine ma non cambiare nulla.
   :align: center
   :width: 90%

   **Accesso web in sola lettura** — un secondo account facoltativo che può vedere tutte le pagine ma non cambiare nulla.

I due ruoli sono descritti in :ref:`it-web-admin`.

.. figure:: /_static/webadmin/system_fs03.png
   :alt: Ora — sincronizzazione dell'ora via NTP: abilitazione, tre server, intervallo di risincronizzazione e il fuso orario con cui il pannello mostra la data.
   :align: center
   :width: 90%

   **Ora** — sincronizzazione dell'ora via NTP: abilitazione, tre server, intervallo di risincronizzazione e il fuso orario con cui il pannello mostra la data.

Tutti i timestamp che la stazione invia in aria sono UTC qualunque sia il fuso
orario. In fondo alla pagina c'è il pulsante di **ripristino di fabbrica**,
che riporta tutte le impostazioni ai valori predefiniti e riavvia.

Archiviazione file
==================

.. figure:: /_static/webadmin/storage_main.png
   :alt: Il browser di LittleFS: utilizzo, caricamento, formattazione e una riga per file con Scarica ed Elimina.
   :align: center
   :width: 90%

   Il browser di LittleFS: utilizzo, caricamento, formattazione e una riga per file con *Scarica* ed *Elimina*.

Ogni servizio conserva le proprie impostazioni nel proprio file JSON, quindi
una pagina si può salvare, ripristinare o copiare su un'altra stazione
scaricando e caricando il suo file. Anche il certificato radice di Telegram si
carica qui. Vedi :ref:`it-storage-ota`.

Firmware e OTA
==============

.. figure:: /_static/webadmin/about_top.png
   :alt: La pagina Firmware all'apertura.
   :align: center
   :width: 100%

   La pagina *Firmware* all'apertura.

.. figure:: /_static/webadmin/about_fs00.png
   :alt: Firmware — progetto, versione, data di compilazione, versione di ESP-IDF e la partizione in esecuzione.
   :align: center
   :width: 90%

   **Firmware** — progetto, versione, data di compilazione, versione di ESP-IDF e la partizione in esecuzione.

.. figure:: /_static/webadmin/about_fs01.png
   :alt: Aggiornamento OTA — l'aggiornamento OTA: scegliere un .bin e Carica e flasha.
   :align: center
   :width: 90%

   **Aggiornamento OTA** — l'aggiornamento OTA: scegliere un ``.bin`` e *Carica e flasha*.

L'immagine viene scritta nello slot inattivo mentre la stazione continua a
funzionare, e la stazione passa al nuovo slot solo quando l'immagine è
completa e verificata. Un'immagine che non riesce ad avviarsi viene annullata
automaticamente al riavvio successivo. Solo la prima programmazione va fatta
via USB.

Registri di console
===================

.. figure:: /_static/webadmin/logs_fs00.png
   :alt: Registro di console — la console seriale riprodotta nel browser: Avvia / Ferma e le ultime 50 righe.
   :align: center
   :width: 90%

   **Registro di console** — la console seriale riprodotta nel browser: *Avvia* / *Ferma* e le ultime 50 righe.

La pagina mostra ciò che la stazione stampa sulla sua porta seriale, quindi
una stazione su un traliccio si può diagnosticare senza cavo. Nulla viene
registrato o scritto in flash; la cattura si ferma lasciando la pagina. Oltre
alle righe di ciascun servizio (trame ricevute, beacon, APRS-IS), una build
normale stampa una riga di heap ogni 10 secondi e il margine di stack di ogni
task una volta all'ora; entrambe, e i dettagli facoltativi per heap e per
allocazione, si scelgono in ``menuconfig`` → *APRS heap instrumentation*. Sono
la prima cosa da guardare quando una stazione si comporta male dopo ore di
funzionamento (vedi :ref:`it-troubleshooting`).
