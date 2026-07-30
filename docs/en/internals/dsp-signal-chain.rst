.. _en-dsp-signal-chain:

====================
The DSP Signal Chain
====================

This chapter explains *how* the modem turns radio audio into frames and back,
and — just as important — *why* the numbers are what they are. The modem's
config header is unusually well documented, and the reasoning matters if you
ever touch it.

The chain, stage by stage
=========================

.. list-table::
   :header-rows: 1
   :widths: 44 22 34

   * - Stage
     - Rate
     - Where
   * - SAR-ADC1 continuous/DMA, 128-sample conversion frames
     - **76 800 Hz**
     - driver ISR on core 0
   * - ingest: pair un-swap, DC-offset removal, AGC, RMS metering
     - 76 800 Hz
     - ``afsk.c``
   * - decimation FIR (ratio **8:1**)
     - → **9 600 Hz**
     - ``afsk.c``
   * - correlator (mark/space), low-pass, DPLL, NRZI decode
     - 9 600 Hz
     - ``modem.c``
   * - HDLC de-framing, bit de-stuffing, FCS check, FX.25 RS decode
     - —
     - ``ax25.c`` / ``fx25.c``
   * - ⟵ TX ⟶ AX.25 encode, FCS, bit stuff, NRZI, 32-bit phase accumulator,
       512-entry sine LUT
     - **38 400 Hz**
     - ``ax25.c`` / ``modem.c`` / ``afsk.c``

Why the numbers are what they are
=================================

**ADC at 76 800 Hz, not 38 400.**
   38 400 gives the 9600 Bd profile exactly *four* ADC samples per symbol. The
   DPLL's sample instant is then quantised to 25 % of a symbol and the
   three-sample majority vote spans 75 % of a symbol — the vote window always
   reaches into a transition. Host simulation of the real ``modem.c``, with real
   clocks and **no noise**, produced hard bit errors at every phase where ADC
   instants line up with DAC update instants; the two clocks differ by ~0.05 %,
   so the alignment walks through those phases every ~55 ms. At 76 800 the same
   simulation gives zero bit errors at every phase and with up to 30 µs of TX
   edge jitter. AFSK profiles never cared (they are demodulated at 9600 Hz
   through a correlator after decimation) and measure identically at either
   rate. **Cost:** twice the RX DSP work, and ``MODEM_RESAMPLE_RATIO`` becomes
   8, which requires the longer decimation FIR — an 8-tap filter cut for 4:1
   does not anti-alias 8:1.

**DAC stays at 38 400 Hz** (= 32 × 1200, an exact multiple of every supported
   baud rate). The transmitter puts symbol edges exactly on DAC samples whatever
   the rate; it was the *receiver* that needed resolution.

**``MODEM_ADC_CONV_FRAME = 128``, not the block size.**
   The IDF's own ADC ISR calls ``xRingbufferSendFromISR()``, which does the
   whole ``memcpy`` **inside** ``portENTER_CRITICAL_ISR()``. On Xtensa that
   raises ``PS.INTLEVEL`` to 3 — and the DAC sample clock *is* a level-3
   interrupt. So the DAC ISR is masked for the length of the copy: 768 samples
   ≈ 11 µs (10 % of a 9600 Bd symbol — fatal); 128 samples ≈ 2 µs (2 % — inside
   budget). No amount of ``IRAM_ATTR`` on our side helps: the blocking code is
   the driver's, already in IRAM, and simply long. At 1200 Bd 11 µs is 1.3 % of
   a symbol and invisible — which is exactly why every AFSK profile passed while
   G3RUH dropped frames.

**``MODEM_DAC_TIMER_CORE (1) ≠ MODEM_ADC_ISR_CORE (0)``.**
   ``portENTER_CRITICAL_ISR()`` masks level ≤ 3 on the *local* core only. Put
   the DAC clock on the other core and the ADC ISR merely spins for the lock
   instead of masking it. Enforced with ``#error``. The two fixes (small frames,
   split cores) are independent and both are applied.

