.. _en-radiomodem:

==========
Radiomodem
==========

*Radiomodem* is the web-admin page that connects the firmware to the radio. It
is where the on-air modulation is chosen, where channel access is timed, where
the station's own airtime is bounded, and where the electrical relationship
between the ESP32's ADC/DAC pins and the transceiver is described. Every other
page decides *what* is transmitted; this one decides *how*, *when* and *how
loud*.

This chapter documents every control on the page, one at a time: what it is,
what it actually changes inside the firmware, how to choose a value, worked
examples, and the mistakes each setting can cause. For the DSP and the modem
component behind it, see :ref:`en-modem` and :ref:`en-dsp-signal-chain`.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Property
     - Value
   * - Menu entry
     - **Radiomodem**
   * - Routes
     - ``GET /radio``, ``POST /radio``, plus ``POST /radio/looptest``,
       ``POST /radio/level`` and ``POST /radio/txtest`` for the three
       diagnostic buttons.
   * - Handler
     - ``components/webconfig/pages/page_radio.c``
   * - Stored in
     - ``/storage/radio.json`` (downloadable and uploadable from the *Storage*
       page)
   * - Applied
     - Live on *Save* for everything except **Enable audio ADC/DAC modem** and
       **Transmit sample rate**, which need a reboot.

Why some fields need a reboot
=============================

``page_radio_post()`` writes the whole form to ``g_config`` under one lock,
saves ``radio.json`` and then calls ``aprs_service_apply_modem_config()``,
which pushes the new settings into the running modem through
``modem_set_modem()``. Everything the modem can accept while it is running
therefore takes effect the instant *Save* returns — no reboot, no dropped
frames, no service restart.

Two settings are outside that path:

* **Enable audio ADC/DAC modem** — ``modem_init()`` runs exactly once, from
  ``main.c``, and only when this switch was already on at boot. Turning it on
  saves the setting but brings no hardware up until the next restart.
* **Transmit sample rate** — the sample-clock period and every phase increment
  derived from it are programmed while the modem hardware is stopped, so
  ``afskSetDacSampleRate()`` refuses to run on a live modem and the value is
  applied by the next ``modem_init()``.

In both cases the form keeps showing the value you saved, which is the value the
next boot will start the modem with, not the one the modem is running with right
now.

.. warning::

   Three buttons on this page — **LOOP TEST**, **RX LEVEL** and **TX TEST** —
   save the form before they run, exactly as if *Save* had been pressed. Do not
   press them to "try" a setting you do not intend to keep. Two of the three
   also key the transmitter.

Protocol
========

FX.25 (forward-error-corrected AX.25)
-------------------------------------

**What it is.** FX.25 wraps an ordinary AX.25 frame in a Reed–Solomon block
with a correlation tag in front of it. A receiver that understands FX.25 can
repair bit errors that would otherwise fail the frame's CRC; a receiver that
does not understand it still finds the plain AX.25 frame inside and decodes it
normally. It is a compatible extension, not a different protocol.

**What the dropdown does here.** A three-option selector stores one of
``fx25Mode = 0``, ``1`` or ``2``, which ``Ax25Init()`` reads directly:

* *Off* (``0``, the default) — plain AX.25 in both directions
  (``Ax25Config.fx25 = 0``, ``Ax25Config.fx25Tx = 0``).
* *Receive only* (``1``) — *FX.25 on receive, plain AX.25 on transmit*
  (``Ax25Config.fx25 = 1``, ``Ax25Config.fx25Tx = 0``). This buys the station
  better decoding of FX.25 stations it hears without adding redundancy to what
  it transmits.
* *Receive and transmit* (``2``) — FX.25 on both directions
  (``Ax25Config.fx25 = 1``, ``Ax25Config.fx25Tx = 1``). This station also wraps
  its own outgoing frames in the Reed–Solomon block, so any neighbour that
  understands FX.25 gets the same error correction from it.

**Second effect, easy to miss.** With flat audio selected (below), FX.25 also
changes which prefilter the first 1200 Bd demodulator runs: without FX.25 it
applies de-emphasis to undo the transmitting station's pre-emphasis; with FX.25
it runs the plain inverse bandpass instead, on the reasoning that the code's
redundancy already covers the small SNR loss. Toggling FX.25 therefore alters
receive behaviour even on a channel where nobody transmits FX.25.

**When to enable it.**

* *Almost always, on a normal APRS channel.* The cost is CPU time in the
  receiver and nothing on the air.
* *Definitely*, on a weak or noisy path where you are hearing partial packets,
  if any neighbour transmits FX.25.
* *Leave it off* while you are chasing a receive problem and want the simplest
  possible signal chain, or when you are A/B-testing de-emphasis behaviour with
  flat audio.

.. warning::

   Enabling FX.25 while a marginal receive path is being tuned makes the two
   demodulators behave differently than they did a moment ago. Tune the audio
   first with FX.25 off, then turn it on and confirm the decode rate improved
   rather than the reverse.

Audio / AFSK
============

Enable audio ADC/DAC modem
--------------------------

**What it is.** The master switch for the on-chip soft-modem. With it on, the
ESP32 itself is the TNC: the SAR-ADC listens to the radio's receive audio, the
DAC generates the transmit audio, and a GPIO keys PTT. With it off,
``modem_init()`` is never called, no audio tasks run, no ADC or DAC is
configured, and ``aprs_service_can_transmit()`` returns false — every RF
transmission the firmware attempts is dropped at the source and counted under
``DROP_MODEM_NOT_READY``.

**Uses.**

* *On* — the normal case: a complete standalone station (IGate, digipeater,
  tracker, weather, telemetry) with a radio attached.
