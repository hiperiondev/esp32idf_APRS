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

Un beacon abilitato su entrambe le tratte viene costruito **due volte**, una per
tratta, e le due righe differiscono in un solo punto: il percorso. La
trasmissione radio porta la selezione di digipeater fatta nella pagina di quel
beacon; la trasmissione via APRS-IS porta ``TCPIP*`` e nient'altro, che è quanto
`la guida alla connessione di aprs-is.net
<https://www.aprs-is.net/Connecting.aspx>`_ richiede a un pacchetto originato
dal client: un alias ``WIDEn-N`` inviato lì descriverebbe ripetitori che il
pacchetto non ha mai attraversato. Entrambe le righe provengono dallo stesso
costruttore e dallo stesso snapshot di configurazione preso sotto lock, quindi
nient'altro può divergere fra loro, e ogni tratta è registrata per ciò che ha
davvero fatto invece che con un'unica riga incondizionata.

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

Estensioni dati (PHG / RNG / DFS / DF)
======================================

I beacon di posizione dell'IGate e del digipeater possono portare ciascuno
una delle estensioni dati APRS da stazione fissa nello slot di 7 byte che segue
il codice del simbolo — lo stesso slot che una stazione in movimento usa per
rotta e velocità, ed è per questo che ne viene emessa sempre una sola.
*Abilita estensione dati* nella pagina IGate o Digi apre lo slot di quel ruolo,
e *Tipo di estensione* sceglie quale lo riempie. I due ruoli hanno impostazioni
proprie, così un IGate e un digipeater sulla stessa stazione con SSID diversi
possono pubblicare coperture diverse:

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
   * - DF
     - ``000/000/270/735``
     - Il rapporto DF del cap.8 di APRS101: il rilevamento verso un segnale,
       seguito dalla terna NRQ che lo qualifica — rilevazioni per periodo di
       campionamento (``N``, dove 0 dichiara che la terna non ha significato), il
       codice di portata (``R``, che vale 2\ :sup:`R` miglia) e la precisione del
       rilevamento (``Q``, dove 9 è meglio di un grado). È la forma che il
       capitolo descrive per una stazione di radiogoniometria che riporta il
       proprio rilevamento. Questi beacon sono di stazione fissa e non hanno una
       sorgente di rotta e velocità, quindi la coppia iniziale è il ``000/000``
       che la specifica usa per dichiararlo. Lo stesso codificatore costruisce il
       token per oggetti e item, dove riporta un rilevamento preso su un'altra
       stazione.

Il rapporto DF è l'unica estensione con un requisito di simbolo tutto suo. Il suo
token è di quindici byte dove lo slot ne ha sette, e il capitolo 8 afferma che il
rilevamento e l'NRQ sono significativi solo quando il rapporto porta il simbolo
DF — tabella dei simboli ``/`` e codice simbolo ``\``. Un ricevitore che vede un
altro simbolo non ha motivo di guardare oltre lo slot: legge ``000/000`` come una
normale coppia rotta/velocità e prende ``/270/735`` come i primi otto caratteri
del campo commento. Perciò il DF viene trasmesso solo con quella coppia di
simbolo; con qualsiasi altra lo slot resta vuoto, il log nomina il simbolo che ha
soppresso il rapporto e la pagina mostra una nota accanto al tipo di
estensione non appena i due non concordano. La stessa regola vale per oggetti e
item e in ricezione: una continuazione DF in arrivo viene sempre scavalcata così
da non finire mai nel commento, ma il suo rilevamento viene letto solo quando il
simbolo del trasmettitore è il simbolo DF.

PHG usa tutti e quattro i sottocampi, DFS tutti tranne la potenza di
trasmissione, e RNG e DF nessuno — DF ha invece i propri input di rilevamento e
NRQ; la pagina disabilita gli input che il tipo selezionato non usa. Poiché un
controllo disabilitato non viene inviato nel POST, i valori memorizzati degli
*altri* tipi sopravvivono al passaggio avanti e indietro.

