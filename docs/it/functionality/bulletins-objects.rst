.. _it-bulletins-objects:

==========================
Bollettini, Oggetti e Item
==========================

Due sottosistemi permettono alla stazione di trasmettere annunci permanenti e
punti mappa con nome propri. Entrambi mantengono il proprio stato in file
LittleFS dedicati invece che in ``g_config``, per mantenere piccola la
configurazione residente, ed entrambi sono azionati dal pianificatore di beacon
condiviso.

Bollettini
==========

``main/bulletins.c`` trasmette fino a cinque bollettini APRS. Ogni bollettino
ha:

* il proprio testo,
* un **identificatore** e un nome di **gruppo** del destinatario,
* un'abilitazione **RF** e/o **APRS-IS**,
* un **intervallo** di trasmissione,
* una finestra opzionale di **"scadi dopo N ore"**.

Identificatore e gruppo insieme selezionano quale delle tre forme di
destinatario definite dal capitolo 14 di APRS101 va in onda:

.. list-table::
   :header-rows: 1
   :widths: 25 20 55

   * - Forma
     - Destinatario
     - Quando
   * - Bollettino generale
     - ``BLN1``
     - Identificatore ``0``–``9``, senza nome di gruppo. I bollettini che
       condividono l'identificatore si sostituiscono a vicenda sul ricevitore,
       quindi l'identificatore funge anche da numero di slot per un bollettino
       su più righe.
   * - Bollettino di gruppo
     - ``BLN1WX``
     - Identificatore ``0``–``9`` più un nome di gruppo fino a cinque caratteri.
       Solo le stazioni iscritte a quel gruppo lo visualizzano.
   * - Annuncio
     - ``BLNQ``
     - Identificatore ``A``–``Z`` e nessun nome di gruppo. La maggior parte del
       software client conserva e ripropone gli annunci molto più a lungo dei
       bollettini, ed è per questo che la specifica dà loro uno spazio di
       identificatori proprio.

``bulletins_build_addressee()`` normalizza mentre costruisce, così nulla che il
campo destinatario di 9 caratteri non possa portare arriva in onda: un
identificatore fuori da ``0``–``9``/``A``–``Z`` ricade sulla cifra dello slot
stesso, il nome del gruppo viene reso maiuscolo e privato di tutto ciò che non è
``A``–``Z``/``0``–``9``, e un identificatore di annuncio sopprime del tutto il
gruppo.

Un bollettino scaduto pulisce automaticamente il suo flag di abilitazione ed esce
dall'onda. I bollettini persistono nel proprio ``/storage/bulletins.json``. La
pagina è condizionata dall'interruttore di compilazione ``ENABLE_BULLETINS``.

.. note::

   I radiogrammi NTS, descritti anch'essi nel capitolo 14, sono un formato di
   messaggio per il traffico e non una forma di destinatario di bollettino, e
   non vengono prodotti qui.

Oggetti e Item
==============

``main/objects_items.c`` trasmette fino a cinque Oggetti/Item APRS, ciascuno con:

* un **nome**, **posizione** e **simbolo**,
* **rotta/velocità** e **commento** opzionali,
* un'abilitazione **RF** e/o **APRS-IS**,
* un **intervallo di ripetizione** con decadimento dell'intervallo opzionale,
* un controllo di **Tipo**: Oggetto (con timestamp, ``;``) oppure Item (senza
  timestamp, ``)``).

**Uccidere** un oggetto lo trasmette qualche volta in più (così che gli
ascoltatori lo rimuovano dalle loro mappe), poi lo disabilita automaticamente.
Gli oggetti/item persistono nel proprio ``/storage/objitems.json``. La pagina è
condizionata dall'interruttore di compilazione ``ENABLE_OBJECTS_ITEMS``.

Oggetti permanenti
-------------------

Un Oggetto puo anche essere marcato come **permanente**. Un Oggetto permanente
viene trasmesso con il timestamp fittizio fisso ``111111z`` definito da
``freqspec.txt``, invece dell'ora corrente ``DDHHMMz``. Questa e la convenzione
raccomandata per gli oggetti di frequenza dei ripetitori voce e per annunci
ricorrenti simili di proprieta della stazione: una stazione ricevente
interpreta il timestamp ``111111z`` come indicazione che l'Oggetto non deve
essere sostituito da un Oggetto omonimo di un'altra stazione, e che solo la
stazione di origine puo aggiornarlo o spostarlo.

La casella Permanente si applica solo a un Oggetto; non ha alcun effetto su un
Item, che non porta mai un timestamp di alcun tipo.

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