* *Off* — an **internet-only** station. An IGate that only forwards APRS-IS to
  Telegram, a receive-only Winlink client over the internet, or a bench unit
  with no radio at all. Turning the modem off frees the audio tasks and their
  CPU share, and makes it impossible to key anything by accident.

.. warning::

   This is the one field on the page whose change does **nothing** until the
   device is restarted. Tick it, *Save*, then reboot from the *About /
   Firmware* page. The LOOP TEST and TX TEST buttons detect this state and say
   so explicitly rather than failing obscurely.

Modulation
----------

**What it is.** The on-air modulation and baud rate used for both receive and
transmit.

.. list-table::
   :header-rows: 1
   :widths: 8 26 10 22 34

   * - Value
     - Profile
     - Baud
     - Tones
     - Where it is used
   * - 0
     - AFSK300
     - 300
     - 1600 / 1800 Hz
     - HF packet (30 m, 20 m). Narrow enough for an SSB channel.
   * - 1
     - **Bell 202** (default)
     - 1200
     - 1200 / 2200 Hz
     - **Standard APRS worldwide** — 144.390 MHz in North America, 144.800 MHz
       in most of Europe, 145.175 MHz in Australia, and so on.
   * - 2
     - ITU V.23
     - 1200
     - 1300 / 2100 Hz
     - Legacy European packet links. Not interoperable with Bell 202.
   * - 3
     - G3RUH FSK
     - 9600
     - direct FSK
     - High-speed links, satellite work. Requires a flat audio path in both
       directions.

**What it changes internally.** ``ModemInit()`` rebuilds the whole demodulator
chain: filter coefficients, PLL step, DCD thresholds, and the number of
demodulators. Both 1200 Bd profiles run **two demodulators in parallel** with
different prefilters, so a frame that one path misses may still be recovered by
the other; 300 Bd and 9600 Bd run a single demodulator.

**Examples.**

* A home IGate on the national APRS frequency → **1200 Bd Bell 202**. Do not
  change this. Anything else makes the station deaf and mute on that channel.
* A 9600 Bd packet link to a neighbouring site over a dedicated simplex
  frequency → **9600 Bd G3RUH**, with *Flat / discriminator audio input*
  ticked and a data-port connection at both ends.
* An HF gateway on 10.147.6 MHz USB → **300 Bd AFSK**.

.. warning::

   The modulation must match every station you intend to work. There is no
   automatic detection and no fallback: a mismatch means nothing decodes in
   either direction, and your transmissions will be heard as noise by everyone
   else on the channel. If a working station suddenly stops decoding anything,
   this is the first field to check.

.. warning::

   9600 Bd G3RUH will not pass through a radio's microphone and speaker jacks.
   The pre-emphasis, de-emphasis and audio bandwidth of a voice path destroy
   it. It requires a true flat data port (a "9600 packet" or discriminator
   connection) on both the transmit and the receive side.

Audio hardware (compile-time)
-----------------------------

Below the modulation selector a single read-only block is shown. It is not a
setting: it is the board definition the firmware was built with, so that a
wiring choice can be told apart from a saved one at a glance. It reports the DAC
output pin (``MODEM_DAC_GPIO``, default GPIO25), the ADC input pin
(``MODEM_ADC_GPIO``, default GPIO33), the PTT pin (``MODEM_PTT_GPIO``,
``Disabled`` when it is -1), PTT active-high (``MODEM_PTT_ACTIVE_HIGH``), the
ADC attenuation (``MODEM_ADC_ATTEN``) and the ADC sample rate
(``MODEM_ADC_SAMPLERATE``) the firmware was built with. The transmit sample
rate is not listed: it is a saved setting, chosen by *Transmit sample rate*
further down this page.

All of the compile-time values come from the top-level ``CMakeLists.txt`` and
can only be changed by rebuilding the firmware — for example
``idf.py build -DMODEM_ADC_GPIO=32``. The PTT pin is registered in the admin's
GPIO ownership table, so it shows up as *used — PTT* in every other GPIO picker
on the web admin (the message alarm output, for instance), which is what stops
two features from claiming the same pin.

.. note::

   The DAC pin can only be GPIO25 (DAC1) or GPIO26 (DAC2); the ESP32's DAC is
   not routable to any other pin, and the build fails with an explicit error if
   another number is given.

Flat / discriminator audio input
--------------------------------

**What it is.** A statement about where the receive audio comes from, not a
filter you switch on for taste. It tells the demodulator whether the audio it
is given has already been de-emphasized.

* **Off** (default) — the audio comes from a **speaker or headphone jack**. A
  voice receiver's audio output is already de-emphasized and band-limited. The
  first demodulator therefore applies pre-emphasis and a normal bandpass.
* **On** — the audio comes from a **data port or the discriminator directly**.
  That signal is flat and unfiltered, and still carries the transmitting
  station's pre-emphasis. The first demodulator runs an inverse bandpass and
  (unless FX.25 is enabled) de-emphasis to undo it.

In both cases the second 1200 Bd demodulator stays on a different path, so the
pair always covers two distinct equalizations.

**Examples.**

* Baofeng UV-5R, audio taken from the 3.5 mm speaker jack → **off**.
* A mobile transceiver with a 6-pin mini-DIN data jack, using pin 4 (1200 Bd
  packet) → usually **off**; using the 9600 Bd discriminator pin → **on**.
* Any 9600 Bd G3RUH link → **on**, always.
* An SDR feeding demodulated FM audio with no de-emphasis in the chain →
  **on**.

.. warning::

   Getting this backwards does not stop decoding outright — it quietly halves
   the station's sensitivity. The symptom is a station that decodes strong
   local packets perfectly and misses everything weak. If your decode rate
   looks poor and the levels are correct, toggle this and compare.

