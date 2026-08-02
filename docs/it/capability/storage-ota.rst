.. _it-storage-ota:

===================
Archiviazione e OTA
===================

LittleFS
========

La partizione ``storage`` da 512 KB è montata come **LittleFS** su ``/storage``
(``main/storage.c``). Al primo avvio si auto-formatta. Contiene ogni file
persistente che il firmware scrive:

* ``/storage/config.json`` — la configurazione residente (vedi
  :ref:`it-configuration`).
* ``/storage/telemetry.json`` — configurazione del canale di telemetria.
* ``/storage/bulletins.json`` — i cinque bollettini.
* ``/storage/objitems.json`` — i cinque oggetti/item.

La pagina web *Storage* è un navigatore LittleFS completo: elenca i file con le
dimensioni, scarica (``GET /download?file=…``), elimina (``POST /delete`` con
il nome file nel corpo del form), accetta
upload multipart (``/upload``), riporta l'uso e può riformattare il volume
(``/format``).

Perché LittleFS e non SPIFFS
============================

Anche se il sottotipo di partizione è ``spiffs`` (un'etichetta della tabella
delle partizioni), il volume è montato con il componente ``joltwallet/littlefs``.
LittleFS è resiliente alle interruzioni di corrente e ha il wear-leveling, ciò che
conta per un dispositivo che scrive la sua configurazione a ogni salvataggio di
impostazioni.

Salvataggi atomici e amici dell'heap
====================================

Ogni file JSON è scritto da un piccolo scrittore a flusso, token per token,
invece di costruire un albero cJSON completo e poi serializzarlo — perché ciò
necessiterebbe dell'albero **e** del suo buffer serializzato vivi
contemporaneamente su un heap piccolo e frammentato. Invece, lo scrittore scorre
direttamente nel file. Ogni salvataggio è **atomico**: scrive ``<file>.tmp`` e poi
rinomina. Ogni scrittore chiama anche ``setvbuf()`` con un buffer statico subito
dopo ``fopen()`` così che newlib non allochi pigramente un grande buffer stdio a
metà scrittura (una fonte sottile di un fallimento intermittente di doppia
eccezione su un heap frammentato che è stato tracciato e corretto).

Il caricamento è fatto con **cJSON**; i file mancanti o corrotti ripiegano su
valori predefiniti che vengono poi salvati immediatamente, così che ogni file
esista sempre e sia coerente. Le chiavi sconosciute in un file esistente sono
ignorate, così i vecchi file di configurazione si caricano comunque.

Aggiornamento firmware OTA
==========================

La tabella delle partizioni fornisce due slot app (``ota_0`` / ``ota_1``), ciò
che abilita l'aggiornamento OTA dalla pagina **About / Firmware**
dell'amministrazione web:

#. L'operatore sceglie un ``.bin`` e lo carica (``POST /ota_update``).
#. È trasmesso a flusso direttamente nello slot inattivo via ``esp_ota_write()``
   — mai memorizzato interamente in RAM — con una barra di progresso.
#. Una volta scritto e verificato, il dispositivo si riavvia nel nuovo slot.

**Rollback automatico.** ``CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`` è attivo,
quindi un'immagine appena scritta si avvia nello stato "in attesa di verifica".
Il firmware conferma l'immagine solo dopo aver montato NVS/LittleFS, avviato il
Wi-Fi e con l'amministrazione web in ascolto
(``esp_ota_mark_app_valid_cancel_rollback()`` in ``main.c``). Un'immagine difettosa
che non raggiunge mai quell'asticella viene automaticamente riportata allo slot
precedente al reset successivo.


