.. _it-igate:

=======================
IGate — gateway APRS-IS
=======================

Il componente ``igate`` (``components/igate/``) è un gateway Internet APRS-IS
bidirezionale completo, costruito su socket LWIP. Legge tutta la sua
configurazione da ``g_config`` (la pagina *IGate* dell'amministrazione web),
quindi l'amministrazione web è l'unica fonte di verità.

Il task client APRS-IS
======================

* **Client TCP** con failover multiserver e riconnessione automatica. Rilegge
  ``g_config`` a ogni riconnessione, quindi la maggior parte delle modifiche dal
  web ha effetto dopo il ciclo di riconnessione successivo, senza riavvio.
* **Condizionato alla connettività reale**, non solo al fatto che "il Wi-Fi è
  attivo": interroga ``net_state_is_connected()``, che diventa vero solo con
  ``IP_EVENT_STA_GOT_IP`` e falso di nuovo alla disconnessione o in modalità
  solo-AP.
* **Riga di login:** ``user <mycall> pass <passcode> vers esp32_APRS_igate
  <versione>``, con `` filter <filter>`` aggiunto solo quando è configurato un
  filtro lato server — il comando ``filter`` richiede uno o più termini, per
  cui la clausola viene omessa del tutto invece di essere inviata come parola
  chiave nuda. Nome e versione vengono da ``APRS_SOFTWARE_NAME`` /
  ``APRS_SOFTWARE_VERSION`` in ``main/include/aprs_service.h`` (la seconda è
  ``FIRMWARE_INFO``), così che la clausola ``vers`` identifichi *questo*
  firmware agli operatori dei server APRS-IS. La riga è registrata esattamente
  come viene inviata (senza il CR/LF), così che un filtro malformato sia
  visibile; senza filtro configurato una seconda riga segnala che vale il
  valore predefinito del server. Il banner del server e la riga
  ``# logresp … verified/unverified`` vengono mostrati; una risposta
  ``unverified`` genera un avviso che nomina ``aprs_mycall`` /
  ``aprs_passcode``.
* **Validazione del filtro lato server.** Prima di essere inviato,
  ``g_config.aprs_filter`` è controllato strutturalmente da
  ``aprs_filter_validate_server_string()`` — ogni termine separato da spazi deve
  essere ``<lettera>/<argomenti>`` con il numero di argomenti corretto per quella
  lettera di filtro.
* **Uplink condiviso.** Il task è sempre in esecuzione, perché lo stesso socket è
  usato dal componente di messaggistica (``igate_send_raw()``) e dal "beacon a
  internet". Resta inattivo a basso costo quando niente lo richiede.
* **Rilevamento del collegamento morto.** ``net_state_is_connected()`` rileva
  solo la caduta del Wi-Fi della stazione stessa; non dice nulla sul fatto che
  l'altro capo di un socket APRS-IS già aperto sia rimasto muto — una voce
  NAT/firewall di un TCP inattivo che viene espulsa, una rotta bloccata, o un
  peer che smette di inviare senza mai chiudere la connessione. Il ciclo di
  ricezione tiene traccia dell'istante dell'ultimo byte effettivamente letto
  dal socket e, se non arriva nulla per ``IGATE_RX_SILENCE_US`` (90 s),
  registra un avviso e chiude il socket così da far partire il normale
  percorso di riconnessione. 90 s resta comodamente sopra la cadenza delle
  righe di commento ``#`` che i server conformi alla `guida di connessione di
  aprs-is.net <https://www.aprs-is.net/Connecting.aspx>`_ inviano quando il
  canale è altrimenti silenzioso — è proprio quella riga di commento a
  impedire che un collegamento sano ma inattivo faccia scattare il timer —
  restando comunque abbastanza breve da recuperare ben entro il tempo di
  espulsione di una tipica voce NAT. Il socket porta anche ``SO_KEEPALIVE``
  (30 s di inattività, intervallo di 10 s, 3 sonde) come backstop indipendente
  a livello più basso; completa il timer lato ricezione senza sostituirlo,
  perché un peer che continua a confermare le sonde TCP ma smette di inviare
  dati applicativi altrimenti gli sfuggirebbe.
* **Algoritmo di Nagle disabilitato (``TCP_NODELAY``).** Impostato sul socket
  prima di ``connect()``, come richiesto dalla `guida di connessione di
  aprs-is.net <https://www.aprs-is.net/Connecting.aspx>`_ per qualsiasi
  client bidirezionale. Ogni riga in uscita — una trama gatewata da RF, un
  messaggio in uscita, un beacon — viene assemblata insieme al proprio
  terminatore CR/LF e scritta con un unico ``send()`` in ``sendToAprsIs()``,
  così con Nagle disabilitato quella singola scrittura raggiunge subito il
  socket invece di attendere un ACK o il timeout di Nagle.

Server failover
===============

La pagina IGate memorizza ``APRS_SERVER_NUM`` (quattro) slot server in
``g_config.aprs_server[]``, ciascuno con la propria casella Abilita, host e
porta. Tutti gli slot condividono la stessa identità di login — indicativo, SSID,
passcode e stringa di filtro sono uno solo — perché rappresentano la stessa
stazione che si connette a server APRS-IS alternativi.

``connectAprsIs()`` prova lo slot selezionato in quel momento. Qualsiasi
fallimento — lookup DNS, ``socket()``, ``connect()`` o l'invio della riga di
login — chiama ``advanceServer()``, che sposta la selezione allo slot
**abilitato** successivo con avvolgimento circolare, e il task attende 1 secondo
prima del tentativo seguente. La rotazione non si ferma mai: continua a
percorrere tutti gli slot abilitati finché uno accetta la connessione.

Gli slot disabilitati vengono saltati anche alla **prima** selezione dopo
l'avvio, non solo dopo un fallimento: togliere la spunta a uno slot lo mette
fuori servizio immediatamente. Se nessuno slot è abilitato il task ripiega sullo
slot 1, così ha sempre una destinazione concreta da tentare e registrare.

Una connessione già stabilita non fa ruotare la selezione: se il server chiude
il collegamento, il task riprova prima lo stesso slot, e passa al successivo
solo quando anche quel nuovo tentativo fallisce.

La dashboard mostra host e porta dello slot in uso in quel momento
(``igate_get_current_server()``), quindi si vede subito su quale server si è
finiti dopo un failover.

RF → INET (``igateProcess()``)
==============================

Ogni frame decodificato da RF che l'applicazione smista (con ``igate_en`` e
``rf2inet`` attivi) attraversa questa pipeline, in ordine. Un frame che fallisce
qualsiasi fase viene scartato, e la *ragione* è registrata su un contatore
per-ragione così che la dashboard possa mostrare "N scartati per X" invece di un
singolo aggregato opaco.

