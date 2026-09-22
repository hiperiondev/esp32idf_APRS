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
   * - ingest: pair un-swap, DC-offset removal, RMS metering, receive gate decision
     - 76 800 Hz
     - ``afsk.c``
   * - decimation FIR (ratio **8:1**), optional CTCSS high-pass, AGC or fixed
       gain, gate hold ring
     - → **9 600 Hz**
     - ``afsk.c``
   * - per demodulator (up to three): band-pass prefilter, correlator
       (mark/space), low-pass, DPLL, NRZI decode, tone-twist tracking
     - 9 600 Hz
     - ``modem.c``
   * - HDLC de-framing, bit de-stuffing, FCS check (optional bit repair),
       FX.25 RS decode, cross-demodulator duplicate suppression
     - —
     - ``ax25.c`` / ``fx25.c``
   * - ⟵ TX ⟶ AX.25 encode, FCS, bit stuff, NRZI, 32-bit phase accumulator,
       512-entry sine LUT
     - **38 400 Hz**
     - ``ax25.c`` / ``modem.c`` / ``afsk.c``

What the frame decoder trusts
=============================

``ax25_decode()`` is a public entry point of the modem component, so it validates
its own input instead of relying on the producer that fills the buffer. Beyond
the minimum header length (destination + source + control + PID = 16 bytes) it
reads nothing without first measuring it against ``len``: the address field is
walked one address at a time, and both the extension octet and the seven bytes
of the repeater it promises must still be inside the frame. A truncated or
corrupted reception whose extension bits never terminate is rejected, rather
than decoding bytes that lie past the frame — in the RX path those bytes are the
tail of the previously received frame, which would otherwise turn into
plausible-looking repeater callsigns in an otherwise valid decode.

The receive tuning
==================

Everything below is set at run time through ``modem_config_t.rx`` (the
*Receive demodulator* fieldset of :ref:`en-radiomodem`) and applied by
``afskSetModem()`` with the receive task held, so no block is ever processed
by a half-built chain.

**Several demodulators, each with a tilted prefilter.**
   The correlator's decision is ``(|LoI|+|LoQ|) − (|HiI|+|HiQ|)``: a tone
   imbalance biases it. On the air that imbalance ranges from none (a flat
   data-port transmitter on a discriminator output) to 5–12 dB in favour of
   the space tone (a pre-emphasizing transmitter on a discriminator output),
   and a speaker output shifts both by the receiver's de-emphasis. A single
   correlator fails once the total twist — signal twist plus prefilter tilt —
   passes roughly ±12 dB, so the 1200 Bd profiles run up to three
   demodulators whose band-pass prefilters have different tilts. The
   prefilters are designed in ``ModemInit()`` (frequency sampling, Hamming
   window, linear phase, passband peak scaled to unity so the int16 and int32
   paths stay in range) from the band edges, the length and the tilt; the tilt
   they actually reach, measured on the coefficients, is logged and used by
   the twist estimate. The legacy set keeps the fixed 8-tap tables.

**Duplicate suppression and statistics.**
   A frame with a valid FCS opens a window of 32 bit periods × the active
   demodulator count; copies with the same CRC from the other demodulators
   inside it are dropped (plain and FX.25 frames alike). The window records
   which demodulators produced the frame, so its closing credits a frame that
   only one of them got to that demodulator — the ``unique`` counter that
   tells what each prefilter adds.

**The receive gate holds what it gates.**
   A block reaches the demodulators while its RMS has exceeded
   ``rx.gate_mv`` for more than three blocks, and until it falls below half of
   it. The decimator and the high-pass run on every block regardless, and the
   last three gated blocks are kept (decimated, 3 × 192 floats); when the gate
   opens they are demodulated first, so the preamble spent deciding to open is
   not lost. ``gate_mv = 0`` feeds every block.

**Gain control on the in-band signal.**
   The AGC measures the decimated block rather than the 76.8 kHz stream, so
   discriminator noise above 5 kHz does not set the gain, and the new gain is
   applied to the block it was measured on. Attack 0.25 and release 0.002 per
   20 ms block, the step per block bounded to ×2 / ÷2. A fixed gain replaces
   it when ``rx.agc_mode`` asks for one.

