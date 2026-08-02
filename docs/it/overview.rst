:orphan:

.. _it-overview:

==========
Panoramica
==========

Che cos'è
=========

``esp32idf_APRS`` è un progetto ESP-IDF **v5.x** (testato e bloccato su IDF
**5.5.4**) che trasforma un semplice ESP32 DevKit più una interfaccia audio
economica in una stazione APRS completa e autonoma. Tutto gira sull'ESP32 stesso
— non c'è un core Arduino, niente ``String``, niente PlatformIO e nessuna
libreria DSP esterna. L'intera catena di segnale, dal demodulatore a
correlazione attraverso il recupero dei bit DPLL, NRZI, l'incapsulatore HDLC, il
codec AX.25 e la correzione d'errore in avanti FX.25 Reed–Solomon, viene
eseguita sul microcontrollore usando solo il SAR-ADC in modalità continua/DMA,
il DAC e un timer generico.

In una frase: il firmware

* **demodula** l'audio AFSK/FSK dall'uscita altoparlante o discriminatore di una
  radio su **ADC1**,
* **decodifica** i frame HDLC/AX.25 (opzionalmente con correzione FX.25),
* li **inoltra** verso APRS-IS su Wi-Fi (``qAR``/``qAO``),
* li **digipeta** di nuovo in RF (WIDEn-N / TRACEn-N / RELAY / ECHO / GATE),
* trasmette **beacon** della propria posizione, meteo e telemetria,
* **modula** e ritrasmette i frame attraverso il **DAC a 8 bit** dell'ESP32,
  attivando la radio tramite un GPIO di PTT,
* ed è configurato interamente tramite un'**amministrazione web HTTP** servita
  dal dispositivo stesso — nessuna console seriale, nessuna ricompilazione per le
  impostazioni ordinarie.

Matrice delle funzionalità
==========================

.. list-table::
   :header-rows: 1
   :widths: 40 12 48

   * - Area
     - Stato
     - Note
   * - AFSK 1200 Bd Bell 202 (APRS standard)
     - ✅
     - doppio demodulatore, profilo predefinito
   * - AFSK 1200 Bd ITU V.23 (1300/2100 Hz)
     - ✅
     -
   * - AFSK 300 Bd (1600/1800 Hz)
     - ✅
     - stile HF
   * - G3RUH FSK 9600 Bd
     - ✅
     - richiede audio piatto/discriminatore
   * - Frame UI HDLC / AX.25 RX+TX
     - ✅
     - ``AX25_FRAME_MAX_SIZE = 329``
   * - FX.25 (RS FEC su AX.25)
     - ✅
     - modalità solo-RX / RX+TX
   * - Attivazione PTT (GPIO + polarità in compilazione)
     - ✅
     - GPIO validato; tempo minimo di **dis-attivazione** a runtime
   * - CSMA / slot temporale TX / p-persistenza / preambolo TXDelay
     - ✅
     - ``preamble``, ``tx_timeslot``, ``csma_persist``
   * - DCD (rilevamento portante dati)
     - ✅
     - derivato dal demodulatore; nessun ingresso squelch hardware
   * - IGate APRS-IS RF→INET
     - ✅
     - filtri, dedup, ``qAR``/``qAO``
   * - IGate APRS-IS INET→RF
     - ✅
     - gating per tipo + budlist + opzione unwrap di terze parti
   * - Gate di portata e di prefisso locale RF→INET
     - ✅
     - distanza haversine + whitelist di prefisso indicativo
   * - Whitelist / blacklist indicativi (budlist)
     - ✅
     - per direzione, si compone (AND) con i filtri di tipo
   * - Elenco digipeater satellitari (ISS)
     - ✅
     - fino a 8 voci, configurabile dal web (pagina IGate), senza ricompilare
   * - Dimensione e finestra della cache soppressione duplicati
     - ✅
     - configurabile dal web (pagina IGate), condiviso da IGate e Digipeater
   * - Digipeater
     - ✅
     - WIDEn-N, TRACEn-N, RELAY/ECHO/GATE, soppressione duplicati
   * - Oggetti / Item APRS della propria stazione
     - ✅
     - fino a 5, RF e/o INET, decadimento intervallo + kill-repeat
   * - Bollettini APRS (BLN1..BLN5)
     - ✅
     - fino a 5, RF e/o INET, scadenza per bollettino
   * - Interfaccia chat messaggi APRS (``/msgchat``)
     - ✅
     - pagina inbox/composizione sul motore di messaggistica
   * - Risponditore di query APRS (APRS101 cap.15)
     - ✅
     - generali ``?APRS?``/``?WX?``/``?IGATE?`` + l'insieme diretto
       (``?APRSD``/``?APRSH``/``?APRSM``/``?APRSO``/``?APRSP``/``?APRSS``/
       ``?APRST``/``?PING?``), con limiti di frequenza per tipo e per sorgente
   * - Beacon a posizione fissa (tracker / igate / digi)
     - ✅
     - un singolo task pianificatore di beacon condiviso
   * - Messaggistica APRS + ack/ritentativo
     - ✅
     - RF e/o INET
   * - Cifratura messaggi APRS AES-128-CBC
     - ✅
     - ``mbedtls``, IV derivato da MD5, payload in base64
   * - Amministrazione web (autenticazione HTTP Basic)
     - ✅
     - 17 pagine nella barra laterale + selettore di simbolo, dashboard in
       tempo reale
   * - Log traffico in tempo reale + tabella last-heard
     - ✅
     - long-poll JSON (``?since=<seq>``)
   * - Archiviazione LittleFS, upload/download/elimina/formatta
     - ✅
     - partizione da 512 KB
   * - Sincronizzazione oraria SNTP (3 host)
     - ✅
     - orologio sempre mantenuto in UTC
   * - Controllo frequenza CPU (80/160/240 MHz)
     - ✅
     - ``esp_pm_configure()``
   * - Wi-Fi AP / STA / AP+STA, scansione, potenza TX
     - ✅
     - 5 slot STA (si usa il primo abilitato)
   * - Localizzazione (EN / ES / IT)
     - ✅
     - in compilazione, una lingua per immagine
   * - Aggiornamento OTA
     - ✅
     - slot ``ota_0``/``ota_1``, rollback automatico su avvio fallito
   * - Report meteo APRS della propria stazione
     - ✅
     - refresh sensori a 1 Hz, media opzionale, beacon WX in onda
   * - Framework driver sensori locali (``sensors_local``)
     - ✅
     - registro dinamico a runtime, driver auto-registranti
   * - Codifica/beacon telemetria APRS in onda
     - ✅
     - analogici A1–A5 + digitali B1–B8, report ``T#nnn`` + metadati

