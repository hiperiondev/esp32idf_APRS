.. _it-aprs-coverage:

=============================
Copertura del protocollo APRS
=============================

Questa pagina confronta il firmware con il protocollo APRS in sé, capitolo per
capitolo, invece di confrontarlo con altri programmi APRS. Dove
:ref:`it-limitations` chiede *"come si colloca questo progetto rispetto a
Direwolf o Xastir?"*, questa pagina chiede *"quanta parte della specifica va
in onda?"*.

Il riferimento usato in tutta la pagina è l'APRS Protocol Reference 1.2 — il
consolidamento dell'APRS101 1.0.1 originale (2000), dell'addendum APRS 1.1
approvato nel 2004 e delle aggiunte 1.2 pubblicate da allora — insieme ai
singoli file di specifica su ``aprs.org`` per le funzioni che esistono solo
lì.

Legenda:

* ✅ — Implementato e funzionante
* ⚠️ — Implementazione parziale o limitata
* ❌ — Non implementato

Un ❌ non è automaticamente un difetto. Diverse righe descrivono formati che
la specifica stessa segna come obsoleti o "non raccomandati", e alcune
descrivono convenzioni che richiedono hardware radio che questo progetto non
pilota. La colonna Note chiarisce quale sia quale.

In tutte le tabelle, *trasmettere* significa che la stazione può originare il
formato, e *ricevere* significa che il formato viene decodificato abbastanza
da alimentare la cache dei duplicati, il filtro di distanza, Last Heard e il
registro del traffico — non semplicemente ritrasmesso. Un pacchetto che il
firmware non sa decodificare viene comunque digipetato e inoltrato ad APRS-IS,
perché entrambi i percorsi lavorano sul campo indirizzi AX.25.

AX.25 e accesso al canale (cap. 3)
==================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Trame UI AX.25, controllo 0x03 / PID 0xF0
     - ✅
     - Soft-modem completo, in trasmissione e ricezione, sull'ADC e DAC dell'ESP32 stesso. Solo trame UI senza connessione, che è tutto ciò che APRS usa.
   * - Campo indirizzi: destinazione, sorgente e 0-8 digipeater
     - ✅
     - Decodificato e ricodificato rispettando il bit di già-ripetuto. Il decodificatore percorre il campo indirizzi confrontandolo con la lunghezza della trama, quindi un'intestazione che promette più ripetitori di quanti la trama ne porti viene rifiutata invece di essere letta oltre. I costruttori di percorso applicano il limite di 8 indirizzi.
   * - Accesso al canale CSMA p-persistente
     - ✅
     - Rilevamento di portante più un valore di persistenza e un TXDelay configurabili, con una soglia anti-starvation perché un canale occupato non blocchi una trama per sempre. Le trasmissioni forzate sono contate separatamente per canale occupato e per sorteggio di persistenza fallito.
   * - Correzione d'errore FX.25
     - ✅
     - Tre modalità: spento, sola ricezione, ricezione e trasmissione. Tutti gli 11 tag di correlazione. I blocchi trasmessi restano leggibili da un ricevitore AX.25 semplice.
   * - Interfaccia host KISS / AGWPE
     - ❌
     - La stazione non può fare da TNC per software client esterno. È deliberato: il firmware è una stazione completa, non una periferica modem.

Campi indirizzo di destinazione e sorgente (cap. 4)
===================================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Identificatore di versione software nella destinazione (TOCALL)
     - ⚠️
     - Ogni pacchetto originato usa ``APZ32L``. È un TOCALL sperimentale valido, ma non è registrato nell'elenco ``aprsorg/aprs-deviceid``, quindi i client riceventi mostrano la stazione come applicazione sperimentale generica invece di nominare il firmware.
   * - Dati Mic-E codificati nell'indirizzo di destinazione
     - ✅
     - Le cifre di latitudine, nord/sud, est/ovest e lo scostamento di longitudine sono codificati in trasmissione e riassemblati in ricezione insieme alla metà che viaggia nel campo informazioni.
   * - Percorso generico di digipeater nell'SSID di destinazione
     - ✅
     - Riconosciuto dal digipeater come forma di instradamento legacy, dietro un interruttore esplicito spento per impostazione predefinita perché non scavalchi mai un percorso esplicito.
   * - Simbolo nell'indirizzo di destinazione (GPSxyz / SYMxyz)
     - ✅
     - Letto in ricezione quando il campo informazioni non fornisce un simbolo proprio, in tutte le forme ``GPSxy``, ``SPCxy``, ``SYMxy``, ``GPSCnn`` e ``GPSEnn``, incluso il carattere di sovrapposizione sui simboli della tabella alternativa. I pacchetti Mic-E sono esclusi, perché il loro indirizzo di destinazione trasporta dati di posizione. Il traffico originato mantiene il simbolo nel campo informazioni, quindi non viene mai scritto in questa forma.
   * - Simbolo dall'SSID dell'indirizzo sorgente (obsoleto)
     - ⚠️
     - Applicato in ricezione solo come ultimo passo della catena di precedenza dei simboli, e solo ai pacchetti NMEA grezzi, l'unico caso per cui la convenzione è stata inventata. Per ogni altro tipo di dato l'SSID resta il ruolo della stazione, che è ciò che significa oggi.
   * - Reti alternative
     - ❌
     - Il traffico originato usa sempre il TOCALL del progetto; non c'è un'impostazione per un indirizzo di destinazione di rete alternativa.