.. tip::

   This is a legitimate thing to test empirically. Run **RX LEVEL** with each
   setting while real traffic is on the channel, and watch which one reports
   ``DCD yes`` more often and gives more decodes in the dashboard traffic
   table.

Preamble (ms)
-------------

**What it is.** AX.25 *TXDelay*: how long the transmitter is keyed, sending
nothing but HDLC flag bytes, before the first data bit of the frame. It gives
the receiving stations' squelch time to open, their AGC time to settle and
their demodulator PLLs time to lock.

**Range 50–2000 ms, default 300 ms.**

``Ax25TxDelay()`` converts milliseconds to a flag-byte count against the
current baud rate. At 1200 Bd, 8 bits take about 6.7 ms, so 300 ms is roughly
45 flag bytes and every extra 1000 ms adds about 150 more bytes of dead carrier
**in front of every single frame you transmit**.

**How to choose.**

* 300 ms is right for most modern transceivers on simplex.
* Raise it to 400–600 ms if your transmitter is slow to come up to full power,
  if you are going through a repeater, or if distant stations report hearing
  your carrier but decoding only some of your frames.
* Raise it further, towards 800–1000 ms, only for a genuinely slow PTT path — a
  relay-switched amplifier, a transverter, an old rig with a slow T/R
  changeover.
* Lower it towards 150–200 ms only on a fast, modern, direct-connected radio,
  and only if you have confirmed with a neighbour that your frames still decode.

.. warning::

   A long preamble is a pure tax on a shared channel. At 1200 Bd, an APRS
   position report is typically on the air for around half a second; a 2000 ms
   preamble means five sixths of every key-up is dead carrier. On a busy
   frequency this is one of the fastest ways to become the station everyone
   else complains about.

.. warning::

   Too *short* a preamble is the classic cause of "some stations never hear
   me". The frame starts before their squelch has opened, so its first bytes —
   including the address field — are lost, and the frame fails without ever
   being counted as an error anywhere you can see it. If you are being
   digipeated inconsistently, try raising this before anything else.

TX time-slot (ms)
-----------------

**What it is.** Despite the name, this field sets the modem's **quiet time**:
``Ax25TimeSlot()`` writes it to ``Ax25Config.quietTime``, the mandatory settle
period the transmit scheduler observes after a transmission (and at modem
start-up) before it will consider keying up again. When a non-zero value is
set, a random 100–1000 ms jitter is added to the first deadline, so two
identically configured stations that come up together do not stay in lockstep.

**Range 0–10000 ms, default 2000 ms.** Setting it to 0 clears the deadline
entirely: the modem will transmit as soon as the channel is heard clear and the
persistence roll succeeds.

.. note::

   The interval between persistence rolls — the classic AX.25 *SlotTime* — is
   a fixed 100 ms inside the modem and is not exposed on this page. This field
   is the quiet time on top of it.

**Examples.**

* Busy metropolitan APRS channel, station also digipeating → keep 2000 ms or
  raise it to 3000 ms. The station will still answer messages promptly, because
  the quiet time only delays the *start* of a new key-up cycle.
* A quiet rural channel with a handful of stations → 1000 ms is comfortable.
* A dedicated point-to-point link on a private frequency with only two stations
  → 0 is reasonable; there is no contention to spread out.

.. warning::

   0 on a shared channel removes the settle period entirely and makes this
   station the most aggressive one on the frequency. Every other station backs
   off after transmitting; yours will not.

.. warning::

   Large values delay *everything*, including message acknowledgements and
   digipeat repeats, which the duty-cycle limiter below deliberately exempts.
   Above about 5000 ms the station starts to feel unresponsive to anyone trying
   to message it.

TX buffers
----------

**What it is.** How many frames may sit in the RF transmit ring — queued
waiting for a clear channel, or on the air right now — before a newly offered
frame is dropped instead of queued.

**Range 1–11, default 1.** The ceiling is the transmit ring's real usable depth
(``AX25_TX_FRAME_RING_MAX``), so the dropdown can never offer a value the ring
could not hold.

**What it protects against.** Without a cap, a burst — an IGate relaying
APRS-IS to RF faster than the channel clears, or a scheduler pass where several
periodic reports fall due together — would queue frames far faster than a
1200 Bd channel can drain them. Those frames would go on the air minutes later,
long after they stopped being true, or be dropped anyway once the ring filled.
Capping the backlog here drops the excess immediately and logs the reason.

**Examples.**

* Default **1** — the right answer for a tracker or a plain beaconing station.
  Position reports are only interesting when they are current; a stale one
  queued behind three others is worse than none.
* **2–3** — a busy digipeater or an IGate with INET→RF gating enabled, on a
  channel that clears in bursts. Absorbs a short pile-up without letting the
  queue grow stale.
* **8–11** — rarely appropriate. Only on a dedicated link where the channel is
  essentially always free and every frame genuinely must be delivered.

.. warning::

   Raising this does not make the channel faster. A queue that is full because
   the channel is busy will still be full; the frames will simply be older when
   they finally transmit. If frames are being dropped for backlog, reduce what
   the station transmits (longer beacon intervals, tighter IGate filters) rather
   than deepening the queue.

.. note::

   This affects only the RF transmit ring. The APRS-IS socket has its own,
   separate buffer, so a congested RF leg never blocks or drops the internet
   leg of the same packet.

Duty-cycle limiter and Duty-cycle limit (%)
-------------------------------------------

**What it is.** A ceiling on this station's *own cumulative transmit airtime*,
measured over a rolling **10-minute window** (split internally into forty
15-second buckets). It is completely independent of CSMA: CSMA stops you from
transmitting *over* someone, and has no memory of what you transmitted a minute
ago; this has memory and no opinion about the channel.

**Off by default. Ceiling range 1–100 %, default 25 %.**

