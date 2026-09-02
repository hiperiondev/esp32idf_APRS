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
   * - ``/storage/telegram.json``
     - L'intera configurazione del bot Telegram: l'interruttore di
       abilitazione, il token del bot, l'identificativo dell'amministratore,
       l'indirizzo della Mini App e gli elenchi di utenti e chat di gruppo
       autorizzati.

Tutti e cinque usano lo stesso scrittore a flusso, ciascuno sotto il proprio
mutex, ciascuno con un ``setvbuf()`` esplicito per evitare un'allocazione pigra di
grande buffer stdio a metà scrittura. Quel buffer è un unico oggetto statico
condiviso da tutti e cinque gli store, poiché il cancello di scrittura
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