Formati di ora e posizione (cap. 6)
===================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Latitudine e longitudine non compresse
     - ✅
     - Trasmesse e analizzate nella forma ``DDMM.mmN`` / ``DDDMM.mmW``, con un formattatore condiviso perché ogni servizio che origina traffico produca coordinate identiche.
   * - Marca temporale zulu giorno/ora/minuto
     - ✅
     - È l'unica forma che questa stazione origina, su posizioni, oggetti, rapporti di stato e meteo. L'orologio funziona sempre in UTC, quindi non interviene alcuna conversione di fuso.
   * - Marche temporali locale giorno/ora/minuto e ora/minuto/secondo in ricezione
     - ✅
     - Vengono lette tutte e quattro le forme: le due forme zulu da 7 byte, quella storica in ora locale e la marca mese/giorno/ora/minuto di un rapporto meteo senza posizione. Le forme zulu sono risolte in UTC assoluto rispetto all'orologio, tornando indietro di un giorno, di un mese o di un anno quando il valore cadrebbe nel futuro, e la forma locale è riportata esattamente come l'ha scritta il mittente, perché il pacchetto non nomina il fuso in cui si trova. Il valore è mostrato nella colonna DECODIFICATO della tabella del traffico, che è ciò che distingue un pacchetto ritrasmesso con minuti di latenza da uno appena sentito.
   * - Ambiguità di posizione
     - ✅
     - Da una a quattro cifre in bianco in trasmissione, a livello di stazione. In ricezione i minuti in bianco sono analizzati cifra per cifra e risolti al centro del riquadro di ambiguità, così una posizione grossolana si filtra comunque per distanza in modo ragionevole invece di collassare verso il bordo del grado.
   * - Altitudine
     - ✅
     - La forma ``/A=`` a sei cifre nei commenti, per ruolo di stazione, più la forma base-91 dentro il Mic-E.
   * - ``!DAO!`` ad alta precisione e opzione datum
     - ✅
     - Trasmesso su posizioni non compresse e dentro il campo di testo Mic-E, soppresso quando è in uso l'ambiguità di posizione o è selezionato il formato compresso, perché entrambi dichiarano già una precisione diversa. Un token ricevuto viene applicato nel verso opposto, in entrambe le sue forme d'aria — le cifre leggibili e la forma base-91 che emette la maggior parte dei tracker — così una posizione non compressa in arrivo viene raffinata fino a circa 18 m prima che il filtro di distanza la misuri. Un rapporto compresso viene lasciato com'è, perché i suoi campi base-91 portano già quella precisione.
   * - Rapporti di posizione NMEA grezzi (``$``)
     - ✅
     - Le frasi ``RMC``, ``GGA`` e ``GLL`` sono decodificate in ricezione, con qualsiasi identificatore di talker di due lettere, così da coprire sia i ricevitori multicostellazione sia quelli solo GPS. Il checksum opzionale è verificato quando presente, e una frase che dichiara un fix non valido viene rifiutata, così il filtro di distanza dell'IGate non valuta mai una stazione su una coordinata vecchia. ``$GPWPL`` nomina un waypoint e non il fix proprio del mittente, ed è lasciato deliberatamente non decodificato; ``$ULTW`` è un record meteorologico ed è instradato come tale.
   * - Posizione nulla predefinita
     - ❌
     - Le coordinate configurate vengono sempre trasmesse così come sono; non c'è convenzione per segnalare "posizione sconosciuta" quando l'operatore non le ha impostate.

