.. _it-digipeater:

==========
Digipeater
==========

Il componente ``digirepeater`` (``components/digirepeater/``) implementa il
digipeating APRS con riscrittura del percorso. Il suo unico punto di ingresso,
``digiProcess()``, è chiamato dallo smistamento RX quando ``g_config.digi_en`` è
attivo. Legge l'indicativo/SSID del digipeater da ``g_config.digi_mycall`` /
``digi_ssid``, quindi la pagina *Digi* dell'amministrazione web è l'unica fonte
di verità.

Il contratto di riscrittura
===========================

``digiProcess(ax25_msg_t *packet)`` riscrive il percorso **sul posto** e
restituisce uno di tre valori:

.. list-table::
   :header-rows: 1
   :widths: 12 88

   * - Ritorno
     - Significato
   * - ``0``
     - Non ripetere (scarta / non per noi / già ritrasmesso / malformato).
   * - ``1``
     - Ripeti così com'è — il percorso porta già il nostro indicativo usato (es.
       un ``*`` di bypass); il chiamante ritrasmette il frame invariato.
   * - ``2``
     - Ripeti con percorso modificato — il chiamante ricodifica l'intestazione
       riscritta e la trasmette in RF.

Quando ``digiProcess()`` restituisce ``2``, lo smistamento di ``aprs_service.c``
ri-rende il frame in TNC2, chiama ``aprs_service_send_tnc2()`` e, se ha successo,
incrementa il contatore ``digi`` della dashboard e registra una voce di traffico
``DIGI``.

La tabella degli alias
======================

Il digipeater non riconosce alias propri. Ogni alias che onora è una riga di
``g_config.digi_alias``, modificata in *Alias di Percorso n-N* della pagina
*Digi*, e la riga dice come quell'alias viene ripetuto. È questo che rende le
convenzioni locali del Nuovo Paradigma n-N — un ``WIDE1-1`` di riempimento, un
``WIDE2-2`` a due hop, un ``SSn-N`` regionale — un'impostazione dell'operatore
e non una costante del firmware.

.. list-table::
   :header-rows: 1
   :widths: 16 12 72

   * - Campo
     - Intervallo
     - Significato
   * - Alias
     - 6 caratteri
     - L'indicativo del ripetitore **senza** il suo SSID; l'SSID è il contatore
       di hop *N* ed è gestito a parte. ``#`` corrisponde esattamente a una
       cifra decimale, quindi una riga copre un'intera famiglia: ``WIDE#``
       reclama da ``WIDE1`` a ``WIDE9``, ma mai ``WIDE``, ``WIDEN`` o
       ``WIDE12``, perché la corrispondenza richiede anche pari lunghezza. Un
       alias vuoto disabilita la riga.
   * - N massimo
     - 1–7
     - Il massimo contatore di hop onorato per questo alias. Un *N* maggiore
       ricevuto via radio viene *intrappolato*.
   * - Modalità
     - Spento / Traccia / Inondazione
     - **Traccia** inserisce l'indicativo di questa stazione davanti all'alias
       rimanente e lo marca come usato, così ogni hop del percorso può essere
       attribuito in seguito. **Inondazione** decrementa il contatore di hop e
       non lascia traccia di chi lo ha fatto. **Spento** ignora del tutto la
       riga.

Le righe vengono consultate nell'ordine della tabella e vince la prima
corrispondenza, quindi un alias specifico posto sopra una riga jolly conserva
il proprio limite di hop. La tabella di fabbrica è ``WIDE1`` (1 hop), ``WIDE2``
(2 hop) e ``WIDE#`` (2 hop), tutte in modalità traccia, con la quarta riga
libera per un alias regionale.

``WIDEn-N`` è tenuto a tracciare. Il paradigma lo ha spostato dal meccanismo di
inondazione non rintracciabile a quello di tracciamento proprio perché ogni hop
di ogni percorso ``WIDEn-N`` sia identificabile — ed è per questo che
*Inondazione* è appropriata solo per un alias regionale che l'operatore decida
di usare senza traccia.

``RELAY``, ``GATE``, ``ECHO`` e ``TRACEn-N`` non sono incorporati: sono stati
abbandonati come percorsi e non sono più nel firmware. Un operatore che ne abbia
ancora bisogno per un vicino datato lo aggiunge come una riga qualsiasi.

Intrappolamento e ruolo di riempimento
======================================

