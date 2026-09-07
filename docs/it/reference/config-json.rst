.. _it-config-json:

==================================
Archiviazione della configurazione
==================================

La configurazione residente persiste su LittleFS come **un file per
funzionalità dell'amministrazione web**, ciascuno con il nome della pagina che
lo possiede. Questo riferimento riassume la meccanica di archiviazione; per i
gruppi di campi vedi :ref:`it-configuration`.

Meccanica
=========

* **Un file per funzionalità**, sotto ``/storage``: ``system.json``,
  ``station.json``, ``wireless.json``, ``radio.json``, ``igate.json``,
  ``brandmeister.json``, ``digi.json``, ``tracker.json``, ``weather.json``,
  ``gps.json``, ``message.json``, ``winlink.json``, ``query.json``. Non esiste
  un file di configurazione combinato.
* **Caricato** con cJSON, un file alla volta; **salvato** da uno scrittore in
  streaming token per token.
* **Salvataggio atomico:** scrive ``<nome>.json.tmp``, poi rinomina.
* **Ogni pagina salva solo il proprio file.** La pagina *My Station* è
  l'eccezione che ne nomina diverse, perché i suoi specchi "Use My Station
  Data" scrivono in campi di altri cinque servizi.
* Mancante, vuoto o corrotto → quel file viene riscritto dai valori predefiniti
  durante il caricamento, così ogni funzionalità ha sempre un file e il
  dispositivo si avvia sempre su un'amministrazione web raggiungibile.
* Senza memoria → **non** viene scritto nulla e il caricamento segnala
  l'errore. Una lettura o un'analisi rimasta senza RAM non dice nulla sul
  contenuto del file, quindi non deve mai prendere la strada precedente: il
  lettore esamina il testo senza allocare nulla per distinguere i due casi, e
  solo i byte genuinamente non analizzabili vengono sovrascritti.
* I nomi dei campi e le chiavi JSON sono mantenuti 1:1 con il progetto di
  riferimento, così un operatore che si sposta tra i due li riconosce; le chiavi
  sconosciute vengono ignorate e una chiave che un file non porta conserva il
  suo predefinito documentato.

Altri file persistenti
======================

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - File
     - Contenuto
   * - ``/storage/telemetry.json``
     - Configurazione del canale 0 di telemetria (``telemetry_config_t``):
       analogici A1–A5, digitali B1–B8, parametri del rapporto, interruttori dei
       messaggi di definizione.
   * - ``/storage/bulletins.json``
     - I cinque bollettini APRS (identificativo e gruppo del destinatario,
       testo, RF/INET, intervallo iniziale, rampa di decadimento, scadenza).
   * - ``/storage/objitems.json``
     - I cinque oggetti/item APRS (nome, posizione, simbolo, rotta/velocità,
       commento, intervallo, flag permanente).
   * - ``/storage/telegram.json``
     - L'intera configurazione del bot Telegram: l'interruttore di abilitazione,
       il token del bot, l'identificativo dell'amministratore, l'indirizzo della
       Mini App e gli elenchi di utenti e chat di gruppo autorizzati.
   * - ``/storage/winlink_mail.json``
     - Le risposte che il servizio Winlink ha rimandato, dalla più vecchia alla
       più recente. Le impostazioni dell'account sono le chiavi ``wl*`` di
       ``winlink.json``; qui vivono solo le risposte, quindi cancellarle non
       tocca mai la configurazione.

Tutti gli archivi usano lo stesso scrittore in streaming, ciascuno sotto il
proprio mutex, ciascuno con un ``setvbuf()`` esplicito per evitare
un'allocazione pigra di un grande buffer stdio a metà scrittura. Il buffer di
``setvbuf()`` è un unico oggetto statico condiviso da tutti, dato che il gate di
scrittura dell'intero filesystem impedisce a due salvataggi di sovrapporsi.

Ognuno di questi file viene creato dai suoi valori predefiniti durante l'avvio
se non esiste, così un primo avvio lascia un insieme completo in flash senza che
l'operatore visiti una sola pagina.

Reset di fabbrica
=================

``POST /default`` (il pulsante di *reset di fabbrica* nella pagina Sistema)
chiama ``app_config_factory_reset()``, che riporta la configurazione a
``app_config_set_defaults()`` e riscrive **tutti** i file di sezione. Di per sé
non rimuove i file separati di telemetria/bollettini/objitems/telegram — quelli
rigenerano i predefiniti al prossimo accesso se cancellati dalla pagina
Archivio.

