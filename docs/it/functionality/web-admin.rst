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
       tabella di traffico in tempo reale (DX / PACKET / AUDIO) alimentata da
       long-poll basato su sequenza.
   * - **Station**
     - L'identità condivisa della propria stazione che ogni beacon, oggetto e
       messaggio legge: indicativo, latitudine, longitudine, altitudine
       (``g_config.my_*``), più le due opzioni di precisione in onda valide per
       tutta la stazione: ambiguità di posizione e prefisso del localizzatore
       Maidenhead nei rapporti di stato.
   * - **IGate**
     - Abilita, RF→INET / INET→RF, entrambe le maschere di filtro, budlist e gate
       di portata/prefisso, indicativo/SSID/passcode, quattro riquadri *APRS-IS
       Server* (ciascuno con casella Abilita più host e porta, usati come
       rotazione di failover), stringa di
       filtro server, beacon on/off, posizione, intervallo, selettore di simbolo,
       oggetto, commento, stato, PHG. *Filtraggio Messaggi* contiene
       l'interruttore dei criteri per i messaggi INET→RF e la finestra di
       ascolto locale.
   * - **Digi**
     - Abilita digipeater, indicativo/SSID e impostazioni beacon (posizione,
       simbolo, intervallo, commento, stato, percorso). *Alias di Percorso n-N*
       contiene le quattro righe di {alias, N massimo, modalità} con cui il
       digipeater ripete, l'interruttore di solo riempimento, la scelta di cosa
       fare con un conteggio hop intrappolato e l'interruttore *Ripetizione
       tramite SSID di destinazione (legacy)*, disattivato per impostazione
       predefinita. Contiene anche i quattro preset
       di percorso condivisi ``path[0..3]`` tra cui sceglie ogni servizio che
       trasmette. La finestra di soppressione dei duplicati è un unico
       controllo, sulla pagina *IGate*.
   * - **Tracker**
     - Abilita tracker, indicativo/SSID, intervallo fisso, posizione, simbolo di
       stazione, commento, opzioni di posizione compressa, posizione Mic-E e
       altitudine.
   * - **Weather**
     - Abilita, invia-in-RF/-INET, timestamp, indicativo/SSID/percorso WX,
       posizione, nome oggetto, commento, caselle *Averaged* per campo, e — per
       ogni campo WX in onda — un **menu a tendina di canale** riempito in tempo
       reale dal registro ``sensors_local`` e filtrato per le capacità pubblicate
       di ogni driver. Valori in tempo reale via ``/wx/values``.
   * - **Telemetry**
     - Parametri di beacon/report, interruttori dei messaggi di definizione,
       analogici A1–A5 con selettori di origine e calibrazione, digitali B1–B8 con
       selettori di origine e senso. Valori in tempo reale via ``/tlm/values``.
   * - **Bulletins**
     - Fino a cinque bollettini (identificatore e gruppo del destinatario,
       testo, RF/INET, intervallo, scadenza).
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
       minimo di risposta (soglia di sicurezza contro loop/uso del canale).
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
  APRS-IS.
* ``/igate_traffic?since=<seq>`` — il delta del log di traffico (JSON). Ogni voce
  porta un'etichetta di direzione (``RX``/``TX``/``DIGI``/``INET2RF``/``RX-IS``),
  l'indicativo DX, il pacchetto grezzo, e il livello audio in mV RMS (o −1). Il
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