**What happens at the ceiling.** Traffic is split in two:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Class
     - Behaviour once the ceiling is reached
   * - **Critical** — APRS messages and their acknowledgements, digipeat
       repeats
     - Transmitted anyway. Their airtime still counts toward the window; only
       the gate is skipped for them.
   * - **Non-critical** — own-station position beacons, objects and items,
       weather reports, telemetry, bulletins, bulk IGate INET→RF relay
     - Held back. Each of these is a periodic task that will offer the same
       report again on its next interval, so this is a deferral rather than a
       loss, although it is counted under ``DROP_TX_DUTY_CYCLE`` on the
       dashboard for visibility.

**Uses.**

* **Unattended stations.** In several band plans a duty-cycle ceiling on an
  automatic station is not a courtesy but a licence condition. This is where you
  enforce it.
* **Protecting the power amplifier.** A handheld or a small brick amplifier
  designed for intermittent voice use will overheat if a misconfigured
  scheduler keys it repeatedly. 20–25 % is a common safe figure for such
  hardware.
* **Solar or battery sites.** Transmit current dominates the power budget;
  a ceiling turns "how much power will this station use" into a number you can
  compute.
* **A safety net for your own mistakes.** A beacon interval accidentally set to
  10 seconds, a telemetry definition storm, or a runaway INET→RF filter cannot
  monopolise the frequency with the limiter on.

**Examples.**

* Mountain-top digipeater on mains power with a proper amplifier → enabled at
  **50 %**: generous, but still a hard stop against a runaway.
* Home IGate on a handheld → enabled at **20 %**. The handheld's finals will
  thank you.
* Solar-powered fill-in digipeater → enabled at **10 %**, with beacon intervals
  set long enough that the limiter is a backstop rather than a daily occurrence.
* A station under test on a dummy load → off.

.. warning::

   Because message traffic and digipeat repeats are exempt, the limiter is not
   a guarantee of maximum airtime. A digipeater in the middle of an emergency
   net will exceed its configured ceiling, by design. Do not treat the number
   as a regulatory guarantee if your licence conditions are absolute.

.. warning::

   A very low ceiling combined with frequent beacons means the beacons are
   silently deferred, sometimes for a long time. If aprs.fi shows your station
   updating far less often than your configured interval, check the dashboard
   drop counter before suspecting the radio.

PTT minimum unkey time (ms)
---------------------------

**What it is.** An extra guaranteed **unkeyed** gap between one transmission
and the next, on top of the fixed one-service-tick release hold-off (about
10 ms) that the modem always applies.

**Range 0–5000 ms, default 0 (no extra hold).**

**Why it exists.** The modem's own floor guarantees that PTT is visibly
released between frames, but some equipment needs considerably more:

* A repeater whose courtesy tone and squelch tail must finish before the next
  packet keys up, or the start of your frame lands on top of it.
* A transceiver with a relay-based T/R changeover that needs settling time, or
  it will clip the start of the next transmission.
* A linear amplifier with a sequencer, where re-keying too quickly hot-switches
  the relay.
* A radio whose own internal time-out or "anti-key-bounce" logic ignores a PTT
  assertion that arrives too soon after a release.

**Examples.**

* Direct connection to a modern handheld or mobile → **0**.
* Working through a repeater with a courtesy beep → **500–1000 ms**, long
  enough for the tail to clear.
* Relay-switched amplifier in the chain → **200–300 ms**.

.. warning::

   This delay applies between *all* consecutive transmissions, including the
   retries of an unacknowledged APRS message. Setting seconds here makes a
   multi-frame exchange noticeably slow.

CSMA persistence (p, 1–255)
---------------------------

**What it is.** The standard AX.25/KISS *Persist* parameter. Once the channel
is heard clear, the modem transmits on that slot with probability ``p/256``;
on a miss it waits one more slot and rolls again.

**Range 1–255, default 63** (≈ 24.6 % per clear slot, the conventional AX.25
default).

**Why randomness.** If every station keyed up the instant the channel went
quiet, every station that was waiting would collide at exactly the same moment
— the more stations waiting, the worse the collision. Rolling dice spreads
those key-ups apart in time without any coordination between stations.

.. list-table::
   :header-rows: 1
   :widths: 16 84

   * - Value
     - Behaviour
   * - 255
     - Transmit on the first clear slot, every time. Equivalent to plain
       non-persistent CSMA. Appropriate only where you are the sole transmitter.
   * - 128
     - ~50 % per slot. Assertive; sensible on a lightly used channel where
       latency matters.
   * - 63
     - ~25 % per slot. The standard. Correct for essentially every shared APRS
       channel.
   * - 20–32
     - ~8–12 %. For a heavily congested urban channel, or a station that should
       yield to others (a low-priority telemetry beacon, a fill-in digi in a
       crowded area).
   * - 1
     - ~0.4 %. The station will wait a very long time for each transmission.

**Anti-starvation.** The scheduler does not let a frame wait forever: after
eight consecutive slots lost to either a busy channel or a failed roll, it
forces the transmission anyway. So even a very low persistence has a bounded
worst-case delay rather than an unbounded one.

.. warning::

   0 is refused and floored to 1, rather than accepted, because a persistence
   of 0 would mean the roll can never succeed and the station would never
   transmit again — a failure mode that looks exactly like broken hardware.

.. warning::

   Raising persistence does not get your packets through a busy channel; it
   gets them *collided* on a busy channel, which costs everyone airtime and
   gets nothing delivered. If your packets are not reaching a digipeater, the
   answer is more preamble, better audio levels or a better antenna — not a
   higher ``p``.

Audio interface
===============