Un contatore di hop superiore al *N massimo* della riga corrispondente viene
intrappolato, e *Salti oltre il N massimo* sceglie come: **Limita al N massimo**
(l'impostazione predefinita) lo riporta al limite e ripete la trama, **Scarta la
trama** la rifiuta del tutto e conta ``DROP_DIGI_N_TRAPPED``. Limitare tiene la
trama in movimento e allo stesso tempo le impedisce di inondare oltre quanto le
condizioni locali consentano, ed è per questo che è l'impostazione predefinita;
ogni hop aggiuntivo moltiplica per circa tre il carico che una trama impone alla
rete.

*Digipeater di riempimento (un solo salto)* limita la stazione alle righe il cui
limite di hop è 1. È tutto il ruolo di riempimento: solleva il traffico dei
vicini che non raggiungono direttamente la dorsale e lascia tutto ciò che è
instradato per più hop ai digipeater ampi, che è ciò che impedisce a una
stazione domestica in una valle di aggiungere una copia ridondante di ogni
pacchetto della regione.

Due trame vengono rifiutate prima ancora di consultare la tabella: quella che
porta già l'indicativo di questa stazione marcato come usato, qualunque cosa
contenga ancora il suo percorso, e quella che ricade nella finestra di
soppressione duplicati qui sotto.

Instradamento legacy tramite SSID di destinazione
=================================================

Prima del Nuovo Paradigma n-N alcuni TNC portavano il conteggio degli hop nel
nibble SSID dell'indirizzo di destinazione AX.25 e non nel percorso.
*Ripetizione tramite SSID di destinazione (legacy)* nella pagina *Digi*
(``digi_dest_ssid_en``) abilita quella convenzione, ed è **disattivata per
impostazione predefinita**.

Attiva, una trama il cui SSID di destinazione sia da 1 a 7 viene ripetuta in
base a quel solo SSID: il conteggio viene decrementato e l'indicativo di questa
stazione viene inserito nel percorso marcato come usato, così l'hop resta
attribuibile in seguito. Quella decisione è presa *prima* della tabella degli
alias, ed è proprio per questo che non è l'impostazione predefinita: una trama
che porti sia un SSID di destinazione sia un percorso ``WIDEn-N`` esplicito
verrebbe ripetuta in base all'SSID e il percorso richiesto dalla stazione di
origine non verrebbe mai letto.

Disattivata, o quando la convenzione non instrada una trama in particolare (un
SSID di destinazione pari a 0, oppure da 8 a 15, che appartiene all'indirizzo di
destinazione stesso; un percorso che porta già l'indicativo di questa stazione
marcato come usato; un percorso già pieno), la trama raggiunge la tabella degli
alias esattamente come è stata ricevuta, SSID di destinazione incluso.

Soppressione duplicati
======================

Prima di qualsiasi lavoro sul percorso, il digipeater controlla il frame con
``isDuplicatePacketScoped(packet, DUP_SCOPE_DIGI)``. La chiave è costruita solo
dall'indirizzo di origine e dal campo informativo — mai dal percorso — quindi
ogni copia di una stessa trasmissione produce lo stesso hash comunque sia
arrivata. Un frame che corrisponde a uno ripetuto entro
``g_config.dup_cache_timeout_ms`` (30 s predefiniti, modificabile nella pagina
*IGate*) viene scartato: è questo che impedisce a due
digipeater nella copertura reciproca di rimbalzarsi lo stesso frame, e che
assorbe un'eco RF di un frame appena ripetuto da questa stazione.

La cache è condivisa con l'IGate ma le finestre no: ogni voce porta l'ambito che
l'ha inserita e corrisponde solo a ricerche dello stesso ambito. Entrambi i
consumatori vedono gli stessi frame dallo stesso dispatch RX, e il digipeater
gira per primo, quindi un'unica finestra condivisa gli farebbe consumare tutti i
frame e l'IGate li tratterebbe tutti come duplicati.

Contatori
=========

Il digipeater non tiene contatori propri. Tutto ciò che l'operatore può vedere
su di esso proviene da due punti che avanzano indipendentemente dal fatto che
``digi_en`` o ``igate_en`` siano attivi:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Valore
     - Da dove proviene
   * - Contatore ``digi`` di testata
     - ``aprs_service.c``, incrementato nel punto in cui il frame riscritto
       viene effettivamente trasmesso. Avanza solo mentre ``digi_en`` è
       attivo, perché con esso spento non c'è nulla da digipetare.
   * - Ogni scarto e frame malformato
     - ``igate_note_drop(DROP_DIGI_…)``, che alimenta la tabella per motivo che
       la dashboard mostra come *Drop Breakdown*. Ogni motivo è una riga
       distinta, quindi un duplicato, un percorso pieno e un nominativo
       segnaposto si distinguono invece di fondersi in un unico totale.

.. note::

   I motivi ``DROP_DIGI_*`` sono contati dentro ``digiProcess()``, quindi
   avanzano solo mentre il digipeater è in funzione. I frame scartati prima del
   dispatch, o in uscita verso RF, sono contati a livello di servizio in
   ``aprs_service.c`` e compaiono indipendentemente da quali funzioni siano
   abilitate.