Chiavi dell'interconnessione BrandMeister
=========================================

.. list-table::
   :header-rows: 1
   :widths: 25 15 60

   * - Chiave
     - Tipo
     - Significato
   * - ``bmEn``
     - bool
     - Interruttore principale dell'interconnessione BrandMeister. Disattivato
       per impostazione predefinita.
   * - ``bmMonitor``
     - bool
     - Intenzione di usare la sottoscrizione mondiale ``u/APBM*``. Forzato a
       disattivato al caricamento quando ``inet2rf`` è attivo e
       ``inet2rfRangeEn`` disattivato, così un file modificato a mano non può
       aggirare l'interblocco.
   * - ``bmMsgInetOnly``
     - bool
     - Instrada i messaggi per destinatari BrandMeister solo via APRS-IS.
       Abilitato per impostazione predefinita; può soltanto togliere la tratta
       RF.
   * - ``bmGateways``
     - array di 4 stringhe
     - Nominativi facoltativi di stazione di ingresso per il terzo test del
       classificatore. Un ``*`` finale confronta per prefisso.
   * - ``inet2rfRangeEn``
     - bool
     - Abilita il filtro di distanza INET→RF. Disattivato per impostazione
       predefinita.
   * - ``inet2rfRangeKm``
     - numero
     - Raggio del filtro di distanza INET→RF in km, 0 = illimitato. Limitato a
       0…20038 al caricamento.

Chiavi Winlink (APRSLink)
=========================

.. list-table::
   :header-rows: 1
   :widths: 25 15 60

   * - Chiave
     - Tipo
     - Significato
   * - ``wlEnable``
     - bool
     - Interruttore principale del client Winlink. Spento per impostazione
       predefinita: il client ha bisogno di un account e di una password prima
       di poter fare qualcosa.
   * - ``wlServiceCall``
     - string
     - Nominativo del servizio APRSLink. ``WLNK-1`` per impostazione
       predefinita; un valore vuoto carica quel valore predefinito.
   * - ``wlPassword``
     - string
     - Password dell'account Winlink, fino a 16 caratteri. Non viene mai
       trasmessa: una sfida di accesso indica posizioni di caratteri e solo
       quei caratteri vengono rimandati indietro.
   * - ``wlUseMsgCall``
     - bool
     - Usa ``msgMycall`` come identità Winlink. Acceso per impostazione
       predefinita, perché quel nominativo è quello che la trama uscente porta
       con sé e quindi quello che il servizio vede.
   * - ``wlMyCall``
     - string
     - Identità Winlink quando ``wlUseMsgCall`` è spento. Il servizio apre la
       casella in base al suo nominativo base, senza l'SSID.
   * - ``wlAutoLogin``
     - bool
     - Apre da sé una sessione quando un comando viene accodato da inattivo.
       Acceso per impostazione predefinita.
   * - ``wlSessionMaxMin``
     - number
     - Durata locale della sessione in minuti, 5…180, 110 per impostazione
       predefinita. Tenuta sotto la scadenza di due ore del servizio stesso,
       così questa stazione abbandona la sessione per prima. Limitata al
       caricamento.
   * - ``wlPollMin``
     - number
     - Minuti tra le richieste spontanee della posta in attesa, 0…1440. 0 non
       chiede mai, ed è il valore predefinito. Limitato al caricamento.
   * - ``wlCommentEn``
     - bool
     - Aggiunge il contrassegno di notifica Winlink al commento del beacon,
       così il servizio sa che questa stazione legge la propria posta. Spento
       per impostazione predefinita.
   * - ``wlInetOnly``
     - bool
     - Tiene fuori dall'aria il traffico Winlink di questa stazione finché ha
       un collegamento APRS-IS. Acceso per impostazione predefinita; può solo
       togliere la tratta RF.
   * - ``wlGateExempt``
     - bool
     - Lascia che una risposta del servizio raggiunga la RF anche quando il suo
       destinatario si vede pure su APRS-IS. Acceso per impostazione
       predefinita; toglie quell'unica condizione di inoltro dei messaggi e
       nessun'altra, e solo per ``wlServiceCall``.
