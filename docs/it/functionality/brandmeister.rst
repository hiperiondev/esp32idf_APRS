.. _it-brandmeister:

=========================================
Interconnessione BrandMeister (senza DMR)
=========================================

Questa stazione può scambiare traffico APRS con la rete DMR BrandMeister via
Internet senza implementare alcuna parte del DMR. Non è coinvolto alcun
collegamento Homebrew/MMDVM, né OpenBridge, né un account BrandMeister, né una
password di master.

Perché non serve alcun collegamento DMR
=======================================

Il lato APRS di BrandMeister **è un client APRS-IS**. Ogni master BrandMeister
esegue un processo gateway che accede a un server APRS-IS pubblico con una
normale riga ``user``/``pass``/``filter`` e immette come normali righe TNC2 il
traffico di posizione, telemetria e messaggi originato in DMR. Nell'altra
direzione si sottoscrive ad APRS-IS e converte ciò che riceve in messaggi di
testo DMR.

Il trasporto di cui questo firmware ha bisogno è quindi la sessione APRS-IS che
l'IGate già mantiene. Ciò che la pagina *BrandMeister* aggiunge è il
riconoscimento, il controllo di sicurezza e l'instradamento dei messaggi al di
sopra di essa.

Riconoscere il traffico BrandMeister
====================================

``main/include/aprs_bm.h`` è l'unica fonte di verità. A ogni riga letta da
APRS-IS vengono applicati tre test indipendenti, e vince il primo che
corrisponde:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Test
     - Che cosa riconosce
   * - TOCALL
     - Un indirizzo di destinazione ``APBM`` più esattamente due caratteri.
       ``APBMxx`` è il blocco assegnato a BrandMeister; ``APBMnD`` è il
       software principale del server e ``APBMnS`` i suoi servizi
       supplementari.
   * - Alias DMR
     - Un elemento del percorso uguale a ``DMR`` che compare prima del q
       construct, con o senza il contrassegno di utilizzo.
   * - Stazione di ingresso
     - Il nominativo immediatamente successivo a ``qAS``/``qAR`` che
       corrisponde a uno dei gateway configurati nella pagina. Un ``*`` finale
       confronta per prefisso. Saltato quando l'elenco è vuoto, che è lo stato
       di fabbrica.