#. **Soppressione duplicati.** Il frame è controllato contro la cache dei
   duplicati condivisa (``isDuplicatePacket()``). Sia la sua profondità
   (``g_config.dup_cache_size``, ``DUP_CACHE_SIZE_MIN``..``DUP_CACHE_SIZE_MAX``
   = 4..40, predefinito 20) sia la sua finestra
   (``g_config.dup_cache_timeout_ms``, 1000..120000 ms, predefinito 30000) sono
   modificabili nella pagina *IGate* e vengono rilette a ogni ricerca, quindi
   una modifica si applica senza riavvio. L'array è sempre allocato alla
   capacità di compilazione ``DUP_CACHE_SIZE_MAX``; ``dup_cache_size`` sceglie
   solo quanta parte usarne. I duplicati sono contati a parte in
   ``dupCount``.
#. **Guardia di frame troppo corto.** I frame il cui campo info è sotto la
   lunghezza minima utilizzabile vengono scartati (``DROP_TOO_SHORT``).
#. **Filtro di token di percorso.** I frame il cui percorso porta ``RFONLY``,
   ``TCPIP``, ``qA*`` o ``NOGATE`` non vengono mai inoltrati (``DROP_PATH_TOKEN``).
#. **Regola di gate satellitare.** Un frame ripetuto tramite un gateway
   satellitare noto il cui indicativo non è marcato come usato (``*``) viene
   scartato (``DROP_SAT_NOT_USED``).
#. **Unwrap di terze parti (``}``).** Un frame il cui campo informativo inizia
   con ``}`` porta una propria riga interna completa
   ``SRC>DST,PATH:payload``. Se quel percorso interno porta già ``TCPIP`` o
   ``TCPXX``, il frame ha già raggiunto APRS-IS una volta e viene scartato
   come loop (``DROP_3RDPARTY_LOOP``). Altrimenti l'intestazione RF esterna
   viene scartata del tutto e tutte le fasi restanti — dal filtro per tipo di
   payload in poi — vengono eseguite contro il pacchetto interno: la sua
   propria origine, destinazione, percorso e payload, esattamente come se
   quella stazione fosse stata ascoltata direttamente. Questo è ciò che
   permette a un gateway cross-band o HF di ritrasmettere una stazione che
   non ha altra via verso Internet.
