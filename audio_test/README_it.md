# Banco di prova audio per esp32idf_APRS — manuale d'uso

🌐 [English](README.md) · [Español](README_es.md) · **Italiano**

Provare il ricevitore APRS AFSK a 1200 baud del firmware **esp32idf_APRS** con
**traffico APRS reale registrato via radio** e misurare quanti pacchetti decodifica
l'ESP32 rispetto a un decodificatore di riferimento (**multimon-ng**).

---

## Indice

1. [Cosa fa questo test](#1-cosa-fa-questo-test)
2. [Cosa serve](#2-cosa-serve)
3. [Costruire il cavo audio (hardware)](#3-costruire-il-cavo-audio-hardware)
4. [Preparare l'ESP32 (impostazioni del firmware)](#4-preparare-lesp32-impostazioni-del-firmware)
5. [Preparare il PC](#5-preparare-il-pc)
6. [Ottenere file audio con traffico APRS reale](#6-ottenere-file-audio-con-traffico-aprs-reale)
7. [Regolare il livello audio](#7-regolare-il-livello-audio) ([calibrazione automatica del volume](#71-calibrazione-automatica-del-volume))
8. [Prima esecuzione: verificare l'intera catena con pacchetti sintetici](#8-prima-esecuzione-verificare-lintera-catena-con-pacchetti-sintetici)
9. [Eseguire il test reale](#9-eseguire-il-test-reale)
10. [Riferimento della riga di comando](#10-riferimento-della-riga-di-comando)
11. [Leggere i risultati](#11-leggere-i-risultati)
12. [Come vengono confrontati i pacchetti](#12-come-vengono-confrontati-i-pacchetti)
13. [Piano di test suggerito con le tracce di WA8LMF](#13-piano-di-test-suggerito-con-le-tracce-di-wa8lmf)
14. [`gen_test_wav.py` in dettaglio](#14-gen_test_wavpy-in-dettaglio)
15. [Risoluzione dei problemi](#15-risoluzione-dei-problemi)
16. [Limitazioni](#16-limitazioni)
17. [Scheda di riferimento rapido](#17-scheda-di-riferimento-rapido)
18. [Glossario](#18-glossario)

---

## 1. Cosa fa questo test

Il firmware dell'ESP32 decodifica i pacchetti APRS a partire da un segnale audio
sul suo ingresso ADC e stampa ogni pacchetto decodificato sulla console seriale
(`RX: SRC>DST,PATH:payload`). La domanda è: **di tutti i pacchetti realmente
presenti nell'audio, quanti ne ha decodificati l'ESP32 e li ha decodificati
correttamente?**

Il programma risponde inviando **lo stesso audio a due decodificatori
contemporaneamente**:

```
                       ┌─► sox (ricampionamento a 22050 Hz, mono) ─► multimon-ng ───► pacchetti A  (riferimento)
                       │                                                                          │
 File WAV ─────────────┤                                                                          ▼
 (traffico APRS reale) │                                                                 test_aprs_wavs.py
                       │                                                                  confronta A e B
                       │                                                                          ▲
                       └─► play ─► scheda audio del PC ─► RV1 + C1 ─► ADC dell'ESP32 (GPIO33)     │
                                                                   │                              │
                                                        modem AFSK dell'ESP32                     │
                                                                   │                              │
                                                        log di console ─► seriale USB ────────────┴► pacchetti B
```

* **multimon-ng** è un noto decodificatore software. Ciò che decodifica dal file
  viene usato come *l'elenco dei pacchetti realmente presenti*.
* L'**ESP32** ascolta lo stesso audio attraverso la scheda audio e un piccolo
  circuito attenuatore/di accoppiamento, e riporta sulla porta seriale ciò che ha
  decodificato.
* Per ogni pacchetto decodificato da multimon-ng, il programma cerca lo stesso
  pacchetto nel log dell'ESP32. Stampa il pacchetto insieme a un verdetto:
  **OK**, **DIFFERENT** (diverso) o **NOT DECODED** (non decodificato).
* Alla fine stampa un riepilogo: pacchetti totali, percentuale decodificata
  correttamente, percentuale decodificata con contenuto diverso e percentuale
  mancante.

**Si può usare qualsiasi file WAV con traffico APRS reale (AFSK 1200 baud)**: una
registrazione da un ricetrasmettitore, da una SDR o una traccia di prova
pubblicata. La sezione 6 spiega dove trovare buoni file e propone le tracce di
prova gratuite di **WA8LMF**.

Il test è **solo in ricezione**: non si trasmette nulla e non occorre collegare
nulla dall'ESP32 verso il PC.

---

## 2. Cosa serve

### Hardware

| Elemento | Note |
|---|---|
| Scheda ESP32 con il firmware esp32idf_APRS | Alimentata e collegata al PC via USB (questo collegamento USB è anche la console seriale). |
| PC con uscita della scheda audio | Uscita cuffie o linea. Una scheda audio USB economica e dedicata è una buona idea (vedere la sezione 5). |
| Cavo audio, jack da 3,5 mm | Punta = canale sinistro, manicotto = massa. Va bene uno qualsiasi dei due canali: il programma invia lo stesso audio a entrambi. |
| RV1: trimmer multigiri da 2 kΩ | Regola il livello. Il multigiri consente una regolazione fine. **Facoltativo** — la sezione 3.1 propone un'alternativa con resistenze fisse se non si dispone di un trimmer. |
| C1: condensatore da 1 µF o più | Può essere elettrolitico (attenzione alla polarità!), da 10 V o più. |
| Fili di collegamento | |

### Software (Linux)

Il programma è scritto per **Linux con ALSA** e richiede:

| Software | A cosa serve | Installazione (Debian/Ubuntu) |
|---|---|---|
| Python 3.7+ | esegue il programma | di solito è già installato |
| pyserial | legge la console dell'ESP32 | `sudo apt install python3-serial` |
| multimon-ng | decodificatore di riferimento | `sudo apt install multimon-ng` |
| sox (con `play`) | riproduce il WAV e converte l'audio | `sudo apt install sox libsox-fmt-all` |
| alsa-utils | `aplay -l` per trovare la scheda audio | `sudo apt install alsa-utils` |

Installare tutto in una volta:

```bash
sudo apt install python3-serial multimon-ng sox libsox-fmt-all alsa-utils
```

(Se si preferisce pip: `pip install pyserial`, all'interno di un ambiente virtuale
sulle distribuzioni recenti.)

---

## 3. Costruire il cavo audio (hardware)

![Circuito minimo di ingresso audio](esp32_audio_input.png)

*L'immagine è `esp32_audio_input.png`, in questa stessa directory. Le sue etichette
sono in inglese: Tip = punta, Sleeve = manicotto, wiper = cursore, Ground = massa.*

### Cosa fa il circuito

* L'uscita della scheda audio del PC è un segnale che oscilla **sia sopra sia sotto
  0 V** (può arrivare a circa ±1 V o più). L'ADC dell'ESP32 accetta solo **da 0 a
  3,3 V** e non deve mai ricevere una tensione negativa.
* **RV1** (il trimmer) è un **controllo di volume / partitore di tensione** ai capi
  dell'uscita del PC. Il suo cursore preleva solo una frazione del segnale.
* **C1** (il condensatore) **blocca la componente continua** e lascia passare
  l'audio. Sul lato ESP32 va a GPIO33, il cui livello continuo è fissato dall'ESP32
  stesso.
* L'ESP32 può polarizzare da solo il proprio pin ADC: collega le **resistenze
  interne di pull-up e pull-down** del pad in modo che il pin a riposo si trovi a
  circa **1,65 V** (metà di 3,3 V). L'audio viaggia quindi sopra questo livello
  continuo. Questa è l'opzione del firmware **ADC input self-bias** (sezione 4).
  Per questo il circuito non ha bisogno di resistenze proprie.
* La **massa** del jack (manicotto) va a **GND** dell'ESP32.

### Collegamento, passo per passo

Spegnere prima tutto.

1. **Punta del jack** → estremo superiore di **RV1**.
2. **Manicotto del jack** → estremo inferiore di **RV1** **e** **GND** dell'ESP32.
3. **Cursore di RV1** (il terminale centrale) → **C1, lato meno (−)**.
4. **C1, lato più (+)** → **GPIO33** dell'ESP32.

### Polarità del condensatore (importante se C1 è elettrolitico)

Un condensatore elettrolitico ha una **polarità**. GPIO33 a riposo si trova a circa
1,65 V e il lato PC a circa 0 V, quindi il lato ESP32 è quello positivo:

| Terminale di C1 | Va a | Come riconoscerlo |
|---|---|---|
| **+** (più) | **GPIO33** (ESP32) | il terminale **più lungo** di un componente nuovo |
| **−** (meno) | **cursore di RV1** (lato PC) | il terminale sotto la **striscia con i segni meno** stampata sul corpo |

Nel disegno l'armatura dritta (contrassegnata con +) è rivolta verso GPIO33 e
l'armatura curva (contrassegnata con −) verso il cursore del trimmer. Collegarlo al
contrario provoca perdite e aggiunge rumore. Un condensatore ceramico o a film da
1 µF non ha polarità, se se ne dispone.

### Regole per i componenti

* **C1 deve essere da 1 µF o più.** Con un condensatore piccolo (per esempio
  100 nF) il circuito diventa un filtro passa-alto che comincia ad attenuare
  intorno a 700 Hz e indebolisce il tono a 1200 Hz, penalizzando la decodifica. Uno
  più grande va bene (da 1 µF a 10 µF).
* **Il trimmer deve stare *prima* del condensatore**, come nel disegno. Se RV1
  fosse collocato tra il condensatore e il pin, la sua gamba inferiore
  collegherebbe GPIO33 a massa e annullerebbe la polarizzazione.
* **RV1 = 2 kΩ** è un valore basso. Presenta un carico di 2 kΩ all'uscita del PC,
  che la maggior parte delle uscite di tipo cuffia sopporta senza problemi. Se il
  suono dell'uscita del PC si distorce, usare un trimmer da 10 kΩ (la
  documentazione del firmware raccomanda 10 kΩ soprattutto per mantenere leggero il
  carico sulla sorgente).
* **Il pin dell'ESP32 qui non ha diodi di protezione.** RV1 è l'unica cosa che
  limita ciò che arriva a GPIO33. Prima di collegare, impostare il volume del PC
  basso e RV1 vicino all'estremo di massa (sezione 7). Un'uscita cuffie a volume
  massimo può oscillare ben oltre i 0 – 3,3 V che il pin tollera.
* Lo schema di riferimento del firmware (l'interfaccia RX completa nella
  documentazione del progetto) aggiunge due **diodi di clamp, D1/D2**, che tengono
  il pin dell'ADC entro i rail di alimentazione, più un piccolo snubber R/C. Questo
  circuito minimo li omette di proposito: il firmware si affida allora all'opzione
  **Warn on receive over-range**. Se si intende lasciare il banco collegato a
  lungo, o si usa una sorgente forte, valutare l'aggiunta dei diodi di clamp come
  disegnati in quello schema.
* Solo **GPIO32 e GPIO33** possono usare l'autopolarizzazione integrata.
  L'ingresso predefinito del firmware è GPIO33; se nella vostra compilazione avete
  cambiato il pin dell'ADC, usare quel pin (deve essere GPIO32 o GPIO33 per questo
  circuito).

### 3.1 Alternativa: resistenze fisse al posto del trimmer

Se non si dispone di un trimmer da 2 kΩ, RV1 può essere sostituito con **due
resistenze fisse** collegate come un partitore di tensione permanente. Si perde
la possibilità di girare una manopola, ma la sezione 7.1 mostra come la
**calibrazione automatica del volume** del programma stesso compensi questo
via software.

![Alternativa con resistenze fisse a RV1](esp32_audio_input_fixed.png)

*L'immagine è `esp32_audio_input_fixed.png`, in questa stessa directory: lo stesso
circuito della sezione 3, con il trimmer sostituito dalla coppia fissa R1/R2. Le
sue etichette sono in inglese.*

Detto più semplicemente: **R1** va dalla **punta** del jack a un nodo
intermedio; **R2** va da quello stesso nodo intermedio al **manicotto** del
jack (massa, collegata a GND dell'ESP32). Il nodo intermedio — dove R1 e R2 si
incontrano — sostituisce il cursore del trimmer e va al **lato meno (−) di
C1**, esattamente come nei passi di collegamento sopra. Tutto il resto (C1, il
collegamento a GPIO33, la massa condivisa) resta invariato.

* **Valori suggeriti di partenza: R1 = 4,7 kΩ, R2 = 1 kΩ.** Questo divide
  l'uscita del PC di circa 5,7×, presentando un carico leggero di ≈5,7 kΩ. È
  solo un punto di partenza: il livello di uscita di linea varia molto tra le
  schede audio, quindi la tensione che arriva davvero a GPIO33 dipende ancora
  dal volume impostato sul PC.
* **Questo partitore è fisso — non si può regolare come un trimmer.** Usare il
  controllo del volume del PC per la regolazione grossolana (come nella sezione
  7) e lasciare che il guadagno via software `--volume` (applicato
  automaticamente dalla calibrazione automatica del volume descritta nella
  sezione 7.1, a meno di passare `--no_auto_volume`) si occupi della
  regolazione fine. È esattamente la situazione per cui esiste quella funzione
  di calibrazione.
* Se, anche con il volume del PC basso, il livello risulta sempre troppo alto
  (over-range) o sempre troppo basso (valori grezzi incollati a 0 o 4095, cosa
  che `--volume` da solo non può correggere), sostituire con un partitore con
  più o meno attenuazione — per esempio R1 = 10 kΩ / R2 = 1 kΩ (più
  attenuazione) o R1 = 2,2 kΩ / R2 = 1 kΩ (meno) — e ricontrollare con RX LEVEL
  (sezione 7) o con un'altra sessione di calibrazione automatica.
* Un trimmer resta la scelta più comoda se si prevede di riutilizzare il banco
  con schede audio o registrazioni diverse: permette di fissare il livello
  analogico una sola volta, in hardware, invece di dipendere ogni volta dal
  guadagno via software.

---

## 4. Preparare l'ESP32 (impostazioni del firmware)

Collegarsi al punto di accesso Wi-Fi dell'ESP32 (SSID di fabbrica `esp32idf_APRS`,
password `esp32idf_APRS`) e aprire `http://192.168.4.1/` in un browser (accesso di
fabbrica `admin` / `admin`, salvo modifiche). L'interfaccia può essere mostrata in
più lingue; qui sotto sono riportate le etichette in italiano.

### 4.1 Impostazioni audio — pagina *Radiomodem*

| Etichetta | Impostare a |
|---|---|
| Abilita modem audio ADC/DAC | **ON** (attivo) |
| Interfaccia audio → Polarizzazione interna dell'ingresso ADC | **ON** (è ciò che fa funzionare il circuito accoppiato con condensatore) |
| Interfaccia audio → Avvisa quando l'audio ricevuto esce dal fondo scala | **ON** (avvisa quando il segnale è troppo forte; il circuito non ha diodi di clamp) |

Salvare (riavviare l'ESP32 se la pagina lo richiede).

### 4.2 ⚠ Impostazioni di sicurezza — da fare PRIMA di riprodurre qualsiasi traffico registrato

Il traffico registrato via radio contiene **nominativi e posizioni reali**. Le
tracce di prova di WA8LMF contengono persino il suo nominativo nei beacon, ed egli
avverte esplicitamente che, se vengono inoltrate a un igate, generano falsi
rapporti su dove si trovava decenni fa. Il vostro ESP32 esegue un firmware da
**IGate/digipeater**, quindi assicurarsi che nulla di ciò che viene decodificato da
una registrazione possa uscire dall'ESP32:

| Pagina del menu | Etichetta | Impostare a |
|---|---|---|
| IGate | **Abilita IGate** | **OFF** (disattivato) |
| IGate | **RF verso Internet** | **OFF** |
| Digipeater | **Abilita Digipeater** | **OFF** |
| Pagine dei beacon | abilitazione di beacon / tracker / meteo / telemetria | **OFF** |

Sicurezza aggiuntiva, consigliata:

* Lasciare l'ESP32 nella sua modalità di fabbrica di **solo punto di accesso Wi-Fi**
  (nessuna connessione Station/client), in modo che **non abbia alcun percorso
  verso Internet** durante il test.
* **Non collegare un trasmettitore né un ricetrasmettitore** all'ESP32 durante il
  test.

Le righe di ricezione che servono sulla console (`RX: …`) vengono comunque stampate
con l'IGate disattivato: la documentazione afferma che il log di una stazione
solo-ricezione viene ristretto dai filtri, non svuotato. Se con l'IGate disattivato
non si vedono mai righe `RX:`, consultare la
[Risoluzione dei problemi](#15-risoluzione-dei-problemi).

### 4.3 Impostazioni del log

| Etichetta | Impostare a | Perché |
|---|---|---|---|
| Pagina IGate → **Registra dopo i filtri** | **OFF** (il valore predefinito) | Se è su ON, la console stampa solo i pacchetti che superano i filtri dell'IGate, e ogni pacchetto filtrato verrebbe contato come *NOT DECODED*. |

Il livello di log della console deve essere **INFO** (il predefinito del firmware),
che è il livello che stampa le righe `RX:`.

### 4.4 La console

La porta USB dell'ESP32 è anche la console seriale: **115200 baud, 8 bit di dati,
nessuna parità, 1 bit di stop (8N1)**. Solo un programma alla volta può usare la
porta seriale, quindi **chiudere** `idf.py monitor`, minicom, screen, PuTTY, ecc.
prima di eseguire il test.

---

## 5. Preparare il PC

### 5.1 Permesso sulla porta seriale

Su Debian/Ubuntu l'utente deve appartenere al gruppo `dialout`:

```bash
sudo usermod -aG dialout $USER
# poi uscire dalla sessione e rientrare
```

Trovare la porta dell'ESP32:

```bash
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
```

Il programma usa `/dev/ttyUSB0` come predefinita; usare `--serial_port` per
un'altra.

### 5.2 Trovare la scheda audio che alimenta l'ESP32

```bash
./test_aprs_wavs.py --list_audio      # come: aplay -l
```

Esempio di output:

```
card 0: PCH [HDA Intel PCH], device 0: ALC3246 Analog [ALC3246 Analog]
card 1: Device [USB Audio Device], device 0: USB Audio [USB Audio]
```

La scheda audio USB qui sopra è la **scheda 1, dispositivo 0**, che in ALSA si
chiama `hw:1,0`. Andrà passata come `--audio_device hw:1,0`. Senza
`--audio_device` viene usata l'uscita predefinita del sistema.

### 5.3 Consigli sulla scheda audio (influiscono sui risultati)

* **Usare una scheda audio USB dedicata** se possibile. Una economica va bene. Tiene
  i suoni del desktop fuori dal test.
* **Non lasciare suonare i suoni di sistema** durante un test: un avviso di
  notifica passa per la stessa uscita ed entra nell'ESP32, e può corrompere un
  pacchetto. Disattivare i suoni di notifica, oppure usare una scheda dedicata che
  il desktop non utilizzi.
* Disattivare qualsiasi **miglioramento audio**, equalizzatore, loudness o
  normalizzazione del volume nelle impostazioni audio.
* Impostare il volume del mixer **una sola volta** e non toccarlo durante
  un'esecuzione. Il livello audio all'ESP32 dipende da esso (sezione 7).
  `alsamixer` (scegliere la scheda audio con F6) è lo strumento abituale.
* Riprodurre solo audio **senza perdite** (WAV/FLAC). WA8LMF osserva che la
  compressione con perdita, come l'MP3, deforma le forme d'onda dei dati, e che i
  sistemi audio economici basati su software possono introdurre errori di
  temporizzazione o una frequenza di campionamento di riproduzione errata. Se i
  risultati sembrano strani, cambiare scheda audio è la prima cosa da provare.
* Non usare audio Bluetooth.

---

## 6. Ottenere file audio con traffico APRS reale

### 6.1 Quali file si possono usare

**Qualsiasi file WAV che contenga traffico APRS in AFSK a 1200 baud** (la normale
modulazione APRS in VHF, toni Bell 202 a 1200/2200 Hz). Il programma accetta
qualsiasi:

* frequenza di campionamento (8000, 11025, 22050, 44100, 48000 Hz …),
* mono o stereo,
* profondità in bit (8, 16, 24 bit).

`sox` converte automaticamente l'audio per multimon-ng (22050 Hz, mono, 16 bit).

Regole:

* Se il WAV è **stereo**, solo il suo **canale sinistro** viene inviato all'ESP32
  (il decodificatore di riferimento riceve la somma dei due). Convertire prima in
  mono le registrazioni stereo i cui due canali differiscono:
  `sox in.wav -c 1 mono.wav`.
* L'estensione deve essere `.wav` (sono accettate anche `.WAV` e `.Wav`). Il
  programma considera solo quei file **direttamente dentro** la directory indicata
  (non cerca nelle sottodirectory). FLAC, MP3, ecc. vanno prima convertiti
  (sezione 6.3).
* L'audio deve essere **senza perdite** e **senza clipping**.
* Il file può avere qualsiasi durata. I file vengono elaborati **uno dopo l'altro,
  in ordine alfabetico** (maiuscole e minuscole si ordinano insieme), e ciascuno viene riprodotto in **tempo reale** (un file
  di 25 minuti richiede 25 minuti).
* Audio migliore: prelevato direttamente dall'**uscita del discriminatore / "dati"**
  di un ricevitore. L'audio prelevato dall'**altoparlante** è de-enfatizzato;
  funziona anch'esso, ma la decodifica è un po' diversa. Provare entrambi è utile.

### 6.2 Fonte consigliata: le tracce di prova TNC di WA8LMF

**WA8LMF** pubblica su **<http://www.wa8lmf.net/TNCtest/>** un "TNC Test CD"
gratuito, creato per confrontare i TNC packet a 1200 baud "sotto tiro" in
condizioni reali. Contiene registrazioni reali via radio, il che lo rende un
insieme di prova ideale per questo banco.

**Sono offerte due versioni; entrambe hanno audio identico:**

| Versione | File | Dimensione | Formato |
|---|---|---|---|
| 2.0 (consigliata) | <http://www.wa8lmf.net/TNCtest/TNC_Test_2.iso> | ≈ 250 MB | Immagine di CD-ROM con file **FLAC** senza perdite |
| 1.1 | <http://www.wa8lmf.net/TNCtest/TNC_Test_CD_Ver-1.1.zip> | ≈ 530 MB | Immagine di CD audio (BIN/CUE): va masterizzata o estratta |

Usare la **versione 2.0**: l'audio è già in file.

**Le tracce** (come descritte in quella pagina):

| Traccia | Cos'è | Durata | Pacchetti | Da usare qui? |
|---|---|---|---|---|
| **1** | Traffico reale su **144,39 MHz, Los Angeles**, nell'ora di punta del pomeriggio: il canale è saturo. 40 minuti di attività con le pause eliminate, compressi in ≈ 25 minuti. Audio del discriminatore, **senza** de-enfasi. Contiene segnali sovra- e sottodeviati, collisioni, pacchetti consecutivi quasi senza pausa, tracker con NMEA grezzo, TinyTrak, identificazioni in CW dentro pacchetti … | ≈ 25 min | molti (centinaia) | **Sì — la prova di stress principale** |
| **2** | Stesso contenuto della traccia 3 ma **de-enfatizzato** (simula l'audio prelevato dall'altoparlante / dal controllo di volume di un ricevitore). | ≈ 5 min | 100 | **Sì** — confrontarla con la traccia 3 |
| **3** | Un rapporto di posizione **Mic-E di un Kenwood D700**, pulito, da un monitor di servizio, copiato 100 volte: **20 burst al minuto per 5 minuti = esattamente 100 pacchetti identici**. | ≈ 5 min | esattamente **100** | **Sì — dà una percentuale esatta** |
| **4** | 25 minuti di **un D700 mobile che emette un beacon ogni 12 s** durante la guida, su un canale tranquillo, a 8–10 miglia dal ricevitore: flutter, multipath, segnali deboli. La pagina dice che diversi pacchetti si sentono ma non vengono decodificati con il motore packet AGW. | ≈ 25 min | fino a ≈ 125 inviati | **Sì — prova di segnale debole** |
| 5, 6, 7 | Toni alternati 1200/2200 Hz della modalità "CAL" di un KPC3+: piatti, de-enfatizzati, pre-enfatizzati. Servono per *allineare* i TNC (rapporto tra i livelli dei toni). | ≈ 1 min ciascuna | **0** | **No** — non contengono pacchetti. Tenerle fuori dalla directory di test. |

Sempre secondo la pagina: ogni traccia inizia e finisce con toni DTMF di
riferimento (la traccia 1 inizia con "1" e finisce con "6", la traccia 2 con
"2"/"7", e così via). Sono innocui.

> **⚠ Avvertenza di WA8LMF:** *non riprodurre queste tracce via radio.* Il suo
> nominativo è incorporato nei beacon e verrebbero inoltrate a un igate,
> generando falsi rapporti di posizione. **Questo banco non trasmette mai**, ma
> mantenere disattivato l'IGate dell'ESP32 come descritto nella sezione 4.2,
> esattamente per lo stesso motivo.

Queste tracce sono materiale di terzi pubblicato da WA8LMF a scopo di prova; non
sono distribuite con questo banco. Citare la fonte se si pubblicano i risultati.

### 6.3 Scaricare, estrarre e convertire (versione 2.0)

```bash
mkdir -p ~/audio_test && cd ~/audio_test

# 1) scaricare (≈ 250 MB)
wget http://www.wa8lmf.net/TNCtest/TNC_Test_2.iso

# 2) aprire l'ISO — oppure montarla (richiede sudo) ...
mkdir -p TNC_Test_2
sudo mount -o loop,ro TNC_Test_2.iso TNC_Test_2
#    ... oppure estrarla senza root:  sudo apt install p7zip-full
# 7z x TNC_Test_2.iso -oTNC_Test_2

# 3) vedere i file FLAC
find TNC_Test_2 -iname '*.flac'
```

I file FLAC nell'ISO prendono il nome dalle tracce e iniziano con il numero di
traccia, quindi il comando `find` qui sopra ne mostra i nomi reali (il primo è la
traccia di 25 minuti di traffico di Los Angeles). Poi convertirli tutti in WAV:

```bash
mkdir -p Audio-Tracks
find TNC_Test_2 -iname '*.flac' | while IFS= read -r f; do
    sox "$f" "Audio-Tracks/$(basename "$f" .flac).wav"
done
ls -l Audio-Tracks
```

Alternative a `sox`: `ffmpeg -i in.flac out.wav` oppure `flac -d in.flac`.

Smontare al termine: `sudo umount TNC_Test_2`.

Poiché i file vengono elaborati in ordine alfabetico e iniziano con il numero di
traccia, vengono eseguiti nell'ordine delle tracce. **Spostare fuori le tracce 5, 6
e 7** da `Audio-Tracks/` (per esempio in `Audio-Tracks/skip/`, che il programma
ignora): non hanno pacchetti.

### 6.4 Registrare il proprio traffico (facoltativo)

Va bene qualsiasi registrazione di traffico APRS reale. Due modi comuni:

* **Da un ricetrasmettitore:** collegare l'uscita del discriminatore/dati della
  radio (oppure l'uscita dell'altoparlante attraverso un attenuatore) all'ingresso
  della scheda audio del PC e registrare con Audacity: **mono, 22050 o 44100 Hz, 16
  bit**, con il livello d'ingresso regolato in modo che i picchi restino ben al di
  sotto del massimo (nessun clipping) e qualsiasi controllo automatico del
  guadagno / riduzione del rumore **disattivato**. Esportare come WAV.
* **Da una RTL-SDR** (non provato in questo progetto — verificare le opzioni della
  propria versione di `rtl_fm`). Usare la frequenza APRS della **propria regione**
  (144,390 MHz in Nord America; in altre regioni è diversa):

  ```bash
  rtl_fm -M fm -f 144.390M -s 22050 - | sox -t raw -r 22050 -e signed -b 16 -c 1 - capture.wav
  ```

Registrare sessioni lunghe (un'ora o più) negli orari di molto traffico; più
pacchetti ci sono, più significative sono le percentuali.

---

## 7. Regolare il livello audio

L'ESP32 ha bisogno della giusta quantità di segnale. **Troppo poco** e non riesce a
decodificare; **troppo** e il segnale va in clipping (e l'ingresso non ha diodi di
protezione).

**Prima di collegare il cavo:** volume del PC basso (circa il 30 %) e RV1 ruotato
completamente verso l'**estremo di massa** (segnale minimo).

Serve un secondo dispositivo sulla pagina web dell'ESP32 mentre l'audio suona: uno
smartphone o lo stesso PC, collegati al punto di accesso dell'ESP32 (sezione 4). La
pagina web funziona contemporaneamente al collegamento seriale USB.

**Passo per passo**

1. Aprire la pagina **Radiomodem** dell'ESP32. Accanto al pulsante **TEST LOOP**
   (interfaccia in inglese: **LOOP TEST**) si trova **LIVELLO RX** (in inglese:
   **RX LEVEL**). **Misura senza trasmettere**.
2. Riprodurre una registrazione reale **in modo continuo**. Usarne una molto
   attiva, come la traccia 1 di WA8LMF (i suoi pacchetti sono quasi consecutivi,
   quindi il livello è stabile). Si può usare lo stesso programma di test (i
   verdetti ora non contano; fermarlo con Ctrl-C a fine taratura):
   ```bash
   ./test_aprs_wavs.py --wav_dir one1 --audio_device hw:1,0
   ```
   oppure un semplice lettore:
   ```bash
   AUDIODRIVER=alsa AUDIODEV=hw:1,0 play -q Audio-Tracks/01_*.wav remix 1 1
   ```
   Non avete ancora una registrazione reale? Riprodurre in ciclo il file sintetico
   (sezione 8) per un po' (il `repeat 50` di sox lo riproduce 50 volte in più,
   circa 5 minuti in totale):
   ```bash
   AUDIODRIVER=alsa AUDIODEV=hw:1,0 play -q Synthetic/sample.wav remix 1 1 repeat 50
   ```
3. Mentre suona, premere **LIVELLO RX**. Il pulsante prima **salva la pagina così
   come appare** (in modo che il modem usi le impostazioni a schermo), poi osserva
   il ricevitore per **1 secondo** e mostra il risultato in verde accanto ai
   pulsanti, per esempio:
   ```
   312 mV RMS (peak 640), DC 1650 mV, AGC 1.00x, raw 1810..2390, DCD no
   ```
   È un'istantanea di un secondo, quindi premerlo **più volte**; con audio a burst
   una lettura può cadere in una pausa tra i pacchetti. Usare i valori più alti e
   stabili.
4. Alzare RV1 a piccoli passi (qualche giro del trimmer multigiri), premendo
   **LIVELLO RX** dopo ciascuno, finché non sono soddisfatti tutti e tre gli
   obiettivi:

   | Lettura | Obiettivo |
   |---|---|
   | Livello RX (`mV RMS`) | **250 – 350 mV RMS** |
   | intervallo grezzo (`raw min..max`) | comodamente **lontano da 0 e 4095** (gli estremi dell'ADC a 12 bit) |
   | livello continuo (`DC … mV`) | **1200 – 2000 mV** (conferma che l'autopolarizzazione funziona; circa 1650 mV è l'ideale) |

5. **Non toccare il volume del PC né RV1** per il resto della sessione. Se si
   cambia la scheda audio, il volume, o si riproduce una registrazione con un
   livello molto diverso (per esempio la traccia 2, de-enfatizzata), ricontrollare.

Se compare l'**avviso di fuori scala**, il livello è troppo alto: abbassare RV1 (o il
volume del PC). Se il livello continuo è vicino a 0 mV o a 3300 mV,
l'autopolarizzazione non è attiva (sezione 4.1) oppure C1 è collegato al contrario
/ manca.

Il programma ha anche `--volume`, un guadagno software solo per il ramo verso
l'ESP32 (predefinito 1.0). Preferire RV1 per la regolazione principale e usare
`--volume` per piccole correzioni (per esempio `--volume 0.8`); valori superiori a
1.0 possono andare in clipping.

### 7.1 Calibrazione automatica del volume

`--volume` non è solo un numero fisso da impostare una volta: a meno di passare
`--no_auto_volume`, **ogni esecuzione reale del test inizia con una ricerca
automatica del miglior guadagno via software**, prima ancora di riprodurre i
pacchetti che finiranno nel report. Questo è attivo per impostazione
predefinita, quindi avviene che lo si sia chiesto o no — è bene saperlo, perché
aggiunge tempo prima del test che si voleva davvero vedere:

* Riproduce l'insieme dei WAV (tornando al primo file se necessario) in lotti
  brevi di `--auto_volume_batch` pacchetti (predefinito 50, contando sia i
  pacchetti di multimon-ng sia quelli "extra" decodificati solo dall'ESP32),
  partendo da `--volume` (predefinito 1.0).
* Dopo ogni lotto controlla l'avviso di **fuori scala** dell'ESP32 (lo stesso
  descritto nella sezione 4.1) e la percentuale decodificata — i pacchetti a cui
  l'ESP32 ha risposto, contando allo stesso modo gli **OK** e i **DIFFERENT**, sul
  totale di multimon-ng di quel tentativo:
  * **Fuori scala in qualche momento** → il livello era troppo alto in quel
    tentativo; il volume viene abbassato del 15% per il tentativo successivo, e
    quel tentativo non può essere ricordato come il migliore.
  * **Nessun fuori scala** → la percentuale decodificata viene confrontata con
    quella del tentativo precedente; il volume viene alzato del 15% per il
    tentativo successivo, e questo diventa il nuovo migliore se ha superato
    tutti i precedenti.
* Continua a spendere l'intero budget di tentativi — fino a
  `--auto_volume_max_rounds` (predefinito 10) — anche dopo aver raggiunto il
  100% o dopo che la percentuale smette di muoversi tra due tentativi, così un
  volume ancora migliore più avanti nella ricerca non viene mai perso solo
  perché uno precedente sembrava già buono.
* Il volume che ha ottenuto la percentuale decodificata più alta **tra i
  tentativi che non sono andati in clipping** è quello usato per il test reale
  che segue, il quale riparte sempre dal primo file.
* Il volume resta entro **0,05 – 8,0** e cambia solo tra un tentativo e l'altro:
  ogni tentativo viene riprodotto sempre con un guadagno fisso.
* Se **tutti** i tentativi hanno segnalato fuori scala, nessuno è idoneo e
  l'esecuzione ricade sul `--volume` iniziale. Abbassare RV1 (o il volume del PC)
  ed eseguire di nuovo.
* Se un tentativo non decodifica nulla con multimon-ng, la ricerca si ferma lì e
  lo segnala: è un problema di file o di instradamento dell'audio, non di livello.
* Interrompere la calibrazione con **Ctrl-C** non ferma il programma: si passa al
  test reale con il volume del tentativo in corso in quel momento — che non è
  necessariamente quello col punteggio migliore, quindi leggere il valore stampato
  nel riepilogo finale prima di citare il risultato.

Il volume scelto viene stampato al termine di ogni tentativo, e di nuovo alla
fine del riepilogo finale come `Playback volume used for this test`. Annotare
quel numero insieme al livello impostato su RV1: se si stanno confrontando
esecuzioni nel tempo (sezione 13) e si vuole che la catena audio sia identica
tra loro, passare lo stesso valore con `--volume X --no_auto_volume` invece di
ricalibrare ogni volta.

Anche `--no_play` (la prova a vuoto) salta la calibrazione: non tocca mai la
scheda audio.

Questo è anche ciò che rende praticabile l'alternativa con resistenze fisse
della sezione 3.1: senza un trimmer da girare, il guadagno via software della
calibrazione è ciò che assorbe la differenza tra schede audio e impostazioni di
volume del PC.

---

## 8. Prima esecuzione: verificare l'intera catena con pacchetti sintetici

Prima di dedicare 25 minuti a una registrazione reale, verificare che tutto
funzioni (cavo, livello, impostazioni dell'ESP32, porta seriale) con un piccolo
file di prova **pulito**. Il `gen_test_wav.py` incluso costruisce un file AFSK 1200
perfetto con 3 pacchetti noti:

```bash
mkdir -p Synthetic
python3 gen_test_wav.py Synthetic/sample.wav
```

### 8.1 Prova a vuoto — non serve hardware

Questa usa solo multimon-ng e conferma che la parte software funziona:

```bash
./test_aprs_wavs.py --wav_dir Synthetic --no_play
```

Risultato atteso: multimon-ng elenca i 3 pacchetti e il programma termina con
`DRY RUN finished: multimon-ng decoded 3 packet(s) in 1 file(s).` In questa
modalità non vengono usati né la porta seriale né la scheda audio, e la
calibrazione automatica del volume viene saltata. I pacchetti sono elencati come
righe `000001 [multimon …]`, senza verdetto, perché non c'è una risposta
dell'ESP32 con cui confrontarli. Il codice di uscita è 0 se è stato decodificato
almeno un pacchetto, 2 se nessuno.

pyserial, multimon-ng, sox e `play` devono comunque essere installati: il
programma importa pyserial e controlla i tre programmi prima di guardare
`--no_play`.

### 8.2 Esecuzione completa con l'ESP32

Con il cavo collegato e il livello regolato (sezione 7):

```bash
./test_aprs_wavs.py --wav_dir Synthetic --serial_port /dev/ttyUSB0 --audio_device hw:1,0
```

Una catena funzionante mostra i tre pacchetti, ciascuno con l'etichetta **OK**:

```
[1/1] sample.wav  (5.5 s)
000001 [multimon 00:01.1] N0CALL-9>APRS-0,WIDE1-1,WIDE2-1:!4903.50N/07201.75W-Test one
    OK [esp32    00:01.3] N0CALL-9>APRS,WIDE1-1,WIDE2-1:!4903.50N/07201.75W-Test one
000002 [multimon 00:02.8] LU1ABC-0>APDW17-0,WIDE1-1:=3450.12S/05812.34W>Movil en ruta
    OK [esp32    00:03.0] LU1ABC>APDW17,WIDE1-1*:=3450.12S/05812.34W>Movil en ruta
000003 [multimon 00:04.4] EA4XYZ-7>APRS-0::LU1ABC   :Hola que tal{12
    OK [esp32    00:04.6] EA4XYZ-7>APRS::LU1ABC   :Hola que tal{12
  multimon-ng decoded 3 packet(s)
  ESP32 decoded 3 packet(s)
  -> OK: 3   DIFFERENT: 0   NOT DECODED: 0   EXTRA(esp only): 0
```

e un riepilogo finale con `Decoded correctly : 3 (100.00%)`. (I tempi possono
differire di qualche decimo di secondo sul vostro sistema.)

Non ci si deve aspettare che le due righe di una coppia siano identiche:
multimon-ng scrive `-0` quando manca l'SSID e non scrive mai il `*` di
digipetizione, mentre il firmware fa il contrario. La sezione 12.1 elenca quali
differenze vengono normalizzate prima del confronto.

Se risultano **NOT DECODED**, il problema è nella catena, non nel decodificatore:
passare alla sezione 15 (risoluzione dei problemi). **Non proseguire con traffico
reale finché questo non funziona.**

---

## 9. Eseguire il test reale

Lista di controllo preliminare:

- [ ] IGate, RF verso Internet, Digipeater e beacon sono su **OFF** (sezione 4.2)
- [ ] Registra dopo i filtri è su **OFF**; il modem audio, l'autopolarizzazione e l'avviso di fuori scala sono su **ON**
- [ ] Nessun altro programma usa la porta seriale
- [ ] Nessun suono di sistema può suonare sulla scheda audio
- [ ] Livello regolato con LIVELLO RX (sezione 7)
- [ ] Il file sintetico della sezione 8 ha dato **OK** su tutti i pacchetti
- [ ] Le tracce 5–7 non sono nella directory di test

Eseguire, salvando nello stesso tempo un file di log:

```bash
./test_aprs_wavs.py --wav_dir Audio-Tracks --serial_port /dev/ttyUSB0 --audio_device hw:1,0 \
    2>&1 | tee results_$(date +%Y-%m-%d_%H%M).log
```

Senza opzioni, il programma usa i file WAV della **directory corrente** e
`/dev/ttyUSB0`:

```bash
cd Audio-Tracks
../test_aprs_wavs.py
```

### Cosa succede durante un'esecuzione

1. Il programma elenca i file WAV trovati e apre la porta seriale (115200 8N1).
2. **L'apertura della porta di norma riavvia l'ESP32** (il chip USB-seriale
   attiva DTR/RTS). Il programma attende `--settle` secondi (4 per impostazione
   predefinita) che si avvii.
3. A meno che sia stato passato `--no_auto_volume`, esegue quindi la
   **calibrazione automatica del volume** della sezione 7.1: diverse brevi
   passate sull'insieme dei WAV per trovare il miglior guadagno via software,
   stampate man mano che avvengono. Questo aggiunge tempo prima che inizi il
   test che viene riportato; saltarla con `--no_auto_volume` se si conosce già
   il volume desiderato.
4. Per ogni file, nello stesso istante:
   * **riproduce** il WAV verso la scheda audio → ESP32 (in tempo reale), e
   * invia lo stesso audio a **multimon-ng**, anch'esso al ritmo del tempo reale,
     in modo che i pacchetti dei due decodificatori compaiano affiancati, e
   * **legge la console dell'ESP32** cercando righe `RX:`.
5. Ogni pacchetto di multimon-ng viene stampato con il suo verdetto non appena è
   noto (sezione 11).
6. Ogni 30 secondi viene stampata una **riga di avanzamento**, così i file lunghi
   non sembrano mai bloccati.
7. Terminato l'audio, il programma attende alcuni secondi in modo che gli ultimi
   pacchetti abbiano la stessa opportunità degli altri, stampa i conteggi del file,
   fa una pausa e continua con il successivo.
8. Alla fine stampa il **riepilogo** di tutti i file.

Tempo totale ≈ la passata di calibrazione automatica del volume (evitabile con
`--no_auto_volume`) + la somma delle durate dei file + circa 6 s per file (+ 4 s
all'inizio). Per le tracce 1–4 del set di WA8LMF, senza calibrazione, sono circa
**un'ora**.

**Ctrl-C** ferma il test in sicurezza: viene conservato tutto ciò che ha già un
verdetto e viene stampato il riepilogo. I pacchetti che attendevano ancora il
verdetto (l'ESP32 non ha avuto la sua intera finestra `--match_window` per
rispondere) vengono scartati silenziosamente: non vengono stampati né contati in
alcun modo.

---

## 10. Riferimento della riga di comando

```
./test_aprs_wavs.py [opzioni]
```

| Opzione | Predefinito | Significato |
|---|---|---|
| `--wav_dir DIR` | directory corrente | Directory con i file `.wav` (non ricorsiva). |
| `--serial_port PORT` | `/dev/ttyUSB0` | Porta seriale della console dell'ESP32. |
| `--baud N` | `115200` | Velocità seriale (l'8N1 è fisso). |
| `--audio_device DEV` | predefinito del sistema | Dispositivo ALSA collegato all'ESP32, ad es. `hw:1,0` (vedere `--list_audio`). |
| `--volume X` | `1.0` | Guadagno software applicato solo all'audio inviato all'ESP32. È anche il punto di partenza della calibrazione automatica del volume (vedi sotto), a meno che sia passato `--no_auto_volume`. Durante la calibrazione resta entro 0,05 – 8,0. |
| `--no_auto_volume` | disattivato | Salta la passata di calibrazione automatica del volume (sezione 7.1) e usa `--volume` così com'è per l'intera esecuzione. |
| `--auto_volume_batch N` | `50` | Pacchetti per tentativo durante la calibrazione automatica del volume (conta insieme i pacchetti di multimon-ng e quelli "extra" solo dell'ESP32). |
| `--auto_volume_max_rounds N` | `10` | Numero di tentativi usati per cercare il miglior volume prima del test reale. |
| `--match_window S` | `5` | Un pacchetto dell'ESP32 risponde a un pacchetto di multimon-ng solo se arriva entro ±S secondi da esso. Un pacchetto che l'ESP32 non ha riportato dopo S secondi è **NOT DECODED**. Vedere le sezioni 12 e 13. |
| `--tail S` | `3` | Secondi di ascolto dopo la fine dell'audio. Il programma attende sempre almeno `--match_window` secondi. |
| `--settle S` | `4` | Secondi di attesa dopo l'apertura della porta seriale (riavvio/avvio dell'ESP32). Aumentare se l'ESP32 si avvia lentamente. |
| `--pause S` | `1` | Pausa tra i file. |
| `--no_play` | disattivato | **Prova a vuoto:** niente suono e niente porta seriale; gira solo multimon-ng. Salta anche la calibrazione automatica del volume. I pacchetti sono elencati come righe `000001 [multimon …]`, senza verdetto. |
| `--mm_args "…"` | nessuno | Argomenti aggiuntivi per multimon-ng, tra virgolette, ad es. `--mm_args "-A"` (raramente necessari). |
| `--list_audio` | — | Stampa i dispositivi di riproduzione ALSA (`aplay -l`) ed esce. |
| `-h`, `--help` | — | Mostra la guida integrata. |

**Codice di uscita** (utile negli script):

| Codice | Significato |
|---|---|
| `0` | Ogni pacchetto di multimon-ng è stato decodificato correttamente dall'ESP32. |
| `1` | Almeno un pacchetto era DIFFERENT o NOT DECODED. (Con traffico reale e affollato è il risultato normale; leggere le percentuali.) |
| `2` | Problema di configurazione (manca un programma, nessun file WAV, la porta non si apre) o multimon-ng non ha decodificato alcun pacchetto. |

Esempi:

```bash
# tutti i WAV della directory corrente, porta predefinita
./test_aprs_wavs.py

# una directory, un'altra porta seriale e una scheda audio USB
./test_aprs_wavs.py --wav_dir ./Audio-Tracks --serial_port /dev/ttyUSB1 --audio_device hw:1,0

# un solo file: metterlo in una directory a parte
mkdir one && cp Audio-Tracks/03_*.wav one/
./test_aprs_wavs.py --wav_dir one --audio_device hw:1,0

# traccia 3 (pacchetti identici ogni 3 s): finestra più stretta
./test_aprs_wavs.py --wav_dir one --audio_device hw:1,0 --match_window 1.5

# solo verifica del software, senza hardware
./test_aprs_wavs.py --wav_dir Audio-Tracks --no_play

# riutilizzare un volume già noto, senza passata di calibrazione
./test_aprs_wavs.py --wav_dir Audio-Tracks --audio_device hw:1,0 --volume 0.85 --no_auto_volume

# far cercare più a fondo la calibrazione (più tentativi, lotti più grandi) su un set ampio
./test_aprs_wavs.py --wav_dir Audio-Tracks --audio_device hw:1,0 --auto_volume_max_rounds 15 --auto_volume_batch 80

# salvare il risultato, poi elencare solo i problemi
./test_aprs_wavs.py --wav_dir Audio-Tracks --audio_device hw:1,0 2>&1 | tee run.log
grep -E "NOT DECODED|DIFFERENT" run.log
```

---

## 11. Leggere i risultati

### 11.1 Righe dei pacchetti

Ogni pacchetto di **multimon-ng** viene stampato, nell'ordine in cui è stato udito,
seguito immediatamente dalla riga propria dell'ESP32 per quel pacchetto (se
esiste) e da un verdetto:

```
000012 [multimon 03:41.2] LU1ABC-0>APDW17-0,WIDE1-1:=3450.12S/05812.34W>Movil
    OK [esp32    03:41.4] LU1ABC>APDW17,WIDE1-1:=3450.12S/05812.34W>Movil
000013 [multimon 03:52.0] LU2XYZ-0>APRS-0:>some status
       [esp32     --:--.-] NOT DECODED
000014 [multimon 04:10.5] LU3AAA-0>APRS-0:>hello
       [esp32    04:11.0] LU3AAA>APRS:>hellX
      ! DECODED BUT DIFFERENT
000015 [multimon  --:--.-] NOT DECODED
       [esp32 only      04:20.1] LU9ZZZ>APRS:>heard only by the ESP32
```

* `000012` — un **contatore di stampa** a sei cifre. Avanza di uno per ogni
  pacchetto stampato, nell'ordine in cui i verdetti diventano noti, e numera anche
  i pacchetti EXTRA, quindi non è il conteggio dei pacchetti di multimon-ng.
  Riparte da `000001` a ogni file.
* `03:41.2` — minuti:secondi nel file in cui multimon-ng lo ha decodificato.
* Il testo è il pacchetto in **formato TNC2**: `ORIGINE>DESTINAZIONE,PERCORSO:contenuto`.
  multimon-ng scrive `-0` dopo i nominativi senza SSID (`LU1ABC-0`) e non scrive mai
  il `*` di digipetizione; il firmware fa l'opposto. Quelle differenze vengono
  normalizzate prima del confronto (sezione 12.1), perciò le due righe di una
  coppia **OK** spesso appaiono leggermente diverse.
* L'**OK** viene stampato *all'inizio della riga dell'ESP32*; gli altri due
  verdetti sono stampati su una riga propria sotto la coppia.
* Anche un pacchetto decodificato **solo dall'ESP32** viene stampato come coppia,
  ma al contrario: prima una riga `[multimon  --:--.-] NOT DECODED` (è multimon-ng
  ad averlo perso), poi la riga dell'ESP32 come `[esp32 only ...]`. La parola EXTRA
  non compare in queste righe dal vivo — quei pacchetti sono contati come
  `EXTRA(esp only)` nei conteggi per file e nel riepilogo finale.

| Verdetto | Significato |
|---|---|
| **OK** | L'ESP32 ha decodificato lo stesso pacchetto (stessi origine, destinazione, percorso e contenuto), vicino nel tempo. |
| **NOT DECODED** | L'ESP32 non lo ha riportato. È il caso "mancante". |
| **! DECODED BUT DIFFERENT** | L'ESP32 ha decodificato un pacchetto con la stessa origine/destinazione/percorso ma un **contenuto diverso**: decodificato, ma non correttamente. |

Tempi: un **OK** compare subito (di solito entro uno o due secondi). **NOT DECODED**
e **! DECODED BUT DIFFERENT** compaiono circa `--match_window` secondi (5 s per
impostazione predefinita) dopo il pacchetto, perché l'ESP32 potrebbe essere sul
punto di riportarlo.

La riga propria dell'ESP32 e i pacchetti EXTRA sono sempre mostrati — non serve
alcuna opzione per vederli.

### 11.2 Riga di avanzamento

```
       [progress 04:00.0 / 25:49.3] multimon=13  ok=12  not-decoded=1  different=0  (serial lines seen: 240)
```

Tempo riprodotto / durata del file, e i conteggi correnti. **`serial lines seen`**
deve continuare a crescere: dimostra che il collegamento seriale è attivo. Se
`multimon` cresce ma `ok` resta a 0 e `not-decoded` cresce, l'ESP32 non sta
ascoltando l'audio (sezione 15).

### 11.3 Conteggi per file

```
  multimon-ng decoded 14 packet(s)
  ESP32 decoded 13 packet(s)
  -> OK: 12   DIFFERENT: 1   NOT DECODED: 1   EXTRA(esp only): 0
    ! DIFFERENT
        multimon: LU3AAA-0>APRS-0:>hello
        esp32   : LU3AAA>APRS:>hellX
    ! NOT DECODED by ESP32: LU2XYZ-0>APRS-0:>some status
```

Dopo i conteggi, ogni pacchetto DIFFERENT e NOT DECODED di quel file viene
elencato di nuovo, così i problemi di una registrazione lunga si leggono tutti
insieme invece di doverli cercare tra le righe dal vivo.

### 11.4 Riepilogo finale

```
========================================================================
SUMMARY
========================================================================
  file                                 mm     ok   diff  n/dec  extra
  01_40-Mins-Traffic-on-144.39.wav    412    371      2     39      6
  03_D700-Mic-E-100-bursts.wav        100     97      0      3      0
  ----------------------------------------------------------------------
  Playback volume used for this test : 1.150
  Files tested                      : 2
  Total packets (multimon-ng)       : 512
  Packets seen by ESP32             : 476
  Decoded correctly                 : 468  (91.41%)
  Decoded with different content    : 2  (0.39%)
  Missing (not decoded)             : 42  (8.20%)
  Extra (ESP32 only, not an error)  : 6
```

*(i numeri qui sopra sono solo un'illustrazione del formato)*

Come si definisce ciascun valore:

| Valore | Definizione |
|---|---|
| **Total packets** | pacchetti decodificati da multimon-ng (il riferimento) |
| **Decoded correctly** | numero di OK e la sua percentuale sul totale |
| **Decoded with different content** | numero di DIFFERENT e la sua percentuale |
| **Missing (not decoded)** | numero di NOT DECODED e la sua percentuale |
| **Extra** | pacchetti decodificati solo dall'ESP32. **Non** rientrano nelle percentuali. |

Le tre percentuali sommano 100 %.

`Playback volume used for this test` è il guadagno via software (sezione 7.1)
usato realmente per riprodurre tutti i file di questa esecuzione: il valore
deciso dalla calibrazione automatica del volume, oppure `--volume` invariato se
è stato passato `--no_auto_volume`. Annotarlo insieme agli altri dettagli
dell'esecuzione se si prevede di confrontare i log più avanti.

### 11.5 Come interpretarli

**Importante:** multimon-ng è un *riferimento*, non la verità. Nessuno dei due
decodificatori è perfetto. Su un canale affollato (traccia 1 di WA8LMF) alcuni
pacchetti vengono decodificati da uno e non dall'altro. Quindi:

* Una percentuale alta di **OK** è buona. Non esiste un valore di superamento
  ufficiale: ciò che conta è **confrontare le esecuzioni** — gli stessi file prima
  e dopo una modifica del firmware, o prima e dopo aver toccato il livello.
  Conservare i file `.log` e annotare data, versione del firmware, livello RX e
  scheda audio.
* **NOT DECODED** = multimon-ng lo ha trovato e l'ESP32 no. Su un canale saturo ci
  si aspettano gruppi di mancanti attorno alle collisioni.
* **EXTRA** = l'ESP32 ha trovato qualcosa che multimon-ng non ha visto. Un
  decodificatore migliore del riferimento mostra degli extra; è un buon segno, non
  un errore.
* **DIFFERENT** dovrebbe essere raro (i frame AX.25 portano un CRC). Guardare la
  riga propria dell'ESP32 stampata sotto il pacchetto per vedere esattamente cosa
  ha decodificato; la causa è spesso un testo di contenuto troncato o alterato nel
  log dell'ESP32.

Schemi tipici:

| Cosa si vede | Causa probabile |
|---|---|
| `ok=0` fin dall'inizio, tutto NOT DECODED | L'audio non arriva all'ESP32 (cavo, scheda audio, livello, modem disattivato). |
| Un intero file quasi tutto NOT DECODED, ma quello sintetico dava OK | Livello troppo basso/alto per quella registrazione, oppure una registrazione de-enfatizzata (traccia 2) che richiede un livello diverso. |
| Mancanti solo nei tratti densi | Normale con traffico saturo (collisioni, pacchetti consecutivi). |
| Improvvisa raffica di NOT DECODED in mezzo a un file | Qualcosa ha disturbato l'audio (un suono di sistema, un cambio di volume) o la scheda audio ha avuto un problema. |
| Molti EXTRA | L'ESP32 è più sensibile di multimon-ng su questo materiale. |

---

## 12. Come vengono confrontati i pacchetti

I numeri sono affidabili solo se si sa come avviene il confronto.

### 12.1 Cosa conta come "lo stesso pacchetto"

I due decodificatori descrivono il pacchetto con stili diversi, quindi il
programma prima li normalizza. Ci sono cinque differenze tra multimon-ng e il
firmware, e tutte sono gestite:

| Differenza | multimon-ng | Firmware dell'ESP32 | Trattamento |
|---|---|---|---|
| SSID 0 | stampa `LU1ABC-0` | stampa `LU1ABC` | un `-0` finale viene rimosso |
| Marcatore di digipeating | non stampa mai `*` | stampa `WIDE1-1*` dopo che un digi lo ha ripetuto | l'`*` viene ignorato |
| Byte non stampabili (i pacchetti Mic-E contengono byte di controllo e a 8 bit) | li mostra come `.` | scrive i byte grezzi | il contenuto dell'ESP32 viene convertito allo stesso modo prima del confronto |
| Ritorno a capo finale | lo scarta | lo scrive grezzo | un CR/LF/NUL finale viene ignorato |
| Maiuscole/minuscole degli indirizzi | come uditi | come uditi | origine, destinazione e percorso vengono portati in maiuscolo prima del confronto, quindi il solo caso delle lettere non produce mai un DIFFERENT |

Ciò che **non** viene ignorato: **spazi e punti** finali sono contenuto reale,
quindi un contenuto troncato (`>hello.` contro `>hello`) viene correttamente
riportato come DIFFERENT. Gli SSID reali (`-9`, `-10`) vengono sempre confrontati.

Per essere OK devono coincidere **origine, destinazione, ogni elemento del
percorso e tutto il contenuto**.

### 12.2 La finestra temporale

Una registrazione può contenere lo stesso pacchetto molte volte (una stazione che
emette un beacon ogni 30 s). Per non attribuire all'ESP32 la trasmissione
sbagliata, una coppia si forma solo se la riga dell'ESP32 è arrivata entro
**±`--match_window` secondi (5 per impostazione predefinita)** dal pacchetto di
multimon-ng. Ogni pacchetto dell'ESP32 viene usato **una sola volta**: un
pacchetto inviato tre volte deve essere decodificato tre volte per fare tre OK.
Quando esistono due candidati, vince quello **più vicino nel tempo**.

### 12.3 Cosa significa la finestra temporale per i pacchetti identici (traccia 3 di WA8LMF)

La traccia 3 ha 100 pacchetti **identici** distanziati di soli **3 secondi**. È il
caso più difficile per l'abbinamento, quindi ecco esattamente cosa aspettarsi
(verificato per simulazione):

* I **totali sono corretti con qualsiasi finestra ragionevole** (per esempio 97 OK
  quando l'ESP32 ha perso 3 su 100).
* Con la finestra predefinita di 5 s, *quali numeri di pacchetto* vengono segnati
  come NOT DECODED può risultare spostato di uno quando la riga dell'ESP32 arriva
  leggermente **prima** di quella di multimon-ng. Il conteggio è giusto; la
  numerazione dei pacchetti segnalati può slittare.
* Regola: scegliere una finestra **maggiore del ritardo reale** tra i due
  decodificatori e **minore della metà della distanza** tra pacchetti identici. Per
  la traccia 3: **1,5 s** (`--match_window 1.5`).
* Se la finestra è *minore* del ritardo reale, i pacchetti che l'ESP32 ha
  decodificato correttamente vengono contati male. Quindi **misurare prima il
  ritardo**: eseguire la traccia 3 e confrontare i due orari stampati per i primi
  pacchetti OK (la riga `[multimon ...]` e la riga `[esp32 ...]` stampata subito
  sotto). Se differiscono di più di circa 1 s, mantenere una finestra più ampia
  (e ricordare che solo i totali sono esatti).

Per tutte le altre tracce (nessun pacchetto identico più vicino di 10 s) la
finestra predefinita di 5 s va bene.

---

## 13. Piano di test suggerito con le tracce di WA8LMF

Eseguire i passi in ordine; ciascuno dà fiducia per il successivo.

| Passo | File | Comando (aggiungere `--audio_device hw:X,0`) | Scopo / cosa guardare |
|---|---|---|---|
| 0 | `sample.wav` sintetico | `--wav_dir Synthetic` | Verifica della catena: i 3 pacchetti devono essere **OK**. |
| 1 | Traccia 3 (100 burst Mic-E) | `--wav_dir one3 --match_window 1.5` | **Percentuale esatta**: l'ESP32 dovrebbe decodificare quasi 100 su 100. Mette alla prova anche contenuti Mic-E con caratteri di controllo. Misurare qui lo sfasamento temporale tra i decodificatori. |
| 2 | Traccia 2 (gli stessi 100, de-enfatizzati) | `--wav_dir one2 --match_window 1.5` | Gli stessi 100 pacchetti con la curva tipo altoparlante: confrontare con il passo 1. Regolare il livello con LIVELLO RX se necessario (il segnale è diverso). |
| 3 | Traccia 4 (mobile, debole) | `--wav_dir one4` | Segnale debole, flutter e multipath. Confrontare i mancanti con ciò che riesce a fare multimon-ng. |
| 4 | Traccia 1 (25 min saturi) | `--wav_dir one1` | La prova di stress: collisioni, pacchetti consecutivi. Aspettarsi una percentuale più bassa rispetto alle tracce 3/4 e qualche EXTRA. |
| 5 | Tutte e quattro | `--wav_dir Audio-Tracks` | La batteria completa, un solo riepilogo con i totali generali. |

Creare una volta sola le directory a file singolo:

```bash
for n in 1 2 3 4; do mkdir -p one$n; cp Audio-Tracks/0${n}_*.wav one$n/; done
```

Ripetere l'intero piano dopo ogni modifica del firmware e confrontare i log. Per
valutare la variazione naturale, eseguire prima **tre volte** lo stesso file: il
controllo automatico di guadagno dell'ESP32 e il clock della scheda audio fanno
differire leggermente i risultati tra un'esecuzione e l'altra.

Valori di riferimento dalla pagina d'origine, a titolo orientativo: la traccia 3
contiene esattamente **100** pacchetti, quindi "numero di OK ÷ 100" è direttamente
il tasso di successo dell'ESP32 con audio pulito.

---

## 14. `gen_test_wav.py` in dettaglio

Un piccolo generatore di **audio APRS sintetico perfetto**, usato per verificare
l'impianto e per creare file di prova riproducibili. Costruisce veri frame AX.25
(CRC-16, bit stuffing, NRZI) modulati come AFSK Bell 202 a 1200 baud a 22050 Hz,
mono, 16 bit, con 40 byte di flag di preambolo (≈ 0,27 s), 8 flag di coda e 1 s di
silenzio tra i pacchetti. Il file inizia inoltre con 0,5 s di silenzio, e lo
stesso intervallo di 1 s segue l'ultimo pacchetto, così nulla viene troncato alle
estremità. I toni sono scritti a circa il 60 % del fondo scala, il che lascia
margine e mantiene il file senza clipping.

### Uso come programma

```bash
python3 gen_test_wav.py output.wav
```

scrive un file con tre pacchetti e stampa `ok`. Senza un nome di file scrive
`sample1.wav` nella directory corrente. Non crea directory, quindi creare prima
quella di destinazione (`mkdir -p Synthetic`). I pacchetti sono:

| Origine | Destinazione | Percorso | Contenuto |
|---|---|---|---|
| N0CALL-9 | APRS | WIDE1-1, WIDE2-1 | `!4903.50N/07201.75W-Test one` |
| LU1ABC | APDW17 | WIDE1-1* | `=3450.12S/05812.34W>Movil en ruta` |
| EA4XYZ-7 | APRS | (nessuno) | `:LU1ABC   :Hola que tal{12` |

(`N0CALL` è il nominativo segnaposto standard; gli altri sono esempi.)

### Uso come modulo (pacchetti propri)

```python
from gen_test_wav import write_wav

packets = [
    # (origine,    destinaz., [percorso],              contenuto)
    ("N0CALL-9",  "APRS",   ["WIDE1-1", "WIDE2-1"], "!4903.50N/07201.75W-Test"),
    ("LU1ABC",    "APDW17", ["WIDE1-1*"],           "=3450.12S/05812.34W>Mobile"),
    # un contenuto in stile Mic-E con caratteri di controllo / a 8 bit:
    ("LU2XYZ-9",  "T2SP0W", ["WIDE1-1"],            "`c2Bl\x1c>/\x1d]mice test\xe9"),
    # un contenuto che termina con un ritorno a capo:
    ("LU4CR",     "APRS",   [],                     ">status\r"),
]
write_wav("my_test.wav", packets, rate=22050, gap_s=1.0)
```

* `rate` — frequenza di campionamento del WAV (si raccomandano 22050 Hz).
* `gap_s` — secondi di silenzio tra i pacchetti.
* Un `*` dopo un elemento del percorso (`"WIDE1-1*"`) significa "già ripetuto da
  quel digi".

Usi: dimostrare che la catena funziona (sezione 8), provare contenuti speciali
(byte Mic-E, CR) e produrre un numero noto di pacchetti per una rapida esecuzione
di regressione.

---

## 15. Risoluzione dei problemi

| Sintomo | Causa e cosa fare |
|---|---|
| `Cannot open serial port … Permission denied` | L'utente non è nel gruppo `dialout` (sezione 5.1). |
| `Cannot open serial port … No such file or directory` | Porta sbagliata. `ls /dev/ttyUSB* /dev/ttyACM*`, controllare `dmesg \| tail`, usare `--serial_port`. |
| `Cannot open serial port … busy` | Un altro programma (idf.py monitor, minicom, screen…) ha la porta. Chiuderlo. |
| `WARNING: no data received from the serial port yet` | Porta o velocità sbagliate; l'ESP32 si sta ancora avviando (aumentare `--settle 8`); il cavo USB è solo di alimentazione. |
| `Missing required program(s): …` | Installare i pacchetti della sezione 2. |
| `No .wav files in …` | `--wav_dir` sbagliato, oppure i file sono FLAC/MP3 (convertirli, sezione 6.3). |
| `[audio] player failed` | `play` non riesce ad aprire il dispositivo audio: `--audio_device` sbagliato, la scheda è occupata (PulseAudio/PipeWire potrebbe tenerla) o manca `libsox-fmt-alsa`. Provare senza `--audio_device`, oppure eseguire `aplay -l`. |
| multimon-ng decodifica pacchetti ma **tutti** risultano NOT DECODED | L'ESP32 non sta ascoltando l'audio. Controllare in quest'ordine: scheda audio corretta (`--audio_device`) → volume/muto del mixer (`alsamixer`) → cavo, collegamento di C1/RV1 e polarità → **Abilita modem audio ADC/DAC** e **Polarizzazione interna dell'ingresso ADC** su ON → premere **LIVELLO RX** mentre l'audio suona. |
| La console dell'ESP32 non mostra alcuna riga `RX:`, neanche con audio buono | Il modem è disattivato; **Registra dopo i filtri** è su ON; il livello di log non è INFO; oppure (raro) l'IGate disattivato le nasconde: attivare **Abilita IGate** ma lasciare **RF verso Internet** su OFF e il Wi-Fi solo come punto di accesso, e riprovare. |
| Avviso di fuori scala sulla console / livello RX molto sopra 350 mV | Livello troppo alto: abbassare RV1 o il volume del PC. Non lasciarlo così: il pin non ha diodi di protezione. |
| Livello RX molto basso (< 100 mV) | Alzare RV1, oppure alzare un poco il volume del PC. |
| Livello continuo vicino a 0 mV o a 3300 mV | L'autopolarizzazione è disattivata, oppure C1 manca o è collegato al contrario, oppure RV1 è tra C1 e il pin. |
| Molti NOT DECODED su un file ma non sugli altri | Il livello differisce tra le registrazioni (soprattutto quelle de-enfatizzate); ricontrollare con LIVELLO RX per quel file. |
| I risultati cambiano tra un'esecuzione e l'altra | Normale in piccola misura (controllo automatico di guadagno, clock della scheda audio). Ripetere 3 volte e confrontare. Se la variazione è grande, controllare la stabilità della scheda audio USB e i suoni di sistema; considerare anche se la calibrazione automatica del volume (sezione 7.1) ha scelto un volume diverso ogni volta — fissarlo con `--volume X --no_auto_volume` per un confronto equo. |
| L'esecuzione richiede molto più tempo di quanto suggerisca la durata dei file | Normale: per impostazione predefinita ogni esecuzione inizia con la passata di calibrazione automatica del volume (sezione 7.1), che riproduce l'insieme dei WAV più volte prima che inizi il test riportato. Usare `--no_auto_volume` per saltarla una volta noto un buon `--volume`. |
| La calibrazione automatica del volume riporta "no packets decoded by multimon-ng at all" e si ferma | multimon-ng non ha trovato nulla a nessun volume — è un problema di file/instradamento audio, non di livello (vedere la riga "multimon-ng ha decodificato 0 pacchetti" più sopra). |
| L'ESP32 si riavvia quando il test parte | L'apertura della porta riavvia la scheda tramite DTR/RTS. È normale; `--settle` attende l'avvio. |
| Il programma sembra bloccato | I file lunghi vengono riprodotti in tempo reale; guardare la riga di avanzamento ogni 30 s. Ctrl-C ferma in sicurezza. |
| multimon-ng ha decodificato 0 pacchetti in un file | Il file non ha pacchetti (le tracce 5–7 sono solo toni), è troppo basso o non è AFSK 1200. Il programma esce con codice 2 se *nessun* file produce pacchetti. |
| Tracce 5–7 nella directory | Contengono toni, non pacchetti: fanno perdere tempo e non danno nulla. Spostarle fuori. |
| Pacchetti DIFFERENT | Confrontare le righe di multimon-ng e dell'ESP32 stampate per quel pacchetto; il contenuto differisce (vedere la sezione 11.5). Gli stessi pacchetti sono rielencati sotto i conteggi del file. |
| Coppie di righe `[multimon  --:--.-] NOT DECODED` seguite da `[esp32 only …]` | Non è un guasto: è così che viene stampato dal vivo un pacchetto EXTRA — decodificato dall'ESP32 e perso da multimon-ng. Viene contato in `EXTRA(esp only)`. |
| I numeri a sei cifre non corrispondono al conteggio dei pacchetti di multimon-ng | Sono un contatore di stampa che numera anche i pacchetti EXTRA e riparte a ogni file (sezione 11.1). |
| Traccia 3: i numeri dei pacchetti segnalati sembrano spostati di uno | Effetto finestra/temporizzazione descritto nella sezione 12.3. Usare `--match_window 1.5`; i totali sono comunque corretti. |

---

## 16. Limitazioni

* **Il riferimento non è la verità.** multimon-ng perde alcuni pacchetti e l'ESP32
  può decodificarli (riportati come EXTRA); e viceversa. Le percentuali misurano
  l'accordo con multimon-ng, non le prestazioni assolute.
* **L'accordo è per testo.** Un pacchetto è OK se il testo di origine,
  destinazione, percorso e contenuto è uguale; il programma non guarda i bit
  grezzi.
* **Solo AFSK 1200 baud** (l'`AFSK1200` di multimon-ng). Altre modalità non sono
  provate.
* **Il percorso audio del PC fa parte del test.** Il suo filtraggio, la precisione
  del suo clock e il suo rumore si sommano al risultato (vedere i consigli sulla
  scheda audio nella sezione 5.3).
* **Solo ricezione.** La catena di trasmissione dell'ESP32 non viene provata.
* **La calibrazione automatica del volume (sezione 7.1) è attiva per
  impostazione predefinita** e può scegliere un volume leggermente diverso da
  un'esecuzione all'altra quando due livelli decodificano quasi altrettanto
  bene. Per confronti rigorosi prima/dopo, fissare il volume con
  `--volume X --no_auto_volume` invece di lasciare che si ricalibri ogni volta.
* Le opzioni audio presuppongono **Linux con ALSA**.
* Il programma è stato sviluppato e verificato con una **console ESP32 simulata**,
  con i veri multimon-ng, sox e pyserial. Con hardware reale, mettere in conto di
  regolare il livello e forse `--match_window`; la sezione 15 copre i problemi
  abituali.

---

## 17. Scheda di riferimento rapido

```bash
# ── configurazione una tantum ─────────────────────────────────────
sudo apt install python3-serial multimon-ng sox libsox-fmt-all alsa-utils
sudo usermod -aG dialout $USER            # poi uscire / rientrare dalla sessione
./test_aprs_wavs.py --list_audio          # trovare la scheda audio → hw:X,0

# ── interfaccia web dell'ESP32 ────────────────────────────────────
#   Radiomodem  : Abilita modem audio ADC/DAC = ON, Polarizzazione interna
#                 dell'ingresso ADC = ON, Avvisa quando l'audio ricevuto esce dal fondo scala = ON
#   IGate       : Abilita IGate = OFF, RF verso Internet = OFF, Registra dopo i filtri = OFF
#   Digipeater  : Abilita Digipeater = OFF        (beacon OFF, Wi-Fi solo come punto di accesso)

# ── regolare il livello: riprodurre una registrazione molto attiva (traccia 1 di
#    WA8LMF), premere LIVELLO RX nella pagina Radiomodem più volte; obiettivo
#    250–350 mV RMS, raw lontano da 0/4095, livello continuo 1200–2000 mV; poi non toccare nulla
./test_aprs_wavs.py --wav_dir one1 --audio_device hw:1,0     # Ctrl-C a taratura finita

# ── verificare la catena (3 pacchetti puliti, si attende tutto OK) ─
mkdir -p Synthetic && python3 gen_test_wav.py Synthetic/sample.wav
./test_aprs_wavs.py --wav_dir Synthetic --audio_device hw:1,0

# ── il test reale (inizia con una passata di calibrazione automatica del
#    volume per impostazione predefinita, vedi 7.1; aggiungere
#    --no_auto_volume --volume X per saltarla e fissare un valore noto) ──
./test_aprs_wavs.py --wav_dir Audio-Tracks --audio_device hw:1,0 2>&1 | tee run.log
grep -E "NOT DECODED|DIFFERENT" run.log
```

| Collegamento | |
|---|---|
| Punta del jack | → parte alta di RV1 (o R1, sezione 3.1) |
| Manicotto del jack | → parte bassa di RV1 **e** GND dell'ESP32 (o l'estremo lontano di R2, sezione 3.1) |
| Cursore di RV1 | → **−** di C1 (lato striscia) (o il nodo R1/R2, sezione 3.1) |
| **+** di C1 | → GPIO33 dell'ESP32 |

---

## 18. Glossario

| Termine | Significato |
|---|---|
| **APRS** | Automatic Packet Reporting System: pacchetti radioamatoriali di posizione, messaggi e telemetria. |
| **AFSK 1200** | Audio Frequency Shift Keying a 1200 baud: due toni audio (1200 Hz e 2200 Hz) trasportano i bit (Bell 202). |
| **AX.25** | Il formato di pacchetto del livello di collegamento usato da APRS in VHF. |
| **Formato TNC2** | La forma testuale di un pacchetto: `ORIGINE>DESTINAZIONE,PERCORSO:contenuto`. |
| **Mic-E** | Una codifica compatta della posizione APRS che mette dati nell'indirizzo di destinazione e usa caratteri di controllo e a 8 bit nel contenuto. |
| **Uscita del discriminatore** | L'audio demodulato grezzo di un ricevitore FM, prima della de-enfasi: la fonte migliore per i dati. |
| **De-enfasi** | L'attenuazione degli acuti applicata dal ricevitore all'audio dell'altoparlante; i dati prelevati dall'altoparlante sono de-enfatizzati. |
| **IGate** | Gateway che inoltra alla rete Internet APRS-IS i pacchetti uditi via radio (e viceversa). |
| **Digipeater** | Una stazione che ripete i pacchetti via radio per estendere la portata. |
| **APRS-IS** | La rete Internet che raccoglie i pacchetti APRS. |
| **RMS** | Valore efficace (root-mean-square): la grandezza effettiva di un segnale alternato; l'ESP32 riporta il livello RX in mV RMS. |
| **Livello continuo (DC offset)** | La tensione continua media a cui riposa il pin dell'ADC; con l'autopolarizzazione dovrebbe aggirarsi intorno a 1650 mV. |
| **SSID** | Il numero dopo un nominativo (`-9`) che distingue più stazioni di uno stesso operatore. |
| **Calibrazione automatica del volume** | La passata predefinita del programma, prima del test che viene riportato, che cerca il guadagno di riproduzione via software (`--volume`) che dà la migliore percentuale decodificata senza fuori scala. Vedere la sezione 7.1. |
