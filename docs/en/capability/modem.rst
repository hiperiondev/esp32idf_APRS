.. _en-modem:

==============
The Soft-Modem
==============

The ``esp32idf_radioamateur_modem`` component (vendored under ``components/``,
GPL-3.0) is the heart of the project: a complete AFSK/FSK soft-modem that
demodulates and modulates APRS audio entirely on the ESP32, using only the
SAR-ADC, the DAC and a GPTimer. This chapter covers the modem as a *capability*
— its profiles, its public API and its runtime configuration. For the DSP
internals and the reasoning behind the sample-rate and core choices, see
:ref:`en-dsp-signal-chain`.

Modem profiles
==============

The selectable profiles (``modem_mode_t``) are numbered identically to the
web-admin's *modulation* dropdown, which is why the application can cast the
saved value straight to the enum:

.. list-table::
   :header-rows: 1
   :widths: 10 30 16 44

   * - Value
     - Profile
     - Baud
     - Tones
   * - 0
     - AFSK300
     - 300
     - 1600 / 1800 Hz
   * - 1
     - **Bell 202** (default, standard APRS)
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

The 1200 Bd profile runs **two demodulators in parallel**, tuned slightly
differently, to raise decode probability
(``MODEM_MAX_DEMODULATOR_COUNT = 2``).

FX.25 forward error correction
==============================

FX.25 wraps AX.25 in a Reed–Solomon code, letting the receiver correct bit
errors that would otherwise fail the CRC. It is fully backward compatible: an
FX.25 frame carries a normal AX.25 frame inside a correlation-tagged RS block,
so plain-AX.25 receivers still decode the inner frame. The mode is selectable:
``0`` = off, ``1`` = RX only, ``2`` = RX+TX (requires ``-DENABLE_FX25`` at
build time). The RS implementation lives in ``lwfec/`` (``rs.c``, ``gf.c``).

Public API
==========

The component's public header (``esp32idf_radioamateur_modem.h``) exposes:

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - Function
     - Purpose
   * - ``modem_init(cfg)``
     - Bring up the hardware and start the internal service tasks. Blocks ~5 s
       once per boot calibrating the real ADC clock.
   * - ``modem_deinit()``
     - Tear down.
   * - ``modem_set_modem(cfg)``
     - Change the active profile and related settings at runtime.
   * - ``modem_set_rx_callback(cb, ctx)``
     - Install the callback invoked for every decoded frame.
   * - ``modem_send_raw(frame, len)``
     - Queue a raw AX.25 frame (no flags/stuffing/FCS — all added
       automatically).
   * - ``modem_build_frame_tnc2(tnc2, out, out_len)``
     - Build a raw frame from a TNC2 monitor string.
   * - ``modem_send_tnc2(tnc2)``
     - Build + queue in one call.
   * - ``modem_format_tnc2(msg, out, out_len)``
     - Render a decoded frame back to a TNC2 string.
   * - ``modem_tx_busy()`` / ``modem_tx_queue_depth()``
     - TX-ring status, used by the RF TX backlog cap.
   * - ``modem_measure_adc_rate()``
     - Measure the real ADC sample rate.

A LibAPRS-style convenience layer (``APRS_setCallsign``, ``APRS_setPath1/2``,
``APRS_setSymbol``, ``APRS_setPower/Height/Gain/Directivity``, ``APRS_sendLoc``,
``APRS_sendMsg``, ``APRS_sendPkt``, ``APRS_printSettings``) is also provided for
compatibility.

Runtime configuration (``modem_config_t``)
==========================================

Built in exactly one place — ``aprs_service_build_modem_config()`` — shared by
boot, the Radio page's Save (live re-apply, no reboot) and the loop test:

.. list-table::
   :header-rows: 1
   :widths: 24 30 46

   * - Field
     - Source
     - Notes
   * - ``modem``
     - ``afsk_modem_type``
     - plain cast; page clamps 0–3
   * - ``flat_audio``
     - ``audio_lpf``
     - despite the name, always the flat-audio-input flag
   * - ``full_duplex``
     - ``false`` normally
     - LOOP TEST passes ``true`` (a DAC→ADC wire means CSMA never sees a clear
       channel)
   * - ``allow_non_aprs``
     - ``false``
     - accept non-0x03/0xF0 Control/PID?
   * - ``preamble_ms``
     - ``preamble`` (300)
     - TXDelay
   * - ``slot_time_ms``
     - ``tx_timeslot`` (2000)
     - CSMA quiet time
   * - ``fx25_mode``
     - ``fx25_mode``
     - 0=off, 1=RX only, 2=RX+TX
   * - ``ptt_active_high``
     - ``MODEM_PTT_ACTIVE_HIGH``
     - compile-time board wiring, not a config field
   * - ``min_unkey_ms``
     - ``ptt_min_unkey_ms``
     - extra minimum PTT-off hold time between transmissions

.. note::

   The PTT GPIO is **not** a field of ``modem_config_t`` — it is a fixed
   compile-time board wiring choice (``MODEM_PTT_GPIO``), like the ADC/DAC
   pins. Only the active *level* is passed at runtime, and it too comes
   straight from the compile-time macro. Explicitly **not** runtime-mapped
   (no equivalent in the component): ADC/DAC pins and attenuation, hardware
   squelch, RF power switch, software squelch, RX volume and the AGC ceiling.

The LOOP TEST
=============

The single most useful bring-up tool in the project. Wire **GPIO25 → GPIO33**,
open *Radio / Modem*, hit **LOOP TEST**. ``aprs_loop_test_run()``:

#. Builds a small APRS packet carrying a **random one-time token**
   (``>LOOPTEST <token>``).
#. **Diverts** decoded frames to its own hook so the test frame is never
   digipeated, uplinked, or logged as real traffic.
#. Switches the modem to **full duplex** — a DAC→ADC wire means the node always
   hears its own carrier and CSMA would never key up.
#. Transmits, then waits up to **4000 ms** for the ADC → demodulator → HDLC →
   AX.25 chain to hand the same frame back.
#. **Always restores** the real hook and the configured duplex mode before
   returning.

Meanwhile a monitor task latches diagnostics the component exposes only
instantaneously: a passive raw-ADC snapshot mid-preamble, peak RMS, peak AGC
gain, a DCD bitmap, and the furthest HDLC RX stage reached per demodulator. The
result message distinguishes:

.. list-table::
   :header-rows: 1
   :widths: 46 54

   * - Symptom
     - Diagnosis
   * - raw ADC min ≈ max
     - ADC dead / not wired
   * - raw swings, RMS ~0
     - no tone reaching the ADC
   * - RMS fine, DCD never set
     - PLL never locked → baud/modem-type mismatch or bad audio
   * - DCD latched, stage < FRAME
     - flags seen but no frame started — bit-recovery issue, not noise
   * - DCD latched, stage = FRAME, no frame
     - frames assembled but failed CRC — marginal level/SNR
   * - frame back, token mismatch
     - distortion, clipping, or wrong loopback wiring
   * - PASS
     - reports the RX level in mV RMS
