.. _it-networking:

====
Rete
====

La messa in funzione del Wi-Fi (``main/main.c``) è una delle parti più
strumentate del firmware, perché "sono passato alla modalità Station e non è
successo niente" era un fallimento silenzioso ricorrente nelle revisioni
precedenti. Ora ogni percorso registra ciò che ha fatto.

Modalità Wi-Fi
==============

``g_config.wifi_mode`` seleziona la configurazione di interfaccia, corrispondente
alla pagina Wireless:

* ``0`` = off
* ``1`` = STA (stazione)
* ``2`` = AP (punto di accesso) — il predefinito più sicuro; il dispositivo è
  sempre raggiungibile
* ``3`` = AP+STA

Sono memorizzati fino a cinque profili STA (``WIFI_STA_NUM = 5``), ciascuno con la
propria casella Enable. La **prima voce abilitata con un SSID non vuoto** è
spinta al driver; il failover multi-AP è annotato come "si può aggiungere più
avanti".

Connessione di stazione robusta
===============================

Diverse correzioni deliberate rendono affidabile il percorso di stazione:

* **Connettere da ``WIFI_EVENT_STA_START``, non immediatamente.**
  ``esp_wifi_connect()`` è legale solo una volta che l'interfaccia di stazione è
  effettivamente avviata, ciò che il driver segnala con ``WIFI_EVENT_STA_START``.
  Chiamarlo subito dopo ``esp_wifi_start()`` perde quella corsa e restituisce
  ``ESP_ERR_WIFI_NOT_STARTED`` — nessuna associazione, nessun evento di
  disconnessione, nessun ritentativo. La connessione è emessa dal gestore
  STA_START e ogni tentativo registra il suo risultato.
* **Intervallo di riconnessione fisso, armato su un timer.** Ogni disconnessione
  attende lo stesso ``RECONNECT_INTERVAL_MS`` (5 s) prima del successivo
  ``esp_wifi_connect()``, armato su un ``esp_timer`` — **non** un
  ``vTaskDelay()`` dentro il gestore
  di eventi, che fermerebbe il loop di eventi condiviso (incluso lo stesso
  ``STA_GOT_IP`` che attende) e, in un loop di disconnessione stretto, affamerebbe
  il task idle finché non scattasse il watchdog dei task.
* **Capacità PMF annunciata.** Un ``wifi_config_t`` azzerato lascia
  ``pmf_cfg.capable = false``, e gli AP WPA3 / WPA2-con-PMF-richiesto
  semplicemente rifiutano tale stazione. Il firmware imposta *capable, non
  required*, che funziona contro AP vecchi e nuovi.
* **Ripiego su AP+STA.** Solo-STA senza nulla a cui unirsi lascerebbe il
  dispositivo irraggiungibile, quindi ripiega su AP+STA e lo dice —
  l'amministrazione web resta attiva.
* **Dump diagnostici.** Se nessuno slot STA è abilitato con un SSID, il firmware
  scarica ogni slot e ti dice qual è l'errore ("abilitato, ma il SSID è VUOTO" vs
  "ha un SSID, ma 'Enable' non è spuntato").

I codici di ragione di disconnessione sono registrati (prima venivano scartati):

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Ragione
     - Significato
   * - 15 (4WAY_HANDSHAKE_TIMEOUT), 204 (NOT_AUTHED)
     - password sbagliata
   * - 201 (NO_AP_FOUND)
     - SSID non visibile: nome sbagliato, fuori portata, o solo 5 GHz
   * - 2 / 8 / 200
     - roaming ordinario / cadute dal lato AP

Il flag "abbiamo internet?"
===========================

``net_state.c`` mantiene un singolo booleano che diventa vero solo con
``IP_EVENT_STA_GOT_IP`` e falso alla disconnessione o in modalità solo-AP.
L'IGate lo interroga e attende un **IP reale** prima ancora di tentare una
connessione ad APRS-IS — essere semplicemente associati a un AP non basta.

Scansione Wi-Fi
===============

La scansione della pagina Wireless commuta temporaneamente una radio solo-AP ad
AP+STA. Un flag ``s_staEnabled`` condiziona ogni ``esp_wifi_connect()``
automatico così che il gestore di eventi non litighi con la scansione.

Potenza TX
==========

La potenza TX (dBm) della pagina Wireless è convertita ×4 in quarti di dBm per
``esp_wifi_set_max_tx_power()``. Questo prima veniva memorizzato e mostrato ma non
raggiungeva mai la radio.

Sincronizzazione oraria
=======================

``time_sync.c`` esegue SNTP contro tre host. Ora è una macchina a stati non
bloccante ripiegata nel tick di servizio a 1 Hz, e imposta l'orologio di
sistema su UTC (``TZ=UTC0``) — i timestamp zulu della specifica APRS lo
richiedono.

Lo stesso file contiene anche una tabella incorporata di offset UTC fissi (senza
regole di ora legale), selezionata da ``g_config.timezone_idx`` dalla sezione
*Time* della pagina System. È **solo una comodità di visualizzazione**:
``time_sync_format_local()`` somma l'``utc_offset_s`` della voce selezionata
all'istante UTC e rilegge il risultato tramite ``gmtime_r()``. La conversione è
pura aritmetica: non prende alcun lock, non alloca nulla e non scrive mai la
variabile ``TZ`` di processo, che viene impostata a ``UTC0`` una sola volta
durante l'avvio SNTP e non viene più riscritta. Questo conta perché la
dashboard interroga la data/ora rappresentata una volta al secondo per tutto il
tempo di accensione del dispositivo. L'orologio di sistema, tutti i timestamp
APRS e ogni altra chiamata di formattazione oraria del firmware (tutte con
``gmtime_r()``) restano in UTC indipendentemente dalla selezione.

Frequenza CPU
=============

``cpu_freq.c`` applica la selezione di 80/160/240 MHz della pagina System via
``esp_pm_configure()``. Senza questo, l'impostazione veniva memorizzata e mostrata
ma non cambiava mai il clock.
