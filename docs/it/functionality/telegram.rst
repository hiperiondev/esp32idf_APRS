.. _it-telegram:

============
Bot Telegram
============

La stazione può eseguire, facoltativamente, un bot Telegram accanto ai suoi
servizi APRS, così un operatore può controllare e comandare leggermente la
stazione da un telefono senza aprire il pannello web. È costruito da due
componenti sovrapposti:

* ``esp_telegram_bot`` — il trasporto HTTPS: il token del bot, gli URL della
  Bot API di Telegram, il client TLS e l'upload multipart.
* ``telegram_service`` — long polling, dispatch dei comandi, autorizzazione
  per utente/chat, avvisi e parametri remoti, costruito su quel trasporto.

``main/telegram_app.c`` è la colla tra questi due componenti e questo
firmware: possiede l'archivio JSON proprio del bot, lo avvia e lo ferma in
modo supervisionato, e pubblica una diagnosi che la pagina web *Telegram*
mostra come frase tradotta.

Il proprio file di configurazione
====================================

Tutto ciò di cui il bot ha bisogno risiede in ``/storage/telegram.json``, non
in ``config.json``:

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Campo
     - Significato
   * - ``enabled``
     - L'interruttore disegnato dalla pagina Telegram. L'unica chiave che
       questo firmware aggiunge all'archivio; un file scritto a mano che la
       omette si carica con il bot spento.
   * - ``routeStationMessages``
     - L'interruttore "Inoltra messaggi della stazione" della pagina
       Telegram. Acceso, ogni messaggio APRS indirizzato al nominativo proprio
       di un utente autorizzato viene anche inviato alla chat Telegram di
       quell'utente, che sia ricevuto dalla rete o originato qui dalla pagina
       *Snd/Rcv Msg*; vedi `Inoltro dei messaggi della stazione a Telegram`_
       più sotto. Assente, come ``enabled``, si carica spento.
   * - ``routeBulletins``
     - L'interruttore "Inoltra bollettini" della pagina Telegram. Acceso,
       ogni bollettino APRS che questa stazione tratta - ricevuto dalla rete o
       trasmesso dalla sua stessa pagina *Bulletins* - viene inviato a tutti
       gli utenti autorizzati, all'amministratore e a tutte le chat di gruppo
       consentite; vedi `Inoltro dei bollettini a Telegram`_ più sotto.
       Assente, come ``enabled``, si carica spento.
   * - ``bulletinWindowSeconds``
     - Il campo "Finestra di ripetizione dei bollettini" della pagina
       Telegram: per quanti secondi un bollettino già inoltrato impedisce
       l'inoltro anche delle proprie ripetizioni, da 0 a 86400. 0 inoltra ogni
       copia. A differenza degli interruttori qui sopra, una chiave assente si
       carica con il valore predefinito di 900 s e non come 0, perché qui 0 è
       un'impostazione legittima e significa l'opposto di "lascia stare".
   * - Token del bot
     - Il token rilasciato da `@BotFather <https://t.me/BotFather>`__.
   * - Identificativo dell'amministratore
     - L'identificativo numerico utente Telegram dell'amministratore della
       stazione. ``telegram_init()`` lo aggiunge da sé alla lista degli
       utenti autorizzati.
   * - Indirizzo della Mini App
     - Indirizzo HTTPS facoltativo di una Mini App Telegram aperta dal
       pulsante di menu del bot.
   * - Utenti autorizzati / chat di gruppo consentite
     - Fino a 8 utenti autorizzati e 4 chat di gruppo consentite, ciascuno con
       un identificativo e un nome visualizzato.

Tutti i campi sopra sono modificabili dalla pagina *Telegram*, che carica
l'intera struttura prima di un salvataggio e la riscrive con le modifiche del
modulo applicate, così l'intera configurazione è anche un unico file che può
essere scaricato, modificato a mano e ricaricato dalla pagina *File Storage*
(:ref:`it-storage-ota`).

Gli identificativi Telegram sono a 64 bit e con segno (l'identificativo di un
supergruppo è un numero negativo grande), quindi sia l'identificativo
dell'amministratore sia ogni voce utente/chat sono gestiti come ``int64_t``,
inviati dal modulo web come testo proprio per questo motivo.

Avvio supervisionato
======================

Attivare l'interruttore non chiama direttamente il servizio. Invece
``telegram_app_apply_config()`` avvia un piccolo task supervisore, perché un
avvio di Telegram può fallire per un motivo che va distinto dagli altri:

