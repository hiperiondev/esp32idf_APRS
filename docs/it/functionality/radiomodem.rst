.. _it-radiomodem:

==========
Radiomodem
==========

*Radiomodem* è la pagina dell'amministrazione web che collega il firmware alla
radio. È lì che si sceglie la modulazione in aria, si temporizza l'accesso al
canale, si limita il tempo di trasmissione proprio della stazione e si descrive
il rapporto elettrico fra i pin ADC/DAC dell'ESP32 e il ricetrasmettitore. Ogni
altra pagina decide *che cosa* viene trasmesso; questa decide *come*, *quando* e
*con quale livello*.

Questo capitolo documenta ogni controllo della pagina, uno alla volta: che cosa
è, che cosa cambia davvero dentro il firmware, come scegliere un valore, esempi
d'uso e gli errori che ciascuna impostazione può causare. Per il DSP e il
componente modem che ci sta dietro, si veda :ref:`it-modem` e
:ref:`it-dsp-signal-chain`.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Proprietà
     - Valore
   * - Voce di menu
     - **Radiomodem**
   * - Rotte
     - ``GET /radio``, ``POST /radio``, più ``POST /radio/looptest``,
       ``POST /radio/level`` e ``POST /radio/txtest`` per i tre pulsanti di
       diagnostica.
   * - Gestore
     - ``components/webconfig/pages/page_radio.c``
   * - Memorizzato in
     - ``/storage/radio.json`` (scaricabile e caricabile dalla pagina
       *Archiviazione*)
   * - Applicazione
     - Immediata al *Salva*, tranne **Abilita modem audio ADC/DAC** e
       **Frequenza di campionamento in trasmissione**, che richiedono un riavvio.

Perché alcuni campi richiedono un riavvio
==========================================

``page_radio_post()`` scrive l'intero modulo in ``g_config`` sotto un solo lock,
salva ``radio.json`` e poi chiama ``aprs_service_apply_modem_config()``, che
spinge le nuove impostazioni nel modem in funzione tramite
``modem_set_modem()``. Tutto ciò che il modem accetta mentre è in funzione ha
quindi effetto nell'istante in cui *Salva* ritorna: nessun riavvio, nessuna
trama persa, nessun riavvio del servizio.

Due impostazioni sono fuori da quel percorso:

* **Abilita modem audio ADC/DAC** — ``modem_init()`` viene eseguita esattamente
  una volta, da ``main.c``, e solo se questo interruttore era già attivo
  all'avvio. Attivarlo salva l'impostazione ma non porta su alcun hardware fino
  al riavvio successivo.
* **Frequenza di campionamento in trasmissione** — il periodo del clock di
  campionamento e ogni incremento di fase da esso derivato vengono programmati
  con l'hardware del modem fermo, quindi ``afskSetDacSampleRate()`` rifiuta di
  agire su un modem vivo e il valore viene applicato dal successivo
  ``modem_init()``.

In entrambi i casi il modulo continua a mostrare il valore salvato, che è quello
con cui partirà il prossimo avvio, non quello con cui il modem sta funzionando in
questo momento.

.. warning::

   Tre pulsanti di questa pagina — **TEST LOOP**, **LIVELLO RX** e **TEST TX** —
   salvano il modulo prima di essere eseguiti, esattamente come se fosse stato
   premuto *Salva*. Non usateli per "provare" un'impostazione che non intendete
   conservare. Due dei tre, inoltre, mandano la radio in trasmissione.

Protocollo
==========

