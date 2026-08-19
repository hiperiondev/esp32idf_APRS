.. _en-hardware:

========
Hardware
========

Supported target
================

* **ESP32** (classic, Xtensa dual-core) — ``CONFIG_IDF_TARGET=esp32``, 4 MB
  flash.
* Dual-core is **not optional**: the ADC ISR and the DAC sample clock are
  pinned to *different* cores on purpose (see :ref:`en-dsp-signal-chain`).
* ESP32-S2 has DACs on GPIO17/18 and would need the config header adjusted.
  **ESP32-S3/C3/C6/H2 have no DAC at all** and cannot run the TX path
  unmodified.

Pinout / board definition
=========================

The board definition lives in the **top-level** ``CMakeLists.txt``, applied
*before* ``project()`` via ``idf_build_set_property(COMPILE_DEFINITIONS …
APPEND)``:

.. code-block:: cmake

   idf_build_set_property(COMPILE_DEFINITIONS "MODEM_ADC_GPIO=33"       APPEND)
   idf_build_set_property(COMPILE_DEFINITIONS "MODEM_DAC_GPIO=25"       APPEND)
   idf_build_set_property(COMPILE_DEFINITIONS "MODEM_PTT_GPIO=26"       APPEND)
   idf_build_set_property(COMPILE_DEFINITIONS "MODEM_PTT_ACTIVE_HIGH=1" APPEND)
   idf_build_set_property(COMPILE_DEFINITIONS "MODEM_LED_TX_GPIO=-1"    APPEND)
   idf_build_set_property(COMPILE_DEFINITIONS "MODEM_LED_RX_GPIO=-1"    APPEND)

.. list-table::
   :header-rows: 1
   :widths: 20 20 60

   * - Signal
     - Default
     - Hard constraints
   * - **Audio in (ADC)**
     - ``GPIO33`` (ADC1_CH5)
     - **32–39 only.** ADC2 is unusable while Wi-Fi is up, and this firmware
       always has Wi-Fi up. Enforced by a compile-time ``#error``.
   * - **Audio out (DAC)**
     - ``GPIO25`` (DAC_CHAN_0)
     - **25 or 26 only.** The ESP32 DAC is hard-wired to those pads and is not
       routable through the GPIO matrix. Enforced by ``#error``.
   * - **PTT**
     - ``GPIO26``
     - Any output-capable GPIO not equal to the ADC/DAC pins; rejects
       input-only GPIO34–39 and flash/PSRAM GPIO6–11. ``-1`` disables it.
       Both the pin **and its polarity** are compile-time constants.
   * - **TX / RX LEDs**
     - disabled (``-1``)
     - Any output-capable GPIO.
   * - **GNSS receive (UART RX)**
     - ``GPIO16``
     - Wired to the module's TX output. Any input-capable GPIO the UART
       matrix can reach. **Unusable on an ESP32-WROVER**, where GPIO16/17
       belong to the SPI PSRAM die. Set in ``main/include/gps.h``.
   * - **GNSS transmit (UART TX)**
     - ``GPIO17``
     - Wired to the module's RX input. Nothing is ever sent on it — the
       firmware never configures the receiver — but the pin is still
       reserved, since it is physically connected to that input.

Wiring to a radio
=================

Neither end of the audio link can be connected directly to the other. The
ESP32 side is a 3.3 V, DC-biased, sampled-data interface; the radio side is an
AC, ground-referenced, millivolt-level analogue interface. Three things have
to happen in between: **attenuate** (TX), **shift and clamp** (RX), and
**switch** (PTT).

What each end presents
----------------------

Every ESP32-side figure below is derived from the modem component's own
compile-time constants, not from a datasheet ideal.

