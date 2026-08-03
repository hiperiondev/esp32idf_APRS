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
       di portata/prefisso, indicativo/SSID/passcode, host/porta, stringa di
       filtro server, beacon on/off, posizione, intervallo, selettore di simbolo,
       oggetto, commento, stato, PHG.
   * - **Digi**
     - Abilita digipeater, indicativo/SSID e impostazioni beacon (posizione,
       simbolo, intervallo, commento, stato, percorso). Porta anche *Auto
       (WIDEn-N)*, *Ritardo di digipeatura* e *Finestra filtro duplicati* — vedi
       la nota più sotto.
   * - **Tracker**
     - Abilita tracker, indicativo/SSID, intervallo fisso, posizione, simbolo (in
       movimento/fermo), commento, opzioni di posizione compressa, posizione
       Mic-E e altitudine.
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
     - L'interfaccia di casella/composizione APRS (``/msgchat``).
   * - **Message**
     - Configura il motore di messaggistica (abilitazione RF/INET, ritentativo,
       cifratura, GPIO di allarme).
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
       dis-attivazione PTT, persistenza CSMA; e il pulsante **LOOP TEST**. Salva
       riapplica il modem in tempo reale — nessun riavvio.
   * - **Wireless**
     - Modalità (off/STA/AP/AP+STA), SSID/pass/canale dell'AP, 5 slot STA ciascuno
       con la propria casella Enable, potenza TX in dBm, più una scansione in
       tempo reale.
   * - **System**
     - Login web, hostname, frequenza CPU (applicata in tempo reale), host NTP ×3,
       intervallo di risincronizzazione, timeout di reset, e i quattro preset di
       percorso condivisi ``path[0..3]``.
   * - **Storage**
     - Navigatore LittleFS: scarica, elimina, upload multipart, uso, formatta.
   * - **About / Firmware**
     - Nome del progetto, versione, data/ora di compilazione, versione di IDF,
       partizione in esecuzione, e il pannello di **OTA Update**.

.. note::

   Tre controlli della pagina *Digi* — *Auto (WIDEn-N)* (``digiAuto``), *Ritardo
   di digipeatura* (``digiDelay``) e *Finestra filtro duplicati*
   (``digiFilter``) — sono accettati, validati e persistiti in ``config.json``,
   ma oggi nessun codice a runtime li legge. Il digipeater gestisce sempre
   WIDEn-N, ripete senza ritardo aggiunto, e prende la sua finestra di duplicati
   dal ``dup_cache_timeout_ms`` condiviso della pagina *IGate*. Sono documentati
   qui perché il comportamento non sia scambiato per un difetto.

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

Questo è deliberato. Con entrambe le funzioni disattivate (una configurazione
comune di solo-RX/monitor) la dashboard resterebbe inchiodata a zero per quanto
traffico venisse decodificato.

Feed in tempo reale
===================

* ``/lastheard`` — la tabella LAST HEARD (JSON), alimentata sia da RF sia da
  APRS-IS.
* ``/igate_traffic?since=<seq>`` — il delta del log di traffico (JSON). Ogni voce
  porta un'etichetta di direzione (``RX``/``TX``/``DIGI``/``INET2RF``/``RX-IS``),
  l'indicativo DX, il pacchetto grezzo, e il livello audio in mV RMS (o −1).
* ``/dashinfo``, ``/sidebarInfo``, ``/heapinfo`` — frammenti compatti di info in
  tempo reale.

Vedi :ref:`it-http-routes` per la tabella completa delle route.
