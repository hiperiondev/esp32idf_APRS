<p align="center">
  <img src="https://github.com/hiperiondev/esp32idf_APRS/raw/main/images/logo.png" width="300">
</p>

<div align="center">

# esp32idf_APRS

### Una stazione APRS completa su un singolo ESP32 — ESP-IDF nativo, senza Arduino.

**IGate · Digipeater · Tracker · Meteo · Telemetria**, con un pannello di amministrazione web integrato, un soft-modem AFSK/FSK sul chip stesso, uplink verso APRS-IS, un framework di driver per sensori a runtime e aggiornamenti firmware OTA.

[![Docs](https://img.shields.io/badge/docs-readthedocs-blue)](https://esp32idf-aprs.readthedocs.io/)
[![License](https://img.shields.io/badge/license-GPLv3-green)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-ESP32-red)](#hardware)
[![Framework](https://img.shields.io/badge/framework-ESP--IDF%205.x-orange)](#)

**🌐 Lingue:** [English](README.md) · [Español](README.es.md) · **Italiano**

</div>

---

## Che cos'è?

`esp32idf_APRS` trasforma una semplice **scheda ESP32 DevKit** più un'interfaccia audio economica in una stazione **APRS** completa e autonoma. Tutto viene eseguito sull'ESP32 stesso — non c'è core Arduino, né PlatformIO, né librerie DSP esterne. L'intera catena di segnale, dal demodulatore a correlazione passando per il recupero dei bit DPLL, NRZI, il framer HDLC, il codec AX.25 e la correzione d'errore in avanti Reed–Solomon FX.25, viene eseguita sul microcontrollore usando soltanto il SAR-ADC in modalità continua/DMA, il DAC e un timer general-purpose.

In una frase, il firmware **demodula** l'audio AFSK/FSK dall'altoparlante o dall'uscita discriminatore della radio, **decodifica** le trame HDLC/AX.25 (opzionalmente corrette con FX.25), le **instrada** verso APRS-IS via Wi-Fi, le **ripete (digipeat)** di nuovo in RF, **trasmette beacon** con la propria posizione, meteo e telemetria, **modula** e trasmette le trame attraverso il DAC a 8 bit dell'ESP32 — ed è configurato interamente tramite un pannello web servito dal dispositivo stesso. Nessuna console seriale, nessuna ricompilazione per le impostazioni ordinarie.

> 📖 **La documentazione completa ed esaustiva si trova su [esp32idf-aprs.readthedocs.io](https://esp32idf-aprs.readthedocs.io/)** — trilingue (English / Español / Italiano), con guide introduttive, cablaggio hardware, la catena di segnale DSP, il motore di configurazione, le rotte HTTP e la risoluzione dei problemi. **Questo README è solo una presentazione. Per qualsiasi cosa oltre a un primo sguardo, consultare la documentazione.**

---

## In evidenza

- **Soft-modem sul chip.** AFSK 1200 Bd Bell 202 (APRS standard) con demodulatore doppio, più AFSK 1200 Bd V.23, AFSK 300 Bd e **G3RUH 9600 Bd FSK** — tutto in C puro sull'ADC/DAC dell'ESP32 stesso.
- **Correzione d'errore FX.25.** FEC Reed–Solomon su AX.25, solo RX o RX+TX, per decodifiche affidabili in condizioni di segnale debole.
- **IGate APRS-IS completo.** Gating bidirezionale **RF→INET** e **INET→RF** con soppressione dei duplicati, costruzione `qAR`/`qAO`, filtraggio per tipo di payload, budlist di nominativi, un range gate locale (distanza haversine) e whitelist per prefisso. Si possono elencare fino a quattro server APRS-IS, con failover automatico tra quelli abilitati.
- **Digipeater.** Una tabella di alias n-N di quattro righe (WIDE1-1 / WIDE2-2 / WIDE#-2 di default), ogni riga con il proprio limite di hop e modalità trace/flood, più trappola per il conteggio hop, funzionamento solo fill-in e soppressione dei duplicati.
- **Beacon, messaggistica e chat.** Beacon a posizione fissa per tracker/igate/digi, messaggistica di testo APRS con ack/ritrasmissione (RF e/o INET) e un'interfaccia chat dei messaggi nel browser.
- **Meteo e telemetria.** Rapporti meteo APRS in onda con refresh dei sensori a 1 Hz e media per campo, più telemetria APRS (analogica A1–A5 + digitale B1–B8) con rapporti `T#nnn` e metadati.
- **Posta radio Winlink (APRSLink).** La stazione legge e scrive la propria posta `NOMINATIVO@winlink.org` attraverso il servizio `WLNK-1` — accesso a sfida/risposta senza che la password vada in aria, una sessione cadenzata di un comando per volta e un terminale nel browser — e, separatamente, inoltra attraverso il suo IGate la sessione Winlink propria di una stazione vicina in RF.
- **Oggetti, item e bollettini.** Fino a cinque Oggetti/Item APRS della stazione e cinque bollettini (BLN1–BLN5), ciascuno via RF e/o INET con controllo di scadenza/decadimento.
- **Framework di sensori a runtime.** Un registro di driver dinamico e auto-registrante (`sensors_local`) — include di serie un driver BME280/BMP280 (I²C), più uno opzionale per BMP180 sullo stesso bus.
- **Pannello web, 20 pagine.** Autenticazione HTTP Basic, una dashboard in tempo reale, un log del traffico in tempo reale e tabella degli ultimi ascoltati (long-poll JSON), gestione file LittleFS (upload/download/eliminazione/formattazione), Wi-Fi AP/STA/AP+STA con scansione e controllo della potenza di TX, controllo della frequenza della CPU (80/160/240 MHz), e un visore del registro di console su richiesta che copia l'uscita seriale nel browser.
- **Aggiornamenti OTA con auto-rollback.** Due slot applicativi `ota_0`/`ota_1`; un'immagine difettosa torna indietro automaticamente al boot successivo.
- **Interfaccia trilingue.** Inglese, spagnolo e italiano (a tempo di compilazione, una lingua per immagine).

---

## Matrice delle funzioni

| Area | Note |
|---|---|
| AFSK 1200 Bd Bell 202 | Demodulatore doppio, profilo predefinito |
| AFSK 1200 Bd V.23 · AFSK 300 Bd · G3RUH 9600 Bd FSK | Più profili di modem selezionabili |
| Trame UI HDLC / AX.25 RX + TX | Catena TX/RX completa del soft-modem |
| FX.25 (FEC Reed–Solomon su AX.25) | Modalità solo RX / RX+TX |
| Comando PTT | GPIO e polarità a compile-time, tempo minimo di rilascio |
| CSMA / time-slot di TX / preambolo TXDelay | `preamble`, `tx_timeslot` |
| Limitatore di duty cycle di TX | Tetto opzionale su una finestra scorrevole di 10 minuti |
| IGate APRS-IS RF→INET e INET→RF | Filtri, dedup, budlist, unwrap third-party opzionale |
| Failover multiserver APRS-IS | 4 slot server, ritentativo circolare sugli slot abilitati |
| Range gate e prefix gate locali | Distanza haversine + whitelist per prefisso di nominativo |
| Digipeater | Tabella di alias n-N configurabile (trace/flood), trappola per gli hop, soppressione duplicati |
| Oggetti / Item · Bollettini | Fino a 5 ciascuno, RF e/o INET, scadenza/decadimento |
| Posta radio Winlink (APRSLink) | Casella propria via `WLNK-1`, più gateway per le stazioni locali |
| Messaggistica + ack/ritrasmissione · Chat | RF e/o INET |
| Rapporto meteo | Refresh dei sensori a 1 Hz, media opzionale |
| Telemetria | Analogica A1–A5 + digitale B1–B8, `T#nnn` + metadati |
| Framework di driver per sensori | Registro dinamico, driver BME280/BMP280 incluso |
| Pannello web | 20 pagine, dashboard live, traffico + ultimi ascoltati, visore del registro di console |
| Archiviazione | LittleFS 512 KB, upload/download/eliminazione/formattazione |
| Rete | Wi-Fi AP/STA/AP+STA, scansione, potenza TX, SNTP (orologio UTC, fuso orario selezionabile per la visualizzazione) |
| Controllo frequenza CPU | 80 / 160 / 240 MHz |
| Aggiornamento OTA | Slot `ota_0`/`ota_1`, auto-rollback |
| Localizzazione | EN / ES / IT, a compile-time |

---

## Hardware

- **Target:** ESP32 (classico, Xtensa dual-core), 4 MB di flash. Il dual-core è **obbligatorio** — la ISR dell'ADC e il clock di campionamento del DAC sono assegnati di proposito a core diversi.
- **Ingresso audio (ADC):** predefinito `GPIO33` (ADC1). **Solo GPIO 32–39** — l'ADC2 è inutilizzabile con il Wi-Fi attivo.
- **Uscita audio (DAC):** predefinito `GPIO25`. **Solo GPIO 25 o 26** — il DAC dell'ESP32 è cablato a quei pad.
- **PTT:** predefinito `GPIO26`, polarità selezionabile a compile-time.
- **Nota:** ESP32-S3/C3/C6/H2 **non hanno il DAC** e non possono eseguire la catena di TX senza modifiche.

Il cablaggio della scheda (pin audio, pin/polarità PTT, frequenze di campionamento) è definito come costanti a compile-time nel `CMakeLists.txt` di primo livello. È incluso uno schema KiCad dell'interfaccia radio sotto `schematics/`.

> Le tabelle complete dei pin e i vincoli di cablaggio sono nel [capitolo Hardware della documentazione](https://esp32idf-aprs.readthedocs.io/en/latest/it/hardware.html).

---

## Avvio rapido

```bash
# Richiede ESP-IDF v6.x (testato e fissato a 6.0.2)
idf.py set-target esp32
idf.py build
idf.py -p PORTA flash monitor
```

Al primo avvio il dispositivo attiva un AP Wi-Fi; collegarsi e aprire il pannello web per configurare il proprio nominativo, la radio e i servizi. Dopo il flash iniziale via USB/UART, tutti gli aggiornamenti successivi possono essere effettuati dalla pagina **About / Firmware** del pannello web tramite OTA.

> 📖 La guida passo-passo al primo avvio è in [Getting Started](https://esp32idf-aprs.readthedocs.io/en/latest/it/getting-started.html).

---

## Documentazione

**Tutto è documentato per intero su 👉 [esp32idf-aprs.readthedocs.io](https://esp32idf-aprs.readthedocs.io/)**

La documentazione è trilingue ed è organizzata in *Funzionalità* (cosa fa la stazione), *Capacità* (proprietà trasversali), *Interni* (come è costruita) e una sezione di *Riferimento*:

- 🇬🇧 [English](https://esp32idf-aprs.readthedocs.io/en/latest/en/index.html)
- 🇪🇸 [Español](https://esp32idf-aprs.readthedocs.io/en/latest/es/index.html)
- 🇮🇹 [Italiano](https://esp32idf-aprs.readthedocs.io/en/latest/it/index.html)

Questo README è solo una presentazione — **si prega di consultare la documentazione per l'installazione, il cablaggio, la configurazione e gli interni.**

---

## Crediti e licenza

Creato da **Emiliano Augusto González (LU3VEA)**.

Costruito su idee di progetti precedenti — [VP-Digi](https://github.com/sq8vps/vp-digi), [ESP32APRS](https://github.com/nakhonthai/ESP32APRS_Audio) e [LibAPRS](https://github.com/markqvist/LibAPRS); fare riferimento ai loro autori per maggiori informazioni.

Rilasciato sotto la **GNU General Public License v3**. Vedere [LICENSE](LICENSE).