.. list-table::
   :header-rows: 1
   :widths: 26 50 24

   * - Node
     - What is really there
     - From
   * - **GPIO25 (DAC), transmitting**
     - 1.65 V DC with a ≈1.97 Vpp swing on top ⇒ ≈0.70 Vrms for a sine, plus
       reconstruction images around 38.4 kHz
     - ``DAC_MID=128``, ``AMPLITUDE_PCT=60``, ``DAC_SAMPLERATE=38400``
   * - **GPIO33 (ADC)**
     - Window 0–3.1 V; AGC targets 310 mVrms at the pin, reaches it from as
       little as ≈39 mVrms, holds below ≈16 mVrms, clips above ≈1.1 Vrms
     - ``ADC_ATTEN_DB_12``, ``AGC_TARGET_RMS=0.2``
   * - Rig **MIC IN**
     - 5–20 mVrms, often pre-emphasised, DC bias for the electret
     - needs ≈30–40 dB of pad
   * - Rig **DATA IN** (mini-DIN-6)
     - ≈40 mVpp ⇒ ≈14 mVrms, flat, no pre-emphasis
     - needs ≈35 dB of pad
   * - Rig **SPKR / AF OUT**
     - 0.1–3 Vrms, volume-knob dependent, de-emphasised
     - needs a pad + bias
   * - Rig **DATA OUT / DISC**
     - 100–300 mVrms, fixed level, squelch-independent, flat
     - needs bias only — this is the good one

Two consequences worth internalising before soldering:

* **The DAC is ~35 dB too hot** for anything on the radio. A direct
  connection will not merely over-deviate, it will splatter.
* **A DATA OUT port is already inside the AGC window** (100–300 mVrms vs. the
  39 mVrms–1.1 Vrms usable range). If your rig has a data jack, the RX side is
  a bias network and nothing else — no pot, no gain.

Minimal functional schematic
----------------------------

Passive, ~15 parts, no op-amps.

.. image:: ../schematics/TX.png
   :alt: TX schematic — ESP32 DAC to rig audio input
   :align: center
   :width: 100%

.. image:: ../schematics/RX.png
   :alt: RX schematic — rig audio output to ESP32 ADC
   :align: center
   :width: 100%

.. image:: ../schematics/PTT_opto.png
   :alt: PTT schematic, option A — optocoupler (PC817), isolated
   :align: center
   :width: 70%

.. image:: ../schematics/PTT_tr.png
   :alt: PTT schematic, option B — NPN transistor (2N2222/BC547), non-isolated
   :align: center
   :width: 70%

Key component roles: R1/R2 + C2/C3 form a two-pole reconstruction low-pass
(fc ≈ 4.8 kHz) that kills the 38.4 kHz DAC images; C1 blocks the DAC's 1.65 V
idle bias from the mic input; R3/RV1 pad and trim the TX level; R5/R6 set a
mid-rail 1.65 V bias for the ADC; R7/C5 snubs the SAR sampling-cap charge
kick; D1/D2 clamp the ADC pin to the rails. For **9600 Bd G3RUH** replace
C2/C3 with 10 nF (fc ≈ 7.2 kHz) to keep the audio flat past ~5 kHz.

.. warning::

   **The PTT default is a trap.** The shipped board definition is
   ``MODEM_PTT_ACTIVE_HIGH=1`` in this revision. Pick a PTT driver whose
   polarity matches the config, or change the config to match your driver. An
   opto (option A) inverts and suits ``ACTIVE_HIGH=0``; a plain NPN low-side
   switch (option B) does not invert and needs ``ACTIVE_HIGH=1``. Always
   verify with a meter that the PTT line is *open* through reset, through the
   whole ~5 s boot, and while idle **before connecting the radio** —
   ``modem_init()`` blocks ~5 s calibrating the ADC clock and beacons transmit
   on entry, so a wrong-polarity PTT gives you seconds of unmodulated carrier.

Baofeng UV-5R and K-plug HTs
----------------------------

The UV-5R (and Kenwood-K1-style two-pin clones: UV-82, BF-888, GT-3, RT-5R…)
exposes two plugs rather than a combined jack:

.. list-table::
   :header-rows: 1
   :widths: 22 22 56

   * - Plug
     - Size
     - Signal
   * - Large
     - 3.5 mm TS
     - Tip = SPKR audio out, Sleeve = GND
   * - Small
     - 2.5 mm TRS
     - Tip = MIC in, Ring = PTT (short to sleeve to key), Sleeve = GND

There is **no separate UV-5R circuit** — build the minimal schematic exactly
as above; only where the three off-board leads land changes, because the rig
splits "rig" across two plugs. Note that these HTs have **no discriminator
jack**, so 9600 Bd G3RUH is out of reach; **AFSK 1200 Bd Bell 202 is the
realistic ceiling** through the stock 2-pin connector. Verify the plug pinout
with a meter before soldering — cheap aftermarket cables sometimes swap mic
and PTT.

Bring-up order
==============

