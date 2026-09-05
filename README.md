<p align="center">
  <img src="https://github.com/hiperiondev/esp32idf_APRS/raw/main/images/logo.png" width="300">
</p>

<div align="center">

# esp32idf_APRS

### A complete APRS station on a single ESP32 — native ESP-IDF, no Arduino.

**IGate · Digipeater · Tracker · Weather · Telemetry**, with a built-in web admin, an on-chip AFSK/FSK soft-modem, APRS-IS uplink, a runtime sensor-driver framework and OTA firmware updates.

[![Docs](https://img.shields.io/badge/docs-readthedocs-blue)](https://esp32idf-aprs.readthedocs.io/)
[![License](https://img.shields.io/badge/license-GPLv3-green)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-ESP32-red)](#hardware)
[![Framework](https://img.shields.io/badge/framework-ESP--IDF%205.x-orange)](#)

**🌐 Languages:** **English** · [Español](README.es.md) · [Italiano](README.it.md)

</div>

---

## What is this?

`esp32idf_APRS` turns a bare **ESP32 DevKit** plus a cheap audio interface into a complete, self-contained **APRS** station. Everything runs on the ESP32 itself — there is no Arduino core, no PlatformIO, and no external DSP library. The entire signal chain, from the correlator demodulator through DPLL bit recovery, NRZI, the HDLC framer, the AX.25 codec and Reed–Solomon FX.25 forward error correction, executes on the microcontroller using only the SAR-ADC in continuous/DMA mode, the DAC, and a general-purpose timer.

In one sentence, the firmware **demodulates** AFSK/FSK audio from a radio's speaker or discriminator output, **decodes** HDLC/AX.25 (optionally FX.25 error-corrected) frames, **gates** them to APRS-IS over Wi-Fi, **digipeats** them back on RF, **beacons** its own position, weather and telemetry, **modulates** and transmits frames back through the ESP32's 8-bit DAC — and is configured entirely through a web admin served by the device itself. No serial console, no recompilation for ordinary settings.

> 📖 **The full, exhaustive documentation lives at [esp32idf-aprs.readthedocs.io](https://esp32idf-aprs.readthedocs.io/)** — trilingual (English / Español / Italiano), with getting-started guides, hardware wiring, the DSP signal chain, the configuration engine, HTTP routes and troubleshooting. **This README is only a presentation. For anything beyond a first look, go to the docs.**

---

## Highlights

- **On-chip soft-modem.** AFSK 1200 Bd Bell 202 (standard APRS) with a dual demodulator, plus AFSK 1200 Bd V.23, AFSK 300 Bd, and **G3RUH 9600 Bd FSK** — all in pure C on the ESP32's own ADC/DAC.
- **FX.25 forward error correction.** Reed–Solomon FEC over AX.25, RX-only or RX+TX, for reliable decodes in weak-signal conditions.
- **Full APRS-IS IGate.** Bidirectional **RF→INET** and **INET→RF** gating with duplicate suppression, `qAR`/`qAO` construction, payload-type gating, callsign budlists, a local range gate (haversine distance) and prefix whitelist. Up to four APRS-IS servers can be listed, with automatic failover between the enabled ones.
- **Digipeater.** A four-row n-N alias table (WIDE1-1 / WIDE2-2 / WIDE#-2 by default), each row with its own hop limit and trace/flood mode, plus hop-count trapping, fill-in-only operation and duplicate suppression.
- **Beacons, messaging & chat.** Fixed-position beacons for tracker/igate/digi, APRS text messaging with ack/retry (RF and/or INET), and an in-browser message chat UI.
- **Weather & telemetry.** On-air APRS Weather Reports with 1 Hz sensor refresh and per-field averaging, plus APRS Telemetry (analog A1–A5 + digital B1–B8) with `T#nnn` reports and metadata.
- **Winlink radio e-mail (APRSLink).** The station reads and writes its own `CALLSIGN@winlink.org` mail through the `WLNK-1` service — challenge/response login with the password never on the air, a paced one-command-at-a-time session, and a browser terminal whose mailbox listing carries per-message read/reply/forward/delete buttons — and, separately, relays a neighbouring RF station's own Winlink session through its IGate.
- **Objects, items & bulletins.** Up to five own-station APRS Objects/Items and five bulletins (BLN1–BLN5), each on RF and/or INET with expiry/decay control.
- **Telegram bot.** An optional bot alongside the APRS services — long polling, per-user and per-chat authorization, station-message and bulletin routing to Telegram, and a Mini App button — so the station can be checked on and lightly controlled from a phone.
- **Runtime sensor framework.** A dynamic, self-registering driver registry (`sensors_local`) — includes a BME280/BMP280 (I²C) driver out of the box, plus an optional BMP180 one on the same bus.
- **Web admin, 22 pages.** HTTP Basic auth, a live dashboard, a live traffic log and last-heard table (JSON long-poll), LittleFS file management (upload/download/delete/format), Wi-Fi AP/STA/AP+STA with scan and TX-power control, CPU frequency control (80/160/240 MHz), and an on-demand console log viewer that mirrors the serial output into the browser. The whole UI is responsive from one stylesheet — on a tablet or a phone the sidebar becomes a slide-in drawer, form rows collapse to a single column, wide tables scroll within the page and the controls grow to a touch size.
- **OTA updates with auto-rollback.** Dual `ota_0`/`ota_1` app slots; a failed image rolls back automatically on the next boot.
- **Trilingual UI.** English, Spanish and Italian (compile-time, one language per image).

---

## Feature matrix

| Area | Notes |
|---|---|
| AFSK 1200 Bd Bell 202 | Dual demodulator, default profile |
| AFSK 1200 Bd V.23 · AFSK 300 Bd · G3RUH 9600 Bd FSK | Multiple selectable modem profiles |
| HDLC / AX.25 UI frame RX + TX | Full soft-modem TX/RX path |
| FX.25 (Reed–Solomon FEC over AX.25) | RX-only / RX+TX modes |
| PTT keying | Compile-time GPIO + polarity, minimum-unkey hold |
| CSMA / TX time-slot / TXDelay preamble | `preamble`, `tx_timeslot` |
| Transmit duty-cycle limiter | Optional ceiling over a rolling 10-minute window |
| APRS-IS IGate RF→INET & INET→RF | Filters, dedup, budlist, third-party unwrap opt-in |
| APRS-IS multiserver failover | 4 server slots, circular retry over the enabled ones |
| Local range gate & prefix gate | Haversine distance + callsign-prefix whitelist |
| Digipeater | Configurable n-N alias table (trace/flood), hop trapping, dup-suppression |
| Objects / Items · Bulletins | Up to 5 each, RF and/or INET, expiry/decay |
| Telegram bot | Long polling, per-user/chat authorization, message & bulletin routing, Mini App button |
| Messaging + ack/retry · Chat UI | RF and/or INET |
| Winlink radio e-mail (APRSLink) | Own mailbox over `WLNK-1`, plus gateway for local stations |
| Weather Report | 1 Hz sensor refresh, optional averaging |
| Telemetry | Analog A1–A5 + digital B1–B8, `T#nnn` + metadata |
| Sensor driver framework | Dynamic registry, BME280/BMP280 driver included |
| Web admin | 22 pages, live dashboard, traffic + last-heard, console log viewer, per-option contextual help |
| Storage | LittleFS 512 KB, upload/download/delete/format |
| Networking | Wi-Fi AP/STA/AP+STA, scan, TX power, SNTP (UTC clock, selectable timezone for display) |
| CPU frequency control | 80 / 160 / 240 MHz |
| OTA update | `ota_0`/`ota_1` slots, auto-rollback |
| Localization | EN / ES / IT, compile-time, labels and help balloons alike |

---

## Hardware

- **Target:** ESP32 (classic, Xtensa dual-core), 4 MB flash. Dual-core is **required** — the ADC ISR and DAC sample clock are pinned to different cores on purpose.
- **Audio in (ADC):** default `GPIO33` (ADC1). **GPIO 32–39 only** — ADC2 is unusable while Wi-Fi is up.
- **Audio out (DAC):** default `GPIO25`. **GPIO 25 or 26 only** — the ESP32 DAC is hard-wired to those pads.
- **PTT:** default `GPIO26`, polarity selectable at compile time.
- **Note:** ESP32-S3/C3/C6/H2 have **no DAC** and cannot run the TX path unmodified.

Board wiring (audio pins, PTT pin/polarity, sample rates) is set as compile-time constants in the top-level `CMakeLists.txt`. A KiCad radio-interface schematic is included under `schematics/`.

> Full pinout tables and wiring constraints are in the [Hardware chapter of the documentation](https://esp32idf-aprs.readthedocs.io/en/latest/en/hardware.html).

---

## Quick start

```bash
# Requires ESP-IDF v6.x (tested and locked at 6.0.2)
idf.py set-target esp32
idf.py build
idf.py -p PORT flash monitor
```

On first boot the device brings up a Wi-Fi AP; connect and open the web admin to configure your callsign, radio and services. After the one-time USB/UART flash, all further updates can be done from the web admin's **About / Firmware** page over OTA.

> 📖 The step-by-step first-run guide is in [Getting Started](https://esp32idf-aprs.readthedocs.io/en/latest/en/getting-started.html).

---

## Documentation

**Everything is documented in full at 👉 [esp32idf-aprs.readthedocs.io](https://esp32idf-aprs.readthedocs.io/)**

The documentation is trilingual and organised into *Functionalities* (what the station does), *Capabilities* (cross-cutting properties), *Internals* (how it is built) and a *Reference* section:

- 🇬🇧 [English](https://esp32idf-aprs.readthedocs.io/en/latest/en/index.html)
- 🇪🇸 [Español](https://esp32idf-aprs.readthedocs.io/en/latest/es/index.html)
- 🇮🇹 [Italiano](https://esp32idf-aprs.readthedocs.io/en/latest/it/index.html)

This README is a presentation only — **please consult the documentation for installation, wiring, configuration and internals.**

---

## Credits & license

Created by **Emiliano Augusto González (LU3VEA)**.

Built on ideas from earlier projects — [VP-Digi](https://github.com/sq8vps/vp-digi), [ESP32APRS](https://github.com/nakhonthai/ESP32APRS_Audio) and [LibAPRS](https://github.com/markqvist/LibAPRS); please refer to their authors for more information.

Released under the **GNU General Public License v3**. See [LICENSE](LICENSE).