Abilitare PHG, DFS o un rapporto DF che il simbolo consente forza il formato di
posizione non compresso. Il formato
compresso non ha spazio per lo slot di 7 byte (APRS101 cap.9 afferma che non
supporta il PHG), e un rapporto DF è più largo dello slot stesso, quindi emettere
quei byte dentro un rapporto compresso sarebbe semplicemente un dato sbagliato, e
scartare l'estensione per mantenere la compressione perderebbe un campo che
l'operatore ha abilitato esplicitamente. Il firmware registra un warning che
nomina quale delle due impostazioni ha ceduto, invece di lasciarlo scoprire in
onda. Un rapporto DF che il simbolo sopprime non mette byte nello slot, quindi
non costa al beacon la sua compressione.

L'ambiguità di posizione è un'altra cosa e viaggia con ciascuna di esse: azzera
cifre decimali del formato non compresso, che mantiene il suo slot di estensione,
quindi nessuna delle due impostazioni deve cedere all'altra.

RNG è l'eccezione, perché il formato compresso porta nativamente una portata
radio precalcolata: i due byte ``cs`` contengono ``{`` seguito da una cifra di
portata, decodificata come ``2 × 1,08^s`` miglia. Una baliza con RNG selezionato
e la compressione spuntata resta quindi compressa, con la portata ripiegata in
quei due byte e senza alcun token ``RNGrrrr`` nel campo informativo. La forma
compressa quantizza la portata a passi di circa l'8 per cento e parte da un
minimo di 2 miglia, quindi una portata impostata sotto quel valore viene
trasmessa come 2.

PHG è l'estensione che ci si aspetta da un digipeater. Il capitolo 7 la presenta
come il modo in cui una stazione dichiara il cerchio di copertura su cui i vicini
ragionano quando scelgono un percorso, e i client di mappa disegnano quel cerchio
prima di tutto per i digipeater — per questo la pagina Digi offre gli stessi
quattro tipi della pagina IGate, sulle proprie impostazioni, invece di lasciare
vuoto lo slot di quel ruolo.

Il beacon Tracker porta PHG e nient'altro, attivato con *Includi estensione dati
PHG* nella sua stessa pagina. Lì non ci sono sottocampi da compilare: i quattro
valori sono i dati d'antenna della stazione stessa, modificati una volta sola nel
blocco PHG della pagina Stazione. Un tracker che trasmette beacon in Mic-E
mantiene il token — Mic-E non ha uno slot di 7 byte dopo il codice di simbolo, ma
APRS 1.2 stabilisce che il suo campo di testo può portare un normale campo di
commento di posizione, PHG incluso, ed è lì che va: dopo il blocco di frequenza,
così una radio continua a sintonizzarsi automaticamente dai primi byte, e prima
del commento dell'operatore.

Posizione GPS in tempo reale e rotta/velocità
================================================

La posizione di ogni beacon usa per default la latitudine/longitudine/
altitudine fissa salvata nella propria pagina — l'unica modalità offerta dai
beacon di IGate e Digipeater. La pagina del beacon Tracker aggiunge un
ulteriore interruttore, *Usa posizione GPS in tempo reale*
(``g_config.trk_use_live_gps``), indipendente dalla casella *Usa GPS*
descritta sopra: mentre *Usa GPS* copia la posizione del ricevitore GNSS nei
campi fissi una sola volta, al salvataggio, *Usa posizione GPS in tempo reale*
fa sì che ``trackerBeaconService()`` (``main/beacon.c``) rilegga il ricevitore
— tramite ``gps_snapshot()`` (``main/gps.c``) — a ogni singola trasmissione, e
trasmetta quella latitudine/longitudine/altitudine in tempo reale al posto dei
valori fissi.

Una posizione in tempo reale viene usata solo quando è effettivamente valida:
``gps_snapshot()`` deve riportare ``valid`` e ``has_position`` (il ricevitore
ha una soluzione RMC attiva, non scaduta oltre ``GPS_LINK_TIMEOUT_S``).
Qualsiasi cosa al di sotto di questo — ricevitore spento, ancora in fase di
acquisizione, o collegamento silenzioso — lascia i parametri del beacon
esattamente come letti da ``g_config.trk_lat``/``trk_lon``/``trk_alt``, così
il Tracker continua a trasmettere la sua posizione fissa di riserva invece di
saltare una trasmissione o inviarne una scaduta. L'altitudine viene sostituita
solo quando il flag ``has_altitude`` del ricevitore stesso è impostato, dato
che una posizione 2D non porta alcuna altitudine da fornire.

Quando il ricevitore riporta anche rotta e velocità per quella stessa lettura
(``has_course`` e ``has_speed`` entrambi impostati), il beacon Tracker li porta
a sua volta, nel formato scelto:

* **Non compresso** — l'estensione dati standard ``CSE/SPD``
  (``"%03u/%03u"``, gradi veri e nodi), nello stesso slot di 7 byte occupato
  da PHG/RNG/DFS/DF. Un'estensione PHG attiva ha comunque priorità su CSE/SPD
  per quello slot, la stessa precedenza che Oggetti/Item danno a PHG rispetto
  al CSE/SPD proprio di un elemento in movimento.
* **Compresso** — ripiegato nello slot a due byte proprio del campo compresso
  (``cs/T``) tramite ``aprs_compressed_cs_from_course_speed()``, lo stesso
  codificatore usato da Oggetti/Item per un elemento in movimento. Una portata
  radio precalcolata ha comunque priorità su rotta/velocità per quello slot,
  perché RNG è un'impostazione che l'operatore ha attivato esplicitamente e
  non ha altrove dove andare; rotta/velocità cede il passo allo stesso modo
  dell'altitudine.
* **Mic-E** — la coppia reale di rotta/velocità, al posto dello "sconosciuto"
  ``000/000`` che invia ogni beacon a posizione fissa (e una posizione in
  tempo reale senza rotta/velocità riportate in quel ciclo).

La velocità viene convertita dai km/h del ricevitore (``gps_data_t::speed_kmh``)
ai nodi in cui sono definiti tutti questi campi, usando lo stesso fattore 1.852
che ``gps.c`` applica già nel verso opposto quando interpreta una velocità NMEA
in nodi nello snapshot.

Smart Beaconing
================

*SmartBeaconing* (``g_config.trk_sb_enable``, pagina Tracker) accorcia o
allunga automaticamente l'intervallo del beacon Tracker in base alla
velocità attuale, e forza un beacon anticipato in presenza di un
cambiamento di rotta sufficientemente marcato, seguendo l'algoritmo
standard pubblicato da Hans-Gunnar Lundahl e implementato da HamHUD e
dalla maggior parte dei tracker moderni. Ha effetto solo insieme a *Usa
posizione GPS in tempo reale* descritto sopra: SmartBeaconing non ha
velocità né rotta su cui lavorare senza una posizione in tempo reale, e il
beacon Tracker torna all'intervallo fisso ``trk_interval`` ogni volta che
non ne è disponibile una - lo stesso ripiego che *Usa posizione GPS in
tempo reale* applica già alla posizione.

Frequenza dinamica
-------------------

Tra le due soglie di velocità seguenti, ``smartBeaconingInterval()``
(``main/beacon.c``) interpola linearmente l'intervallo del beacon dal
valore lento al valore veloce man mano che la velocità aumenta:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Impostazione
     - Significato
   * - Intervallo lento
     - Periodo del beacon usato a o sotto la *Velocità bassa* - la cadenza
       per una stazione ferma o in movimento lento (valore di fabbrica:
       600 s).
   * - Intervallo veloce
     - Periodo del beacon usato a o sopra la *Velocità alta* (valore di
       fabbrica: 60 s).
   * - Velocità bassa / Velocità alta
     - Le due soglie di velocità, km/h, che delimitano l'interpolazione.
       Una posizione in tempo reale senza rotta/velocità riportate in
       quel ciclo (ancora in acquisizione, o un ricevitore che omette la
       rotta a velocità zero) viene trattata come frequenza lenta.

Corner-pegging
---------------

Indipendentemente dalla frequenza sopra, ``trackerBeaconService()``
interroga la posizione in tempo reale ogni pochi secondi alla ricerca di
un cambiamento di rotta rispetto all'ultimo beacon trasmesso e, non
appena questo supera la soglia di svolta, anticipa la prossima
trasmissione a quel ciclo invece di attendere che l'intervallo sopra
trascorra - il comportamento classico di SmartBeaconing di un tracker che
segnala visibilmente la curva che sta percorrendo, invece di segnalare
una posizione solo più avanti sulla nuova strada. La soglia stessa si
allarga a bassa velocità e si restringe ad alta velocità:

.. code-block:: text

   soglia (gradi) = Angolo di svolta + Pendenza di svolta / velocità (km/h)

per cui una stazione veloce attiva il corner-pegging con un cambiamento
di rotta molto più piccolo rispetto a una lenta - *Pendenza di svolta* è
ciò che rende innocuo il normale rumore di rotta a bassa velocità
(manovre in un parcheggio, una lettura di rotta GPS che oscilla da fermi)
senza per questo attenuare il rilevamento delle curve ad alta velocità.
*Tempo minimo tra svolte* rifiuta inoltre un secondo beacon da
corner-pegging entro quel numero di secondi dall'ultimo, indipendentemente
da quanto marcato appaia il nuovo cambiamento di rotta, così che una
stazione lenta o momentaneamente ferma con una lettura di rotta rumorosa
non possa riattivare il corner-pegging a ogni ciclo.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Impostazione
     - Significato
   * - Angolo di svolta
     - Cambiamento minimo di rotta, in gradi, che attiva una svolta a o
       sopra la soglia di velocità alta (valore di fabbrica: 25°).
   * - Pendenza di svolta
     - Gradi aggiunti a *Angolo di svolta*, scalati in modo inversamente
       proporzionale alla velocità attuale, allargando la soglia effettiva
       a bassa velocità (valore di fabbrica: 255°).
   * - Tempo minimo tra svolte
     - Intervallo minimo, in secondi, consentito tra due beacon da
       corner-pegging (valore di fabbrica: 15 s).

Una trasmissione inviata per qualsiasi motivo - l'intervallo di frequenza,
o il corner-pegging - aggiorna il riferimento del corner-pegging, cosicché
un beacon appena inviato per una rotta non riattivi subito il corner-pegging
sulla stessa rotta.

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

Sovrapposizioni numeriche in un rapporto compresso
==================================================

Una sovrapposizione di simbolo si scrive nella posizione di tabella della coppia
di simbolo, e APRS 1.2 cap.21 ammette che sia una lettera ``A``-``Z`` o una
cifra ``0``-``9``: quelle numeriche sono il modo in cui un digipeater annuncia la
propria politica di instradamento, il "cerchio numerato" della tabella
alternativa. Il formato compresso non può portare la cifra in sé: il primo byte
di un campo di posizione è proprio ciò che dice a un ricevitore quale dei due
formati sta leggendo, e una cifra iniziale significa non compresso.

Per questo una sovrapposizione numerica viaggia come la lettera minuscola
corrispondente, ``a`` per ``0`` fino a ``j`` per ``9``, e il ricevitore la
rimappa sulla cifra. Il firmware applica quella mappatura mentre costruisce il
campo, così la sovrapposizione si configura una volta sola, come cifra, e la
spunta *Comprimi posizione* non cambia nulla né di come si inserisce né di come
viene tracciata.

Anche i due byte della coppia sono limitati in ingresso — nel modulo e di nuovo
alla lettura del file di configurazione — perché nessuno dei due è cosmetico: un
identificatore di tabella fuori da ``/``, ``\``, ``A``-``Z`` e ``0``-``9``
ripiega sulla tabella primaria, e un codice fuori dall'intervallo stampabile
ripiega su ``&``.

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

Testo di stato Mic-E
====================

La coda di testo libero del campo informazioni Mic-E — tutto ciò che segue il
blocco di frequenza, il token PHG/estensione dati e il campo altitudine —
porta, byte per byte, ciò che l'operatore ha scritto come commento del
beacon. L'unica eccezione è il primo byte di quella coda: APRS12c cap.10
riserva una ``,`` o ``0x1d`` iniziale al sottoformato Mic-E Telemetry Data
(ormai obsoleto), quindi un commento che iniziasse con uno di quei due byte
verrebbe letto come telemetria anziché come testo. ``aprs_mice_encode()`` si
protegge da questo inserendo un singolo spazio prima di uno di quei
caratteri prima di aggiungere il commento; un commento che inizia con
qualsiasi altro byte arriva in aria invariato. Lo spazio inserito non porta
informazione propria e un client ricevente lo mostra come un normale spazio
iniziale del commento.

Direzione d'antenna ed ERP nei rapporti di stato
================================================

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

Il campo informativo di stato ha un tetto di 63 byte, e l'assemblaggio scarta i
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
grado descritto sopra si applica prima, quindi una coordinata i cui minuti
danno un 60.00 pieno per errore in virgola mobile al limite di un grado viene
riportata nel grado successivo e non in quello precedente.

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
``DDMM.mmN``/``DDDMM.mmW`` troncano via — un ordine di grandezza in più
di precisione rispetto a quella che il formato non compresso porta da solo,
pari alla risoluzione in virgola mobile di latitudine/longitudine di questo
firmware. Sia il campo base sia questa cifra extra derivano dallo stesso
troncamento del valore dei minuti, quindi aggiungerla recupera sempre una
posizione almeno tanto vicina a quella reale quanto il solo campo base.

firmware.

Poiché ripristina precisione, ``!DAO!`` viene applicata solo quando
*Position ambiguity* è 0 e il formato non è quello compresso: una stazione
che nasconde deliberatamente la propria posizione, o che invia già
coordinate compresse a piena risoluzione, non deve recuperare quella
risoluzione tramite questa estensione. Un ricevitore che non riconosce
l'estensione vede semplicemente cinque byte in più di testo nel commento,
quindi è sempre sicuro attivarla.

Il marcatore di non archiviazione !x!
=======================================

*Richiedi ad APRS-IS di non archiviare i miei pacchetti*, nella pagina
Stazione, è un'impostazione di privacy valida per tutta la stazione,
disattivata per impostazione predefinita. Se attivata, ogni campo di testo
libero proprio viene preceduto dal marcatore di non archiviazione di APRS-IS,
``!x!``, seguito da uno spazio e poi dal testo dell'operatore, se presente. Il
marcatore è indirizzato ai database dietro APRS-IS, non a un gateway: chiede
loro di non memorizzare il pacchetto, ma non lo trattiene da RF né da APRS-IS
stesso, e un ricevitore che non lo riconosce vede semplicemente tre byte in
più di testo nel commento.

I campi che raggiunge sono:

* i commenti di posizione di Tracker, IGate e Digipeater,
* i testi di stato di Tracker, IGate e Digipeater,
* il commento del rapporto meteorologico, in tutte e quattro le sue forme
  (oggetto, posizionata con e senza marca temporale, e senza posizione),
* i commenti di oggetti e item,
* il testo di bollettini e annunci.

Tre tipi di pacchetto restano fuori deliberatamente. Il testo di un messaggio
non è testo descrittivo su questa stazione: è una parola che il
corrispondente legge nel corpo di un messaggio a lui indirizzato. I pacchetti
di definizione della telemetria ``PARM``/``UNIT``/``EQNS``/``BITS`` sono
metadati a formato fisso, senza spazio per testo libero e con un budget di
cui la definizione stessa ha bisogno. Le risposte alle interrogazioni
rispondono alla domanda di un'altra stazione anziché riportare la posizione o
lo stato di questa.

Tutti questi campi sono costruiti da un unico builder condiviso,
``aprs_free_text_build()`` in ``main/include/aprs_free_text.h``, che applica
il marcatore e rimuove i caratteri che APRS riserva al gruppo di telemetria
di commento in base 91 (``|`` e ``~``) nello stesso punto. Un campo in cui
l'operatore ha già scritto il marcatore viene lasciato intatto, quindi il
marcatore non viene mai inviato due volte.

Questa impostazione influisce solo sui pacchetti che questa stazione
origina. Un pacchetto che questa stazione ritrasmette, sia esso da IGate a
RF, da RF a IGate o digipetuto, viene passato invariato; il marcatore, se la
stazione di origine lo aveva già inserito, viaggia comunque con esso, perché
la ritrasmissione non riscrive mai i byte del payload.

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

APRS101 cap.16 limita il campo informativo di un rapporto di stato a 63 byte:
il DTI ``>``, seguito da un timestamp DHM opzionale di 7 caratteri e al
massimo 55 caratteri di testo di stato, oppure senza timestamp e al massimo 62
caratteri di testo di stato. Tutto ciò che il rapporto può portare oltre alle
parole dell'operatore viene preso dallo stesso budget — il campo iniziale (il
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

Lo spazio separatore che precede il testo di stato appartiene al blocco che lo
precede, quindi viene scartato insieme a quel blocco: un rapporto rimasto senza
campo iniziale e senza blocco di frequenza — perché non ne è stato configurato
alcuno, oppure perché il budget qui sopra li ha tolti entrambi — esce come
``>Il mio testo di stato``, con le parole dell'operatore subito dopo il DTI
``>``, che è la forma definita da APRS101 cap.16. Lo spazio compare solo fra due
elementi entrambi presenti.

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
