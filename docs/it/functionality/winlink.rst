.. _it-winlink:

===================================
Posta radio Winlink (APRSLink)
===================================

`APRSLink <https://winlink.org/APRSLink>`__ è il gateway ospitato dal CMS tra
APRS e il sistema mondiale di posta radio Winlink. Lo si raggiunge con normali
messaggi di testo APRS indirizzati a un nominativo di servizio — ``WLNK-1`` — e
risponde con messaggi di testo APRS. Non c'è un protocollo separato, né uno
strato di sessione, né un incapsulamento proprio: in aria una sessione APRSLink
somiglia esattamente a una conversazione con un operatore umano molto
letterale.

Con «gateway Winlink» si intendono di solito due cose diverse, e questa
stazione le fa entrambe. Sono indipendenti tra loro e si configurano
separatamente, quindi conviene avere chiaro quale sia quale prima di toccare la
pagina *Winlink*.

Due ruoli
=========

**Client.** Questa stazione svolge la propria sessione, legge e scrive la
propria posta e conserva le risposte del servizio dove l'amministrazione web
può mostrarle. È il componente ``winlink``, il gruppo *Account Winlink* e il
terminale di sessione. Richiede un account Winlink e la sua password.

**Gateway.** Una stazione vicina sul canale RF locale si rivolge essa stessa al
servizio. I suoi comandi viaggiano verso APRS-IS attraverso il normale percorso
RF→INET di questa stazione, e le risposte del servizio tornano attraverso il
percorso INET→RF. Nulla di vostro è coinvolto: il nominativo del vicino apre la
propria casella e la sua password risponde alla propria sfida di accesso.
Questo ruolo è quasi interamente l'IGate che fa ciò che già fa, e tutta la sua
configurazione è l'unico interruttore del gruppo *Gateway per le stazioni
locali*.

Se l'obiettivo è permettere a chi sta intorno di usare Winlink, il ruolo che
conta è quello di gateway e quello di client è facoltativo. Se l'obiettivo è
leggere la propria posta dalla stazione, è il contrario.

Il client
=========

Identità e casella
------------------

Il servizio apre la casella in base al **nominativo base** di chi invia il
comando, ignorando ogni suffisso ``-SSID``, quindi l'account raggiunto è sempre
``<NOMINATIVOBASE>@winlink.org``. Il nominativo che la trama uscente porta
davvero è quello del servizio messaggi (``msgMycall``, la pagina *Messaggio*),
ed è il motivo per cui *Usa il nominativo del servizio messaggi* è acceso di
default; il campo separato esiste solo per una stazione il cui account Winlink
è sotto un altro nominativo.

Sessioni
--------

Inviare un comando qualsiasi al servizio apre una sessione. Se l'account ha
l'accesso sicuro attivo, il servizio risponde con una sfida della forma
``Login [NNN]``, dove ogni cifra è una posizione di carattere nella password
contando da 1. La risposta è composta da quei caratteri più altri tre a piacere,
in qualsiasi ordine — così la password stessa non viaggia mai in aria, e una
risposta osservata sul canale rivela solo tre dei suoi caratteri e non da dove
vengono. Con l'accesso sicuro spento non c'è alcuna sfida e il servizio
risponde semplicemente al comando; il client lo accetta altrettanto volentieri
e passa direttamente alla modalità comandi.

Una sessione scade dal lato del servizio dopo circa due ore.
``wlSessionMaxMin`` (110 minuti di default) resta al di sotto, così questa
stazione abbandona la sessione poco prima del servizio invece di scoprire che
non esiste più inviandoci dentro un comando.

Ritmo
-----

C'è un comando in sospeso per volta. Il successivo parte solo dopo che il
servizio ha confermato il precedente, e sono richiesti inoltre tre secondi di
silenzio dopo qualsiasi risposta prima che inizi lo scambio seguente. È
volutamente senza fretta: ancora ogni passo della sessione a qualcosa che il
servizio ha davvero detto anziché a un timer locale, e impedisce che una
sessione diventi una raffica di trame su un canale condiviso. Un comando non
confermato viene ritrasmesso due volte e poi la sessione viene abbandonata, con
il motivo in evidenza sulla pagina.