FX.25 (AX.25 con correzione d'errore)
--------------------------------------

**Che cos'è.** FX.25 avvolge una normale trama AX.25 in un blocco
Reed–Solomon preceduto da un tag di correlazione. Un ricevitore che conosce
FX.25 può riparare errori di bit che altrimenti farebbero fallire il CRC della
trama; un ricevitore che non lo conosce trova comunque all'interno la semplice
trama AX.25 e la decodifica normalmente. È un'estensione compatibile, non un
protocollo diverso.

**Che cosa fa qui il menu a tendina.** Un selettore a tre opzioni memorizza
``fx25Mode = 0``, ``1`` o ``2``, che ``Ax25Init()`` interpreta direttamente:

* *Disattivato* (``0``, valore predefinito) — AX.25 semplice in entrambe le
  direzioni (``Ax25Config.fx25 = 0``, ``Ax25Config.fx25Tx = 0``).
* *Solo ricezione* (``1``) — *FX.25 in ricezione, AX.25 semplice in
  trasmissione* (``Ax25Config.fx25 = 1``, ``Ax25Config.fx25Tx = 0``). Questo
  procura alla stazione una migliore decodifica delle stazioni FX.25 che
  ascolta, senza aggiungere ridondanza a ciò che trasmette.
* *Ricezione e trasmissione* (``2``) — FX.25 in entrambe le direzioni
  (``Ax25Config.fx25 = 1``, ``Ax25Config.fx25Tx = 1``). Questa stazione avvolge
  anche le proprie trame in uscita nel blocco Reed–Solomon, così ogni vicino
  che capisce FX.25 riceve da essa la stessa correzione d'errore.

**Secondo effetto, facile da non notare.** Con l'ingresso audio piatto
selezionato (più sotto), FX.25 cambia anche quale prefiltro esegue il primo
demodulatore a 1200 Bd: senza FX.25 applica la deenfasi per annullare la
preenfasi della stazione trasmittente; con FX.25 esegue invece il solo passa
banda inverso, sul presupposto che la ridondanza del codice copra già la piccola
perdita di SNR. Commutare FX.25 altera quindi il comportamento in ricezione
anche su un canale dove nessuno trasmette FX.25.

**Quando abilitarlo.**

* *Quasi sempre, su un canale APRS normale.* Il costo è tempo di CPU nel
  ricevitore e nulla in aria.
* *Senz'altro*, su un percorso debole o rumoroso dove si sentono pacchetti
  parziali, se qualche vicino trasmette in FX.25.
* *Lasciatelo spento* mentre state inseguendo un problema di ricezione e volete
  la catena di segnale più semplice possibile, o quando state confrontando il
  comportamento della deenfasi con audio piatto.

.. warning::

   Abilitare FX.25 mentre si regola un percorso di ricezione marginale fa sì che
   i due demodulatori si comportino diversamente da un momento prima. Regolate
   prima l'audio con FX.25 spento, poi accendetelo e verificate che il tasso di
   decodifica sia migliorato e non il contrario.

Audio / AFSK
============

Abilita modem audio ADC/DAC
----------------------------

**Che cos'è.** L'interruttore principale del soft-modem integrato. Con esso
attivo, l'ESP32 stesso è il TNC: il SAR-ADC ascolta l'audio di ricezione della
radio, il DAC genera l'audio di trasmissione e un GPIO comanda il PTT. Con esso
spento, ``modem_init()`` non viene mai chiamata, non gira alcun task audio, non
viene configurato né ADC né DAC, e ``aprs_service_can_transmit()`` restituisce
falso: ogni trasmissione RF che il firmware tenta viene scartata all'origine e
conteggiata sotto ``DROP_MODEM_NOT_READY``.

**Usi.**

* *Attivo* — il caso normale: una stazione completa e autonoma (IGate,
  digipeater, tracker, meteo, telemetria) con una radio collegata.
* *Spento* — una stazione **solo internet**. Un IGate che inoltra solo APRS-IS
  a Telegram, un client Winlink di sola ricezione via internet, o un'unità da
  banco senza alcuna radio. Spegnere il modem libera i task audio e la loro
  quota di CPU, e rende impossibile mandare in trasmissione qualcosa per
  sbaglio.

.. warning::

   Questo è l'unico campo della pagina il cui cambiamento **non fa nulla** finché
   il dispositivo non viene riavviato. Spuntatelo, *Salva*, poi riavviate dalla
   pagina *Informazioni / Firmware*. I pulsanti TEST LOOP e TEST TX rilevano
   questo stato e lo dichiarano esplicitamente invece di fallire in modo oscuro.

Modulazione
-----------

**Che cos'è.** La modulazione in aria e la velocità in baud usate sia in
ricezione sia in trasmissione.

.. list-table::
   :header-rows: 1
   :widths: 8 26 10 22 34

   * - Valore
     - Profilo
     - Baud
     - Toni
     - Dove si usa
   * - 0
     - AFSK300
     - 300
     - 1600 / 1800 Hz
     - Packet in HF (30 m, 20 m). Abbastanza stretto per un canale SSB.
   * - 1
     - **Bell 202** (predefinito)
     - 1200
     - 1200 / 2200 Hz
     - **APRS standard in tutto il mondo** — 144.390 MHz in Nord America,
       144.800 MHz nella maggior parte d'Europa, 145.175 MHz in Australia, e
       così via.
   * - 2
     - ITU V.23
     - 1200
     - 1300 / 2100 Hz
     - Collegamenti packet europei di vecchia data. Non interoperabile con
       Bell 202.
   * - 3
     - G3RUH FSK
     - 9600
     - FSK diretta
     - Collegamenti ad alta velocità, attività satellitare. Richiede un percorso
       audio piatto in entrambe le direzioni.

**Che cosa cambia internamente.** ``ModemInit()`` ricostruisce l'intera catena
di demodulazione: coefficienti dei filtri, passo del PLL, soglie del DCD e
numero di demodulatori. Entrambi i profili a 1200 Bd eseguono **due
demodulatori in parallelo** con prefiltri diversi, così una trama che un
percorso perde può essere recuperata dall'altro; quelli a 300 Bd e 9600 Bd
eseguono un solo demodulatore.

**Esempi.**

* Un IGate domestico sulla frequenza APRS nazionale → **1200 Bd Bell 202**. Non
  cambiatelo. Qualunque altra cosa rende la stazione sorda e muta su quel canale.
* Un collegamento packet a 9600 Bd con un sito vicino su una frequenza simplex
  dedicata → **9600 Bd G3RUH**, con *Ingresso audio piatto / da discriminatore*
  spuntato e connessione alla porta dati a entrambi i capi.
* Un gateway HF su 10.147,6 MHz USB → **300 Bd AFSK**.

.. warning::

   La modulazione deve coincidere con quella di ogni stazione con cui intendete
   lavorare. Non c'è rilevamento automatico né ripiego: una discordanza
   significa che non si decodifica nulla in nessuna direzione, e le vostre
   trasmissioni verranno sentite come rumore da tutti gli altri sul canale. Se
   una stazione funzionante smette improvvisamente di decodificare, questo è il
   primo campo da controllare.

.. warning::

   9600 Bd G3RUH non passa attraverso le prese di microfono e altoparlante di
   una radio. La preenfasi, la deenfasi e la banda audio di un percorso vocale
   lo distruggono. Richiede una vera porta dati piatta (una connessione "packet
   9600" o da discriminatore) sia in trasmissione sia in ricezione.

Hardware audio (in fase di compilazione)
-----------------------------------------

Sotto il selettore di modulazione è mostrato un unico blocco di sola lettura. Non
è un'impostazione: è la definizione di scheda con cui il firmware è stato
compilato, così che una scelta di cablaggio possa essere distinta a colpo d'occhio
da un valore salvato. Riporta il pin di uscita del DAC (``MODEM_DAC_GPIO``, per
default GPIO25), il pin di ingresso dell'ADC (``MODEM_ADC_GPIO``, per default
GPIO33), il pin del PTT (``MODEM_PTT_GPIO``, ``Disabilitato`` quando vale -1),
PTT attivo alto (``MODEM_PTT_ACTIVE_HIGH``), l'attenuazione dell'ADC
(``MODEM_ADC_ATTEN``) e la frequenza di campionamento dell'ADC
(``MODEM_ADC_SAMPLERATE``) con cui il firmware è stato compilato. La frequenza
di campionamento in trasmissione non è elencata: è un'impostazione salvata,
scelta da *Frequenza di campionamento in trasmissione* più in basso in questa
stessa pagina.

Tutti i valori di compilazione provengono dal ``CMakeLists.txt`` di primo
livello e possono essere cambiati solo ricompilando il firmware — per esempio
``idf.py build -DMODEM_ADC_GPIO=32``. Il pin del PTT è registrato nella tabella
di proprietà dei GPIO dell'amministrazione, quindi compare come *usato — PTT* in
ogni altro selettore di GPIO della web (l'uscita di allarme messaggi, per
esempio), il che impedisce che due funzioni rivendichino lo stesso pin.

.. note::

   Il pin del DAC può essere solo GPIO25 (DAC1) o GPIO26 (DAC2); il DAC
   dell'ESP32 non è instradabile su altri pin, e la compilazione fallisce con un
   errore esplicito se viene indicato un altro numero.

Ingresso audio piatto / da discriminatore
------------------------------------------

**Che cos'è.** Una dichiarazione su da dove proviene l'audio di ricezione, non
un filtro da attivare a piacere. Dice al demodulatore se l'audio che riceve è
già stato deenfatizzato.

* **Spento** (predefinito) — l'audio proviene da una **presa di altoparlante o
  cuffia**. L'uscita audio di un ricevitore vocale è già deenfatizzata e
  limitata in banda. Il primo demodulatore applica quindi preenfasi e un passa
  banda normale.
* **Attivo** — l'audio proviene da una **porta dati o direttamente dal
  discriminatore**. Quel segnale è piatto e non filtrato, e porta ancora la
  preenfasi della stazione trasmittente. Il primo demodulatore esegue un passa
  banda inverso e (a meno che FX.25 sia attivo) la deenfasi per annullarla.

In entrambi i casi il secondo demodulatore a 1200 Bd resta su un percorso
diverso, quindi la coppia copre sempre due equalizzazioni distinte.

**Esempi.**

* Baofeng UV-5R, audio prelevato dalla presa altoparlante da 3,5 mm →
  **spento**.
* Un ricetrasmettitore veicolare con presa dati mini-DIN a 6 pin, usando il pin
  4 (packet 1200 Bd) → di solito **spento**; usando il pin del discriminatore a
  9600 Bd → **attivo**.
* Qualunque collegamento G3RUH a 9600 Bd → **attivo**, sempre.
* Un SDR che fornisce audio FM demodulato senza deenfasi nella catena →
  **attivo**.

.. warning::

   Invertire questa impostazione non blocca del tutto la decodifica: dimezza in
   silenzio la sensibilità della stazione. Il sintomo è una stazione che
   decodifica perfettamente i pacchetti locali forti e perde tutto ciò che è
   debole. Se il vostro tasso di decodifica sembra scarso e i livelli sono
   corretti, commutate questa opzione e confrontate.

.. tip::

   È un'impostazione che ha senso provare empiricamente. Eseguite **LIVELLO RX**
   con ciascuna scelta mentre c'è traffico reale sul canale, e osservate quale
   riporta ``DCD sì`` più spesso e produce più decodifiche nella tabella del
   traffico del pannello.

Preambolo (ms)
--------------

**Che cos'è.** Il *TXDelay* di AX.25: per quanto tempo la radio resta in
trasmissione, inviando solo byte di flag HDLC, prima del primo bit di dati della
trama. Dà tempo allo squelch delle stazioni riceventi di aprirsi, al loro AGC di
assestarsi e ai PLL dei loro demodulatori di agganciarsi.

**Intervallo 50–2000 ms, predefinito 300 ms.**

``Ax25TxDelay()`` converte i millisecondi in un numero di byte di flag rispetto
alla velocità in baud corrente. A 1200 Bd, 8 bit durano circa 6,7 ms, quindi
300 ms sono circa 45 byte di flag e ogni 1000 ms in più aggiungono circa 150
byte di portante muta **davanti a ogni singola trama trasmessa**.

**Come scegliere.**

* 300 ms va bene per la maggior parte dei ricetrasmettitori moderni in simplex.
* Alzatelo a 400–600 ms se il vostro trasmettitore è lento a salire a piena
  potenza, se passate attraverso un ripetitore, o se stazioni lontane riferiscono
  di sentire la vostra portante ma di decodificare solo alcune trame.
* Alzatelo ancora, verso 800–1000 ms, solo per un percorso PTT davvero lento: un
  amplificatore commutato a relè, un transverter, un apparato vecchio con
  commutazione T/R lenta.
* Abbassatelo verso 150–200 ms solo con una radio moderna, veloce e collegata
  direttamente, e solo dopo aver verificato con un vicino che le vostre trame
  vengono ancora decodificate.

.. warning::

   Un preambolo lungo è una pura tassa su un canale condiviso. A 1200 Bd, un
   rapporto di posizione APRS sta in aria circa mezzo secondo; un preambolo di
   2000 ms significa che cinque sesti di ogni trasmissione sono portante muta. Su
   una frequenza affollata questo è uno dei modi più rapidi per diventare la
   stazione di cui tutti si lamentano.

.. warning::

   Un preambolo troppo *corto* è la causa classica di "alcune stazioni non mi
   sentono mai". La trama inizia prima che il loro squelch si sia aperto, così i
   suoi primi byte — incluso il campo indirizzo — vanno persi, e la trama
   fallisce senza essere mai conteggiata come errore in un punto visibile. Se
   venite digipetati in modo incostante, provate ad alzare questo valore prima di
   ogni altra cosa.

Slot temporale TX (ms)
-----------------------

**Che cos'è.** Malgrado il nome, questo campo imposta il **tempo di silenzio**
del modem: ``Ax25TimeSlot()`` lo scrive in ``Ax25Config.quietTime``, il periodo
di assestamento obbligatorio che lo schedulatore di trasmissione osserva dopo una
trasmissione (e all'avvio del modem) prima di prendere in considerazione una
nuova trasmissione. Quando si imposta un valore diverso da zero, al primo termine
viene aggiunto un jitter casuale di 100–1000 ms, così due stazioni configurate
allo stesso modo che si avviano insieme non restano in sincronia.

**Intervallo 0–10000 ms, predefinito 2000 ms.** Impostarlo a 0 elimina del tutto
il termine: il modem trasmetterà appena il canale è sentito libero e il lancio
di persistenza riesce.

.. note::

   L'intervallo fra i lanci di persistenza — il classico *SlotTime* di AX.25 — è
   fissato a 100 ms dentro il modem e non è esposto in questa pagina. Questo
   campo è il tempo di silenzio che vi si somma.

**Esempi.**

* Canale APRS metropolitano affollato, stazione che fa anche da digipeater →
  mantenete 2000 ms o alzate a 3000 ms. La stazione risponderà comunque
  prontamente ai messaggi, perché il tempo di silenzio ritarda solo l'*inizio*
  di un nuovo ciclo di trasmissione.
* Un canale rurale tranquillo con poche stazioni → 1000 ms è comodo.
* Un collegamento punto-punto dedicato su una frequenza privata con due sole
  stazioni → 0 è ragionevole; non c'è contesa da distribuire.

.. warning::

   0 su un canale condiviso elimina completamente il periodo di assestamento e
   rende questa stazione la più aggressiva della frequenza. Tutte le altre si
   ritirano dopo aver trasmesso; la vostra no.

.. warning::

   I valori grandi ritardano *tutto*, inclusi i riscontri dei messaggi e le
   ripetizioni del digipeater, che il limitatore di duty cycle più sotto esenta
   deliberatamente. Oltre i 5000 ms circa la stazione comincia a sembrare poco
   reattiva a chi prova a messaggiarla.

Buffer TX
---------

**Che cos'è.** Quante trame possono stare nell'anello di trasmissione RF — in
coda in attesa di canale libero, o in aria in quel momento — prima che una trama
appena offerta venga scartata invece che accodata.

**Intervallo 1–11, predefinito 1.** Il tetto è la profondità utile reale
dell'anello di trasmissione (``AX25_TX_FRAME_RING_MAX``), così il menu a tendina
non può mai offrire un valore che l'anello non potrebbe contenere.

**Da che cosa protegge.** Senza un tetto, una raffica — un IGate che ritrasmette
APRS-IS verso RF più in fretta di quanto il canale si liberi, o una passata dello
schedulatore in cui più rapporti periodici scadono insieme — accoderebbe trame
molto più in fretta di quanto un canale a 1200 Bd possa smaltire. Quelle trame
andrebbero in aria minuti dopo, molto oltre il momento in cui erano ancora vere,
oppure verrebbero scartate comunque a anello pieno. Limitare qui l'arretrato
scarta subito l'eccesso e registra il motivo.

**Esempi.**

* Il valore predefinito **1** — la risposta giusta per un tracker o una stazione
  che si limita a inviare beacon. I rapporti di posizione interessano solo se
  attuali; uno vecchio accodato dietro altri tre è peggio di nessuno.
* **2–3** — un digipeater affollato o un IGate con gateway INET→RF attivo, su un
  canale che si libera a raffiche. Assorbe un breve accumulo senza lasciare
  invecchiare la coda.
* **8–11** — raramente appropriato. Solo su un collegamento dedicato dove il
  canale è praticamente sempre libero e ogni trama deve davvero essere
  consegnata.

.. warning::

   Alzare questo valore non rende il canale più veloce. Una coda piena perché il
   canale è occupato resterà piena; le trame saranno semplicemente più vecchie
   quando finalmente partiranno. Se vengono scartate trame per arretrato,
   riducete ciò che la stazione trasmette (intervalli di beacon più lunghi,
   filtri IGate più stretti) invece di approfondire la coda.

.. note::

   Questo riguarda solo l'anello di trasmissione RF. Il socket APRS-IS ha un suo
   buffer separato, quindi un ramo RF congestionato non blocca né scarta mai il
   ramo internet dello stesso pacchetto.

Limitatore duty cycle e Limite duty cycle (%)
----------------------------------------------

**Che cos'è.** Un tetto sul *tempo di trasmissione cumulativo proprio* di questa
stazione, misurato su una **finestra mobile di 10 minuti** (suddivisa
internamente in quaranta secchi da 15 secondi). È del tutto indipendente dal
CSMA: il CSMA impedisce di trasmettere *sopra* qualcun altro e non ha memoria di
ciò che avete trasmesso un minuto fa; questo ha memoria e nessuna opinione sul
canale.

**Spento per default. Intervallo del tetto 1–100 %, predefinito 25 %.**

**Che cosa succede al tetto.** Il traffico si divide in due:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Classe
     - Comportamento una volta raggiunto il tetto
   * - **Critico** — messaggi APRS e relativi riscontri, ripetizioni del
       digipeater
     - Vengono trasmessi comunque. Il loro tempo in aria continua a contare per
       la finestra; solo il controllo viene saltato per essi.
   * - **Non critico** — beacon di posizione propri, oggetti e item, rapporti
       meteo, telemetria, bollettini, ritrasmissione massiva INET→RF dell'IGate
     - Vengono trattenuti. Ciascuno di essi è un task periodico che offrirà di
       nuovo lo stesso rapporto al proprio intervallo successivo, quindi si
       tratta di un rinvio e non di una perdita, anche se viene conteggiato sotto
       ``DROP_TX_DUTY_CYCLE`` nel pannello per renderlo visibile.

**Usi.**

* **Stazioni non presidiate.** In diversi piani di banda un tetto di duty cycle
  su una stazione automatica non è una cortesia ma una condizione di licenza. È
  qui che lo si impone.
* **Proteggere l'amplificatore di potenza.** Un portatile o un piccolo
  amplificatore progettato per uso vocale intermittente si surriscalda se uno
  schedulatore mal configurato lo manda in trasmissione ripetutamente. 20–25 % è
  una cifra sicura comune per questo hardware.
* **Siti solari o a batteria.** La corrente in trasmissione domina il bilancio
  energetico; un tetto trasforma "quanta energia consumerà questa stazione" in un
  numero calcolabile.
* **Una rete di sicurezza contro i vostri stessi errori.** Un intervallo di
  beacon impostato per sbaglio a 10 secondi, una tempesta di definizioni di
  telemetria o un filtro INET→RF impazzito non possono monopolizzare la frequenza
  con il limitatore attivo.

**Esempi.**

* Digipeater in vetta con alimentazione di rete e amplificatore adeguato →
  attivo al **50 %**: generoso, ma pur sempre un limite duro contro una fuga di
  controllo.
* IGate domestico su un portatile → attivo al **20 %**. I finali del portatile
  ringrazieranno.
* Digipeater di riempimento alimentato a energia solare → attivo al **10 %**, con
  intervalli di beacon abbastanza lunghi perché il limitatore sia una rete di
  sicurezza e non un fatto quotidiano.
* Una stazione in prova su carico fittizio → spento.

.. warning::

   Poiché il traffico dei messaggi e le ripetizioni del digipeater sono esenti,
   il limitatore non è una garanzia di tempo massimo in aria. Un digipeater nel
   mezzo di una rete di emergenza supererà il tetto configurato, per progetto. Non
   trattate il numero come una garanzia regolamentare se le condizioni della
   vostra licenza sono assolute.

.. warning::

   Un tetto molto basso combinato con beacon frequenti fa sì che i beacon vengano
   rinviati in silenzio, a volte a lungo. Se aprs.fi mostra la vostra stazione
   aggiornarsi molto meno spesso dell'intervallo configurato, controllate il
   contatore degli scarti nel pannello prima di sospettare della radio.

Tempo minimo PTT sbloccato (ms)
--------------------------------

**Che cos'è.** Un intervallo **senza PTT** garantito aggiuntivo fra una
trasmissione e la successiva, oltre al ritardo fisso di rilascio di un tick di
servizio (circa 10 ms) che il modem applica sempre.

**Intervallo 0–5000 ms, predefinito 0 (nessuna attesa aggiuntiva).**

**Perché esiste.** Il limite proprio del modem garantisce che il PTT venga
rilasciato in modo visibile fra le trame, ma certi apparati necessitano di
parecchio di più:

* Un ripetitore il cui tono di cortesia e coda di squelch devono terminare prima
  che il pacchetto successivo vada in trasmissione, altrimenti l'inizio della
  vostra trama ci finisce sopra.
* Un ricetrasmettitore con commutazione T/R a relè che ha bisogno di tempo di
  assestamento, altrimenti taglierà l'inizio della trasmissione successiva.
* Un amplificatore lineare con sequenziatore, dove ritrasmettere troppo in
  fretta commuta il relè a caldo.
* Una radio la cui logica interna di temporizzazione o "antirimbalzo del PTT"
  ignora un'attivazione che arriva troppo presto dopo un rilascio.

**Esempi.**

* Collegamento diretto a un portatile o veicolare moderno → **0**.
* Lavorando attraverso un ripetitore con beep di cortesia → **500–1000 ms**,
  abbastanza perché la coda si esaurisca.
* Amplificatore commutato a relè nella catena → **200–300 ms**.

.. warning::

   Questo ritardo si applica fra *tutte* le trasmissioni consecutive, inclusi i
   tentativi ripetuti di un messaggio APRS non riscontrato. Impostare secondi qui
   rende uno scambio di più trame sensibilmente lento.

Persistenza CSMA (p, 1–255)
----------------------------

**Che cos'è.** Il parametro *Persist* standard di AX.25/KISS. Una volta sentito
il canale libero, il modem trasmette in quello slot con probabilità ``p/256``; a
un mancato successo aspetta un altro slot e rilancia.

**Intervallo 1–255, predefinito 63** (≈ 24,6 % per slot libero, il valore
convenzionale di AX.25).

**Perché la casualità.** Se tutte le stazioni andassero in trasmissione
nell'istante in cui il canale tace, tutte quelle in attesa colliderebbero
esattamente nello stesso momento — e più stazioni aspettano, peggiore è la
collisione. Tirare i dadi distribuisce quelle trasmissioni nel tempo senza alcun
coordinamento fra stazioni.

.. list-table::
   :header-rows: 1
   :widths: 16 84

   * - Valore
     - Comportamento
   * - 255
     - Trasmette al primo slot libero, sempre. Equivale al CSMA non persistente
       semplice. Appropriato solo dove si è l'unico trasmettitore.
   * - 128
     - ~50 % per slot. Deciso; sensato su un canale poco usato dove la latenza
       conta.
   * - 63
     - ~25 % per slot. Lo standard. Corretto per praticamente ogni canale APRS
       condiviso.
   * - 20–32
     - ~8–12 %. Per un canale urbano molto congestionato, o per una stazione che
       dovrebbe cedere il passo alle altre (un beacon di telemetria a bassa
       priorità, un digi di riempimento in una zona affollata).
   * - 1
     - ~0,4 %. La stazione aspetterà moltissimo per ogni trasmissione.

**Anti-starvation.** Lo schedulatore non lascia che una trama aspetti per
sempre: dopo otto slot consecutivi persi per canale occupato o per lancio
fallito, forza comunque la trasmissione. Così anche una persistenza molto bassa
ha un ritardo di caso peggiore limitato invece che illimitato.

.. warning::

   Lo 0 viene rifiutato e portato a 1 anziché accettato, perché una persistenza
   di 0 significherebbe che il lancio non può mai riuscire e la stazione non
   trasmetterebbe mai più: un modo di guasto che assomiglia esattamente a un
   hardware rotto.

.. warning::

   Alzare la persistenza non fa passare i vostri pacchetti su un canale occupato;
   li fa *collidere* su un canale occupato, il che costa tempo in aria a tutti e
   non consegna nulla. Se i vostri pacchetti non arrivano a un digipeater, la
   risposta è più preambolo, livelli audio migliori o un'antenna migliore, non una
   ``p`` più alta.

Interfaccia audio
=================

Questo gruppo descrive ciò che sta elettricamente fra i pin dell'ESP32 e il
ricetrasmettitore. I valori predefiniti si adattano a una scheda di interfaccia
che porta la propria rete di polarizzazione, gli attenuatori e il filtro di
ricostruzione — il progetto mostrato negli schemi elettrici del progetto. Le
impostazioni qui sono ciò di cui ha bisogno invece un'interfaccia ridotta a un
condensatore di accoppiamento e a un trimmer di livello per direzione.

Polarizzazione interna dell'ingresso ADC
-----------------------------------------

**Che cos'è.** L'ADC dell'ESP32 misura una tensione fra 0 V e circa 3,1 V.
L'audio è un segnale che oscilla in positivo e in negativo attorno allo zero,
quindi va sollevato fino al centro di quell'intervallo prima di poter essere
campionato; altrimenti la metà negativa va semplicemente persa. Quel
sollevamento si chiama polarizzazione.

* **Spento** (predefinito) — la scheda di interfaccia fornisce la
  polarizzazione, tipicamente con due resistenze che formano un partitore sul pin
  dell'ADC. È ciò che mostrano gli schemi del progetto.
* **Attivo** — il firmware abilita le resistenze interne di pull-up *e* pull-down
  del pad dell'ADC, che insieme portano il pin a circa metà dell'alimentazione. È
  ciò di cui ha bisogno un ingresso accoppiato solo tramite un condensatore.

**Quando attivarlo.** Avete costruito un'interfaccia minima: un condensatore
dall'uscita altoparlante della radio (attraverso un trimmer di livello)
direttamente al pin dell'ADC, senza resistenze di polarizzazione vostre.
Eseguite **LIVELLO RX**: se il ``DC`` riportato è vicino a 0 mV o vicino al
binario di alimentazione invece che attorno a 1500–1600 mV, l'ingresso non è
polarizzato ed è questo l'interruttore che lo risolve.

.. warning::

   **Limite hardware.** Le resistenze interne esistono solo su GPIO32 e GPIO33.
   Se il firmware è stato compilato con l'ADC su GPIO34–GPIO39 — che sono pad di
   solo ingresso, privi di qualsiasi resistenza di pull — abilitare questa
   opzione registra un errore e non cambia nulla. Un ingresso del genere può
   essere polarizzato solo esternamente.

.. warning::

   **Non** attivatela quando la scheda di interfaccia imposta già la
   polarizzazione. Le resistenze interne sono deboli ma non trascurabili, e
   caricano il partitore esterno, spostando il punto di lavoro e riducendo
   l'escursione utile. Se usate la scheda di interfaccia del progetto, lasciatela
   spenta.

Avvisa quando l'audio ricevuto esce dal fondo scala
----------------------------------------------------

**Che cos'è.** Una diagnostica. Con essa attiva, il firmware registra un avviso
ogni volta che i risultati di conversione grezzi raggiungono uno degli estremi
del campo del convertitore (sotto il codice 15 o sopra il 4080, su 0–4095), al
massimo una volta ogni cinque secondi perché un problema persistente non inondi
la console.

**Spenta per default**, perché su un'interfaccia costruita correttamente non
dovrebbe mai scattare e il controllo non costa nulla quando è disabilitato.

**Quando attivarla.**

* Mentre regolate per la prima volta il trimmer di livello di ricezione —
  combinatela con la pagina *Log* per vedere gli avvisi mentre accadono.
* In modo permanente, su un'interfaccia **senza diodi di clamp in ingresso**. Lì
  una lettura fuori scala non significa solo "troppo forte": significa che il pin
  viene pilotato oltre i binari di alimentazione, il che è una strada per
  danneggiare l'ESP32.
* Quando una stazione decodifica bene a volume moderato e smette di decodificare
  alzando il volume della radio — la firma classica della saturazione.

.. warning::

   L'audio saturato non suona rotto all'orecchio umano, ma una forma d'onda AFSK
   saturata perde la relazione di ampiezza da cui il demodulatore dipende. Una
   stazione che ne soffre tipicamente decodifica *peggio* le stazioni forti che
   quelle deboli, cosa così controintuitiva che c'è chi cambia antenna per
   questo.

Ampiezza di uscita in trasmissione (%)
---------------------------------------

**Che cos'è.** L'ampiezza picco-picco dell'audio di trasmissione, in percentuale
sull'intera escursione 0–3,3 V del DAC. Internamente ogni campione viene scalato
attorno al codice mediano del DAC prima di essere scritto, così il tono resta
centrato qualunque sia la percentuale.

**Intervallo 20–100 %, predefinito 60 %.** Applicata subito, e già al campione
successivo: non occorre fermare il modulatore.

**Perché il minimo è 20 %.** Il DAC dell'ESP32 è a 8 bit. La percentuale decide
con quanti dei suoi 256 codici viene effettivamente disegnato un periodo di
sinusoide: al 20 % un periodo completo copre circa 50 codici, e al di sotto la
quantizzazione trasforma il tono in una scala visibile le cui armoniche cadono
nella banda audio e degradano la deviazione che il ricevitore vede. I 30–40 dB
di attenuazione di cui ha bisogno un ingresso microfonico competono a un
**attenuatore resistivo esterno**, non a questo campo.

**Come regolarla.** Usate il pulsante **TEST TX** con un misuratore di
deviazione, un altro ricevitore o un SDR sul segnale trasmesso, e regolate per
**2,5–3,5 kHz** di deviazione su un canale FM a 5 kHz di deviazione. Se non
riuscite a scendere abbastanza al 20 %, aggiungete o aumentate l'attenuatore
esterno. Se non riuscite a salire abbastanza al 100 %, l'attenuatore è troppo
aggressivo.

.. warning::

   La sovradeviazione è il guasto di trasmissione più comune in APRS. Sporca il
   canale adiacente e — poiché il discriminatore del ricevitore satura — il
   pacchetto spesso decodifica *peggio* all'altro capo di quanto avrebbe fatto con
   la deviazione corretta. Più forte non è meglio.

.. warning::

   La sottodeviazione è un guasto altrettanto reale: la trama resta sepolta nel
   rumore di fondo del ricevitore e la decodificano solo stazioni molto vicine. Se
   vi sente il digipeater dall'altra parte della strada e nessun altro, misurate
   la deviazione prima di dare la colpa alla propagazione.

Frequenza di campionamento in trasmissione
-------------------------------------------

**Che cos'è.** La frequenza con cui il DAC genera la forma d'onda di
trasmissione: **38400 Hz** (predefinita) o **76800 Hz**.

Entrambe sono multipli esatti di 1200 e 9600, il che è un requisito rigido: il
modulatore ricava la temporizzazione dei simboli per divisione intera, quindi una
frequenza che non sia multiplo di entrambe desincronizza l'una o l'altra
velocità. Un valore diverso da questi due ripiega sulla frequenza standard invece
di essere memorizzato e rifiutato più tardi.

**Che cosa porta la frequenza più alta.** Ogni uscita campionata porta con sé
immagini di ricostruzione: copie del segnale desiderato riflesse attorno ai
multipli della frequenza di campionamento. Raddoppiare la frequenza sposta quelle
immagini un'ottava più in là, dove un filtro molto più dolce — o la banda audio
del trasmettitore stesso — può eliminarle.

**Quando scegliere 76800 Hz.**

* La scheda di interfaccia **non ha filtro di ricostruzione**: il pin del DAC
  passa per un condensatore e un trimmer direttamente alla radio.
* Osservate audio spurio inspiegabile, un tono trasmesso dal suono ruvido o un
  ricevitore scontento su un montaggio per il resto corretto.

**Quando restare a 38400 Hz.**

* L'interfaccia ha un filtro passa basso adeguato dopo il DAC, che è ciò che
  mostrano gli schemi del progetto.
* Il margine di CPU è scarso: la frequenza più alta raddoppia il carico di
  interrupt del DAC, e su una stazione che esegue anche l'IGate,
  l'amministrazione web, Telegram e il polling dei sensori quel carico non è
  gratis.

.. warning::

   Questa impostazione ha effetto **solo dopo un riavvio**. Il modulo mostra il
   valore salvato, che è quello con cui partirà il prossimo avvio; fino ad allora
   il modem continua a trasmettere alla frequenza con cui è stato avviato.

Tempo massimo di trasmissione (ms)
-----------------------------------

**Che cos'è.** Un watchdog di sicurezza su una singola trasmissione. Se la radio
è in trasmissione continuativamente da più tempo di questo, il firmware rilascia
il PTT, interrompe la trama in corso, svuota il FIFO del modulatore e registra un
errore.

**Intervallo 0–60000 ms. 0 disabilita il limite — e 0 è il valore predefinito.**

**Perché esiste.** Non è uno schedulatore e non è un controllo di duty cycle.
Esiste per il caso in cui il percorso di trasmissione si sia *bloccato*: un task
che non termina mai, una macchina a stati del modem incastrata a metà trama, un
guasto che altrimenti lascerebbe una portante in aria finché qualcuno non se ne
accorge. Su una stazione non presidiata in vetta, questa è la differenza fra
un'ora storta e una frequenza bloccata più un coordinatore arrabbiato.

**Come scegliere un valore.** Calcolate la trasmissione più lunga che questa
stazione può legittimamente produrre e lasciate un margine generoso al di sopra.
Quel caso peggiore è il preambolo massimo più una trama di lunghezza massima alla
velocità più lenta in uso: qualche secondo a 1200 Bd, di più con la ridondanza
FX.25. Valori pratici:

* **5000–10000 ms** per una stazione a 1200 Bd con preambolo di 300 ms. Molto
  sopra qualunque trasmissione reale, molto sotto qualunque cosa si chiamerebbe
  portante bloccata.
* **15000–20000 ms** per una stazione HF a 300 Bd, dove le trame sono davvero
  lunghe.
* **0** su un montaggio da banco dove preferite vedere accadere un guasto
  piuttosto che vederlo ripulito alle vostre spalle.

.. warning::

   Impostarlo vicino alla durata di una trasmissione reale taglia le trame reali.
   La stazione sembrerà trasmettere spazzatura che nessuno decodifica, e il log
   mostrerà il limite scattare ripetutamente. Se vedete quel messaggio,
   l'impostazione è troppo bassa, non la radio è rotta.

.. tip::

   Abilitarlo è fortemente consigliato per qualunque stazione lasciata non
   presidiata, specialmente con un amplificatore. Non costa nulla quando non
   succede nulla.

I tre pulsanti di diagnostica
==============================

Tutti e tre stanno accanto alla casella *Abilita modem audio ADC/DAC*. Tutti e
tre **salvano prima il modulo**, così ciò che viene provato è sempre ciò che c'è
sullo schermo. Tutti e tre sono rotte ``POST``, deliberatamente: due di essi
mandano la radio in trasmissione, e il controllo di stessa origine che protegge
questa amministrazione si applica solo al ``POST`` — una rotta ``GET`` qui
potrebbe essere innescata da qualunque altra pagina che un operatore autenticato
avesse aperta, semplicemente puntandovi un tag immagine.

Solo uno dei tre può essere in esecuzione per volta; una seconda richiesta viene
rifiutata con un messaggio chiaro invece di essere accodata.

TEST LOOP
---------

**Che cosa fa.** Costruisce una piccola trama di stato APRS che porta un token
casuale monouso (``SELFTST>APLT1T:>LOOPTEST <token>``), devia le trame
decodificate a un proprio hook privato così che la trama di prova non venga mai
digipetata né inviata ad APRS-IS, commuta il modem in full duplex per la durata,
la trasmette e attende fino a 4 secondi che la catena
ADC / demodulatore / decodificatore restituisca la stessa trama. L'hook reale e
la modalità duplex configurata vengono ripristinati qualunque sia l'esito.

Prima di trasmettere attende fino a 3 secondi che il canale si liberi, così una
stazione reale in aria non causa un falso fallimento. Se il canale non si libera
mai, trasmette comunque invece di restare appeso.

**Che cosa richiede.** Un **loop audio** fisico: il pin del DAC collegato al pin
dell'ADC (attraverso gli attenuatori della scheda di interfaccia, o
direttamente), con massa comune. Può anche essere eseguito attraverso un
ricetrasmettitore in vero full duplex, o tramite una radio che si ascolta da sé
su un'altra radio — ma nella sua forma normale è una prova da banco della
scheda, non della radio.

.. warning::

   Il full duplex viene forzato durante la prova perché un loop via cavo fa sì
   che il modem senta in permanenza la propria portante, e il CSMA non
   troverebbe mai un canale libero. È una forzatura deliberata e temporanea, ma
   implica che la prova trasmette senza riguardo per ciò che c'è sul canale. Non
   eseguitela con un'antenna collegata su una frequenza affollata.

**Come leggere un PASS.** Un esito positivo riporta il livello RX in mV RMS,
l'escursione grezza dell'ADC con i binari del convertitore (0/4095) come
riferimento, e il guadagno di picco dell'AGC. Quei numeri contano quanto la
parola PASS: un'escursione sana e centrata dovrebbe restare ben lontana da
entrambi i binari. Un PASS con l'escursione quasi a toccare 0 o 4095 significa
che state decodificando *e* saturando, e smetterete di decodificare appena
qualcosa alzerà il livello.

La sola corrispondenza del token non viene accettata come PASS. L'esecuzione deve
avere catturato anche almeno 50 conteggi di escursione reale dell'ADC e un
livello RX non nullo, così che un PASS non possa mai essere riportato su un loop
aperto, un ingresso flottante o una decodifica vecchia proveniente da altro.

**Come leggere un FAIL.** I messaggi di fallimento sono graduati e ciascuno
punta a una parte diversa della catena:

.. list-table::
   :header-rows: 1
   :widths: 42 58

   * - Che cosa riporta
     - Che cosa significa
   * - L'ADC non ha consegnato nemmeno un campione
     - Il driver continuo dell'ADC o il suo timer non sono in funzione. Un
       fallimento di inizializzazione, non un problema di cablaggio o di livello.
       Riavviate; se persiste, la compilazione o l'assegnazione dei pin è errata.
   * - Campiona, ma il codice grezzo si è mosso appena
     - L'ADC è vivo ma non vede alcun tono. Il filo del loop manca, è rotto, o le
       masse non sono comuni. L'offset in continua riportato dice di più: una
       linea inchiodata vicino al binario indica un corto o un cablaggio errato.
   * - È arrivato un segnale reale, ma nessun demodulatore si è agganciato
     - Il tono arriva all'ADC ma il correlatore/PLL non riesce a interpretarlo.
       Verificate che *Modulazione* corrisponda a ciò che è stato trasmesso, e
       provate a commutare *Ingresso audio piatto / da discriminatore* — un loop
       diretto DAC-ADC non passa mai per la rete di deenfasi di una radio vera. Se
       il guadagno dell'AGC non è mai salito sopra l'unità, il problema è nel
       percorso dell'AGC e non nella velocità in baud.
   * - Il PLL si è agganciato, ma non è tornata alcuna trama valida
     - Il messaggio riporta fin dove è arrivata la macchina a stati HDLC: non
       iniziare mai una trama indica il recupero dei bit; iniziare trame che
       falliscono il CRC indica un livello o un SNR marginali più che una
       discordanza di modulazione.
   * - È tornata una trama, ma non corrispondeva
     - Distorsione audio, saturazione, o un loop che raccoglie qualcosa di
       diverso dalla trasmissione di questa stazione.

LIVELLO RX
----------

**Che cosa fa.** Osserva lo stadio d'ingresso di ricezione per circa un secondo
e riporta ciò che ha visto. **Non trasmette nulla e non tocca alcuno stato del
modem**, quindi — a differenza del test loop — può essere eseguito con il
ricetrasmettitore collegato, l'antenna su e traffico reale in decodifica.

È la misura contro cui si regola il lato ricezione dell'interfaccia audio.

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Campo
     - Come leggerlo
   * - ``mVrms`` / ``peak``
     - Livello audio medio e di picco sulla finestra. Con la radio senza squelch
       su un canale inattivo state leggendo rumore; con un pacchetto in arrivo
       state leggendo segnale. Un livello sano lascia margine chiaro sotto i
       binari.
   * - ``DC``
     - Dove è polarizzato l'ingresso. Dovrebbe stare vicino al centro del campo
       del convertitore, attorno a 1500–1600 mV. Vicino a 0 mV o al binario
       significa che l'ingresso non è polarizzato — si veda *Polarizzazione
       interna dell'ingresso ADC*.
   * - ``AGC``
     - Il guadagno automatico che il modem sta applicando. Un guadagno che resta
       a 1,00x su segnale reale significa che l'ingresso è già al bersaglio o
       sopra. Un guadagno molto grande significa che il segnale è troppo debole e
       il modem sta amplificando rumore insieme a esso.
   * - ``raw``
     - Gli estremi di conversione grezzi, rispetto a binari 0 e 4095. È il vostro
       margine di saturazione: raggiungere 0 o 4095 è fuori scala.
   * - ``DCD``
     - Se un demodulatore è stato agganciato durante la finestra. ``sì`` mentre
       arriva un pacchetto è esattamente giusto; ``sì`` su un canale silenzioso
       indica un ingresso rumoroso o un aggancio falso.

**Procedura tipica.** Togliete lo squelch alla radio, premete **LIVELLO RX** e
regolate il trimmer di ricezione finché gli estremi grezzi usano una buona parte
del campo senza avvicinarsi ai binari e l'offset in continua è centrato. Poi
rimettete lo squelch normale, aspettate traffico reale e verificate che compaia
``DCD sì`` e che il pannello mostri decodifiche.

.. tip::

   È anche il modo più rapido per rispondere a "la radio è almeno collegata?".
   Una lettura piatta con il contatore di campioni fermo dice che l'audio non
   arriva affatto, e nessuna regolazione altrove cambierà la cosa.

TEST TX
-------

**Che cosa fa.** Manda la radio in trasmissione, modula una breve trama di stato
APRS (``SELFTST>APLT1T:>TXTEST``) e rilascia il PTT, senza attendere nulla in
ritorno. È la controparte in trasmissione di LIVELLO RX: ciò contro cui si regola
il livello di trasmissione quando è collegato un ricetrasmettitore invece di un
loop via cavo.

La raffica passa per il **normale percorso di accesso al canale**: attende canale
libero come qualunque altra trama, e viene trattenuta dal tetto di duty cycle se
questo è attivo e già raggiunto. La pagina attende che la raffica finisca, con un
limite di circa tre secondi, così che il risultato significhi qualcosa quando lo
si legge.

In caso di successo riporta la lunghezza del preambolo e l'ampiezza di uscita
usate, e ricorda l'obiettivo: misurate la deviazione con altra strumentazione e
regolate il trimmer di livello di trasmissione per **2,5–3,5 kHz**.

.. warning::

   **Questo mette un segnale reale in aria.** Prima di premerlo, verificate la
   frequenza, verificate l'antenna o il carico fittizio e verificate di essere
   autorizzati a trasmettere lì. Su un canale APRS condiviso, non premetelo
   ripetutamente mentre regolate un trimmer: usate un carico fittizio per la
   regolazione e una sola raffica in aria per la verifica.

Se rifiuta, il messaggio dice perché: il modem non è abilitato, un'altra
diagnostica è in corso, o il percorso di accesso al canale ha scartato la trama —
il che in pratica significa un tetto di duty cycle già raggiunto o una coda di
trasmissione piena. Il log degli eventi indica quale.

Messa in servizio di una stazione nuova, in ordine
===================================================

I campi di questa pagina interagiscono, quindi esiste una sequenza che evita di
rincorrersi la coda.

.. list-table::
   :header-rows: 1
   :widths: 6 34 60

   * - #
     - Passo
     - Note
   * - 1
     - Spuntate **Abilita modem audio ADC/DAC**, *Salva*, **riavviate**.
     - Nient'altro in questa pagina può essere provato finché l'hardware del
       modem non è attivo.
   * - 2
     - Impostate **Modulazione** in modo che corrisponda al canale.
     - 1200 Bd Bell 202 per l'APRS standard. Sbagliare qui rende privo di senso
       ogni passo successivo.
   * - 3
     - Con i pin del DAC e dell'ADC collegati in loop, eseguite **TEST LOOP**.
     - Dimostra che la scheda funziona da capo a capo prima di coinvolgere una
       radio. Annotate le cifre di escursione, non solo il PASS.
   * - 4
     - Collegate la radio. Impostate **Ingresso audio piatto / da
       discriminatore** in base alla presa usata.
     - Presa altoparlante → spento. Porta dati/discriminatore → attivo.
   * - 5
     - Eseguite **LIVELLO RX** e regolate il trimmer di ricezione.
     - Controllate prima l'offset in continua; abilitate **Polarizzazione interna
       dell'ingresso ADC** se l'ingresso non è polarizzato. Poi impostate il
       livello per una buona escursione con margine.
   * - 6
     - Abilitate **Avvisa quando l'audio ricevuto esce dal fondo scala** e
       tenete d'occhio la pagina *Log* mentre arriva traffico reale.
     - Conferma che non state saturando sulle stazioni locali forti.
   * - 7
     - Su carico fittizio, eseguite **TEST TX** e regolate **Ampiezza di uscita
       in trasmissione** per 2,5–3,5 kHz di deviazione.
     - Aggiungete un attenuatore esterno se il 20 % è ancora troppo forte.
   * - 8
     - Impostate **Preambolo** in base al comportamento del PTT della vostra
       radio.
     - 300 ms salvo motivi diversi. Verificate con un vicino di essere
       digipetati.
   * - 9
     - Impostate **Persistenza CSMA** e **Slot temporale TX** in base alla
       congestione del canale.
     - I valori predefiniti (63 / 2000 ms) sono corretti per un normale canale
       condiviso.
   * - 10
     - Abilitate il **limitatore di duty cycle** e impostate il **Tempo massimo
       di trasmissione**.
     - Fatelo prima di lasciare la stazione non presidiata, non dopo.

Profili suggeriti
=================

.. list-table::
   :header-rows: 1
   :widths: 22 13 13 10 10 10 11 11

   * - Stazione
     - Modulazione
     - Preambolo
     - Silenzio
     - Persist.
     - Buffer
     - Duty
     - Max TX
   * - IGate domestico, radio portatile, canale condiviso
     - 1200 Bell 202
     - 300 ms
     - 2000 ms
     - 63
     - 1
     - sì, 20 %
     - 8000 ms
   * - Digipeater in vetta, rete elettrica + amplificatore
     - 1200 Bell 202
     - 400 ms
     - 2000 ms
     - 63
     - 2
     - sì, 50 %
     - 10000 ms
   * - Digipeater di riempimento, canale urbano affollato
     - 1200 Bell 202
     - 300 ms
     - 3000 ms
     - 32
     - 1
     - sì, 15 %
     - 8000 ms
   * - Tracker / beacon solare
     - 1200 Bell 202
     - 300 ms
     - 2000 ms
     - 63
     - 1
     - sì, 10 %
     - 8000 ms
   * - Collegamento punto-punto dedicato a 9600 Bd
     - 9600 G3RUH
     - 150 ms
     - 0 ms
     - 255
     - 3
     - no
     - 5000 ms
   * - Gateway HF, 300 Bd
     - 300 AFSK
     - 500 ms
     - 3000 ms
     - 63
     - 1
     - sì, 25 %
     - 20000 ms

Sono punti di partenza, non prescrizioni. Le cifre di duty cycle in particolare
vanno conciliate con le condizioni della vostra licenza.

Riferimento dei campi
=====================

.. list-table::
   :header-rows: 1
   :widths: 30 14 14 42

   * - Campo
     - Intervallo
     - Predefinito
     - Applicazione
   * - FX.25
     - disattivato / solo ricezione / ricezione e trasmissione
     - disattivato
     - Immediata
   * - Abilita modem audio ADC/DAC
     - sì / no
     - sì
     - **Riavvio successivo**
   * - Modulazione
     - 0–3
     - 1 (Bell 202)
     - Immediata
   * - Ingresso audio piatto / da discriminatore
     - sì / no
     - sì
     - Immediata
   * - Preambolo
     - 50–2000 ms
     - 300 ms
     - Immediata
   * - Slot temporale TX
     - 0–10000 ms
     - 2000 ms
     - Immediata
   * - Buffer TX
     - 1–11
     - 1
     - Immediata (letto a ogni trasmissione)
   * - Limitatore duty cycle
     - sì / no
     - no
     - Immediata (letto a ogni trasmissione)
   * - Limite duty cycle
     - 1–100 %
     - 25 %
     - Immediata (letto a ogni trasmissione)
   * - Tempo minimo PTT sbloccato
     - 0–5000 ms
     - 0
     - Immediata
   * - Persistenza CSMA
     - 1–255
     - 63
     - Immediata
   * - Polarizzazione interna dell'ingresso ADC
     - sì / no
     - no
     - Immediata
   * - Avvisa quando l'audio ricevuto esce dal fondo scala
     - sì / no
     - no
     - Immediata
   * - Ampiezza di uscita in trasmissione
     - 20–100 %
     - 60 %
     - Immediata (campione successivo)
   * - Frequenza di campionamento in trasmissione
     - 38400 / 76800 Hz
     - 38400 Hz
     - **Riavvio successivo**
   * - Tempo massimo di trasmissione
     - 0–60000 ms
     - 0 (disattivato)
     - Immediata

Ogni campo numerico è limitato in tre punti contro le stesse costanti di
``main/include/aprs_service.h``: gli attributi ``min``/``max`` del campo stesso,
il gestore che analizza il modulo inviato e il caricatore che legge
``radio.json`` dalla flash. Un file di configurazione modificato a mano o un POST
malformato non possono quindi mettere in servizio un valore fuori intervallo.

Ciò che deliberatamente *non* c'è in questa pagina
===================================================

Diverse impostazioni che un operatore potrebbe aspettarsi qui non esistono,
perché il firmware non ha un equivalente a runtime per esse. Sono mostrate in
sola lettura, o non sono mostrate affatto, invece di essere offerte come
controlli che salverebbero in flash senza cambiare nulla.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Impostazione assente
     - Perché
   * - Livello di squelch
     - Non c'è squelch software. Tutti i campioni raggiungono il demodulatore, e
       il decodificatore AX.25 si appoggia al DCD del demodulatore stesso. Usate
       lo squelch della radio, oppure lasciatelo aperto, il che spesso decodifica
       meglio.
   * - Volume / guadagno di ricezione
     - Non c'è uno stadio di guadagno RX da regolare. L'AGC si autolimita.
       Impostate il livello con il trimmer dell'interfaccia, guidati da
       **LIVELLO RX**.
   * - Guadagno massimo dell'AGC
     - L'AGC si limita da sé; non c'è nulla da configurare.
   * - Attenuazione dell'ADC
     - Una costante di compilazione (``MODEM_ADC_ATTEN``). Mostrata in sola
       lettura.
   * - Pin audio (ADC/DAC)
     - Cablaggio di scheda fissato in compilazione (``MODEM_ADC_GPIO`` /
       ``MODEM_DAC_GPIO``). Mostrato in sola lettura.
   * - Pin e polarità del PTT
     - Cablaggio di scheda fissato in compilazione (``MODEM_PTT_GPIO`` /
       ``MODEM_PTT_ACTIVE_HIGH``). Mostrato in sola lettura; il pin è registrato
       nella tabella di proprietà dei GPIO così che nessun'altra funzione possa
       rivendicarlo.
   * - Soppressione dei duplicati
     - Un'unica coppia di controlli per tutto il firmware, nella pagina *IGate*.
   * - Intervallo di slot CSMA
     - Fissato a 100 ms dentro il modem. *Slot temporale TX* in questa pagina è
       il tempo di silenzio, che è il parametro che vale la pena regolare.

Risoluzione dei problemi
========================

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - Sintomo
     - Dove guardare
   * - Non si decodifica nulla, in nessuna direzione
     - **Modulazione** per prima cosa. Poi eseguite **TEST LOOP** su un loop via
       cavo per separare un problema di scheda da uno di radio.
   * - TEST LOOP dice che il modem non è abilitato
     - **Abilita modem audio ADC/DAC** è spento, o è stato acceso senza
       riavviare.
   * - Le stazioni locali forti decodificano, quelle deboli mai
     - Livello di ricezione troppo basso, o **Ingresso audio piatto / da
       discriminatore** impostato al contrario. Eseguite **LIVELLO RX**.
   * - Le stazioni deboli decodificano, quelle forti no
     - Saturazione. Abilitate **Avvisa quando l'audio ricevuto esce dal fondo
       scala**, eseguite **LIVELLO RX** e abbassate il trimmer di ricezione.
   * - Le altre stazioni sentono la mia portante ma decodificano solo alcune
       trame
     - **Preambolo** troppo corto, o deviazione sbagliata. Alzate il preambolo,
       poi misurate la deviazione con **TEST TX**.
   * - Nessuno mi sente affatto, ma la radio trasmette
     - Deviazione troppo bassa, o l'audio di trasmissione non arriva all'ingresso
       microfonico. Impostate **Ampiezza di uscita in trasmissione** contro una
       misura di deviazione.
   * - I beacon compaiono molto meno spesso di quanto configurato
     - Il **limitatore di duty cycle** li sta rinviando. Controllate il contatore
       degli scarti nel pannello e alzate il tetto o allungate gli intervalli.
   * - Vengono scartate trame per arretrato
     - La stazione offre più traffico di quanto il canale possa trasportare.
       Riducete ciò che trasmette prima di alzare **Buffer TX**.
   * - Il log riporta lo scatto del tempo massimo di trasmissione
     - **Tempo massimo di trasmissione** è sotto la durata di una trasmissione
       reale. Alzatelo ben sopra il caso peggiore.
   * - La frequenza di campionamento in trasmissione salvata non sembra
       applicarsi
     - Richiede un riavvio. Il modulo mostra il valore salvato, non quello in
       funzione.

.. seealso::

   :ref:`it-modem` per i profili del modem e l'API del componente,
   :ref:`it-dsp-signal-chain` per i percorsi di segnale in ricezione e
   trasmissione, :ref:`it-web-admin` per l'amministrazione nel suo complesso, e
   il capitolo *Hardware* per gli schemi di interfaccia che i valori predefiniti
   di questa pagina presuppongono.
