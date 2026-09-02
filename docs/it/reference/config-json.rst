.. _it-config-json:

==================================
Archiviazione della configurazione
==================================

La configurazione residente persiste in ``/storage/config.json`` su LittleFS.
Questo riferimento riassume la meccanica di archiviazione; per i gruppi di campi
vedi :ref:`it-configuration`.

Meccanica
=========

* **Percorso:** ``/storage/config.json``.
* **Caricato** con cJSON; **salvato** da uno scrittore a flusso token per token.
* **Salvataggio atomico:** scrive ``config.json.tmp``, poi rinomina.
* Mancante o corrotto → si applicano i default e si salvano immediatamente, così
  che il file esista sempre e sia coerente.
* I nomi dei campi / chiavi JSON sono mantenuti 1:1 con il progetto di
  riferimento, così i vecchi file si caricano senza modifiche; le chiavi
  sconosciute sono ignorate.

Altri file persistenti
======================

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - File
     - Contenuto
   * - ``/storage/config.json``
     - L'``app_config_t`` residente (sistema, stazione, Wi-Fi, IGate, BrandMeister, digi,
       tracker, meteo, GPS, modem, messaggio).
   * - ``/storage/telemetry.json``
     - Configurazione di telemetria (``telemetry_config_t``): analogici A1–A5,
       digitali B1–B8, parametri del report, interruttori dei messaggi di
       definizione.
   * - ``/storage/bulletins.json``
     - I cinque bollettini APRS (identificatore e gruppo del destinatario,
       testo, RF/INET, intervallo iniziale, rampa di decadimento, scadenza).
   * - ``/storage/objitems.json``
     - I cinque oggetti/item APRS (nome, posizione, simbolo, rotta/velocità,
       commento, intervallo, flag permanente).
   * - ``/storage/winlink.json``
     - Le risposte inviate dal servizio Winlink, dalla più vecchia. Le
       impostazioni dell'account sono chiavi ``wl*`` di ``config.json``; qui
       vivono solo le risposte, quindi cancellarle non tocca mai la
       configurazione.
   * - ``/storage/telegram.json``
     - L'intera configurazione del bot Telegram: l'interruttore di
       abilitazione, il token del bot, l'identificativo dell'amministratore,
       l'indirizzo della Mini App e gli elenchi di utenti e chat di gruppo
       autorizzati.

Tutti e sei usano lo stesso scrittore a flusso, ciascuno sotto il proprio
mutex, ciascuno con un ``setvbuf()`` esplicito per evitare un'allocazione pigra di
grande buffer stdio a metà scrittura. Quel buffer è un unico oggetto statico
condiviso da tutti e sei gli store, poiché il cancello di scrittura
dell'intero filesystem impedisce la sovrapposizione di due salvataggi.

Reset di fabbrica
=================

``POST /default`` (il pulsante di *factory reset* della pagina System) chiama
``app_config_factory_reset()``, che riporta la configurazione a
``app_config_set_defaults()`` e la persiste. Da solo non elimina i file separati
di telemetria/bollettini/objitems — quelli rigenerano valori predefiniti al
prossimo accesso se cancellati tramite la pagina Storage.

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
