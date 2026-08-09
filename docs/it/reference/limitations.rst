.. _it-limitations:

========================
Stato e limitazioni note
========================

Il firmware è **work in progress**. Il percorso di trasmissione RF, l'IGate, il
digipeater, i beacon, il meteo, la telemetria, la messaggistica e
l'amministrazione web sono tutti funzionanti.

Tabella comparativa delle funzionalità
=========================================

La tabella seguente confronta le funzionalità implementate in questo progetto
con l'unione delle funzioni presenti nei software APRS più diffusi (client
desktop/di mappatura come Xastir, APRSIS32 e YAAC; TNC software come Direwolf
e UZ7HO Soundmodem; e stack iGate/digipeater headless come aprx e VP-Digi).
Nessun singolo pacchetto di quell'ecosistema implementa tutte le righe — è
normale e atteso. La legenda è:

* ✅ — Implementato e funzionante
* ⚠️ — Implementazione parziale / limitata
* ❌ — Non implementato

Modem / Livello 2
--------------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacità specifica
     - Tipico nei software APRS diffusi
     - Qui
     - Note sull'implementazione di questo progetto
   * - AFSK 1200 Bd Bell 202 (APRS VHF standard)
     - ✅ (Direwolf, UZ7HO, VP-Digi, TNC hardware)
     - ✅
     - Profilo predefinito; doppio demodulatore in parallelo per aumentare la probabilità di decodifica
   * - AFSK 1200 Bd V.23
     - ⚠️ (Direwolf lo supporta; molti client no)
     - ✅
     - Profilo modem selezionabile n. 2; come Bell 202 esegue due demodulatori
       in parallelo
   * - AFSK 300 Bd (APRS HF)
     - ✅ (Direwolf, UZ7HO)
     - ✅
     - Profilo modem selezionabile n. 0
   * - FSK G3RUH 9600 Bd
     - ✅ (Direwolf, TNC pacchetto dedicati)
     - ✅
     - Profilo modem selezionabile n. 3
   * - Framing HDLC / codifica-decodifica AX.25 UI
     - ✅ (universale)
     - ✅
     - Percorso TX/RX completo via software, su ADC/DAC. ``ax25_decode()`` legge
       solo i byte della trama che riceve: il campo indirizzi viene percorso un
       indirizzo alla volta rispetto alla lunghezza della trama, quindi
       un'intestazione i cui bit di estensione dichiarano più ripetitori di
       quanti la trama ne porti viene respinta invece di decodificare ciò che la
       segue in memoria
   * - FEC Reed-Solomon FX.25
     - ⚠️ (Direwolf sì; la maggior parte dei TNC hardware no)
     - ✅
     - Tre modalità nella pagina Radiomodem: spento, solo RX (decodifica FX.25 e
       trasmette AX.25 semplice) e RX+TX. I blocchi trasmessi restano
       retrocompatibili — un ricevitore AX.25 semplice ignora il tag di
       correlazione e i byte di parità e decodifica il frame contenuto
   * - IL2P (alternativa a FX.25)
     - ⚠️ (solo Direwolf)
     - ❌
     - Non implementato
   * - Protocollo KISS (seriale o TCP) per fungere da TNC per software client esterno
     - ✅ (Direwolf, UZ7HO, praticamente tutti i soundmodem)
     - ❌
     - Non implementato. Nessun server KISS/AGWPE seriale o di rete — questo progetto non può fungere da "back end" TNC per Xastir/APRSIS32/YAAC ecc.
   * - Protocollo AGWPE
     - ⚠️ (TNC orientati a Windows)
     - ❌
     - Non implementato
   * - CSMA / rilevamento canale occupato prima della TX
     - ✅
     - ✅
     - Accesso p-persistente condizionato dal DCD: persistenza configurabile
       (``csma_persist``, 1-255), tempo di silenzio prima dell'accesso
       (``tx_timeslot``) e preambolo/TXDelay, più un limite anti-starvation di
       otto slot perché un canale che non si libera mai non trattenga per sempre
       un frame in coda. La dashboard riporta quante volte è intervenuto quel
       limite, distinguendo canale occupato da canale libero, come *CSMA FORZATO
       (OCCUP./PERSIST.)*
   * - Tetto di duty cycle di trasmissione a lungo termine
     - ⚠️ (raro al di fuori di apparati commerciali/regolamentati)
     - ✅
     - Tetto opzionale (``duty_cycle_en``, disattivato di default) di
       ``duty_cycle_pct`` per cento (1-100, default 25) misurato su una
       finestra scorrevole di 10 minuti, accumulato dal tempo in onda stimato
       di ogni frame effettivamente trasmesso alla velocità configurata. Viene
       trattenuto solo il traffico non critico: i messaggi e le ripetizioni del
       digipeater partono sempre. Un beacon trattenuto viene differito, non
       perso - il task periodico che lo genera lo ripropone al suo intervallo
       successivo -, anche se viene conteggiato come ``DROP_TX_DUTY_CYCLE`` per
       renderlo visibile. La dashboard mostra la percentuale misurata rispetto
       a quella configurata come *CICLO DI LAVORO TX*, ed è popolata anche con
       il limitatore spento per poter valutare il tetto prima di attivarlo
   * - Attivazione PTT (senza VOX, GPIO hardware)
     - ✅
     - ✅
     - GPIO e polarità a tempo di compilazione; tempo minimo di mantenimento dis-attivazione regolabile a runtime
   * - Strumento integrato di loopback RF/autotest
     - ⚠️ (raro)
     - ✅
     - "LOOP TEST" — trasmette un pacchetto con token e verifica che l'intera catena RX lo decodifichi correttamente, con diagnostica dettagliata per fase
   * - Ingresso audio piatto/discriminatore rispetto ad audio de-enfatizzato
     - ✅ (Direwolf, UZ7HO)
     - ✅
     - Indica al demodulatore se riceve audio da altoparlante o audio non
       filtrato dal discriminatore; applicato in tempo reale al salvataggio
   * - Controllo della profondità della coda di TX
     - ⚠️ (di solito una coda interna fissa)
     - ✅
     - ``rf_tx_buffers``: quanti frame possono attendere nell'anello di TX RF
       prima che i nuovi pacchetti vengano scartati anziché accodati; letto a
       ogni trasmissione, quindi ha effetto senza riavvio
   * - Tempo minimo di PTT rilasciato tra i frame
     - ⚠️ (TXTAIL su alcuni TNC)
     - ✅
     - ``ptt_min_unkey_ms``, 0-5000 ms oltre al rilascio fisso di un tick che il
       modem applica sempre — per radio o ripetitori che richiedono un
       intervallo garantito più lungo tra le trasmissioni