Tre task, una sessione
----------------------

Una sessione è guidata da tre punti contemporaneamente: la pagina web che
l'operatore sta usando, il tick di servizio una volta al secondo che ritrasmette
e invia il comando successivo in coda, e il percorso di ricezione che applica
ogni risposta del servizio. Due mutex dentro
``components/winlink/winlink.c`` impediscono che si pestino i piedi — uno sulla
casella e uno sulla sessione vera e propria: lo stato, la coda dei comandi, il
comando in sospeso e il suo contatore di ritentativi, il flag di composizione,
i riferimenti temporali della sessione e il motivo del fallimento.

Senza quel secondo lucchetto i sintomi visibili sono comandi persi o inviati due
volte — un operatore che scrive mentre il tick ne sta prelevando uno dalla coda
— e ripetizioni: una conferma che arriva a metà di un ritentativo può azzerare
il comando in sospeso proprio mentre il ritentativo lo rimette in aria, cosicché
il servizio vede un comando a cui aveva già risposto. Nessuno dei due lucchetti
resta preso mentre una trama viene trasmessa o mentre la casella viene scritta
in flash, quindi nulla di ciò che l'operatore fa sulla pagina attende la radio o
il filesystem.

Comandi
-------

L'intero insieme di comandi APRSLink è testo, quindi il campo *Comando* li
accetta tutti direttamente. I più frequenti sono:

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Comando
     - Significato
   * - ``L``
     - Elenca i messaggi in attesa. Il servizio ne restituisce fino a cinque.
   * - ``R<n>``
     - Legge il messaggio *n* come appariva nell'elenco.
   * - ``Y<n>``
     - Inizia una risposta al messaggio *n*.
   * - ``F<n> <destinatario>``
     - Inoltra il messaggio *n* a un altro indirizzo.
   * - ``K<n>``
     - Cancella il messaggio *n*.
   * - ``SP <destinatario> <oggetto>``
     - Inizia un messaggio. Tutto ciò che segue è il testo.
   * - ``/EX``
     - Termina il testo e consegna il messaggio al servizio.
   * - ``A <alias>=<indirizzo>``
     - Crea o aggiorna un alias. ``A <alias>=`` lo cancella, ``AL`` li elenca.
   * - ``B``
     - Esce dalla sessione.
   * - ``?``
     - Aiuto. ``?L`` dà l'aiuto di un singolo comando.

I pulsanti *Inizia messaggio* / *Aggiungi riga* / *Invia messaggio* percorrono
la sequenza ``SP`` … ``/EX``, l'unica il cui ordine conta, e non fanno nulla che
il campo comando non possa fare anche a mano.

I quattro comandi che agiscono su un messaggio elencato hanno pulsanti propri:
*Leggi*, *Rispondi*, *Inoltra* ed *Elimina*, che inviano ``R<n>``, ``Y<n>``,
``F<n> <destinatario>`` e ``K<n>`` rispettivamente. *Inoltra* chiede prima
l'indirizzo ed *Elimina* chiede conferma, nominando il numero che sta per
eliminare.

Compaiono in due punti. Una risposta memorizzata che inizia con un numero di
messaggio è una riga di un elenco e porta sotto di sé i quattro pulsanti, già
puntati al numero di quella riga. Ciò che separa quel numero dal resto della
riga lo decide la formattazione del servizio stesso, quindi si cerca solo il
numero iniziale; una risposta che non inizia con uno — il testo di un messaggio,
una sfida di login, una conferma — non riceve la fila. Il campo *Numero del
messaggio* sotto la casella porta gli stessi quattro pulsanti per un numero
scritto a mano, ed è quello da usare per una risposta il cui numero non è stato
riconosciuto, o dopo aver cancellato l'elenco.

Dove passa il traffico
----------------------