Filosofia di progetto
=====================

Diverse decisioni architetturali deliberate ricorrono in tutto il codice e
vale la pena interiorizzarle subito:

**Una configurazione residente, una copia viva.**
   Una singola istanza ``app_config_t g_config`` è la fonte di verità che ogni
   sottosistema legge. Persiste su ``/storage/config.json``. I sottosistemi non
   duplicano mai lo stato di configurazione; leggono ``g_config`` direttamente.
   Due sottosistemi che necessitano di uno stato più grande e specifico della
   pagina lo mantengono in file LittleFS separati invece di gonfiare
   ``g_config``: telemetria (``/storage/telemetry.json``), bollettini
   (``/storage/bulletins.json``) e oggetti/item (``/storage/objitems.json``).

**Cablaggio di scheda in compilazione, tutto il resto a runtime.**
   I tre pin audio (ADC, DAC, PTT), la polarità del PTT, l'attenuazione dell'ADC
   e le frequenze di campionamento sono *costanti di compilazione* impostate nel
   ``CMakeLists.txt`` di livello superiore. Sono scelte di cablaggio fisico,
   quindi non sono esposte nell'amministrazione web. Tutto ciò che un operatore
   regola legittimamente senza ricablare — profilo di modulazione, preambolo,
   slot temporale, modalità FX.25, filtri, indicativi, intervalli — è
   modificabile a runtime e, nella maggior parte dei casi, applicato in tempo
   reale senza riavvio.

**Statistiche che riflettono la realtà, non la configurazione.**
   I contatori della dashboard (RF RX/TX, RF→INET, INET→RF, digi, drop, errore)
   sono tracciati nei punti dove i frame effettivamente scorrono,
   indipendentemente dal fatto che le funzioni IGate o digipeater siano
   abilitate — così una configurazione di puro monitoraggio solo-RX mostra
   comunque attività di decodifica reale invece di un muro di zeri.

**Fallire rumorosamente, fallire in sicurezza.**
   Il percorso di avvio del Wi-Fi è pesantemente strumentato: i codici di
   disconnessione sono registrati, un dispositivo solo-STA senza nulla a cui
   unirsi ripiega su AP+STA così l'amministrazione web resta raggiungibile, e le
   riconnessioni usano un back-off crescente armato su un timer invece di un
   ritardo bloccante dentro il loop degli eventi.

Lignaggio e riconoscimenti
==========================

