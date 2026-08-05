.. _it-beacons:

=========================
Beacon e il pianificatore
=========================

I beacon della propria stazione sono ciò che fa apparire la stazione su
aprs.fi. L'IGate e il digipeater da soli si limitano a ritrasmettere il traffico
che sentono; non annunciano mai la propria posizione. Esistono tre beacon logici
— **tracker**, **igate** e **digi** — ciascuno con i propri flag di abilitazione,
intervallo, coordinate, simbolo, commento e instradamento RF/INET, salvati dalla
rispettiva pagina dell'amministrazione web (``g_config.trk_*``,
``g_config.igate_*``, ``g_config.digi_*``).

Il pianificatore di beacon condiviso
====================================

Revisioni precedenti eseguivano i beacon di tracker, igate e digi, il report
meteo e i bollettini ciascuno nel **proprio task FreeRTOS**. Ognuno di quei task
faceva la stessa cosa — dormire, svegliarsi, costruire un pacchetto, percorrere la
catena TX TNC2/AX.25 condivisa (carica di operazioni in virgola mobile), dormire
di nuovo — e quindi ognuno doveva trascinare uno stack grande (10–14 KB)
dimensionato per quell'albero di chiamate, anche se quasi mai vengono eseguiti
contemporaneamente e il modem semi-duplex serializza comunque le loro
trasmissioni.

Il componente ``beacon_scheduler`` **fonde quei cinque task in uno**. A ogni
passata chiama la funzione "service" di ogni sottosistema (``beacon_service()``,
``weather_beacon_service()``, ``bulletins_service()``, e i servizi di
oggetti/item e telemetria), ciascuna delle quali trasmette ciò che è dovuto e
riporta quanti secondi mancano prima che serva di nuovo; il pianificatore poi
dorme fino al più vicino di questi. I sottosistemi conservano i propri flag di
abilitazione e intervalli indipendenti — solo il task (e il suo stack) è
condiviso.

Effetto netto: cinque stack (~61 KB in totale) diventano uno (~14 KB),
liberando ~46 KB di heap interno in questa build senza PSRAM.

Le risposte alle query viaggiano sullo stesso task
==================================================

Una risposta a una query APRS è un beacon in tutto tranne che nel suo innesco:
``?APRS?`` e ``?APRSP`` eseguono il costruttore di posizione, ``?APRSS`` quello
di stato, ``?WX?`` quello meteo, e tutte finiscono nella stessa catena di TX
TNC2/AX.25 carica di virgola mobile. Per questo vengono servite anche qui.
``query_process()`` e ``query_process_directed()``, che girano sui task che
ricevono traffico, si limitano ad accodare la richiesta; lo scheduler chiama
``query_service()`` all'inizio di ogni passata e svolge la costruzione e la
trasmissione sullo stack dimensionato per quell'albero di chiamate (vedi
:ref:`it-query`).

Poiché una query non è periodica, attendere la scadenza del beacon successivo si
noterebbe come una risposta tardiva. Accodare una richiesta chiama quindi
``beacon_scheduler_wake()``, che accorcia il sonno dello scheduler — quel sonno è
un ``ulTaskNotifyTake()`` con la prossima scadenza come timeout e non un semplice
ritardo. Un risveglio sollevato mentre il task è nel mezzo di una passata viene
trattenuto da FreeRTOS e raccolto dal sonno successivo, così nulla di accodato
viene dormito.

Funzioni di servizio
====================

Ogni sottosistema espone una ``*_service()`` che:

#. Controlla i propri flag di abilitazione. Un beacon disabilitato è un no-op a
   basso costo che restituisce un intervallo di ri-controllo breve, così che
   attivarlo dal web abbia comunque effetto senza riavvio.
#. Trasmette qualsiasi beacon attualmente dovuto, in RF
   (``aprs_service_send_tnc2()``) e/o ad APRS-IS (``igate_send_raw()``) secondo i
   flag ``loc2rf`` / ``loc2inet`` della pagina.
#. Restituisce il numero di secondi (sempre ≥ 1) fino al prossimo evento dovuto
   più vicino.

``beacon_service()`` gestisce i tre beacon di posizione in una singola passata.

Jitter anti-collisione
======================

