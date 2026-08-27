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
       Telegram. Acceso, ogni messaggio APRS ricevuto indirizzato al
       nominativo proprio di un utente autorizzato viene anche inviato alla
       chat Telegram di quell'utente; vedi `Inoltro dei messaggi della
       stazione a Telegram`_ più sotto. Assente, come ``enabled``, si carica
       spento.
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
   manda l'operatore a cercare memoria che non era mai il problema.
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

Mentre il bot gira, lo stesso tick a 1 Hz ripubblica i suoi contatori e nota
un collegamento a internet scomparso o un task di polling terminato, così una
stazione che perde la connessione segnala "in attesa di un percorso di rete"
invece di un conteggio crescente e inspiegato di errori di polling.

Il riavvio è deliberatamente disabilitato (``allow_reboot = false``): questa
stazione porta un trasmettitore e uno scheduler con impegni a tempo, e un
riavvio è già disponibile, dietro il login stesso del pannello web, dalla
pagina *System*.

Nessuna pubblicazione all'avvio
==================================

Due comodità offerte dal servizio — pubblicare il proprio elenco di comandi
sull'interfaccia di Telegram, e annunciare all'avvio che il bot è ora attivo
— sono entrambe disattivate (``publish_commands = false``,
``announce_start = false``). Entrambe aprirebbero una seconda sessione TLS
nell'istante in cui la connessione di polling si avvia, mentre i buffer della
prima sono ancora occupati; su una stazione la cui memoria porta anche i
buffer DMA del modem radio, quella seconda sessione non trova posto, e
l'unico effetto visibile sarebbe una coppia di errori nel log a ogni avvio.
Né i comandi né l'avviso di avvio vengono persi in alcun senso funzionale:
ogni comando funziona indipendentemente dal fatto che Telegram ne sia stato
informato, e lo stato live della pagina *Telegram* mostra già a un
amministratore che il bot è attivo.

Inoltro dei messaggi della stazione a Telegram
================================================

L'interruttore "Inoltra messaggi della stazione" della pagina *Telegram*,
accanto all'interruttore di accensione del bot stesso, consegna i messaggi
APRS in arrivo alla chat Telegram del loro destinatario, così ogni operatore
legge sul proprio telefono ciò che gli è stato inviato senza dover aprire la
pagina *Snd/Rcv Msg*.

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
stazione.

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
una chiamata di rete verso Telegram per il suo handshake TLS. Un task
lavoratore di breve durata, dimensionato e avviato allo stesso modo del task
di avvio descritto sopra, svuota quella coda tramite
``telegram_send_message()``.

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