IGate (RF <-> APRS-IS)
------------------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacità specifica
     - Tipico nei software APRS diffusi
     - Qui
     - Note sull'implementazione di questo progetto
   * - Inoltro RF -> APRS-IS
     - ✅ (universale)
     - ✅
     - Pipeline completa: dedup -> controllo lunghezza minima -> filtro token di percorso -> regola sat-gate -> filtro per tipo di payload -> gate di raggio -> gate di prefisso -> budlist
   * - Inoltro APRS-IS -> RF (IGate bidirezionale)
     - ✅ (modalità igate di Direwolf, aprx, VP-Digi)
     - ✅
     - Soppressione dell'eco dei report propri, filtro per tipo di payload, unwrap di terze parti ristretto, budlist
   * - Soppressione dei pacchetti duplicati
     - ✅
     - ✅
     - Cache condivisa; profondità e finestra configurabili via web nella
       pagina IGate (4-40 voci, predefinito 20; finestra 1-120 s, predefinito
       30 s)
   * - Inserimento Q-construct ``qAR``/``qAO``
     - ✅
     - ✅
     - ``qAR`` quando questo IGate può inoltrare messaggi verso RF per la
       stazione che viene inoltrata; ``qAO`` altrimenti (IGate a sola
       ricezione, oppure IGate bidirezionale con l'inoltro INET→RF
       disattivato)
   * - Stringa di filtro APRS-IS lato server (``r/``, ``p/``, ``t/``, ``b/``...)
     - ✅
     - ✅
     - Inviata testualmente nella riga di login, con validazione locale della grammatica prima dell'invio
   * - Gate di raggio locale (distanza ortodromica)
     - ⚠️ (alcuni, es. ``filter`` di aprx)
     - ✅
     - Distanza haversine rispetto a "La mia stazione"; supporta posizioni compresse e non compresse
   * - Whitelist locale sui prefissi del nominativo
     - ⚠️ (poco comune come funzione di prima classe)
     - ✅
     - Elenco di prefissi separati da virgola (es. ``EA,EB,EC``)
   * - Budlist di nominativi (whitelist/blacklist)
     - ✅
     - ✅
     - Modalità per direzione: disattivato / whitelist / blacklist
   * - Filtro per tipo di payload (msg/status/tlm/wx/obj/item/query/buoy/position)
     - ✅ (principalmente tramite filtri APRS-IS)
     - ✅
     - Locale, basato su bitmask, applicato in entrambe le direzioni indipendentemente dal filtro del server
   * - Gestione pacchetti di terze parti (``}``) / protezione anti-loop
     - ✅ (critico, spesso manuale)
     - ✅
     - Disattivato di default; l'unwrap opzionale è vincolato alla sola modalità whitelist proprio per prevenire i loop di IGate
   * - Riconnessione automatica ad APRS-IS con backoff
     - ✅
     - ⚠️
     - Riconnessione TCP automatica, rilegge la configurazione a ogni
       riconnessione, ma con un intervallo di ritentativo fisso di 1 s (anche
       1 s finché il dispositivo non ha una rotta verso internet) e non con un
       backoff esponenziale. Ogni tentativo fallito passa al server configurato
       successivo invece di ripetere lo stesso
   * - Login ad APRS-IS basato su passcode
     - ✅
     - ✅
     - Riga di login standard ``user/pass/vers/filter``; la risposta verified/unverified del server viene mostrata
   * - Server APRS-IS multipli / failover
     - ⚠️ (alcuni supportano elenchi di server)
     - ✅
     - Quattro slot server (``APRS_SERVER_NUM``), ognuno con la propria casella
       Abilita, host e porta. Un fallimento di DNS, connessione o login passa
       allo slot abilitato successivo e riparte circolarmente, ritentando ogni
       secondo finché uno accetta; gli slot disabilitati vengono saltati, anche
       al primo tentativo dopo l'avvio. Tutti gli slot condividono la stessa
       identità di login (nominativo/SSID/passcode/filtro). Il pannello indica
       lo slot in uso
   * - Statistiche per motivo di scarto
     - ⚠️ (poco comune, di solito solo totali)
     - ✅
     - Contatori nominati (``DROP_TOO_SHORT``, ``DROP_PATH_TOKEN``, ``DROP_RANGE_FILTER``, ecc.)
   * - Elenco dei nominativi dei gate satellitari/ISS
     - ⚠️ (aprx e alcuni satgate dedicati)
     - ✅
     - Fino a 8 nominativi di digipeater satellitari; un frame ripetuto da uno
       di essi senza il flag di ripetuto (``*``) impostato viene scartato
       prima di raggiungere APRS-IS

   * - Criteri di filtraggio messaggi (località di destinatario/mittente)
     - ✅ (richiesto a un IGate conforme)
     - ✅
     - Tutte e quattro le condizioni sono applicate prima che un messaggio letto
       da APRS-IS raggiunga la RF: destinatario ascoltato localmente entro la
       finestra, mittente non ascoltato in RF, nessun ``TCPXX``/``NOGATE``/
       ``RFONLY`` nell'intestazione del mittente, destinatario non connesso a
       Internet. Ogni fallimento ha il proprio motivo di scarto
   * - Finestra di ascolto locale configurabile
     - ⚠️ (spesso fissa)
     - ✅
     - ``igate_local_window_sec``, 60-3600 s, un'ora per impostazione predefinita
   * - Posizione associata dopo un messaggio ritrasmesso
     - ⚠️ (poco comune)
     - ✅
     - Anello di otto destinatari; il primo rapporto di posizione o di boa visto
       per uno di essi viene ritrasmesso una volta, in sostituzione della pratica
       obsoleta di ripetere le posizioni storiche

