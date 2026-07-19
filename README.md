# esp32idf_APRS

**A native ESP-IDF (C, no Arduino) APRS IGate / Digipeater / Tracker for the ESP32, with a built-in web admin, an on-chip AFSK/FSK soft-modem (ADC + DAC), and APRS-IS uplink.**

> ⚠️ **Work in progress.** The RF transmit path, IGate, digipeater, beacons and web admin are functional; several config pages are present but their features are not implemented yet (see [Status & known limitations](#status--known-limitations)).

---

## Table of contents

- [What this is](#what-this-is)
- [Feature matrix](#feature-matrix)
- [Hardware](#hardware)
  - [Supported target](#supported-target)
  - [Pinout / board definition](#pinout--board-definition)
  - [Typical wiring to a radio](#typical-wiring-to-a-radio)
    - [What each end actually presents](#what-each-end-actually-presents)
    - [Minimal functional schematic](#minimal-functional-schematic)
    - [A full functional interface for the Baofeng UV-5R](#a-full-functional-interface-for-the-baofeng-uv-5r)
    - [Why the PTT default is a trap](#why-the-ptt-default-is-a-trap)
    - [Isolation and ground loops](#isolation-and-ground-loops)
    - [Bring-up order](#bring-up-order)
  - [Loopback bench wiring](#loopback-bench-wiring)
- [Repository layout](#repository-layout)
- [Architecture](#architecture)
  - [Boot sequence](#boot-sequence)
  - [Task map](#task-map)
  - [Data flow](#data-flow)
- [The modem component](#the-modem-component-esp32idf_radioamateur_modem)
  - [Signal chain](#signal-chain)
  - [Why the numbers are what they are](#why-the-numbers-are-what-they-are)
  - [Compile-time configuration reference](#compile-time-configuration-reference)
  - [Runtime configuration (`modem_config_t`)](#runtime-configuration-modem_config_t)
- [Application components](#application-components)
  - [`igate` — APRS-IS gateway](#igate--aprs-is-gateway)
  - [`digirepeater` — digipeater](#digirepeater--digipeater)
  - [`beacon` — own-station beacons](#beacon--own-station-beacons)
  - [`message` — APRS messaging](#message--aprs-messaging)
  - [`lastheard` / `trafficlog` — dashboard feeds](#lastheard--trafficlog--dashboard-feeds)
  - [`webconfig` — web admin](#webconfig--web-admin)
- [Building and flashing](#building-and-flashing)
- [First boot & configuration](#first-boot--configuration)
- [Web admin reference](#web-admin-reference)
  - [HTTP routes](#http-routes)
  - [Page-by-page](#page-by-page)
- [Configuration storage (`config.json`)](#configuration-storage-configjson)
- [Path presets and the path bitmask](#path-presets-and-the-path-bitmask)
- [The LOOP TEST](#the-loop-test)
- [Localization](#localization)
- [Troubleshooting](#troubleshooting)
- [Status & known limitations](#status--known-limitations)
- [Porting notes](#porting-notes)
- [Credits](#credits)
- [License](#license)

---

## What this is

`esp32idf_APRS` is an ESP-IDF **v5.x** project that turns a bare ESP32 DevKit plus a cheap audio interface into a complete APRS station:

* it **demodulates** AFSK/FSK audio from a radio's speaker/discriminator output on **ADC1**,
* it **decodes** HDLC/AX.25 (optionally FX.25 forward-error-corrected) frames,
* it **gates** them to APRS-IS over Wi-Fi (`qAR`/`qAO`),
* it **digipeats** them back on RF (WIDEn-N / TRACEn-N / RELAY / ECHO / GATE),
* it **beacons** its own position,
* it **modulates** and transmits frames back out through the ESP32's **8-bit DAC**, keying the radio via a PTT GPIO,
* and it is configured entirely through an **HTTP web admin** served by the device itself — no serial console, no recompilation for ordinary settings.

Everything is plain C. There is no Arduino core, no `String`, no PlatformIO. The whole DSP chain — correlator demodulator, DPLL bit recovery, NRZI, HDLC framer, AX.25 codec, Reed–Solomon FX.25 FEC — runs on the ESP32 itself, using only the SAR-ADC in DMA/continuous mode, the DAC, and a GPTimer.

---

## Feature matrix

| Area | Status | Notes |
|---|---|---|
| AFSK 1200 Bd Bell 202 (standard APRS) | ✅ | dual demodulator, default profile |
| AFSK 1200 Bd ITU V.23 (1300/2100 Hz) | ✅ | |
| AFSK 300 Bd (1600/1800 Hz) | ✅ | HF-style |
| G3RUH FSK 9600 Bd | ✅ | needs flat/discriminator audio |
| HDLC / AX.25 UI frame RX+TX | ✅ | `AX25_FRAME_MAX_SIZE = 329` |
| FX.25 (RS FEC over AX.25) | ✅ | `-DENABLE_FX25`, RX-only / RX+TX modes |
| PTT keying (runtime-selectable GPIO + polarity) | ✅ | validated against ADC/DAC/flash pins |
| CSMA / TX time-slot / TXDelay preamble | ✅ | `preamble`, `tx_timeslot` |
| DCD (data carrier detect) | ✅ | demodulator-derived; no hardware squelch input |
| APRS-IS IGate RF→INET | ✅ | filters, dedup, `qAR`/`qAO` |
| APRS-IS IGate INET→RF | ✅ | `inet2rfFilter` payload-type gating (`aprs_filter.c`) |
| Digipeater | ✅ | WIDEn-N, TRACEn-N, RELAY/ECHO/GATE, dup-suppression |
| Fixed-position beacons (tracker / igate / digi) | ✅ | three independent FreeRTOS tasks |
| SmartBeaconing / GPS-driven tracker | ❌ | config fields exist, logic not implemented |
| APRS messaging + ack/retry | ✅ | RF and/or INET |
| APRS message AES-128-CBC encryption | ✅ | `mbedtls`, MD5-derived IV, base64 payload |
| Web admin (HTTP Basic auth) | ✅ | ~25 pages, live dashboard |
| Live traffic log + last-heard table | ✅ | JSON long-poll (`?since=<seq>`) |
| LittleFS storage, upload/download/delete/format | ✅ | 400 KB partition |
| SNTP time sync (3 hosts) | ✅ | clock always kept in UTC |
| CPU frequency control (80/160/240 MHz) | ✅ | `esp_pm_configure()` |
| Wi-Fi AP / STA / AP+STA, scan, TX power | ✅ | 5 STA slots (first enabled one is used) |
| Localization (EN / ES / IT) | ✅ | compile-time, one language per image |
| OTA update | ❌ | partition table is single-`factory`; About page says so |
| LoRa / SX127x-SX128x RF module | ❌ | UI + config only, `ENABLE_RF_MODULE` is commented out |
| WireGuard VPN, MQTT, GNSS, weather, telemetry, sensors | ❌ | pages/config exist; modules disabled in `app_config.h` |
| Bluetooth, PPP/GSM, OLED display, Modbus | ❌ | config fields kept for compatibility only |

Legend: ✅ implemented · 🟡 partial · ❌ not implemented (scaffolding only)

---

## Hardware

### Supported target

* **ESP32** (classic, Xtensa dual-core) — `CONFIG_IDF_TARGET=esp32`, 4 MB flash.
* Dual-core is **not optional**: the ADC ISR and the DAC sample clock are pinned to *different* cores on purpose (see [Why the numbers are what they are](#why-the-numbers-are-what-they-are)).
* ESP32-S2 has DACs on GPIO17/18 and would need the config header adjusted. **ESP32-S3/C3/C6/H2 have no DAC at all** and cannot run the TX path unmodified.

### Pinout / board definition

The board definition lives in the **top-level `CMakeLists.txt`**, applied *before* `project()` via `idf_build_set_property(COMPILE_DEFINITIONS ... APPEND)`:

```cmake
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_ADC_GPIO=33"      APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_DAC_GPIO=25"      APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_PTT_GPIO=26"      APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_PTT_ACTIVE_HIGH=0" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_LED_TX_GPIO=-1"   APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_LED_RX_GPIO=-1"   APPEND)
```

| Signal | Default | Hard constraints |
|---|---|---|
| **Audio in (ADC)** | `GPIO33` (ADC1_CH5) | **32–39 only.** ADC2 is unusable while Wi-Fi is up, and this firmware always has Wi-Fi up. Enforced by `#error`. |
| **Audio out (DAC)** | `GPIO25` (DAC_CHAN_0) | **25 or 26 only.** The ESP32 DAC is hard-wired to those pads and is not routable through the GPIO matrix. Enforced by `#error`. |
| **PTT** | `GPIO26`, active **low** | Compile-time value is only the *fallback default*; the effective PTT pin/polarity is **runtime-selectable on the Radio page**, validated by `afsk_ptt_gpio_is_valid()` (rejects GPIO34–39 input-only, GPIO6–11 flash/PSRAM, and the ADC/DAC pins themselves). `-1` = disabled. |
| **TX / RX LEDs** | disabled (`-1`) | Any output-capable GPIO. |

> ⚠️ With the shipped defaults, **`MODEM_DAC_GPIO=25` and `MODEM_PTT_GPIO=26` are both in the DAC pad pair.** That is fine (26 is only DAC_CHAN_1 if you *select* it), but if you move the audio output to GPIO26 you must move PTT elsewhere. The validator will refuse the overlap at runtime.

> **Note on the "Mod" page GPIO fields.** `g_config.adc_gpio`, `dac_gpio`, `rf_sql_gpio`, `rf_pwr_gpio`, `adc_atten`, `sql_level`, `volume`, `agc_max_gain` are still loaded, saved and editable — but **nothing in the audio modem path reads them any more** since the `esp32_IDF_libAPRS` → `esp32idf_radioamateur_modem` swap. They are retained purely so existing `config.json` files round-trip unchanged. To move the audio pins, edit the `CMakeLists.txt` block above and rebuild.

### Typical wiring to a radio

Neither end of this link can be connected directly to the other. The ESP32 side is a 3.3 V, DC-biased, sampled-data interface; the radio side is an AC, ground-referenced, millivolt-level analogue interface. Three things have to happen in between: **attenuate** (TX), **shift and clamp** (RX), and **switch** (PTT).

#### What each end actually presents

Every ESP32-side figure below is derived from the component's own compile-time constants, not from a datasheet ideal — see [Compile-time configuration reference](#compile-time-configuration-reference).

| Node | What is really there | Where it comes from |
|---|---|---|
| **GPIO25 (DAC), transmitting** | 1.65 V DC with a **≈1.97 Vpp** swing on top (codes 52…204 → 0.67–2.64 V) ⇒ **≈0.70 Vrms** for a sine, plus **reconstruction images around 38.4 kHz** | `DAC_MID = 128`, `MODEM_DAC_AMPLITUDE_PCT = 60`, `MODEM_DAC_SAMPLERATE = 38400` |
| **GPIO25 (DAC), idle / before `modem_init()`** | ~1.65 V once initialised; **undefined and floating during reset and the first ~5 s of boot** (the ADC clock calibration) | `modem_init()` blocks ~5 s |
| **GPIO33 (ADC)** | Window **0–3.1 V**, normalised as `(raw − dc_avg)/2048`, i.e. ±1.0 ≙ ±1.55 V. AGC targets **310 mVrms** at the pin, reaches it from as little as **≈39 mVrms** (`AGC_MAX_GAIN = 8`), holds gain below **≈16 mVrms** (noise floor), and **clips above ≈1.1 Vrms** | `MODEM_ADC_ATTEN = ADC_ATTEN_DB_12`, `AGC_TARGET_RMS = 0.2` |
| **GPIO26 (PTT)** | Plain 3.3 V CMOS output, **active LOW with the shipped board definition**, and a **floating input during reset** | `MODEM_PTT_GPIO=26`, `MODEM_PTT_ACTIVE_HIGH=0` |
| Rig **MIC IN** (hand-mic jack) | 5–20 mVrms, often with pre-emphasis and a DC bias for the electret | needs ≈30–40 dB of pad |
| Rig **DATA IN** (mini-DIN-6 pin 1, "PKT IN") | ≈40 mVpp ⇒ **≈14 mVrms**, flat, no pre-emphasis | needs ≈35 dB of pad |
| Rig **SPKR / AF OUT** | 0.1–3 Vrms, volume-knob dependent, de-emphasised | needs a pad + bias |
| Rig **DATA OUT / DISC** (mini-DIN-6 pin 4) | 100–300 mVrms, **fixed level, squelch-independent, flat** | needs bias only — this is the good one |

Two consequences worth internalising before soldering:

* **The DAC is ~35 dB too hot** for anything on the radio. Direct connection will not merely over-deviate, it will splatter.
* **A `DATA OUT` port is already inside the AGC window** (100–300 mVrms vs. the 39 mVrms–1.1 Vrms usable range). If your rig has a data jack, the RX side is a bias network and nothing else — no pot, no gain.

#### Minimal functional schematic

Passive, ~15 parts, no op-amps. This is the whole thing.

```
 ── TX ── ESP32 ─────────────────────────────────────────────────────► rig ──

  GPIO25 ──[R1 2k2]──┬──[R2 2k2]──┬──[C1 10µ]──[R3 10k]──┬─ RV1 top
   (DAC)             │            │      +               │
                   [C2 15n]     [C3 15n]              [RV1 1k]  level trim
                     │            │                      ├─ wiper ──► MIC / DATA IN
                    GND          GND                     │
                                                         └─ bottom ─► rig audio GND

 ── RX ── rig ──────────────────────────────────────────────────────► ESP32 ──

                                          bias node
  SPKR/DISC ─[RV2 10k]─ wiper ─[C4 10µ]──────┬──────[R7 1k]───┬──► GPIO33 (ADC)
                 │                +          │                │
   rig GND ──────┘                     [R5 10k]─► 3V3     [D1]─► 3V3   BAT54S
   (omit RV2 for a fixed DATA OUT:          │             [D2]─► GND   (or 2×1N4148)
    wire straight into C4)              [R6 10k]─► GND        │
                                            │              [C5 1n]
                                           GND                │
                                                             GND

 ── PTT ── option A: opto, isolated, matches the shipped active-LOW default ──

                      ┌─ PC817 ─┐
   3V3 ──[R8 470]──[A]│▶      C│──────────► rig PTT
   GPIO26 ─────────[K]│        E│──────────► rig PTT GND
                      └─────────┘
   GPIO26 LOW → LED on → keyed.   Floating at reset → LED off → unkeyed.

 ── PTT ── option B: low-side MOSFET, non-isolated, needs ACTIVE_HIGH=1 ──

                       ┌─ 2N7000 / BS170 ─┐
   GPIO26 ──[R9 1k]────┤ G              D ├───► rig PTT
                       │                S ├───► GND (common)
          [R10 10k]────┤G→S               │
                       └──────────────────┘
   GPIO26 HIGH → keyed.   R10 holds it unkeyed through reset and deep sleep.
```

| Ref | Value | Job | If you change it |
|---|---|---|---|
| **R1, R2 / C2, C3** | 2k2 / **15 nF** | Two-pole reconstruction low-pass, **fc ≈ 4.8 kHz**. Kills the 38.4 kHz DAC images at **≈−36 dB** while costing only −0.3 dB at 1200 Hz and −0.8 dB at 2200 Hz (≈0.5 dB of twist) | **22 nF** (fc 3.3 kHz, −43 dB at 38.4 kHz) if you only ever run AFSK and want the images gone harder; **10 nF** (fc 7.2 kHz, −29 dB) is **mandatory for 9600 Bd G3RUH**, which needs flat response past ~5 kHz |
| **C1** | 10 µF | DC block. The DAC's 1.65 V idle bias must never reach a mic input | Into R3+RV1 = 11 kΩ this is fc ≈ 1.4 Hz — do not go below 1 µF |
| **R3 / RV1** | 10k / 1k trim | Pad + level. Fixed 11:1 divider means the trimmer works across **0–64 mVrms** with mid-rotation ≈32 mV, instead of living in the bottom 4 % of a bare 10k pot | Wiper impedance ≤250 Ω, so it drives any mic or data input without further loading |
| **RV2** | 10k | RX level. **Omit entirely for a `DATA OUT` port** — it is already at the right level | |
| **C4** | 10 µF | DC block + high-pass. With the 5 kΩ bias Thévenin: fc ≈ 3 Hz | |
| **R5, R6** | 10k / 10k | Mid-rail bias, **1.65 V**, dead centre of the 0–3.1 V ADC window. Thévenin 5 kΩ | Drop to **4k7/4k7** (2.35 kΩ) if you see level errors — the ESP32 SAR wants a low source impedance |
| **R7 / C5** | 1k / 1 nF | Snubber for the SAR sampling-cap charge kick. **Not** an audio filter: fc ≈ 159 kHz | **Never** fit 100 nF here, the usual "ADC decoupling" reflex — with R7 that is a 1.6 kHz low-pass and it eats the 2200 Hz mark tone |
| **D1, D2** | BAT54S | Clamp GPIO33 to the rails. R7 limits the fault current. Cheap insurance against a volume knob at 3 Vrms | |
| **R8** | 470 Ω | ≈4.5 mA through the PC817 LED, sunk by GPIO26 — well inside the 12 mA comfortable / 20 mA absolute sink budget | |
| **R10** | 10k | **The one part people leave out.** Without it the MOSFET gate floats during reset and the rig can key on power-up | |

#### A full functional interface for the Baofeng UV-5R

The Baofeng UV-5R (and most of its Kenwood-K1-style two-pin clones — UV-82, BF-888, GT-3, RT-5R, etc.) does **not** expose a single combined mic/speaker/PTT jack. It exposes two:

| Plug | Size | Contacts | Signal |
|---|---|---|---|
| **Large plug** | 3.5 mm, TS (mono) | Tip / Sleeve | Tip = **SPKR audio out**, Sleeve = **GND** |
| **Small plug** | 2.5 mm, TRS | Tip / Ring / Sleeve | Tip = **MIC in**, Ring = **PTT** (short to Sleeve to key), Sleeve = **GND** |

That's the whole interface: TX is a mic-level signal into the small plug's tip, RX is a speaker-level signal out of the large plug's tip, and PTT is a **switch closure** between the small plug's ring and sleeve — not a logic level the radio reads, just a short. This lines up exactly with the [minimal functional schematic](#minimal-functional-schematic) above; only the destination pins change:

```
 ── TX ── ESP32 ───────────────────────────────────────────────────► UV-5R small plug ──

  GPIO25 ──[R1 2k2]──┬──[R2 2k2]──┬──[C1 10µ]──[R3 10k]──┬─ RV1 top
   (DAC)             │            │      +               │
                   [C2 15n]     [C3 15n]              [RV1 1k]  level trim
                     │            │                      ├─ wiper ──► 2.5 mm TIP  (MIC)
                    GND          GND                     │
                                                         └─ bottom ─► 2.5 mm SLEEVE (GND)

 ── RX ── UV-5R large plug ───────────────────────────────────────► ESP32 ──

                                          bias node
  3.5mm TIP (SPKR) ─[RV2 10k]─ wiper ─[C4 10µ]──┬──────[R7 1k]───┬──► GPIO33 (ADC)
                 │                +              │                │
   3.5mm SLEEVE ─┘                        [R5 10k]─► 3V3     [D1]─► 3V3   BAT54S
   (GND, common with small plug sleeve)        │             [D2]─► GND   (or 2×1N4148)
                                           [R6 10k]─► GND        │
                                               │              [C5 1n]
                                              GND                │
                                                                 GND

 ── PTT ── shorts the small plug's ring to its sleeve — option A, B, or C, unchanged ──

                      ┌─ PC817 ─┐
   3V3 ──[R8 470]──[A]│▶      C│──────────► 2.5 mm RING
   GPIO26 ─────────[K]│        E│──────────► 2.5 mm SLEEVE (GND)
                      └─────────┘
   GPIO26 LOW → LED on → ring shorted to sleeve → keyed.

 ── PTT ── option C: bare NPN transistor, non-isolated, needs ACTIVE_HIGH=1 ──

                       ┌─ 2N2222 / BC547 ─┐
   GPIO26 ──[R9 1k]────┤ B              C ├───► 2.5 mm RING
                       │                E ├───► 2.5 mm SLEEVE (GND)
          [R10 10k]────┤ B→E              │
                       └──────────────────┘
   GPIO26 HIGH → base current flows → C-E conducts → ring shorted to sleeve → keyed.
   R10 holds the base low (unkeyed) through reset and deep sleep — same job R10 does in the MOSFET option.
```

Everything left of the plugs — R1–R3, RV1, C1–C4, R5–R7, D1–D2, C5, R8 (or R9/R10 for option C) — is identical to the [parts table](#minimal-functional-schematic) above; only the endpoints move, from "rig MIC/DATA IN" and "rig SPKR/DISC" to the UV-5R's small- and large-plug tips.

Option C trades the opto's isolation for parts-bin convenience: any small-signal NPN works (2N2222, BC547, PN2200, S8050 — `hFE` ≥ 100 is plenty, since the collector current here is only a few mA through the K-plug's PTT switch contacts), and it's a two-resistor, one-transistor build instead of stocking an optocoupler. Like the MOSFET option, it **does not isolate** the ESP32's ground from the radio's, and it is **active-HIGH** — set `MODEM_PTT_ACTIVE_HIGH=1` (compile-time default or the runtime **Radio** page toggle) to match, exactly as for option B.

A few things specific to this radio:

* **No DATA IN / DATA OUT.** The UV-5R has no discriminator jack, so there is no way to reach the flat, fixed-level path this project's 9600 Bd G3RUH mode needs. Through the stock 2-pin connector, **AFSK 1200 Bd Bell 202 is the realistic ceiling.**
* **The mic level sits in the generic "Rig MIC IN" band** from the [what-each-end-actually-presents table](#what-each-end-actually-presents) — a few mV to a few tens of mV — so the R3/RV1 pad network is used exactly as specified; start RV1 near mid-rotation and trim for ≈3 kHz deviation per [Bring-up order](#bring-up-order).
* **Speaker output is volume-knob dependent.** Fix the UV-5R's volume at a low-to-moderate, repeatable setting (mark the knob) and do the level trim with RV2, not the radio's volume control — the AGC has the least headroom at the extremes of its range.
* **VOX is not used.** PTT is driven directly by the opto, MOSFET, or transistor switch, so leave the radio's VOX off; VOX fighting a hard PTT short is a good way to get truncated first characters or a stuck key-up.
* **Picking A vs. B vs. C:** option A (opto) is the only one of the three that isolates the ESP32 ground from the radio's, and it needs no polarity change from the shipped default — it's the one to reach for first. Options B and C are both non-isolated, active-HIGH low-side switches that differ only in which part you already have on hand (MOSFET vs. small-signal NPN); either is fine on a bench where hum and RF ingress aren't a concern.
* **Verify the pinout before soldering.** Cheap aftermarket 2-pin K-plug cables are not all wired the same — some third-party cables swap which small-plug contact is mic vs. PTT. Ring the plug out with a multimeter against the table above before committing; a swapped pair either floats the mic (no TX audio) or shorts PTT permanently (radio keys the instant it's plugged in).
* Ground loop and isolation guidance from [Isolation and ground loops](#isolation-and-ground-loops) applies unchanged — the small- and large-plug sleeves are the same node inside the radio, so treat them as one ground reference.

#### Why the PTT default is a trap

The shipped board definition is `MODEM_PTT_ACTIVE_HIGH=0` — **GPIO26 is driven LOW to key up**. The reflexive "NPN with the base off the GPIO, collector on PTT" circuit is an **active-HIGH** driver and will key your transmitter for as long as the ESP32 is *not* transmitting, i.e. permanently.

So: pick a driver whose polarity matches the config, or change the config to match your driver.

* **Option A (opto)** inverts, so it matches the shipped `ACTIVE_HIGH=0` as-is, and gives galvanic isolation for free.
* **Option B (MOSFET)** does not invert. Set `MODEM_PTT_ACTIVE_HIGH=1` in the top-level `CMakeLists.txt`, or flip the polarity at runtime on the **Radio** page (the pin and polarity are both runtime-selectable — see [Pinout / board definition](#pinout--board-definition)).

Either way, **verify before connecting the radio**: power the board, and with a meter confirm the PTT line is open through reset, through the whole ~5 s boot, and while idle. `modem_init()` blocks for about 5 seconds calibrating the ADC clock, and the beacon tasks transmit on entry — a wrong-polarity PTT gives you five seconds of unmodulated carrier before the firmware even reaches the modem.

HT "K-plug" rigs (Baofeng et al.) are a different animal: PTT there is a switch from the mic ring to sleeve, usually through a resistor, and mic audio shares the same conductor. The opto output goes across that switch, and the mic pad drives the same node.

#### Isolation and ground loops

The circuit above shares a ground with the radio, which is the normal source of hum, alternator whine and "it works until I transmit". If you hear any of that:

* **Audio isolation transformers**, 600:600 Ω (Bourns 42TL022, Tamura MET-01, or any 1:1 telecom transformer), in place of C1 and C4. The bias network stays on the ESP32 side of the RX transformer.
* **Keep the opto** (option A) so the PTT return does not re-create the ground you just broke.
* **RF ingress** on TX shows up as PTT latch-up or a modem that only fails at full power. Shielded cable, short leads, a clamp-on ferrite at the rig connector, and 47–100 pF from each audio line to the *rig's* chassis at the connector.

#### Bring-up order

1. **Loop test first, no radio.** GPIO25 → GPIO33 with a plain wire (see [Loopback bench wiring](#loopback-bench-wiring)). If that fails, no amount of external circuitry will help.
2. **RX next, still no TX.** Open the squelch, feed real traffic in, and watch the **AUDIO** column of the live traffic table (it is the modem's own `mVrms` at the pin). Turn RV2 for **≈300 mVrms on packets** — that is the AGC's target, so the loop sits at unity and has the most headroom in both directions. Anything from ~50 mV to ~800 mV will decode; below 40 mV you are running out of AGC gain, above 1.1 Vrms you are clipping. There is **no hardware squelch input** in this firmware — DCD comes from the demodulator — so leaving the squelch open is correct, not a workaround.
3. **TX last, into a dummy load.** Set RV1 for **≈3.0 kHz deviation** (2.5–3.5 kHz) with a deviation meter, or by comparing to a known-good station's audio on a second receiver. Over-deviation is the single most common cause of "my igate hears everyone but nobody hears me".
4. **9600 Bd G3RUH** needs the flat/discriminator path at both ends: `DATA IN`/`DATA OUT`, 10 nF in C2/C3, and the *Audio low-pass filter* checkbox set for `flat_audio`. A speaker output and a mic input will not carry it, no matter how the levels are set.

### Loopback bench wiring

For the [LOOP TEST](#the-loop-test), simply wire **`GPIO25` → `GPIO33`** (DAC out straight into ADC in). No radio, no PTT needed. The test transmits an APRS frame and expects the same board to decode it back.

---

## Repository layout

```
workspace-APRS/esp32_APRS_igate/
├── CMakeLists.txt                  ← board definition (ADC/DAC/PTT/LED pins) + project()
├── partitions.csv                  ← nvs / phy_init / factory(1500K) / storage(400K, LittleFS)
├── sdkconfig                       ← target=esp32, 4MB flash, custom partitions
├── dependencies.lock               ← idf 5.5.4, joltwallet/littlefs 1.22.1
├── LICENSE                         ← GPL-3.0
│
├── main/                                  (the application)
│   ├── main.c                      ← app_main, Wi-Fi bring-up/reconnect, boot order
│   ├── app_config.c/.h             ← app_config_t, factory defaults, JSON load/save
│   ├── storage.c                   ← LittleFS mount/format/usage
│   ├── aprs_service.c/.h           ← the glue: RX dispatch, TX helper, modem cfg, stats, loop test
│   ├── beacon.c/.h                 ← 3 independent beacon tasks (trk / igate / digi)
│   ├── net_state.c/.h              ← "do we actually have internet?" flag
│   ├── time_sync.c/.h              ← SNTP (UTC always)
│   └── cpu_freq.c/.h               ← esp_pm_configure() from the System page
│
├── components/
│   ├── esp32idf_radioamateur_modem/       (the soft-modem — the heart of the project)
│   │   ├── esp32idf_radioamateur_modem.h  ← public API + APRS convenience layer
│   │   ├── include/…_config.h             ← ALL compile-time board/DSP constants
│   │   ├── src/afsk.c    (1447 ln)        ← ADC DMA ingest, AGC, decimation FIR, DAC ISR, PTT
│   │   ├── src/modem.c   (899 ln)         ← correlators, DPLL, tone tables, DCD, calibration
│   │   ├── src/ax25.c    (1326 ln)        ← HDLC framer, NRZI, bit-stuffing, AX.25 codec, TX queue
│   │   ├── src/fx25.c, lwfec/rs.c, gf.c   ← FX.25 Reed–Solomon FEC
│   │   └── src/crc_ccit.c                 ← FCS
│   │
│   ├── igate/          ← APRS-IS TCP client, login, filters, dedup, RF→INET / INET→RF
│   ├── digirepeater/   ← WIDEn-N / TRACEn-N / RELAY / ECHO / GATE path logic
│   ├── message/        ← APRS messaging, ack/retry, AES-128-CBC + base64
│   ├── lastheard/      ← in-RAM ring of heard stations → dashboard JSON
│   ├── trafficlog/     ← in-RAM ring of traffic lines → dashboard JSON (seq-based long-poll)
│   └── webconfig/      ← esp_http_server admin
│       ├── web_server.c            ← route table
│       ├── web_common.c            ← auth, form parsing, HTML shell, field helpers
│       ├── pages/*.c               ← one file per admin page
│       └── translations/           ← translations.h + lang_en.h + lang_es.h + lang_it.h
│
└── managed_components/joltwallet__littlefs/   (fetched by the component manager)
```

**Source size:** ~18.2 k lines of first-party C across `main/` + `components/` (excluding `managed_components/`), of which ~4.9 k is the modem DSP core and ~3.4 k is the web admin.

---

## Architecture

### Boot sequence

`app_main()` runs on the system main task, whose stack is fixed at `CONFIG_ESP_MAIN_TASK_STACK_SIZE` (3584 B) — far too small for `esp_netif` + `esp_wifi` + `esp_http_server` + cJSON. So `app_main()` does only the two things that must precede everything, then hands off:

```
app_main()
 ├─ nvs_flash_init()          (erase+retry on NO_FREE_PAGES / NEW_VERSION_FOUND)
 ├─ storage_init()            (mount LittleFS at /storage, auto-format on first boot)
 └─ xTaskCreate(app_task, 8192 B, prio 5)   ── and returns; FreeRTOS reclaims the main task

app_task()
 ├─ app_config_load()                  ← /storage/config.json, or write+load factory defaults
 ├─ cpu_freq_apply()                   ← 80/160/240 MHz from the System page
 ├─ net_state_init()                   ← "no internet yet"
 ├─ wifi_init()                        ← AP / STA / AP+STA per g_config.wifi_mode
 ├─ vTaskDelay(10 ms)                  ← yield so IDLE runs; avoids a false TWDT trip
 ├─ time_sync_start()                  ← SNTP, non-blocking
 ├─ web_server_start()                 ← esp_http_server, 64 URI handlers, 8 KB stack
 ├─ aprs_service_start()               ← ⚠ MUST precede modem_init(): installs the RX callback
 │    ├─ trafficlog_init / lastheard_init / message_init
 │    ├─ message_set_tx_handler / igate_set_inet2rf_handler
 │    ├─ modem_set_rx_callback(on_rx_frame)
 │    ├─ igate_start()                 ← always started; self-idles when nothing needs APRS-IS
 │    ├─ beacon_start()                ← 3 tasks
 │    └─ xTaskCreate(serviceTickTask)  ← 1 Hz message-retry tick
 ├─ if (audio_modem_en) modem_init()   ← ⏳ BLOCKS ~5 s calibrating the real ADC clock (once per boot)
 │      └─ aprs_service_notify_modem_ready()
 └─ APRS_setCallsign(...)
```

Two ordering rules are load-bearing and commented as such in the source:

1. **`aprs_service_start()` before `modem_init()`** — the modem starts delivering frames *from inside* `modem_init()`; the callback must already be installed.
2. **Beacons start before the modem is ready** — they transmit immediately on entry, so `aprs_service_send_tnc2()` drops frames with a debug log until `s_modemReady` is set, rather than reaching `Ax25WriteTxFrame()` before `Ax25Init()` ran.

### Task map

| Task | Stack | Prio | Core | Created by | Role |
|---|---|---|---|---|---|
| `app_task` | 8192 B | 5 | any | `app_main` | boot + idle |
| modem RX DSP | `MODEM_RX_TASK_STACK` 4096 B | 10 | **0** (`MODEM_RX_TASK_CORE`) | `AFSK_init()` | drains the ADC ring, runs the demodulators |
| `modem_svc` | 6144 B | — | any | `modem_init()` | drives `AFSK_ServiceTx()` / `Ax25TransmitCheck()`, delivers RX frames to the callback |
| ADC DMA ISR | — | — | **0** (`MODEM_ADC_ISR_CORE`) | driver | conversion frames → ring buffer |
| DAC sample clock (GPTimer, level 3) | — | — | **1** (`MODEM_DAC_TIMER_CORE`) | `AFSK_init()` | one DAC sample every 1/38400 s |
| `igate_task` | — | — | any | `igate_start()` | APRS-IS socket, login, RX pump, reconnect |
| `trk_beacon_task` / `igate_beacon_task` / `digi_beacon_task` | `BEACON_TASK_STACK_WORDS` | 4 | any | `beacon_start()` | own-position beacons |
| `aprs_svc_tick` | 3072 B | 4 | any | `aprs_service_start()` | 1 Hz message retry |
| `httpd` | 8192 B | — | any | `web_server_start()` | web admin |
| `esp_timer` | — | — | — | IDF | Wi-Fi reconnect back-off |

### Data flow

```
                        ┌──────────────── RF RX ────────────────┐
  radio audio ─► ADC1 (DMA, 76800 Hz)
                   │
                   ├─ adc_ingest()  (un-swap DMA pairs, DC-block, AGC, RMS)
                   ├─ decimation FIR ─► 9600 Hz demod stream
                   ├─ correlator ×1–2  ─► DPLL bit recovery ─► NRZI ─► HDLC
                   ├─ FX.25 RS decode (optional)
                   └─ AX.25 frame ──► modem_rx_frame_t ──► on_rx_frame()          [aprs_service.c]
                                                              │ ax25_decode()
                                                              ▼
                                                       s_rxHook ──► aprs_msg_callback()
                                                              │
             ┌────────────────────────────┬──────────────────┼───────────────────┐
             ▼                            ▼                  ▼                   ▼
   trafficlog_add_pkt("RX")     lastheard_add(RF)    digiProcess()        igateProcess()
                                                        │ =2 → rewrite       │ →  qAR/qAO line
                                                        ▼                    ▼
                                            aprs_service_send_tnc2()   APRS-IS socket
                                                                            │
                        ┌──────────────── INET → RF ────────────────────────┘
                        ▼
                inet2rfHandler(line) ─► lastheard_add(INET)
                                     ├─ handleIncomingAPRS()  (messages/acks)
                                     └─ aprs_service_send_tnc2(line)      [if inet2rf]

                        ┌──────────────── RF TX ────────────────┐
  aprs_service_send_tnc2(text,len)
        ├─ drop if !s_modemReady  or  len ≥ AX25_FRAME_MAX_SIZE (329)
        ├─ modem_send_tnc2() ─► ax25_encode() ─► TX queue
        └─ CSMA wait (unless full_duplex) ─► PTT on ─► preamble (TXDelay)
                 ─► HDLC + bit-stuffing + FCS ─► NRZI ─► phase accumulator
                 ─► 512-entry sine LUT ─► DAC ISR @ 38400 Hz ─► GPIO25 ─► radio
```

---

## The modem component (`esp32idf_radioamateur_modem`)

Vendored under `components/`, GPL-3.0, by **Emiliano Augusto González (LU3VEA)** — upstream: <https://github.com/hiperiondev/esp32idf_radioamateur_modem>. It is derived from **VP-Digi** (SQ8VPS), **ESP32APRS_Audio** (nakhonthai) and **LibAPRS** (Mark Qvist).

### Signal chain

| Stage | Rate | Where |
|---|---|---|
| SAR-ADC1 continuous/DMA, 128-sample conversion frames | **76 800 Hz** | driver ISR on core 0 |
| ingest: pair un-swap, DC-offset removal, AGC, RMS metering | 76 800 Hz | `afsk.c` |
| decimation FIR (ratio **8:1**) | → **9 600 Hz** | `afsk.c` |
| correlator (mark/space), low-pass, DPLL, NRZI decode | 9 600 Hz | `modem.c` |
| HDLC de-framing, bit de-stuffing, FCS check, FX.25 RS decode | — | `ax25.c` / `fx25.c` |
| ⟵ TX ⟶ AX.25 encode, FCS, bit stuff, NRZI, 32-bit phase accumulator, 512-entry sine LUT | **38 400 Hz** | `ax25.c` / `modem.c` / `afsk.c` |

Profiles (`modem_mode_t` / `enum ModemType`, same numbering in both, which is why the app can plain-cast):

| Value | Profile | Baud | Tones |
|---|---|---|---|
| 0 | AFSK300 | 300 | 1600 / 1800 Hz |
| 1 | **Bell 202** (default, standard APRS) | 1200 | 1200 / 2200 Hz |
| 2 | ITU V.23 | 1200 | 1300 / 2100 Hz |
| 3 | G3RUH FSK | 9600 | — |

The 1200 Bd profile runs **two demodulators in parallel**, tuned slightly differently, to raise decode probability (`MODEM_MAX_DEMODULATOR_COUNT = 2`).

### Why the numbers are what they are

This component's config header is unusually well-documented, and the reasoning matters if you touch it:

* **ADC at 76 800 Hz, not 38 400.** 38 400 gives the 9600 Bd profile exactly *four* ADC samples per symbol. The DPLL's sample instant is then quantised to 25 % of a symbol and `decode()`'s three-sample majority vote spans 75 % of a symbol — the vote window always reaches into a transition. Host simulation of the real `modem.c`, with real clocks and **no noise**, produced hard bit errors at every phase where ADC instants line up with DAC update instants; the two clocks differ by ~0.05 %, so the alignment walks through those phases every ~55 ms. At 76 800 the same simulation gives zero bit errors at every phase and with up to 30 µs of TX edge jitter. AFSK profiles never cared (they're demodulated at 9600 Hz through a correlator after decimation) and measure identically at either rate. **Cost:** twice the RX DSP work and `MODEM_RESAMPLE_RATIO` becomes 8, which requires the longer decimation FIR — an 8-tap filter cut for 4:1 does not anti-alias 8:1.
* **DAC stays at 38 400 Hz** (= 32 × 1200, an exact multiple of every supported baud rate). The transmitter puts symbol edges exactly on DAC samples whatever the rate; it was the *receiver* that needed resolution.
* **`MODEM_ADC_CONV_FRAME = 128`, not the block size.** The IDF's own ADC ISR calls `xRingbufferSendFromISR()`, which does the whole `memcpy` **inside `portENTER_CRITICAL_ISR()`**. On Xtensa that raises `PS.INTLEVEL` to 3 — and the DAC sample clock *is* a level-3 interrupt. So the DAC ISR is masked for the length of the copy: 768 samples ≈ 11 µs (10 % of a 9600 Bd symbol — fatal), 128 samples ≈ 2 µs (2 % — inside budget). No amount of `IRAM_ATTR` on our side helps: the blocking code is the driver's, already in IRAM, and simply long. At 1200 Bd 11 µs is 1.3 % of a symbol and invisible — which is exactly why every AFSK profile passed while G3RUH dropped frames.
* **`MODEM_DAC_TIMER_CORE (1) ≠ MODEM_ADC_ISR_CORE (0)`.** `portENTER_CRITICAL_ISR()` masks level ≤3 on the *local* core only. Put the DAC clock on the other core and the ADC ISR merely spins for the lock instead of masking it. Enforced with `#error`. The two fixes (small frames, split cores) are independent and both are applied.
* **`ModemCalibrateSampleRate()`** — `modem_init()` blocks ~5 s at boot measuring the *real* ADC rate (`modem_measure_adc_rate()`), because every profile's PLL step is computed from the *nominal* ADC/DAC ratio and the gap is otherwise a steady-state error the DPLL must track for a whole transmission. The DAC alarm rate is already known exactly from the timer config (`afskGetDacAlarmRate()`), so only the ADC side needs measuring. Both clocks derive from the same crystal, so the ratio is a fixed board property: measured **once per boot**, reapplied on every profile switch.
* **`MODEM_RX_FIFO_SIZE = 4096` samples** — sized in *samples*, so it shrank in *time* when the rate doubled (2048 was 53 ms at 38.4 k, only 26.7 ms at 76.8 k — barely one 20 ms block). 4096 restores the margin. Checked: must hold ≥ 2 blocks, since `AFSK_Poll()` consumes whole blocks only.

Compile-time `#error` guards enforce: DAC pin ∈ {25, 26}; ADC pin ∈ 32–39; `MODEM_ADC_SAMPLERATE % 9600 == 0`; FIFO ≥ 2 blocks; `MODEM_ADC_CONV_FRAME` even, dividing `MODEM_BLOCK_SIZE`, and byte-aligned to `SOC_ADC_DIGI_DATA_BYTES_PER_CONV`; DAC timer core ≠ ADC ISR core; DAC timer priority ∈ 1..3.

### Compile-time configuration reference

All in `components/esp32idf_radioamateur_modem/include/esp32idf_radioamateur_modem_config.h`, every macro `#ifndef`-guarded so the build system can override it.

| Macro | Default | Meaning |
|---|---|---|
| `MODEM_DAC_GPIO` | 25 | audio out; 25 or 26 only |
| `MODEM_ADC_GPIO` | 33 | audio in; 32–39 only |
| `MODEM_PTT_GPIO` | −1 | fallback PTT default (overridden at runtime) |
| `MODEM_PTT_ACTIVE_HIGH` | 1 | fallback polarity |
| `MODEM_LED_TX_GPIO` / `MODEM_LED_RX_GPIO` | −1 | status LEDs |
| `MODEM_DAC_SAMPLERATE` | 38400 | = 32 × 1200 |
| `MODEM_ADC_SAMPLERATE` | 76800 | = 8 × 9600 |
| `MODEM_ADC_RATE_NUM` / `_DEN` | 1 / 1 | fudge factor on the requested ADC rate |
| `MODEM_DAC_AMPLITUDE_PCT` | 60 | DAC swing, % of 0–3.3 V |
| `MODEM_ADC_ATTEN` | `ADC_ATTEN_DB_12` | ≈0–3.1 V window |
| `MODEM_RX_FIFO_SIZE` | 4096 | samples, power of two |
| `MODEM_ADC_CONV_FRAME` | 128 | samples per DMA frame |
| `MODEM_ADC_POOL_FRAMES` | 32 | driver pool depth (= 53 ms) |
| `MODEM_RX_TASK_PRIO` / `_STACK` / `_CORE` | 10 / 4096 / 0 | RX DSP task |
| `MODEM_ADC_ISR_CORE` | 0 | ADC DMA ISR core |
| `MODEM_DAC_TIMER_CORE` | 1 | **must differ from ADC ISR core** |
| `MODEM_DAC_TIMER_INTR_PRIO` | 3 | 1..3 |
| *(derived)* `MODEM_DEMOD_SAMPLERATE` | 9600 | fixed |
| *(derived)* `MODEM_RESAMPLE_RATIO` | 8 | ADC ÷ demod |
| *(derived)* `MODEM_BLOCK_SIZE` | 1536 | 20 ms at 76.8 kHz |

### Runtime configuration (`modem_config_t`)

Built in exactly one place — `aprs_service_build_modem_config()` — shared by boot, the Radio page's Save (live re-apply, no reboot) and the loop test:

| Field | Source | Notes |
|---|---|---|
| `modem` | `afsk_modem_type` | plain cast; page clamps 0–3 |
| `flat_audio` | `audio_lpf` | despite the name, this always was the flat-audio-input flag |
| `full_duplex` | `false` normally | LOOP TEST passes `true` — a DAC→ADC wire means CSMA never sees a clear channel |
| `allow_non_aprs` | `false` | accept non-`0x03`/`0xF0` Control/PID? |
| `preamble_ms` | `preamble` (300) | TXDelay |
| `slot_time_ms` | `tx_timeslot` (2000) | CSMA quiet time |
| `fx25_mode` | `fx25_mode` | 0=off, 1=RX only, 2=RX+TX |
| `ptt_gpio` | `rf_ptt_gpio` | via `afsk_ptt_gpio_is_valid()`, else −1 |
| `ptt_active_high` | `rf_ptt_active` | |

**Explicitly *not* runtime-mapped** (no equivalent in the new component): ADC/DAC pins & attenuation (compile-time), hardware squelch (`rf_sql_*` — RX gates on real DCD instead), RF power switch (`rf_pwr_*`), software squelch (`sql_level`), RX volume, AGC ceiling (`agc_max_gain` — the AGC is self-limiting).

**Public API surface** (`esp32idf_radioamateur_modem.h`): `modem_init`, `modem_deinit`, `modem_set_modem`, `modem_set_rx_callback`, `modem_send_raw`, `modem_build_frame_tnc2`, `modem_send_tnc2`, `modem_format_tnc2`, `modem_tx_busy`, `modem_measure_adc_rate`, plus a LibAPRS-style convenience layer (`APRS_setCallsign`, `APRS_setPath1/2`, `APRS_setSymbol`, `APRS_setPower/Height/Gain/Directivity`, `APRS_sendLoc`, `APRS_sendMsg`, `APRS_sendPkt`, `APRS_printSettings`).

---

## Application components

### `igate` — APRS-IS gateway

* **TCP client** over LWIP sockets with auto-reconnect; re-reads `g_config` on every reconnect, so most web-admin changes land after the next reconnect cycle without a reboot.
* **Gated on real connectivity**, not on "Wi-Fi is up": it polls `net_state_is_connected()`, which only becomes true on `IP_EVENT_STA_GOT_IP` and false again on disconnect / AP-only mode.
* **Login line:** `user <mycall> pass <passcode> vers ESP32APRS 1.0 filter <filter>` — logged verbatim so a malformed filter is visible. The server banner and `# logresp … verified/unverified` line are surfaced; `unverified` raises a warning naming `aprs_mycall`/`aprs_passcode`.
* **RF→INET** (`igateProcess()`): drops frames whose path contains `RFONLY`, `TCPIP`, `qA*` or `NOGATE`; applies the `rf2inetFilter` bitmask (message/status/telemetry/weather/object/item/query/buoy/position); builds a `,qAR,<mycall>-<ssid>` header — or `,<mycall>-<ssid>*,qAO,<object>` for the object/satellite-gate form; de-duplicates against a 10-entry / 30 s cache.
* **INET→RF**: every non-`#` line increments `isRxCount`, is handed to `handleIncomingAPRS()` when messaging is on, and re-transmitted if `inet2rf` is set **and** the payload's type bit is set in `inet2rfFilter`. The type is decided by `aprs_filter_classify_tnc2()` (`main/aprs_filter.c`) from the APRS data type identifier plus, for position/object/item reports, the symbol (`_` → weather, `/N` → buoy). Unclassifiable payloads — third-party traffic `}` above all, the classic IGate-loop source — classify as 0 and are never relayed. Filtered lines are logged at `ESP_LOGD` only; the `RX-IS` traffic entry already covers them.
* **Shared uplink:** the task always runs, because the socket is also used by the message component (`igate_send_raw()`) and by "beacon to internet". It idles cheaply when nothing needs it.
* **Counters** (`igate_stats_t`): `rxCount`, `txCount`, `dropCount`, `dupCount`, `isRxCount` (all APRS-IS lines), `isTxCount` (all socket writes).

### `digirepeater` — digipeater

`digiProcess(ax25_msg_t*)` rewrites the path **in place** and returns:

| Return | Meaning |
|---|---|
| `0` | do not repeat (drop / not for us / already relayed) |
| `1` | repeat as-is (path already carries our used call, e.g. bypass `*`) |
| `2` | repeat with modified path — caller re-encodes and transmits |

Handles **WIDEn-N**, **TRACEn-N**, **RELAY / GATE / ECHO**, and WIDEn-N encoded in the destination SSID field. Counters: `rxPkts`, `txPkts`, `dropRx`, `erPkts` (malformed: too short / no path). Callsign/SSID come from `g_config.digi_mycall` / `digi_ssid`.

### `beacon` — own-station beacons

Three **independent FreeRTOS tasks** (`trk_beacon_task`, `igate_beacon_task`, `digi_beacon_task`), each with its own enable flag, interval, coordinates, symbol, comment and `loc2rf`/`loc2inet` routing. Each idles and re-checks periodically when disabled, so toggling in the web admin takes effect **without a reboot**. Timestamps are zulu/UTC (`051200z`) per the APRS spec — which is why `time_sync.c` pins the system clock to `TZ=UTC0` regardless of `g_config.timeZone`.

This is what makes the station itself appear on aprs.fi: the IGate and digipeater alone only relay traffic they hear; they never announce themselves.

**Not implemented:** GPS/live position and SmartBeaconing. These are fixed-station beacons from each page's saved coordinates only.

### `message` — APRS messaging

* 20-entry in-RAM queue (`MSG_QUEUE_SIZE`), 200-char text max.
* `sendAPRSMessage()`, `sendAPRSAck()`, `sendAPRSMessageRetry()` (ticked at 1 Hz by `aprs_svc_tick`), `handleIncomingAPRS()` parses any TNC2 line from RF *or* APRS-IS.
* **Encryption:** optional AES-128-CBC (`mbedtls`) with the IV derived via MD5 from the callsign + message ID, payload base64-encoded. Key = `g_config.msg_key`.
* Routing via a channel bitmask: `MSG_CHANNEL_RF (1<<0)` → `aprs_service_send_tnc2()`, `MSG_CHANNEL_INET (1<<1)` → `igate_send_raw()`.

### `lastheard` / `trafficlog` — dashboard feeds

* **`lastheard`** — ring of heard stations with a per-callsign packet counter, wall-clock timestamps (once NTP has synced), and APRS symbol table/code parsed from `!`/`=` position reports (the `/` and `@` timestamped forms are left unparsed — icon stays blank). Fed from **both** RF and APRS-IS, so there's something to see while verifying the uplink before the radio decodes anything. JSON: `[{"time":"HH:MM:SS","call":"…","path":"RF: WIDE1-1","sym":"91-1","packets":3}, …]`.
* **`trafficlog`** — ring of traffic lines with `esp_timer` timestamps and an ever-increasing **sequence number**, so the browser long-polls only what it hasn't seen: `GET /igate_traffic?since=<seq>`. Structured entries carry `dir` (`RX`/`TX`/`DIGI`/`INET2RF`/`RX-IS`), `dx`, raw `pkt`, and `au` (audio level in mV RMS, or −1).

Both are thread-safe and callable from any task.

### `webconfig` — web admin

`esp_http_server`, 64 URI handlers, wildcard URI matching, 8 KB handler stack, LRU purge. **HTTP Basic auth** against `g_config.http_username` / `http_password` on every page. HTML is emitted through small per-field helpers (`web_field_text`, `web_field_int`, `web_field_checkbox`, `web_select_*`, `web_field_symbol`, …) rather than one giant `snprintf` — deliberately, to avoid `-Werror=format-truncation`.

---

## Building and flashing

### Prerequisites

* **ESP-IDF v5.1 or newer** (locked/tested at **5.5.4** — see `dependencies.lock`).
* An ESP32 with **≥ 4 MB flash**.
* The IDF component manager fetches **`joltwallet/littlefs ^1.14`** automatically (locked at 1.22.1).

### Build

```bash
. $IDF_PATH/export.sh

cd workspace-APRS/esp32_APRS_igate

idf.py set-target esp32          # sdkconfig already ships with target=esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Build in Spanish or Italian instead of English:

```bash
idf.py build -DLANGUAGE=LANG_ES
idf.py build -DLANGUAGE=LANG_IT
```

> `espressif/esp-dsp` is intentionally **not** a dependency: it was pulled in only by the old `esp32_IDF_libAPRS` component. The current modem implements its own filters and nothing in the project calls `dsps_*`. If you're upgrading from an older checkout, delete `dependencies.lock` and let `idf.py` regenerate it.

### Partition table (`partitions.csv`)

| Name | Type | SubType | Offset | Size |
|---|---|---|---|---|
| `nvs` | data | nvs | 0x9000 | 24 K |
| `phy_init` | data | phy | 0xF000 | 4 K |
| `factory` | app | factory | 0x10000 | **1500 K** |
| `storage` | data | spiffs | (auto) | **400 K** → mounted as **LittleFS** at `/storage` |

Single `factory` app slot → **no OTA**. `sdkconfig` ships with `CONFIG_COMPILER_OPTIMIZATION_DEBUG=y` (`-Og`) and assertions on; switch to `-Os` if you're tight on flash.

---

## First boot & configuration

1. On a fresh partition, LittleFS auto-formats and `app_config_load()` writes `/storage/config.json` from factory defaults.
2. The ESP32 comes up as a **Wi-Fi AP**:
   * SSID **`esp32idf_APRS`**, password **`esp32idf_APRS`**, channel 1, max 4 clients, WPA2-PSK.
3. Join it and browse to the device (default `http://192.168.4.1/`).
4. **Log in: `admin` / `admin`** — change this on the *System* page.
5. On *Wireless*: pick **Station** or **AP+STA**, tick **Enable** in a Wi-Fi Client block, enter SSID/password, Save.
6. On *IGate*: set your **callsign**, **SSID**, **passcode**, APRS-IS **host**/**port**, filter, coordinates, symbol, comment.
7. On *Radio / Modem*: enable the audio modem, pick the modulation, PTT pin and polarity, preamble, TX time slot.
8. Reboot (or Save — most things re-apply live).

### Notable factory defaults

| Setting | Default |
|---|---|
| Wi-Fi mode | AP (`2`) — always reachable |
| AP SSID / pass | `esp32idf_APRS` / `esp32idf_APRS` |
| Web login | `admin` / `admin` |
| Hostname | `ESP32APRS` |
| CPU frequency | 160 MHz |
| Time zone | 7.0 (clock itself is always UTC) |
| NTP hosts | `pool.ntp.org`, `time.google.com`, `time.cloudflare.com` |
| IGate | **enabled**, `rf2inet` on, `inet2rf` off |
| Callsign / SSID | `NOCALL` / 10, passcode `-1` |
| APRS-IS host / port | `aprs.dprns.com` : 14580 |
| IGate position | 13.7563 / 100.5018 (Bangkok), interval 30 |
| Symbol | `N&` |
| Path preset 0 | `WIDE1-1,WIDE2-1` |
| Digipeater | disabled, SSID 1 |
| Tracker | disabled, SSID 9 |
| Audio modem | **enabled**, 1200 Bd Bell 202 |
| Preamble / TX slot | 300 ms / 2000 ms |
| FX.25 | off |
| PTT | GPIO26, active low |
| Messaging | enabled, RF + INET, encryption off |

> 🔴 **Change `NOCALL` and set a real passcode before transmitting.** Also verify you're licensed for the frequency and the duty cycle you're about to key up on.

---

## Web admin reference

### HTTP routes

| Method | Route | Purpose |
|---|---|---|
| GET | `/` | root / login landing |
| GET | `/logout` | drop Basic auth |
| GET | `/dashboard` | live dashboard |
| GET | `/style.css` | shared stylesheet |
| GET | `/sidebarInfo` | sidebar stats fragment |
| GET | `/sysinfo` | system info |
| GET | `/dashinfo` | compact live info strip (JSON) |
| GET | `/lastheard` | LAST HEARD table (JSON) |
| GET | `/igate_traffic?since=<seq>` | traffic log delta (JSON) |
| GET/POST | `/wireless` | Wi-Fi mode, AP, 5 STA slots, TX power |
| GET | `/wifiscan` | AP scan results (JSON) |
| GET/POST | `/system` | login, hostname, CPU freq, NTP, path presets, reset timeout |
| POST | `/default` | factory reset |
| GET/POST | `/igate` | IGate settings |
| GET/POST | `/digi` | digipeater settings |
| GET/POST | `/tracker` | tracker settings |
| GET/POST | `/wx` | weather (scaffolding) |
| GET/POST | `/tlm` | telemetry (scaffolding) |
| GET/POST | `/sensor` | sensors (scaffolding) |
| GET/POST | `/radio` | RF module + audio AFSK modem |
| GET | `/radio/looptest` | run the loop test (JSON result) |
| GET/POST | `/vpn` | WireGuard (scaffolding) |
| GET/POST | `/mqtt` | MQTT (scaffolding) |
| GET/POST | `/msg` | messaging |
| GET/POST | `/gnss` | GNSS (scaffolding) |
| GET/POST | `/mod` | GPIO / hardware mapping |
| GET | `/symbol` | APRS symbol reference/picker |
| GET | `/test` | config self-test summary |
| GET | `/storage` | file browser |
| GET | `/download?file=…` | download from LittleFS |
| GET | `/delete?file=…` | delete a file |
| POST | `/upload` | multipart upload |
| POST | `/format` | reformat LittleFS |
| GET | `/about` | firmware/IDF version, partition, OTA note |

### Page-by-page

**Dashboard** — Network Status pills (Wi-Fi, APRS-IS via `igate_is_connected()`), STATISTICS panel, LAST HEARD table with symbol icons, and a live traffic table (DX / PACKET / AUDIO columns) fed by sequence-based long polling.

The statistics come from `aprs_service_get_stats()`, tracked **independently** of `igate_en`/`digi_en`:

| Counter | Meaning |
|---|---|
| `radio_rx` | every frame the modem decoded off RF |
| `radio_tx` | every frame successfully transmitted on RF |
| `rf2inet` | frames the IGate actually uplinked |
| `inet2rf` | APRS-IS lines actually transmitted on RF |
| `digi` | frames digipeated (path rewritten + retransmitted) |

> This is deliberate. The counters used to be improvised from `digi_get_stats()`/`igate_get_stats()`, which only move from inside `digiProcess()`/`igateProcess()` — so with both features off (a common RX-only/monitor setup) the dashboard stayed at zero forever no matter how much traffic was decoded.

**Radio / Modem** — *Protocol*: FX.25 toggle. *RF module* (only when `ENABLE_RF_MODULE`): SX127x/SX126x/SX128x type, LoRa/G3RUH/GFSK/D-PRS, RX/TX MHz, CTCSS/DCS. *Audio / AFSK*: enable, modulation (300 / 1200 Bell202 / 1200 V.23 / 9600 G3RUH), **PTT GPIO dropdown** (only valid pins offered, plus *Disabled*), PTT active-high, audio LPF (flat-audio), preamble ms, TX time slot ms, and the **LOOP TEST** button. Save re-applies the modem live via `aprs_service_apply_modem_config()` — no reboot.

**IGate** — enable, RF→INET / INET→RF, both filter bitmasks, callsign/SSID/passcode, host/port, server-side filter string, beacon on/off, lat/lon/alt, interval, symbol picker, object, comment, status, PHG (computed client-side from power/gain/height/direction, persisted so the form redisplays).

**Wireless** — mode (off/STA/AP/AP+STA), AP SSID/pass/channel, 5 STA slots each with its own **Enable** checkbox, TX power in dBm (converted ×4 to quarter-dBm for `esp_wifi_set_max_tx_power()`), plus a live scan. The scan temporarily flips an AP-only radio to AP+STA — which is why `s_staEnabled` gates every automatic `esp_wifi_connect()`, so the event handler doesn't fight the scan.

**System** — web login, hostname, CPU frequency (applied live), NTP hosts ×3, resync interval, reset timeout, and the **four path presets** `path[0..3]`.

**Storage** — LittleFS browser: download, delete, multipart upload, usage, format.

**About** — project name, version, build date/time, IDF version, running partition label/offset/size, and an explicit note that OTA is not available with this partition table.

---

## Configuration storage (`config.json`)

* Path: **`/storage/config.json`** on LittleFS.
* Serialized with **cJSON**; field names and JSON keys are kept **1:1 with the original `config.h`/`config.cpp`**, so every value the web admin shows has a home and old files load unchanged.
* **Atomic save**: write `/storage/config.json.tmp`, then rename.
* Missing or corrupt → defaults are applied **and immediately saved**, so the file always exists and is consistent.
* API: `app_config_set_defaults()`, `app_config_load()`, `app_config_save()`, `app_config_factory_reset()`. Live instance: `extern app_config_t g_config`.

### Compile-time module switches (`main/include/app_config.h`)

```c
#define ENABLE_DASHBOARD          #define ENABLE_IGATE
#define ENABLE_RADIO_MODEM        #define ENABLE_DIGIPEATER
//#define ENABLE_RF_MODULE        #define ENABLE_TRACKER
//#define ENABLE_VPN              //#define ENABLE_WEATHER
//#define ENABLE_MQTT             //#define ENABLE_TELEMETRY
#define ENABLE_MESSAGE            //#define ENABLE_SENSORS
//#define ENABLE_MOD_GPIO         #define ENABLE_SYSTEM
#define ENABLE_WIRELESS           //#define ENABLE_GNSS
#define ENABLE_FILE_STORAGE       #define ENABLE_ABOUT_FIRMWARE
```

Commenting one out removes its sidebar entry and its page from the image.

---

## Path presets and the path bitmask

Each service (tracker / igate / digi / wx / …) stores a **bitmask**, not a path string. Bit *N* selects **`g_config.path[N]`**, the free-text preset edited on the *System* page. `buildPathSuffix()` concatenates every selected non-empty slot; selected-but-empty slots are simply skipped.

Activation flags double as the default bitmask values:

```
ACTIVATE_OFF 0 · TRACKER 1<<0 · IGATE 1<<1 · DIGI 1<<2 · WX 1<<3
ACTIVATE_TELEMETRY 1<<4 · QUERY 1<<5 · STATUS 1<<6 · WIFI 1<<7
```

IGate filter bits (shared by `rf2inetFilter` and `inet2rfFilter`):

```
MESSAGE 1<<0 · STATUS 1<<1 · TELEMETRY 1<<2 · WEATHER 1<<3 · OBJECT 1<<4
ITEM 1<<5 · QUERY 1<<6 · BUOY 1<<7 · POSITION 1<<8
```

---

## The LOOP TEST

The single most useful bring-up tool in the project. Wire **GPIO25 → GPIO33**, open *Radio / Modem*, hit **LOOP TEST**.

What it does (`aprs_loop_test_run()`):

1. Builds a small APRS packet carrying a **random one-time token** (`>LOOPTEST <token>`).
2. **Diverts** decoded frames to its own hook (`s_rxHook`) so the test frame is never digipeated, uplinked to APRS-IS, or logged as real traffic.
3. Switches the modem to **full duplex** — a DAC→ADC wire means the node always hears its own carrier and CSMA would never key up.
4. Transmits, then waits up to **4000 ms** for the ADC → demodulator → HDLC → AX.25 chain to hand the same frame back.
5. **Always restores** the real hook and the configured duplex mode before returning.

Meanwhile a monitor task latches diagnostics the component only exposes instantaneously: a passive raw-ADC snapshot mid-preamble (`afskDiagCaptureRaw()`, straight off the conversion ISR, without disturbing the live RX task), peak RMS (`afskGetRms()`), peak AGC gain (`afskGetAgcGain()`), a DCD bitmap (`ModemDcdState()`), and the furthest HDLC RX stage reached per demodulator (`Ax25GetRxStage()`).

The failure messages then distinguish, in the same way the old component's purpose-built latching diagnostics did:

| Symptom | Diagnosis |
|---|---|
| raw ADC min≈max | ADC dead / not wired |
| raw swings, RMS ~0 | no tone reaching the ADC |
| RMS fine, DCD never set | tone present, PLL never locked → baud/modem-type mismatch or bad audio |
| DCD latched, `rxStageMax < RX_STAGE_FRAME` | flags seen but no frame ever started — bit-recovery bug, not noise |
| DCD latched, `rxStageMax = FRAME`, no frame | frames assembled but all failed CRC — marginal level/SNR |
| frame back, token mismatch | distortion, clipping, or wrong loopback wiring |
| PASS | reports the RX level in mV RMS |

Two distinctions the old diagnostics drew are **gone by design**: there's no software squelch (so no "squelch never opened"), and there are no CRC-failure counters (the furthest HDLC stage stands in).

---

## Localization

**One language per firmware image.** No runtime switch; no other language's strings are compiled in.

* `app_config.h` defines `LANG_EN 0`, `LANG_ES 1`, `LANG_IT 2` and the active `LANGUAGE` (default `LANG_EN`).
* `translations/translations.h` is the *only* place that decides which `lang_xx.h` gets included.
* Every user-visible string goes through a `TR_xxx` macro.

**Adding a language:**

1. Copy `translations/lang_en.h` → `lang_xx.h`, translate every literal, keep every macro name identical.
2. `#define LANG_XX <next free number>` in `app_config.h`.
3. Add an `#elif LANGUAGE == LANG_XX` branch in `translations.h`.
4. Build with `-DLANGUAGE=LANG_XX`.

Missing a `TR_xxx` in one language is a **compile error in that language's build** — intentional, so untranslated strings can't ship silently.

---

## Troubleshooting

**"I switched to Station mode, saved, rebooted, and nothing happens."**
Read the boot log — this path is instrumented heavily on purpose:

* `esp_wifi_connect()` is only legal once the station has *actually* started, which the driver signals with `WIFI_EVENT_STA_START`. Calling it right after `esp_wifi_start()` loses that race and returns `ESP_ERR_WIFI_NOT_STARTED`; no association, no `STA_DISCONNECTED`, therefore no retry. The connect is issued **from the STA_START handler** and every attempt logs its result.
* If no Wi-Fi Client slot is **enabled with an SSID**, the firmware dumps **every slot** and tells you which mistake it is ("enabled, but the SSID is EMPTY" vs "has an SSID, but 'Enable' is not ticked").
* STA-only with nothing to join would leave the device unreachable, so it **falls back to AP+STA** and says so — the web admin stays up.

**Disconnect reason codes** are logged (they used to be discarded):

| Reason | Meaning |
|---|---|
| 15 (`4WAY_HANDSHAKE_TIMEOUT`), 204 (`NOT_AUTHED`) | wrong password |
| 201 (`NO_AP_FOUND`) | SSID not visible: wrong name, out of range, or 5 GHz-only |
| 2 / 8 / 200 | ordinary roaming / AP-side drops |

Reconnects use a **growing back-off** (500 ms per consecutive failure, capped at 8 s), armed on an `esp_timer` — **not** `vTaskDelay()` inside the event handler, which would stall the shared event loop (including the very `IP_EVENT_STA_GOT_IP` it's waiting for) and, in a tight disconnect loop, starve the idle task until the task watchdog fired.

**"AP won't associate at all"** — a zeroed `wifi_config_t` leaves `pmf_cfg.capable = false`, and WPA3 / WPA2-with-PMF-required APs simply refuse such a station. The firmware sets *capable, not required*, which works against both old and new APs.

**"Boot hangs for ~5 seconds"** — expected: `modem_init()` blocks while `ModemCalibrateSampleRate()` measures the real ADC clock. Once per boot.

**"Beacons at boot don't transmit"** — expected: `aprs_service_start()` runs before `modem_init()`, so early beacons are dropped with a debug log until `s_modemReady`.

**"LOOP TEST fails with 'no packet received back'"** — check the ADC attenuation story: the DAC swings the full rail while a 0 dB attenuation only measures ~0–1.1 V, clipping the tone beyond the demodulator's ability to lock. The component hard-codes `ADC_ATTEN_DB_12`, which is correct; if you overrode it, put it back.

**"IGate says unverified"** — wrong `aprs_mycall` / `aprs_passcode`. The banner is logged; so is the exact login line, including the filter string, so a malformed filter is visible immediately.

**"Everything works but aprs.fi doesn't show my station"** — beacons: enable `igate_bcn` and at least one of `igate_loc2rf` / `igate_loc2inet`, and set real coordinates. Relaying traffic never announces you.

**"9600 Bd loses frames"** — that's the pathology the ADC rate, conversion-frame size and core split were changed to fix. If you've overridden `MODEM_ADC_SAMPLERATE`, `MODEM_ADC_CONV_FRAME`, `MODEM_DAC_TIMER_CORE` or `MODEM_ADC_ISR_CORE`, re-read [Why the numbers are what they are](#why-the-numbers-are-what-they-are). Also confirm you're feeding **flat/discriminator** audio.

---

## Status & known limitations

* **Work in progress.** The upstream README says so, and so does this one.
* **No OTA** — single `factory` partition; flash over serial.
* **`rf2inetFilter` is not applied.** `igateProcess()` still applies only the RFONLY/TCPIP/qA/NOGATE/satellite rules and dedup, not the payload-type bitmask. `aprs_filter_classify_tnc2()` / `aprs_filter_pass()` are direction-agnostic, so wiring it there is a two-line change.
* **Only the first enabled Wi-Fi STA slot is used.** Multi-AP failover is noted as "can be added later".
* **No GPS, no SmartBeaconing.** Config fields exist; beacons are fixed-position only.
* **No LoRa / RF-module driver.** `ENABLE_RF_MODULE` is commented out; the SX12xx UI and config are scaffolding.
* **VPN / MQTT / GNSS / weather / telemetry / sensors / Bluetooth / PPP / OLED / Modbus**: config fields and (some) pages exist, no implementations.
* **Symbol parsing** only covers the no-timestamp `!` / `=` position formats; `/` and `@` leave the icon blank.
* **`agc_max_gain`, `sql_level`, `volume`, `adc_gpio`, `dac_gpio`, `rf_sql_*`, `rf_pwr_*`, `adc_atten`** are inert since the modem swap; kept only for `config.json` compatibility.
* `sdkconfig` ships with `-Og` + assertions, not a release profile.

---

## Porting notes

Migrating from the old **`esp32_IDF_libAPRS`** component to **`esp32idf_radioamateur_modem`** changed several contracts. If you're carrying patches forward:

| Old | New |
|---|---|
| ADC/DAC/PTT pins in `aprs_modem_config_t` at **runtime** from `g_config` | **compile-time** `MODEM_*_GPIO` via `idf_build_set_property()` in the top-level `CMakeLists.txt` (PTT is the exception: now runtime again, validated) |
| App pumped `AFSK_Poll()` / `APRS_poll()` from its own task | The component owns both: `AFSK_init()` starts a pinned RX DSP task; `modem_init()` starts `modem_svc`. Calling `AFSK_Poll()` yourself now **races** that task over the same FIFO. |
| Component decoded frames and called a global `ax25_callback_t _hook` with a ready-made `AX25Msg` | Component hands back **raw AX.25 bytes**; the app does `ax25_decode()` in `on_rx_frame()` and dispatches through its own `s_rxHook` indirection |
| `APRS_sendTNC2Pkt(raw, len)` | `modem_send_tnc2(const char*)` — NUL-terminated; `aprs_service_send_tnc2()` does the pointer+length conversion and the `AX25_FRAME_MAX_SIZE` check centrally |
| Local `ax25ToTnc2()` | thin wrapper over `modem_format_tnc2()` so the two renderings can't drift |
| Latching diagnostics (`AFSK_getAdcDiag`, `AFSK_getSquelchDiag`, `Ax25GetFrameDiag`, …) | Instantaneous getters (`afskGetRms`, `afskGetAgcGain`, `afskGetDcOffset`, `ModemDcdState`, `Ax25GetRxStage`, `ModemGetSignalLevel`) + a passive raw tap (`afskDiagCaptureRaw`); latching is now done by the app's loop-test monitor task |
| `espressif/esp-dsp` dependency | dropped — the modem implements its own filters |
| Software squelch, RX volume, AGC ceiling, RF power switch | **gone**, no equivalent. RX gates on the demodulator's real DCD. |
| `MODEM_DEFAULT_CONFIG()` ships `full_duplex = true` | targets the wire-loopback demo; **real on-air use must set `full_duplex = false`** or it will key up over anyone already transmitting |

---

## Credits

* **This project & the modem component:** Emiliano Augusto González — **LU3VEA** — `lu3vea @ gmail . com` · <https://github.com/hiperiondev>
* The modem is based on, and owes its DSP lineage to:
  * **VP-Digi** — SQ8VPS — <https://github.com/sq8vps/vp-digi>
  * **ESP32APRS_Audio** — nakhonthai — <https://github.com/nakhonthai/ESP32APRS_Audio>
  * **LibAPRS** — Mark Qvist — <https://github.com/markqvist/LibAPRS>

  Please contact their authors for information about those projects.
* **littlefs** — ARM/joltwallet (BSD-3-Clause), via the ESP component registry.
* Configuration schema, web-admin layout and dashboard semantics follow the reference **esp32idf_APRS / ESP32APRS** project so existing `config.json` files and user expectations carry over.

---

## License

**GNU General Public License v3.0** — see [`LICENSE`](LICENSE).

Bundled `managed_components/joltwallet__littlefs` carries its own license (BSD-3-Clause for littlefs itself).

---

### Amateur radio disclaimer

Transmitting on amateur radio frequencies requires a valid licence for your country and band. **Set a real callsign** (the default is `NOCALL`), use a legitimate APRS-IS passcode, respect your local band plan and digipeating conventions (`WIDE1-1,WIDE2-1` is *not* always appropriate), and don't gate `NOGATE`/`RFONLY` traffic. You are responsible for everything this device transmits.