Il secondo test non è ridondante rispetto al primo. Esiste traffico
BrandMeister autentico che porta il tocall generico ``APRS`` e soltanto il salto
DMR::

   PA0WCH>APRS,DMR*,qAS,PI1DMR-10:@043258h5123.03N/00526.95E(036/000
   DC6RN-9>APRS,DB0CJ,DMR*,qAR,DB0CJ:@043233h4925.11N/01152.85Ev148/000

Un classificatore basato sul solo tocall non vedrebbe nessuno dei due. Dalla
versione di master 20170909 anche il nominativo del ripetitore di origine è
presente nel digipath, ed è per questo che il secondo esempio porta ``DB0CJ``
davanti all'alias.

Una trama decodificata via radio **non** viene mai classificata come traffico
BrandMeister, qualunque cosa dica il suo percorso: la domanda a cui il
classificatore risponde è "la rete ha immesso questo in APRS-IS?", e una trama
ascoltata via radio non è arrivata così.

Due modi di usarlo
==================

**BrandMeister locale** — l'impostazione predefinita, e ciò che la maggior parte
degli operatori desidera davvero. Lasciate il filtro server come una normale
sottoscrizione locale. Il traffico BrandMeister nel vostro raggio arriva da solo,
perché è normale traffico APRS-IS; il classificatore lo contrassegna, la tabella
LAST HEARD lo mostra con il prefisso ``BM:`` al posto di ``INET:``, e l'inoltro
da Internet a RF lo tratta come qualsiasi altra stazione Internet. Questo non
costa banda né tempo di antenna aggiuntivi.

**Monitor mondiale** — facoltativo. Aggiungere ``u/APBM*`` al filtro server
della pagina IGate sottoscrive il traffico BrandMeister dell'intera rete.

.. warning::

   I termini del filtro server APRS-IS sono combinati in OR, mai in AND: passa
   ogni pacchetto che corrisponda a uno qualsiasi di essi. ``u/APBM*
   r/lat/lon/150`` chiede quindi traffico BrandMeister **mondiale** *oppure*
   qualsiasi cosa entro 150 km: l'intersezione non è esprimibile al server. La
   restrizione a un raggio locale deve avvenire su questa stazione, ed è ciò di
   cui si occupa il filtro di distanza da Internet a RF nella pagina IGate.

Per questo motivo, abilitare l'interruttore del monitor mentre l'inoltro da
Internet a RF è attivo e il filtro di distanza da Internet a RF è disattivato
viene **rifiutato**, con una spiegazione sotto l'interruttore. La stessa regola
viene riapplicata quando la pagina IGate disattiva il filtro di distanza, e di
nuovo al caricamento di un ``config.json``, quindi non è aggirabile modificando
il file a mano.

La pagina non modifica mai da sé la stringa del filtro server. Il filtro
appartiene all'operatore, e una pagina che lo riscrivesse in silenzio farebbe
sì che la pagina IGate riportasse in modo errato ciò che è stato realmente
inviato al server; viene invece mostrato il termine esatto da aggiungere, e la
tabella di stato riporta se il filtro in uso lo contiene già.

Inviare verso BrandMeister
==========================

Messaggio privato a un utente DMR
---------------------------------

Inviate un normale messaggio APRS indirizzato al nominativo-SSID che l'utente ha
associato al proprio ID DMR in SelfCare. La radio mostra::

   <NOMINATIVO DEL MITTENTE> <testo del messaggio>

Il rapporto di consegna DMR torna come una normale conferma di ricezione APRS.

.. note::

   La consegna non è garantita e il fallimento è silenzioso. Il gateway di ogni
   master applica un proprio modello al destinatario dei messaggi in arrivo: una
   espressione regolare su base nazionale che questa stazione non può vedere né
   prevedere. Un messaggio da essa filtrato semplicemente non produce alcuna
   conferma. L'assenza di conferma non è quindi prova di mancata consegna, e la
   pagina di chat non presenta come consegnato un messaggio non confermato.

Con *Invia i messaggi alle stazioni BrandMeister solo via Internet* abilitato
(il valore predefinito), un messaggio indirizzato a una stazione sentita
l'ultima volta come traffico BrandMeister esce solo via APRS-IS. Tale stazione è
sulla rete, non sul canale locale, quindi ogni copia via RF è tempo di antenna
speso per un ricevitore che non c'è, moltiplicato per il numero di ritentativi
finché il messaggio resta non confermato. Questo può soltanto togliere la tratta
RF: con *Invia a Internet* disattivato nella pagina Messaggi non viene inviato
nulla.

Messaggio a un talkgroup DMR
----------------------------

Usate la pagina *Bollettini* esistente. Un bollettino il cui identificatore sia
``0``–``9`` e il cui nome di gruppo sia l'ID del talkgroup di 1–5 cifre forma il
destinatario che BrandMeister legge, con *Invia a Internet* abilitato:

.. list-table::
   :header-rows: 1
   :widths: 25 25 50

   * - Identificatore
     - Gruppo
     - Destinatario inviato, e dove arriva
   * - ``0``
     - ``2509``
     - ``BLN02509`` → talkgroup DMR 2509

I destinatari vedono ``<NOMINATIVO DEL MITTENTE> BLN02509 <testo>``. Un nome di
gruppo non numerico resta un bollettino di gruppo APRS perfettamente valido:
semplicemente non raggiungerà alcun talkgroup.

Interrogazioni
--------------

``?APRSP`` (posizione) e ``?APRSS`` (stato) indirizzate a una stazione DMR
funzionano per le radio con ARS/RRS/LRRP configurato.

Configurazione
==============

Tutto si trova nella pagina *BrandMeister*, collocata subito dopo *IGate* nel
menu:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Impostazione
     - Significato
   * - Abilita l'interconnessione BrandMeister
     - Interruttore principale. Con esso disattivato nessuna riga viene
       classificata, nessuna stazione viene contrassegnata e l'instradamento dei
       messaggi resta invariato.
   * - Sottoscrivi il traffico BrandMeister mondiale
     - Registra l'intenzione di usare la sottoscrizione ``u/APBM*`` e applica il
       requisito del filtro di distanza descritto sopra.
   * - Invia i messaggi alle stazioni BrandMeister solo via Internet
     - Abilitato per impostazione predefinita. Sopprime la tratta RF per un
       destinatario BrandMeister.
   * - Gateway 1–4
     - Nominativi facoltativi di stazione di ingresso per il terzo test del
       classificatore. Un ``*`` finale confronta per prefisso.

Il filtro di distanza da Internet a RF si trova nella pagina *IGate*, accanto al
suo gemello da RF a Internet, perché governa ogni riga che il flusso offre al
trasmettitore e non solo il traffico BrandMeister. La tabella di stato della
pagina *BrandMeister* ne riporta la condizione.

Ciò che deliberatamente non è implementato
==========================================

* Qualsiasi connessione DMR, Homebrew/MMDVM o OpenBridge.
* L'API REST di BrandMeister e il flusso LastHeard. Entrambi sono solo
  HTTPS/WSS e questo firmware è compilato senza stack TLS; inoltre restituiscono
  metadati di sessioni DMR e inventario di rete, non APRS.
* Qualsiasi riproduzione locale del modello di destinatario di un master. È
  proprio di ogni master e non è scopribile da qui.