This fieldset describes what sits electrically between the ESP32's pins and the
transceiver. The defaults suit an interface board that carries its own bias
network, attenuators and reconstruction filter — the design shown in the
project's schematics. The settings here are what an interface reduced to a
coupling capacitor and a level trimmer per direction needs instead.

ADC input self-bias
-------------------

**What it is.** The ESP32's ADC measures a voltage between 0 V and roughly
3.1 V. Audio is a signal that swings both positive and negative around zero, so
it must be lifted to sit around the middle of that range before it can be
sampled; the negative half is otherwise simply lost. That lifting is called
biasing.

* **Off** (default) — the interface board provides the bias, typically with two
  resistors forming a divider at the ADC pin. This is what the project's
  schematics show.
* **On** — the firmware enables the ADC pad's own internal pull-up *and*
  pull-down resistors, which together pull the pin to about half the supply.
  This is what an input coupled through nothing but a capacitor needs.

**When to turn it on.** You built a minimal interface: a capacitor from the
radio's speaker output (through a level trimmer) straight to the ADC pin, with
no bias resistors of your own. Run **RX LEVEL**: if the reported ``DC`` sits
near 0 mV or near the supply rail instead of around 1500–1600 mV, the input is
not biased and this is the switch that fixes it.

.. warning::

   **Hardware limitation.** The internal pull resistors only exist on GPIO32
   and GPIO33. If the firmware was built with the ADC on GPIO34–GPIO39 — which
   are input-only pads with no pull resistors at all — enabling this logs an
   error and changes nothing. Such an input can only be biased externally.

.. warning::

   Do **not** enable it when the interface board already sets the bias. The
   internal resistors are weak but not negligible, and they load the external
   divider, shifting the operating point and reducing the usable swing. If you
   are using the project's interface board, leave this off.

Warn on receive over-range
--------------------------

**What it is.** A diagnostic. With it on, the firmware logs a warning whenever
raw conversion results reach either end of the converter's range (below code 15
or above code 4080 out of 0–4095), at most once every five seconds so a
sustained problem cannot flood the console.

**Off by default**, because on a correctly built interface it should never fire
and the check costs nothing when disabled.

**When to turn it on.**

* While setting the receive level trimmer for the first time — combine it with
  the *Logs* page to see the warnings as they happen.
* Permanently, on an interface with **no input clamp diodes**. There, an
  over-range reading does not merely mean "too loud": it means the pin is being
  driven beyond the supply rails, which is a path to damaging the ESP32.
* When a station decodes fine at moderate volume and stops decoding when the
  radio's volume is raised — the classic signature of clipping.

.. warning::

   Clipped audio does not sound broken to a human ear, but a clipped AFSK
   waveform loses the amplitude relationship the demodulator depends on. A
   station suffering this will typically decode strong stations *worse* than
   weak ones, which is counter-intuitive enough that people replace antennas
   over it.

Transmit output swing (%)
-------------------------

**What it is.** The peak-to-peak amplitude of the transmit audio, as a
percentage of the DAC's full 0–3.3 V range. Internally, every sample is scaled
around the DAC's mid-code before it is written out, so the tone stays centred
whatever the percentage.

**Range 20–100 %, default 60 %.** Applied live, and on the very next sample —
the modulator does not have to be stopped.

**Why the floor is 20 %.** The ESP32's DAC is 8 bits wide. The percentage
decides how many of its 256 codes a sine period is actually drawn with: at
20 % a full period spans roughly 50 codes, and below that quantization turns
the tone into a visible staircase whose harmonics fall inside the audio
passband and degrade the deviation the receiver sees. The 30–40 dB of
attenuation a microphone input needs belongs in an **external resistive
attenuator**, not here.

**How to set it.** Use the **TX TEST** button with a deviation meter, another
receiver or an SDR on the transmitted signal, and adjust for **2.5–3.5 kHz**
deviation on a 5 kHz-deviation FM channel. If you cannot get low enough at
20 %, add or increase the external attenuator. If you cannot get high enough at
100 %, the attenuator is too aggressive.

.. warning::

   Over-deviation is the most common transmit fault on APRS. It splatters into
   the adjacent channel, and — because a receiver's discriminator clips — the
   packet often decodes *worse* at the far end than it would have at correct
   deviation. Louder is not better.

.. warning::

   Under-deviation is just as real a failure: the frame is buried in the
   receiver's noise floor and only very close stations decode it. If you are
   heard by the digipeater across the road and nobody else, measure your
   deviation before blaming propagation.

Transmit sample rate
--------------------

**What it is.** The rate at which the DAC generates the transmit waveform:
either **38400 Hz** (default) or **76800 Hz**.

Both are exact multiples of 1200 and 9600, which is a hard requirement — the
modulator derives its symbol timing by integer division, so a rate that is not
a multiple of both desynchronizes one baud rate or the other. A value outside
these two falls back to the standard rate rather than being stored and refused
later.

**What the higher rate buys.** Any sampled output carries reconstruction
images: copies of the wanted signal reflected around multiples of the sample
rate. Doubling the rate moves those images an octave further away, where a much
gentler filter — or the transmitter's own audio bandwidth — can remove them.

**When to choose 76800 Hz.**

* The interface board carries **no reconstruction filter** — the DAC pin goes
  through a capacitor and a trimmer straight to the radio.
* You are seeing unexplained spurious audio, a rough-sounding transmitted tone,
  or an unhappy receiver on an otherwise correct setup.

**When to stay at 38400 Hz.**

* The interface has a proper low-pass filter after the DAC, which is what the
  project's schematics show.
* CPU headroom is tight — the higher rate doubles the DAC interrupt load, and
  on a station also running the IGate, the web admin, Telegram and sensor
  polling, that load is not free.

.. warning::

   This setting takes effect **only after a reboot**. The form shows the value
   that was saved, which is the value the next boot will use; until then the
   modem keeps transmitting at the rate it was started with.

