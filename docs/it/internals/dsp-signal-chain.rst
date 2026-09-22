.. _it-dsp-signal-chain:

========================
La catena di segnale DSP
========================

Questo capitolo spiega *come* il modem converte l'audio radio in frame e
viceversa, e — altrettanto importante — *perché* i numeri sono quelli che sono.
L'header di configurazione del modem è insolitamente ben documentato, e il
ragionamento conta se mai lo tocchi.

La catena, fase per fase
========================

.. list-table::
   :header-rows: 1
   :widths: 44 22 34

   * - Fase
     - Frequenza
     - Dove
   * - SAR-ADC1 continuo/DMA, frame di conversione da 128 campioni
     - **76 800 Hz**
     - ISR del driver su core 0
   * - ingest: de-interleave coppie, rimozione offset DC, misura RMS, decisione
       della soglia di ricezione
     - 76 800 Hz
     - ``afsk.c``
   * - FIR di decimazione (rapporto **8:1**), passa-alto CTCSS opzionale, AGC o
       guadagno fisso, anello di trattenuta della soglia
     - → **9 600 Hz**
     - ``afsk.c``
   * - per demodulatore (fino a tre): prefiltro passa-banda, correlatore
       (mark/space), passa-basso, DPLL, decodifica NRZI, stima dello
       sbilanciamento dei toni
     - 9 600 Hz
     - ``modem.c``
   * - de-framing HDLC, de-stuffing di bit, controllo FCS (riparazione dei bit
       opzionale), decodifica RS FX.25, soppressione dei duplicati tra
       demodulatori
     - —
     - ``ax25.c`` / ``fx25.c``
   * - ⟵ TX ⟶ codifica AX.25, FCS, bit stuff, NRZI, accumulatore di fase a 32 bit,
       LUT seno da 512 voci
     - **38 400 Hz**
     - ``ax25.c`` / ``modem.c`` / ``afsk.c``

Cosa dà per buono il decodificatore di trame
============================================

``ax25_decode()`` è un punto di ingresso pubblico del componente modem, quindi
convalida il proprio input invece di fidarsi del produttore che riempie il
buffer. Oltre alla lunghezza minima dell'intestazione (destinazione + sorgente +
controllo + PID = 16 byte) non legge nulla senza prima misurarlo rispetto a
``len``: il campo indirizzi viene percorso un indirizzo alla volta, e sia
l'ottetto di estensione sia i sette byte del ripetitore che dichiara devono
essere ancora dentro la trama. Una ricezione troncata o corrotta i cui bit di
estensione non terminano mai viene respinta, invece di decodificare byte che
stanno oltre la trama — nel percorso RX quei byte sono la coda della trama
ricevuta in precedenza, che altrimenti diventerebbero nominativi di ripetitore
del tutto plausibili in una decodifica per il resto valida.

La regolazione della ricezione
==============================

Tutto quanto segue si imposta a runtime tramite ``modem_config_t.rx`` (il
gruppo *Demodulatore di ricezione* di :ref:`it-radiomodem`) ed è applicato da
``afskSetModem()`` con il task di ricezione fermo, quindi nessun blocco viene
mai elaborato da una catena costruita a metà.

