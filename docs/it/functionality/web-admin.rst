.. _it-web-admin:

===================
Amministrazione web
===================

Il componente ``webconfig`` (``components/webconfig/``) è un'amministrazione
basata su ``esp_http_server`` costruita con un file per pagina (``pages/*.c``),
una tabella di route (``web_server.c``) e un insieme di helper condivisi
(``web_common.c``). Usa **autenticazione HTTP Basic** contro
``g_config.http_username`` / ``http_password`` su ogni pagina — con l'unica
eccezione dello ``/style.css`` statico, che non porta dati di configurazione o di
traffico — oltre a corrispondenza URI con wildcard, uno stack di gestore da 20 KB
e purga LRU.

Perché helper per campo
=======================

L'HTML è emesso tramite piccoli helper per campo (``web_field_text``,
``web_field_int``, ``web_field_checkbox``, ``web_select_*``, ``web_field_symbol``,
…) invece di un singolo ``snprintf`` gigante — deliberatamente, per evitare
``-Werror=format-truncation`` e mantenere leggibile ogni pagina.

Gli helper numerici (``web_field_int``, ``web_field_float``) ricevono
l'intervallo accettato dal campo e lo emettono sempre come attributi HTML
``min``/``max`` dell'input, così ogni campo numerico di ogni pagina viene
validato dal browser prima dell'invio del modulo. Questa è la prima linea di
difesa contro un errore di battitura; il gestore POST continua a limitare ciò
che memorizza, ed è quello che regge davanti a una richiesta manipolata. I
domini ricorrenti (SSID, intervallo di trasmissione, latitudine, longitudine,
altitudine) provengono dalle costanti ``WEB_RANGE_*`` di ``web_common.h``, così
un limite è definito una sola volta per tutte le pagine che lo condividono.

Le pagine
=========

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Pagina
     - Cosa fa
   * - **Dashboard**
     - Pillole di Network Status (Wi-Fi, APRS-IS via ``igate_is_connected()``),
       un pannello STATISTICS, una tabella LAST HEARD con icone di simbolo, e una
       tabella di traffico in tempo reale (DX / PACKET / DECODIFICATO / AUDIO)
       alimentata da long-poll basato su sequenza. DECODIFICATO riporta ciò che
       è stato letto dal payload stesso — la marca temporale propria del
       pacchetto, rotta, velocità, altitudine, portata radio, PHG o DFS, e il
       rilevamento e l'NRQ di un rapporto DF — e resta vuoto per un payload che
       non ne porta nessuno. La colonna conserva l'intera riga di riepilogo
       qualunque sia il payload: l'anello dimensiona il proprio campo con la
       stessa costante in cui scrive il formattatore, così un rapporto che
       riempie ogni campo viene mostrato per intero e non troncato.
   * - **Station**
     - L'identità condivisa della propria stazione che ogni beacon, oggetto e
       messaggio legge: indicativo, latitudine, longitudine, altitudine
       (``g_config.my_*``), più le opzioni in onda valide per tutta la
       stazione: ambiguità di posizione, prefisso del localizzatore Maidenhead
       nei rapporti di stato, e la direzione d'antenna e l'ERP di meteor scatter
       che li chiudono. La posizione può essere digitata oppure presa in tempo
       reale dal ricevitore GNSS tramite *Usa GPS*, che disabilita i tre campi
       e li riempie da ``GET /gps/live`` una volta al secondo mentre è
       selezionata, arrotondando latitudine e longitudine a 4 decimali e
       l'altitudine a 1 decimale, così i valori inseriti superano sempre la
       validazione dei campi.
   * - **IGate**
     - Abilita, RF→INET / INET→RF, entrambe le maschere di filtro, budlist e gate
       di portata/prefisso, indicativo/SSID/passcode, quattro riquadri *APRS-IS
       Server* (ciascuno con casella Abilita più host e porta, usati come
       rotazione di failover), stringa di
       filtro server, l'interruttore *Registra dopo i filtri* che restringe la
       tabella del traffico e la console seriale a ciò che i filtri locali
       accettano, nove caselle di tipo di payload per direzione (la nona,
       *Altri*, copre capacità di stazione, formati definiti dall'utente,
       radiogoniometria Agrelo, radiofari di locatore Maidenhead e l'elemento di
       mappa riservato), beacon on/off, posizione, intervallo, selettore di simbolo,
       oggetto, commento, stato, PHG. *Filtraggio Messaggi* contiene
       l'interruttore dei criteri per i messaggi INET→RF, il limite di hop del
       destinatario e la finestra di ascolto locale. La posizione può essere
       digitata, rispecchiare *Usa i Dati della Mia Stazione* oppure essere
       presa in tempo reale dal ricevitore GNSS tramite *Usa GPS*; le tre
       opzioni si escludono a vicenda.
   * - **BrandMeister**
     - Interruttore dell'interconnessione, la sottoscrizione del monitor
       mondiale, l'interruttore di instradamento dei messaggi solo via Internet
       per i destinatari BrandMeister e quattro nominativi di gateway
       facoltativi. L'interruttore del monitor viene rifiutato mentre l'inoltro
       INET→RF è attivo e il filtro di distanza INET→RF nella pagina *IGate* è
       disattivato, poiché i termini del filtro APRS-IS sono in OR e nulla
       resterebbe tra un flusso mondiale e il trasmettitore. Una tabella di
       stato in sola lettura riporta lo stato dell'interconnessione, se
       ``u/APBM*`` è presente nel filtro server, l'impostazione del filtro di
       distanza e quante stazioni BrandMeister sono in LAST HEARD. Non è
       coinvolta alcuna connessione DMR; vedere :ref:`it-brandmeister`.
   * - **Digi**
     - Abilita digipeater, indicativo/SSID e impostazioni beacon (posizione,
       simbolo, intervallo, commento, stato, percorso). *Estensione Dati*
       sceglie cosa porta il beacon di posizione nello spazio dopo il codice di
       simbolo — PHG, RNG, DFS o un rapporto DF — con gli stessi sottocampi e
       lo stesso specchio *Usa i Dati della Mia Stazione* offerto dalla pagina
       *IGate*. *Alias di Percorso n-N*
       contiene le quattro righe di {alias, N massimo, modalità} con cui il
       digipeater ripete, l'interruttore di solo riempimento, la scelta di cosa
       fare con un conteggio hop intrappolato e l'interruttore *Ripetizione
       tramite SSID di destinazione (legacy)*, disattivato per impostazione
       predefinita. Contiene anche i quattro preset
       di percorso condivisi ``path[0..3]`` tra cui sceglie ogni servizio che
       trasmette. La finestra di soppressione dei duplicati è un unico
       controllo, sulla pagina *IGate*. La posizione può anche essere presa in
       tempo reale dal ricevitore GNSS tramite *Usa GPS*, mutuamente esclusiva
       con *Usa i Dati della Mia Stazione*.
   * - **Tracker**
     - Abilita tracker, indicativo/SSID, intervallo fisso, posizione, simbolo di
       stazione, commento, opzioni di posizione compressa, posizione Mic-E (con
       il suo selettore di commento di posizione), PHG e altitudine. La
       posizione fissa può essere digitata, rispecchiare *Usa i Dati della Mia
       Stazione* oppure essere presa in tempo reale dal ricevitore GNSS tramite
       *Usa GPS*; le tre opzioni si escludono a vicenda. *Usa posizione GPS in
       tempo reale* è indipendente da tutte e tre: lascia la posizione fissa
       come ripiego e fa leggere il ricevitore a ogni trasmissione. Il fieldset
       *SmartBeaconing* (intervallo lento/veloce, velocità bassa/alta, angolo
       di virata, pendenza di virata, tempo minimo di virata) rende quell'
       intervallo adattivo alla velocità; ha bisogno della posizione in tempo
       reale per avere qualcosa da leggere. Chiudono la pagina un fieldset di
       beacon di stato (intervallo e testo) e uno di frequenza/ripetitore
       (frequenza, duplex, offset, tono).
   * - **Weather**
     - Abilita, invia-in-RF/-INET, timestamp, indicativo/SSID/percorso WX,
       posizione, nome oggetto, commento, caselle *Averaged* per campo, e — per
       ogni campo WX in onda — un **menu a tendina di canale** riempito in tempo
       reale dal registro ``sensors_local`` e filtrato per le capacità pubblicate
       di ogni driver. Valori in tempo reale via ``/wx/values``. La posizione
       può essere digitata, rispecchiare *Usa i Dati della Mia Stazione* oppure
       essere presa in tempo reale dal ricevitore GNSS tramite *Usa GPS*; le
       tre opzioni si escludono a vicenda.
   * - **Telemetry**
     - Parametri di beacon/report, interruttori dei messaggi di definizione,
       analogici A1–A5 con selettori di origine e calibrazione, digitali B1–B8 con
       selettori di origine e senso. Valori in tempo reale via ``/tlm/values``.
   * - **GPS**
     - *Abilita Ricevitore GPS* è l'unico interruttore che il resto del
       firmware consulta prima di usare qualsiasi cosa riportata dal modulo;
       con esso spento la UART non viene nemmeno installata e la task di
       lettura non gira. Spostarlo ha effetto immediato, senza riavvio. Sotto,
       una vista in tempo reale di sola lettura del ricevitore, guidata da un
       badge *Stato del Modulo* a colori che trasforma una pagina di numeri
       in un'unica diagnosi: rosso *Disabilitato* quando l'interruttore è
       spento o la UART non è riuscita ad avviarsi, rosso *Nessun dato
       (controllare il cablaggio)* quando il modulo è abilitato ma non è
       arrivato nulla sul pin di ricezione entro il timeout di collegamento,
       ambra *Ricerca in corso (nessuna posizione)* quando le frasi arrivano
       ma non è stata ancora riportata una soluzione di navigazione valida, e
       verde *Posizione OK* una volta ottenuta. Sotto il badge, stato
       del collegamento, stato di navigazione, qualità del fix e modo 2D/3D,
       posizione, altitudine e separazione del geoide, velocità al suolo,
       rotta e variazione magnetica, data e ora UTC, satelliti usati e in
       vista, HDOP/PDOP/VDOP, i contatori delle frasi accettate e scartate e
       l'età dell'ultima frase e dell'ultimo fix. La porta seriale e i suoi
       pin sono cablaggio di scheda fissato in compilazione e sono mostrati
       come testo. Valori in tempo reale via ``/gps/values``, interrogato
       ogni secondo. La sua controparte numerica, ``/gps/live``, è quella
       interrogata dalla casella *Usa GPS* di ogni altra pagina per
       autocompilare i propri campi di posizione/moto (Station, IGate, Digi,
       Tracker, Weather); lo script di ogni pagina arrotonda la latitudine, la
       longitudine e l'altitudine ricevute alla precisione accettata dai
       propri campi (4 decimali per la posizione, 1 decimale per
       l'altitudine) prima di scriverle.
   * - **Telegram**
     - *Abilita bot Telegram* governa l'intero sottosistema; con esso spento
       nulla si collega a Telegram e nessuna attività di interrogazione viene
       eseguita, e spostarlo ha effetto immediato senza riavviare. Sotto, il
       token del bot (come campo password, con lo stesso controllo
       mostra/nascondi usato dal passcode dell'IGate) e l'identificativo
       numerico dell'amministratore, portato come valore a 64 bit e inviato
       come testo perché gli identificativi utente di Telegram non stanno più
       in 32 bit. Sotto, l'indirizzo della Mini App e le tabelle a dimensione
       fissa di utenti autorizzati e chat di gruppo consentite (fino a 8
       utenti e 4 chat di gruppo, ciascuno con un identificativo e un nome
       visualizzato). Tutto ciò che si trova in questa pagina è memorizzato in
       ``/storage/telegram.json``, non in ``config.json``, quindi può anche
       essere scaricato e ricaricato dalla pagina Archivio file. La
       tabella di stato sotto il modulo riporta a che punto è la connessione e,
       quando non avanza, esattamente quale passo è fallito e cosa fare: manca
       il file di configurazione o non è analizzabile, il token è vuoto o non
       ha la forma ``<numeri>:<segreto>``, il certificato radice non è nella
       partizione di archiviazione, non c'è ancora una rotta verso Internet, la
       memoria non è bastata per una sessione TLS, oppure Telegram stesso ha
       risposto e ha rifiutato, nel qual caso si mostrano il suo codice di
       errore e il suo testo non tradotti. Valori live via
       ``/telegram/status``, ogni due secondi. Il bot che questa pagina avvia
       risponde a ``/status`` con l'interruttore di ogni servizio - igate,
       digipeater, tracker, meteo, telemetria, messaggistica, risponditore di
       interrogazioni, BrandMeister, ricevitore GNSS, modem AFSK, limitatore
       del ciclo di lavoro in TX e sincronizzazione SNTP - e a ``/sensors`` con
       ogni campo meteo e ogni canale di telemetria abilitato nelle pagine
       Weather e Telemetry, con il driver di sensore a cui è mappato e la sua
       lettura attuale. Entrambe le risposte sono costruite sulla
       configurazione in vigore quando il comando arriva, quindi un Save ha
       effetto al comando successivo senza riavviare.
   * - **Winlink**
     - I due ruoli Winlink della stazione, in una sola pagina. *Account
       Winlink* contiene ciò che serve a una sessione propria: il nominativo
       del servizio APRSLink, l'identità con cui si apre la casella (il
       nominativo base, senza il suo SSID), la password con cui si risponde a
       una sfida di accesso, e gli interruttori che decidono se una sessione si
       apre da sé, quanto può durare, se il suo traffico resta fuori dall'aria
       e se il commento del beacon annuncia questa stazione come lettrice
       Winlink. La password è resa come campo password con lo stesso comando
       mostra/nascondi usato dal passcode dell'IGate, e non viene mai
       trasmessa: una sfida indica tre posizioni di caratteri e solo quei
       caratteri vengono rimandati indietro. *Gateway per le stazioni locali*
       contiene l'unica impostazione dell'altro ruolo, inoltrare la sessione
       propria di un vicino, insieme a una vista in sola lettura delle tre
       impostazioni dell'IGate che decidono la stessa questione, così da vedere
       in un colpo d'occhio i quattro ingressi dell'inoltro dei messaggi. Sotto
       il modulo, il terminale di sessione: a che punto è la sessione e quanto
       le resta, pulsanti per accedere, uscire ed elencare la posta, un campo di
       comando libero che accetta l'intero insieme di comandi APRSLink, un
       assistente in tre passi per scrivere un messaggio, e le risposte inviate
       dal servizio. Ogni risposta memorizzata che inizia con un numero di
       messaggio è una riga di un elenco della casella e porta una fila
       *Leggi* / *Rispondi* / *Inoltra* / *Elimina* per quel messaggio. Valori
       live via ``/winlink/status`` e ``/winlink/list``,
       interrogati ogni tre secondi; le azioni sono inviate a
       ``/winlink/cmd``, che è POST perché aziona il trasmettitore.
   * - **Logs**
     - Un visore della console seriale, per poter leggere ciò che la stazione
       stampa senza un cavo collegato. Non c'è nulla da configurare: un
       pulsante, che dice *Avvia* mentre non si cattura nulla e *Ferma* mentre
       si cattura, e una finestra sotto di esso che conserva le ultime 50
       righe. Una riga di console più lunga di 255 caratteri prosegue nella
       riga successiva invece di essere troncata, e la finestra scorre in
       entrambe le direzioni: verticalmente perché conserva più righe di
       quante ne stiano sullo schermo, orizzontalmente perché ogni riga viene
       mantenuta intera. *Avvia* installa una copia sullo scrittore del
       registro; l'uscita seriale in sé resta invariata in ogni caso, e
       l'anello che la copia riempie viene allocato solo mentre una cattura è
       in corso. La cattura non sopravvive mai alla pagina: arrivarci (o
       ricaricarla) ferma tutto ciò che fosse rimasto attivo, così il pulsante
       si presenta sempre nel suo stato *Avvia*; lasciarla ferma la cattura dal
       browser; e una scheda chiusa, addormentata o interrotta a metà sessione
       non dice nulla, ed è per questo che la copia si ferma anche da sola
       quando nessuno la legge per dieci secondi. Non viene scritto nulla nella
       flash e non viene registrato nulla: viene mostrato solo ciò che arriva
       mentre la finestra è aperta. Righe live tramite ``/logs/read``,
       interrogato ogni secondo.
   * - **Bulletins**
     - Fino a cinque bollettini (identificatore e gruppo del destinatario,
       testo, RF/INET, intervallo iniziale, rampa di decadimento, scadenza).
   * - **Objects and Items**
     - Fino a cinque oggetti/item (nome, posizione, simbolo, rotta/velocità,
       commento, RF/INET, intervallo, flag permanente, kill).
   * - **Snd/Rcv Msg**
     - L'interfaccia di casella/composizione APRS (``/msgchat``): un unico filo
       di messaggi inviati e ricevuti, cinque visibili per volta e dieci
       conservati.
   * - **Message**
     - Configura il motore di messaggistica (abilitazione RF/INET, ritentativo,
       percorso di digipeating, GPIO di allarme).
   * - **Query**
     - Abilitazione del risponditore di query APRS, quale sorgente viene
       risposta (RF / APRS-IS — la risposta torna sempre sul canale da cui è
       arrivata la domanda), tipi di query generali (``?APRS?``, e dove
       compilati, ``?WX?``/``?IGATE?``),
       abilitazione query dirette, insieme esteso di query dirette, intervallo
       minimo di risposta (soglia di sicurezza contro loop/uso del canale), e il
       beacon periodico delle capacità di stazione: abilitazione, intervallo,
       selezione dei canali RF e APRS-IS, ed eventuali elementi di capacità
       aggiuntivi da accodare.
   * - **Radio / Modem**
     - Modalità FX.25 (spento / solo RX / RX+TX); abilita modem audio, modulazione
       (300 / 1200 Bell202 / 1200 V.23 / 9600 G3RUH), LPF audio (audio piatto), ms
       di preambolo, ms di slot temporale TX, buffer TX, ritenzione extra di
       dis-attivazione PTT, persistenza CSMA, e il limitatore di duty cycle a
       lungo termine (abilitazione più percentuale di tetto); e il pulsante
       **LOOP TEST**. Salva riapplica il modem in tempo reale — nessun riavvio.
   * - **Wireless**
     - Modalità (off/STA/AP/AP+STA), SSID/pass/canale dell'AP, 5 slot STA ciascuno
       con la propria casella Enable, potenza TX in dBm, più una scansione in
       tempo reale.
   * - **System**
     - Login web, frequenza CPU (applicata in tempo reale) e una sezione
       *Time*: abilitazione NTP, host NTP ×3, intervallo di risincronizzazione,
       e un selettore di fuso orario che imposta la data/ora locale mostrata
       nella dashboard (l'orologio stesso resta UTC). Inoltre il pulsante di
       reset di fabbrica.
   * - **Storage**
     - Navigatore LittleFS: scarica, elimina, upload multipart, uso, formatta.
   * - **About / Firmware**
     - Nome del progetto, versione, data/ora di compilazione, versione di IDF,
       partizione in esecuzione, e il pannello di **OTA Update**.

.. note::

   Ogni controllo di queste pagine governa comportamento reale: un'impostazione
   che arriva in ``config.json`` viene letta dal servizio che la possiede. Il
   digipeater gestisce sempre WIDEn-N e ripete senza ritardo aggiunto, quindi
   nessuno dei due viene offerto come opzione.

   La soppressione dei duplicati ha esattamente una coppia di controlli, *Dup
   cache size* (``dupCacheSize``) e *Dup cache timeout* (``dupCacheTimeoutMs``)
   sulla pagina *IGate*, e governano tanto il digipeater quanto l'IGate:
   entrambi i servizi condividono l'unica cache di ``components/igate``,
   ciascuno con il proprio ambito.

Le statistiche della dashboard
==============================

Le statistiche vengono da ``aprs_service_get_stats()``, tracciate in modo
**indipendente** da ``igate_en``/``digi_en``:

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Contatore
     - Significato
   * - ``radio_rx``
     - Ogni frame che il modem ha decodificato da RF.
   * - ``radio_tx``
     - Ogni frame trasmesso con successo in RF.
   * - ``rf2inet``
     - Frame che l'IGate ha effettivamente caricato.
   * - ``inet2rf``
     - Righe da APRS-IS effettivamente trasmesse in RF.
   * - ``digi``
     - Frame digipetati (percorso riscritto + ritrasmesso).
   * - ``drop`` / ``err``
     - Frame scartati / che hanno fallito la decodifica, a livello di
       RX/servizio.
   * - ``tx_queue_depth`` / ``tx_queue_limit``
     - L'arretrato attuale dell'anello TX RF e il tetto effettivo di *TX buffers*,
       così che la dashboard si legga come la riga "n/n in attesa" della console.
   * - ``csma_busy_forced`` / ``csma_persist_forced``
     - Quante volte il limite anti-starvation di otto slot ha forzato una
       trasmissione, distinguendo se qualche slot ha visto il canale occupato o
       se tutti lo hanno trovato libero. Mostrato come *CSMA FORZATO
       (OCCUP./PERSIST.)*. Sono trasmissioni, non scarti.
   * - ``tx_duty_cycle_pct`` / ``duty_cycle_limit_pct``
     - Duty cycle di trasmissione misurato sulla finestra scorrevole di 10
       minuti rispetto al tetto configurato, come *CICLO DI LAVORO TX*. Il
       limite vale ``0`` quando il limitatore è spento, mentre la misura è
       popolata in ogni caso.

Questo è deliberato. Con entrambe le funzioni disattivate (una configurazione
comune di solo-RX/monitor) la dashboard resterebbe inchiodata a zero per quanto
traffico venisse decodificato.

Feed in tempo reale
===================

* ``/lastheard`` — la tabella LAST HEARD (JSON), alimentata sia da RF sia da
  APRS-IS. Una stazione ascoltata per l'ultima volta prima che NTP
  sincronizzasse porta il campo ``time`` vuoto: quando la trama è arrivata
  l'orologio contava ancora dall'epoca, quindi non c'è alcuna ora del giorno da
  indicare e nessuna viene inventata.
* ``/igate_traffic?since=<seq>`` — il delta del log di traffico (JSON). Ogni voce
  porta un'etichetta di direzione (``RX``/``TX``/``DIGI``/``INET2RF``/``RX-IS``),
  l'indicativo DX, il pacchetto grezzo, il riepilogo dei campi decodificati
  (``dec``, vuoto quando il payload non ne porta nessuno), e il livello audio in
  mV RMS (o −1). Con *Registra dopo i filtri* attivo nella pagina IGate, le voci
  ``RX`` e ``RX-IS`` coprono solo il traffico che i filtri di questa stazione
  accettano — vedi :ref:`it-igate`. Il
  corpo viene trasmesso una voce per chunk HTTP, così un client molto arretrato
  riceve comunque tutte le righe memorizzate: la risposta non ha un tetto di
  dimensione e il firmware non assembla mai l'intero documento in RAM. Il
  ``seq`` restituito è il numero di sequenza dell'ultima voce effettivamente
  consegnata, quindi il cursore può avanzare solo oltre le righe che il client
  ha ricevuto; un cursore davanti al ring — il dispositivo si è riavviato e la
  numerazione è ripartita da 1 — rinvia dalla voce più vecchia ancora
  memorizzata.
* ``/dashinfo``, ``/sidebarInfo``, ``/heapinfo`` — frammenti compatti di info in
  tempo reale.

Vedi :ref:`it-http-routes` per la tabella completa delle route.

Politica di blocco del login
=============================

``web_check_auth()`` tiene traccia dei tentativi di Basic Auth falliti per
indirizzo IPv4 di origine in una piccola tabella di dimensione fissa
(``components/webconfig/web_common.c``). Conta come fallimento solo una
richiesta che ha effettivamente presentato credenziali e che è stata
rifiutata — un payload Basic malformato, oppure una coppia utente/password
errata. Una richiesta senza intestazione ``Authorization``, o con
un'intestazione che non è ``Basic``, è la metà senza credenziali dell'handshake
Basic Auth che ogni browser esegue da sé, e riceve una risposta ``401`` senza
essere addebitata sul budget; è questo che permette ai poller autenticati
della dashboard (``/dashinfo``, ``/sidebarInfo``, ``/heapinfo``,
``/lastheard``, ``/igate_traffic``) di trovarsi davanti a una nuova pagina di
login senza mai far scattare da soli un blocco.

Dopo 5 credenziali rifiutate consecutive dalla stessa origine, quell'origine
viene bloccata e ogni richiesta successiva riceve ``429 Too Many Requests``
con un'intestazione ``Retry-After`` invece di un ``401``, per una finestra che
parte da 5 s e raddoppia a ogni ulteriore tentativo rifiutato mentre il blocco
è ancora attivo, con un tetto di 300 s. Una finestra che scade senza un login
riuscito viene riarmata un fallimento sotto la soglia invece di riprendere dal
conteggio accumulato, così un client che continua a riprovare le stesse
credenziali scadute dopo ogni scadenza fa scattare di nuovo solo il blocco
base di 5 s ogni volta, invece di risalire direttamente al tetto di 300 s. Un
login riuscito azzera completamente la voce di quell'origine.

Protezione same-origin (CSRF)
==============================

``web_check_auth()`` applica anche un controllo di stessa origine su ogni
richiesta ``HTTP_POST``, indipendentemente dal fatto che
``g_config.http_username`` sia impostato o meno. Il controllo verifica che
l'intestazione ``Origin`` della richiesta (con fallback su ``Referer``)
indichi l'``Host`` di questo stesso dispositivo prima che venga eseguito
qualsiasi altro codice, e fallisce in modo chiuso: una richiesta priva di
entrambe le intestazioni, o con una che non corrisponde, viene rifiutata con
``403 Forbidden`` indipendentemente dalle credenziali che porta.

Questo è deliberatamente indipendente da Basic Auth. Lasciare vuoto il nome
utente nella pagina System è un modo supportato per eseguire il pannello di
amministrazione senza password, ma disattiva solo la richiesta di login —
non allenta il requisito di stessa origine, perché una richiesta cross-site
originata dal browser è una minaccia con o senza password configurata:
senza password non c'è alcuna credenziale da rubare, ma la pagina
dell'attaccante può comunque far inviare al browser dell'operatore una
richiesta che modifica lo stato del dispositivo per suo conto. Ogni route
che modifica lo stato (``/ota_update``, ``/format``, ``/upload``,
``/delete``, ``/msgchat``, e il gestore di salvataggio di ogni pagina di
configurazione) è registrata come ``HTTP_POST`` esattamente per questo
motivo; nessuna route ``GET`` registrata ha effetti collaterali, quindi
questo controllo non deve mai intervenire su una normale navigazione, un
segnalibro o un URL digitato a mano.

.. seealso::

   :ref:`it-telegram` — il sottosistema del bot Telegram dietro la pagina
   *Telegram*: il proprio file di configurazione, l'avvio supervisionato e
   il set di comandi integrati.
