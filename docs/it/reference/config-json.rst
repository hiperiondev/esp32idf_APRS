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
     - L'``app_config_t`` residente (sistema, stazione, Wi-Fi, IGate, digi,
       tracker, meteo, GPS, modem, messaggio).
   * - ``/storage/telemetry.json``
     - Configurazione di telemetria (``telemetry_config_t``): analogici A1–A5,
       digitali B1–B8, parametri del report, interruttori dei messaggi di
       definizione.
   * - ``/storage/bulletins.json``
     - I cinque bollettini APRS (identificatore e gruppo del destinatario,
       testo, RF/INET, intervallo, scadenza).
   * - ``/storage/objitems.json``
     - I cinque oggetti/item APRS (nome, posizione, simbolo, rotta/velocità,
       commento, intervallo, flag permanente).

Tutti e quattro usano lo stesso scrittore a flusso, ciascuno sotto il proprio
mutex, ciascuno con un ``setvbuf()`` esplicito per evitare un'allocazione pigra di
grande buffer stdio a metà scrittura.

Reset di fabbrica
=================

``POST /default`` (il pulsante di *factory reset* della pagina System) chiama
``app_config_factory_reset()``, che riporta la configurazione a
``app_config_set_defaults()`` e la persiste. Da solo non elimina i file separati
di telemetria/bollettini/objitems — quelli rigenerano valori predefiniti al
prossimo accesso se cancellati tramite la pagina Storage.