Estensioni dati (cap. 7)
========================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Rotta e velocità
     - ✅
     - Nello slot da 7 byte non compresso, nella forma compressa a due byte e nel Mic-E.
   * - Direzione e velocità del vento
     - ✅
     - Occupa lo stesso slot da 7 byte nei rapporti meteo con posizione, con segnaposto quando nessun sensore è mappato.
   * - Potenza / altezza / guadagno / direttività (PHG)
     - ✅
     - Costruito da watt, piedi, dBi e una direzione nella pagina Station e rispecchiato nei beacon per ruolo e negli oggetti.
   * - Sonde PHGR (carattere di cadenza del beacon)
     - ✅
     - La forma a nove byte della 1.2 ("PHGphgd" più un carattere di cadenza in beacon all'ora e la barra finale obbligatoria) viene trasmessa ogni volta che l'intervallo proprio del beacon IGate è noto, il che avviene sempre, ed è analizzata in ricezione: il carattere di cadenza e la barra vengono riconosciuti e rimossi, così il commento che segue viene letto correttamente invece di iniziare con una barra vagante.
   * - Portata radio precalcolata (RNG)
     - ✅
     - Selezionabile come estensione dati per qualsiasi ruolo di beacon, in miglia terrestri.
   * - Intensità di segnale omni-DF (DFS)
     - ✅
     - Selezionabile come estensione dati, con l'intensità in punti S più gli stessi codici di altezza, guadagno e direttività usati da PHG.
   * - Rilevamento e numero/portata/qualità (BRG/NRQ)
     - ✅
     - Disponibile su oggetti e item, nella forma ``000/000`` richiesta dalla specifica.
   * - Estensioni dati in ricezione
     - ✅
     - Lo slot da 7 byte di un rapporto non compresso in arrivo viene analizzato invece di essere letto come i primi sette caratteri del commento: ``PHGphgd`` e la sua forma PHGR da nove byte, ``RNGrrrr``, ``DFSshgd`` e ``CSE/SPD``, riportata come direzione e velocità del vento quando il simbolo è una stazione meteo. Il commento viene poi preso dal primo byte successivo al token trovato, così la forma da nove byte non lascia più un carattere di frequenza e una barra spaiati in testa.
   * - Descrittore di oggetto area
     - ✅
     - Codifica completa di forma, colore e dimensione, inclusa la regola che sostituisce la barra con una cifra per valori di colore da dieci in su.

Formati dei rapporti di posizione e DF (cap. 8)
===============================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Tutti e quattro gli identificatori di tipo dato di posizione
     - ✅
     - In trasmissione e ricezione. La scelta fra gli identificatori con e senza capacità di messaggistica segue l'impostazione reale di messaggistica della stazione, non il fatto che ci sia o meno una marca temporale.
   * - Campo commento
     - ✅
     - Presente in ogni posizione originata, con il blocco di frequenza, ``!DAO!`` e la telemetria nel commento che riservano i loro byte prima che il testo libero riempia il campo, così un commento lungo viene troncato invece di far cadere un'estensione.
   * - Formato del rapporto DF
     - ⚠️
     - I campi di rilevamento e NRQ sono prodotti su oggetti e item, che copre il riportare un rilevamento su un'altra stazione. Non esiste un ruolo di rapporto DF dedicato per il beacon della stazione stessa.

Rapporti di posizione compressi (cap. 9)
========================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Latitudine e longitudine compresse in base-91
     - ✅
     - Selezionabile per servizio — tracker, IGate, digipeater e oggetti — e decodificata in ricezione. La compressione è soppressa automaticamente quando sono in uso un'estensione PHG o DF o l'ambiguità di posizione, perché nessuna di queste sopravvive al formato compresso; la portata radio precalcolata è l'unica estensione che invece lo fa, e viene ripiegata nel campo a due byte.
   * - Rotta/velocità compresse e il byte di tipo di compressione
     - ✅
     - Un tracker in movimento codifica rotta e velocità nel campo a due byte e imposta il byte di tipo su rotta/velocità compresse con fix corrente; una stazione senza nulla da riportare invia la codifica a tre spazi di "nessun dato". Il campo quantizza la rotta a passi di 4 gradi e la velocità a passi di circa l'8 per cento, e il codificatore mantiene entrambi i byte nell'intervallo che appartiene alla forma rotta/velocità, quindi un token non viene mai letto come la forma di portata radio che condivide quei due byte. In ricezione i tre byte sono riletti allo stesso modo: il byte di tipo decide fra altitudine, portata radio e rotta/velocità, così una stazione in movimento mostra la sua rotta e la sua velocità invece di una coordinata nuda.
   * - Portata radio precalcolata compressa
     - ✅
     - Una baliza la cui estensione dati è la portata radio precalcolata la ripiega nel campo a due byte — il marcatore riservato ``{`` seguito dalla cifra di portata — invece di ricadere sul formato non compresso, così il cerchio di copertura viaggia con una posizione compressa e nel campo informativo non resta alcun token ``RNGrrrr``. Il campo quantizza la portata a passi di circa l'8 per cento, da un minimo di 2 miglia.
   * - Altitudine compressa
     - ✅
     - Un beacon compresso che porta un'altitudine la mette negli stessi due byte, con il byte di tipo che dichiara GGA come sorgente, che è ciò che seleziona quella lettura. Quando lo fa, il token ``/A=`` del commento viene omesso, così l'altitudine è dichiarata una volta sola e non costa nulla invece di nove byte. Se è configurato anche un raggio radio, è il raggio a tenersi i due byte: non ha altro posto dove andare, e l'altitudine ha ancora la forma nel commento. Il passo è di circa lo 0,2 %.

Formato dati Mic-E (cap. 10)
============================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Mic-E in trasmissione e ricezione
     - ✅
     - Codificatore e decodificatore completi, che coprono la metà nell'indirizzo di destinazione, la longitudine, la velocità e la rotta, e entrambi gli identificatori di tipo dato, l'attuale e il vecchio.
   * - Codice di tipo e identificatore del costruttore
     - ✅
     - Il byte di tipo segue il byte della tabella simboli e riflette la capacità di messaggistica della stazione; la coppia costruttore/versione chiude il campo di testo. Senza di essi un beacon Mic-E è anonimo per ogni client, perché l'indirizzo di destinazione porta la posizione e non può anche identificare il firmware.
   * - Altitudine, blocco di frequenza e ``!DAO!`` nel campo di testo Mic-E
     - ✅
     - L'altitudine apre il campo di testo e sposta il commento invece di sostituirlo, e il blocco di frequenza e l'estensione datum sono emessi nell'ordine canonico mostrato dagli esempi della specifica stessa.
   * - Codici di commento di posizione (Off Duty, En Route … Emergency)
     - ✅
     - Il ricevitore decodifica tutti e quindici i valori, incluso l'insieme personalizzato e lo schema Emergency di tutti zeri, e riporta correttamente come indefinito uno schema misto standard/personalizzato. La pagina Tracker consente di scegliere per la trasmissione uno qualsiasi dei quattordici valori standard e personalizzati. Emergency è deliberatamente assente da quell'elenco: chiede ad altri operatori di rispondere a un'emergenza reale, cosa che una pagina di configurazione non dovrebbe poter attivare con un clic sbagliato e lasciare attiva per tutti i beacon successivi.
   * - Indicazione di emergenza
     - ✅
     - Un'emergenza Mic-E ricevuta via radio o da APRS-IS produce una riga di log di livello warning e una propria voce nel registro del traffico, accanto al pacchetto che l'ha trasportata. Gli altri quattordici commenti di posizione sono registrati a livello informativo, perché il valore vive nell'indirizzo di destinazione ed è altrimenti invisibile nel testo del pacchetto. Le forme tra parentesi esclamative nel campo commento che una stazione priva di Mic-E usa per la stessa segnalazione (``aprs.org/aprs12/EmergencyCode.txt``) vengono riconosciute allo stesso modo: ``!EMERGENCY!`` all'inizio del commento di una posizione, oggetto o item - dopo l'estensione dati PHG/DFS/RNG/CSE-SPD quando presente, che è dove la proposta la colloca - produce lo stesso warning e la stessa riga nel registro del traffico, e le altre tredici forme tra parentesi esclamative (``!TESTALARM!``, ``!PRIORITY!``, ``!WXALARM!`` e il resto di quell'insieme proposto) sono registrate a livello informativo come i loro equivalenti Mic-E.
   * - Velocità oltre i 670 nodi
     - ✅
     - L'estensione 1.2 è applicata in entrambi i versi, quindi una trama digipetata da una stazione spaziale riporta la propria velocità orbitale invece di una troncata. Quella scala è quantizzata a passi di 112 nodi e ha un vuoto fra 671 e 781 nodi che la regola pubblicata stessa lascia senza rappresentazione; sotto i 671 nodi il campo resta esatto al nodo.
   * - PHG dentro il campo di testo Mic-E
     - ✅
     - L'estensione dati viene scritta dentro il testo Mic-E, dopo il blocco di frequenza e prima del commento dell'operatore, così una stazione che trasmette beacon in Mic-E annuncia la propria copertura come consente l'aggiunta 1.2. L'interruttore è nella pagina Tracker; i suoi quattro sottocampi sono i dati d'antenna della stazione stessa.
   * - Telemetria Mic-E
     - ❌
     - Deliberatamente assente: la versione 1.2 depreca questo formato a favore dei codici di tipo del costruttore e della telemetria base-91 nel commento, entrambi implementati da questo firmware.

Rapporti di oggetto e item (cap. 11)
====================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Rapporti di oggetto
     - ✅
     - Cinque slot configurabili, nomi riempiti a nove caratteri, stati vivo e annullato, posizione compressa o non compressa, con l'intero insieme in trasmissione e ricezione.
   * - Rapporti di item
     - ✅
     - Nomi variabili da tre a nove caratteri con i marcatori di vivo e annullato. Va notato che la specifica segna il formato item come non raccomandato in RF; gli oggetti sono la scelta migliore per qualcosa di lunga durata.
   * - Annullare un oggetto o item
     - ✅
     - Un oggetto annullato viene ritrasmesso alcune volte come rapporto di annullamento prima che il suo flag di abilitazione venga azzerato, così i riceventi vedono davvero il ritiro invece di lasciare che l'oggetto invecchi nelle loro liste.
   * - Oggetti permanenti
     - ✅
     - Viene emessa la pseudo-marca temporale fissa di tutti uno per un oggetto contrassegnato come permanente, che è ciò che serve a un oggetto fisso di ripetitore o punto di riferimento.
   * - Oggetti di area
     - ✅
     - Forma, colore, spessore della linea e dimensione, sugli stessi slot di oggetto.
   * - Oggetti e item segnaletici
     - ✅
     - Fino a tre caratteri di testo del cartello fra graffe, sul simbolo apposito.
   * - Instradamento proporzionale per gli oggetti
     - ✅
     - Un preset di percorso per trasmissione, ruotando fra loro, così un oggetto di lunga durata non inonda ogni salto a ogni ciclo. Ogni preset è contato in salti ed escluso dalla rotazione se supera il limite AX.25.

Rapporti meteorologici (cap. 12)
================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Rapporto meteo completo, con e senza marca temporale
     - ✅
     - La forma raccomandata, con il simbolo meteo, l'estensione dati del vento e il blocco completo dei dati meteo. L'identificatore di tipo con capacità di messaggistica segue l'impostazione di messaggistica della stazione, come per le posizioni ordinarie.
   * - Rapporto meteo come oggetto
     - ✅
     - Un oggetto meteo con nome in una posizione diversa da quella della stazione.
   * - Rapporto meteo senza posizione
     - ⚠️
     - Emesso quando la stazione non ha coordinate configurate, con la marca temporale mese/giorno/ora/minuto richiesta dal formato. La neve è omessa da questa forma di proposito, perché la lettera che userebbe è già la velocità del vento lì; il firmware registra a log quando questo scarta una lettura configurata. La specifica segna questo formato come non raccomandato.
   * - Campi meteo obbligatori
     - ✅
     - Direzione e velocità del vento, raffica e temperatura sono sempre emesse, ripiegando sui segnaposto a punti quando nessun sensore è mappato, così il rapporto resta una trama meteo valida e non troncata.
   * - Pioggia, umidità e pressione barometrica
     - ✅
     - Pioggia nell'ultima ora, nelle ultime 24 ore e da mezzanotte; umidità con la codifica a due zeri per il 100 %; pressione in decimi di millibar nel campo completo a sei caratteri confermato dalla revisione 1.2.
   * - Luminosità e nevicata
     - ✅
     - La luminosità usa la lettera maiuscola sotto i 1000 W/m² e quella minuscola sopra; la nevicata usa la forma frazionaria sotto i dieci pollici e quella intera con zeri sopra.
   * - Idrometro / altezza di piena
     - ✅
     - Entrambe le forme, in piedi e in metri, della proposta idrometro della 1.2, con risoluzione al decimo e senza riempimento, come mostra l'esempio di quel documento.
   * - Identificatori di tipo software e di unità meteo
     - ⚠️
     - Entrambi sono emessi, come ultimo token del campo informazioni perché un parser rigido non assorba il commento dell'operatore nella stringa dell'unità. Il codice di unità è libero e va bene; il singolo carattere di tipo software è uno che la specifica non assegna.
   * - Contatore di pioggia grezzo
     - ✅
     - Il conteggio corrente del pluviometro stesso, quattro cifre dopo un ``#``, trasmesso senza scalatura e mai azzerato dalla stazione, così un ricevitore può sottrarre due rapporti. È una riga della tabella di mappatura dei sensori come ogni altro campo meteo, emessa solo quando un driver è mappato su di essa.
   * - Campi di radiazione e tensione
     - ❌
     - I due campi proposti per la 1.2 non hanno un token meteo proprio e viaggiano come canali analogici di telemetria, che è dove li instrada il framework dei sensori.
   * - Rapporti meteo grezzi Peet Bros e Ultimeter
     - ⚠️
     - Riconosciuti come meteo dal classificatore di gateway, quindi vengono instradati correttamente, ma i payload grezzi non sono mai decodificati in letture. La specifica dice comunque che chi trasmette dovrebbe convertire al formato completo, quindi si tratta di ampiezza in ricezione e non di una lacuna in trasmissione.
   * - Dati di tempesta
     - ❌
     - Il modello dati per i rapporti di ciclone tropicale esiste negli header, ma nulla codifica o decodifica la forma in onda. Una stazione di questo tipo non ha da dove ricavare quell'informazione — viene dai servizi meteo — quindi ritrasmettere tali pacchetti intatti, come accade oggi, è il comportamento sensato.

Dati di telemetria (cap. 13)
============================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Rapporto di telemetria
     - ✅
     - Numero di sequenza, cinque canali analogici e il banco digitale a otto bit, con i canali mappati per nome ai driver dei sensori locali perché abilitare o disabilitare un driver non li ripunti silenziosamente.
   * - Intervallo analogico esteso 000-999
     - ✅
     - Il campo a tre cifre accetta l'intero intervallo 1.2 invece della finestra originale 000-255, e le letture fuori intervallo sono limitate invece che avvolte. Prima si applicano un minimo e un massimo grezzi per canale, così l'operatore può tenere i valori dentro la finestra che il suo software ricevente comprende.
   * - Definizioni dei parametri in onda
     - ✅
     - Tutti e quattro i messaggi di definizione — nomi, unità ed etichette, coefficienti dell'equazione e senso dei bit con titolo del progetto — inviati come messaggi indirizzati, come richiede la specifica. Il rapporto porta il valore grezzo e il ricevitore applica i coefficienti.
   * - Telemetria base-91 nel commento
     - ✅
     - Il contatore di sequenza, i canali analogici e il banco digitale a otto bit sono codificati nel gruppo delimitato da barre verticali. Poiché quel gruppo è posizionale — l'n-esima coppia *è* il canale n, senza identificatore per coppia — il codificatore si ferma al primo canale disabilitato o non risolto invece di lasciare un vuoto che sposterebbe di un posto ogni canale successivo, e la coppia digitale viene aggiunta solo dietro un insieme completo di cinque coppie analogiche, l'unico punto in cui la specifica la consente. Una stazione senza alcun canale da riportare non emette alcun gruppo, poiché l'estensione deve portare il contatore e almeno un canale.
   * - Forma alternativa del numero di sequenza
     - ❌
     - L'identificatore di sequenza a tre lettere che alcuni codificatori usano al posto di un numero non è prodotto né riconosciuto in modo speciale in ricezione.

Messaggi, bollettini e annunci (cap. 14)
========================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Messaggi di testo con conferma e rifiuto
     - ✅
     - Campo destinatario a nove caratteri, corrispondenza di conferma e rifiuto limitata a uno-cinque caratteri alfanumerici come consente la specifica, e un timer di ritentativo per i messaggi in uscita non confermati.
   * - Reply-ACK
     - ✅
     - Tutte e sette le regole dell'algoritmo, incluso costruire il suffisso nell'istante della trasmissione e tenere una piccola tabella per stazione delle conferme dovute. È ciò che fa scorrere uno scambio di messaggi a velocità di conversazione invece di due pacchetti per turno.
   * - Formato del numero di messaggio
     - ⚠️
     - I numeri in ingresso di qualsiasi lunghezza legale vengono riconosciuti. I messaggi in uscita si numerano con due cifre, con ritorno a capo a 99, così un suffisso Reply-ACK rientra ancora nei cinque caratteri che la specifica consente per l'intero identificatore.
   * - Gruppi di messaggi
     - ✅
     - I messaggi indirizzati ai nomi di gruppo integrati ``ALL``, ``QST`` e ``CQ``, o a uno qualsiasi dei nomi di gruppo definiti dall'operatore, vengono confrontati senza distinguere maiuscole/minuscole insieme al nominativo proprio della stazione, con qualsiasi SSID. Un messaggio di gruppo resta distinto da un messaggio diretto con lo stesso testo e viene mostrato ma mai confermato, poiché un gruppo non ha un unico proprietario che risponda per esso.
   * - OBJECT-in-MSG e ITEM-in-MSG
     - ❌
     - Le due proposte 1.2 che portano un report completo di oggetto o item dentro il corpo di un messaggio, pensate per una stazione che non può digiripetere il pacchetto oggetto/item ordinario, non sono riconosciute come classe a sé. Questa stazione non ha una mappa su cui tracciarne uno e non origina né necessita di quell'espediente; un messaggio che usa una delle due forme arriva comunque all'operatore come testo semplice.
   * - Codifica del testo UTF-8
     - ✅
     - I campi messaggio e gli altri campi di testo libero sono trasparenti a 8 bit da un capo all'altro: nulla qui ricodifica o rifiuta un byte non ASCII, il che è la raccomandazione della specifica stessa (``aprs.org/aprs12/utf-8.txt``). Ogni troncamento dettato dalla lunghezza di un campo di testo libero - il percorso dei messaggi in uscita, il testo di stato e commento, e il testo di oggetti/elementi e bollettini, sia inserito nelle pagine di amministrazione web sia caricato dalla configurazione memorizzata - cade su un confine di carattere invece che a metà di uno.
   * - Bollettini generali, annunci e bollettini di gruppo
     - ✅
     - Cinque slot configurabili con le forme di destinatario corrette per tutti e tre: identificatore numerico per i bollettini, identificatore a lettera per gli annunci e suffisso col nome del gruppo per i bollettini di gruppo. Ogni slot ha la propria scadenza.
   * - Cadenza di trasmissione dei bollettini
     - ⚠️
     - I bollettini escono a intervallo fisso con un tempo di scadenza. La specifica raccomanda invece una cadenza decrescente — frequente all'inizio e poi diradata nell'arco di ore — che carica meno un canale condiviso a parità di effetto.
   * - Bollettini del servizio meteorologico nazionale
     - ❌
     - I bollettini indirizzati ai prefissi del servizio meteorologico sono gestiti come messaggi ordinari invece di essere riconosciuti come classe a sé. Vengono ritrasmessi correttamente e mai confermati, perché il destinatario non è questa stazione, quindi l'effetto pratico si limita a come vengono etichettati nell'interfaccia.
   * - Radiogrammi NTS
     - ❌
     - La convenzione dei prefissi di riga non viene analizzata. La specifica dice esplicitamente che un'applicazione non deve necessariamente comprenderla, perché le righe sono messaggi ordinari e si leggono correttamente come testo semplice, che è ciò che accade qui.

Capacità di stazione, interrogazioni e risposte (cap. 15)
=========================================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Interrogazioni generali
     - ✅
     - L'interrogazione a tutte le stazioni, inclusa l'impronta opzionale di latitudine, longitudine e raggio, più le interrogazioni broadcast di meteo, IGate e QRU. Un'interrogazione che arriva da internet riceve risposta su internet e una che arriva in RF riceve risposta in RF, così un'interrogazione da internet non può mai eccitare il trasmettitore.
   * - Interrogazioni dirette a una stazione
     - ✅
     - L'insieme completo: posizione, stato, stazioni udite in diretta, cronologia di una stazione udita, messaggi in sospeso, oggetti e traccia del percorso, incluso l'alias ping per l'interrogazione di traccia. Le risposte sono costruite dallo scheduler dei beacon e non dal task di ricezione, così una raffica di interrogazioni non può far traboccare uno stack di ricezione.
   * - Cronologia delle stazioni udite
     - ✅
     - L'istogramma di otto ore richiesto dalla specifica, tenuto per stazione e trasportato con essa quando la sua riga si sposta in cima alla tabella.
   * - Pacchetto delle capacità di stazione
     - ⚠️
     - Inviato in risposta all'interrogazione IGate con il token di gateway e i contatori di messaggi e di stazioni locali, dove i contatori significano ciò che dice la specifica e non totali grezzi di trame. Il modello delle capacità è aperto, quindi la stazione potrebbe anche annunciare i suoi ruoli di digipeater, meteo e telemetria; non lo fa.

Rapporti di stato (cap. 16)
===========================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Rapporto di stato con e senza marca temporale
     - ✅
     - Testo di stato per ruolo con marca temporale zulu opzionale.
   * - Budget di lunghezza del testo di stato
     - ✅
     - Il limite di 62 caratteri è applicato scartando i blocchi opzionali in un ordine definito — prima il locatore, poi il blocco di frequenza — e il testo dell'operatore non viene mai toccato. Se ancora non entra, il rapporto viene rifiutato invece di essere troncato in una trama malformata.
   * - Rapporto di stato con locatore Maidenhead
     - ✅
     - Il locatore a quattro o sei caratteri e il suo simbolo vengono prodotti subito dopo l'identificatore di tipo dato, prima del blocco di frequenza. Non viene mai combinato con una marca temporale: se entrambi sono abilitati, il locatore ha la precedenza e la marca temporale viene omessa.
   * - Direzione d'antenna e potenza irradiata efficace
     - ✅
     - I due caratteri chiudono il testo di stato dopo un ``^``, a partire da una direzione e da una potenza valide per l'intera stazione e impostate nella pagina Station. La direzione avanza di dieci gradi e la potenza è portata alla voce più vicina della tabella della specifica, che va da 10 a 7290 watt. Servono entrambe le metà perché il blocco compaia, ed è l'unico blocco che il budget di lunghezza non scarta mai: una stazione che lavora in meteor scatter manda il rapporto proprio per quei tre byte.

Tunneling di rete e traffico di terze parti (cap. 17)
=====================================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Trame di terze parti
     - ✅
     - Costruite quando si ritrasmette traffico da internet verso RF e scartate in ingresso, usando il nominativo della stazione interna per la soppressione dei duplicati e Last Heard, non quello del gateway. Una trama che non entra viene rifiutata invece che troncata.
   * - Token di percorso che vietano il gateway
     - ✅
     - Sono rispettati tutti i classici token di divieto di gateway e i q-construct di sola internet, e solo nell'intestazione dove hanno senso: un pacchetto il cui commento contenga per caso una di quelle parole non viene scambiato per uno instradato con essa.
   * - Marcatore di non archiviazione
     - ✅
     - La stringa ``!x!`` che chiede ai database dietro APRS-IS di non memorizzare un pacchetto può essere anteposta a ogni campo di testo libero della stazione - commenti di posizione, testi di stato, il commento meteorologico, i commenti di oggetti e item e il testo dei bollettini - con un'unica casella a livello di stazione (pagina Station), disattivata per impostazione predefinita. Il testo dei messaggi, i pacchetti di definizione della telemetria e le risposte alle interrogazioni restano fuori dalla sua portata per scelta progettuale. Un campo già marcato dall'operatore non viene marcato due volte. Si rivolge agli archivi, non ai gateway, quindi non decide mai dove possa viaggiare una trama né se raggiunga RF/APRS-IS; i pacchetti ritrasmessi non sono influenzati da questa opzione e conservano il marcatore che la stazione di origine vi aveva già messo, se presente, perché il payload viene passato byte per byte.
   * - Rapporto del percorso IGate→RF
     - ❌
     - L'involucro sperimentale ``{IP-`` che permetterebbe a questa stazione di annunciare via APRS-IS il percorso AX.25 usato per far passare un pacchetto su RF non viene generato. A differenza della maggior parte delle altre proposte 1.2 di questa tabella, la stazione rientra proprio nell'ambito di questa: è un IGate bidirezionale e trasmette effettivamente verso RF. Resta una proposta, non un'aggiunta ratificata, ha avuto una diffusione modesta fra gli IGate in generale, e aggiungerebbe una seconda trasmissione per ogni pacchetto passato a RF, a costo di un tempo di canale che questo design volutamente leggero non spende altrove. Da riconsiderare se la proposta venisse ratificata o se un operatore la richiedesse.

Specifica di frequenza (cap. 18)
================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Blocco di frequenza in posizioni, oggetti e stato
     - ✅
     - Vengono prodotte tutte e tre le forme fisse da dieci byte: quella a dieci kilohertz sotto i 100 MHz, la forma comune in VHF e UHF e quella con prefisso a lettera per le bande a microonde; si lavora in kilohertz interi perché nessuna cifra derivi, e si rifiuta di emettere qualsiasi cosa non misuri esattamente dieci byte.
   * - Tono, scostamento e portata
     - ✅
     - Il tono CTCSS a tre cifre, lo scostamento in unità di dieci kilohertz e la portata di copertura a due cifre in miglia o chilometri, nell'ordine definito dalla specifica di frequenza. La portata è un sottocampo di Oggetti/Elementi (la copertura che un ripetitore fisso annuncia di sé), non un'impostazione per servizio di tracker, IGate o ripetitore digitale.
   * - Tono a banda stretta, codice DCS e frequenza TX/RX separata
     - ❌
     - Altri tre sottocampi opzionali definiti dalla specifica non vengono costruiti: l'indicatore di modulazione a banda stretta (minuscolo), il codice DCS che può sostituire il tono CTCSS, e la forma con frequenza di trasmissione/ricezione separata. Nessuno dei tre è un difetto nel blocco che questa stazione trasmette, che resta valido e auto-sintonizzabile anche senza di essi - sono lacune di capacità, tralasciate perché il firmware non ha un'impostazione radio a banda stretta/DCS da riportare e objitem_t modella un'unica frequenza di monitoraggio invece di TX/RX indipendenti.
   * - Richieste di frequenza e QSY nei messaggi
     - ❌
     - Non vengono generate né gestite le forme di messaggio proposte che richiedono o comandano un cambio di frequenza operativa. Sono proposte 1.2 con scarsa diffusione sul campo.

Formati definiti dall'utente e altri tipi di pacchetto (cap. 19-20)
===================================================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Formato dati definito dall'utente
     - ❌
     - Lo spazio di estensione privata che la specifica riserva agli sperimentatori è inutilizzato. Sarebbe la sede corretta per qualsiasi diagnostica specifica del firmware che il progetto volesse mettere in onda, invece di sovraccaricare il testo di stato.
   * - Pacchetti di dati non validi e di prova
     - ❌
     - L'identificatore dei dati di prova non è riconosciuto come classe a sé, quindi tali pacchetti finiscono nel percorso generico. Non dovrebbero mai essere inoltrati a internet.
   * - Formato di radiogoniometria Agrelo
     - ❌
     - Il formato di rilevamento e qualità del radiogoniometro autonomo non viene decodificato. Restano pochissime unità in servizio.
   * - Beacon con locatore Maidenhead
     - ❌
     - L'identificatore del beacon di locatore autonomo non viene decodificato. La specifica stessa lo segna come obsoleto; il locatore ora vive nei rapporti di stato.
   * - Identificatori riservati (elemento di mappa, dati di rifugio, meteo spaziale)
     - ❌
     - Riservati dalla specifica e mai definiti ulteriormente, quindi non c'è nulla da implementare.

Nessuno dei cinque identificatori qui sopra porta un payload che questo
firmware decodifichi, ma tutti e cinque sono classificati per l'instradamento
sotto un unico bit di filtro condiviso, la casella "Altri" della pagina IGate
Filter, così un pacchetto di una qualsiasi di quelle classi può essere inoltrato
ad APRS-IS invece di essere scartato qualunque cosa spunti l'operatore. Il
traffico di terze parti e i dati di test restano non classificati di proposito e
non vengono mai ritrasmessi: re-instradare il traffico di terze parti è il modo
in cui nascono i loop di IGate, e i dati di test non sono pensati per lasciare
il canale su cui sono stati inviati.

Simboli (cap. 21)
=================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Tabelle dei simboli primaria e alternativa
     - ✅
     - Un selettore visuale nell'amministrazione web copre entrambe le tabelle, con un simbolo per ruolo per tracker, IGate, digipeater, stazione meteo e ogni oggetto.
   * - Caratteri di sovrapposizione
     - ✅
     - Un carattere di sovrapposizione può essere collocato nella posizione della tabella per i simboli che lo accettano, ed è così che un digipeater annuncia la propria politica di instradamento sulla mappa. Sono accettate sovrapposizioni alfabetiche e numeriche, e una numerica viene emessa in un rapporto compresso come la lettera minuscola ``a``-``j`` che quel formato richiede, perché un campo di posizione compresso non può mai iniziare con una cifra.
   * - Precedenza dei simboli
     - ⚠️
     - Viene letto solo il simbolo del campo informazioni, quindi la questione della precedenza non si pone in pratica; ma significa anche che le sorgenti di ripiego descritte dalla regola non vengono mai consultate per un pacchetto che lì non porti simbolo.
   * - Convenzioni SSID
     - ✅
     - Gli SSID di ruolo raccomandati sono i valori di fabbrica di ogni servizio, e ogni campo SSID è verificato per intervallo sia nel modulo sia nel caricatore di configurazione.

Digipeating e il paradigma New-N
================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Digipeating New-N tracciato
     - ✅
     - Una tabella di alias definita dall'operatore, con forma jolly, un massimo di salti per riga e una modalità traccia o riempimento per riga. Gli alias legacy che il paradigma New-N ha sostituito non sono onorati, di proposito.
   * - Ruolo di digipeater di riempimento (domestico)
     - ✅
     - Un singolo interruttore limita la stazione alle righe a salto singolo, che è la configurazione corretta per un digipeater domestico al servizio delle stazioni che non raggiungono quello ad ampio raggio.
   * - Trappola sul conteggio dei salti
     - ✅
     - Un conteggio di salti superiore al massimo della riga corrispondente viene limitato e ripetuto oppure scartato, a scelta dell'operatore, con lo scarto contato sotto la sua motivazione nel pannello.
   * - Soppressione dei duplicati
     - ✅
     - Una cache condivisa con profondità e finestra temporale configurabili, usata sia dal digipeater sia dal gateway, così una trama non può essere ripetuta da un percorso dopo che l'altro l'ha già vista.
   * - Digipeating preventivo
     - ✅
     - Spento per impostazione predefinita e selezionabile in due modalità indicatrici: gli indirizzi saltati vengono mantenuti e marcati come usati, oppure scartati così che esca solo ciò che resta da fare. La scansione va dal primo indirizzo inutilizzato fino alla fine del percorso e reclama solo un'identità fissa, quindi entrambe le esclusioni enunciate dalla proposta - gli alias n-N generici e un alias scritto con un conteggio di salti - sono garantite per costruzione.
   * - Instradamento legacy tramite SSID di destinazione
     - ✅
     - Disponibile dietro un interruttore spento per impostazione predefinita. Quando è spento, un pacchetto che usa quella convenzione ricade sulla logica del percorso esplicito invece di essere scartato.
   * - Segnalazione di precedenza e di operatore presente con i bit RR
     - ❌
     - Le proposte che riutilizzano i bit riservati dell'ottetto SSID non sono implementate in nessuna delle due direzioni. La modalità di marcatura del digipeating preventivo accende il bit riservato basso sugli indirizzi che salta, ma la trama viene ricodificata dalla sua rappresentazione TNC2, che non porta quei bit sul canale.

Gateway APRS-IS
===============

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Funzione APRS
     - Stato
     - Note
   * - Accesso e identificazione del software
     - ✅
     - La riga di accesso porta il nominativo, il passcode, il nome e la versione del software, e il filtro lato server solo quando ne è configurato uno: la parola chiave di filtro nuda senza argomento non viene mai inviata.
   * - Identità di accesso
     - ✅
     - La stazione accede con il proprio nominativo-SSID, la stessa identità che i suoi pacchetti portano come sorgente e la stessa che segue il costrutto q sulle trame instradate, scritta in un unico punto perché le tre non possano divergere. È l'indirizzo a cui va inviato un messaggio da APRS-IS, dato che il server confronta il destinatario con l'accesso in modo esatto.
   * - Percorso del traffico originato localmente
     - ✅
     - Tutto ciò che questa stazione immette da sé in APRS-IS porta ``TCPIP*`` come percorso completo e nessun alias di digipeater. Ogni originatore costruisce un pacchetto per tratta, così la trasmissione radio conserva il percorso di digipeater dell'operatore mentre quella internet dichiara che il pacchetto è stato immesso e non ripetuto.
   * - Gateway da RF a internet
     - ✅
     - Caselle di gateway per tipo, un filtro di distanza attorno alla stazione, filtri di prefisso di nominativo e lista nera, e un elenco di gateway satellitare per stazioni udite tramite digipeater spaziali.
   * - q-construct per stazione
     - ✅
     - Il gateway sceglie fra i due costrutti di ricezione per stazione sorgente e non con un flag globale, così una stazione a cui questo gateway non consegnerebbe un messaggio viene marcata come tale, che è esattamente ciò che il costrutto significa.
   * - Gateway da internet a RF
     - ✅
     - Una maschera configurabile decide quali categorie possono attraversare, e i messaggi devono inoltre soddisfare le tre condizioni stabilite dalla documentazione dei gateway, seguite dal pacchetto di posizione della stazione destinataria.
   * - Validazione del filtro lato server
     - ✅
     - La stringa di filtro viene controllata grammaticalmente prima dell'invio — forma del termine e tipo di argomento — mentre l'intervallo e la sensatezza dei valori sono lasciati al server, che è l'autorità.
   * - Commutazione fra server
     - ⚠️
     - Quattro slot di server con riconnessione automatica. La rotazione avviene su un fallimento nello stabilire la sessione; un server che accetta la connessione e poi la chiude viene ritentato invece che saltato, quindi un server in manutenzione può trattenere la stazione finché non si riprende.

Riepilogo
=========

Contando le righe precedenti: la stazione implementa per intero il nucleo di
posizione, oggetti, messaggi, interrogazioni e stato del protocollo, in
entrambe le direzioni, più i due ruoli di rete (digipeater e IGate) e le
aggiunte successive al 2000 che contano di più nel traffico quotidiano —
posizioni compresse, ``!DAO!``, Mic-E con identificazione dell'apparato, il
campo di frequenza, Reply-ACK, telemetria base-91 nel commento e il paradigma
New-N.

Le lacune si concentrano in tre punti, e vale la pena dirlo chiaramente:

* **Ampiezza in ricezione.** La stazione trasmette più formati di quanti ne
  decodifichi. I record meteorologici grezzi Peet Bros e Ultimeter sono
  riconosciuti come *categoria* — quanto basta per farli passare attraverso i
  filtri di gateway — ma il loro contenuto non viene mai analizzato. In un
  IGate questo si manifesta come stazioni che superano il filtro di tipo ma
  le cui misure non sono disponibili localmente.
* **Proposte successive al 2004.** Manca la proposta di segnalazione con i
  bit RR, mentre le sonde PHGR sono supportate. Sono aggiunte reali alla specifica, non folclore,
  ma la loro diffusione sul campo è disomogenea. Il digipeating preventivo, il più rilevante del gruppo,
  è implementato e spento per impostazione predefinita.
* **Formati senza sorgente locale.** I dati di tempesta, i bollettini NWS e i
  radiogrammi NTS sono, per una stazione di questo tipo, questioni di puro
  trasporto: il firmware non ha da dove ricavare quelle informazioni. Sono
  elencati per completezza, e il comportamento sensato — ritrasmetterli
  intatti — è già quello attuale.

Nessuna delle righe ❌ impedisce alla stazione di operare come IGate,
digipeater, tracker, stazione meteo o di telemetria pienamente conforme.