La pianificazione dei beacon è altrimenti deterministica, quindi più stazioni che
scelgono tutte lo stesso intervallo tondo (es. WX ogni 600 s) tendono a
sincronizzarsi in fase e a collidere su un canale RF condiviso — una classica
patologia APRS. ``beacon_scheduler_jitter()`` disperde il momento dovuto di un
beacon di ± una piccola percentuale (seminato con ``esp_random()``, uniforme),
così che i beacon della propria stazione si decorrelano sia tra loro sia dalle
stazioni vicine, e i beacon dovuti simultaneamente derivano separandosi nel
tempo. Il jitter è applicato all'intervallo usato per calcolare il timestamp del
**prossimo dovuto** di un beacon — non semplicemente al sonno del pianificatore,
che lascerebbe la griglia temporale dei dovuti sottostante deterministica e le
permetterebbe di ri-sincronizzarsi al ciclo successivo.

Scaglionamento del TX dentro una passata
========================================

Quando più beacon della propria stazione diventano dovuti insieme, vengono
serviti consecutivamente nel task del pianificatore, molto più velocemente di
quanto un frame a 1200 Bd liberi l'etere. Con il valore di fabbrica *TX buffers =
1*, il 2° e 3° frame urterebbero un anello TX RF pieno e verrebbero scartati. Per
evitarlo, il task del pianificatore si registra tramite
``aprs_service_set_beacon_context()``, e **solo in quel task**
``aprs_service_send_tnc2()`` può attendere brevemente (fino a 4 s) che l'anello
scenda sotto il limite prima di arrendersi — così ogni beacon dovuto finisce per
attivare la radio, mentre tutti gli altri chiamanti (RX/digipeat, INET→RF, TX di
messaggi) mantengono il comportamento non bloccante di scarta-se-pieno e un ramo
RF occupato non ferma mai la decodifica RX né il socket APRS-IS.

Estensioni dati (PHG / RNG / DFS)
=================================

Il beacon di posizione dell'IGate può portare una delle tre estensioni dati APRS
da stazione fissa nello slot di 7 byte che segue il codice del simbolo — lo
stesso slot che una stazione in movimento usa per rotta e velocità, ed è per
questo che ne viene emessa sempre una sola. *Abilita estensione dati* nella
pagina IGate apre lo slot, e *Tipo di estensione* sceglie quale delle tre lo
riempie:

.. list-table::
   :header-rows: 1
   :widths: 12 18 70

   * - Tipo
     - In onda
     - Significato
   * - PHG
     - ``PHG5132``
     - Potenza di trasmissione, altezza dell'antenna sul terreno medio, guadagno
       e direttività. Il software ricevente disegna la copertura stimata
       risultante come un cerchio (o un lobo, con antenna direttiva).
   * - RNG
     - ``RNG0025``
     - Una singola portata radio omnidirezionale precalcolata in miglia
       terrestri, per un operatore che conosce già il proprio raggio di
       copertura reale e preferisce dichiararlo anziché farlo dedurre dal PHG.
   * - DFS
     - ``DFS3364``
     - Intensità del segnale omni-DF: gli stessi codici di altezza/guadagno/
       direttività del PHG, ma riportando l'intensità del segnale *ricevuto* in
       punti S invece della potenza trasmessa. Un'intensità di 0 significa che
       questa stazione **non** riceve il segnale, e il software di tracciamento
       lo rappresenta come un cerchio di esclusione anziché di copertura.

PHG usa tutti e quattro i sottocampi, DFS tutti tranne la potenza di
trasmissione, e RNG nessuno; la pagina disabilita gli input che il tipo
selezionato non usa. Poiché un controllo disabilitato non viene inviato nel
POST, i valori memorizzati dell'*altro* tipo sopravvivono al passaggio avanti e
indietro.

Abilitare una qualsiasi estensione forza il formato di posizione non compresso.
Il formato compresso non ha spazio per lo slot di 7 byte (APRS101 cap.9 afferma
che non supporta il PHG), quindi emettere quei byte dentro un rapporto compresso
sarebbe semplicemente un dato sbagliato, e scartare l'estensione per mantenere la
compressione perderebbe in silenzio un campo che l'operatore ha abilitato
esplicitamente.

La capacità di messaggistica sta nell'identificatore di tipo dati
=================================================================

