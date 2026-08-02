.. _it-modem:

=================
Il modem software
=================

Il componente ``esp32idf_radioamateur_modem`` (integrato sotto ``components/``,
GPL-3.0) è il cuore del progetto: un modem software AFSK/FSK completo che demodula
e modula audio APRS interamente sull'ESP32, usando solo il SAR-ADC, il DAC e un
GPTimer. Questo capitolo copre il modem come *capacità* — i suoi profili, la sua
API pubblica e la sua configurazione a runtime. Per gli interni del DSP e il
ragionamento dietro le scelte di frequenza di campionamento e core, vedi
:ref:`it-dsp-signal-chain`.

Profili del modem
=================

I profili selezionabili (``modem_mode_t``) sono numerati in modo identico al menu
a tendina di *modulazione* dell'amministrazione web, così che l'applicazione
possa convertire il valore salvato direttamente nell'enum:

.. list-table::
   :header-rows: 1
   :widths: 10 30 16 44

   * - Valore
     - Profilo
     - Baud
     - Toni
   * - 0
     - AFSK300
     - 300
     - 1600 / 1800 Hz
   * - 1
     - **Bell 202** (predefinito, APRS standard)
     - 1200
     - 1200 / 2200 Hz
   * - 2
     - ITU V.23
     - 1200
     - 1300 / 2100 Hz
   * - 3
     - G3RUH FSK
     - 9600
     - —

Il profilo a 1200 Bd esegue **due demodulatori in parallelo**, sintonizzati
leggermente diversi, per elevare la probabilità di decodifica
(``MODEM_MAX_DEMODULATOR_COUNT = 2``).

Correzione d'errore in avanti FX.25
===================================

FX.25 incapsula AX.25 in un codice Reed–Solomon, permettendo al ricevitore di
correggere errori di bit che altrimenti fallirebbero il CRC. È totalmente
retrocompatibile: un frame FX.25 porta un normale frame AX.25 dentro un blocco RS
con tag di correlazione, quindi i ricevitori di puro AX.25 decodificano comunque
il frame interno. La modalità è selezionabile: ``0`` = disattivato, ``1`` =
solo RX, ``2`` = RX+TX (richiede ``-DENABLE_FX25`` in compilazione).
L'implementazione RS vive in ``lwfec/`` (``rs.c``, ``gf.c``).

API pubblica
============

L'header pubblico del componente (``esp32idf_radioamateur_modem.h``) espone:

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - Funzione
     - Scopo
   * - ``modem_init(cfg)``
     - Avvia l'hardware e i task di servizio interni. Si blocca ~5 s una volta
       per avvio calibrando il clock reale dell'ADC.
   * - ``modem_set_modem(cfg)``
     - Cambiare il profilo attivo e impostazioni correlate a runtime.
   * - ``modem_set_rx_callback(cb, ctx)``
     - Installare il callback invocato per ogni frame decodificato.
   * - ``modem_send_raw(frame, len)``
     - Accodare un frame AX.25 grezzo (senza flag/stuffing/FCS — tutto aggiunto
       automaticamente).
   * - ``modem_build_frame_tnc2(tnc2, out, out_len)``
     - Costruire un frame grezzo da una stringa monitor TNC2.
   * - ``modem_send_tnc2(tnc2)``
     - Costruire + accodare in una singola chiamata.
   * - ``modem_format_tnc2(msg, out, out_len)``
     - Rendere un frame decodificato di nuovo in una stringa TNC2.
   * - ``modem_tx_queue_depth()``
     - Numero di frame ancora in coda/in volo su TX RF (0 = inattivo). È lo stato
       dell'anello TX che legge il tetto di arretrato TX RF.
   * - ``modem_persistence_missed_count()``
     - Quante volte il pavimento anti-starvation della persistenza CSMA ha
       forzato una trasmissione dall'avvio, per chi voglia esporlo come
       statistica.
   * - ``modem_measure_adc_rate(ms)``
     - Misurare la frequenza reale di campionamento dell'ADC; si blocca per la
       finestra richiesta.

L'header porta inoltre ``MODEM_DEFAULT_CONFIG()`` (un inizializzatore di
``modem_config_t``), l'helper ``MODEM_DELAY_TICKS(ms)``, ``modem_rx_frame_t`` e il
tipo di callback ``modem_rx_cb_t``. Si noti che **non** esiste un punto di
ingresso di smontaggio: il modem viene avviato una volta per boot e
riconfigurato sul posto con ``modem_set_modem()``.

Configurazione a runtime (``modem_config_t``)
=============================================