#. **Loop test first, no radio.** Wire GPIO25 → GPIO33 with a plain wire (see
   :ref:`en-web-admin` and the LOOP TEST). If that fails, no external circuit
   will help.
#. **RX next, still no TX.** Open the squelch, feed real traffic in, and watch
   the **AUDIO** column of the live traffic table (the modem's own mVrms at
   the pin). Trim RV2 for **≈300 mVrms on packets** — the AGC's target, where
   the loop sits at unity with the most headroom.
#. **TX last, into a dummy load.** Set RV1 for **≈3.0 kHz deviation**
   (2.5–3.5 kHz). Over-deviation is the single most common cause of "my igate
   hears everyone but nobody hears me".
#. **9600 Bd G3RUH** needs the flat/discriminator path at both ends: DATA
   IN/DATA OUT, 10 nF in C2/C3, and the *Audio low-pass filter* checkbox set
   for flat audio.

Isolation and ground loops
==========================

The passive circuit shares a ground with the radio, the normal source of hum,
alternator whine and "it works until I transmit". If you hear any of that, use
600:600 Ω audio isolation transformers in place of C1 and C4, keep the opto
(option A) so the PTT return does not re-create the ground you just broke, and
fight RF ingress with shielded cable, short leads, a clamp-on ferrite at the
rig connector and 47–100 pF from each audio line to the rig's chassis.

.. _en-loop-test-tuning:

Tuning procedure
=================

Reference points
-----------------

.. list-table::
   :header-rows: 1
   :widths: 34 20 46

   * - Signal
     - GPIO
     - Function
   * - DAC (TX audio out)
     - GPIO25
     - AFSK transmit audio
   * - ADC (RX audio in)
     - GPIO33
     - AFSK receive audio
   * - PTT out
     - GPIO26
     - Radio keying

.. list-table::
   :header-rows: 1
   :widths: 12 12 30 46

   * - Trimmer
     - Value
     - Location
     - Function
   * - **RV1**
     - 1 kΩ
     - Between DAC and the interface's ``MIC`` output terminal
     - **TX audio level** — sets drive strength into the radio's mic/audio-in
   * - **RV2**
     - 10 kΩ
     - Between the interface's ``SPKR`` input terminal and the ADC
     - **RX audio level** — attenuates the radio's speaker/audio-out down to
       what the ADC expects

1. Method A — Bare modem loopback test
----------------------------------------

Use this to verify the ESP32's own DAC/ADC/AX.25 chain in isolation, before
involving the interface board.

1.1 Minimal schematic
^^^^^^^^^^^^^^^^^^^^^^

.. image:: /_static/tuning/tuning_1_1_en.png
   :alt: Minimal loopback schematic — ESP32 GPIO25 (DAC1) jumpered to GPIO33 (ADC1)
   :align: center
   :width: 80%

One jumper wire, GPIO25 → GPIO33. Both pins are 0–3.3 V single-ended and
share the board's ground plane, so no coupling capacitor or attenuator is
required for a pass/fail result.

1.2 Procedure
^^^^^^^^^^^^^^

#. Wire the jumper as above (power off first).
#. Enable the audio modem in the web UI (Radio/Modem page → "Enable audio
   ADC/DAC modem" → Save → reboot).
#. Open the serial console to observe results.
#. Run the self-test from the Radio/Modem page's **LOOP TEST** button (or the
   equivalent web endpoint).
#. Read the resulting ``PASS``/``FAIL`` line, which reports RX level (mV
   RMS), raw ADC swing, and AGC gain, plus — on failure — a specific
   stage-by-stage diagnosis.
#. Repeat 3–5 times to rule out an intermittent connection before drawing
   conclusions.

1.3 Interpreting the result
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - Result
     - Meaning
   * - ``PASS``
     - Full TX→RX→HDLC→CRC path is functionally correct
   * - ``FAIL``, ADC never sampled
     - ADC driver/init issue, not the jumper
   * - ``FAIL``, flat/near-DC line
     - No signal reaching the ADC — check the jumper and grounds
   * - ``FAIL``, signal present but no lock
     - Tone reaches the ADC but the demodulator's PLL never syncs — check AGC
       gain (near 1.0× points at the AGC path) or the baud-rate/modem-type
       setting
   * - ``FAIL``, PLL locked, no frame (stage < FRAME)
     - Bit-sync never starts a frame — deeper bit-recovery issue
   * - ``FAIL``, PLL locked, no frame (stage = FRAME)
     - Framing works but CRC fails — marginal signal/clipping
   * - ``FAIL``, content mismatch
     - Frame received but corrupted — distortion, clipping, or a loose
       loopback wire

