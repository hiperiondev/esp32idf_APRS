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

Schemi di percorso supportati
=============================

* **WIDEn-N** — l'alias di inondazione standard. Il contatore di hop *N* viene
  decrementato e l'indicativo del digipeater viene inserito (marcato come usato
  con ``*``) quando l'alias è consumato.
* **TRACEn-N** — come WIDEn-N ma ogni hop inserisce il proprio indicativo,
  costruendo una traccia esplicita del percorso seguito.
* **RELAY / GATE / ECHO** — gli alias generici ereditati, ciascuno sostituito
  dall'indicativo del digipeater.
* **WIDEn-N codificato nel campo SSID di destinazione** — la convenzione più
  vecchia in cui il contatore di hop vive nel nibble SSID dell'indirizzo di
  destinazione AX.25 è anch'essa riconosciuta e gestita.

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
