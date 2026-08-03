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
   * - ingest: de-interleave coppie, rimozione offset DC, AGC, misura RMS
     - 76 800 Hz
     - ``afsk.c``
   * - FIR di decimazione (rapporto **8:1**)
     - → **9 600 Hz**
     - ``afsk.c``
   * - correlatore (mark/space), passa-basso, DPLL, decodifica NRZI
     - 9 600 Hz
     - ``modem.c``
   * - de-framing HDLC, de-stuffing di bit, controllo FCS, decodifica RS FX.25
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
   di baud supportata). Il trasmettitore mette i fronti di simbolo esattamente su
   campioni del DAC qualunque sia la frequenza; era il *ricevitore* ad aver
   bisogno di risoluzione.

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
   profilo è calcolato dal rapporto ADC/DAC *nominale* e la differenza è altrimenti
   un errore di stato stazionario che il DPLL deve inseguire per un'intera
   trasmissione. La frequenza di allarme del DAC è già nota esattamente dalla
   configurazione del timer, quindi solo il lato ADC necessita di misura. Entrambi
   i clock derivano dallo stesso cristallo, quindi il rapporto è una proprietà
   fissa della scheda: misurata **una volta per avvio**, riapplicata a ogni cambio
   di profilo.

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
ADC; priorità del timer DAC ∈ 1..3.

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
   * - ``src/afsk.c`` (~1440 righe)
     - ingest DMA dell'ADC, AGC, FIR di decimazione, ISR del DAC, PTT
   * - ``src/modem.c`` (~870 righe)
     - correlatori, DPLL, tabelle di toni, DCD, calibrazione
   * - ``src/ax25.c`` (~1500 righe)
     - framer HDLC, NRZI, bit-stuffing, codec AX.25, coda TX
   * - ``src/fx25.c``, ``lwfec/rs.c``, ``lwfec/gf.c``
     - FEC Reed–Solomon FX.25
   * - ``src/crc_ccit.c``
     - FCS (sequenza di controllo del frame)