**Più demodulatori, ognuno con un prefiltro inclinato.**
   La decisione del correlatore è ``(|LoI|+|LoQ|) − (|HiI|+|HiQ|)``: uno
   sbilanciamento dei toni la distorce. In aria tale sbilanciamento va da nulla
   (un trasmettitore con porta dati piatta su un'uscita discriminatore) a
   5–12 dB a favore del tono di spazio (un trasmettitore con preenfasi su
   un'uscita discriminatore), e un'uscita altoparlante sposta entrambi della
   deenfasi del ricevitore. Un singolo correlatore fallisce quando lo
   sbilanciamento totale — quello del segnale più l'inclinazione del prefiltro —
   supera circa ±12 dB, quindi i profili a 1200 Bd eseguono fino a tre
   demodulatori i cui prefiltri passa-banda hanno inclinazioni diverse. I
   prefiltri vengono progettati in ``ModemInit()`` (campionamento in frequenza,
   finestra di Hamming, fase lineare, picco della banda passante scalato
   all'unità perché i percorsi int16 e int32 restino nei limiti) a partire dai
   bordi di banda, dalla lunghezza e dall'inclinazione; l'inclinazione che
   raggiungono davvero, misurata sui coefficienti, viene scritta nel log e usata
   dalla stima dello sbilanciamento. Il set classico conserva le tabelle fisse a
   8 coefficienti.

**Soppressione dei duplicati e statistiche.**
   Una trama con FCS valido apre una finestra di 32 periodi di bit × il numero
   di demodulatori attivi; le copie con lo stesso CRC degli altri demodulatori
   al suo interno vengono scartate (trame semplici e FX.25 allo stesso modo).
   La finestra registra quali demodulatori hanno prodotto la trama, quindi alla
   chiusura attribuisce una trama ottenuta da uno solo a quel demodulatore — il
   contatore ``esclusivi`` che dice ciò che ogni prefiltro aggiunge.

**La soglia di ricezione conserva ciò che trattiene.**
   Un blocco raggiunge i demodulatori mentre il suo RMS ha superato
   ``rx.gate_mv`` per più di tre blocchi, e finché non scende sotto la metà. Il
   decimatore e il passa-alto girano su ogni blocco, e gli ultimi tre blocchi
   trattenuti vengono conservati (decimati, 3 × 192 float); quando la soglia si
   apre vengono demodulati per primi, così il preambolo speso per decidere
   l'apertura non va perso. ``gate_mv = 0`` alimenta ogni blocco.

**Controllo del guadagno sul segnale in banda.**
   L'AGC misura il blocco decimato e non il flusso a 76,8 kHz, quindi il rumore
   del discriminatore sopra i 5 kHz non determina il guadagno, e il nuovo
   guadagno si applica al blocco su cui è stato misurato. Attacco 0,25 e rilascio
   0,002 per blocco da 20 ms, con il passo per blocco limitato a ×2 / ÷2. Un
   guadagno fisso lo sostituisce quando ``rx.agc_mode`` lo richiede.

**Riparazione dei bit tramite sindrome del CRC.**
   CRC-16/X.25 è affine su GF(2): il registro dopo una trama e il suo FCS è
   ``0xF0B8`` XOR una sindrome che dipende solo dallo schema d'errore. La
   sindrome di un singolo bit errato in posizione *p* è un passo di CRC a
   ingresso zero della sindrome in *p* + 1, quindi ogni bit singolo e ogni
   coppia adiacente (lo schema di un simbolo errato dopo NRZI) vengono
   verificati in una sola passata. Una correzione è accettata solo quando
   esattamente un candidato corrisponde e la trama supera un rigoroso test di
   plausibilità APRS, e mai mentre la copia intatta di un altro demodulatore è
   nella finestra dei duplicati.

**Il componente modem è compilato con ``-O2``.**
   Il DSP di ricezione gira su ogni campione di ogni demodulatore; il
   ``CMakeLists.txt`` del componente lo compila ottimizzato per la velocità
   qualunque sia il livello usato dal resto del progetto.

Perché i numeri sono quelli che sono
====================================

**ADC a 76 800 Hz, non 38 400.**
   38 400 dà al profilo a 9600 Bd esattamente *quattro* campioni ADC per simbolo.
   L'istante di campionamento del DPLL è allora quantizzato al 25 % di un simbolo
   e il voto a maggioranza di tre campioni copre il 75 % di un simbolo — la
   finestra di voto raggiunge sempre una transizione. La simulazione su host del
   ``modem.c`` reale, con clock reali e **senza rumore**, ha prodotto errori di
   bit netti in ogni fase in cui gli istanti dell'ADC si allineano con gli istanti
   di aggiornamento del DAC; i due clock differiscono di ~0,05 %, quindi
   l'allineamento percorre quelle fasi ogni ~55 ms. A 76 800 la stessa simulazione
   dà zero errori di bit in ogni fase e con fino a 30 µs di jitter di fronte TX. Ai
   profili AFSK non è mai importato (si demodulano a 9600 Hz attraverso un
   correlatore dopo la decimazione) e misurano identico a qualsiasi frequenza.
   **Costo:** il doppio del lavoro RX DSP, e ``MODEM_RESAMPLE_RATIO`` diventa 8,
   ciò che richiede il FIR di decimazione più lungo — un filtro a 8 tap tagliato
   per 4:1 non fa antialias di 8:1.

**Il DAC resta a 38 400 Hz** (= 32 × 1200, un multiplo esatto di ogni frequenza
   di baud supportata). I profili AFSK costruiscono i passi dei toni e un
   accumulatore di fase di simbolo Q32 dalla frequenza *reale* di allarme del
   DAC, quindi toni e velocità sono esatti anche se il periodo di allarme si
   arrotonda a tick interi del timer; un fronte di simbolo cade sul campione del
   DAC più vicino, al massimo il 3 % di un simbolo a 1200 Bd e nascosto dal tono
   a fase continua. G3RUH mantiene invece ogni simbolo per un numero intero di
   campioni del DAC, con tutti i fronti sulla stessa griglia: un accumulatore
   frazionario a quattro campioni per simbolo sposterebbe ogni tanto un fronte di
   un quarto di simbolo. Era il *ricevitore* ad aver bisogno di risoluzione.

**``MODEM_ADC_CONV_FRAME = 128``, non la dimensione di blocco.**
   L'ISR dell'ADC di IDF stessa chiama ``xRingbufferSendFromISR()``, che fa tutto
   il ``memcpy`` **dentro** ``portENTER_CRITICAL_ISR()``. Su Xtensa ciò alza
   ``PS.INTLEVEL`` a 3 — e il clock di campionamento del DAC *è* un'interruzione di
   livello 3. Quindi l'ISR del DAC è mascherata durante la copia: 768 campioni ≈
   11 µs (10 % di un simbolo a 9600 Bd — fatale); 128 campioni ≈ 2 µs (2 % —
   entro il budget). Nessuna quantità di ``IRAM_ATTR`` dal nostro lato aiuta: il
   codice bloccante è del driver, già in IRAM, e semplicemente lungo. A 1200 Bd,
   11 µs è l'1,3 % di un simbolo e invisibile — che è esattamente perché ogni
   profilo AFSK passava mentre G3RUH perdeva frame.

**``MODEM_DAC_TIMER_CORE (1) ≠ MODEM_ADC_ISR_CORE (0)``.**
   ``portENTER_CRITICAL_ISR()`` maschera il livello ≤ 3 solo sul core *locale*.
   Metti il clock del DAC sull'altro core e l'ISR dell'ADC gira solo aspettando il
   lock invece di mascherarlo. Imposto con ``#error``. Le due correzioni (frame
   piccoli, core separati) sono indipendenti ed entrambe applicate.

**``ModemCalibrateSampleRate()``.**
   ``modem_init()`` si blocca ~5 s all'avvio misurando la frequenza *reale*
   dell'ADC (``modem_measure_adc_rate()``), perché il passo del PLL di ogni
   profilo presuppone la frequenza *nominale* dell'ADC e la differenza è
   altrimenti un errore di stato stazionario che il DPLL deve inseguire per
   un'intera trasmissione. Le stazioni in aria trasmettono alla propria velocità
   nominale, quindi la correzione in ricezione è il solo errore dell'ADC. La
   frequenza reale di allarme del DAC, nota esattamente dalla configurazione del
   timer, viene registrata anch'essa ma usata solo quando un ricevitore G3RUH
   sente il trasmettitore del nodo stesso (full duplex, l'autotest con loop via
   filo). Entrambi i clock derivano dallo stesso cristallo, quindi i rapporti
   sono proprietà fisse della scheda: misurati **una volta per avvio**,
   riapplicati a ogni cambio di profilo.

**Il FIR di decimazione filtra sul posto.**
   Il campione di uscita *i* viene scritto in ``buf[i]`` mentre i coefficienti
   leggono la finestra che termina in ``buf[i × MODEM_RESAMPLE_RATIO]``, quindi
   con qualunque rapporto ≥ 2 il puntatore di scrittura resta dietro alla
   finestra di lettura tranne che nelle prime ``FILTER_TAPS − 1`` posizioni.
   Quei pochi campioni iniziali, insieme alla coda del blocco precedente, sono
   preparati in un breve array di stack prima che il ciclo inizi; il resto del
   blocco è letto grezzo da ``buf[]`` stesso. È tutta qui la ragione per cui il
   percorso di RX non tiene una seconda copia del blocco da 20 ms.

**``MODEM_RX_FIFO_SIZE = 4096`` campioni.**
   Dimensionato in *campioni*, quindi si è ristretto nel *tempo* quando la
   frequenza è raddoppiata (2048 erano 53 ms a 38,4 k, solo 26,7 ms a 76,8 k —
   appena un blocco da 20 ms). 4096 ripristina il margine; deve contenere ≥ 2
   blocchi, poiché ``AFSK_Poll()`` consuma solo blocchi interi.

Guardie di compilazione
=======================

Guardie ``#error`` di compilazione impongono: pin del DAC ∈ {25, 26}; pin
dell'ADC ∈ 32–39; ``MODEM_ADC_SAMPLERATE % 9600 == 0``; FIFO ≥ 2 blocchi;
``MODEM_ADC_CONV_FRAME`` pari, che divida ``MODEM_BLOCK_SIZE``, e allineato ai
byte a ``SOC_ADC_DIGI_DATA_BYTES_PER_CONV``; core del timer DAC ≠ core dell'ISR
ADC; priorità del timer DAC ∈ 1..3. Due ``_Static_assert`` in ``afsk.c`` fissano
l'invariante di lavoro sul posto del decimatore: ``MODEM_RESAMPLE_RATIO`` ≥ 2 a
meno che il filtro non abbia un solo coefficiente, e ``MODEM_BLOCK_SIZE`` ≥ 2 ×
(``FILTER_TAPS`` − 1) perché i tratti di storia iniziale e finale non possano
sovrapporsi.

Riferimento di configurazione in compilazione
=============================================

Tutto in
``components/esp32idf_radioamateur_modem/include/esp32idf_radioamateur_modem_config.h``,
ogni macro protetta con ``#ifndef`` così che il sistema di build possa
sovrascriverla.

.. list-table::
   :header-rows: 1
   :widths: 34 16 50

   * - Macro
     - Predefinito
     - Significato
   * - ``MODEM_DAC_GPIO``
     - 25
     - uscita audio; solo 25 o 26
   * - ``MODEM_ADC_GPIO``
     - 33
     - ingresso audio; solo 32–39
   * - ``MODEM_PTT_GPIO``
     - −1
     - pin di PTT (cablaggio di scheda). Il valore predefinito dell'header è −1
       (disabilitato); il ``CMakeLists.txt`` di livello superiore di questo
       progetto lo sovrascrive a 26.
   * - ``MODEM_PTT_ACTIVE_HIGH``
     - 1
     - polarità di PTT
   * - ``MODEM_LED_TX_GPIO`` / ``_RX_GPIO``
     - −1
     - LED di stato
   * - ``MODEM_DAC_SAMPLERATE``
     - 38400
     - = 32 × 1200
   * - ``MODEM_ADC_SAMPLERATE``
     - 76800
     - = 8 × 9600
   * - ``MODEM_DAC_AMPLITUDE_PCT``
     - 60
     - oscillazione del DAC, % di 0–3,3 V
   * - ``MODEM_ADC_ATTEN``
     - ``ADC_ATTEN_DB_12``
     - finestra ≈ 0–3,1 V
   * - ``MODEM_RX_FIFO_SIZE``
     - 4096
     - campioni, potenza di due
   * - ``MODEM_ADC_CONV_FRAME``
     - 128
     - campioni per frame DMA
   * - ``MODEM_RX_TASK_PRIO`` / ``_STACK`` / ``_CORE``
     - 10 / 4096 / 0
     - task RX DSP
   * - ``MODEM_ADC_ISR_CORE``
     - 0
     - core dell'ISR DMA dell'ADC
   * - ``MODEM_DAC_TIMER_CORE``
     - 1
     - **deve differire dal core dell'ISR dell'ADC**
   * - ``MODEM_DAC_TIMER_INTR_PRIO``
     - 3
     - 1..3
   * - ``MODEM_RX_MAX_DEMODULATORS``
     - 3
     - demodulatori a 1200 Bd in parallelo, 3..8
   * - *(derivato)* ``MODEM_DEMOD_SAMPLERATE``
     - 9600
     - fisso
   * - *(derivato)* ``MODEM_RESAMPLE_RATIO``
     - 8
     - ADC ÷ demod
   * - *(derivato)* ``MODEM_BLOCK_SIZE``
     - 1536
     - 20 ms a 76,8 kHz

I file sorgente del modem
=========================

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - File
     - Ruolo
   * - ``src/afsk.c`` (~1710 righe)
     - ingest DMA dell'ADC, soglia di ricezione e anello di trattenuta, FIR di
       decimazione, passa-alto, AGC, ISR del DAC, PTT
   * - ``src/modem.c`` (~1070 righe)
     - progetto dei prefiltri e set di demodulatori, correlatori, DPLL, tabelle
       di toni, DCD, stima dello sbilanciamento, calibrazione
   * - ``src/ax25.c`` (~1910 righe)
     - framer HDLC, NRZI, bit-stuffing, soppressione dei duplicati,
       riparazione dei bit, codec AX.25, coda TX
   * - ``src/esp32idf_radioamateur_modem.c`` (~590 righe)
     - l'API pubblica del componente: ``modem_init()``/``modem_set_modem()``, gli
       helper TNC2 e il task ``modem_svc`` che aziona il TX e consegna i frame
       decodificati al callback RX
   * - ``src/fx25.c``, ``lwfec/rs.c``, ``lwfec/gf.c``
     - FEC Reed–Solomon FX.25
   * - ``src/crc_ccit.c``
     - FCS (sequenza di controllo del frame)
