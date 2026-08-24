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
"Telegram smette di rispondere dopo un po' di funzionamento, con 'mbedtls_ssl_fetch_input' o 'Socket is not connected' nel log."
==================================================================================================================================

Il percorso di *polling* mantiene aperta la connessione HTTPS verso l'API di
Telegram tra un ciclo e l'altro, così un *long poll* che non restituisce nulla
non paga un nuovo handshake TLS ogni dieci secondi. Se quella connessione
resta inattiva abbastanza a lungo, il peer o un NAT intermedio può chiuderla
in silenzio; il socket resta quindi obsoleto anche se localmente nulla se n'è
accorto. ``telegram_bot_client_call()`` tratta un errore di trasporto proprio
come segnale di questo: chiude forzatamente la connessione e ritenta la
richiesta su un socket appena aperto, fino a tre tentativi totali con una
pausa che cresce tra loro, così una singola sessione obsoleta si recupera da
sola entro la stessa chiamata. Se l'errore continua a ripetersi su ogni
tentativo, è la rete stessa a essere giù e non un singolo socket obsoleto;
verifica la connettività Wi-Fi/Internet e il token del bot.

"sendMessage fallisce con 'ESP_ERR_HTTP_CONNECT' subito dopo l'arrivo di un aggiornamento, preceduto da 'Dynamic Impl: alloc(...) failed'."
===========================================================================================================================================

Un handshake TLS nuovo chiede all'heap i propri buffer di record come
allocazioni singole di pochi kilobyte ciascuna, quindi a decidere se riesce è
il più grande blocco **contiguo** libero, non l'heap libero totale.
L'allocatore dell'ESP-IDF registra il rifiuto come ``Dynamic Impl: alloc(...)
failed``, mbedTLS lo trasforma in ``mbedtls_ssl_handshake returned -0x008D``
e il trasporto vede ``ESP_ERR_HTTP_CONNECT``.

Una sessione TLS viva trattiene un blocco di dimensione paragonabile finché
viene mantenuta, perciò il firmware non ne tiene mai due insieme. Il gestore
di trasmissione lavora con il keep-alive disattivato ed è quindi vuoto nel
momento in cui la chiamata ritorna, e la connessione di polling viene
rilasciata da ``telegram_release_poll_connection()`` subito prima di
qualunque richiesta in uscita, che è il momento che conta: una risposta parte
subito dopo l'arrivo di un lotto di aggiornamenti, con il payload e l'albero
decodificato ancora in memoria. Il polling paga un handshake in più al ciclo
successivo e nulla d'altro.

Ogni tentativo fallito viene registrato con l'heap libero e il blocco libero
più grande in quell'istante. Se il blocco più grande è ampiamente sopra i
quattro kilobyte e la chiamata fallisce comunque, il guasto è il collegamento
e non l'heap. Se non lo è, al dispositivo manca davvero memoria contigua:
abbassa ``rx_buffer_size`` sui gestori del client, oppure riduci quello che il
resto del firmware trattiene in quel momento.

``CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN`` non è la leva che sembra. È un tetto sul
record che il peer può inviare, e la catena di certificati presentata da
Telegram si aggira sui quattro kilobyte in un solo record, quindi abbassarlo
sotto quella cifra non risparmia memoria: fa fallire l'handshake del tutto, a
ogni tentativo e con qualsiasi stato dell'heap.

"I pulsanti del menu continuano a girare e il log mostra 'query is too old and response timeout expired or query ID is invalid'."
=================================================================================================================================

Telegram invalida una callback query pochi secondi dopo la pressione del
pulsante. Rispondere è una richiesta a sé, e su questo dispositivo una
richiesta può costare un handshake TLS, quindi l'ordine in cui il lavoro
viene svolto decide se la risposta arriva ancora in tempo.

Tre cose la tengono dentro la scadenza. La query viene risposta prima che il
gestore del pulsante venga eseguito, non dopo, così costruire e inviare un
rapporto non ritarda mai la risposta. La connessione di trasmissione resta
aperta per l'intero lotto di aggiornamenti, così una raffica di pressioni
paga un handshake per tutte invece di uno ciascuna. E un singolo ciclo di
polling fallito non aggiunge più la propria pausa di cinque secondi sopra i
tentativi che il trasporto ha già speso, perché quella pausa è tempo che le
query in coda passano a invecchiare; la pausa torna non appena i fallimenti
si ripetono, cioè quando la rete è davvero giù.

Una query realmente troppo vecchia viene rifiutata da Telegram con un 400 e
il messaggio qui sopra, e il lotto a cui apparteneva viene comunque
elaborato. Se compare una volta dopo un fallimento di polling o una
riconnessione, la coda è semplicemente sopravvissuta ai suoi aggiornamenti.
Se compare con regolarità, il dispositivo non sta affatto stando dietro al
polling: cerca i fallimenti di polling sopra di esso nel log.

"Le richieste falliscono a caso con 'mbedtls_ssl_handshake returned -0x2700', con heap in abbondanza."
======================================================================================================

``-0x2700`` è ``MBEDTLS_ERR_X509_CERT_VERIFY_FAILED``: l'handshake TLS ha
raggiunto il server, ha scambiato messaggi e poi ha rifiutato il certificato
che gli è stato mostrato. Non c'era nulla di sbagliato nel collegamento né
nell'heap, ed è per questo che le cifre stampate accanto al fallimento
sembrano sane.

``api.telegram.org`` è servito da più di un front-end e non tutti si
agganciano alla stessa autorità di certificazione. Quando il trasporto valida
contro un file PEM invece che contro il bundle dell'ESP-IDF, quel file si
fida solo delle autorità che porta davvero, quindi un file con una sola
radice valida le connessioni che finiscono su un front-end compatibile e
fallisce le altre. Quale front-end restituisca il DNS varia tra un tentativo
e l'altro, ed è esattamente per questo che il guasto sembra casuale e che un
nuovo tentativo di solito riesce.

Il trasporto lo segnala esplicitamente. Una catena rifiutata viene registrata
come ``Peer certificate refused, verification flags 0x…, validating against
<percorso>``, e l'avvio registra quanti ancoraggi di fiducia ha prodotto il
file (``Loaded N trust anchors from …``). Un solo ancoraggio con ``-0x2700``
intermittente è la firma di questo problema.

Le soluzioni sono due. Concatenare le radici mancanti nel file PEM: ogni
certificato che contiene diventa un ancoraggio di fiducia, e il file è
sostituibile dalla pagina File Storage dell'admin web senza ricompilare.
Oppure selezionare ``TELEGRAM_BOT_CERT_BUNDLE`` in menuconfig e validare
contro il bundle di certificati fornito con ESP-IDF, che copre le autorità
pubbliche e continua a funzionare quando Telegram ruota la propria catena, al
prezzo di portare il bundle nell'immagine.

Si noti che ``CONFIG_MBEDTLS_HAVE_TIME_DATE`` non è abilitato in questo
firmware, quindi le date di validità dei certificati non vengono
controllate. Un orologio non sincronizzato non è mai qui la causa di
``-0x2700``.