Costruita in esattamente un posto — ``aprs_service_build_modem_config()`` —
condivisa dall'avvio, dal Salva della pagina Radio (riapplicazione in tempo
reale, nessun riavvio) e dal test di loop:

.. list-table::
   :header-rows: 1
   :widths: 24 30 46

   * - Campo
     - Origine
     - Note
   * - ``modem``
     - ``afsk_modem_type``
     - conversione diretta; la pagina fissa 0–3
   * - ``flat_audio``
     - ``audio_lpf``
     - nonostante il nome, è sempre il flag di ingresso audio piatto
   * - ``full_duplex``
     - ``false`` normalmente
     - LOOP TEST passa ``true`` (un cavo DAC→ADC significa che CSMA non vede mai
       il canale libero)
   * - ``allow_non_aprs``
     - ``false``
     - accettare Control/PID diversi da 0x03/0xF0?
   * - ``preamble_ms``
     - ``preamble`` (300)
     - TXDelay
   * - ``slot_time_ms``
     - ``tx_timeslot`` (2000)
     - tempo di silenzio CSMA; ignorato in full duplex
   * - ``persist``
     - ``csma_persist`` (63)
     - p-persistenza CSMA (il *Persist* standard AX.25/KISS): una volta che il
       canale è sentito libero, il modem trasmette con probabilità
       ``persist``/256 per slot e altrimenti attende un altro ``slot_time_ms``
       prima di rilanciare. 255 = trasmette sempre al primo slot libero; valori
       più bassi distanziano le stazioni in contesa. Ignorato in full duplex.
   * - ``fx25_mode``
     - ``fx25_mode``
     - 0=off, 1=solo RX, 2=RX+TX
   * - ``ptt_active_high``
     - ``MODEM_PTT_ACTIVE_HIGH``
     - cablaggio di scheda in compilazione, non un campo di configurazione
   * - ``min_unkey_ms``
     - ``ptt_min_unkey_ms``
     - tempo minimo extra di PTT-disattivato tra le trasmissioni

.. note::

   Il GPIO del PTT **non** è un campo di ``modem_config_t`` — è una scelta di
   cablaggio di scheda fissata in compilazione (``MODEM_PTT_GPIO``), come i pin
   ADC/DAC. Solo il *livello* attivo è passato a runtime, e viene anch'esso
   direttamente dalla macro di compilazione. Esplicitamente **non** mappati a
   runtime (senza equivalente nel componente): pin e attenuazione ADC/DAC, squelch
   hardware, interruttore di potenza RF, squelch software, volume RX e il tetto
   dell'AGC.

Il LOOP TEST
============

Lo strumento di messa in funzione più utile del progetto. Cabla
**GPIO25 → GPIO33**, apri *Radio / Modem*, premi **LOOP TEST**.
``aprs_loop_test_run()``:

#. Costruisce un piccolo pacchetto APRS che porta un **token casuale monouso**
   (``>LOOPTEST <token>``).
#. **Devia** i frame decodificati al proprio hook così che il frame di test non
   venga mai digipetato, caricato, né registrato come traffico reale.
#. Commuta il modem a **full duplex** — un cavo DAC→ADC significa che il nodo
   sente sempre la propria portante e CSMA non attiverebbe mai la radio.
#. Trasmette, poi attende fino a **4000 ms** che la catena ADC → demodulatore →
   HDLC → AX.25 restituisca lo stesso frame.
#. **Ripristina sempre** l'hook reale e la modalità duplex configurata prima di
   tornare.

Nel frattempo un task di monitor cattura diagnostici che il componente espone
solo istantaneamente: uno snapshot dell'ADC grezzo passivo a metà preambolo, RMS
di picco, guadagno AGC di picco, una mappa di bit di DCD, e la fase RX HDLC più
lontana raggiunta per demodulatore. Il messaggio di risultato distingue:

.. list-table::
   :header-rows: 1
   :widths: 46 54

   * - Sintomo
     - Diagnosi
   * - ADC grezzo min ≈ max
     - ADC morto / non cablato
   * - il grezzo oscilla, RMS ~0
     - nessun tono raggiunge l'ADC
   * - RMS ok, DCD mai attivo
     - il PLL non ha mai agganciato → mismatch baud/tipo modem o audio cattivo
   * - DCD attivo, fase < FRAME
     - flag visti ma nessun frame iniziato — problema di recupero bit, non rumore
   * - DCD attivo, fase = FRAME, nessun frame
     - frame assemblati ma falliti al CRC — livello/SNR marginale
   * - frame di ritorno, token non corrisponde
     - distorsione, clipping, o cablaggio di loop sbagliato
   * - PASS
     - riporta il livello RX in mV RMS