#. **Gate di query generica.** Un payload il cui primo byte è ``?``
   (``?APRS?``, ``?WX?``, ``?IGATE?``, …) viene scartato incondizionatamente
   (``DROP_GENERIC_QUERY``), indipendentemente da ``g_config.rf2inetFilter``
   o da qualsiasi altra casella. Vedi :ref:`it-filtering`.
#. **Filtro per tipo di payload.** Il payload (eventualmente spacchettato) è
   classificato da ``aprs_filter_classify_info()`` e testato contro
   ``g_config.rf2inetFilter`` (``DROP_TYPE_FILTER``). Vedi :ref:`it-filtering`.
#. **Gate di portata locale.** Se abilitato, la posizione del pacchetto è
   decodificata e la sua distanza sul cerchio massimo (haversine) da "My Station"
   è confrontata con ``g_config.rf2inet_range_km``; i pacchetti troppo distanti
   vengono scartati (``DROP_RANGE_FILTER``). I pacchetti la cui posizione non può
   essere decodificata passano questo controllo.
#. **Gate di prefisso locale.** Se abilitato, l'indicativo di origine deve
   iniziare con uno dei prefissi separati da virgole in
   ``g_config.rf2inet_prefixes`` (es. ``EA,EB,EC``), altrimenti viene scartato
   (``DROP_PREFIX_FILTER``).
#. **Budlist.** L'indicativo di origine è testato contro la whitelist/blacklist
   locale in ``g_config.rf2inet_budlist_mode`` (``DROP_BUDLIST``).

Un frame che sopravvive a tutte le fasi riceve un'intestazione
``,qAR,<mycall>-<ssid>`` oppure ``,qAO,<mycall>-<ssid>`` ed è scritto su
APRS-IS. Secondo QCON il costrutto descrive la **stazione inoltrata**, non il
gateway: ``qAO`` segnala una stazione a cui questo IGate non consegnerebbe un
messaggio, ed è così che lo leggono i consumatori a valle (router di messaggi,
l'indicazione "messageable" sui siti di mappe APRS-IS). Per questo
``qConstructFor()`` sceglie ``qAR`` solo quando valgono entrambe le
condizioni:

* questa stazione può inoltrare messaggi verso RF
  (``aprs_service_can_gate_to_rf()``: trasmissione disponibile, ``igate_en``
  attivo, ``inet2rf`` attivo), e
* la stazione inoltrata **non** è stata vista su APRS-IS entro
  ``igate_local_window_sec`` — la stessa condizione che ``messageGatePass()``
  applica al destinatario nella direzione INET → RF, dato che una stazione
  connessa a Internet ha già tutto ciò che le è indirizzato.

Tutto il resto riceve ``qAO``, quindi un IGate a sola ricezione invia ``qAO``
per ogni pacchetto. Il nominativo-SSID che segue il q construct è sempre
l'identità di login di questa stazione.

INET → RF (``inet2rfHandler()``)
================================

Ogni riga diversa da ``#`` letta dal socket incrementa ``isRxCount`` ed è
consegnata al motore di messaggistica (``handleIncomingAPRS()``) quando la
messaggistica è attiva. È poi considerata per la ritrasmissione in RF solo se
``inet2rf`` è impostato, e solo dopo aver superato:

#. **Gate di query generica.** Una riga il cui payload inizia con ``?`` viene
   scartata incondizionatamente (``DROP_GENERIC_QUERY``), indipendentemente
   da ``g_config.inet2rfFilter`` o da qualsiasi altra casella — l'immagine
   speculare del gate di query generica RF→INET sopra, verificata prima di
   ogni altra fase seguente. Vedi :ref:`it-filtering`.
#. **Soppressione dell'eco dei report propri.** Ogni report che questa stazione
   carica con il suo flag ``*_2inet`` viene rimandato indietro come eco
   direttamente dal server APRS-IS. ``inet_line_is_own_report()`` riconosce quegli
   echi (confrontando l'indicativo base di origine contro ogni indicativo di
   report della propria stazione) e non li reinoltra mai in RF. I report propri
   raggiungono RF esclusivamente tramite i loro flag "Send via RF" (``*_2rf``).
#. **Filtro per tipo di payload.** La riga è classificata da
   ``aprs_filter_classify_tnc2()`` e testata contro ``g_config.inet2rfFilter``.
#. **Unwrap selettivo di terze parti (opzionale).** Il traffico di terze parti
   (``}``) — la classica fonte di loop IGate — classifica come 0 e non viene mai
   ritrasmesso di default. Con ``inet2rf_3rdparty_unwrap_en`` attivo **e**
   ``inet2rf_budlist_mode == BUDLIST_WHITELIST``, un livello di incapsulamento
   ``}`` può essere spacchettato e il pacchetto interno riclassificato e
   ritrasmesso, ma *solo* quando l'origine del pacchetto interno è essa stessa
   nella whitelist. Non è mai un interruttore generale di "ritrasmetti tutto il
   traffico di terze parti".