**``ModemCalibrateSampleRate()``.**
   ``modem_init()`` blocks ~5 s at boot measuring the *real* ADC rate
   (``modem_measure_adc_rate()``), because every profile's PLL step is computed
   from the *nominal* ADC/DAC ratio and the gap is otherwise a steady-state
   error the DPLL must track for a whole transmission. The DAC alarm rate is
   already known exactly from the timer config, so only the ADC side needs
   measuring. Both clocks derive from the same crystal, so the ratio is a fixed
   board property: measured **once per boot**, reapplied on every profile
   switch.

**``MODEM_RX_FIFO_SIZE = 4096`` samples.**
   Sized in *samples*, so it shrank in *time* when the rate doubled (2048 was
   53 ms at 38.4 k, only 26.7 ms at 76.8 k — barely one 20 ms block). 4096
   restores the margin; it must hold ≥ 2 blocks, since ``AFSK_Poll()`` consumes
   whole blocks only.

Compile-time guards
===================

Compile-time ``#error`` guards enforce: DAC pin ∈ {25, 26}; ADC pin ∈ 32–39;
``MODEM_ADC_SAMPLERATE % 9600 == 0``; FIFO ≥ 2 blocks; ``MODEM_ADC_CONV_FRAME``
even, dividing ``MODEM_BLOCK_SIZE``, and byte-aligned to
``SOC_ADC_DIGI_DATA_BYTES_PER_CONV``; DAC timer core ≠ ADC ISR core; DAC timer
priority ∈ 1..3.

Compile-time configuration reference
====================================

All in ``components/esp32idf_radioamateur_modem/include/esp32idf_radioamateur_modem_config.h``,
every macro ``#ifndef``-guarded so the build system can override it.

.. list-table::
   :header-rows: 1
   :widths: 34 16 50

   * - Macro
     - Default
     - Meaning
   * - ``MODEM_DAC_GPIO``
     - 25
     - audio out; 25 or 26 only
   * - ``MODEM_ADC_GPIO``
     - 33
     - audio in; 32–39 only
   * - ``MODEM_PTT_GPIO``
     - 26
     - PTT pin (board wiring)
   * - ``MODEM_PTT_ACTIVE_HIGH``
     - 1
     - PTT polarity
   * - ``MODEM_LED_TX_GPIO`` / ``_RX_GPIO``
     - −1
     - status LEDs
   * - ``MODEM_DAC_SAMPLERATE``
     - 38400
     - = 32 × 1200
   * - ``MODEM_ADC_SAMPLERATE``
     - 76800
     - = 8 × 9600
   * - ``MODEM_DAC_AMPLITUDE_PCT``
     - 60
     - DAC swing, % of 0–3.3 V
   * - ``MODEM_ADC_ATTEN``
     - ``ADC_ATTEN_DB_12``
     - ≈ 0–3.1 V window
   * - ``MODEM_RX_FIFO_SIZE``
     - 4096
     - samples, power of two
   * - ``MODEM_ADC_CONV_FRAME``
     - 128
     - samples per DMA frame
   * - ``MODEM_RX_TASK_PRIO`` / ``_STACK`` / ``_CORE``
     - 10 / 4096 / 0
     - RX DSP task
   * - ``MODEM_ADC_ISR_CORE``
     - 0
     - ADC DMA ISR core
   * - ``MODEM_DAC_TIMER_CORE``
     - 1
     - **must differ from ADC ISR core**
   * - ``MODEM_DAC_TIMER_INTR_PRIO``
     - 3
     - 1..3
   * - *(derived)* ``MODEM_DEMOD_SAMPLERATE``
     - 9600
     - fixed
   * - *(derived)* ``MODEM_RESAMPLE_RATIO``
     - 8
     - ADC ÷ demod
   * - *(derived)* ``MODEM_BLOCK_SIZE``
     - 1536
     - 20 ms at 76.8 kHz

The modem source files
======================

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - File
     - Role
   * - ``src/afsk.c`` (~1440 ln)
     - ADC DMA ingest, AGC, decimation FIR, DAC ISR, PTT
   * - ``src/modem.c`` (~870 ln)
     - correlators, DPLL, tone tables, DCD, calibration
   * - ``src/ax25.c`` (~1500 ln)
     - HDLC framer, NRZI, bit-stuffing, AX.25 codec, TX queue
   * - ``src/fx25.c``, ``lwfec/rs.c``, ``lwfec/gf.c``
     - FX.25 Reed–Solomon FEC
   * - ``src/crc_ccit.c``
     - FCS (frame check sequence)