Il progetto e il suo componente modem sono di **Emiliano Augusto González
(LU3VEA)**. Il lignaggio DSP del modem software risale a tre progetti
precedenti: **VP-Digi** (SQ8VPS), **ESP32APRS_Audio** (nakhonthai) e **LibAPRS**
(Mark Qvist). Lo schema di configurazione, la disposizione dell'amministrazione
web e la semantica della dashboard seguono il progetto di riferimento
**ESP32APRS** così che i file ``config.json`` esistenti e le aspettative
dell'operatore vengano mantenuti. Vedi :ref:`it-credits` per l'attribuzione e la
licenza complete.

Il firmware è rilasciato sotto la **GNU General Public License v3.0**.

.. warning::

   **Avviso legale radioamatoriale.** Trasmettere su frequenze radioamatoriali
   richiede una licenza valida per il tuo paese e banda. Imposta un indicativo
   reale — il valore predefinito è ``NOCALL`` — usa un passcode APRS-IS
   legittimo, rispetta il tuo piano di banda locale e le convenzioni di
   digipeating (``WIDE1-1,WIDE2-1`` *non* è sempre appropriato), e non inoltrare
   traffico ``NOGATE``/``RFONLY``. Sei responsabile di tutto ciò che questo
   dispositivo trasmette.

Confronto con il software APRS più diffuso
=============================================

Una stazione APRS viene normalmente assemblata da pezzi di software
separati, ognuno dei quali copre una parte del lavoro: un modem/TNC su
scheda audio, un client con mappa e interfaccia di messaggistica e, a
volte, un programma dedicato di digipeater o IGate eseguito su un PC o su
una scheda a singola board. La tabella seguente mette a confronto i
pacchetti più diffusi con ``esp32idf_APRS``, funzione per funzione, per
chiarire cosa un singolo ESP32 con questo firmware sostituisce e cosa no.

Il confronto copre **Direwolf** (WB2OSZ — il modem/TNC software de facto
standard per Linux/Windows/macOS, con digipeater e IGate inclusi),
**Xastir** (un client desktop X11/Linux maturo e molto configurabile con
mappatura estesa), **YAAC** ("Yet Another APRS Client", KA2DDO — un client
Java multipiattaforma con interfaccia moderna), **APRSIS32 / UI-View**
(client desktop solo Windows, storicamente dominanti, UI-View ormai
legacy/non più mantenuto) e **APRSdroid** (il client mobile Android più
diffuso). Questi programmi vengono spesso combinati tra loro — per
esempio Direwolf come modem/TNC che alimenta Xastir o YAAC come client —
piuttosto che usati completamente da soli; ``esp32idf_APRS`` è insolito
perché fornisce l'equivalente del modem, della logica di
gateway/digipeater *e* dell'interfaccia operatore in un unico firmware su
scheda singola.