Transmitter time-out (ms)
-------------------------

**What it is.** A failsafe watchdog on a single key-up. If the transmitter has
been keyed continuously for longer than this, the firmware releases PTT, aborts
the frame in progress, flushes the modulator FIFO and logs an error.

**Range 0–60000 ms. 0 disables the time-out — and 0 is the default.**

**Why it exists.** It is not a scheduler and it is not a duty-cycle control. It
exists for the case where the transmit path has *stalled*: a task that never
completes, a modem state machine wedged mid-frame, a fault that would otherwise
leave a carrier on the air until someone notices. On an unattended
mountain-top station, that is the difference between a bad hour and a blocked
frequency plus an angry coordinator.

**How to choose a value.** Work out the longest key-up this station can
legitimately produce and leave generous margin above it. That worst case is the
maximum preamble plus a maximum-length frame at the slowest baud rate in use —
a few seconds at 1200 Bd, more with FX.25 redundancy. Practical settings:

* **5000–10000 ms** for a 1200 Bd station with a 300 ms preamble. Far above any
  real transmission, far below anything anyone would call a stuck carrier.
* **15000–20000 ms** for a 300 Bd HF station, where frames are genuinely long.
* **0** on a bench setup where you would rather see a fault happen than have it
  cleaned up behind your back.

.. warning::

   Setting this close to the length of a real transmission cuts real frames
   short. The station will appear to transmit garbage that nobody decodes, and
   the log will show the time-out firing repeatedly. If you see that message,
   the setting is too low, not the radio broken.

.. tip::

   Enabling this is strongly recommended for any station left unattended,
   especially one with an amplifier. It costs nothing when nothing goes wrong.

The three diagnostic buttons
=============================

All three sit next to the *Enable audio ADC/DAC modem* checkbox. All three
**save the form first**, so what is on screen is always what is tested. All
three are ``POST`` routes, deliberately: two of them key the transmitter, and
the same-origin check that protects this admin applies to ``POST`` only — a
``GET`` route here could be triggered from any other page an authenticated
operator happened to have open, simply by pointing an image tag at it.

Only one of the three may run at a time; a second request is refused with a
clear message rather than being queued.

LOOP TEST
---------

**What it does.** Builds a small APRS status frame carrying a random one-time
token (``SELFTST>APLT1T:>LOOPTEST <token>``), diverts decoded frames to its own
private hook so the test frame is never digipeated or uplinked to APRS-IS,
switches the modem to full duplex for the duration, transmits it, and waits up
to 4 seconds for the ADC / demodulator / decoder chain to hand the same frame
back. The real hook and the configured duplex mode are restored whatever the
outcome.

Before keying up, it waits up to 3 seconds for the channel to go quiet, so a
real station on the air does not cause a spurious failure. If the channel never
clears, it transmits anyway rather than hanging.