Il primo byte del campo informativo di un rapporto di posizione dichiara due
cose insieme (APRS101 cap.6): se segue una marca temporale, e se la stazione può
accettare messaggi APRS.

.. list-table::
   :header-rows: 1
   :widths: 25 25 25 25

   * - Marca temporale
     - ``msg_enable`` spento
     - ``msg_enable`` acceso
     - Significato
   * - No
     - ``!``
     - ``=``
     - Posizione, senza marca temporale
   * - Sì
     - ``/``
     - ``@``
     - Posizione con marca temporale

La distinzione non è decorativa: è così che un client ricevente decide se
offrire al proprio operatore l'azione *invia messaggio* per quella stazione. Le
radio Kenwood TH-D7/D700/D710, APRSISCE/32, Xastir, YAAC e aprs.fi leggono tutte
questo bit, e una stazione che dichiara di non accettare messaggi viene mostrata
senza alcuna via di risposta.

Questa stazione esegue un motore di messaggistica completo, risponde alle query
dirette e conferma i messaggi che riceve, quindi con *Abilita messaggistica*
attivo tutte e tre le balise di posizione lo dichiarano. L'identificatore viene
scelto in ``buildPositionPacket()`` dalla stessa copia sotto lock da cui
provengono tutti gli altri campi della balise. Oggetti e item non sono toccati —
portano i propri identificatori ``;`` e ``)`` — e Mic-E ha il proprio formato
fisso.

Ambiguità di posizione
======================

*Ambiguità di posizione*, nella pagina Stazione, vale per tutta la stazione: si
applica a tutti e tre i beacon di posizione, perché con quanta precisione una
stazione è disposta a dire dove si trova è una proprietà della stazione e non di
un singolo beacon. I livelli 0–4 svuotano le cifre di minuto meno significative
in onda (APRS101 cap.6) — il punto decimale, il carattere di emisfero e le
larghezze dei campi non cambiano mai, ed è questo che mantiene il rapporto
analizzabile:

.. list-table::
   :header-rows: 1
   :widths: 10 25 25 40

   * - Livello
     - Latitudine
     - Longitudine
     - Precisione
   * - 0
     - ``4903.50N``
     - ``07201.75W``
     - Centesimi di minuto (piena).
   * - 1
     - ``4903.5 N``
     - ``07201.7 W``
     - Al 1/10 di minuto.
   * - 2
     - ``4903.  N``
     - ``07201.  W``
     - Al minuto.
   * - 3
     - ``490 .  N``
     - ``0720 .  W``
     - Ai 10 minuti.
   * - 4
     - ``49  .  N``
     - ``072  .  W``
     - Al grado.

Le cifre vengono svuotate, mai arrotondate via, come fanno i decodificatori di
riferimento che leggono una cifra svuotata come "sconosciuta". Il riporto di
arrotondamento si applica prima, quindi una coordinata che arrotonda al grado
successivo viene riportata in quel grado e non in quello precedente.

Un livello diverso da zero forza anche il formato non compresso, per lo stesso
tipo di motivo di un'estensione dati: il formato compresso non ha cifre decimali
da svuotare, quindi rispettare una spunta di *compresso* insieme all'ambiguità
trasmetterebbe la posizione esatta che l'operatore ha chiesto di nascondere.
Mic-E, invece, porta l'ambiguità in modo nativo e non ha bisogno di questo
ripiego.

Localizzatore Maidenhead nei rapporti di stato
==============================================

*Localizzatore Maidenhead nei rapporti di stato*, anch'esso valido per tutta la
stazione nella pagina Stazione, antepone a ogni rapporto di stato il
localizzatore della posizione propria di quel beacon, il byte della tabella dei
simboli e il codice del simbolo — la forma ``>IO91SX/G`` di APRS101 cap.16 —
seguito da uno spazio e dal testo di stato configurato. I ricevitori che
comprendono la forma posizionano la stazione con il solo localizzatore; gli
altri mostrano il tutto come testo di stato. Il testo configurato non viene mai
interpretato.

I timestamp sono UTC
====================

I timestamp dei beacon sono zulu/UTC (``051200z``) secondo la specifica APRS —
per cui ``time_sync.c`` imposta l'orologio di sistema su ``TZ=UTC0``. Nel
firmware non esiste alcun offset di ora locale.
