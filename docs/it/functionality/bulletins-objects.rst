.. _it-bulletins-objects:

==========================
Bollettini, Oggetti e Item
==========================

Due sottosistemi permettono alla stazione di trasmettere annunci permanenti e
punti mappa con nome propri. Entrambi mantengono il proprio stato in file
LittleFS dedicati invece che in ``g_config``, per mantenere piccola la
configurazione residente, ed entrambi sono azionati dal pianificatore di beacon
condiviso.

Bollettini (BLN1..BLN5)
=======================

``main/bulletins.c`` trasmette fino a cinque bollettini APRS, indirizzati da
``BLN1`` a ``BLN5``. Ogni bollettino ha:

* il proprio testo,
* un'abilitazione **RF** e/o **APRS-IS**,
* un **intervallo** di trasmissione,
* una finestra opzionale di **"scadi dopo N ore"**.

Un bollettino scaduto pulisce automaticamente il suo flag di abilitazione ed esce
dall'onda. I bollettini persistono nel proprio ``/storage/bulletins.json``. La
pagina è condizionata dall'interruttore di compilazione ``ENABLE_BULLETINS``.

Oggetti e Item
==============

``main/objects_items.c`` trasmette fino a cinque Oggetti/Item APRS, ciascuno con:

* un **nome**, **posizione** e **simbolo**,
* **rotta/velocità** e **commento** opzionali,
* un'abilitazione **RF** e/o **APRS-IS**,
* un **intervallo di ripetizione** con decadimento dell'intervallo opzionale,
* un flag **"permanente"** in stile YAAC: permanente → un Item senza timestamp,
  altrimenti un Oggetto con timestamp.

**Uccidere** un oggetto lo trasmette qualche volta in più (così che gli
ascoltatori lo rimuovano dalle loro mappe), poi lo disabilita automaticamente.
Gli oggetti/item persistono nel proprio ``/storage/objitems.json``. La pagina è
condizionata dall'interruttore di compilazione ``ENABLE_OBJECTS_ITEMS``.

Perché file JSON separati
=========================

Entrambi i sottosistemi, come la telemetria, mantengono stato specifico della
pagina che ingrandirebbe significativamente l'``app_config_t`` residente (e
quindi ogni salvataggio di ``config.json``, che gira contro un heap piccolo e
frammentato). Mantenerli nei propri file significa che la configurazione
residente resta leggera e che il salvataggio di ogni sottosistema tocca solo i
propri dati. Ogni file è scritto con lo stesso scrittore JSON a flusso, byte per
byte, usato dalla configurazione principale, sotto il proprio mutex, con un
``setvbuf()`` esplicito così che newlib non allochi pigramente un grande buffer
stdio a metà scrittura su un heap frammentato.