**Bit repair by CRC syndrome.**
   CRC-16/X.25 is affine over GF(2): the register after a frame and its FCS is
   ``0xF0B8`` XOR a syndrome that depends only on the error pattern. The
   syndrome of a single wrong bit at position *p* is one zero-input CRC step of
   the syndrome at *p* + 1, so every single bit and every adjacent pair (the
   pattern of one wrong symbol after NRZI) is checked in one pass. A correction
   is accepted only when exactly one candidate matches and the frame passes a
   strict APRS plausibility test, and never while another demodulator's intact
   copy is inside the duplicate window.

**The modem component is built at ``-O2``.**
   The receive DSP runs on every sample of every demodulator; the component's
   ``CMakeLists.txt`` compiles it optimized for speed whatever level the rest
   of the project uses.

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
   baud rate). The AFSK profiles build their tone steps and a Q32 symbol-phase
   accumulator from the *real* DAC alarm rate, so tones and baud rate are exact
   even though the alarm period rounds to whole timer ticks; a symbol edge
   lands on the nearest DAC sample, at most 3 % of a symbol at 1200 Bd and
   hidden by the continuous-phase tone. G3RUH holds every symbol for a whole
   number of DAC samples instead, keeping every edge on the same grid: a
   fractional accumulator at four samples per symbol would move an occasional
   edge by a quarter of a symbol. It was the *receiver* that needed resolution.

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
   (``modem_measure_adc_rate()``), because every profile's PLL step assumes the
   *nominal* ADC rate and the gap is otherwise a steady-state error the DPLL
   must track for a whole transmission. Stations on the air transmit at their
   own nominal baud rate, so the receive correction is the ADC error alone. The
   real DAC alarm rate, known exactly from the timer configuration, is recorded
   too but used only when a G3RUH receiver hears this node's own transmitter
   (full duplex, the wire loopback self-test). Both clocks derive from the same
   crystal, so the ratios are fixed board properties: measured **once per
   boot**, reapplied on every profile switch.

**The decimation FIR filters in place.**
   Output sample *i* is written to ``buf[i]`` while the taps read the window
   ending at ``buf[i × MODEM_RESAMPLE_RATIO]``, so for any ratio ≥ 2 the write
   pointer stays behind the read window except for the first
   ``FILTER_TAPS − 1`` slots. Those few leading samples, together with the
   previous block's tail, are staged in a short stack array before the loop
   starts; the rest of the block is read raw from ``buf[]`` itself. That is the
   whole reason the RX path holds no second copy of the 20 ms block.

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
priority ∈ 1..3. Two ``_Static_assert``\ s in ``afsk.c`` pin the decimator's
in-place invariant: ``MODEM_RESAMPLE_RATIO`` ≥ 2 unless the filter is a single
tap, and ``MODEM_BLOCK_SIZE`` ≥ 2 × (``FILTER_TAPS`` − 1) so the leading and
trailing history runs cannot overlap.

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
     - −1
     - PTT pin (board wiring). The header default is −1 (disabled); this
       project's top-level ``CMakeLists.txt`` overrides it to 26.
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
   * - ``MODEM_RX_MAX_DEMODULATORS``
     - 3
     - parallel 1200 Bd demodulators, 3..8
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
   * - ``src/afsk.c`` (~1710 ln)
     - ADC DMA ingest, receive gate and hold ring, decimation FIR, high-pass,
       AGC, DAC ISR, PTT
   * - ``src/modem.c`` (~1070 ln)
     - prefilter design and demodulator sets, correlators, DPLL, tone tables,
       DCD, twist estimate, calibration
   * - ``src/ax25.c`` (~1910 ln)
     - HDLC framer, NRZI, bit-stuffing, duplicate suppression, bit repair,
       AX.25 codec, TX queue
   * - ``src/esp32idf_radioamateur_modem.c`` (~590 ln)
     - the component's public API: ``modem_init()``/``modem_set_modem()``, the
       TNC2 helpers, and the ``modem_svc`` task that drives TX and delivers
       decoded frames to the RX callback
   * - ``src/fx25.c``, ``lwfec/rs.c``, ``lwfec/gf.c``
     - FX.25 Reed–Solomon FEC
   * - ``src/crc_ccit.c``
     - FCS (frame check sequence)
