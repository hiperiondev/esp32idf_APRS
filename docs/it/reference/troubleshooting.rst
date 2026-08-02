.. _it-troubleshooting:

========================
Risoluzione dei problemi
========================

"Sono passato alla modalità Station, ho salvato, riavviato, e non succede niente."
==================================================================================

Leggi il log di avvio — questo percorso è molto strumentato:

* ``esp_wifi_connect()`` è legale solo una volta che la stazione è *davvero*
  avviata (``WIFI_EVENT_STA_START``). La connessione è emessa da quel gestore e
  ogni tentativo registra il suo risultato.
* Se nessuno slot di Client Wi-Fi è **abilitato con un SSID**, il firmware scarica
  ogni slot e ti dice qual è l'errore ("abilitato, ma il SSID è VUOTO" vs "ha un
  SSID, ma 'Enable' non è spuntato").
* Solo-STA senza nulla a cui unirsi ripiega su AP+STA così che l'amministrazione
  web resti attiva.

I codici di ragione di disconnessione sono registrati:

.. list-table::
   :header-rows: 1
   :widths: 44 56

   * - Ragione
     - Significato
   * - 15, 204
     - password sbagliata
   * - 201
     - SSID non visibile: nome sbagliato, fuori portata, o solo 5 GHz
   * - 2 / 8 / 200
     - roaming ordinario / cadute dal lato AP

"L'AP non associa affatto."
===========================

Un ``wifi_config_t`` azzerato lascia ``pmf_cfg.capable = false``, e gli AP
WPA3 / WPA2-con-PMF-richiesto rifiutano tale stazione. Il firmware imposta
*capable, non required*, che funziona contro AP vecchi e nuovi.

"L'avvio si blocca ~5 secondi."
===============================

Atteso: ``modem_init()`` si blocca mentre ``ModemCalibrateSampleRate()`` misura il
clock reale dell'ADC. Una volta per avvio.

"I beacon all'avvio non trasmettono."
=====================================

Atteso: ``aprs_service_start()`` gira prima di ``modem_init()``, quindi i beacon
precoci vengono scartati con un log di debug fino a ``s_modemReady``.

"Il LOOP TEST fallisce con 'nessun pacchetto ricevuto di ritorno'."
===================================================================

Controlla l'attenuazione dell'ADC: il DAC oscilla il rail completo mentre
un'attenuazione di 0 dB misura solo ~0–1,1 V, tagliando il tono oltre la capacità
del demodulatore di agganciare. Il componente cabla ``ADC_ATTEN_DB_12``, che è
corretto; se lo hai sovrascritto, ripristinalo. Conferma anche il cavo di loop
GPIO25 → GPIO33.

"L'IGate dice unverified."
==========================

``aprs_mycall`` / ``aprs_passcode`` sbagliati. Il banner è registrato; anche la
riga di login esatta, inclusa la stringa di filtro, così che un filtro malformato
sia visibile subito.

"Tutto funziona ma aprs.fi non mostra la mia stazione."
=======================================================

Beacon: abilita il beacon di posizione e almeno uno di ``loc2rf`` / ``loc2inet``,
e imposta coordinate reali. Ritrasmettere traffico non ti annuncia mai.

"9600 Bd perde frame."
======================

Quella è la patologia che la frequenza dell'ADC, la dimensione del frame di
conversione e la separazione dei core sono stati cambiati per correggere (vedi
:ref:`it-dsp-signal-chain`). Se hai sovrascritto ``MODEM_ADC_SAMPLERATE``,
``MODEM_ADC_CONV_FRAME``, ``MODEM_DAC_TIMER_CORE`` o ``MODEM_ADC_ISR_CORE``,
ripristinali. Conferma anche di alimentare audio **piatto/di discriminatore**.

"Il LED del PTT resta acceso in riposo."
========================================

La logica del PTT è corretta; la sua polarità è una costante di compilazione, e la
definizione di scheda distribuita è ``MODEM_PTT_ACTIVE_HIGH=1`` (attivo-alto) nel
``CMakeLists.txt`` di livello superiore. Attivo-alto significa che
riposo/non-attivato aziona il pin **basso** e attivato lo aziona alto;
attivo-basso è l'immagine speculare, quindi a riposo il pin resta alto e un LED su
quel pin resta acceso. Se il LED segue l'opposto di quanto ti aspetti, il tuo
stadio di pilotaggio inverte (un optoisolatore sì; un semplice NPN low-side no):
porta la macro all'altro valore e fai una ricompilazione pulita completa — il
valore è incorporato in ``afsk.c``, quindi una compilazione incrementale non lo
recepirà.