.. list-table::
   :header-rows: 1
   :widths: 26 15 12 12 12 12 27

   * - Funzione
     - esp32idf_APRS
     - Direwolf
     - Xastir
     - YAAC
     - APRSIS32 / UI-View
     - Note
   * - Funziona in autonomia, senza PC host
     - ✅
     - ❌ (richiede un SO host)
     - ❌
     - ❌
     - ❌
     - Elemento distintivo di questo progetto: modem + logica + UI su un
       unico MCU.
   * - Modem software AFSK/FSK (scheda audio)
     - ✅ (ADC/DAC sul chip)
     - ✅ (scheda audio del PC)
     - ➖ (solitamente via Direwolf)
     - ➖ (solitamente via Direwolf/AGW)
     - ➖ (via TNC o AGW)
     - Solo Direwolf e questo progetto *sono* il modem; gli altri ne
       consumano uno.
   * - Supporto TNC hardware / KISS
     - ❌
     - ✅
     - ✅
     - ✅
     - ✅
     - Questo firmware è esso stesso il modem; non dialoga con un TNC
       esterno.
   * - Trame UI AX.25 RX/TX
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - Base comune a tutto il software APRS.
   * - FX.25 (FEC Reed–Solomon)
     - ✅
     - ✅
     - ❌
     - ➖ (solo lato client)
     - ❌
     - Direwolf e questo progetto codificano/decodificano FX.25
       direttamente.
   * - IGate (RF → APRS-IS)
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - Comune a quasi tutto il software APRS.
   * - IGate (APRS-IS → RF, "bidirezionale")
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - Generalmente filtrato ovunque da filtri locali/duplicati/di tipo.
   * - Digipeater (WIDEn-N / TRACEn-N)
     - ✅
     - ✅
     - ✅
     - ✅
     - ➖ (limitato)
     -
   * - Amministrazione web integrata
     - ✅
     - ❌ (file di config + interfacce web di terze parti opzionali)
     - ❌ (GUI nativa X11)
     - ❌ (GUI nativa Java Swing)
     - ❌ (GUI nativa Windows)
     - Questo firmware è l'unico configurato interamente da browser.
   * - Mappa in tempo reale
     - ❌
     - ❌ (solo testo/log)
     - ✅ (estesa)
     - ✅
     - ✅
     - Fuori ambito deliberatamente — questa è una stazione, non un client
       di mappatura.
   * - Tracciamento in tempo reale via GPS
     - ❌ (solo posizione fissa)
     - ➖ (via GPS/tracker collegato)
     - ✅
     - ✅
     - ✅
     - Segnalato come limitazione nota; vedi :ref:`it-limitations`.
   * - Beacon di posizione (stazione fissa)
     - ✅
     - ✅ (secondo configurazione)
     - ✅
     - ✅
     - ✅
     -
   * - Messaggistica APRS (chat, ack/ritentativo)
     - ✅ (UI di chat web)
     - ➖ (via client collegato)
     - ✅
     - ✅
     - ✅
     -
   * - Cifratura dei messaggi
     - ✅ (AES-128-CBC)
     - ❌
     - ❌
     - ❌
     - ❌
     - Non fa parte dello standard APRS; è un'estensione propria di questo
       progetto.
   * - Bollettini / annunci
     - ✅
     - ➖ (ritrasmette, non compone)
     - ✅
     - ✅
     - ✅
     -
   * - Oggetti / item
     - ✅ (fino a 5, della propria stazione)
     - ➖ (ritrasmette, non compone)
     - ✅
     - ✅
     - ✅
     -
   * - Report di stazione meteo
     - ✅ (framework sensori nativo)
     - ➖ (via software meteo esterno)
     - ✅ (via feed esterno)
     - ✅ (via feed esterno)
     - ✅ (via feed esterno)
     - Questo firmware legge i sensori e codifica i report WX da solo, sul
       chip.
   * - Telemetria (canali analogici/digitali)
     - ✅ (A1–A5, B1–B8, EQNS/UNIT/BITS)
     - ❌
     - ➖ (solo visualizzazione)
     - ➖ (solo visualizzazione)
     - ➖ (solo visualizzazione)
     -
   * - APRStt (gateway DTMF-to-APRS)
     - ❌
     - ✅
     - ❌
     - ❌
     - ❌
     - Non implementato in questo progetto.
   * - Aggiornamento firmware/software OTA
     - ✅ (doppio slot OTA, rollback automatico)
     - ➖ (gestore pacchetti del SO)
     - ➖ (gestore pacchetti del SO)
     - ➖ (gestore pacchetti del SO)
     - ➖ (installer manuale)
     - "OTA" qui è specifico del modello di aggiornamento firmware
       embedded.
   * - Interfaccia multilingua
     - ✅ (EN/ES/IT, a tempo di compilazione)
     - ❌ (solo inglese)
     - ➖ (traduzioni parziali)
     - ❌ (solo inglese)
     - ❌ (solo inglese)
     -
   * - Costo / ingombro hardware
     - Una singola scheda ESP32 (~5–10 USD) + interfaccia audio
     - PC/RPi + scheda audio + radio
     - PC/RPi + TNC + radio
     - PC/telefono + TNC + radio
     - PC Windows + TNC + radio
     -

Legenda: ✅ implementato / nativo · ➖ parziale, o disponibile solo tramite
un altro programma della catena · ❌ non implementato / non applicabile.

**Ciò che questo progetto implementa deliberatamente**, eguagliando il
nucleo di ciò che offre una stazione APRS desktop completa: il modem
stesso, il framing AX.25/FX.25, l'IGate bidirezionale, il digipeating, i
beacon, la messaggistica con cifratura, i bollettini, gli oggetti, il meteo
e la telemetria, tutto raggiungibile da un'interfaccia web self-hosted
senza software companion su PC.

**Ciò che questo progetto deliberatamente non implementa**: non ha
visualizzazione su mappa né tracciamento mobile basato su GPS (solo beacon
a posizione fissa; vedi :ref:`it-limitations`), e non include un gateway
APRStt da DTMF ad APRS. Questi elementi sono intenzionalmente fuori ambito
per una stazione embedded headless configurata via browser — un client di
mappatura complementare come YAAC, Xastir o `aprs.fi <https://aprs.fi>`__
resta il modo naturale per *visualizzare* il traffico che questo firmware
genera e ritrasmette.