``wlInetOnly`` è acceso di default e tiene fuori dall'aria il traffico Winlink
di questa stazione. Il ragionamento è che questa stazione *è* un IGate: un
comando che trasmette occupa il canale locale con traffico che nessuna stazione
di quel canale ha bisogno di sentire, e la risposta del servizio arriva
comunque dallo stesso collegamento Internet. L'interruttore può solo togliere la
tratta RF — con *Invia/ricevi via Internet* della messaggistica spento non c'è
nulla a cui ripiegare, e il comando esce in RF come prima, che è esattamente
ciò che vuole una stazione senza collegamento APRS-IS.

Essere avvisati della posta in attesa
-------------------------------------

APRSLink osserva i commenti dei rapporti di posizione che vede in cerca della
parola ``winlink`` e la usa per decidere quali stazioni avvisare, senza che
glielo si chieda, quando hanno posta in attesa. *Annuncia questa stazione come
lettrice Winlink* aggiunge quella parola al commento del beacon. Viene
aggiunta, non sostituita, e solo finché ci sta, così un commento che già riempie
il campo conserva ogni carattere scritto — e la pagina lo dice se non resta
spazio.

*Controlla la posta ogni* è l'altra metà della stessa idea e non dipende dal
fatto che il servizio si accorga di qualcosa: apre una sessione e invia ``L`` a
intervallo fisso. Agisce solo da inattivo, quindi non interrompe mai una
sessione che sta facendo qualcosa.

Risposte memorizzate
--------------------

Tutto ciò che il servizio rimanda indietro è conservato in
``/storage/winlink.json``, dalla più vecchia alla più recente, fino alle ultime
24 risposte. Cancellarle dalla pagina rimuove quel file e nient'altro — le
impostazioni dell'account vivono in ``config.json`` e restano intatte.

Il gateway
==========

Un IGate non mette in aria ogni messaggio che legge da APRS-IS. L'inoltro dei
messaggi (``main/aprs_service.c``, ``messageGatePass()``) chiede quattro cose a
un messaggio prima di trasmetterlo:

* il destinatario è stato sentito in RF entro ``igateLocalWindow`` secondi;
* è stato sentito entro non più di ``igateMsgMaxHops`` salti di digipeater;
* il destinatario **non** è stato sentito su APRS-IS nella stessa finestra;
* il mittente non è stato sentito esso stesso in RF.

Una risposta del servizio Winlink soddisfa naturalmente la prima, la seconda e
la quarta condizione. La terza è il problema: questa stazione inoltra il
comando del vicino su APRS-IS, il server lo rimanda indietro come eco sul
flusso, e quell'eco può bastare a far sembrare il vicino collegato a Internet —
a quel punto la risposta che stava aspettando viene scartata come
``DROP_MSG_ADDRESSEE_INET`` e la sessione si blocca senza che nulla sembri
andato storto.

*Lascia che le risposte del servizio raggiungano in RF le stazioni locali*
(``wlGateExempt``, acceso di default) toglie quell'unica condizione, e solo per
i messaggi il cui mittente è il nominativo di servizio configurato. La premessa
su cui la regola si regge — che un destinatario presente su APRS-IS possa
leggere lì il messaggio da sé — è falsa per costruzione per una risposta
APRSLink: esiste soltanto perché il destinatario l'ha chiesta al gateway di
questa stazione, e ha esattamente un percorso di consegna. Tutte le altre
condizioni, i controlli di intestazione che tengono il traffico
``TCPXX``/``NOGATE``/``RFONLY`` fuori dall'aria e lo scarto di
bollettini/diffusioni restano invariati.

Le tre impostazioni dell'IGate che l'inoltro consulta sono mostrate in sola
lettura sulla pagina *Winlink*, accanto a quell'interruttore, così da vedere in
un colpo d'occhio i quattro ingressi della decisione. Si modificano sulla pagina
*IGate*. ``igateLocalWindow`` merita in particolare un'occhiata: il servizio può
impiegare un minuto o più a rispondere a una lettura, e una finestra più breve
di quel giro completo scarterà la risposta per il primo motivo e non per il
terzo.

Configurazione
==============

Ogni impostazione è una chiave ``wl*`` di ``config.json`` — vedere
:ref:`it-config-json`. La password vi è memorizzata in chiaro, esattamente come
già avviene per il passcode APRS-IS; chi può leggere la partizione di
archiviazione può leggere entrambe.