**What it requires.** A physical **audio loopback**: the DAC pin wired to the
ADC pin (through the interface board's own attenuators, or directly), with a
common ground. It can also be run through a transceiver in true full duplex, or
through a radio listening to itself on another radio — but in its normal form
it is a bench test of the board, not of the radio.

.. warning::

   Full duplex is forced during the test because a wire loop means the modem
   permanently hears its own carrier, and CSMA would therefore never find a
   clear channel. This is a deliberate, temporary override — but it does mean
   the test transmits without regard to what is on the channel. Do not run it
   with an antenna connected on a busy frequency.

**Reading a PASS.** A pass reports the RX level in mV RMS, the raw ADC swing
with the converter's rails (0/4095) for reference, and the AGC's peak gain.
Those numbers matter as much as the word PASS: a healthy centred swing should
sit well short of either rail. A pass with the swing nearly touching 0 or 4095
means you are decoding *and* clipping, and will stop decoding as soon as
anything gets louder.

A token match alone is not accepted as a pass. The run must also have captured
at least 50 counts of real ADC swing and a non-zero RX level, so a pass can
never be reported over an open loop, a floating input, or a stale decode from
something else.

**Reading a FAIL.** The failure messages are graded, and each one points at a
different part of the chain:

.. list-table::
   :header-rows: 1
   :widths: 42 58

   * - What it reports
     - What it means
   * - The ADC never delivered a single sample
     - The ADC continuous driver or its timer is not running. An
       initialization failure, not a wiring or level problem. Reboot; if it
       persists, the build or the pin assignment is wrong.
   * - Sampling, but the raw code barely moved
     - The ADC is alive but sees no tone at all. The loopback wire is missing,
       broken, or the grounds are not common. The reported DC offset tells you
       more: a line pinned near the supply rail points at a short or a miswire.
   * - A real signal arrived, but no demodulator ever locked
     - The tone is reaching the ADC but the correlator/PLL cannot make sense of
       it. Check that *Modulation* matches what was transmitted, and try
       toggling *Flat / discriminator audio input* — a direct DAC-to-ADC loop
       never passes through a real radio's de-emphasis network. If the AGC gain
       never rose above unity, the problem is in the AGC path rather than the
       baud rate.
   * - The PLL locked, but no valid frame came back
     - The message reports how far the HDLC state machine got: never starting a
       frame points at bit recovery; starting frames that fail CRC points at a
       marginal level or SNR rather than a modulation mismatch.
   * - A frame came back but did not match
     - Audio distortion, clipping, or a loopback that is picking up something
       other than this station's own transmission.

RX LEVEL
--------

**What it does.** Watches the receive front-end for about one second and
reports what it saw. **Nothing is transmitted and no modem state is touched**,
so — unlike the loop test — it can be run with the transceiver connected, the
antenna up, and real traffic decoding.

This is the measurement the receive side of the audio interface is adjusted
against.

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Field
     - How to read it
   * - ``mVrms`` / ``peak``
     - Mean and peak audio level over the window. With the radio unsquelched on
       an idle channel you are reading noise; with a packet arriving you are
       reading signal. A healthy level leaves clear headroom below the rails.
   * - ``DC``
     - Where the input is biased. Should sit near the middle of the converter's
       range, around 1500–1600 mV. Near 0 mV or near the rail means the input
       is not biased — see *ADC input self-bias*.
   * - ``AGC``
     - The automatic gain the modem is applying. A gain that stays at 1.00x on
       a real signal means the input is already at or above target. A very
       large gain means the signal is far too quiet and the modem is amplifying
       noise along with it.
   * - ``raw``
     - The raw conversion extremes, against rails of 0 and 4095. This is your
       clipping margin: reaching 0 or 4095 is over-range.
   * - ``DCD``
     - Whether a demodulator was locked during the window. ``yes`` while a
       packet is arriving is exactly right; ``yes`` on a silent channel points
       at a noisy input or a false lock.

**Typical procedure.** Unsquelch the radio, press **RX LEVEL**, and adjust the
receive trimmer until the raw extremes use a good share of the range without
approaching the rails, and the DC offset is centred. Then squelch normally, wait
for real traffic, and confirm ``DCD yes`` appears and the dashboard shows
decodes.

.. tip::

   This is also the fastest way to answer "is the radio even connected?".
   A flat reading with a stuck sample count says the audio is not arriving at
   all, and no amount of adjustment elsewhere will change that.

TX TEST
-------

**What it does.** Keys the transmitter, modulates a short APRS status frame
(``SELFTST>APLT1T:>TXTEST``) and unkeys, waiting for nothing to come back. It
is the transmit-side counterpart of RX LEVEL: what you set the transmit level
against when a transceiver, rather than a wire loop, is connected.

The burst goes through the **normal channel access path** — it waits for a
clear channel like any other frame, and it is held back by the duty-cycle
ceiling if that is enabled and already reached. The page waits for the burst to
finish, bounded at about three seconds, so the result means something when you
read it.

On success it reports the preamble length and output swing that were used, and
reminds you of the target: measure the deviation on other equipment and set the
transmit level trimmer for **2.5–3.5 kHz**.

.. warning::

   **This puts a real signal on the air.** Before pressing it, confirm the
   frequency, confirm the antenna or dummy load, and confirm you are licensed
   to transmit there. On a shared APRS channel, do not press it repeatedly
   while adjusting a trimmer — use a dummy load for the adjustment and one
   on-air burst to confirm.

If it refuses, the message says why: the modem is not enabled, another
diagnostic is running, or the channel-access path discarded the frame — which
in practice means a duty-cycle ceiling that is already reached, or a full
transmit queue. The event log names which.

Bringing up a new station, in order
====================================

The fields on this page interact, so there is a sequence that avoids chasing
your own tail.

.. list-table::
   :header-rows: 1
   :widths: 6 34 60

   * - #
     - Step
     - Notes
   * - 1
     - Tick **Enable audio ADC/DAC modem**, *Save*, **reboot**.
     - Nothing else on this page can be tested until the modem hardware is up.
   * - 2
     - Set **Modulation** to match the channel.
     - 1200 Bd Bell 202 for standard APRS. Get this wrong and every later step
       is meaningless.
   * - 3
     - With the DAC and ADC pins looped together, run **LOOP TEST**.
     - Proves the board itself works, end to end, before a radio is involved.
       Note the swing figures, not just PASS.
   * - 4
     - Connect the radio. Set **Flat / discriminator audio input** according to
       which jack you used.
     - Speaker jack → off. Data/discriminator port → on.
   * - 5
     - Run **RX LEVEL** and adjust the receive trimmer.
     - Check the DC offset first; enable **ADC input self-bias** if the input
       is unbiased. Then set the level for good swing with headroom.
   * - 6
     - Enable **Warn on receive over-range** and watch the *Logs* page while
       real traffic arrives.
     - Confirms you are not clipping on strong local stations.
   * - 7
     - Into a dummy load, run **TX TEST** and set **Transmit output swing** for
       2.5–3.5 kHz deviation.
     - Add an external attenuator if 20 % is still too loud.
   * - 8
     - Set **Preamble** for your radio's PTT behaviour.
     - 300 ms unless you have a reason. Confirm with a neighbour that you are
       being digipeated.
   * - 9
     - Set **CSMA persistence** and **TX time-slot** for the channel's
       congestion.
     - Defaults (63 / 2000 ms) are correct for a normal shared channel.
   * - 10
     - Enable the **duty-cycle limiter** and set **Transmitter time-out**.
     - Do this before leaving the station unattended, not after.

Suggested profiles
==================

.. list-table::
   :header-rows: 1
   :widths: 22 13 13 10 10 10 11 11

   * - Station
     - Modulation
     - Preamble
     - Slot
     - Persist
     - Buffers
     - Duty
     - TX t/o
   * - Home IGate, handheld radio, shared channel
     - 1200 Bell 202
     - 300 ms
     - 2000 ms
     - 63
     - 1
     - on, 20 %
     - 8000 ms
   * - Mountain-top digipeater, mains + amplifier
     - 1200 Bell 202
     - 400 ms
     - 2000 ms
     - 63
     - 2
     - on, 50 %
     - 10000 ms
   * - Fill-in digipeater, crowded urban channel
     - 1200 Bell 202
     - 300 ms
     - 3000 ms
     - 32
     - 1
     - on, 15 %
     - 8000 ms
   * - Solar tracker / beacon
     - 1200 Bell 202
     - 300 ms
     - 2000 ms
     - 63
     - 1
     - on, 10 %
     - 8000 ms
   * - Dedicated 9600 Bd point-to-point link
     - 9600 G3RUH
     - 150 ms
     - 0 ms
     - 255
     - 3
     - off
     - 5000 ms
   * - HF gateway, 300 Bd
     - 300 AFSK
     - 500 ms
     - 3000 ms
     - 63
     - 1
     - on, 25 %
     - 20000 ms

These are starting points, not prescriptions. The duty-cycle figures in
particular must be reconciled with your own licence conditions.

Field reference
===============

.. list-table::
   :header-rows: 1
   :widths: 30 14 14 42

   * - Field
     - Range
     - Default
     - Applied
   * - FX.25
     - off / receive only / receive and transmit
     - off
     - Live
   * - Enable audio ADC/DAC modem
     - on / off
     - on
     - **Next reboot**
   * - Modulation
     - 0–3
     - 1 (Bell 202)
     - Live
   * - Flat / discriminator audio input
     - on / off
     - on
     - Live
   * - Preamble
     - 50–2000 ms
     - 300 ms
     - Live
   * - TX time-slot
     - 0–10000 ms
     - 2000 ms
     - Live
   * - TX buffers
     - 1–11
     - 1
     - Live (read on every transmit)
   * - Duty-cycle limiter
     - on / off
     - off
     - Live (read on every transmit)
   * - Duty-cycle limit
     - 1–100 %
     - 25 %
     - Live (read on every transmit)
   * - PTT minimum unkey time
     - 0–5000 ms
     - 0
     - Live
   * - CSMA persistence
     - 1–255
     - 63
     - Live
   * - ADC input self-bias
     - on / off
     - off
     - Live
   * - Warn on receive over-range
     - on / off
     - off
     - Live
   * - Transmit output swing
     - 20–100 %
     - 60 %
     - Live (next sample)
   * - Transmit sample rate
     - 38400 / 76800 Hz
     - 38400 Hz
     - **Next reboot**
   * - Transmitter time-out
     - 0–60000 ms
     - 0 (off)
     - Live

Every numeric field is clamped in three places against the same constants in
``main/include/aprs_service.h``: the input's own ``min``/``max`` attributes, the
handler that parses the posted form, and the loader that reads ``radio.json``
from flash. A hand-edited configuration file or a malformed POST therefore
cannot put an out-of-range value into service.

What is deliberately *not* on this page
=======================================

Several settings an operator might expect here do not exist, because the
firmware has no runtime equivalent for them. They are shown read-only, or not
at all, rather than offered as controls that would save to flash and change
nothing.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Absent setting
     - Why
   * - Squelch level
     - There is no software squelch. Every sample reaches the demodulator, and
       the AX.25 decoder gates on the demodulator's own DCD instead. Use the
       radio's own squelch — or leave it open, which often decodes better.
   * - Receive volume / gain trim
     - There is no RX gain stage to trim. The AGC is self-limiting. Set the
       level with the interface's trimmer, guided by **RX LEVEL**.
   * - AGC maximum gain
     - The AGC bounds itself; there is nothing to configure.
   * - ADC attenuation
     - A compile-time constant (``MODEM_ADC_ATTEN``). Shown read-only.
   * - Audio pins (ADC/DAC)
     - Compile-time board wiring (``MODEM_ADC_GPIO`` / ``MODEM_DAC_GPIO``).
       Shown read-only.
   * - PTT pin and PTT polarity
     - Compile-time board wiring (``MODEM_PTT_GPIO`` /
       ``MODEM_PTT_ACTIVE_HIGH``). Shown read-only; the pin is registered in
       the GPIO ownership table so no other feature can claim it.
   * - Duplicate suppression
     - One pair of controls for the whole firmware, on the *IGate* page.
   * - CSMA slot interval
     - Fixed at 100 ms inside the modem. *TX time-slot* on this page is the
       quiet time, which is the parameter worth adjusting.

Troubleshooting
===============

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - Symptom
     - Where to look
   * - Nothing decodes at all, in either direction
     - **Modulation** first. Then run **LOOP TEST** on a wire loop to separate
       a board problem from a radio problem.
   * - LOOP TEST says the modem is not enabled
     - **Enable audio ADC/DAC modem** is off, or was turned on without
       rebooting.
   * - Strong local stations decode, weak ones never do
     - Receive level too low, or **Flat / discriminator audio input** set
       backwards. Run **RX LEVEL**.
   * - Weak stations decode, strong ones do not
     - Clipping. Enable **Warn on receive over-range**, run **RX LEVEL**, and
       reduce the receive trimmer.
   * - Other stations hear my carrier but decode only some frames
     - **Preamble** too short, or deviation wrong. Raise the preamble, then
       measure deviation with **TX TEST**.
   * - Nobody hears me at all, but the radio keys
     - Deviation far too low, or the transmit audio is not reaching the
       microphone input. Set **Transmit output swing** against a deviation
       measurement.
   * - Beacons appear far less often than configured
     - The **duty-cycle limiter** is deferring them. Check the dashboard's drop
       counter and either raise the ceiling or lengthen the intervals.
   * - Frames are being dropped for backlog
     - The station is offering more traffic than the channel can carry. Reduce
       what it transmits before raising **TX buffers**.
   * - The log reports the transmitter time-out firing
     - **Transmitter time-out** is set below the length of a real transmission.
       Raise it well above the worst case.
   * - The saved transmit sample rate does not seem to apply
     - It needs a reboot. The form shows the saved value, not the running one.

.. seealso::

   :ref:`en-modem` for the modem profiles and the component's API,
   :ref:`en-dsp-signal-chain` for the receive and transmit signal paths,
   :ref:`en-web-admin` for the admin as a whole, and the *Hardware* chapter for
   the interface schematics the defaults on this page assume.
