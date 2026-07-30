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

Prima di ripetere, il digipeater controlla il frame contro la stessa cache di
rilevamento duplicati usata dall'IGate (``isDuplicatePacket()``), così che un
frame già digipetato entro la finestra di soppressione non venga ritrasmesso — la
classica difesa contro il ping-pong tra digipeater.

Contatori
=========

``digi_get_stats()`` restituisce un ``digi_stats_t``:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Contatore
     - Significato
   * - ``rxPkts``
     - Pacchetti visti dal digipeater.
   * - ``txPkts``
     - Pacchetti digipetati (percorso modificato, ``digiProcess()`` ha
       restituito ``2``).
   * - ``dropRx``
     - Pacchetti scartati (duplicato, percorso filtrato, non per noi, già
       ritrasmesso).
   * - ``erPkts``
     - Pacchetti malformati (troppo corti / senza percorso).

.. note::

   Questi contatori per-funzione avanzano solo mentre ``digi_en`` è attivo. Il
   contatore ``digi`` di testata della dashboard è tracciato separatamente in
   ``aprs_service.c`` nel punto in cui il frame riscritto viene effettivamente
   trasmesso, quindi riflette la realtà indipendentemente dal fatto che altre
   funzioni siano abilitate.
