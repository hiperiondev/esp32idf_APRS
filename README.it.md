# esp32idf_APRS

**Un IGate / Digipeater / Tracker APRS nativo ESP-IDF (C, senza Arduino) per l'ESP32, con amministrazione web integrata, un soft-modem AFSK/FSK on-chip (ADC + DAC) e uplink verso APRS-IS.**

> ⚠️ **Lavori in corso.** Il percorso di trasmissione RF, l'IGate, il digipeater, i beacon e l'amministrazione web sono funzionanti; sono presenti diverse pagine di configurazione le cui funzionalità non sono ancora implementate (vedi [Stato e limiti noti](#stato-e-limiti-noti)).

---

## Indice

- [Cos'è questo progetto](#cosè-questo-progetto)
- [Matrice delle funzionalità](#matrice-delle-funzionalità)
- [Hardware](#hardware)
  - [Target supportato](#target-supportato)
  - [Piedinatura / definizione della scheda](#piedinatura--definizione-della-scheda)
  - [Cablaggio tipico verso una radio](#cablaggio-tipico-verso-una-radio)
    - [Cosa presenta realmente ciascun capo](#cosa-presenta-realmente-ciascun-capo)
    - [Schema funzionale minimo](#schema-funzionale-minimo)
    - [Un'interfaccia funzionale completa per il Baofeng UV-5R](#unnterfaccia-funzionale-completa-per-il-baofeng-uv-5r)
    - [Perché il default del PTT è una trappola](#perché-il-default-del-ptt-è-una-trappola)
    - [Isolamento e loop di massa](#isolamento-e-loop-di-massa)
    - [Ordine di messa in servizio](#ordine-di-messa-in-servizio)
  - [Cablaggio da banco per il loopback](#cablaggio-da-banco-per-il-loopback)
- [Struttura del repository](#struttura-del-repository)
- [Architettura](#architettura)
  - [Sequenza di avvio](#sequenza-di-avvio)
  - [Mappa dei task](#mappa-dei-task)
  - [Flusso dei dati](#flusso-dei-dati)
- [Il componente modem](#il-componente-modem-esp32idf_radioamateur_modem)
  - [Catena del segnale](#catena-del-segnale)
  - [Perché i valori sono quelli che sono](#perché-i-valori-sono-quelli-che-sono)
  - [Riferimento alla configurazione a tempo di compilazione](#riferimento-alla-configurazione-a-tempo-di-compilazione)
  - [Configurazione a runtime (`modem_config_t`)](#configurazione-a-runtime-modem_config_t)
- [Componenti dell'applicazione](#componenti-dellapplicazione)
  - [`igate` — gateway APRS-IS](#igate--gateway-aprs-is)
  - [`digirepeater` — digipeater](#digirepeater--digipeater)
  - [`beacon` — beacon della propria stazione](#beacon--beacon-della-propria-stazione)
  - [`message` — messaggistica APRS](#message--messaggistica-aprs)
  - [`lastheard` / `trafficlog` — feed della dashboard](#lastheard--trafficlog--feed-della-dashboard)
  - [`webconfig` — amministrazione web](#webconfig--amministrazione-web)
- [Sensori](#sensori)
  - [Perché un framework di driver invece di una lista fissa](#perché-un-framework-di-driver-invece-di-una-lista-fissa)
  - [Le due famiglie di payload](#le-due-famiglie-di-payload)
  - [Anatomia di un driver (`sensor_local_driver_t`)](#anatomia-di-un-driver-sensor_local_driver_t)
  - [Il registro: come un driver viene trovato e chiamato](#il-registro-come-un-driver-viene-trovato-e-chiamato)
  - [Flusso dati end-to-end, dal sensore all'APRS](#flusso-dati-end-to-end-dal-sensore-allaprs)
  - [I due driver di esempio inclusi](#i-due-driver-di-esempio-inclusi)
  - [Aggiungere un nuovo sensore, passo per passo](#aggiungere-un-nuovo-sensore-passo-per-passo)
    - [1. Decidere cosa produce il driver](#1-decidere-cosa-produce-il-driver)
    - [2. Copiare uno scheletro e rinominarlo](#2-copiare-uno-scheletro-e-rinominarlo)
    - [3. Completare `init()`](#3-completare-init)
    - [4. Completare `save()`](#4-completare-save)
    - [5. Dichiarare il descrittore e auto-registrarlo](#5-dichiarare-il-descrittore-e-auto-registrarlo)
    - [6. Compilare — nient'altro da collegare](#6-compilare--nientaltro-da-collegare)
    - [7. Mapparlo nella pagina Weather](#7-mapparlo-nella-pagina-weather)
    - [8. Esempio pratico: un BME280 I2C reale](#8-esempio-pratico-un-bme280-i2c-reale)
  - [Istanze multiple dello stesso tipo di sensore](#istanze-multiple-dello-stesso-tipo-di-sensore)
  - [Gestione degli errori e fallimento del driver](#gestione-degli-errori-e-fallimento-del-driver)
  - [Thread safety](#thread-safety)
  - [Aggiungere un nuovo kind di sensore](#aggiungere-un-nuovo-kind-di-sensore)
  - [La pagina legacy `/sensor` — non è la stessa cosa](#la-pagina-legacy-sensor--non-è-la-stessa-cosa)
  - [Riepilogo di riferimento Sensori](#riepilogo-di-riferimento-sensori)
- [Compilazione e flashing](#compilazione-e-flashing)
- [Primo avvio e configurazione](#primo-avvio-e-configurazione)
- [Riferimento amministrazione web](#riferimento-amministrazione-web)
  - [Rotte HTTP](#rotte-http)
  - [Pagina per pagina](#pagina-per-pagina)
- [Memorizzazione della configurazione (`config.json`)](#memorizzazione-della-configurazione-configjson)
- [Preset del percorso e bitmask del percorso](#preset-del-percorso-e-bitmask-del-percorso)
- [Il LOOP TEST](#il-loop-test)
- [Localizzazione](#localizzazione)
- [Risoluzione dei problemi](#risoluzione-dei-problemi)
- [Stato e limiti noti](#stato-e-limiti-noti)
- [Crediti](#crediti)
- [Licenza](#licenza)

---

## Cos'è questo progetto

`esp32idf_APRS` è un progetto ESP-IDF **v5.x** che trasforma una semplice DevKit ESP32 più un'economica interfaccia audio in una stazione APRS completa:

* **demodula** l'audio AFSK/FSK proveniente dall'uscita altoparlante/discriminatore di una radio, sull'**ADC1**,
* **decodifica** i frame HDLC/AX.25 (opzionalmente con correzione d'errore forward FX.25),
* li **inoltra** verso APRS-IS via Wi-Fi (`qAR`/`qAO`),
* li **ridigipeta** in RF (WIDEn-N / TRACEn-N / RELAY / ECHO / GATE),
* **trasmette in beacon** la propria posizione,
* **modula** e ritrasmette i frame tramite il **DAC a 8 bit** dell'ESP32, comandando la radio con un GPIO di PTT,
* ed è configurato interamente tramite un'**amministrazione web HTTP** servita dal dispositivo stesso — nessuna console seriale, nessuna ricompilazione per le impostazioni ordinarie.

Tutto è scritto in C puro. Non c'è core Arduino, non c'è `String`, non c'è PlatformIO. L'intera catena DSP — demodulatore a correlatore, recupero bit tramite DPLL, NRZI, framer HDLC, codec AX.25, FEC Reed–Solomon FX.25 — gira sull'ESP32 stesso, usando solo il SAR-ADC in modalità DMA/continua, il DAC e un GPTimer.

---

## Matrice delle funzionalità

| Area | Stato | Note |
|---|---|---|
| AFSK 1200 Bd Bell 202 (APRS standard) | ✅ | doppio demodulatore, profilo predefinito |
| AFSK 1200 Bd ITU V.23 (1300/2100 Hz) | ✅ | |
| AFSK 300 Bd (1600/1800 Hz) | ✅ | in stile HF |
| G3RUH FSK 9600 Bd | ✅ | richiede audio piatto/discriminatore |
| RX+TX frame HDLC / AX.25 UI | ✅ | `AX25_FRAME_MAX_SIZE = 329` |
| FX.25 (FEC Reed-Solomon su AX.25) | ✅ | `-DENABLE_FX25`, modalità solo RX / RX+TX |
| Comando PTT (GPIO e polarità selezionabili a runtime) | ✅ | validato contro i pin di ADC/DAC/flash |
| CSMA / time-slot TX / preambolo TXDelay | ✅ | `preamble`, `tx_timeslot` |
| DCD (rilevamento portante dati) | ✅ | derivato dal demodulatore; nessun ingresso squelch hardware |
| IGate APRS-IS RF→INET | ✅ | filtri, deduplica, `qAR`/`qAO` |
| IGate APRS-IS INET→RF | ✅ | gating per tipo di payload `inet2rfFilter` (`aprs_filter.c`) |
| Digipeater | ✅ | WIDEn-N, TRACEn-N, RELAY/ECHO/GATE, soppressione duplicati |
| Oggetti / Item APRS della propria stazione | ✅ | `objects_items.c`, fino a 5, via RF e/o INET, decadimento dell'intervallo + ripetizioni di kill, `objitems.json` dedicato |
| Bollettini APRS (BLN1..BLN5) | ✅ | `bulletins.c`, fino a 5, via RF e/o INET, scadenza per bollettino, `bulletins.json` dedicato |
| UI chat messaggi APRS (`/msgchat`) | ✅ | pagina inbox/composizione sopra il motore di messaggistica (`ENABLE_MSG_CHAT`) |
| Beacon a posizione fissa (tracker / igate / digi) | ✅ | gestiti da un unico task scheduler condiviso (vedi [Mappa dei task](#mappa-dei-task)) |
| SmartBeaconing / tracker guidato da GPS | ❌ | i campi di configurazione esistono, la logica non è implementata |
| Messaggistica APRS + ack/retry | ✅ | RF e/o INET |
| Crittografia AES-128-CBC dei messaggi APRS | ✅ | `mbedtls`, IV derivato da MD5, payload in base64 |
| Amministrazione web (autenticazione HTTP Basic) | ✅ | ~22 pagine, dashboard live |
| Log di traffico live + tabella last-heard | ✅ | long-poll JSON (`?since=<seq>`) |
| Storage LittleFS, upload/download/cancellazione/formattazione | ✅ | partizione da 512 KB |
| Sincronizzazione oraria SNTP (3 host) | ✅ | l'orologio è sempre mantenuto in UTC |
| Controllo frequenza CPU (80/160/240 MHz) | ✅ | `esp_pm_configure()` |
| Wi-Fi AP / STA / AP+STA, scansione, potenza TX | ✅ | 5 slot STA (viene usato il primo abilitato) |
| Localizzazione (EN / ES / IT) | ✅ | a tempo di compilazione, una lingua per immagine |
| Aggiornamento OTA | ✅ | pagina About / Firmware dell'amministrazione web, slot `ota_0`/`ota_1`, rollback automatico in caso di errore di avvio |
| Modulo RF LoRa / SX127x-SX128x | ❌ | solo UI + configurazione, `ENABLE_RF_MODULE` è commentato |
| VPN WireGuard, MQTT, GNSS | ❌ | pagine/configurazione esistono; moduli disabilitati in `app_config.h` |
| Rapporto meteo APRS della propria stazione | ✅ | `weather.c`, refresh dei sensori a 1 Hz, media opzionale per campo, beacon WX reale via etere (RF e/o APRS-IS) — vedi [Sensori](#sensori) |
| Framework driver sensori locali (`sensors_local`) | ✅ | registro dinamico a runtime, driver auto-registranti, alimenta il selettore di canale della pagina Weather — vedi [Sensori](#sensori) |
| Codifica/beacon Telemetria APRS via etere | 🟡 | `sensors_local` può già raccogliere i valori dei canali analogici/digitali in `weather_telemetry_data_t`; non esiste ancora un encoder o un task di beacon `T#nnn`, quindi la pagina Telemetria è solo configurazione — vedi [Sensori](#sensori) |
| Pagina legacy per-slot `/sensor` (`g_config.sensor[]`) | ❌ | il sorgente della pagina (`page_sensor.c`) è orfano — non viene più compilato né instradato — e i suoi campi `g_config.sensor[]` sono stati rimossi; usare il framework `sensors_local`, vedi [Sensori](#sensori) |
| Bluetooth, PPP/GSM, display OLED, Modbus | ❌ | campi di configurazione mantenuti solo per compatibilità |

Legenda: ✅ implementato · 🟡 parziale · ❌ non implementato (solo impalcatura)

---

## Hardware

### Target supportato

* **ESP32** (classico, dual-core Xtensa) — `CONFIG_IDF_TARGET=esp32`, 4 MB di flash.
* Il dual-core **non è opzionale**: la ISR dell'ADC e il clock di campionamento del DAC sono assegnati intenzionalmente a core *diversi* (vedi [Perché i valori sono quelli che sono](#perché-i-valori-sono-quelli-che-sono)).
* L'ESP32-S2 ha i DAC sui GPIO17/18 e richiederebbe di modificare l'header di configurazione. **Gli ESP32-S3/C3/C6/H2 non hanno alcun DAC** e non possono eseguire il percorso TX senza modifiche.

### Piedinatura / definizione della scheda

La definizione della scheda si trova nel **`CMakeLists.txt` di primo livello**, applicata *prima* di `project()` tramite `idf_build_set_property(COMPILE_DEFINITIONS ... APPEND)`:

```cmake
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_ADC_GPIO=33"      APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_DAC_GPIO=25"      APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_PTT_GPIO=26"      APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_PTT_ACTIVE_HIGH=0" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_LED_TX_GPIO=-1"   APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_LED_RX_GPIO=-1"   APPEND)
```

| Segnale | Predefinito | Vincoli rigidi |
|---|---|---|
| **Ingresso audio (ADC)** | `GPIO33` (ADC1_CH5) | **Solo 32–39.** L'ADC2 è inutilizzabile quando il Wi-Fi è attivo, e questo firmware ha sempre il Wi-Fi attivo. Imposto con `#error`. |
| **Uscita audio (DAC)** | `GPIO25` (DAC_CHAN_0) | **Solo 25 o 26.** Il DAC dell'ESP32 è cablato in modo fisso a quei pad e non è instradabile tramite la matrice GPIO. Imposto con `#error`. |
| **PTT** | `GPIO26`, attivo **basso** | Il valore a tempo di compilazione è solo il *default di riserva*; il pin/polarità effettivi del PTT sono **selezionabili a runtime nella pagina Radio**, validati da `afsk_ptt_gpio_is_valid()` (rifiuta i GPIO34–39 solo input, i GPIO6–11 flash/PSRAM, e i pin stessi di ADC/DAC). `-1` = disabilitato. |
| **LED TX / RX** | disabilitati (`-1`) | Qualsiasi GPIO capace di output. |

> ⚠️ Con i valori predefiniti di fabbrica, **sia `MODEM_DAC_GPIO=25` che `MODEM_PTT_GPIO=26` fanno parte della coppia di pad del DAC.** Questo va bene (26 è solo DAC_CHAN_1 se lo *selezioni*), ma se sposti l'uscita audio sul GPIO26 devi spostare il PTT altrove. Il validatore rifiuterà la sovrapposizione a runtime.

> **Nota sui campi GPIO della pagina "Mod".** `g_config.adc_gpio`, `dac_gpio`, `rf_sql_gpio`, `rf_pwr_gpio`, `adc_atten`, `sql_level`, `volume`, `agc_max_gain` vengono ancora caricati, salvati e sono modificabili — ma **nulla nel percorso del modem audio li legge più** dopo la sostituzione `esp32_IDF_libAPRS` → `esp32idf_radioamateur_modem`. Sono mantenuti solo affinché i file `config.json` esistenti vengano letti senza modifiche. Per spostare i pin audio, modifica il blocco `CMakeLists.txt` sopra e ricompila.

### Cablaggio tipico verso una radio

Nessuno dei due capi di questo collegamento può essere connesso direttamente all'altro. Il lato ESP32 è un'interfaccia a dati campionati, a 3,3 V, con polarizzazione DC; il lato radio è un'interfaccia analogica in AC, riferita a massa, a livello di millivolt. Tra i due devono avvenire tre cose: **attenuazione** (TX), **traslazione e clamping** (RX) e **commutazione** (PTT).

#### Cosa presenta realmente ciascun capo

Ogni valore lato ESP32 riportato di seguito è derivato dalle costanti proprie del componente definite a tempo di compilazione, non da un ideale da datasheet — vedi [Riferimento alla configurazione a tempo di compilazione](#riferimento-alla-configurazione-a-tempo-di-compilazione).

| Nodo | Cosa c'è realmente | Da dove deriva |
|---|---|---|
| **GPIO25 (DAC), in trasmissione** | 1,65 V DC con un'oscillazione sovrapposta di **≈1,97 Vpp** (codici 52…204 → 0,67–2,64 V) ⇒ **≈0,70 Vrms** per un'onda sinusoidale, più **immagini di ricostruzione intorno a 38,4 kHz** | `DAC_MID = 128`, `MODEM_DAC_AMPLITUDE_PCT = 60`, `MODEM_DAC_SAMPLERATE = 38400` |
| **GPIO25 (DAC), a riposo / prima di `modem_init()`** | ~1,65 V una volta inizializzato; **indefinito e fluttuante durante il reset e i primi ~5 s di avvio** (calibrazione del clock ADC) | `modem_init()` blocca per ~5 s |
| **GPIO33 (ADC)** | Finestra **0–3,1 V**, normalizzata come `(raw − dc_avg)/2048`, cioè ±1,0 ≙ ±1,55 V. L'AGC punta a **310 mVrms** al pin, lo raggiunge partendo anche da soli **≈39 mVrms** (`AGC_MAX_GAIN = 8`), mantiene il guadagno sotto **≈16 mVrms** (rumore di fondo), e **taglia sopra ≈1,1 Vrms** | `MODEM_ADC_ATTEN = ADC_ATTEN_DB_12`, `AGC_TARGET_RMS = 0.2` |
| **GPIO26 (PTT)** | Semplice uscita CMOS a 3,3 V, **attiva BASSA con la definizione di scheda di fabbrica**, e un **ingresso fluttuante durante il reset** | `MODEM_PTT_GPIO=26`, `MODEM_PTT_ACTIVE_HIGH=0` |
| Rig **MIC IN** (jack microfono a mano) | 5–20 mVrms, spesso con pre-enfasi e una polarizzazione DC per l'elettret | richiede ≈30–40 dB di attenuazione |
| Rig **DATA IN** (mini-DIN-6 piedino 1, "PKT IN") | ≈40 mVpp ⇒ **≈14 mVrms**, piatto, senza pre-enfasi | richiede ≈35 dB di attenuazione |
| Rig **SPKR / AF OUT** | 0,1–3 Vrms, dipendente dalla manopola del volume, de-enfatizzato | richiede attenuazione + polarizzazione |
| Rig **DATA OUT / DISC** (mini-DIN-6 piedino 4) | 100–300 mVrms, **livello fisso, indipendente dallo squelch, piatto** | richiede solo polarizzazione — questa è l'opzione buona |

Due conseguenze da tenere bene a mente prima di saldare:

* **Il DAC è ~35 dB troppo "caldo"** per qualsiasi ingresso della radio. Un collegamento diretto non si limiterà a sovradeviare, causerà distorsione grave.
* **Una porta `DATA OUT` è già dentro la finestra dell'AGC** (100–300 mVrms contro l'intervallo utilizzabile di 39 mVrms–1,1 Vrms). Se il tuo apparato ha un jack dati, il lato RX è solo una rete di polarizzazione — niente potenziometro, niente guadagno.

#### Schema funzionale minimo

Passivo, ~15 componenti, nessun op-amp. Questo è tutto.

```
 ── TX ── ESP32 ─────────────────────────────────────────────────────► rig ──

  GPIO25 ──[R1 2k2]──┬──[R2 2k2]──┬──[C1 10µ]──[R3 10k]──┬─ RV1 top
   (DAC)             │            │      +               │
                   [C2 15n]     [C3 15n]              [RV1 1k]  trim di livello
                     │            │                      ├─ wiper ──► MIC / DATA IN
                    GND          GND                     │
                                                         └─ bottom ─► GND audio del rig

 ── RX ── rig ──────────────────────────────────────────────────────► ESP32 ──

                                          nodo di polarizzazione
  SPKR/DISC ─[RV2 10k]─ wiper ─[C4 10µ]──────┬──────[R7 1k]───┬──► GPIO33 (ADC)
                 │                +          │                │
   rig GND ──────┘                     [R5 10k]─► 3V3     [D1]─► 3V3   BAT54S
   (omettere RV2 per un DATA OUT fisso:       │             [D2]─► GND   (o 2×1N4148)
    collegare direttamente a C4)         [R6 10k]─► GND        │
                                            │              [C5 1n]
                                           GND                │
                                                             GND

 ── PTT ── opzione A: opto, isolato, corrisponde al default attivo-BASSO di fabbrica ──

                      ┌─ PC817 ─┐
   3V3 ──[R8 470]──[A]│▶      C│──────────► rig PTT
   GPIO26 ─────────[K]│        E│──────────► rig PTT GND
                      └─────────┘
   GPIO26 BASSO → LED acceso → chiuso in trasmissione.   Fluttuante al reset → LED spento → non trasmette.

 ── PTT ── opzione B: MOSFET lato basso, non isolato, richiede ACTIVE_HIGH=1 ──

                       ┌─ 2N7000 / BS170 ─┐
   GPIO26 ──[R9 1k]────┤ G              D ├───► rig PTT
                       │                S ├───► GND (comune)
          [R10 10k]────┤G→S               │
                       └──────────────────┘
   GPIO26 ALTO → chiuso in trasmissione.   R10 lo mantiene non trasmittente durante reset e deep sleep.
```

| Rif | Valore | Funzione | Se lo modifichi |
|---|---|---|---|
| **R1, R2 / C2, C3** | 2k2 / **15 nF** | Filtro passa-basso di ricostruzione a due poli, **fc ≈ 4,8 kHz**. Elimina le immagini del DAC a 38,4 kHz di **≈−36 dB** costando solo −0,3 dB a 1200 Hz e −0,8 dB a 2200 Hz (≈0,5 dB di twist) | **22 nF** (fc 3,3 kHz, −43 dB a 38,4 kHz) se usi solo AFSK e vuoi eliminare le immagini più a fondo; **10 nF** (fc 7,2 kHz, −29 dB) è **obbligatorio per G3RUH a 9600 Bd**, che richiede una risposta piatta oltre i ~5 kHz |
| **C1** | 10 µF | Blocco DC. La polarizzazione a riposo di 1,65 V del DAC non deve mai raggiungere un ingresso microfonico | Verso R3+RV1 = 11 kΩ questo dà fc ≈ 1,4 Hz — non scendere sotto 1 µF |
| **R3 / RV1** | 10k / trimmer 1k | Attenuazione + livello. Il partitore fisso 11:1 fa sì che il trimmer lavori nell'intervallo **0–64 mVrms**, con la metà corsa a ≈32 mV, invece di stare confinato nel 4% inferiore di un semplice potenziometro da 10k | Impedenza del wiper ≤250 Ω, quindi pilota qualsiasi ingresso mic o dati senza ulteriore carico |
| **RV2** | 10k | Livello RX. **Da omettere del tutto per una porta `DATA OUT`** — è già al livello giusto | |
| **C4** | 10 µF | Blocco DC + passa-alto. Con la Thévenin di polarizzazione da 5 kΩ: fc ≈ 3 Hz | |
| **R5, R6** | 10k / 10k | Polarizzazione a metà rail, **1,65 V**, esattamente al centro della finestra ADC 0–3,1 V. Thévenin 5 kΩ | Scendere a **4k7/4k7** (2,35 kΩ) se si notano errori di livello — il SAR dell'ESP32 vuole un'impedenza di sorgente bassa |
| **R7 / C5** | 1k / 1 nF | Snubber per il colpo di carica del condensatore di campionamento del SAR. **Non** è un filtro audio: fc ≈ 159 kHz | **Mai** montare 100 nF qui, il classico riflesso da "disaccoppiamento ADC" — con R7 questo diventa un passa-basso a 1,6 kHz e mangia il tono di marca a 2200 Hz |
| **D1, D2** | BAT54S | Clampano GPIO33 ai rail. R7 limita la corrente di guasto. Assicurazione economica contro una manopola del volume a 3 Vrms | |
| **R8** | 470 Ω | ≈4,5 mA attraverso il LED del PC817, assorbiti da GPIO26 — ben dentro il budget di 12 mA confortevole / 20 mA assoluto | |
| **R10** | 10k | **Il componente che la gente dimentica.** Senza di esso il gate del MOSFET fluttua durante il reset e il rig può chiudersi in trasmissione all'accensione | |

#### Un'interfaccia funzionale completa per il Baofeng UV-5R

Il Baofeng UV-5R (e la maggior parte dei suoi cloni in stile Kenwood-K1 a due piedini — UV-82, BF-888, GT-3, RT-5R, ecc.) **non** espone un unico jack combinato mic/altoparlante/PTT. Ne espone due:

| Spinotto | Dimensione | Contatti | Segnale |
|---|---|---|---|
| **Spinotto grande** | 3,5 mm, TS (mono) | Punta / Manicotto | Punta = **uscita audio SPKR**, Manicotto = **GND** |
| **Spinotto piccolo** | 2,5 mm, TRS | Punta / Anello / Manicotto | Punta = **ingresso MIC**, Anello = **PTT** (cortocircuito verso il manicotto per trasmettere), Manicotto = **GND** |

Questa è l'intera interfaccia: il TX è un segnale a livello microfonico sulla punta dello spinotto piccolo, l'RX è un segnale a livello altoparlante in uscita dalla punta dello spinotto grande, e il PTT è una **chiusura di interruttore** tra l'anello e il manicotto dello spinotto piccolo — non un livello logico letto dalla radio, solo un cortocircuito. Questo corrisponde esattamente allo [schema funzionale minimo](#schema-funzionale-minimo) di cui sopra; cambiano solo i pin di destinazione:

```
 ── TX ── ESP32 ───────────────────────────────────────────────────► spinotto piccolo UV-5R ──

  GPIO25 ──[R1 2k2]──┬──[R2 2k2]──┬──[C1 10µ]──[R3 10k]──┬─ RV1 top
   (DAC)             │            │      +               │
                   [C2 15n]     [C3 15n]              [RV1 1k]  trim di livello
                     │            │                      ├─ wiper ──► PUNTA 2,5 mm  (MIC)
                    GND          GND                     │
                                                         └─ bottom ─► MANICOTTO 2,5 mm (GND)

 ── RX ── spinotto grande UV-5R ───────────────────────────────────► ESP32 ──

                                          nodo di polarizzazione
  PUNTA 3,5mm (SPKR) ─[RV2 10k]─ wiper ─[C4 10µ]──┬──────[R7 1k]───┬──► GPIO33 (ADC)
                 │                    +           │                │
   MANICOTTO 3,5mm ─┘                       [R5 10k]─► 3V3     [D1]─► 3V3   BAT54S
   (GND, comune col manicotto dello spinotto piccolo)        │      [D2]─► GND   (o 2×1N4148)
                                           [R6 10k]─► GND        │
                                               │              [C5 1n]
                                              GND                │
                                                                GND

 ── PTT ── cortocircuita l'anello dello spinotto piccolo col suo manicotto — opzione A, B o C, invariate ──

                      ┌─ PC817 ─┐
   3V3 ──[R8 470]──[A]│▶      C│──────────► ANELLO 2,5 mm
   GPIO26 ─────────[K]│        E│──────────► MANICOTTO 2,5 mm (GND)
                      └─────────┘
   GPIO26 BASSO → LED acceso → anello in corto col manicotto → trasmette.

 ── PTT ── opzione C: transistor NPN semplice, non isolato, richiede ACTIVE_HIGH=1 ──

                       ┌─ 2N2222 / BC547 ─┐
   GPIO26 ──[R9 1k]────┤ B              C ├───► ANELLO 2,5 mm
                       │                E ├───► MANICOTTO 2,5 mm (GND)
          [R10 10k]────┤ B→E              │
                       └──────────────────┘
   GPIO26 ALTO → scorre corrente di base → C-E conduce → anello in corto col manicotto → trasmette.
   R10 mantiene la base bassa (non trasmittente) durante reset e deep sleep — stesso ruolo di R10 nell'opzione MOSFET.
```

Tutto ciò che sta a sinistra degli spinotti — R1–R3, RV1, C1–C4, R5–R7, D1–D2, C5, R8 (o R9/R10 per l'opzione C) — è identico alla [tabella dei componenti](#schema-funzionale-minimo) sopra; cambiano solo i punti di arrivo, da "rig MIC/DATA IN" e "rig SPKR/DISC" alle punte dello spinotto piccolo e grande dell'UV-5R.

L'opzione C scambia l'isolamento dell'opto con la comodità di usare ciò che si ha già in cassetto: qualsiasi NPN per piccoli segnali funziona (2N2222, BC547, PN2200, S8050 — `hFE` ≥ 100 è più che sufficiente, dato che la corrente di collettore qui è di appena pochi mA attraverso i contatti dell'interruttore PTT dello spinotto K), ed è una realizzazione a due resistori e un transistor invece di dover procurarsi un optoisolatore. Come l'opzione MOSFET, **non isola** la massa dell'ESP32 da quella della radio, ed è **attiva-ALTA** — impostare `MODEM_PTT_ACTIVE_HIGH=1` (default a tempo di compilazione o l'interruttore a runtime nella pagina **Radio**) di conseguenza, esattamente come per l'opzione B.

Alcune cose specifiche di questa radio:

* **Nessun DATA IN / DATA OUT.** L'UV-5R non ha un jack discriminatore, quindi non c'è modo di raggiungere il percorso piatto a livello fisso di cui ha bisogno la modalità G3RUH a 9600 Bd di questo progetto. Attraverso il connettore a 2 piedini di serie, **AFSK 1200 Bd Bell 202 è il tetto realistico.**
* **Il livello del microfono rientra nella banda generica "Rig MIC IN"** della [tabella su cosa presenta realmente ciascun capo](#cosa-presenta-realmente-ciascun-capo) — da pochi mV a poche decine di mV — quindi la rete di attenuazione R3/RV1 viene usata esattamente come specificato; iniziare con RV1 vicino a metà corsa e regolare per ≈3 kHz di deviazione secondo l'[ordine di messa in servizio](#ordine-di-messa-in-servizio).
* **L'uscita altoparlante dipende dalla manopola del volume.** Fissare il volume dell'UV-5R su un'impostazione bassa-moderata e ripetibile (segnare la manopola) e fare la regolazione di livello con RV2, non con il controllo del volume della radio — l'AGC ha il minimo margine agli estremi del suo intervallo.
* **Il VOX non viene usato.** Il PTT è pilotato direttamente dall'interruttore opto, MOSFET o transistor, quindi lasciare disattivato il VOX della radio; il VOX in conflitto con un PTT a corto diretto è un buon modo per ottenere i primi caratteri troncati o una chiave bloccata in trasmissione.
* **Scegliere tra A, B e C:** l'opzione A (opto) è l'unica delle tre che isola la massa dell'ESP32 da quella della radio, e non richiede alcun cambio di polarità rispetto al default di fabbrica — è quella da provare per prima. Le opzioni B e C sono entrambe interruttori lato basso non isolati e attivi-ALTI, che differiscono solo per quale componente si ha già a portata di mano (MOSFET vs NPN per piccoli segnali); su un banco dove ronzio e ingresso RF non sono un problema, vanno bene entrambe.
* **Verificare la piedinatura prima di saldare.** I cavi economici con spinotto K a 2 piedini non sono tutti cablati allo stesso modo — alcuni cavi di terze parti scambiano quale contatto dello spinotto piccolo sia il mic e quale il PTT. Controllare lo spinotto con un multimetro rispetto alla tabella sopra prima di procedere; una coppia invertita o lascia il mic fluttuante (niente audio TX) o cortocircuita il PTT permanentemente (la radio si chiude in trasmissione nell'istante in cui viene collegata).
* Le indicazioni su massa e isolamento di [Isolamento e loop di massa](#isolamento-e-loop-di-massa) si applicano senza modifiche — i manicotti dello spinotto piccolo e grande sono lo stesso nodo all'interno della radio, quindi vanno trattati come un unico riferimento di massa.

#### Perché il default del PTT è una trappola

La definizione di scheda di fabbrica è `MODEM_PTT_ACTIVE_HIGH=0` — **il GPIO26 viene portato BASSO per chiudere in trasmissione**. Il circuito riflesso "NPN con la base pilotata dal GPIO, collettore sul PTT" è un pilotaggio **attivo-ALTO** e chiuderà in trasmissione il tuo apparato per tutto il tempo in cui l'ESP32 *non* sta trasmettendo, cioè permanentemente.

Quindi: scegliere un driver la cui polarità corrisponda alla configurazione, oppure modificare la configurazione perché corrisponda al proprio driver.

* **L'opzione A (opto)** inverte, quindi corrisponde al default di fabbrica `ACTIVE_HIGH=0` così com'è, e offre isolamento galvanico gratuitamente.
* **L'opzione B (MOSFET)** non inverte. Impostare `MODEM_PTT_ACTIVE_HIGH=1` nel `CMakeLists.txt` di primo livello, oppure invertire la polarità a runtime nella pagina **Radio** (sia il pin sia la polarità sono selezionabili a runtime — vedi [Piedinatura / definizione della scheda](#piedinatura--definizione-della-scheda)).

In ogni caso, **verificare prima di collegare la radio**: alimentare la scheda e, con un multimetro, confermare che la linea PTT resta aperta durante il reset, durante l'intero avvio di ~5 s e durante l'inattività. `modem_init()` blocca per circa 5 secondi calibrando il clock dell'ADC, e i task di beacon trasmettono subito all'avvio — un PTT con polarità sbagliata regala cinque secondi di portante non modulata prima ancora che il firmware raggiunga il modem.

I ricetrasmettitori portatili con "spinotto K" (Baofeng e simili) sono un animale diverso: lì il PTT è un interruttore dall'anello del microfono al manicotto, di solito attraverso una resistenza, e l'audio del microfono condivide lo stesso conduttore. L'uscita dell'opto va a cavallo di quell'interruttore, e l'attenuazione del microfono pilota lo stesso nodo.

#### Isolamento e loop di massa

Il circuito sopra condivide la massa con la radio, causa normale di ronzio, fischio dell'alternatore e sintomi del tipo "funziona finché non trasmetto". Se si sente qualcosa del genere:

* **Trasformatori di isolamento audio**, 600:600 Ω (Bourns 42TL022, Tamura MET-01, o qualsiasi trasformatore telecom 1:1), al posto di C1 e C4. La rete di polarizzazione resta sul lato ESP32 del trasformatore RX.
* **Mantenere l'opto** (opzione A) affinché il ritorno del PTT non ricrei la massa appena interrotta.
* **L'ingresso RF** in trasmissione si manifesta come un PTT che si blocca o un modem che fallisce solo a piena potenza. Cavo schermato, conduttori corti, una ferrite a clip sul connettore del rig, e 47–100 pF da ciascuna linea audio verso il telaio *del rig* al connettore.

#### Ordine di messa in servizio

1. **Prima il loop test, senza radio.** GPIO25 → GPIO33 con un semplice filo (vedi [Cablaggio da banco per il loopback](#cablaggio-da-banco-per-il-loopback)). Se questo fallisce, nessuna quantità di circuiteria esterna aiuterà.
2. **Poi l'RX, ancora senza TX.** Aprire lo squelch, immettere traffico reale e osservare la colonna **AUDIO** della tabella di traffico live (è il valore `mVrms` del modem stesso al pin). Regolare RV2 per **≈300 mVrms sui pacchetti** — è l'obiettivo dell'AGC, quindi l'anello lavora a guadagno unitario e ha il massimo margine in entrambe le direzioni. Qualsiasi valore tra ~50 mV e ~800 mV verrà decodificato; sotto 40 mV si sta esaurendo il guadagno dell'AGC, sopra 1,1 Vrms si sta tagliando. Non c'è **nessun ingresso squelch hardware** in questo firmware — il DCD viene dal demodulatore — quindi lasciare lo squelch aperto è corretto, non un ripiego.
3. **Il TX per ultimo, su un carico fittizio.** Impostare RV1 per **≈3,0 kHz di deviazione** (2,5–3,5 kHz) con un deviometro, oppure confrontando con l'audio di una stazione nota su un secondo ricevitore. La sovradeviazione è la causa singola più comune del "il mio igate sente tutti ma nessuno sente me".
4. **G3RUH a 9600 Bd** richiede il percorso piatto/discriminatore a entrambi i capi: `DATA IN`/`DATA OUT`, 10 nF in C2/C3, e la casella *Filtro passa-basso audio* impostata su `flat_audio`. Un'uscita altoparlante e un ingresso microfonico non lo trasporteranno, indipendentemente da come vengono impostati i livelli.

### Cablaggio da banco per il loopback

Per il [LOOP TEST](#il-loop-test), basta collegare **`GPIO25` → `GPIO33`** (uscita DAC direttamente nell'ingresso ADC). Nessuna radio, nessun PTT necessario. Il test trasmette un frame APRS e si aspetta che la stessa scheda lo decodifichi indietro.

---

## Struttura del repository

```
workspace-APRS/esp32_APRS_igate/
├── CMakeLists.txt                  ← definizione della scheda (pin ADC/DAC/PTT/LED) + project()
├── partitions.csv                  ← nvs / otadata / phy_init / ota_0(1728K) / ota_1(1728K) / storage(512K, LittleFS)
├── sdkconfig                       ← target=esp32, flash 4MB, partizioni personalizzate
├── dependencies.lock               ← idf 5.5.4, littlefs 1.22.2, esp-idf-lib bmp180/i2cdev/helpers
├── LICENSE                         ← GPL-3.0
│
├── main/                                  (l'applicazione)
│   ├── main.c                      ← app_main, avvio/riconnessione Wi-Fi, ordine di boot
│   ├── app_config.c/.h             ← app_config_t, default di fabbrica, caricamento/salvataggio JSON
│   ├── storage.c                   ← mount/formattazione/utilizzo LittleFS
│   ├── aprs_service.c/.h           ← il collante: dispatch RX, helper TX, config modem, statistiche, loop test
│   ├── aprs_filter.c/.h            ← classificatore del tipo di payload TNC2 (messaggio/stato/telemetria/meteo/…)
│   ├── beacon.c/.h                 ← beacon della propria posizione (trk / igate / digi), guidati dallo scheduler condiviso
│   ├── weather.c/.h                ← rapporto meteo APRS della propria stazione: refresh via sensors_local + beacon WX (vedi Sensori)
│   ├── beacon_scheduler.c/.h       ← un unico task condiviso che guida TUTTA la TX periodica (beacon, WX, bollettini, oggetti)
│   ├── bulletins.c/.h              ← bollettini APRS BLN1..BLN5 (bulletins.json dedicato, non g_config)
│   ├── objects_items.c/.h          ← Oggetti/Item APRS (objitems.json dedicato, non g_config)
│   ├── net_state.c/.h              ← flag "abbiamo davvero internet?"
│   ├── time_sync.c/.h              ← SNTP (sempre UTC)
│   └── cpu_freq.c/.h               ← esp_pm_configure() dalla pagina System
│
├── components/
│   ├── esp32idf_radioamateur_modem/       (il soft-modem — il cuore del progetto)
│   │   ├── esp32idf_radioamateur_modem.h  ← API pubblica + strato di comodo APRS
│   │   ├── include/…_config.h             ← TUTTE le costanti scheda/DSP a tempo di compilazione
│   │   ├── src/afsk.c    (1526 righe)     ← ingestione DMA ADC, AGC, FIR di decimazione, ISR DAC, PTT
│   │   ├── src/modem.c   (903 righe)      ← correlatori, DPLL, tabelle di tono, DCD, calibrazione
│   │   ├── src/ax25.c    (1364 righe)     ← framer HDLC, NRZI, bit-stuffing, codec AX.25, coda TX
│   │   ├── src/fx25.c, lwfec/rs.c, gf.c   ← FEC Reed–Solomon FX.25
│   │   └── src/crc_ccit.c                 ← FCS
│   │
│   ├── igate/          ← client TCP APRS-IS, login, filtri, deduplica, RF→INET / INET→RF
│   ├── digirepeater/   ← logica del percorso WIDEn-N / TRACEn-N / RELAY / ECHO / GATE
│   ├── message/        ← messaggistica APRS, ack/retry, AES-128-CBC + base64
│   ├── lastheard/      ← anello in RAM delle stazioni udite → JSON per la dashboard
│   ├── trafficlog/     ← anello in RAM delle righe di traffico → JSON per la dashboard (long-poll basato su seq)
│   ├── weather_telemetry/  ← solo struct a livello di protocollo: weather_telemetry_data_t, aprs_weather_report_t,
│   │                          aprs_telemetry_report_t (definizioni dei campi APRS101 WX + Telemetria, nessuna logica)
│   ├── sensors_local/      ← IL framework driver dei sensori (vedi Sensori)
│   │   ├── include/sensors_local.h        ← API pubblica: register / unregister / save / scorrimento registro
│   │   ├── sensors_local.c                ← il registro dinamico vero e proprio
│   │   ├── include/sensor_local_properties.h ← descrittore di capacità per driver (quali campi WX / canali TLM)
│   │   └── drivers/<name>/                 ← una cartella per driver (<name>.c + <name>_properties.h), auto-registrato
│   │       ├── example/sensor_local_weather_example.c    ← scheletro WEATHER con dati casuali da copiare
│   │       ├── example/sensor_local_telemetry_example.c  ← scheletro TELEMETRY con dati casuali da copiare
│   │       └── bmp180/bmp180.c                           ← driver reale I2C temperatura/pressione
│   └── webconfig/      ← amministrazione esp_http_server
│       ├── web_server.c            ← tabella delle rotte
│       ├── web_common.c            ← autenticazione, parsing dei form, shell HTML, helper dei campi
│       ├── pages/*.c               ← un file per pagina di amministrazione (station, bulletins, objects, wx, tlm, msgchat, …)
│       └── translations/           ← translations.h + lang_en.h + lang_es.h + lang_it.h
│
└── managed_components/                     (scaricato dal component manager)
    ├── joltwallet__littlefs/
    ├── esp-idf-lib__bmp180/                ← driver I2C BMP180 (per il driver sensors_local bmp180)
    ├── esp-idf-lib__i2cdev/
    └── esp-idf-lib__esp_idf_lib_helpers/
```

**Dimensione del sorgente:** ~19,7 mila righe di C proprietario (`.c`, ~28 mila con gli header) tra `main/` + `components/` (escluso `managed_components/`), di cui ~5,0 mila sono il nucleo DSP del modem e ~5,9 mila l'amministrazione web.

---

## Architettura

### Sequenza di avvio

`app_main()` gira sul task principale di sistema, il cui stack è fissato a `CONFIG_ESP_MAIN_TASK_STACK_SIZE` (3584 B) — di gran lunga troppo piccolo per `esp_netif` + `esp_wifi` + `esp_http_server` + cJSON. Quindi `app_main()` esegue solo le due cose che devono precedere tutto il resto, e poi passa il testimone:

```
app_main()
 ├─ nvs_flash_init()          (cancella e riprova su NO_FREE_PAGES / NEW_VERSION_FOUND)
 ├─ storage_init()            (monta LittleFS su /storage, auto-formattazione al primo avvio)
 └─ xTaskCreate(app_task, 8192 B, prio 5)   ── e ritorna; FreeRTOS recupera il task principale

app_task()
 ├─ app_config_load()                  ← /storage/config.json, oppure scrive+carica i default di fabbrica
 ├─ cpu_freq_apply()                   ← 80/160/240 MHz dalla pagina System
 ├─ net_state_init()                   ← "internet non ancora disponibile"
 ├─ wifi_init()                        ← AP / STA / AP+STA secondo g_config.wifi_mode
 ├─ vTaskDelay(10 ms)                  ← cede il passo affinché IDLE giri; evita un falso trip del TWDT
 ├─ time_sync_start()                  ← SNTP, non bloccante
 ├─ web_server_start()                 ← esp_http_server, 56 gestori URI, stack 8 KB
 ├─ aprs_service_start()               ← ⚠ DEVE precedere modem_init(): installa la callback RX
 │    ├─ trafficlog_init / lastheard_init / message_init
 │    ├─ message_set_tx_handler / igate_set_inet2rf_handler
 │    ├─ modem_set_rx_callback(on_rx_frame)
 │    ├─ igate_start()                 ← sempre avviato; si mette in idle da solo quando nulla richiede APRS-IS
 │    ├─ beacon_start() / weather_start() / bulletins_start() / objitems_start()  ← preparano lo stato della TX periodica
 │    ├─ beacon_scheduler_start()      ← UN task condiviso guida tutto quanto sopra (~61 KB di stack → ~14 KB)
 │    └─ xTaskCreate(serviceTickTask)  ← 1 Hz: refresh dei sensori WX + retry dei messaggi
 ├─ if (audio_modem_en) modem_init()   ← ⏳ BLOCCA ~5 s calibrando il clock ADC reale (una volta per avvio)
 │      └─ aprs_service_notify_modem_ready()
 └─ APRS_setCallsign(...)
```

Due regole di ordinamento sono strutturali e commentate come tali nel sorgente:

1. **`aprs_service_start()` prima di `modem_init()`** — il modem inizia a consegnare frame *dall'interno di* `modem_init()`; la callback deve essere già installata.
2. **I beacon partono prima che il modem sia pronto** — trasmettono immediatamente all'ingresso, quindi `aprs_service_send_tnc2()` scarta i frame con un log di debug finché `s_modemReady` non viene impostato, invece di raggiungere `Ax25WriteTxFrame()` prima che `Ax25Init()` sia stato eseguito.

### Mappa dei task

| Task | Stack | Prio | Core | Creato da | Ruolo |
|---|---|---|---|---|---|
| `app_task` | 8192 B | 5 | qualsiasi | `app_main` | avvio + idle |
| DSP RX del modem | `MODEM_RX_TASK_STACK` 4096 B | 10 | **0** (`MODEM_RX_TASK_CORE`) | `AFSK_init()` | svuota l'anello ADC, esegue i demodulatori |
| `modem_svc` | 6144 B | — | qualsiasi | `modem_init()` | guida `AFSK_ServiceTx()` / `Ax25TransmitCheck()`, consegna i frame RX alla callback |
| ISR DMA ADC | — | — | **0** (`MODEM_ADC_ISR_CORE`) | driver | frame di conversione → buffer ad anello |
| Clock di campionamento DAC (GPTimer, livello 3) | — | — | **1** (`MODEM_DAC_TIMER_CORE`) | `AFSK_init()` | un campione DAC ogni 1/38400 s |
| `igate_task` | — | — | qualsiasi | `igate_start()` | socket APRS-IS, login, pompa RX, riconnessione |
| `beacon_sched` | 14336 B | 4 | qualsiasi | `beacon_scheduler_start()` | UN task condiviso: beacon tracker/igate/digi + rapporto WX + bollettini + oggetti/item, eseguiti in sequenza |
| `aprs_svc_tick` | 10240 B | 4 | qualsiasi | `aprs_service_start()` | tick a 1 Hz: refresh dei sensori WX (`weather_service_1hz`) + retry dei messaggi |
| `httpd` | 8192 B | — | qualsiasi | `web_server_start()` | amministrazione web |
| `esp_timer` | — | — | — | IDF | back-off di riconnessione Wi-Fi |

### Flusso dei dati

```
                        ┌──────────────── RX RF ────────────────┐
  audio radio ─► ADC1 (DMA, 76800 Hz)
                   │
                   ├─ adc_ingest()  (de-swap delle coppie DMA, blocco DC, AGC, RMS)
                   ├─ FIR di decimazione ─► flusso di demodulazione a 9600 Hz
                   ├─ correlatore ×1–2  ─► recupero bit DPLL ─► NRZI ─► HDLC
                   ├─ decodifica RS FX.25 (opzionale)
                   └─ frame AX.25 ──► modem_rx_frame_t ──► on_rx_frame()          [aprs_service.c]
                                                              │ ax25_decode()
                                                              ▼
                                                       s_rxHook ──► aprs_msg_callback()
                                                              │
             ┌────────────────────────────┬──────────────────┼───────────────────┐
             ▼                            ▼                  ▼                   ▼
   trafficlog_add_pkt("RX")     lastheard_add(RF)    digiProcess()        igateProcess()
                                                        │ =2 → riscrive     │ →  linea qAR/qAO
                                                        ▼                    ▼
                                            aprs_service_send_tnc2()   socket APRS-IS
                                                                            │
                        ┌──────────────── INET → RF ────────────────────────┘
                        ▼
                inet2rfHandler(line) ─► lastheard_add(INET)
                                     ├─ handleIncomingAPRS()  (messaggi/ack)
                                     └─ aprs_service_send_tnc2(line)      [se inet2rf]

                        ┌──────────────── TX RF ────────────────┐
  aprs_service_send_tnc2(text,len)
        ├─ scarta se !s_modemReady  o  len ≥ AX25_FRAME_MAX_SIZE (329)
        ├─ modem_send_tnc2() ─► ax25_encode() ─► coda TX
        └─ attesa CSMA (a meno di full_duplex) ─► PTT on ─► preambolo (TXDelay)
                 ─► HDLC + bit-stuffing + FCS ─► NRZI ─► accumulatore di fase
                 ─► LUT seno a 512 voci ─► ISR DAC @ 38400 Hz ─► GPIO25 ─► radio
```

---

## Il componente modem (`esp32idf_radioamateur_modem`)

Vendorizzato sotto `components/`, GPL-3.0, di **Emiliano Augusto González (LU3VEA)** — upstream: <https://github.com/hiperiondev/esp32idf_radioamateur_modem>. Deriva da **VP-Digi** (SQ8VPS), **ESP32APRS_Audio** (nakhonthai) e **LibAPRS** (Mark Qvist).

### Catena del segnale

| Fase | Frequenza | Dove |
|---|---|---|
| SAR-ADC1 continuo/DMA, frame di conversione da 128 campioni | **76 800 Hz** | ISR del driver sul core 0 |
| ingestione: de-swap delle coppie, rimozione offset DC, AGC, misura RMS | 76 800 Hz | `afsk.c` |
| FIR di decimazione (rapporto **8:1**) | → **9 600 Hz** | `afsk.c` |
| correlatore (marca/spazio), passa-basso, DPLL, decodifica NRZI | 9 600 Hz | `modem.c` |
| deframing HDLC, bit de-stuffing, verifica FCS, decodifica RS FX.25 | — | `ax25.c` / `fx25.c` |
| ⟵ TX ⟶ codifica AX.25, FCS, bit stuffing, NRZI, accumulatore di fase a 32 bit, LUT seno a 512 voci | **38 400 Hz** | `ax25.c` / `modem.c` / `afsk.c` |

Profili (`modem_mode_t` / `enum ModemType`, stessa numerazione in entrambi, per questo l'app può fare un semplice cast):

| Valore | Profilo | Baud | Toni |
|---|---|---|---|
| 0 | AFSK300 | 300 | 1600 / 1800 Hz |
| 1 | **Bell 202** (predefinito, APRS standard) | 1200 | 1200 / 2200 Hz |
| 2 | ITU V.23 | 1200 | 1300 / 2100 Hz |
| 3 | G3RUH FSK | 9600 | — |

Il profilo a 1200 Bd esegue **due demodulatori in parallelo**, sintonizzati leggermente in modo diverso, per aumentare la probabilità di decodifica (`MODEM_MAX_DEMODULATOR_COUNT = 2`).

### Perché i valori sono quelli che sono

L'header di configurazione di questo componente è insolitamente ben documentato, e il ragionamento conta se lo si tocca:

* **ADC a 76 800 Hz, non 38 400.** 38 400 dà al profilo a 9600 Bd esattamente *quattro* campioni ADC per simbolo. L'istante di campionamento della DPLL viene allora quantizzato al 25% di un simbolo, e il voto a maggioranza a tre campioni di `decode()` copre il 75% di un simbolo — la finestra di voto entra sempre in una transizione. La simulazione host del vero `modem.c`, con clock reali e **senza rumore**, produceva errori di bit rigidi in ogni fase in cui gli istanti dell'ADC si allineano con gli istanti di aggiornamento del DAC; i due clock differiscono di ~0,05%, quindi l'allineamento attraversa quelle fasi ogni ~55 ms. A 76 800 la stessa simulazione dà zero errori di bit in ogni fase e con fino a 30 µs di jitter sui fronti TX. I profili AFSK non se ne sono mai curati (vengono demodulati a 9600 Hz tramite un correlatore dopo la decimazione) e misurano in modo identico a entrambe le frequenze. **Costo:** il doppio del lavoro DSP in RX, e `MODEM_RESAMPLE_RATIO` diventa 8, il che richiede il FIR di decimazione più lungo — un filtro a 8 tap tagliato per 4:1 non fa anti-aliasing a 8:1.
* **Il DAC resta a 38 400 Hz** (= 32 × 1200, un multiplo esatto di ogni baud rate supportato). Il trasmettitore posiziona i fronti di simbolo esattamente sui campioni DAC a qualsiasi frequenza; era il *ricevitore* ad aver bisogno di risoluzione.
* **`MODEM_ADC_CONV_FRAME = 128`, non la dimensione del blocco.** La ISR ADC dell'IDF stesso chiama `xRingbufferSendFromISR()`, che esegue l'intero `memcpy` **dentro `portENTER_CRITICAL_ISR()`**. Su Xtensa questo alza `PS.INTLEVEL` a 3 — e il clock di campionamento del DAC *è* un interrupt di livello 3. Quindi la ISR del DAC viene mascherata per la durata della copia: 768 campioni ≈ 11 µs (10% di un simbolo a 9600 Bd — fatale), 128 campioni ≈ 2 µs (2% — dentro budget). Nessuna quantità di `IRAM_ATTR` dal nostro lato aiuta: il codice bloccante è quello del driver, già in IRAM, e semplicemente lungo. A 1200 Bd 11 µs è l'1,3% di un simbolo ed è invisibile — motivo esatto per cui ogni profilo AFSK funzionava mentre G3RUH perdeva frame.
* **`MODEM_DAC_TIMER_CORE (1) ≠ MODEM_ADC_ISR_CORE (0)`.** `portENTER_CRITICAL_ISR()` maschera i livelli ≤3 solo sul core *locale*. Mettendo il clock del DAC sull'altro core, la ISR dell'ADC si limita ad attendere il lock invece di mascherarlo. Imposto con `#error`. Le due correzioni (frame piccoli, core separati) sono indipendenti e vengono applicate entrambe.
* **`ModemCalibrateSampleRate()`** — `modem_init()` blocca per ~5 s all'avvio misurando la frequenza *reale* dell'ADC (`modem_measure_adc_rate()`), perché il passo del PLL di ogni profilo è calcolato dal rapporto ADC/DAC *nominale*, e lo scarto è altrimenti un errore a regime che la DPLL dovrebbe inseguire per un'intera trasmissione. La frequenza dell'allarme DAC è già nota esattamente dalla configurazione del timer (`afskGetDacAlarmRate()`), quindi solo il lato ADC ha bisogno di essere misurato. Entrambi i clock derivano dallo stesso cristallo, quindi il rapporto è una proprietà fissa della scheda: misurato **una volta per avvio**, riapplicato a ogni cambio di profilo.
* **`MODEM_RX_FIFO_SIZE = 4096` campioni** — dimensionata in *campioni*, quindi si è ridotta in *tempo* quando la frequenza è raddoppiata (2048 erano 53 ms a 38,4 k, solo 26,7 ms a 76,8 k — a malapena un blocco da 20 ms). 4096 ripristina il margine. Verificato: deve contenere ≥ 2 blocchi, dato che `AFSK_Poll()` consuma solo blocchi interi.

Le guardie `#error` a tempo di compilazione impongono: pin DAC ∈ {25, 26}; pin ADC ∈ 32–39; `MODEM_ADC_SAMPLERATE % 9600 == 0`; FIFO ≥ 2 blocchi; `MODEM_ADC_CONV_FRAME` pari, che divide `MODEM_BLOCK_SIZE`, e allineato ai byte secondo `SOC_ADC_DIGI_DATA_BYTES_PER_CONV`; core del timer DAC ≠ core della ISR ADC; priorità del timer DAC ∈ 1..3.

### Riferimento alla configurazione a tempo di compilazione

Tutto in `components/esp32idf_radioamateur_modem/include/esp32idf_radioamateur_modem_config.h`, ogni macro protetta con `#ifndef` in modo che il sistema di build possa sovrascriverla.

| Macro | Predefinito | Significato |
|---|---|---|
| `MODEM_DAC_GPIO` | 25 | uscita audio; solo 25 o 26 |
| `MODEM_ADC_GPIO` | 33 | ingresso audio; solo 32–39 |
| `MODEM_PTT_GPIO` | −1 | default PTT di riserva (sovrascritto a runtime) |
| `MODEM_PTT_ACTIVE_HIGH` | 1 | polarità di riserva |
| `MODEM_LED_TX_GPIO` / `MODEM_LED_RX_GPIO` | −1 | LED di stato |
| `MODEM_DAC_SAMPLERATE` | 38400 | = 32 × 1200 |
| `MODEM_ADC_SAMPLERATE` | 76800 | = 8 × 9600 |
| `MODEM_ADC_RATE_NUM` / `_DEN` | 1 / 1 | fattore di correzione sulla frequenza ADC richiesta |
| `MODEM_DAC_AMPLITUDE_PCT` | 60 | oscillazione DAC, % di 0–3,3 V |
| `MODEM_ADC_ATTEN` | `ADC_ATTEN_DB_12` | finestra ≈0–3,1 V |
| `MODEM_RX_FIFO_SIZE` | 4096 | campioni, potenza di due |
| `MODEM_ADC_CONV_FRAME` | 128 | campioni per frame DMA |
| `MODEM_ADC_POOL_FRAMES` | 32 | profondità del pool del driver (= 53 ms) |
| `MODEM_RX_TASK_PRIO` / `_STACK` / `_CORE` | 10 / 4096 / 0 | task DSP RX |
| `MODEM_ADC_ISR_CORE` | 0 | core della ISR DMA ADC |
| `MODEM_DAC_TIMER_CORE` | 1 | **deve differire dal core della ISR ADC** |
| `MODEM_DAC_TIMER_INTR_PRIO` | 3 | 1..3 |
| *(derivato)* `MODEM_DEMOD_SAMPLERATE` | 9600 | fisso |
| *(derivato)* `MODEM_RESAMPLE_RATIO` | 8 | ADC ÷ demod |
| *(derivato)* `MODEM_BLOCK_SIZE` | 1536 | 20 ms a 76,8 kHz |

### Configurazione a runtime (`modem_config_t`)

Costruita in esattamente un unico punto — `aprs_service_build_modem_config()` — condiviso da avvio, salvataggio della pagina Radio (riapplicazione live, senza reboot) e loop test:

| Campo | Origine | Note |
|---|---|---|
| `modem` | `afsk_modem_type` | semplice cast; la pagina limita a 0–3 |
| `flat_audio` | `audio_lpf` | nonostante il nome, è sempre stato il flag di ingresso audio piatto |
| `full_duplex` | `false` normalmente | il LOOP TEST passa `true` — un filo DAC→ADC significa che il CSMA non vede mai un canale libero |
| `allow_non_aprs` | `false` | accettare Control/PID diversi da `0x03`/`0xF0`? |
| `preamble_ms` | `preamble` (300) | TXDelay |
| `slot_time_ms` | `tx_timeslot` (2000) | tempo di silenzio CSMA |
| `fx25_mode` | `fx25_mode` | 0=off, 1=solo RX, 2=RX+TX |
| `ptt_gpio` | `rf_ptt_gpio` | tramite `afsk_ptt_gpio_is_valid()`, altrimenti −1 |
| `ptt_active_high` | `rf_ptt_active` | |

**Esplicitamente *non* mappati a runtime** (nessun equivalente nel nuovo componente): pin ADC/DAC e attenuazione (a tempo di compilazione), squelch hardware (`rf_sql_*` — l'RX si basa invece sul DCD reale), interruttore di potenza RF (`rf_pwr_*`), squelch software (`sql_level`), volume RX, tetto dell'AGC (`agc_max_gain` — l'AGC si autolimita).

**Superficie API pubblica** (`esp32idf_radioamateur_modem.h`): `modem_init`, `modem_deinit`, `modem_set_modem`, `modem_set_rx_callback`, `modem_send_raw`, `modem_build_frame_tnc2`, `modem_send_tnc2`, `modem_format_tnc2`, `modem_tx_busy`, `modem_measure_adc_rate`, più uno strato di comodo in stile LibAPRS (`APRS_setCallsign`, `APRS_setPath1/2`, `APRS_setSymbol`, `APRS_setPower/Height/Gain/Directivity`, `APRS_sendLoc`, `APRS_sendMsg`, `APRS_sendPkt`, `APRS_printSettings`).

---

## Componenti dell'applicazione

### `igate` — gateway APRS-IS

* **Client TCP** su socket LWIP con riconnessione automatica; rilegge `g_config` a ogni riconnessione, quindi la maggior parte delle modifiche fatte dall'amministrazione web arrivano al ciclo di riconnessione successivo senza bisogno di reboot.
* **Vincolato alla connettività reale**, non a "il Wi-Fi è attivo": interroga `net_state_is_connected()`, che diventa vero solo su `IP_EVENT_STA_GOT_IP` e falso di nuovo alla disconnessione / modalità solo-AP.
* **Riga di login:** `user <mycall> pass <passcode> vers ESP32APRS 1.0 filter <filter>` — loggata testualmente in modo che un filtro malformato sia visibile. Il banner del server e la riga `# logresp … verified/unverified` vengono mostrati; `unverified` genera un avviso che nomina `aprs_mycall`/`aprs_passcode`.
* **RF→INET** (`igateProcess()`): scarta i frame il cui percorso contiene `RFONLY`, `TCPIP`, `qA*` o `NOGATE`; applica la bitmask `rf2inetFilter` (messaggio/status/telemetria/meteo/oggetto/item/query/boa/posizione); costruisce un header `,qAR,<mycall>-<ssid>` — o `,<mycall>-<ssid>*,qAO,<object>` per la forma object/satellite-gate; deduplica contro una cache di 10 voci / 30 s.
* **INET→RF**: ogni riga non `#` incrementa `isRxCount`, viene passata a `handleIncomingAPRS()` quando la messaggistica è attiva, e ritrasmessa se `inet2rf` è impostato **e** il bit di tipo del payload è impostato in `inet2rfFilter`. Il tipo è deciso da `aprs_filter_classify_tnc2()` (`main/aprs_filter.c`) a partire dall'identificatore del tipo di dati APRS più, per i report di posizione/oggetto/item, il simbolo (`_` → meteo, `/N` → boa). I payload non classificabili — traffico di terze parti `}` sopra ogni altra cosa, la classica fonte di loop dell'IGate — vengono classificati come 0 e non vengono mai inoltrati. Le righe filtrate vengono loggate solo a `ESP_LOGD`; la voce di traffico `RX-IS` le copre già.
* **Uplink condiviso:** il task gira sempre, perché il socket è usato anche dal componente message (`igate_send_raw()`) e dal "beacon verso internet". Resta inattivo a basso costo quando nulla ne ha bisogno.
* **Contatori** (`igate_stats_t`): `rxCount`, `txCount`, `dropCount`, `dupCount`, `isRxCount` (tutte le righe APRS-IS), `isTxCount` (tutte le scritture sul socket).

### `digirepeater` — digipeater

`digiProcess(ax25_msg_t*)` riscrive il percorso **sul posto** e restituisce:

| Ritorno | Significato |
|---|---|
| `0` | non ripetere (scarta / non per noi / già ritrasmesso) |
| `1` | ripeti così com'è (il percorso porta già il nostro nominativo usato, es. bypass `*`) |
| `2` | ripeti con percorso modificato — chi chiama riCodifica e trasmette |

Gestisce **WIDEn-N**, **TRACEn-N**, **RELAY / GATE / ECHO**, e WIDEn-N codificato nel campo SSID di destinazione. Contatori: `rxPkts`, `txPkts`, `dropRx`, `erPkts` (malformati: troppo corti / nessun percorso). Nominativo/SSID provengono da `g_config.digi_mycall` / `digi_ssid`.

### `beacon` — beacon della propria stazione

Tre **task FreeRTOS indipendenti** (`trk_beacon_task`, `igate_beacon_task`, `digi_beacon_task`), ciascuno con il proprio flag di abilitazione, intervallo, coordinate, simbolo, commento e instradamento `loc2rf`/`loc2inet`. Ciascuno resta inattivo e riverifica periodicamente quando disabilitato, quindi commutarlo nell'amministrazione web ha effetto **senza reboot**. I timestamp sono zulu/UTC (`051200z`) secondo la specifica APRS — motivo per cui `time_sync.c` blocca l'orologio di sistema su `TZ=UTC0` indipendentemente da `g_config.timeZone`.

Questo è ciò che fa apparire la stazione stessa su aprs.fi: l'IGate e il digipeater da soli inoltrano solo il traffico che sentono; non si annunciano mai da soli.

**Non implementato:** posizione live da GPS e SmartBeaconing. Questi sono beacon a stazione fissa basati solo sulle coordinate salvate in ciascuna pagina.

### `message` — messaggistica APRS

* Coda in RAM da 20 voci (`MSG_QUEUE_SIZE`), massimo 200 caratteri di testo.
* `sendAPRSMessage()`, `sendAPRSAck()`, `sendAPRSMessageRetry()` (ticchettata a 1 Hz da `aprs_svc_tick`), `handleIncomingAPRS()` interpreta qualsiasi riga TNC2 da RF *o* da APRS-IS.
* **Crittografia:** AES-128-CBC opzionale (`mbedtls`) con l'IV derivato tramite MD5 dal nominativo + ID messaggio, payload codificato in base64. Chiave = `g_config.msg_key`.
* Instradamento tramite una bitmask di canale: `MSG_CHANNEL_RF (1<<0)` → `aprs_service_send_tnc2()`, `MSG_CHANNEL_INET (1<<1)` → `igate_send_raw()`.

### `lastheard` / `trafficlog` — feed della dashboard

* **`lastheard`** — anello delle stazioni udite con un contatore di pacchetti per nominativo, timestamp da orologio reale (una volta che l'NTP si è sincronizzato), e tabella/codice del simbolo APRS analizzato dai report di posizione `!`/`=` (le forme temporizzate `/` e `@` vengono lasciate non analizzate — l'icona resta vuota). Alimentato **sia** da RF che da APRS-IS, così c'è qualcosa da vedere mentre si verifica l'uplink prima che la radio decodifichi qualcosa. JSON: `[{"time":"HH:MM:SS","call":"…","path":"RF: WIDE1-1","sym":"91-1","packets":3}, …]`.
* **`trafficlog`** — anello delle righe di traffico con timestamp `esp_timer` e un **numero di sequenza** sempre crescente, così il browser fa long-poll solo di ciò che non ha ancora visto: `GET /igate_traffic?since=<seq>`. Le voci strutturate portano `dir` (`RX`/`TX`/`DIGI`/`INET2RF`/`RX-IS`), `dx`, il `pkt` grezzo, e `au` (livello audio in mV RMS, o −1).

Entrambi sono thread-safe e richiamabili da qualsiasi task.

### `webconfig` — amministrazione web

`esp_http_server`, 56 gestori URI, corrispondenza URI con wildcard, stack del gestore da 8 KB, purge LRU. **Autenticazione HTTP Basic** contro `g_config.http_username` / `http_password` su ogni pagina. L'HTML viene emesso tramite piccoli helper per campo (`web_field_text`, `web_field_int`, `web_field_checkbox`, `web_select_*`, `web_field_symbol`, …) invece di un unico enorme `snprintf` — deliberatamente, per evitare `-Werror=format-truncation`.

---

## Sensori

Questa sezione copre il componente **`sensors_local`**: il framework a runtime che permette a sensori hardware reali (o simulati) di alimentare il Rapporto Meteo APRS della propria stazione e, in futuro, il sottosistema di Telemetria, senza che il nucleo debba mai avere una lista fissa di "i sensori supportati da questa build". Se siete arrivati qui per collegare un BME280, un DS18B20, un ADS1115, una sonda di umidità del suolo, un partitore di tensione per batteria, o qualsiasi altra cosa a questo firmware, questa è la sezione da leggere — spiega esattamente come funziona il meccanismo e percorre passo passo l'aggiunta di un nuovo driver dall'inizio alla fine.

### Perché un framework di driver invece di una lista fissa

I firmware APRS precedenti di questa stirpe (e la pagina legacy `/sensor`, il cui sorgente resta ancora nell'albero ma non viene più compilato, vedi [più avanti](#la-pagina-legacy-sensor--non-è-la-stessa-cosa)) adottavano l'approccio opposto: un array di dimensione fissa di "slot sensore" in `g_config`, ciascuno descritto da un `type`/`port`/`address` numerico che uno `switch` centrale doveva interpretare. Ogni nuovo sensore significava modificare quello switch centrale, ricompilare, e sperare che gli ID numerici non entrassero in collisione con quelli di qualcun altro.

`sensors_local` ribalta questo schema:

* Il nucleo (`sensors_local.c`) non sa **nulla** di nessun sensore specifico. Sa solo mantenere una lista di struct "driver" opachi e chiamare su di essi una manciata di puntatori a funzione.
* Ogni sensore reale vive nel suo **proprio file `.c`** sotto `components/sensors_local/drivers/`, e si aggiunge alla lista **automaticamente all'avvio**, prima ancora che `app_main()` venga eseguito, usando un attributo constructor del C nascosto dietro la macro `SENSORS_LOCAL_DRIVER_AUTOREGISTER`.
* Il sistema di build (`components/sensors_local/CMakeLists.txt`) compila **ogni** file `.c` trovato in `drivers/` con un `file(GLOB …)` — non c'è nessuna riga per driver da aggiungere a nessun `CMakeLists.txt`, né alcuna voce per driver da aggiungere a nessun header.

Il risultato pratico: **aggiungere un sensore significa "mettere un nuovo file in `drivers/`, ricompilare"** — nulla in `sensors_local.c`, `weather.c`, `sensors_local.h`, o in qualsiasi `CMakeLists.txt` deve cambiare per un normale nuovo driver.

### Le due famiglie di payload

Un driver non restituisce un flusso di byte grezzo; compila **campi a livello applicativo già raggruppati per tipo di payload APRS**, definiti nel componente separato `weather_telemetry` (`weather_telemetry.h`, una trascrizione diretta dello spec APRS101 più gli addenda 1.1/1.2):

| Famiglia | Bit (`sensor_local_data_kind_t`) | Struct di destinazione | Consumato oggi da |
|---|---|---|---|
| **Weather** | `SENSOR_LOCAL_DATA_WEATHER` (`1u << 0`) | `aprs_weather_report_t` (vento, temperatura, umidità, pressione, pioggia ×3, neve, luminosità, altezza inondazione ×2, …) | `weather.c` → beacon WX APRS reale via etere |
| **Telemetry** | `SENSOR_LOCAL_DATA_TELEMETRY` (`1u << 1`) | `aprs_telemetry_report_t` (5 canali analogici `A1..A5`, 8 canali digitali `B1..B8`) | `weather.c` popola il contenitore condiviso da `sensors_local`, ma **nessun encoder/beacon lo rilegge ancora per trasmetterlo via etere** — vedi [limitazioni](#7-mapparlo-nella-pagina-weather) |
| *(riservato per il futuro)* | es. `SENSOR_LOCAL_DATA_GPS = 1u << 2` | *(nuovo struct, es. una posizione fix)* | non ancora definito — vedi [Aggiungere un nuovo kind di sensore](#aggiungere-un-nuovo-kind-di-sensore) |

`SENSOR_LOCAL_DATA_ALL` è semplicemente l'OR di ogni bit attualmente definito, ed è ciò che `weather.c` passa quando chiede al registro di aggiornare tutto una volta al secondo.

Un singolo driver è libero di dichiarare **entrambi o uno solo** dei bit nel suo campo `capabilities` — ad esempio, una scheda combinata con un sensore barometrico *e* un canale ADC libero potrebbe riportare Weather **e** Telemetry dalla stessa chiamata a `save()`.

### Anatomia di un driver (`sensor_local_driver_t`)

Ogni driver è un'istanza di questo struct (dichiarato in `components/sensors_local/include/sensors_local.h`):

```c
struct sensor_local_driver {
    const char *name;      // id stabile, unico, leggibile, es. "bme280", "ads1115-batt"
    uint32_t capabilities; // OR di SENSOR_LOCAL_DATA_WEATHER / _TELEMETRY (non può essere zero)

    sensor_local_init_fn_t   init;   // avvio opzionale una tantum (può essere NULL)
    sensor_local_save_fn_t   save;   // OBBLIGATORIO: l'unico ingresso che legge davvero il sensore
    sensor_local_deinit_fn_t deinit; // spegnimento opzionale (può essere NULL)

    const sensor_local_properties_t *properties; // quali campi WX / canali TLM può produrre questo driver (vedi sensor_local_properties.h)

    void *ctx; // stato privato del driver, opaco per il registro

    // --- di proprietà del registro; un driver non deve mai toccarli da solo ---
    bool initialized;
    bool failed;
};
```

Tre ruoli di puntatore a funzione, ciascuno con un contratto preciso:

* **`init(self)`** — chiamato **al massimo una volta**, in modo lazy, la prima volta che il driver serve davvero (oppure eagerly, per ogni driver, quando `weather_start()` chiama `sensors_local_init_all()` all'avvio). Qui si apre un bus I2C/SPI/UART, si sonda il registro chip-ID, si allocano eventuali buffer privati, e si inizializza ciò che serve inizializzare (es. `srand()`). Restituire `ESP_OK` in caso di successo; qualsiasi altro valore **marca il driver come `failed` in modo permanente** per tutta la vita del registro (finché non viene deregistrato e ri-registrato), e viene saltato da quel momento in poi.
* **`save(self, data, kind)`** — L'ingresso comune, chiamato a ogni ciclo di refresh (1 Hz, guidato dal task `aprs_svc_tick` tramite `weather_service_1hz()`). `kind` è **già mascherato** solo ai bit che sia il chiamante vuole sia il driver ha dichiarato, quindi un driver solo-Weather non deve mai controllare Telemetry da sé. Il driver legge il sensore e scrive direttamente nel contenitore `data` di proprietà del chiamante — nessuna allocazione, nessuna coda. Deve toccare **solo** la famiglia dichiarata in `capabilities`, e deve **tollerare una destinazione vuota** (es. `data->weather_qty == 0`) non facendo nulla per quella famiglia invece di dereferenziare un array nullo.
* **`deinit(self)`** — specchio opzionale di `init()`, chiamato da `sensors_local_unregister()` o `sensors_local_deinit()`. Chiude ciò che `init()` ha aperto.

`ctx` è a vostra disposizione: puntatelo a uno struct `static` (come fanno entrambi i driver di esempio) se il driver non ha motivo di supportare più di un'istanza, oppure a storage su heap/pool se invece serve (vedi [Istanze multiple](#istanze-multiple-dello-stesso-tipo-di-sensore)).

### Il registro: come un driver viene trovato e chiamato

`sensors_local.c` implementa il registro come un piccolo array di **puntatori** a driver, protetto da mutex e ridimensionabile su heap (mai copie — lo storage del vostro struct `static` è ciò che vive realmente nella tabella):

```
sensors_local_init()          // crea il mutex del registro; sicuro chiamarlo più volte
sensors_local_register(drv)   // aggiunge alla tabella; rifiuta save NULL, nome vuoto, nome
                               // duplicato, o capabilities == SENSOR_LOCAL_DATA_NONE
sensors_local_unregister(name)// rimuove per nome, chiamando deinit() se il driver era inizializzato
sensors_local_count()         // quanti driver sono attualmente registrati
sensors_local_get(index)      // ottiene per posizione 0..count-1 (usato dal menu a tendina della pagina Weather)
sensors_local_find(name)      // ottiene per nome
sensors_local_init_all()      // esegue init() eagerly su ogni driver non ancora inizializzato
sensors_local_save(data,kind) // scorre la tabella; per ogni driver le cui capabilities intersecano
                               // kind, lo inizializza lazily se necessario, poi chiama il suo save()
sensors_local_deinit()        // deinit() + svuota tutto; libera l'array sottostante
```

`sensors_local_register()` può essere chiamato **anche prima che lo scheduler di FreeRTOS sia in esecuzione**, perché `SENSORS_LOCAL_DRIVER_AUTOREGISTER` scatta da una funzione `__attribute__((constructor))`, che il runtime C invoca durante l'inizializzazione statica, prima di `app_main()`. A quel punto `s_lock` (il mutex del registro) non esiste ancora — `registry_lock()`/`registry_unlock()` sono no-op finché `s_lock == NULL`, il che è sicuro solo perché tutta quella fase è a singolo thread. La prima vera chiamata a `sensors_local_init()` (da `weather_start()`, una volta che lo scheduler è attivo) crea il mutex e rende thread-safe ogni accesso successivo al registro.

Un driver che fallisce il proprio `init()` o restituisce un errore da `save()` viene registrato nel log (`ESP_LOGW`) e **saltato**; non interrompe mai il passaggio per gli altri driver, e non fa mai fallire il beacon Weather.

### Flusso dati end-to-end, dal sensore all'APRS

```
 avvio (prima di app_main)
   └─ viene eseguito il constructor di ogni file drivers/*.c
        └─ SENSORS_LOCAL_DRIVER_AUTOREGISTER → sensors_local_register(&my_driver)

 weather_start()  (chiamato da aprs_service.c, una volta, all'avvio)
   ├─ collega weather_telemetry_data.weather/.telemetry_report allo storage statico
   ├─ sensors_local_init()          ← crea il mutex del registro (thread-safe da qui in poi)
   ├─ sensors_local_init_all()      ← esegue init() su ogni driver auto-registrato
   └─ registra weather_service_1hz() (eseguito a 1 Hz da aprs_svc_tick) e weather_beacon_service() (eseguito dallo scheduler di beacon condiviso)

 weather_service_1hz()   (chiamato a 1 Hz da aprs_svc_tick)
   ├─ azzera i flag "enabled" in weather_telemetry_data (così un driver che smette di
   │    riportare un campo in questo ciclo non lascia un valore obsoleto che sembra valido)
   ├─ sensors_local_save(&weather_telemetry_data, SENSOR_LOCAL_DATA_ALL)
   │    └─ per ogni driver registrato le cui capabilities corrispondono:
   │         lo inizializza lazily se non già fatto, poi chiama il suo save()
   │         → il driver scrive direttamente in aprs_weather_report_t / aprs_telemetry_report_t
   └─ accumula ogni campo "Mediato" (checkbox della pagina Weather) in una somma/conteggio corrente

 weather_beacon_service()   (chiamato dallo scheduler condiviso; trasmette ogni g_config.wx_interval secondi, solo se wx_en)
   ├─ resolve_fields(): per ogni token WX via etere, legge il valore live direttamente da
   │    weather_telemetry_data, oppure il valore mediato accumulato sopra, in base al
   │    checkbox "Mediato" per campo — NON direttamente dal sensore, così un sensore
   │    intermittente contribuisce comunque a una media sensata
   ├─ build_wx_packet(): costruisce la riga TNC2 standard "!lat/lon_WIND/SPDgGUSTtTTTrRRRhHHbBBBBB…"
   └─ la trasmette via RF (aprs_service_send_tnc2()) e/o APRS-IS (igate_send_raw()), secondo la config

 Pagina di amministrazione web Weather (/wx)
   └─ wx_channel_select() di page_wx.c scorre il registro e, per ogni campo via etere, elenca solo
        i driver le cui proprietà pubblicate (sensor_local_properties.h) dichiarano QUEL campo, così
        l'operatore mappa "<sensore> <canale>" su di esso; la tabella di mappatura mostra anche il
        valore live di ogni canale via /wx/values (sensors_local_save_one())
```

Il punto chiave per chi aggiunge un sensore: **non si chiama mai nulla da `weather.c` o dall'amministrazione web manualmente.** Registrare il driver è tutta l'integrazione necessaria; il refresh a 1 Hz, la media, la codifica WX via etere e il selettore di canale lo scoprono tutti da soli attraverso il registro.

### I due driver di esempio inclusi

Due driver vengono compilati di default, unicamente per poter esercitare l'intera pipeline (registro → refresh 1 Hz → encoder/beacon WX → selettore di canale della pagina Weather) **senza alcun hardware reale collegato**:

* **`components/sensors_local/drivers/example/sensor_local_weather_example.c`** (nome driver `wx-example`) — dichiara solo `SENSOR_LOCAL_DATA_WEATHER`. Ad ogni `save()` compila vento (direzione/sostenuto/raffica), temperatura, umidità, pressione barometrica, pioggia dell'ultima ora e luminosità con valori **casuali** plausibili (`rnd(lo, hi)`) e imposta il flag `enabled[...]` di ogni campo. Condizionato da `CONFIG_SENSORS_LOCAL_WEATHER_EXAMPLE_DRIVER`, un'opzione Kconfig (menu `Sensors Local`) attiva di default.
* **`components/sensors_local/drivers/example/sensor_local_telemetry_example.c`** (nome driver `tlm-example`) — dichiara solo `SENSOR_LOCAL_DATA_TELEMETRY`. Ad ogni `save()` compila ogni canale analogico **allocato** con un valore casuale in `0..255` e ogni canale digitale con uno `0`/`1` casuale, toccando ancora una volta solo i canali effettivamente richiesti dal chiamante (`analog_count`/`digital_count`). Condizionato da `CONFIG_SENSORS_LOCAL_TELEMETRY_EXAMPLE_DRIVER`.

Entrambi sono pensati per essere **copiati, non conservati**: sono lo scheletro documentato per un driver reale della famiglia corrispondente. Cancellateli o mettetli in `#if 0` una volta che avete hardware reale, oppure lasciateli semplicemente registrati accanto al/ai vostro/i driver reale/i — il registro non ha problemi a tenerli entrambi contemporaneamente, e `sensors_local_unregister("wx-example")` ne rimuove uno in modo pulito se preferite non ricompilare.

### Aggiungere un nuovo sensore, passo per passo

#### 1. Decidere cosa produce il driver

Scegliete la famiglia di payload (o entrambe): un BME280 o un DS18B20 è Weather; un partitore di tensione batteria su un pin ADC, un interruttore porta/reed, o una sonda di umidità del suolo è naturalmente Telemetry (canale analogico o digitale); una scheda combinata può essere entrambe.

#### 2. Copiare uno scheletro e rinominarlo

Copiate il driver di esempio corrispondente (`sensor_local_weather_example.c` per Weather, `sensor_local_telemetry_example.c` per Telemetry, o partite da entrambi se vi servono entrambe) in una nuova cartella sotto `components/sensors_local/drivers/`, es. `drivers/bme280/bme280.c` (con il proprio `bme280_properties.h`). Poi aggiungete quella sorgente alla lista `SRCS` in `components/sensors_local/CMakeLists.txt` — il componente elenca le sue sorgenti driver esplicitamente, **non** c'è alcun glob con wildcard.

#### 3. Completare `init()`

Sostituite l'avvio con `srand()` con la vostra configurazione reale una tantum: configurate e sondate il bus I2C/SPI/UART, leggete e verificate un registro chip-ID, allocate eventuale storage per i coefficienti di calibrazione, e restituite `ESP_OK` solo quando siete sicuri che le letture successive avranno successo. Salvate ciò che servirà alla chiamata `save()` in seguito (un handle, costanti di calibrazione, un numero di GPIO, …) in `self->ctx`.

#### 4. Completare `save()`

Leggete il sensore, convertite nelle unità ingegneristiche che `weather_telemetry.h` documenta per ogni campo (Fahrenheit per la temperatura, mph per il vento, decimi di millibar per la pressione, centesimi di pollice per la pioggia, ecc. — l'header specifica l'unità e il range via etere di ogni campo), scrivete il/i valore/i, e impostate il/i flag `enabled[...]` corrispondente/i — altrimenti il campo viene trattato come "non riportato in questo ciclo" indipendentemente dal valore presente nello struct. Controllate sempre `kind` e che i puntatori/`*_qty` di destinazione non siano NULL/zero prima di scrivere, esattamente come fanno entrambi gli esempi; un driver invocato con `data->weather_qty == 0` (perché il chiamante voleva solo Telemetry in questo ciclo) deve restituire `ESP_OK` senza aver toccato nulla.

#### 5. Dichiarare il descrittore e auto-registrarlo

```c
static sensor_local_driver_t bme280_driver = {
    .name         = "bme280",
    .capabilities = SENSOR_LOCAL_DATA_WEATHER,
    .init         = bme280_init,
    .save         = bme280_save,
    .deinit       = bme280_deinit, // o NULL se non c'è nulla da spegnere
    .properties   = &bme280_properties, // da bme280_properties.h: quali campi WX può riempire questo driver
    .ctx          = &s_bme280_ctx,
};
SENSORS_LOCAL_DRIVER_AUTOREGISTER(bme280_driver);
```

`name` deve essere unico tra tutti i driver registrati (altrimenti la registrazione fallisce con `ESP_ERR_INVALID_STATE`) — è anche ciò che appare, testualmente, nel menu a tendina di canale della pagina Weather ("`0: bme280`"), quindi scegliete qualcosa che un operatore di stazione riconoscerà.

#### 6. Compilare — nient'altro da collegare

`idf.py build`. Poiché il componente collega con `WHOLE_ARCHIVE` (così il `--gc-sections` del linker non può scartare un oggetto il cui unico riferimento è il proprio constructor), una volta che la vostra sorgente è elencata nel `SRCS` del componente viene compilata, collegata, e si auto-registra all'avvio con **zero modifiche** a `sensors_local.c`, `sensors_local.h` o `weather.c` — l'unica modifica al build è aggiungere la vostra sorgente a `components/sensors_local/CMakeLists.txt`.

#### 7. Mapparlo nella pagina Weather

Flashate, aprite la pagina **Weather** dell'amministrazione web, e il nome del vostro driver ora appare come opzione nel menu a tendina di canale di ogni campo (`wx_channel_select()` di `page_wx.c` lo elenca automaticamente perché scorre il registro live). Scegliete quale/i campo/i via etere dovrebbe alimentare e salvate. **I canali Telemetry non hanno ancora un selettore equivalente né un encoder via etere** — i valori di un driver Telemetry arrivano a `weather_telemetry_data.telemetry_report[0]` e restano lì, letti solo dal futuro codice che aggiungerà il beacon `T#nnn`; oggi nulla li trasmette (vedi la [matrice delle funzionalità](#matrice-delle-funzionalità)).

#### 8. Esempio pratico: un BME280 I2C reale

Un pattern ridotto ma completo (la gestione degli errori e l'aritmetica reale dei registri sono lasciate al datasheet/libreria del sensore — il punto qui è la forma dell'integrazione con `sensors_local`, non un driver BME280 da zero):

```c
#include "esp_log.h"
#include "sensors_local.h"
#include "driver/i2c_master.h"   // o il vostro driver I2C preferito

typedef struct {
    i2c_master_dev_handle_t dev;
    // ... coefficienti di calibrazione letti durante init() ...
} bme280_ctx_t;

static bme280_ctx_t s_ctx;

static esp_err_t bme280_init(sensor_local_driver_t *self) {
    bme280_ctx_t *c = (bme280_ctx_t *)self->ctx;
    // apre il bus I2C / aggiunge il dispositivo al suo indirizzo a 7 bit, sonda il chip-id (0x60), ...
    // legge i registri di calibrazione in c-> ...
    if (/* sonda fallita */ false)
        return ESP_FAIL; // -> il driver viene marcato come fallito e saltato da quel momento
    return ESP_OK;
}

static esp_err_t bme280_save(sensor_local_driver_t *self, weather_telemetry_data_t *data, sensor_local_data_kind_t kind) {
    bme280_ctx_t *c = (bme280_ctx_t *)self->ctx;

    if (!(kind & SENSOR_LOCAL_DATA_WEATHER) || data->weather == NULL || data->weather_qty < 1)
        return ESP_OK; // nulla da fare in questo ciclo

    aprs_weather_report_t *wx = &data->weather[0];

    float temp_c, pressure_pa, humidity_pct;
    // ... avvia una misura in modalità forzata e legge + compensa i registri grezzi in
    //     temp_c / pressure_pa / humidity_pct usando i coefficienti di calibrazione di c ...

    wx->temperature_f = (int16_t)lroundf(temp_c * 9.0f / 5.0f + 32.0f);
    wx->enabled[APRS_WX_SENSOR_TEMPERATURE] = true;

    wx->barometric_pressure_tenths_mb = (uint32_t)lroundf(pressure_pa / 10.0f);
    wx->enabled[APRS_WX_SENSOR_BAROMETRIC_PRESSURE] = true;

    wx->humidity_percent = (uint8_t)lroundf(humidity_pct);
    wx->enabled[APRS_WX_SENSOR_HUMIDITY] = true;

    return ESP_OK;
}

static sensor_local_driver_t bme280_driver = {
    .name = "bme280",
    .capabilities = SENSOR_LOCAL_DATA_WEATHER,
    .init = bme280_init,
    .save = bme280_save,
    .deinit = NULL,
    .ctx = &s_ctx,
};
SENSORS_LOCAL_DRIVER_AUTOREGISTER(bme280_driver);
```

Salvate questo come `components/sensors_local/drivers/sensor_local_bme280.c`, collegate la lettura I2C reale dove i commenti lo indicano, `idf.py build`, e la pagina Weather offrirà `"N: bme280"` come sorgente per Temperatura, Pressione e Umidità.

### Istanze multiple dello stesso tipo di sensore

Nulla impedisce a due sensori fisici dello stesso tipo di coesistere (es. un BME280 interno e uno esterno): date a ciascuno la propria translation unit (o lo stesso file `.c` con due descrittori), un `name` **distinto** (`"bme280-indoor"` / `"bme280-outdoor"`), la propria istanza di struct `ctx`, e il proprio indirizzo I2C / bus / GPIO incorporato in quel `ctx`. Ognuno si registra indipendentemente e appare come una propria riga nel selettore di canale della pagina Weather.

### Gestione degli errori e fallimento del driver

* Un `init()` che restituisce qualcosa di diverso da `ESP_OK` imposta `failed = true` **permanentemente** per quella registrazione — il driver viene saltato da ogni futura `sensors_local_save()`, registrato nel log una volta (`ESP_LOGW`) nel momento del fallimento, finché qualcosa non chiama esplicitamente `sensors_local_unregister()` seguito da un nuovo `sensors_local_register()` (che resetta sia `initialized` sia `failed`).
* Un `save()` che restituisce un errore viene registrato nel log (`ESP_LOGW`) e semplicemente saltato **per quel singolo ciclo** — `initialized`/`failed` restano intatti, quindi il prossimo tick a 1 Hz riprova. Questo conta per i sensori con un singhiozzo di bus occasionale: una singola transazione I2C fallita non disabilita permanentemente il driver come farebbe invece un `init()` fallito.
* Entrambi i tipi di fallimento sono isolati a quel singolo driver; `sensors_local_save()` continua sempre con i driver rimanenti nel registro.

### Thread safety

`sensors_local_register()`/`unregister()`/`save()`/`get()`/`find()`/`count()` prendono tutti il mutex interno del registro, quindi sono sicuri da chiamare da qualsiasi task una volta che `sensors_local_init()` è stato eseguito. L'unica eccezione, per design, sono proprio i constructor di auto-registrazione: girano prima che lo scheduler esista, a singolo thread, senza che il mutex sia ancora creato — che è esattamente il motivo per cui `registry_lock()`/`registry_unlock()` sono scritti come no-op finché `s_lock == NULL`.

L'`init()`/`save()`/`deinit()` di un driver **non** sono avvolti in alcun lock dal framework — se lo stato privato del vostro driver (`ctx`) viene mai toccato da qualcosa di diverso dal refresh WX a 1 Hz (`weather_service_1hz`, eseguito da `aprs_svc_tick`) (per esempio, un ISR che aggiorna un contatore condiviso), il driver stesso è responsabile della sincronizzazione necessaria.

### Aggiungere un nuovo kind di sensore

Weather e Telemetry non sono le uniche famiglie di payload che il framework può mai trasportare — `sensors_local.h` documenta esattamente come estenderlo, proprio accanto all'enum:

```c
/*
 * To add a new sensor family in the future (e.g. GPS, power/battery, air
 * quality, ...), append a new bit here:
 *
 *     SENSOR_LOCAL_DATA_GPS = 1u << 2,
 *
 * and OR it into SENSOR_LOCAL_DATA_ALL. Nothing else in the registry needs to
 * change - sensors_local_register() only requires that a driver's
 * capabilities be non-zero, and any UI page (like the Weather "Sensor
 * Mapping" table) that wants only its own kind of sensor filters the
 * registry by testing `driver->capabilities & SENSOR_LOCAL_DATA_xxx`.
 */
```

Concretamente, aggiungere ad esempio un kind GPS significa:

1. Aggiungere `SENSOR_LOCAL_DATA_GPS = 1u << 2` a `sensor_local_data_kind_t` in `sensors_local.h`, e fargli l'OR in `SENSOR_LOCAL_DATA_ALL`.
2. Aggiungere lo struct di destinazione dove dovrebbe finire un fix GPS (un nuovo campo su `weather_telemetry_data_t`, o uno struct completamente nuovo, in `weather_telemetry.h`) — il registro stesso non ha mai bisogno di conoscerne la forma, poiché i driver ci scrivono direttamente.
3. Scrivere driver le cui `capabilities` includano il nuovo bit e il cui `save()` popoli il nuovo struct.
4. Ovunque un consumatore abbia bisogno del nuovo kind, filtrare il registro con `driver->capabilities & SENSOR_LOCAL_DATA_GPS`, esattamente come `wx_channel_select()` di `page_wx.c` filtra oggi su `SENSOR_LOCAL_DATA_WEATHER`. Il registro, `sensors_local_save()`, e ogni driver esistente restano completamente inalterati.

### La pagina legacy `/sensor` — non è la stessa cosa

L'albero contiene ancora un `page_sensor.c` (`components/webconfig/pages/`) di una vecchia pagina **`/sensor`** per slot — campi per slot (`enable`, `type`, `port`, `address`, `samplerate`, `averagerate`, tre coefficienti di equazione lineare `A`/`B`/`C`, un nome e un'unità) memorizzati in un array `g_config.sensor[]`. **Questo non è il framework `sensors_local`, e non è più attivo:** il file *non* è elencato in `webconfig/CMakeLists.txt` (quindi non viene compilato), nessuna rotta `/sensor` è registrata, e i campi `g_config.sensor[]`/`SENSOR_NUMBER` che referenziava sono stati rimossi da `app_config.h` — il sorgente non compilerebbe nemmeno più contro la configurazione attuale. Sopravvive solo come riferimento storico. Se state collegando hardware reale, usate `sensors_local` (questa sezione).

### Riepilogo di riferimento Sensori

| Concetto | Dove | Scopo |
|---|---|---|
| `sensor_local_driver_t` | `components/sensors_local/include/sensors_local.h` | descrittore di un driver: nome, capabilities, init/save/deinit, ctx |
| `sensors_local_register()` / `_unregister()` | `sensors_local.h` / `.c` | aggiungere/rimuovere un driver dal registro a runtime |
| `SENSORS_LOCAL_DRIVER_AUTOREGISTER(sym)` | `sensors_local.h` | macro constructor del C: auto-registra un driver `static` prima di `app_main()` |
| `sensors_local_save(data, kind)` | `sensors_local.h` / `.c` | L'ingresso aggregato: chiede a ogni driver capace e sano di riempire `data` |
| `weather_telemetry_data_t` | `components/weather_telemetry/include/weather_telemetry.h` | il contenitore condiviso in cui scrivono i driver (`weather[]` + `telemetry_report[]`) |
| `weather.c` | `main/weather.c` | possiede il contenitore, guida il refresh a 1 Hz, codifica e trasmette il vero rapporto WX APRS |
| `sensor_local_properties_t` | `components/sensors_local/include/sensor_local_properties.h` | descrittore per driver di quali campi WX / canali TLM può produrre + le loro etichette |
| `sensors_local_save_one(index,data,kind)` | `sensors_local.h` / `.c` | legge UN driver per indice (anteprima live per canale dietro `/wx/values`) |
| `page_wx.c` | `components/webconfig/pages/page_wx.c` | pagina Weather; selettore di canale per campo filtrato dalle proprietà del driver, valori live via `/wx/values` |
| `drivers/*.c` | `components/sensors_local/drivers/` | dove aggiungere un nuovo sensore — un file, nessun'altra modifica |
| pagina `/sensor` (`page_sensor.c`) | `components/webconfig/pages/` | **sorgente legacy orfano** — non compilato, non instradato; campi `g_config.sensor[]` rimossi |

---

## Compilazione e flashing

### Prerequisiti

* **ESP-IDF v5.1 o successivo** (bloccato/testato alla **5.5.4** — vedi `dependencies.lock`).
* Un ESP32 con **≥ 4 MB di flash**.
* Il component manager dell'IDF scarica automaticamente **`joltwallet/littlefs ^1.14`** (bloccato alla 1.22.2) e, tramite il componente `sensors_local`, **`esp-idf-lib/bmp180`** (che trascina `i2cdev` + `esp_idf_lib_helpers`).

### Compilazione

```bash
. $IDF_PATH/export.sh

cd workspace-APRS/esp32_APRS_igate

idf.py set-target esp32          # sdkconfig ha già target=esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Per compilare in spagnolo o italiano invece che in inglese:

```bash
idf.py build -DLANGUAGE=LANG_ES
idf.py build -DLANGUAGE=LANG_IT
```

> `espressif/esp-dsp` intenzionalmente **non** è una dipendenza: veniva incluso solo dal vecchio componente `esp32_IDF_libAPRS`. Il modem attuale implementa i propri filtri e nulla nel progetto chiama `dsps_*`. Se stai aggiornando da un checkout più vecchio, elimina `dependencies.lock` e lascia che `idf.py` lo rigeneri.

### Tabella delle partizioni (`partitions.csv`)

| Nome | Tipo | Sottotipo | Offset | Dimensione |
|---|---|---|---|---|
| `nvs` | data | nvs | 0x9000 | 24 K |
| `otadata` | data | ota | 0xF000 | 8 K |
| `phy_init` | data | phy | 0x11000 | 4 K |
| `ota_0` | app | ota_0 | 0x20000 | **1728 K** |
| `ota_1` | app | ota_1 | 0x1D0000 | **1728 K** |
| `storage` | data | spiffs | 0x380000 | **512 K** → montata come **LittleFS** su `/storage` |

Due slot app OTA (`ota_0` / `ota_1`) → **l'aggiornamento OTA è supportato** dalla pagina **About / Firmware** dell'amministrazione web: si carica un `.bin`, viene scritto nello slot che non è attualmente in esecuzione, e il dispositivo si riavvia in quello slot una volta che la scrittura è verificata. `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` è attivo, quindi una nuova immagine che non riesce a far ripartire l'amministrazione web viene riportata automaticamente allo slot precedente al reset successivo (vedi `esp_ota_mark_app_valid_cancel_rollback()` in `main.c`).

> **Migrazione di un dispositivo esistente:** questa tabella delle partizioni ha sostituito il vecchio layout a singola partizione `factory`. Un dispositivo che gira ancora sulla vecchia tabella non ha `ota_0`/`ota_1` verso cui aggiornarsi, quindi il primo passaggio a questo firmware deve essere un reflash una tantum via USB/UART (`idf.py -p PORT flash`, oppure un `.bin` unificato completo). Ogni aggiornamento successivo può passare dall'amministrazione web.

`sdkconfig` viene fornito con `CONFIG_COMPILER_OPTIMIZATION_DEBUG=y` (`-Og`) e le asserzioni attive; passare a `-Os` se si è a corto di flash.

---

## Primo avvio e configurazione

1. Su una partizione nuova, LittleFS si auto-formatta e `app_config_load()` scrive `/storage/config.json` a partire dai default di fabbrica.
2. L'ESP32 si avvia come **Access Point Wi-Fi**:
   * SSID **`esp32idf_APRS`**, password **`esp32idf_APRS`**, canale 1, massimo 4 client, WPA2-PSK.
3. Collegarsi ad esso e navigare verso il dispositivo (predefinito `http://192.168.4.1/`).
4. **Accedere: `admin` / `admin`** — cambiarlo nella pagina *System*.
5. Su *Wireless*: scegliere **Station** o **AP+STA**, spuntare **Enable** in un blocco Wi-Fi Client, inserire SSID/password, Salvare.
6. Su *IGate*: impostare il proprio **nominativo**, **SSID**, **passcode**, **host**/**porta** APRS-IS, filtro, coordinate, simbolo, commento.
7. Su *Radio / Modem*: abilitare il modem audio, scegliere la modulazione, il pin del PTT e la sua polarità, il preambolo, lo slot temporale TX.
8. Riavviare (oppure Salvare — la maggior parte delle cose si riapplica live).

### Default di fabbrica notevoli

| Impostazione | Predefinito |
|---|---|
| Modalità Wi-Fi | AP (`2`) — sempre raggiungibile |
| SSID / password AP | `esp32idf_APRS` / `esp32idf_APRS` |
| Login web | `admin` / `admin` |
| Hostname | `ESP32APRS` |
| Frequenza CPU | 160 MHz |
| Fuso orario | 7,0 (l'orologio stesso è sempre UTC) |
| Host NTP | `pool.ntp.org`, `time.google.com`, `time.cloudflare.com` |
| IGate | **abilitato**, `rf2inet` attivo, `inet2rf` disattivo |
| Nominativo / SSID | `NOCALL` / 10, passcode `-1` |
| Host / porta APRS-IS | `aprs.dprns.com` : 14580 |
| Posizione IGate | 13,7563 / 100,5018 (Bangkok), intervallo 30 |
| Simbolo | `N&` |
| Preset di percorso 0 | `WIDE1-1,WIDE2-1` |
| Digipeater | disabilitato, SSID 1 |
| Tracker | disabilitato, SSID 9 |
| Modem audio | **abilitato**, 1200 Bd Bell 202 |
| Preambolo / slot TX | 300 ms / 2000 ms |
| FX.25 | disattivo |
| PTT | GPIO26, attivo basso |
| Messaggistica | abilitata, RF + INET, crittografia disattivata |

> 🔴 **Cambiare `NOCALL` e impostare un passcode reale prima di trasmettere.** Verificare inoltre di essere abilitati per la frequenza e il duty cycle su cui si sta per chiudere in trasmissione.

---

## Riferimento amministrazione web

### Rotte HTTP

| Metodo | Rotta | Scopo |
|---|---|---|
| GET | `/` | landing radice / login |
| GET | `/logout` | rimuove l'autenticazione Basic |
| GET | `/dashboard` | dashboard live |
| GET/POST | `/station` | identità della propria stazione: nominativo, lat/lon/alt |
| GET/POST | `/bulletins` | bollettini APRS BLN1..BLN5 |
| GET/POST | `/objects` | Oggetti / Item APRS |
| GET | `/sidebarInfo` | frammento di statistiche della sidebar |
| GET | `/dashinfo` | striscia compatta di informazioni live (JSON) |
| GET | `/style.css` | foglio di stile condiviso |
| GET | `/lastheard` | tabella LAST HEARD (JSON) |
| GET | `/igate_traffic?since=<seq>` | delta del log di traffico (JSON) |
| GET/POST | `/wireless` | modalità Wi-Fi, AP, 5 slot STA, potenza TX |
| GET | `/wifiscan` | risultati della scansione AP (JSON) |
| GET/POST | `/system` | login, hostname, frequenza CPU, NTP, preset di percorso, timeout di reset |
| POST | `/default` | ripristino di fabbrica |
| GET/POST | `/igate` | impostazioni IGate |
| GET/POST | `/digi` | impostazioni digipeater |
| GET/POST | `/tracker` | impostazioni tracker |
| GET/POST | `/wx` | impostazioni del rapporto meteo — completamente implementato, invia beacon WX APRS reali, vedi [Sensori](#sensori) |
| GET | `/wx/values` | valori dei sensori per canale in tempo reale per la tabella di mappatura Weather (JSON) |
| GET/POST | `/tlm` | impostazioni di telemetria (solo configurazione; i valori sono raccolti tramite `sensors_local` ma non ancora codificati/trasmessi via etere, vedi [Sensori](#sensori)) |
| GET/POST | `/radio` | modulo RF + modem audio AFSK |
| GET | `/radio/looptest` | esegue il loop test (risultato JSON) |
| GET/POST | `/vpn` | WireGuard (impalcatura) |
| GET/POST | `/mqtt` | MQTT (impalcatura) |
| GET/POST | `/msg` | config del motore di messaggistica (RF/INET, retry, cifratura) |
| GET/POST | `/msgchat` | UI inbox/composizione in stile chat |
| GET | `/msgchat/list` | frammento della lista messaggi (JSON) |
| GET/POST | `/gnss` | GNSS (impalcatura) |
| GET/POST | `/mod` | mappatura GPIO / hardware |
| GET | `/symbol` | riferimento/selettore dei simboli APRS |
| GET | `/test` | riepilogo dell'autotest di configurazione |
| GET | `/storage` | browser dei file |
| GET | `/download?file=…` | scarica da LittleFS |
| GET | `/delete?file=…` | elimina un file |
| POST | `/upload` | upload multipart |
| POST | `/format` | riformatta LittleFS |
| GET | `/about` | versione firmware/IDF, partizione, form di aggiornamento OTA |
| POST | `/ota_update` | upload multipart del firmware → flash dello slot OTA inattivo → riavvio |

### Pagina per pagina

**Dashboard** — pillole di stato della rete (Wi-Fi, APRS-IS tramite `igate_is_connected()`), pannello STATISTICS, tabella LAST HEARD con icone dei simboli, e una tabella di traffico live (colonne DX / PACKET / AUDIO) alimentata da long-polling basato su sequenza.

Le statistiche provengono da `aprs_service_get_stats()`, tracciate **indipendentemente** da `igate_en`/`digi_en`:

| Contatore | Significato |
|---|---|
| `radio_rx` | ogni frame decodificato dal modem via RF |
| `radio_tx` | ogni frame trasmesso con successo via RF |
| `rf2inet` | frame che l'IGate ha effettivamente inoltrato in uplink |
| `inet2rf` | righe APRS-IS effettivamente trasmesse via RF |
| `digi` | frame ridigipetati (percorso riscritto + ritrasmesso) |

> Questo è intenzionale. I contatori venivano un tempo improvvisati da `digi_get_stats()`/`igate_get_stats()`, che si muovono solo dall'interno di `digiProcess()`/`igateProcess()` — quindi con entrambe le funzionalità disattivate (una comune configurazione di solo RX/monitoraggio) la dashboard restava a zero per sempre, indipendentemente da quanto traffico veniva decodificato.

**Radio / Modem** — *Protocollo*: interruttore FX.25. *Modulo RF* (solo con `ENABLE_RF_MODULE`): tipo SX127x/SX126x/SX128x, LoRa/G3RUH/GFSK/D-PRS, MHz RX/TX, CTCSS/DCS. *Audio / AFSK*: abilitazione, modulazione (300 / 1200 Bell202 / 1200 V.23 / 9600 G3RUH), **menu a tendina GPIO del PTT** (vengono offerti solo i pin validi, più *Disabled*), PTT attivo-alto, LPF audio (audio piatto), preambolo in ms, slot temporale TX in ms, e il pulsante **LOOP TEST**. Salvare riapplica il modem live tramite `aprs_service_apply_modem_config()` — senza riavvio.

**IGate** — abilitazione, RF→INET / INET→RF, entrambe le bitmask dei filtri, nominativo/SSID/passcode, host/porta, stringa di filtro lato server, beacon on/off, lat/lon/alt, intervallo, selettore del simbolo, oggetto, commento, status, PHG (calcolato lato client da potenza/guadagno/altezza/direzione, persistito così il form si ripresenta correttamente).

**Weather** — abilitazione, invio via RF/-INET, interruttore timestamp, nominativo/SSID/percorso WX, posizione (lat/lon/alt fissa o flag GPS), nome oggetto, commento, caselle "Averaged" per campo, e — per ogni campo WX via etere — un **menu a tendina di canale** popolato live dal registro `sensors_local` e **filtrato in base alle capacità pubblicate da ciascun driver** (`sensor_local_properties.h`), così sulla riga di ogni campo compaiono solo i driver che possono davvero produrlo (Vento / Temperatura / Umidità / Pressione / Pioggia / Luminosità / Altezza di piena), etichettati come "`<sensore> <canale>`". La tabella di mappatura mostra anche il **valore live** di ogni canale selezionato, recuperato su richiesta da `/wx/values` (basato su `sensors_local_save_one()`). Vedi [Sensori](#sensori).

**Station** — l'identità condivisa della propria stazione letta da ogni beacon, oggetto e messaggio: nominativo, latitudine, longitudine e altitudine (`g_config.my_*`). Regolata da `ENABLE_STATION`.

**Bulletins** — fino a cinque bollettini APRS (destinatario `BLN1`..`BLN5`), ciascuno con il proprio testo, abilitazione via RF e/o APRS-IS, intervallo di trasmissione e una finestra opzionale "scadenza dopo N ore". I bollettini risiedono nel proprio `/storage/bulletins.json` (non in `g_config`, per mantenere piccola la configurazione residente); un bollettino scaduto azzera da solo il flag di abilitazione e lascia l'etere. Regolata da `ENABLE_BULLETINS`.

**Objects and Items** — fino a cinque Oggetti/Item APRS, ciascuno con nome, posizione, simbolo, rotta/velocità, commento, abilitazione via RF e/o APRS-IS, intervallo di ripetizione e un flag "permanente" in stile YAAC (permanente → Item senza timestamp, altrimenti → Oggetto con timestamp) più decadimento opzionale dell'intervallo. "Uccidere" un elemento lo trasmette qualche volta in più (così gli ascoltatori lo scartano) e poi lo disabilita da solo. Salvati nel proprio `/storage/objitems.json`. Regolata da `ENABLE_OBJECTS_ITEMS`.

**Snd/Rcv Msg (chat messaggi)** — la vera UI inbox/composizione APRS (`/msgchat`): un pannello scorrevole dei messaggi inviati/ricevuti da questa stazione, un campo nominativo di destinazione, una casella di testo (limitata alla lunghezza del messaggio APRS) e un pulsante Invia, aggiornata tramite `/msgchat/list`. Distinta dalla pagina **Message**, che si limita a *configurare* il motore di messaggistica (abilitazione RF/INET, retry, cifratura). Regolata da `ENABLE_MSG_CHAT`.

**Wireless** — modalità (off/STA/AP/AP+STA), SSID/password/canale dell'AP, 5 slot STA ciascuno con la propria casella **Enable**, potenza TX in dBm (convertita ×4 in quarti di dBm per `esp_wifi_set_max_tx_power()`), più una scansione live. La scansione commuta temporaneamente una radio solo-AP in AP+STA — motivo per cui `s_staEnabled` regola ogni `esp_wifi_connect()` automatico, così il gestore degli eventi non entra in conflitto con la scansione.

**System** — login web, hostname, frequenza CPU (applicata live), 3 host NTP, intervallo di risincronizzazione, timeout di reset, e i **quattro preset di percorso** `path[0..3]`.

**Storage** — browser LittleFS: download, cancellazione, upload multipart, utilizzo, formattazione.

**About** — nome del progetto, versione, data/ora di build, versione IDF, etichetta/offset/dimensione della partizione in esecuzione, e il pannello **OTA Update**: scegliere un `.bin`, caricarlo (barra di avanzamento, trasmesso in streaming direttamente nello slot `ota_0`/`ota_1` inattivo tramite `esp_ota_write()` — mai bufferizzato per intero in RAM), e il dispositivo si riavvia in quello slot una volta scritto e verificato. Un'immagine difettosa che non viene mai confermata (vedi `esp_ota_mark_app_valid_cancel_rollback()` in `main.c`) viene automaticamente riportata allo slot precedente al reset successivo. Se il dispositivo è ancora sulla vecchia tabella a singola partizione `factory`, questo pannello lo segnala e richiede prima un reflash manuale via USB/UART.

---

## Memorizzazione della configurazione (`config.json`)

* Percorso: **`/storage/config.json`** su LittleFS.
* Serializzato con **cJSON**; i nomi dei campi e le chiavi JSON sono mantenuti **1:1 con il `config.h`/`config.cpp` originale**, così ogni valore mostrato dall'amministrazione web ha una collocazione e i vecchi file si caricano senza modifiche.
* **Salvataggio atomico**: scrive `/storage/config.json.tmp`, poi rinomina.
* Mancante o corrotto → vengono applicati i default **e salvati immediatamente**, così il file esiste sempre ed è coerente.
* API: `app_config_set_defaults()`, `app_config_load()`, `app_config_save()`, `app_config_factory_reset()`. Istanza live: `extern app_config_t g_config`.

### Interruttori dei moduli a tempo di compilazione (`main/include/app_config.h`)

```c
#define ENABLE_DASHBOARD
#define ENABLE_MSG_CHAT
#define ENABLE_BULLETINS
#define ENABLE_OBJECTS_ITEMS
#define ENABLE_STATION
#define ENABLE_RADIO_MODEM
//#define ENABLE_RF_MODULE
//#define ENABLE_VPN
//#define ENABLE_MQTT
#define ENABLE_MESSAGE
//#define ENABLE_MOD_GPIO
#define ENABLE_IGATE
#define ENABLE_DIGIPEATER
#define ENABLE_TRACKER
//#define ENABLE_SMARTBEACONING
#define ENABLE_WEATHER
//#define ENABLE_TELEMETRY
#define ENABLE_SYSTEM
#define ENABLE_WIRELESS
//#define ENABLE_GNSS
#define ENABLE_FILE_STORAGE
#define ENABLE_ABOUT_FIRMWARE
```

Commentandone uno si rimuove la sua voce dalla sidebar e la sua pagina dall'immagine. `ENABLE_WEATHER` è **attivo** di default: il rapporto meteo della propria stazione è una funzionalità completa, non impalcatura (vedi [Sensori](#sensori)). `ENABLE_MSG_CHAT`, `ENABLE_BULLETINS`, `ENABLE_OBJECTS_ITEMS` e `ENABLE_STATION` regolano rispettivamente le pagine di chat messaggi, bollettini, oggetti/item e identità di stazione. **Non** esiste un interruttore `ENABLE_SENSORS`: il framework driver `sensors_local` non ha una disabilitazione a tempo di compilazione ed è sempre incluso (i suoi singoli driver sono regolati dai rispettivi `CONFIG_SENSORS_LOCAL_*_DRIVER` in `components/sensors_local/CMakeLists.txt`, non da questa lista). Allo stesso modo non esiste più una pagina `/sensor` funzionante — `page_sensor.c` è sorgente orfano che non viene più compilato né instradato.

---

## Preset del percorso e bitmask del percorso

Ogni servizio (tracker / igate / digi / wx / …) memorizza una **bitmask**, non una stringa di percorso. Il bit *N* seleziona **`g_config.path[N]`**, il preset in testo libero modificabile nella pagina *System*. `buildPathSuffix()` concatena ogni slot non vuoto selezionato; gli slot selezionati ma vuoti vengono semplicemente saltati.

I flag di attivazione fungono anche da valori predefiniti della bitmask:

```
ACTIVATE_OFF 0 · TRACKER 1<<0 · IGATE 1<<1 · DIGI 1<<2 · WX 1<<3
ACTIVATE_TELEMETRY 1<<4 · QUERY 1<<5 · STATUS 1<<6 · WIFI 1<<7
```

Bit del filtro IGate (condivisi da `rf2inetFilter` e `inet2rfFilter`):

```
MESSAGE 1<<0 · STATUS 1<<1 · TELEMETRY 1<<2 · WEATHER 1<<3 · OBJECT 1<<4
ITEM 1<<5 · QUERY 1<<6 · BUOY 1<<7 · POSITION 1<<8
```

---

## Il LOOP TEST

Lo strumento di messa in servizio singolarmente più utile del progetto. Collegare **GPIO25 → GPIO33**, aprire *Radio / Modem*, premere **LOOP TEST**.

Cosa fa (`aprs_loop_test_run()`):

1. Costruisce un piccolo pacchetto APRS che porta un **token casuale monouso** (`>LOOPTEST <token>`).
2. **Devia** i frame decodificati verso il proprio hook (`s_rxHook`) in modo che il frame di test non venga mai ridigipetato, inoltrato in uplink verso APRS-IS, o loggato come traffico reale.
3. Commuta il modem in **full duplex** — un filo DAC→ADC significa che il nodo sente sempre la propria portante e il CSMA non chiuderebbe mai in trasmissione.
4. Trasmette, poi attende fino a **4000 ms** che la catena ADC → demodulatore → HDLC → AX.25 restituisca lo stesso frame.
5. **Ripristina sempre** l'hook reale e la modalità duplex configurata prima di ritornare.

Nel frattempo un task di monitoraggio cattura diagnostiche che il componente espone solo in modo istantaneo: uno snapshot passivo dell'ADC grezzo a metà preambolo (`afskDiagCaptureRaw()`, direttamente dalla ISR di conversione, senza disturbare il task RX live), l'RMS di picco (`afskGetRms()`), il guadagno AGC di picco (`afskGetAgcGain()`), una bitmap del DCD (`ModemDcdState()`), e la fase RX HDLC più avanzata raggiunta per demodulatore (`Ax25GetRxStage()`).

I messaggi di errore distinguono quindi, nello stesso modo in cui lo facevano le diagnostiche latching appositamente costruite del vecchio componente:

| Sintomo | Diagnosi |
|---|---|
| ADC grezzo min≈max | ADC morto / non cablato |
| l'ADC grezzo oscilla, RMS ~0 | nessun tono raggiunge l'ADC |
| RMS ok, DCD mai impostato | tono presente, PLL mai agganciata → discrepanza baud/tipo di modem o audio difettoso |
| DCD agganciato, `rxStageMax < RX_STAGE_FRAME` | flag visti ma nessun frame mai iniziato — bug nel recupero bit, non rumore |
| DCD agganciato, `rxStageMax = FRAME`, nessun frame | frame assemblati ma tutti falliti al CRC — livello/SNR marginale |
| frame tornato, token non corrispondente | distorsione, clipping, o cablaggio del loopback sbagliato |
| PASS | riporta il livello RX in mV RMS |

Due distinzioni che le vecchie diagnostiche facevano sono **sparite per progetto**: non esiste uno squelch software (quindi niente "squelch mai aperto"), e non ci sono contatori di fallimento CRC (la fase HDLC più avanzata raggiunta li sostituisce).

---

## Localizzazione

**Una lingua per immagine firmware.** Nessun cambio a runtime; le stringhe di nessun'altra lingua vengono compilate.

* `app_config.h` definisce `LANG_EN 0`, `LANG_ES 1`, `LANG_IT 2` e la `LANGUAGE` attiva (predefinita `LANG_EN`).
* `translations/translations.h` è l'*unico* punto che decide quale `lang_xx.h` viene incluso.
* Ogni stringa visibile all'utente passa attraverso una macro `TR_xxx`.

**Aggiungere una lingua:**

1. Copiare `translations/lang_en.h` → `lang_xx.h`, tradurre ogni stringa letterale, mantenere identico ogni nome di macro.
2. `#define LANG_XX <prossimo numero libero>` in `app_config.h`.
3. Aggiungere un ramo `#elif LANGUAGE == LANG_XX` in `translations.h`.
4. Compilare con `-DLANGUAGE=LANG_XX`.

Una `TR_xxx` mancante in una lingua è un **errore di compilazione in quella build linguistica** — intenzionale, così le stringhe non tradotte non possono essere spedite silenziosamente.

---

## Risoluzione dei problemi

**"Sono passato alla modalità Station, ho salvato, riavviato, e non succede nulla."**
Leggere il log di avvio — questo percorso è strumentato pesantemente apposta:

* `esp_wifi_connect()` è legale solo una volta che la stazione è *effettivamente* partita, cosa che il driver segnala con `WIFI_EVENT_STA_START`. Chiamarlo subito dopo `esp_wifi_start()` perde questa corsa e restituisce `ESP_ERR_WIFI_NOT_STARTED`; nessuna associazione, nessun `STA_DISCONNECTED`, quindi nessun retry. La connessione viene emessa **dal gestore di STA_START** e ogni tentativo logga il proprio risultato.
* Se nessuno slot Wi-Fi Client è **abilitato con un SSID**, il firmware dumpa **ogni slot** e indica quale sia l'errore ("abilitato, ma l'SSID è VUOTO" contro "ha un SSID, ma 'Enable' non è spuntato").
* Il solo STA senza nulla a cui unirsi lascerebbe il dispositivo irraggiungibile, quindi **ricade su AP+STA** e lo segnala — l'amministrazione web resta attiva.

**I codici motivo di disconnessione** vengono loggati (prima venivano scartati):

| Motivo | Significato |
|---|---|
| 15 (`4WAY_HANDSHAKE_TIMEOUT`), 204 (`NOT_AUTHED`) | password sbagliata |
| 201 (`NO_AP_FOUND`) | SSID non visibile: nome sbagliato, fuori portata, o solo 5 GHz |
| 2 / 8 / 200 | roaming ordinario / cadute lato AP |

Le riconnessioni usano un **back-off crescente** (500 ms per ogni fallimento consecutivo, limitato a 8 s), armato su un `esp_timer` — **non** `vTaskDelay()` dentro il gestore degli eventi, cosa che bloccherebbe il loop degli eventi condiviso (incluso proprio quel `IP_EVENT_STA_GOT_IP` che sta aspettando) e, in un ciclo di disconnessione stretto, affamerebbe il task idle finché il watchdog del task non scatta.

**"L'AP non si associa affatto"** — un `wifi_config_t` azzerato lascia `pmf_cfg.capable = false`, e gli AP WPA3 / WPA2-con-PMF-richiesto rifiutano semplicemente una tale stazione. Il firmware imposta *capable, non required*, il che funziona sia con gli AP vecchi che con quelli nuovi.

**"L'avvio si blocca per ~5 secondi"** — previsto: `modem_init()` blocca mentre `ModemCalibrateSampleRate()` misura il clock ADC reale. Una volta per avvio.

**"I beacon all'avvio non trasmettono"** — previsto: `aprs_service_start()` gira prima di `modem_init()`, quindi i primi beacon vengono scartati con un log di debug finché `s_modemReady` non è impostato.

**"Il LOOP TEST fallisce con 'nessun pacchetto ricevuto indietro'"** — verificare la storia dell'attenuazione ADC: il DAC oscilla su tutto il rail mentre un'attenuazione di 0 dB misura solo ~0–1,1 V, tagliando il tono oltre la capacità del demodulatore di agganciarsi. Il componente ha codificato `ADC_ATTEN_DB_12`, che è corretto; se l'hai sovrascritto, rimettilo com'era.

**"L'IGate dice unverified"** — `aprs_mycall` / `aprs_passcode` sbagliati. Il banner viene loggato; così anche la riga di login esatta, inclusa la stringa di filtro, in modo che un filtro malformato sia visibile immediatamente.

**"Funziona tutto ma aprs.fi non mostra la mia stazione"** — beacon: abilitare `igate_bcn` e almeno uno tra `igate_loc2rf` / `igate_loc2inet`, e impostare coordinate reali. Ridigipetare il traffico non ti annuncia mai.

**"9600 Bd perde frame"** — è esattamente la patologia per cui sono stati modificati la frequenza dell'ADC, la dimensione del frame di conversione e la divisione dei core. Se hai sovrascritto `MODEM_ADC_SAMPLERATE`, `MODEM_ADC_CONV_FRAME`, `MODEM_DAC_TIMER_CORE` o `MODEM_ADC_ISR_CORE`, rileggi [Perché i valori sono quelli che sono](#perché-i-valori-sono-quelli-che-sono). Verifica anche di star fornendo audio **piatto/discriminatore**.

---

## Stato e limiti noti

* **Lavori in corso.** Lo dice il README a monte, e lo dice anche questo.
* **OTA disponibile** — pagina About / Firmware, slot `ota_0`/`ota_1` con rollback automatico. I dispositivi ancora sulla vecchia tabella a singola partizione `factory` necessitano di un reflash seriale per passare a questo layout.
* **`rf2inetFilter` non è applicato.** `igateProcess()` applica ancora solo le regole RFONLY/TCPIP/qA/NOGATE/satellite e la deduplica, non la bitmask del tipo di payload. `aprs_filter_classify_tnc2()` / `aprs_filter_pass()` sono indipendenti dalla direzione, quindi collegarlo lì è una modifica di due righe.
* **Viene usato solo il primo slot Wi-Fi STA abilitato.** Il failover multi-AP è annotato come "può essere aggiunto in seguito".
* **Nessun GPS, nessun SmartBeaconing.** I campi di configurazione esistono; i beacon sono solo a posizione fissa.
* **Nessun driver LoRa / modulo RF.** `ENABLE_RF_MODULE` è commentato; l'UI e la configurazione SX12xx sono solo impalcatura.
* **VPN / MQTT / GNSS / Bluetooth / PPP / OLED / Modbus**: campi di configurazione e (alcune) pagine esistono, nessuna implementazione.
* **Il meteo è implementato** (non è impalcatura): `weather.c` esegue un vero refresh a 1 Hz di `sensors_local` e un vero beacon WX via etere. Vedi [Sensori](#sensori).
* **La raccolta di telemetria funziona; la telemetria via etere no.** `sensors_local` riempie già i canali analogici/digitali ogni secondo, ma nulla codifica o trasmette ancora un pacchetto `T#nnn`, e la pagina Telemetria non ha un selettore di canale `sensors_local`. Vedi [Sensori](#sensori).
* **La pagina legacy `/sensor` è stata rimossa.** `page_sensor.c` è orfano (non più compilato né instradato) e i suoi campi `g_config.sensor[]` sono stati rimossi; usare `sensors_local`.
* **L'analisi dei simboli** copre solo i formati di posizione senza timestamp `!` / `=`; `/` e `@` lasciano l'icona vuota.
* **`agc_max_gain`, `sql_level`, `volume`, `adc_gpio`, `dac_gpio`, `rf_sql_*`, `rf_pwr_*`, `adc_atten`** sono inerti dalla sostituzione del modem; mantenuti solo per compatibilità con `config.json`.
* `sdkconfig` viene fornito con `-Og` + asserzioni, non un profilo di release.

---

## Crediti

* **Questo progetto e il componente modem:** Emiliano Augusto González — **LU3VEA** — `lu3vea @ gmail . com` · <https://github.com/hiperiondev>
* Il modem si basa su, e deve la sua discendenza DSP a:
  * **VP-Digi** — SQ8VPS — <https://github.com/sq8vps/vp-digi>
  * **ESP32APRS_Audio** — nakhonthai — <https://github.com/nakhonthai/ESP32APRS_Audio>
  * **LibAPRS** — Mark Qvist — <https://github.com/markqvist/LibAPRS>

  Contattare i rispettivi autori per informazioni su quei progetti.
* **littlefs** — ARM/joltwallet (BSD-3-Clause), tramite l'ESP component registry.
* Lo schema di configurazione, il layout dell'amministrazione web e la semantica della dashboard seguono il progetto di riferimento **esp32idf_APRS / ESP32APRS**, così i file `config.json` esistenti e le aspettative degli utenti restano valide.

---

## Licenza

**GNU General Public License v3.0** — vedi [`LICENSE`](LICENSE).

Il componente incluso `managed_components/joltwallet__littlefs` porta la propria licenza (BSD-3-Clause per littlefs stesso).

---

### Avvertenza radioamatoriale

Trasmettere sulle frequenze radioamatoriali richiede una licenza valida per il proprio paese e la propria banda. **Impostare un nominativo reale** (il default è `NOCALL`), usare un passcode APRS-IS legittimo, rispettare il proprio band plan locale e le convenzioni di digipeating (`WIDE1-1,WIDE2-1` *non* è sempre appropriato), e non inoltrare traffico `NOGATE`/`RFONLY`. Sei responsabile di tutto ciò che questo dispositivo trasmette.