#. **Connettività di rete.** Controllata per prima e a basso costo, prima che
   venga allocato altro, così una stazione senza percorso verso internet lo
   segnala subito.
#. **Certificato radice.** Quando il trasporto non è compilato con il bundle
   di certificati di ESP-IDF, serve un file PEM sulla partizione di storage
   (``/storage/telegram_certificate.pem`` per impostazione predefinita, fino
   a 8 KB); un file mancante o non valido viene segnalato prima di qualsiasi
   tentativo di rete.
#. **Forma del token.** Il token deve avere la forma ``<cifre>:<segreto>``
   prima di essere inviato ovunque.
#. **Memoria.** Un buffer, una coda o una sessione TLS che non trovano posto
   vengono distinti da ogni altro fallimento, perché una diagnosi sbagliata
   manda l'operatore a cercare memoria che non era mai il problema. La soglia
   di heap libero viene verificata insieme a un lock condiviso che protegge
   anche la ``connect()`` TCP dell'uplink APRS-IS (:ref:`it-igate`), cosicché
   le due operazioni di rete più pesanti di questo firmware non vengono mai
   eseguite nello stesso istante; chi trova il lock già occupato riprova
   semplicemente al proprio ciclo successivo invece di competere per la stessa
   memoria.
#. **DNS e TCP.** La risoluzione di ``api.telegram.org`` e l'apertura della
   connessione TLS verso di esso vengono controllate separatamente.
#. **La risposta di Telegram stessa.** Un token dalla forma corretta ma non
   valido viene rilevato solo quando l'API stessa lo rifiuta.

Ogni tentativo inizia prendendo, sotto lock, un'istantanea della copia in
memoria di ``telegram.json`` — token, identificativo dell'amministratore,
utenti autorizzati e chat consentite — in una copia locale sullo stack da cui
il worker esegue il resto del tentativo. Un salvataggio effettuato dalla
pagina *Telegram* mentre un handshake è già in corso da decine di secondi non
raggiunge quindi mai il token né le tabelle che un tentativo in corso sta
usando; viene recepito normalmente dal tentativo successivo.

Sia l'avvio sia lo spegnimento avvengono fuori dal task chiamante, affidati a
``telegram_app_tick_1hz()`` (chiamata una volta al secondo), che avvia un
worker di breve durata solo quando è effettivamente dovuto. Questo mantiene
lo stack della dimensione richiesta da un handshake TLS (indicazione della
stessa Telegram: circa 8 KB è il minimo pratico) fuori sia dal gestore di
salvataggio del server web sia da qualsiasi task permanente, così attivare
l'interruttore sulla pagina *Telegram* non blocca mai il browser e non tiene
mai allocato quello stack mentre il bot è semplicemente in esecuzione.

C'è un solo task lavoratore, non uno per ciascun tipo di compito. Un avvio,
uno spegnimento e la consegna delle notifiche inoltrate (descritta più avanti)
sono tutti dimensionati per un handshake TLS, e sono tutti svolti dallo stesso
task invece che da uno per tipo, perché più di uno di quegli stack alla volta
è più di quanto questa scheda possa reggere accanto al task di polling del
servizio stesso, al task del server web e all'handshake medesimo. Quello
avviato per primo gira da solo, e appena finisce lo stesso task svuota la coda
delle notifiche prima di uscire, invece di avviarne un secondo per farlo. Un
avvio o uno spegnimento che trova il task lavoratore già in esecuzione resta
in sospeso e viene riproposto dal tick successivo, e una notifica messa in
coda mentre il task è occupato lascia la sua riga al suo posto fino
all'avvio che innescherà la prossima riga inoltrata, oppure fino allo
svuotamento che il task in esecuzione esegue uscendo. In nessuno dei due casi
si perde qualcosa; l'unico costo è un ritardo di un secondo o poco più.

Mentre il bot gira, lo stesso tick a 1 Hz ripubblica i suoi contatori e nota
un collegamento a internet scomparso o un task di polling terminato, così una
stazione che perde la connessione segnala "in attesa di un percorso di rete"
invece di un conteggio crescente e inspiegato di errori di polling.

Il riavvio è deliberatamente disabilitato (``allow_reboot = false``): questa
stazione porta un trasmettitore e uno scheduler con impegni a tempo, e un
riavvio è già disponibile, dietro il login stesso del pannello web, dalla
pagina *System*.