Digipeater
-----------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacità specifica
     - Tipico nei software APRS diffusi
     - Qui
     - Note sull'implementazione di questo progetto
   * - Digipeating WIDEn-N, Nuovo Paradigma n-N (tracciato)
     - ✅ (universale)
     - ✅
     - Decremento del conteggio hop **e** inserimento del proprio nominativo
       marcato come usato, così ogni hop di un percorso ripetuto è
       identificabile
   * - Tabella di alias configurabile
     - ⚠️ (variabile; spesso un elenco fisso)
     - ✅
     - Quattro righe di {alias, N massimo, modalità} nella pagina Digi; ``#`` in
       un alias corrisponde a una cifra, quindi una riga copre un'intera
       famiglia (``WIDE#``). Tabella di fabbrica: ``WIDE1`` 1 hop, ``WIDE2``
       2 hop, ``WIDE#`` 2 hop, tutte tracciate
   * - Intrappolamento di N grande
     - ✅ (atteso da ogni digipeater moderno)
     - ✅
     - ``N massimo`` per alias; un conteggio hop maggiore viene limitato al tetto
       (predefinito) o scartato (``DROP_DIGI_N_TRAPPED``), a scelta
       dell'operatore
   * - Ruolo di digipeater di riempimento (solo ``WIDE1-1``)
     - ✅
     - ✅
     - Una sola casella; limita la stazione alle righe di alias a un solo hop
   * - Instradamento regionale ``SSn-N``
     - ⚠️ (convenzione regionale)
     - ✅
     - Una normale riga di alias, tipicamente in modalità Inondazione con il
       limite di hop della regione
   * - Inondazione ``WIDEn-N`` non rintracciabile (NOID)
     - ⚠️ (comportamento datato)
     - ❌
     - Non prodotta per ``WIDEn-N``: il paradigma l'ha spostato sul meccanismo di
       tracciamento. La modalità Inondazione esiste, ma solo per una riga di
       alias che l'operatore decida di usare senza traccia
   * - Alias datati ``TRACEn-N`` / ``RELAY`` / ``ECHO`` / ``GATE``
     - ⚠️ (obsoleti)
     - ❌
     - Abbandonati come percorsi e non incorporati. Un operatore che ne abbia
       ancora bisogno per un vicino datato lo aggiunge come una normale riga di
       alias
   * - Conteggio hop codificato nel SSID di destinazione (legacy)
     - ⚠️ (TNC più datati)
     - ✅
     - Disattivato per impostazione predefinita (*Ripetizione tramite SSID di
       destinazione*). Instrada prima della tabella degli alias e in base a quel
       solo SSID, quindi un percorso esplicito non verrebbe mai letto;
       disattivato, l'SSID di destinazione resta intatto e decide la tabella
       degli alias
   * - Soppressione duplicati/ping-pong nel digipeating
     - ✅
     - ✅
     - Finestra propria di 30 s nella cache di deduplica condivisa
       (``DUP_SCOPE_DIGI``), con chiave di sola origine e payload, verificata
       prima di qualsiasi lavoro sul percorso
   * - Filtro di digipeating per nominativo (ripetere solo certe fonti)
     - ⚠️ (alcuni, es. VP-Digi)
     - ❌
     - Non esposto come filtro specifico del digipeater (la budlist dell'IGate non equivale a un filtro del digi)
   * - Digipeating viscoso/preventivo
     - ⚠️ (raro, TNC avanzati)
     - ❌
     - Non implementato

Tracciamento / Beaconing
----------------------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacità specifica
     - Tipico nei software APRS diffusi
     - Qui
     - Note sull'implementazione di questo progetto
   * - Ingresso posizione GPS in tempo reale (NMEA)
     - ✅ (universale per tracker mobili)
     - ❌
     - Non implementato. I beacon sono solo a posizione fissa — non c'è alcun ingresso di posizione in tempo reale né configurazione relativa al GPS
   * - Beaconing a posizione fissa (stazione base)
     - ✅
     - ✅
     - Posizione/intervallo/simbolo/commento separati per ruolo (tracker, IGate, digi)
   * - Smart Beaconing (intervallo adattivo su velocità/direzione)
     - ✅ (client mobili, OpenTracker)
     - ❌
     - Nessun GPS, quindi non applicabile
   * - Rotta/velocità nei report di posizione
     - ✅
     - ⚠️
     - Supportato in Oggetti/Item, ma il beacon tracker della stazione non ha una fonte live di rotta/velocità (nessun GPS)
   * - Codifica posizione compressa (Base-91)
     - ✅
     - ✅
     - Opzione per servizio nelle pagine Tracker, IGate, Digipeater e
       Oggetti/Item; anche il decoder la comprende. Viene saltata
       automaticamente quando l'ambiguità di posizione non è zero o è in uso
       un'estensione dati, perché il formato compresso non ha spazio per
       nessuna delle due
   * - Codifica posizione Mic-E (TX)
     - ⚠️ (soprattutto firmware per tracker mobili)
     - ✅
     - La pagina beacon Tracker offre un'opzione Mic-E
       (``aprs_mice_encode()``); solo posizione fissa, quindi rotta/velocità
       vengono sempre inviate come "sconosciute" e il codice messaggio è fisso
       su Off Duty. Il campo informativo segue l'ordine canonico di
       ``mic-e-examples.txt``: byte TYPE, altitudine, blocco di frequenza,
       commento, ``!DAO!`` e la coppia Produttore/Versione che identifica il
       firmware (l'indirizzo di destinazione porta dati di posizione, quindi il
       TOCALL ``APxxxx`` non può)
   * - PHG / potenza-altezza-guadagno-direttività
     - ✅
     - ✅
     - Esposto nella pagina beacon dell'IGate
   * - RNG / portata radio precalcolata
     - ⚠️
     - ✅
     - Selezionabile come estensione dati del beacon dell'IGate (``RNGrrrr``)
   * - DFS / intensità del segnale omni-DF
     - ⚠️ (software specifico per DF)
     - ✅
     - Selezionabile come estensione dati del beacon dell'IGate (``DFSshgd``)
   * - Ambiguità di posizione nei rapporti trasmessi
     - ⚠️
     - ✅
     - Livello 0-4 a livello di stazione nella pagina Stazione; si applica ai
       formati non compresso e Mic-E, e forza il formato non compresso quando è
       diverso da zero
   * - Localizzatore Maidenhead nei rapporti di stato
     - ⚠️
     - ✅
     - Opzione a livello di stazione; emette la forma ``>IO91SX/G`` di APRS101
       cap.16
   * - Localizzatore Maidenhead nella destinazione AX.25 (``[IO91SX]``,
       obsoleto)
     - ⚠️ (software legacy)
     - ❌
     - Contrassegnato come obsoleto dalla specifica stessa; non prodotto
   * - Altitudine nei beacon
     - ✅
     - ✅
     - Altitudine per ruolo (tracker, IGate, digipeater), ciascuna copiata dal
       valore di "La mia stazione" quando è spuntato *Usa i dati de La mia
       stazione*. I report meteo non contengono alcun campo di altitudine
   * - Percorso di digipeating configurabile per servizio
     - ✅
     - ✅
     - Quattro preset di percorso condivisi; ogni servizio che trasmette
       (tracker, IGate, digipeater, meteo, telemetria, messaggi, oggetti,
       bollettini) sceglie tra questi con la propria maschera di bit

   * - Identificatore di tipo dati con capacità di messaggistica (``=`` / ``@``)
     - ✅ (universale)
     - ✅
     - Scelto in base ad *Abilita messaggistica*: ``!``/``/`` con la
       messaggistica spenta, ``=``/``@`` con essa accesa, così i client riceventi
       offrono una via di risposta. Al formato Mic-E non avanza un
       identificatore e dichiara la stessa cosa con il suo byte TYPE
       (`` ` `` / ``'``), letto dalla stessa spunta

Messaggistica
--------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacità specifica
     - Tipico nei software APRS diffusi
     - Qui
     - Note sull'implementazione di questo progetto
   * - Messaggistica APRS indirizzata
     - ✅ (universale)
     - ✅
     - Instradamento RF e/o APRS-IS per messaggio
   * - Conferma di ricezione messaggio (``ackNNN``)
     - ✅
     - ✅
     - Auto-ack alla ricezione, auto-riprova fino a conferma
   * - Reply-ACK (APRS 1.1, ``{MM}AA``)
     - ⚠️ (APRSdos, APRS+SA, Xastir, APRSIS32)
     - ✅
     - In entrambe le direzioni. I numeri in uscita sono ``{MM}`` o ``{MM}AA``,
       con la conferma gratuita aggiunta nell'istante della trasmissione, così
       una riprova porta l'ultima dovuta; un ``AA`` in arrivo chiude il
       messaggio in uscita che nomina, e l'``MM`` del mittente diventa ciò che
       si deve a quella stazione. La numerazione è limitata a due cifre perché
       l'identificatore completo resti nei cinque caratteri ammessi da APRS101
   * - Riprova messaggi con numero/intervallo configurabili
     - ✅
     - ✅
     - ``msg_retry`` / ``msg_interval``, valutato a 1 Hz
   * - UI di chat/inbox integrata
     - ✅ (Xastir, YAAC, APRSIS32)
     - ✅
     - Pagina ``/msgchat`` nel browser, con polling JSON; un unico filo di messaggi inviati e ricevuti, 5 visibili, ultimi 10 conservati
   * - Avviso messaggio ricevuto (suono/visivo/GPIO)
     - ⚠️ (client desktop: suono/popup)
     - ✅
     - Avviso via GPIO (LED/cicalino) invece di un popup desktop, adatto a un dispositivo headless
   * - Messaggistica broadcast/di gruppo
     - ⚠️ (alcuni tramite bollettini)
     - ❌
     - Usare i Bollettini per il broadcast; la messaggistica diretta è solo 1 a 1

Meteo
------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacità specifica
     - Tipico nei software APRS diffusi
     - Qui
     - Note sull'implementazione di questo progetto
   * - Generazione report meteo APRS proprio
     - ✅ (Xastir, aprx, molti firmware TNC con kit WX)
     - ✅
     - Set completo del cap. 12 + aggiunte APRS 1.2 (neve, luminosità, alluvione)
   * - Framework di polling sensori live (driver collegabili)
     - ⚠️ (poco comune come framework generico; di solito fissato a una singola scheda WX)
     - ✅
     - Registro dinamico e autoregistrante ``sensors_local``; include driver BMP180, estensibile
   * - Media per campo sull'intervallo di report
     - ⚠️
     - ✅
     - Casella opzionale "Media" per campo
   * - Ricezione/registrazione dei report WX di altre stazioni
     - ✅ (overlay mappa Xastir, aprs.fi)
     - ⚠️
     - Decodificato/instradato/digipeated come qualsiasi pacchetto, ma non c'è una vista dedicata di storico WX nell'amministrazione web

Telemetria
-----------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacità specifica
     - Tipico nei software APRS diffusi
     - Qui
     - Note sull'implementazione di questo progetto
   * - Generazione telemetria propria (``T#nnn``)
     - ✅ (alcuni TNC/client)
     - ✅
     - 5 canali analogici + 8 digitali
   * - Messaggi di metadati PARM/UNIT/EQNS/BITS
     - ⚠️ (spesso configurati manualmente)
     - ✅
     - Generazione attivabile individualmente
   * - Calibrazione quadratica (EQNS) per canale analogico
     - ⚠️
     - ✅
     - Coefficienti a/b/c per canale trasmessi nel messaggio ``EQNS.``. Il
       report dati porta la lettura grezza del sensore ed è il ricevitore ad
       applicare la conversione — la divisione standard di APRS101 tra report e
       metadati. L'intervallo grezzo per canale limita il valore che va in onda,
       così una sonda che legge oltre la propria scala riporta l'estremo
       dell'intervallo dichiarato invece di una cifra che nessun ricevitore può
       tracciare; un intervallo invertito o vuoto non dichiara nulla e viene
       ignorato
   * - Mappatura dei sensori in tempo reale per canale di telemetria
     - ⚠️ (di solito fissa nel codice, o alimentata da uno script esterno)
     - ✅
     - Ogni canale analogico A1-A5 e digitale B1-B8 sceglie la sorgente dal
       registro ``sensors_local``, salvata per nome del driver, così abilitare o
       disabilitare un driver non ripunta mai silenziosamente un canale su un
       altro sensore
   * - Ricezione/grafico della telemetria altrui
     - ✅ (grafici Xastir, aprs.fi)
     - ❌
     - Non implementato — nessuna vista di grafico/storico per la telemetria ricevuta

Oggetti, Item, Bollettini, Stato
------------------------------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacità specifica
     - Tipico nei software APRS diffusi
     - Qui
     - Note sull'implementazione di questo progetto
   * - Oggetti propri (con timestamp)
     - ✅
     - ✅
     - Fino a 5, RF e/o INET, con intervallo/decadimento
   * - Item propri (senza timestamp)
     - ✅
     - ✅
     - Stesso pool di 5 slot; flag "permanente" in stile YAAC sceglie tra Oggetto e Item
   * - "Uccidere" un oggetto/item
     - ✅
     - ✅
     - Trasmette la rimozione qualche volta in più, poi si autodisattiva
   * - Bollettini (``BLN1``-``BLNn``)
     - ✅
     - ✅
     - 5 slot, testo/intervallo/scadenza propri, ``BLN1``-``BLN5``
   * - Report di stato (testo libero della stazione)
     - ✅
     - ✅
     - Beacon di stato in testo libero per ruolo (DTI ``>``, APRS101 cap.16)
       per tracker, IGate e digi, ciascuno con il proprio intervallo
       (``*_sts_interval``) e testo (``*_status``); vedere ``main/beacon.c``
   * - Risposta a query (``?APRS?``, ``?WX?``, ecc.)
     - ⚠️
     - ✅
     - Query generali (``?APRS?``/``?WX?``/``?IGATE?``) e dirette, ciascuna con
       il proprio limitatore di frequenza. Ricevute sui task RF/APRS-IS, servite
       dal task dello scheduler dei beacon. Ogni sorgente ha il proprio
       interruttore e le sue risposte tornano sul canale da cui è arrivata la
       domanda, con la sorgente APRS-IS spenta per impostazione predefinita così
       il traffico di dorsale non può attivare il trasmettitore; vedi
       :ref:`it-query`
   * - Insieme di query dirette (``?APRSD``/``?APRSH``/``?APRSM``/``?APRSO``/
       ``?APRSP``/``?APRSS``/``?APRST``/``?PING?``)
     - ⚠️ (APRSISCE/32, YAAC)
     - ✅
     - Risposte fornite quando *Interrogazioni dirette estese* è abilitato. Le
       risposte in forma di elenco tornano come messaggi APRS alla stazione
       richiedente; ``?APRSO`` riannuncia gli Oggetti/Item più avanti nella stessa
       passata dello scheduler, e ``?APRSM`` ritrasmette al massimo
       ``MSG_QUERY_BURST_MAX`` (3) messaggi trattenuti per query, lasciando il
       resto alla pianificazione dei ritentativi di messaggistica
   * - Grafico della cronologia di ascolto di ``?APRSH``
     - ⚠️
     - ✅
     - La stazione tiene un istogramma di ascolto di 18 ore per nominativo
       (vedi ``components/lastheard``), quindi la risposta è il grafico
       ``Hrd: h0 h1 ... h17`` definito da APRS101 cap.15, sei conteggi per
       periodo separati da ``.``, con l'ora 0 pari all'ora corrente.
       L'istogramma appartiene alla riga della stazione e viaggia con essa quando
       la riga passa in testa alla tabella, e solo una trama ricevuta viene
       conteggiata al suo interno — rispondere alla query porta avanti il grafico
       fino all'ora corrente ma lascia intatti i conteggi memorizzati, quindi si
       può chiedere di una stazione quante volte si vuole
   * - Capacità di stazione (DTI ``<``)
     - ✅
     - ✅
     - Emesse come risposta a ``?IGATE?``
       (``<IGATE,MSG_CNT=n,LOC_CNT=n>``), dove ``MSG_CNT`` è il conteggio
       cumulativo dei pacchetti di messaggio APRS inoltrati in entrambe le
       direzioni e ``LOC_CNT`` il numero vivo di stazioni presenti nell'elenco
       delle ascoltate locali (in RF)

Mappatura / Visualizzazione
-------------------------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacità specifica
     - Tipico nei software APRS diffusi
     - Qui
     - Note sull'implementazione di questo progetto
   * - Mappa live delle stazioni ricevute
     - ✅ (Xastir, APRSIS32, YAAC, aprs.fi — centrale nella maggior parte dei client)
     - ❌
     - Non implementato. L'amministrazione web ha una tabella Last-Heard, non una mappa
   * - Rendering di simboli/icone secondo la tabella dei simboli APRS
     - ✅
     - ✅
     - Esiste un selettore di simbolo per configurare beacon/oggetti propri; Last-Heard e il Traffic Log mostrano le icone dei simboli sia per i report di posizione non compressi (``!``/``=`` e, con timestamp, ``/``/``@``) sia per il formato compresso Base-91, e anche per i report Object (``;``) e Item (``)``) con entrambi i formati di posizione (vedere ``aprs_extract_symbol()`` in ``main/aprs_coord.c``)
   * - Riproduzione dello storico delle tracce
     - ✅ (client desktop)
     - ❌
     - Non implementato
   * - Grafico di meteo/telemetria nel tempo
     - ✅ (aprs.fi, plugin Xastir)
     - ❌
     - Non implementato

Gestione stazione / Operatività
-----------------------------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacità specifica
     - Tipico nei software APRS diffusi
     - Qui
     - Note sull'implementazione di questo progetto
   * - UI di configurazione via web
     - ⚠️ (VP-Digi e alcuni progetti ESP32 ce l'hanno; la maggior parte dei client desktop usa GUI native)
     - ✅
     - 17 pagine nella barra laterale + selettore di simbolo, autenticazione HTTP Basic, riapplicazione live della maggior parte delle impostazioni senza riavvio
   * - Dashboard live (stato, contatori)
     - ⚠️
     - ✅
     - Indicatori di stato rete, pannello statistiche, log di traffico live, tabella last-heard (long-poll JSON)
   * - Log di traffico/pacchetti con vista del frame grezzo
     - ✅
     - ✅
     - Etichettato per direzione (RX/TX/DIGI/INET2RF/RX-IS), include livello audio RMS
   * - Tabella delle ultime stazioni ascoltate
     - ✅
     - ✅
     - Una riga per stazione anziché per pacchetto, la più recente per prima e
       con sfratto LRU, più l'istogramma orario di 18 ore che risponde a
       ``?APRSH``. Gli indicativi sono memorizzati in maiuscolo e confrontati
       senza distinzione tra maiuscole e minuscole, così le due sorgenti che
       riempiono la tabella — indirizzi AX.25 grezzi dall'aria e testo TNC2
       grezzo da APRS-IS — non possono dare due righe alla stessa stazione
   * - Ripristino ai valori di fabbrica compilati
     - ⚠️
     - ✅
     - Un pulsante nella pagina Sistema riscrive ``config.json`` con i valori di
       fabbrica
   * - UI multilingua
     - ⚠️ (raro; la maggior parte è solo inglese o localizzata dal SO)
     - ✅
     - EN/ES/IT, solo a tempo di compilazione — nessun cambio a runtime
   * - Aggiornamento firmware OTA/remoto
     - ⚠️ (raro nei TNC embedded; comune nell'IoT consumer)
     - ✅
     - Doppia partizione (``ota_0``/``ota_1``) con rollback automatico su immagine difettosa
   * - Archiviazione configurazione locale persistente e versionata
     - ✅
     - ✅
     - LittleFS, scritture atomiche (``.tmp`` + rinomina), tollerante a chiavi sconosciute/mancanti
   * - Gestione file (upload/download/esplorazione)
     - ❌ (non applicabile alla maggior parte del software APRS; rilevante qui trattandosi di un FS embedded)
     - ✅
     - Browser LittleFS completo (elenco/download/eliminazione/upload/formattazione)
   * - Gestione Wi-Fi AP/STA con scansione, potenza TX
     - N/D (il software desktop non ne ha bisogno)
     - ✅
     - AP/STA/AP+STA, 5 profili STA, scansione live, controllo potenza TX
   * - Sincronizzazione NTP/orario
     - ⚠️ (il SO desktop se ne occupa; rilevante in ambito embedded)
     - ✅
     - 3 host NTP configurabili, fissato a UTC per timestamp zulu corretti
   * - Regolazione prestazioni/CPU
     - N/D per software desktop
     - ✅
     - Selezione a runtime di 80/160/240 MHz
   * - Accesso remoto/console seriale per diagnostica
     - ✅ (la maggior parte dei TNC ha una console seriale)
     - ⚠️
     - Nessuna console seriale per l'operatività ordinaria (per progetto); la diagnostica vive nella dashboard web e nel LOOP TEST
   * - Controllo accessi multiutente / basato su ruoli
     - ⚠️ (raro)
     - ❌
     - Singolo utente/password HTTP Basic, senza ruoli