2. Method B — Interface-board loopback (through RV1/RV2)
-----------------------------------------------------------

Use this to validate — and tune — the actual audio interface board,
including both level trimmers, with no radio attached.

2.1 Minimal schematic
^^^^^^^^^^^^^^^^^^^^^^

.. image:: /_static/tuning/tuning_2_1_en.png
   :alt: Minimal interface-board loopback schematic — ESP32 DAC/ADC/PTT wired to the APRS_Radio_Interface board, with a jumper from MIC out to SPKR in
   :align: center
   :width: 100%

Wire the interface board to the ESP32 exactly as for normal operation
(DAC/ADC/PTT), then add **one extra jumper on the interface board itself,
from its** ``MIC`` **output terminal to its** ``SPKR`` **input terminal.**
No radio is connected. This closes the loop through the full analog chain —
both trimmers, coupling caps, and level networks.

2.2 What to read
^^^^^^^^^^^^^^^^^^

Each self-test run (§1.2, step 4) now reports, whether it passes or fails:

* **RX level**, in mV RMS
* **Raw ADC swing** (min–max, out of a 0–4095 12-bit range) — the
  clipping-margin indicator
* **AGC peak gain**

These three numbers are what stand in for an oscilloscope trace when tuning
the trimmers below.

3. Trimmer tuning procedure (RV1 then RV2)
----------------------------------------------

Perform this with the loop wired as in §2.1, running the self-test after
every adjustment.

#. **Start low.** Set RV1 (TX level) near minimum and RV2 (RX level) at
   roughly mid-travel.
#. **Sweep RV1 upward**, running the test at each step:

   * **Too low:** ``FAIL`` — weak/no PLL lock, small raw ADC swing, low RMS.
   * **Too high:** raw ADC swing approaches the rails (toward 0 and/or
     4095), eventually causing a content-mismatch or CRC failure.
   * Note the **range of RV1 positions that give a clean** ``PASS`` with the
     swing comfortably clear of both rails.

#. **Set RV1 to the middle of that passing range** — not an edge — so
   there's equal margin against a weak connection on one side and clipping
   on the other.
#. **With RV1 fixed, sweep RV2** the same way:

   * **Too low:** weak RMS, marginal or absent DCD lock.
   * **Too high:** swing nears the rails, distortion/CRC failures.
   * Set RV2 to the middle of its own passing range.

#. **Re-verify.** Run the self-test 3–5 times back to back at the chosen
   settings. All runs should ``PASS``, with the swing staying clear of both
   rails and RMS comfortably above the "no lock" threshold seen during the
   sweep.
#. **Record the final positions** (e.g. "RV1: 40% from minimum, RV2: 55%
   from minimum") for future reference, since trimmers can drift or get
   bumped.

No scope is needed at any point: the swing/RMS/AGC readings from each
self-test run substitute for it, and the PASS/FAIL boundary itself marks the
edges of each trimmer's usable window.

Partition table
===============

The firmware ships an OTA-enabled 4 MB layout (``partitions.csv``):

.. list-table::
   :header-rows: 1
   :widths: 20 12 12 16 14 26

   * - Name
     - Type
     - SubType
     - Offset
     - Size
     - Notes
   * - ``nvs``
     - data
     - nvs
     - 0x9000
     - 24 K
     -
   * - ``otadata``
     - data
     - ota
     - 0xF000
     - 8 K
     -
   * - ``phy_init``
     - data
     - phy
     - 0x11000
     - 4 K
     -
   * - ``ota_0``
     - app
     - ota_0
     - 0x20000
     - **1728 K**
     - first app slot
   * - ``ota_1``
     - app
     - ota_1
     - 0x1D0000
     - **1728 K**
     - second app slot
   * - ``storage``
     - data
     - spiffs
     - 0x380000
     - **512 K**
     - mounted as **LittleFS** at ``/storage``

Two app slots let the web-admin OTA update flash whichever slot is not
currently running and roll back automatically if the new image fails the
post-boot self-check. A device still on the old single-``factory`` table needs
one serial reflash to migrate onto this layout; every update after that can go
through the web admin.