L'avviso di avvio proprio del servizio è spento
===============================================

Due comodità offerte dal servizio — pubblicare il proprio elenco di comandi
sull'interfaccia di Telegram, e annunciare all'avvio che il bot è ora attivo
— sono entrambe disattivate (``publish_commands = false``,
``announce_start = false``). Entrambe inviano nell'istante in cui la
connessione di polling si avvia, aprendo una seconda sessione TLS mentre i
buffer della prima sono ancora occupati; su una stazione la cui memoria porta
anche i buffer DMA del modem radio, quella seconda sessione non trova posto, e
l'unico effetto visibile sarebbe una coppia di errori nel log a ogni avvio.
L'elenco dei comandi non viene perso in alcun senso funzionale — ogni comando
funziona indipendentemente dal fatto che Telegram ne sia stato informato — e
l'annuncio di avvio è sostituito dall'avviso che questo firmware invia per
conto proprio, descritto qui sotto, che viaggia in un lotto di trasmissione
invece che in una sessione tutta sua.

Avviso di avvio
===============

Il primo avvio del bot che raggiunge Telegram dopo un'accensione o un reset
invia un messaggio a ogni utente autorizzato, all'amministratore e a ogni chat
di gruppo consentita:

.. code-block:: text

   START
   Reason: Power-on
   Station: LU3VEA-10
   Firmware: esp32_APRS_igate 1.0.0

La riga della causa è ``esp_reset_reason()``, formulata con la stessa tabella
che legge la striscia System Info del pannello
(``main/include/reset_reason.h``), così le due non possono scrivere in modo
diverso la stessa causa. Una stazione tornata da un panic, da un watchdog o da
un calo di tensione dice quale, e non soltanto che è tornata: un operatore che
non sta guardando il pannello web viene a sapere sia che una stazione lasciata
in funzione si è riavviata, sia che cosa l'ha riavviata.

L'invio avviene una volta per avvio, non una volta per messa in servizio del
bot. Il bot viene rimesso in servizio ogni volta che si salva la pagina
*Telegram*, e di nuovo quando una connessione caduta viene ricostruita, e un
avviso a ognuna di quelle occasioni segnalerebbe un riavvio mai avvenuto. Il
fermo che lo impedisce è RAM normale, quindi si azzera esattamente sull'evento
che l'avviso segnala e resta impostato per il resto dell'avvio.

Non viene inviato come parte della messa in servizio che lo ha armato, ma circa
quindici secondi dopo. Una messa in servizio termina con l'attività di polling
che apre la propria sessione TLS, e i buffer di record di quell'handshake sono
l'allocazione contigua più grande che questo firmware compie; inviare in
quell'istante rilascia di nuovo la connessione di polling e fa pagare al
polling successivo un secondo handshake mentre la memoria del primo è ancora
occupata, che su questa scheda è esattamente il momento in cui non ci sta. Un
quarto di minuto dopo le allocazioni della messa in servizio sono state restituite e un
invio costa quanto costa una riga instradata. Prima di chiedere la sessione
vengono ricontrollate le stesse due soglie di memoria che controlla una messa
in servizio, così una stazione momentaneamente al limite aspetta semplicemente
un altro secondo.

Non ha un interruttore, e nulla viene conservato a tempo indeterminato quando
non può essere inviato. Una stazione con il bot disabilitato, o il cui bot non
raggiunge mai Telegram, non ne invia alcuno, e un avviso che in due minuti non
trova un momento con spazio per una sessione viene scartato con una riga nel
log: l'avviso descrive un avvio già concluso nel momento in cui qualcuno
potrebbe agire, quindi una copia che arrivasse molto dopo direbbe meno di
quanto già mostra lo stato live della pagina *Telegram*. La consegna usa la
stessa attività di lavoro di breve durata e lo stesso lotto di trasmissione di
una notifica instradata, quindi non costa una sessione TLS propria, e va per
prima in quel lotto, davanti a tutto il resto che la stessa passata trasporta.

Una sola sessione TLS, condivisa da un lotto
============================================

Il servizio mantiene due client HTTP, uno per il polling lungo e uno per le
richieste in uscita, e fra i due c'è sempre al più una sessione TLS viva. Un
handshake ha bisogno dei propri buffer di record in blocchi contigui che una
stazione che porta anche il modem radio, lo stack Wi-Fi e il server web non
può fornire due volte, quindi una richiesta in uscita rilascia la sessione di
polling prima di aprire la propria, e il ciclo di polling successivo se la
riprende.

