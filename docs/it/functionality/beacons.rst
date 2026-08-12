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

Abilitare PHG o DFS forza il formato di posizione non compresso. Il formato
compresso non ha spazio per lo slot di 7 byte (APRS101 cap.9 afferma che non
supporta il PHG), quindi emettere quei byte dentro un rapporto compresso sarebbe
semplicemente un dato sbagliato, e scartare l'estensione per mantenere la
compressione perderebbe in silenzio un campo che l'operatore ha abilitato
esplicitamente.

RNG è l'eccezione, perché il formato compresso porta nativamente una portata
radio precalcolata: i due byte ``cs`` contengono ``{`` seguito da una cifra di
portata, decodificata come ``2 × 1,08^s`` miglia. Una baliza con RNG selezionato
e la compressione spuntata resta quindi compressa, con la portata ripiegata in
quei due byte e senza alcun token ``RNGrrrr`` nel campo informativo. La forma
compressa quantizza la portata a passi di circa l'8 per cento e parte da un
minimo di 2 miglia, quindi una portata impostata sotto quel valore viene
trasmessa come 2.

Il beacon Tracker porta PHG e nient'altro, attivato con *Includi estensione dati
PHG* nella sua stessa pagina. Lì non ci sono sottocampi da compilare: i quattro
valori sono i dati d'antenna della stazione stessa, modificati una volta sola nel
blocco PHG della pagina Stazione. Un tracker che trasmette beacon in Mic-E
mantiene il token — Mic-E non ha uno slot di 7 byte dopo il codice di simbolo, ma
APRS 1.2 stabilisce che il suo campo di testo può portare un normale campo di
commento di posizione, PHG incluso, ed è lì che va: dopo il blocco di frequenza,
così una radio continua a sintonizzarsi automaticamente dai primi byte, e prima
del commento dell'operatore.

Altitudine compressa
====================

Un rapporto di posizione compresso non ha un token di commento per l'altitudine,
ma non gli serve. Gli stessi due byte ``cs`` che portano rotta/velocità o una
portata radio portano un'altitudine quando il byte di tipo dichiara GGA come
sorgente NMEA, decodificata come ``1,002^(c × 91 + s)`` piedi. Un beacon con
*Includi altitudine* e *Comprimi posizione* entrambe spuntate usa quella forma, e
il token ``/A=`` viene omesso dal commento, così l'altitudine è dichiarata una
volta sola — nove byte di commento risparmiati in cambio di nulla, con un passo
di circa lo 0,2 %.

I due byte portano una cosa per volta, quindi un beacon che abbia anche RNG
selezionato li cede alla portata: la portata non ha altro posto dove andare,
mentre l'altitudine ha ancora ``/A=`` come ripiego, ed è questo che un beacon
del genere emette.

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
portano i propri identificatori ``;`` e ``)``. Mic-E dichiara la stessa cosa in
un altro punto: la sua posizione viaggia nell'indirizzo di destinazione AX.25,
quindi non gli avanza un identificatore di tipo di dato, e il flag di capacità
di messaggistica è portato dal byte TYPE (`` ` `` capace di messaggi, ``'``
tracker a senso unico) che segue il byte della tabella dei simboli. Entrambi i
formati leggono la stessa spunta *Abilita messaggistica*, quindi non possono
contraddirsi su ciò che questa stazione dichiara.

Commento di posizione Mic-E
===========================

Un rapporto Mic-E porta, nei bit A/B/C del suo indirizzo di destinazione, uno
di quindici *commenti di posizione*: i sette valori standard M0 *Off Duty*, M1
*En Route*, M2 *In Service*, M3 *Returning*, M4 *Committed*, M5 *Special* e M6
*Priority*; sette valori personalizzati di definizione locale C0–C6; ed
*Emergency*, che è lo schema con tutti e tre i bit a zero. Ogni radio APRS
Kenwood e Yaesu lo espone in un menu del pannello frontale, e i client riceventi
lo mostrano accanto alla stazione.

*Commento di posizione Mic-E*, nella pagina Tracker, sceglie fra i quattordici
valori standard e personalizzati. Si applica solo quando *Codifica posizione
Mic-E* è attiva, perché nessun altro formato ha un campo per esso, e il valore
di fabbrica è M0 *Off Duty*: quello convenzionale per una stazione che non si
muove, che è ciò che questo firmware trasmette.

Emergency è deliberatamente assente da quell'elenco. Trasmetterlo chiede ad
altri operatori, e in alcune regioni ai servizi di emergenza, di rispondere a
un'emergenza reale; non è qualcosa che una pagina di configurazione debba poter
attivare con un clic sbagliato e lasciare attivo per tutti i beacon successivi.
In ricezione è gestito per intero: vedere :ref:`it-filtering` per il
decodificatore, e il registro del traffico per la riga di avviso che
un'emergenza ricevuta produce.

Direzione d'antenna ed ERP nei rapporti di stato
===============================================

Un rapporto di stato può terminare con due caratteri dopo un ``^``: la direzione
d'antenna in unità di dieci gradi, e un codice che rappresenta la potenza
irradiata efficace. L'operatività in meteor scatter è la ragione d'essere della
coppia — le due cifre che serve a un corrispondente per sapere se vale la pena
aspettare un burst — e APRS101 cap.16 la fissa come *ultimo* campo del testo di
stato, che è l'unico posto in cui può essere riconosciuta.

Entrambe le metà si impostano nella pagina Stazione e valgono per tutta la
stazione, come l'opzione Maidenhead: *Direzione antenna nei rapporti di stato*
avanza di dieci gradi da 0 a 350, ed *ERP nei rapporti di stato* offre la tabella
della specifica stessa, da 10 W a 7290 W nei passi che seguono il quadrato della
cifra del codice. Una direzione senza potenza, o una potenza senza direzione, non
dice nulla, quindi il blocco viene emesso solo quando entrambe sono impostate —
lasciarne una su *Off* è ciò che fa una stazione che non lavora in meteor
scatter, e allora trasmette esattamente il rapporto di stato che trasmetteva
prima.

Il campo informativo di stato ha un tetto di 70 byte, e l'assemblaggio scarta i
suoi blocchi opzionali in ordine finché non entra: prima il campo iniziale, poi
il blocco di frequenza. La coppia direzione/ERP non viene mai scartata. Sono tre
byte, e una stazione che trasmette un rapporto di stato durante un appuntamento
di meteor scatter lo trasmette proprio per quei tre byte.

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

L'estensione di precisione !DAO!
=================================

*Estensione DAO* nella pagina Station è una seconda impostazione di
precisione, a livello di stazione, che si affianca all'ambiguità invece di
sostituirla. Quando è attiva, ``aprs_dao_build()`` aggiunge l'estensione di
precisione/datum ``!DAO!`` nella sua forma leggibile (WGS-84,
``aprs12/datum.txt``) al commento di ogni rapporto di posizione non
compresso e al campo di testo Mic-E, dove lo standard riserva la stessa
posizione finale. I cinque byte recuperano, come una cifra decimale in più
per asse, la terza cifra dei minuti che i campi semplici
``DDMM.mmN``/``DDDMM.mmW`` arrotondano via — un ordine di grandezza in più
di precisione rispetto a quella che il formato non compresso porta da solo,
pari alla risoluzione in virgola mobile di latitudine/longitudine di questo
firmware.

Poiché ripristina precisione, ``!DAO!`` viene applicata solo quando
*Position ambiguity* è 0 e il formato non è quello compresso: una stazione
che nasconde deliberatamente la propria posizione, o che invia già
coordinate compresse a piena risoluzione, non deve recuperare quella
risoluzione tramite questa estensione. Un ricevitore che non riconosce
l'estensione vede semplicemente cinque byte in più di testo nel commento,
quindi è sempre sicuro attivarla.

Localizzatore Maidenhead nei rapporti di stato
==============================================

*Localizzatore Maidenhead nei rapporti di stato*, anch'esso valido per tutta la
stazione nella pagina Stazione, antepone a ogni rapporto di stato il
localizzatore della posizione propria di quel beacon, il byte della tabella dei
simboli e il codice del simbolo — la forma ``>IO91SX/G`` di APRS101 cap.16 —
seguito da uno spazio e dal testo di stato configurato. I ricevitori che
comprendono la forma posizionano la stazione con il solo localizzatore; gli
altri mostrano il tutto come testo di stato. Il testo configurato non viene mai
interpretato. Il localizzatore è sempre il campo fisso di 6 caratteri, in
maiuscolo.

APRS101 cap.16 ammette un solo campo iniziale nel campo informativo di un
rapporto di stato: il timestamp DHM oppure il localizzatore Maidenhead, mai
entrambi insieme — un ricevitore legge ciò che segue immediatamente il DTI
``>`` come il localizzatore, quindi un timestamp in quella posizione verrebbe
letto erroneamente come tale. Quando *Timestamp di stato* e *Localizzatore
Maidenhead nei rapporti di stato* sono entrambi attivi sullo stesso beacon, il
localizzatore ha la precedenza e il timestamp viene omesso dai rapporti di
stato di quel beacon, poiché il localizzatore porta la posizione della
stazione, cosa che il timestamp non porta.

Budget di lunghezza del rapporto di stato
=========================================

APRS101 cap.16 limita il campo informativo di un rapporto di stato a 70 byte: il
DTI ``>``, un timestamp DHM opzionale di 7 caratteri e al massimo 62 caratteri
di testo di stato. Tutto ciò che il rapporto può portare oltre alle parole
dell'operatore viene preso dallo stesso budget — il campo iniziale (il
timestamp, oppure il localizzatore Maidenhead quando ha la precedenza) e il
blocco di frequenza — e un testo di stato completo di 49 caratteri più
entrambi i blocchi opzionali chiede più di quanto entri.

Quando questo accade i blocchi opzionali vengono scartati, in quest'ordine, fino
a che il campo entra:

#. il campo iniziale (il localizzatore Maidenhead, oppure il timestamp quando
   non si usa il localizzatore), che si limita a ripetere informazioni che
   questa stazione già trasmette altrove — la propria posizione o l'ora
   corrente;
#. il blocco di frequenza, l'unica parte del rapporto su cui una radio ricevente
   può agire.

Il testo di stato configurato non viene mai accorciato: è ciò per cui il
rapporto esiste. Se non entra nemmeno da solo, l'intero rapporto viene rifiutato
e il motivo registrato, invece di mettere in aria una riga di stato troncata — e
quindi malformata.

Blocco di frequenza
===================

Quando un beacon ha una frequenza di monitoraggio configurata, sia il suo
commento di posizione sia il suo rapporto di stato iniziano con il campo fisso
di 10 byte della frequenza di ``freqspec.txt``, seguito dal tono
(``Tnnn``/``Toff``) e, per un ripetitore duplex, dallo shift in unità di 10 kHz.
Quale delle tre forme definite dalla specifica venga usata dipende solo dalla
frequenza:

.. list-table::
   :header-rows: 1
   :widths: 26 24 50

   * - Frequenza
     - Emesso
     - Forma
   * - Sotto i 100 MHz
     - ``  50.62 MHz``
     - Forma da 10 kHz ``FFF.FF MHz``, allineata a destra contro il suo spazio
   * - 100.000-999.999 MHz
     - ``146.520MHz``
     - Forma da 1 kHz ``FFF.FFFMHz``
   * - Sopra i 999.999 MHz
     - ``A96.000MHz``
     - Designazione a lettera per le microonde, una lettera per blocco da 100 MHz

La tabella delle lettere copre solo le bande che ``freqspec.txt`` enumera:
A (1200), B (2300), C (2400), D (3400), E (5600), F (5700), G (5800), H (10100),
I (10200), J (10300), K (10400), L (10500), M (24000), N (24100) e O (24200),
ciascuna estesa dalla sua base più 99 MHz. Una frequenza sopra i 999.999 MHz
fuori da tutte queste non ha alcuna forma da 10 byte, quindi nessun blocco viene
emesso e l'omissione viene registrata — un campo da 11 byte sposterebbe ogni
byte che un ricevitore legge dopo di esso.

I timestamp sono UTC
====================

I timestamp dei beacon sono zulu/UTC (``051200z``) secondo la specifica APRS —
per cui ``time_sync.c`` imposta l'orologio di sistema su ``TZ=UTC0``. Nel
firmware non esiste alcun offset di ora locale.