#. **Budlist.** L'indicativo di origine (che qui può portare un ``-SSID``) è
   testato contro ``g_config.inet2rf_budlist_mode``.
#. **Filtraggio dei messaggi.** Si applica al solo tipo ``MESSAGE``; gli altri
   tipi sono ritrasmessi a discrezione del sysop, che è ciò che il filtro dei
   tipi e la budlist qui sopra già esprimono. Vedi sotto.

Una riga che supera tutte le fasi non viene mai trasmessa in RF con la sua
intestazione APRS-IS intatta. ``build_thirdparty_frame()`` scarta del tutto
quell'intestazione e avvolge l'originale ``SRC>DST`` e il campo informativo,
inalterati, dietro un ``}`` come payload dell'intestazione propria di questa
stazione (``MYCALL[-SSID]>APE32L,<percorso igate>:}SRC>DST,TCPIP,
MYCALL[-SSID]*:info``) — la forma di terze parti richiesta dalla
specifica APRS per il traffico ritrasmesso. Questo mantiene i costrutti
``qA`` e un ``TCPIP`` non incapsulato fuori dall'etere, e permette a
qualsiasi altro IGate che ascolti il pacchetto di riconoscerlo come già
ritrasmesso invece di rimandarlo indietro.

.. warning::

   Reinoltrare il traffico di terze parti senza restrizioni è la causa numero uno
   di loop IGate. L'unwrap di terze parti è deliberatamente condizionato a
   un'opzione esplicita *e* a una whitelist proprio per questa ragione.

Filtraggio dei messaggi
=======================

Un IGate è affacciato su un flusso di dati enorme e non deve ritrasmettere in
modo indiscriminato. Con ``igate_msg_gate_en`` attivo (il valore di fabbrica),
un messaggio APRS letto da APRS-IS viene trasmesso solo se valgono **tutte e
quattro** le condizioni insieme:

.. list-table::
   :header-rows: 1
   :widths: 46 54

   * - Condizione
     - Motivo di scarto quando fallisce
   * - L'intestazione del mittente non contiene ``TCPXX``, ``NOGATE``, ``RFONLY``
     - ``DROP_MSG_NOGATE``
   * - Il destinatario è stato ascoltato in RF entro ``igate_local_window_sec``
     - ``DROP_MSG_NOT_LOCAL``
   * - Il destinatario non è a sua volta connesso a Internet
     - ``DROP_MSG_ADDRESSEE_INET``
   * - Il mittente **non** è stato ascoltato in RF entro la stessa finestra
     - ``DROP_MSG_SENDER_LOCAL``

Ogni fallimento ha il proprio motivo, così il *Drop Breakdown* del cruscotto
dice quale condizione ha fermato un messaggio — la domanda di assistenza più
frequente su un IGate. I token ``TCPXX``/``NOGATE``/``RFONLY`` sono cercati solo
nell'intestazione, quindi un messaggio il cui *testo* ne menzioni uno non viene
scambiato per uno instradato con esso.

Le prove di località leggono ``lastheard_heard_rf_within()`` e
``lastheard_heard_inet_within()``, che tengono una marca temporale per canale:
una stazione può essere udibile localmente e connessa a Internet allo stesso
tempo, e ogni condizione prova la propria. Una trama ascoltata via radio conta
anche come avvistamento Internet quando il suo percorso porta ``TCPIP`` o
``TCPXX`` — la firma via radio di un pacchetto già passato per un gateway.