Rilasciarla a ogni messaggio farebbe pagare quello scambio di continuo a una
stazione con l'inoltro attivo: ogni riga inoltrata costerebbe un handshake
all'andata e un altro al ritorno verso il polling. Gli invii che vanno insieme
sono perciò raggruppati in un *lotto di trasmissione*. La sessione di polling
viene rilasciata una volta sola, dal primo messaggio del gruppo, e tutti gli
altri riusano la sessione che quello ha aperto: svuotare l'intera coda —
compreso un bollettino distribuito a otto utenti, all'amministratore e a
quattro chat di gruppo — costa un handshake e non uno per destinatario. Un
ciclo di polling che scade mentre un lotto è ancora in corso attende che si
chiuda, fino a otto secondi, prima di riprendersi la connessione, così una
distribuzione non viene mai spezzata a metà; oltre quel limite il polling si
riprende comunque la connessione, perché un invio bloccato su un socket che
non risponde non deve impedire al bot di leggere i propri aggiornamenti.

Il raggruppamento vale per lo svuotamento delle notifiche inoltrate, per un
ciclo di allarmi e per ``telegram_broadcast()``. Un invio singolo è un gruppo
di uno: rilascia la sessione di polling, apre la propria e la lascia al ciclo
di polling successivo.

Inoltro dei messaggi della stazione a Telegram
================================================

L'interruttore "Inoltra messaggi della stazione" della pagina *Telegram*,
accanto all'interruttore di accensione del bot stesso e sopra l'interruttore
"Inoltra bollettini" descritto più sotto, consegna i messaggi APRS alla chat
Telegram del loro destinatario, così ogni operatore legge sul proprio telefono
ciò che gli è stato inviato senza dover aprire la pagina *Snd/Rcv Msg*.

Entrambe le provenienze vengono inoltrate alle stesse condizioni. Un messaggio
ricevuto via radio o dal flusso APRS-IS viene inoltrato quando è decodificato,
e un messaggio che questa stazione origina dalla pagina *Snd/Rcv Msg* viene
inoltrato quando è trasmesso, così inviare a un nominativo elencato nella
pagina *Telegram* mette la riga nella chat di quell'utente oltre che in aria.
Nel secondo caso ciò che viene inoltrato è il testo effettivamente uscito,
dopo la sanificazione che applica il percorso di uscita, e senza il suffisso
del numero di messaggio, che riguarda il protocollo radio e non la persona che
legge la riga. Viene inoltrata solo la prima trasmissione: i ritenti
automatici portano lo stesso testo allo stesso destinatario e arriverebbero
come duplicati.

L'insieme dei destinatari è la tabella degli utenti autorizzati della pagina
*Telegram* e nient'altro. Ogni scheda utente porta il Nominativo proprio di
quell'operatore, e un messaggio ricevuto viene consegnato a ogni utente il cui
Nominativo corrisponde esattamente al destinatario del messaggio, SSID
incluso. Il nominativo proprio di questa stazione — il My Callsign della
pagina *Station* — non entra nella decisione, così un messaggio indirizzato a
un utente elencato viene inoltrato a quell'utente indipendentemente dal fatto
che questa stazione legga il frame per conto proprio, e più utenti che
condividono uno stesso nominativo base con SSID diversi ricevono solo ciò che
è stato inviato al proprio. Un destinatario che non corrisponde al Nominativo
di alcun utente non viene inoltrato a nessuno.

