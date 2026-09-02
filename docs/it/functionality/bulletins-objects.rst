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
* un **intervallo** iniziale di trasmissione, con una **rampa di decadimento**
  opzionale: una cadenza lenta e il rapporto per cui viene moltiplicato
  l'intervallo dopo ogni trasmissione,
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

Il capitolo 14 descrive un bollettino come ripetuto spesso nella sua prima ora e
poi via via meno nelle ore successive, e un annuncio come ripetuto molto più
lentamente ancora. Ogni slot porta quel diradamento come una rampa di
decadimento sopra il proprio intervallo iniziale: dopo ogni trasmissione
l'intervallo vivo viene moltiplicato per il **rapporto di decadimento** dello
slot finché non raggiunge la sua **cadenza lenta**, dove si mantiene. Un
bollettino inviato ogni dieci minuti con un rapporto di 2.0 e una cadenza lenta
di due ore esce così tre volte nella prima ora e si assesta sulle due ore, il
che raggiunge lo stesso pubblico con una frazione del tempo d'antenna su un
canale condiviso.

Lasciare la cadenza lenta a 0, o il rapporto sotto 1.0, mantiene l'intervallo
piatto, che è anche come si comporta un bollettino memorizzato che non porta
nessuno dei due campi. La rampa è stato di esecuzione e non configurazione: non
viene persistita, e un riavvio o qualsiasi modifica allo slot la fa ripartire
dall'intervallo iniziale, così un bollettino riscritto o ritemporizzato torna a
farsi sentire subito invece che alla spaziatura a cui era decaduto il testo
precedente.

La scadenza e la rampa sono complementari. La rampa dirada le ripetizioni
mentre il bollettino è attuale; la scadenza decide quando smette di esserlo. Un
bollettino scaduto pulisce automaticamente il suo flag di abilitazione ed esce
dall'onda. I bollettini persistono nel proprio ``/storage/bulletins.json``. La
pagina è condizionata dall'interruttore di compilazione ``ENABLE_BULLETINS``.

Anche un bollettino che questa stazione trasmette viene consegnato al bot
Telegram, alle stesse condizioni di uno sentito da un'altra stazione, così gli
operatori che leggono il bot vedono gli annunci della stazione accanto a
quelli di tutti gli altri. Lo governa l'interruttore "Inoltra bollettini"
della pagina *Telegram*; vedi :ref:`it-telegram`.

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

Oggetti di area
---------------

Un elemento il cui simbolo è il simbolo di area (``\\l``, la lettera L
minuscola della tabella alternativa) disegna una figura sulla mappa ricevente
invece di un punto. Lo slot di estensione dati da 7 byte porta allora il
descrittore ``Tyy/Cxx`` del capitolo 11 di APRS101 anziché rotta/velocità:

* **forma** — una delle dieci cifre: cerchio, linea giù/destra, ellisse,
  triangolo, riquadro, e poi il cerchio pieno, la linea giù/sinistra,
  l'ellisse piena, il triangolo pieno e il riquadro pieno,
* **colore** — da 0 a 15. I valori da dieci in su sostituiscono la barra con un
  ``1`` e scrivono la cifra delle unità, così il campo misura sempre sette
  byte fissi,
* **offset di latitudine e longitudine** — la distanza in gradi dalla posizione
  riportata, che è l'angolo superiore sinistro della figura, fino al suo angolo
  inferiore destro (o al centro, nel caso di un cerchio).

Ogni offset viene trasmesso come codice di due cifre, la radice quadrata
dell'offset espresso in 1500-esimi di grado; il ricevitore lo recupera come
``codice × codice ÷ 1500``. La specifica usava in origine un fattore 100 ed è
stata corretta a 1500 da ``aprs.org/aprs11/areaobjects.txt``, che è la scala
con cui decodificano le applicazioni attuali. Due cifre raggiungono quindi
6,534 gradi per asse, ed entrambi i campi di offset vengono limitati a quel
valore al salvataggio, così che il valore memorizzato e la figura trasmessa
descrivano sempre la stessa area.

Le due forme a linea possono inoltre dichiarare un **corridoio**: una fascia
della larghezza indicata in miglia su ciascun lato della linea, trasmessa come
token ``{www}`` in testa al testo del commento, esattamente dove la colloca
l'esempio della specifica stessa. Una larghezza pari a zero omette il token, e
il campo viene ignorato per le otto forme chiuse.

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