*Finestra di ascolto locale (s)* è ``igate_local_window_sec``, 60–3600 s, un'ora
per impostazione predefinita, che è il limite superiore raccomandato dalle note
di progetto degli IGate APRS-IS.

Disattivare il filtraggio dei messaggi trasmette **ogni** messaggio consentito
dal filtro dei tipi, verso destinatari in qualsiasi parte del mondo, che sul
canale locale ci sia o meno qualcuno in grado di sentirli.

Posizione associata
===================

Invece di ritrasmettere i rapporti di posizione storici di una stazione, il
gateway annota le stazioni **a cui** ha ritrasmesso un messaggio — un anello di
otto voci — e inoltra il primo rapporto di posizione semplice o di boa che vede
per ciascuna di esse, qualunque cosa dica il filtro dei tipi, così l'operatore
locale ha qualcosa da posizionare per l'altro capo della conversazione. Quel
singolo rapporto libera la voce, ed è questo a renderlo un seguito e non un
abbonamento; un rapporto meteo o un oggetto viene ritrasmesso sotto il proprio
bit di tipo, per meriti propri.

Contatori e ragioni di scarto
=============================

Lo snapshot ``igate_stats_t`` (``igate_get_stats()``) porta:

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Contatore
     - Significato
   * - ``rxCount``
     - Frame considerati per l'inoltro (RF→INET).
   * - ``txCount``
     - Frame effettivamente inviati ad APRS-IS come risultato dell'inoltro.
   * - ``msgCount``
     - Pacchetti di messaggio APRS (identificatore di tipo dato ``:``) inoltrati
       in entrambe le direzioni — RF→INET da ``igateProcess()``, INET→RF da
       ``igate_note_message_gated()`` da ``aprs_service.c``. È la cifra
       ``MSG_CNT`` riportata dalla risposta a ``?IGATE?``, quindi conta solo i
       messaggi e non il resto del traffico inoltrato.
   * - ``dupCount``
     - Frame duplicati soppressi.
   * - ``isRxCount``
     - **Tutte** le righe lette dal socket (soprainsieme di ciò che raggiunge il
       gestore INET→RF).
   * - ``isTxCount``
     - **Tutte** le scritture sul socket: frame inoltrati, messaggi in uscita e
       invii "beacon a internet" del digi allo stesso modo.
   * - ``dropByReason[]``
     - Contatori di scarto per-ragione, indicizzati da ``drop_reason_t``. Le
       fasi RF→INET sopra coprono ``DROP_TOO_SHORT``, ``DROP_PATH_TOKEN``,
       ``DROP_SAT_NOT_USED``, ``DROP_3RDPARTY_LOOP``, ``DROP_GENERIC_QUERY``,
       ``DROP_TYPE_FILTER``, ``DROP_RANGE_FILTER``, ``DROP_PREFIX_FILTER``,
       ``DROP_BUDLIST`` e ``DROP_TX_FAIL``; l'array
       porta anche ragioni incrementate altrove nel firmware (percorso TX RF,
       digipeater, decodifica AX.25) — vedere ``drop_reason_t`` in
       ``components/igate/include/igate.h`` per l'elenco completo e
       autorevole. Non esiste una ragione generica/opaca di "altro": ogni
       scarto è attribuito a una causa specifica e nominata.
       ``igate_stats_total_drop()`` somma le ragioni non di errore;
       ``igate_stats_total_err()`` somma separatamente le due ragioni di
       errore di decodifica/invio.

``igate_note_drop()`` è esposto così che altri componenti che condividono gli
stessi concetti di filtraggio — attualmente il gestore INET→RF di
``aprs_service.c``, per i suoi controlli di filtro-tipo e budlist — contribuiscano
alla stessa suddivisione per-ragione.

Indicatore di connettività
==========================

``igate_is_connected()`` è vero mentre il socket TCP APRS-IS è aperto, con login
effettuato e con il lettore di righe RX in funzione. Il pannello *Network Status*
della dashboard web (la pillola APRS-IS) lo legge. Poiché il ciclo di ricezione
chiude il socket non appena scatta il rilevamento del collegamento morto (vedi
sopra), questo restituisce falso anche per l'intero intervallo tra una caduta
silenziosa del collegamento e il successivo re-login riuscito, invece di
continuare a mostrare "connesso" su un socket che ha già smesso di consegnare
qualsiasi cosa.