Una risposta ``ackNN``/``rejNN`` non porta testo leggibile e non viene mai
inoltrata, e nemmeno un messaggio indirizzato a un gruppo (``ALL``, ``QST``,
``CQ`` o uno dei nomi di gruppo propri dell'operatore), poiché un nome di
gruppo non è il nominativo di un utente. L'inoltro è indipendente da ciò che
questa stazione fa poi con lo stesso frame: la conferma automatica, l'impulso
di allarme di ``/status`` e lo storico di *Snd/Rcv Msg* continuano ad
applicarsi solo ai messaggi indirizzati al nominativo proprio di questa
stazione. È anche indipendente dall'interruttore di abilitazione della pagina
*Message*. L'inoltro a Telegram è un consumatore del frame ricevuto a pieno
titolo, così un messaggio o un bollettino in arrivo viene decodificato e
inoltrato anche su una stazione che fa girare il bot con la messaggistica APRS
spenta; tutto ciò che sta dietro quell'interruttore - la regola di
accettazione, la conferma, l'impulso di allarme e lo storico della chat -
resta dietro di esso.

Un messaggio inoltrato raggiunge il suo utente come un'unica riga:

.. code-block:: text

   msg from <nominativo del mittente> to <nominativo del destinatario> :: <testo del messaggio>

L'invio avviene solo quando l'interruttore è acceso, il bot stesso è
abilitato e il bot è attualmente connesso; un messaggio che arriva mentre una
di queste condizioni non è soddisfatta semplicemente non viene inoltrato, e
non resta in coda per quando il bot torna disponibile.
``telegram_app_notify_station_message()`` (dichiarata in ``telegram_app.h``)
è il punto d'ingresso chiamato da message.c; si limita a comporre la riga, a
cercare gli utenti a cui compete e a consegnare un elemento per utente a una
piccola coda, poiché il percorso di decodifica dei frame che la chiama gira
sul task di ricezione del modem stesso, che non porta lo stack necessario a
una chiamata di rete verso Telegram per il suo handshake TLS. Lo stesso task
lavoratore di breve durata che esegue un avvio o uno spegnimento svuota quella
coda tramite ``telegram_send_message()`` come ultima cosa che fa prima di
uscire.

La coda stessa viene creata al momento in cui si applica la configurazione —
cioè all'avvio e a ogni salvataggio della pagina *Telegram* — e solo per un bot
abilitato con almeno uno dei due interruttori di inoltro acceso. Una stazione
che li lascia entrambi spenti non la alloca mai, mentre una che ne accende uno
ha la coda al suo posto prima che la prima riga possa essere inoltrata, così
nessuno dei due punti d'ingresso alloca qualcosa sul task che lo chiama.

Inoltro dei bollettini a Telegram
===================================

L'interruttore "Inoltra bollettini" della pagina *Telegram*, subito sotto
quello precedente, consegna a Telegram i bollettini APRS. Un
bollettino è indirizzato a tutta la rete e non a una stazione, quindi non c'è
alcun destinatario da confrontare né alcun nominativo con cui scegliere a chi
consegnarlo: ogni bollettino va a tutti gli utenti autorizzati della tabella
degli utenti, all'amministratore se ne è configurato uno e a tutte le chat di
gruppo consentite. Un amministratore che compare anche nella tabella degli
utenti riceve una copia, non due.

Un bollettino si riconosce dal suo destinatario, secondo il capitolo 14 di
APRS101: ``BLN`` seguito da una sola cifra è un bollettino generale, ``BLN``
seguito da una sola lettera maiuscola è un annuncio, e ciascuno dei due può
portare un nome di gruppo di al massimo altri cinque caratteri (``BLN1``,
``BLNA``, ``BLN1WX``). Contano tutte le provenienze: un bollettino sentito via
radio, uno arrivato dal flusso APRS-IS e uno che questa stazione trasmette da
sé vengono inoltrati allo stesso modo, così gli operatori che leggono il bot
vedono gli annunci della stazione nella stessa chat e nella stessa forma di
quelli di chiunque altro.

Un bollettino originato da questa stazione viene inoltrato dallo scheduler che
lo trasmette, una volta per passata di trasmissione e non una per canale, così
un bollettino inviato sia via RF sia via Internet raggiunge comunque ogni chat
una sola volta. Viene inoltrato indipendentemente dall'esito di ciascuna
trasmissione, perché ciò che viaggia verso Telegram è l'annuncio e non un
resoconto sulla radio, e porta il testo come esce in aria, marcatore di non
archiviazione incluso, anziché la bozza memorizzata. Uno slot di bollettino
senza né RF né Internet spuntati non viene trasmesso affatto e quindi non
viene nemmeno inoltrato.

Un bollettino arriva ai suoi destinatari come una sola riga:

.. code-block:: text

   bulletin from <nominativo del mittente> to <destinatario del bollettino> :: <testo del bollettino>

I bollettini si ripetono, ed è questo che li rende bollettini: il mittente li
ritrasmette a intervalli, ogni digipeater a portata ripete ciò che sente e in
più torna una copia igatata da APRS-IS. Perciò un bollettino il cui mittente,
destinatario e testo coincidono con uno inoltrato entro la "Finestra di
ripetizione dei bollettini" della pagina *Telegram* viene scartato invece di
essere inviato di nuovo, così un bollettino periodico arriva una sola volta in
ogni chat anziché riempirla di copie di sé stesso. Modificare il testo, o
l'invio da parte di un'altra stazione, ne fa un bollettino nuovo che viene
inoltrato subito. Vengono ricordati gli otto bollettini inoltrati più di
recente, come un hash di quei tre campi e il momento in cui sono stati visti,
qualunque sia la finestra. È anche ciò che mantiene a una sola copia un
bollettino proprio di questa stazione quando il frame digipetuto torna entro
la finestra: porta lo stesso mittente, destinatario e testo, così la copia di
ritorno viene riconosciuta per la ripetizione che è. Il destinatario viene
confrontato senza gli spazi finali, perché i due punti di ingresso non lo
scrivono allo stesso modo - lo schedulatore consegna il campo di nove
caratteri riempito di spazi che ha appena messo in aria (``BLN1     ``), il
decodificatore di frame quello ridotto (``BLN1``) - e senza questo la copia di
ritorno sarebbe un altro bollettino.

La finestra viene armata da una consegna, mai da un tentativo. Un bollettino
che non si è potuto consegnare la lascia intatta e viene inoltrato alla sua
trasmissione successiva, cosa che conta soprattutto con l'intervallo più breve
che la pagina *Bollettini* consente: un bollettino che si ripete ogni 30 s
contro la finestra predefinita perderebbe altrimenti le sue ventinove
trasmissioni successive per un solo armamento a cui non è seguita alcuna
consegna, e le perderebbe tutte finché persiste ciò che ha bloccato la prima.

La finestra è il campo subito sotto l'interruttore, in secondi, da 0 a 86400
(24 h), e vale 900 s per impostazione predefinita. Impostala più lunga
dell'intervallo con cui vengono trasmessi i bollettini che si sentono sul
canale, così ognuno arriva nelle chat una volta per modifica e non una volta
per trasmissione; una stazione i cui vicini ripetono i loro bollettini ogni
dieci minuti vuole qui più di 600 s. Portarla a 0 disattiva del tutto il
controllo e inoltra ogni copia, comprese quelle che tornano dai digipeater e
dal flusso APRS-IS, che è ciò che vuole chi osserva le ritrasmissioni su un
canale congestionato e ciò che non vuole nessuno che legga una chat. Il valore
risiede in ``bulletinWindowSeconds`` e ha effetto al salvataggio, senza
riavviare.

La consegna è vincolata alle stesse tre condizioni di un messaggio della
stazione inoltrato - interruttore acceso, bot abilitato, bot connesso - e
nulla resta in attesa per quando una di esse tornerà a valere. Quale di esse
manchi, o quale altro motivo sia intervenuto, viene scritto nel registro ogni
volta che cambia e non una volta per bollettino: una chat che resta muta dice
perché una sola volta, nel momento in cui il motivo inizia e di nuovo quando
finisce, invece che in silenzio o a ogni ripetizione.
``telegram_app_notify_bulletin()`` (dichiarata in ``telegram_app.h``) è il
punto di ingresso che chiama message.c per un bollettino ricevuto e
bulletins.c per uno proprio di questa stazione, entrambi per lo stesso
ragionamento sullo stack del task chiamante. Accoda un solo elemento per l'intero bollettino invece di uno per
destinatario: l'elemento porta il testo una sola volta e il task che svuota la
coda lo distribuisce, ed è lì che viene letto l'elenco dei destinatari, così
un salvataggio che cade fra i due momenti si riflette nella consegna.

Uno svuotamento viene richiesto ogni volta che una riga entra in coda, e la
richiesta è respinta finché l'unico task lavoratore breve del bot sta già
eseguendo un avvio, un arresto o un altro svuotamento. La domanda viene
perciò riposta nel punto in cui quel task termina e libera il suo posto, così
uno svuotamento respinto è rinviato e non perduto: un bollettino accodato
mentre il bot veniva ricostruito parte appena la ricostruzione finisce, senza
attendere un'altra riga che la finestra dei duplicati avrebbe comunque
scartato.

La distribuzione avviene come un solo lotto di trasmissione, quindi l'intero
elenco dei destinatari condivide l'unica sessione TLS descritta sopra invece
di pagare un handshake per chat, ed è limitata dal numero di destinatari che
il file stesso può nominare — otto utenti, quattro chat di gruppo e
l'amministratore — così una distribuzione trattiene quella sessione per un
tempo che fissa il firmware e non una configurazione. Ogni passata riporta a
INFO quanti destinatari ha raggiunto e quanto è durata, ed è questo a dire a
un operatore che legge il registro che a consumare memoria è una raffica di
bollettini e non altro.

Comandi integrati
===================

``telegram_service`` registra un unico set di comandi condiviso su ogni
dispositivo che lo incorpora; questo firmware non aggiunge comandi, sensori
o parametri remoti propri al di sopra di esso.

.. list-table::
   :header-rows: 1
   :widths: 20 12 68

   * - Comando
     - Accesso
     - Cosa fa
   * - ``/start``
     - chiunque
     - Saluta e mostra i punti di ingresso del bot.
   * - ``/help``
     - chiunque
     - Elenca i comandi disponibili.
   * - ``/status``
     - chiunque
     - Segnala quali servizi di questo firmware sono attivi o spenti (IGate,
       digiripetitore, tracker, meteo, telemetria, messaggistica,
       responder query, BrandMeister, ricevitore GNSS, modem AFSK,
       limitatore di duty-cycle TX, sincronizzazione SNTP) e la memoria
       libera attuale.
   * - ``/sensors``
     - chiunque
     - Segnala ogni campo meteo e canale di telemetria che sia abilitato e
       abbia anche un sensore assegnato, con il relativo valore oppure
       "nessuna lettura". I campi abilitati senza sensore assegnato non
       vengono mostrati.
   * - ``/uptime``
     - chiunque
     - Segnala da quanto tempo il dispositivo è acceso.
   * - ``/whoami``
     - chiunque
     - Segnala gli identificativi utente e chat di chi chiama — i valori da
       aggiungere a mano alla lista utenti o chat di ``telegram.json``.
   * - ``/menu``
     - chiunque
     - Mostra l'interfaccia a pulsanti (tastiera inline di Telegram) per i
       comandi precedenti.
   * - ``/stats``
     - chiunque
     - Segnala i contatori propri del servizio: aggiornamenti ricevuti,
       comandi eseguiti, messaggi inviati, aggiornamenti rifiutati, errori
       di polling.
   * - ``/config``, ``/get``, ``/set``
     - solo admin
     - Elencano, leggono e cambiano parametri registrati da remoto. Questo
       firmware non ne registra nessuno, quindi oggi non elencano nulla, ma
       restano disponibili per un futuro driver o funzionalità che ne
       registri uno.
   * - ``/users``
     - solo admin
     - Elenca gli utenti autorizzati.
   * - ``/alerts``
     - solo admin
     - Abilita o disabilita gli avvisi push del servizio verso
       l'amministratore.
   * - ``/reboot``
     - solo admin, disabilitato qui
     - Presente nella libreria; non registrato da questo firmware (vedi
       sopra).

"Solo admin" significa che l'identificativo di chi chiama deve corrispondere
ad ``admin_id`` o a uno degli utenti autorizzati aggiunti con il flag admin;
il tentativo di chiunque altro viene contato come un aggiornamento rifiutato
anziché ricevere risposta.

Chi può parlare con il bot
===========================

L'autorizzazione è un elenco, e l'elenco è chiuso. Un mittente viene accettato
solo quando il suo identificativo vi compare — inizializzato da ``admin_id``
in ``telegram_init()`` ed esteso con gli utenti autorizzati di
``telegram.json`` — oppure quando il servizio gira in modalità ad accesso
aperto, che questo firmware non abilita mai. Un elenco vuoto non è
un'eccezione a quella regola: respinge tutti.

Questo conta per una stazione il cui token viene configurato prima del suo
amministratore, che è il caso normale di un'immagine clonata o di un
``telegram.json`` scritto una volta e copiato su più dispositivi. Un elenco
che si aprisse da solo finché è vuoto lascerebbe quel dispositivo a
rispondere al primo sconosciuto che trova il bot, per tutto il tempo in cui
il campo dell'identificativo resta a 0.

Chiudere l'elenco non rende più difficile scoprire il primo identificativo,
perché scoprirlo non è mai dipeso dall'essere ammessi. Un comando privato di
un mittente non elencato viene rifiutato con una risposta che riporta
l'identificativo numerico di quello stesso mittente, e lo stesso numero viene
scritto nel log; inserirlo come identificativo dell'amministratore nella
pagina *Telegram* è tutta la messa in servizio. Finché nessuno è autorizzato,
la pagina lo segnala sotto le credenziali e il servizio lo annota una volta
nel log quando inizia a interrogare, così un bot che non risponde a nessuno
non viene mai scambiato per un bot che non riesce a connettersi.

Quella risposta è l'unica cosa che il dispositivo invia per conto di chi non ha
autorizzato, quindi è limitata in frequenza: al massimo un rifiuto per
identificativo di mittente ogni ``TELEGRAM_SERVICE_UNKNOWN_REPLY_INTERVAL_S``
secondi (300 per impostazione predefinita), con i mittenti già serviti tenuti in
una tabella di ``TELEGRAM_SERVICE_MAX_UNKNOWN_SENDERS`` voci (4 per impostazione
predefinita) che scarta per prima quella risposta più tempo fa. Entrambe sono
opzioni di compilazione sotto *Telegram bot service* in ``menuconfig``. Il
motivo è la memoria, non la banda: le connessioni di interrogazione e di
trasmissione si alternano un'unica sessione TLS, perché un solo handshake alla
volta è tutto ciò che questa scheda consente accanto a un modem radio e a un
server web, e una risposta non misurata lascerebbe che un estraneo consumasse
il bilancio con cui gira l'interrogazione della stazione stessa. I comandi
rimasti senza risposta sono comunque contati come aggiornamenti rifiutati e
comunque annotati nel log con l'identificativo del mittente, e un mittente
scartato dalla tabella riceve di nuovo risposta al comando successivo: a chi sta
mettendo in servizio una stazione non manca mai il numero per cui è venuto.

Un aggiornamento che non nomina alcun mittente — il totale aggregato delle
reazioni che Telegram consegna nelle chat che nascondono chi ha reagito — è
accettato solo dentro un gruppo dell'elenco delle chat ammesse, che è l'unico
varco che possa rispondere per esso. Fuori da un gruppo non c'è né elenco di
chat né mittente, quindi non c'è nulla da autorizzare e viene scartato.

La pagina Telegram
=====================

``GET``/``POST /telegram`` (:ref:`it-http-routes`) espone tutti i campi di
``telegram.json``: l'interruttore di abilitazione, il token del bot
(mascherato, con un controllo *mostra password*), l'identificativo
dell'amministratore, l'indirizzo della Mini App e le tabelle degli utenti
autorizzati e delle chat di gruppo consentite. Sotto il modulo di
salvataggio, una tabella di stato live (``GET /telegram/status``, JSON,
interrogata ogni 2 secondi) mostra lo stato generale, il motivo preciso,
ogni dettaglio non tradotto restituito da Telegram o dallo stack di rete, il
nome utente del bot stesso una volta noto, il suo uptime e i suoi contatori.

Le tabelle degli utenti autorizzati e delle chat di gruppo consentite hanno
dimensione fissa — fino a 8 utenti e 4 chat di gruppo, secondo
``TELEGRAM_APP_USERS_MAX`` e ``TELEGRAM_APP_CHATS_MAX`` — e ogni voce è
mostrata come una scheda a comparsa con un campo identificativo e un campo
nome visualizzato. Un account già presente nell'elenco legge il proprio
identificativo con ``/whoami``; uno che non c'è riceve lo stesso numero nel
rifiuto con cui il bot risponde a qualsiasi comando, e l'identificativo di un
gruppo si legge inviando ``/whoami`` dentro il gruppo. Lasciare vuoto (o a 0)
l'identificativo di una scheda lascia quello slot
inutilizzato, e un salvataggio compatta la tabella così uno slot svuotato in
mezzo non lascia un vuoto.

Tutto ciò che la pagina mostra resta lo stesso ``telegram.json`` descritto
sopra, quindi può anche essere scaricato, modificato a mano e ricaricato
dalla pagina *File Storage* — utile per modificare più voci alla volta, o
per ripristinare una configurazione nota e funzionante.

.. seealso::

   :ref:`it-web-admin` — il resto delle pagine e delle rotte del pannello web.

   :ref:`it-storage-ota` — come vengono memorizzati ``telegram.json`` e il
   file del certificato, e come l'upload/download della pagina File Storage
   permetta a un operatore di modificare le parti che la pagina Telegram non
   mostra.
