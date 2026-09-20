# esp32idf_APRS audio test bench — user manual

🌐 **English** · [Español](README_es.md) · [Italiano](README_it.md)

Test the AFSK 1200 baud APRS receiver of the **esp32idf_APRS** firmware with
**real APRS traffic recorded off the air**, and measure how many packets the
ESP32 decodes compared with a reference decoder (**multimon-ng**).

---

## Contents

1. [What this test does](#1-what-this-test-does)
2. [What you need](#2-what-you-need)
3. [Build the audio cable (hardware)](#3-build-the-audio-cable-hardware)
4. [Prepare the ESP32 (firmware settings)](#4-prepare-the-esp32-firmware-settings)
5. [Prepare the PC](#5-prepare-the-pc)
6. [Get audio files with real APRS traffic](#6-get-audio-files-with-real-aprs-traffic)
7. [Set the audio level](#7-set-the-audio-level) ([auto-volume calibration](#71-automatic-volume-calibration))
8. [First run: check the whole chain with synthetic packets](#8-first-run-check-the-whole-chain-with-synthetic-packets)
9. [Run the real test](#9-run-the-real-test)
10. [Command-line reference](#10-command-line-reference)
11. [Reading the results](#11-reading-the-results)
12. [How packets are compared](#12-how-packets-are-compared)
13. [Suggested test plan with the WA8LMF tracks](#13-suggested-test-plan-with-the-wa8lmf-tracks)
14. [`gen_test_wav.py` in detail](#14-gen_test_wavpy-in-detail)
15. [Troubleshooting](#15-troubleshooting)
16. [Limitations](#16-limitations)
17. [Cheat sheet](#17-cheat-sheet)
18. [Glossary](#18-glossary)

---

## 1. What this test does

The ESP32 firmware decodes APRS packets from an audio signal on its ADC input
and prints every packet it decodes on its serial console (`RX: SRC>DST,PATH:payload`).
The question is: **of all the packets that are really in the audio, how many did
the ESP32 decode, and did it decode them correctly?**

The program answers it by feeding the **same audio to two decoders at the same
time**:

```
                       ┌─► sox (resample to 22050 Hz mono) ─► multimon-ng ──────────► packets A  (reference)
                       │                                                                  │
 WAV file ─────────────┤                                                                  ▼
 (real APRS traffic)   │                                                          test_aprs_wavs.py
                       │                                                          compares A and B
                       │                                                                  ▲ 
                       └─► play ─► PC sound card ─► RV1 + C1 ─► ESP32 ADC (GPIO33)        │
                                                                   │                      │ 
                                                            ESP32 AFSK modem              │
                                                                   │                      │
                                                             console log ─► USB serial ───┴► packets B
```

* **multimon-ng** is a well-known software decoder. Whatever it decodes from the
  file is used as the *list of packets that are really there*.
* The **ESP32** hears the same audio through the sound card and a small
  attenuator/coupling circuit, and reports what it decoded on the serial port.
* For every packet multimon-ng decodes, the program looks for the same packet in
  the ESP32 log. It prints the packet together with a verdict:
  **OK**, **DIFFERENT** or **NOT DECODED**.
* At the end it prints a summary: total packets, percent decoded correctly,
  percent decoded with different content, percent missing.

**Any WAV file with real APRS traffic (AFSK 1200 baud) can be used** — a recording
from a radio, from an SDR, or a published test track. Section 6 explains where to
get good ones and proposes the free **WA8LMF TNC test tracks**.

The test is **receive-only**: nothing is transmitted, and nothing needs to be
connected from the ESP32 back to the PC.

---

## 2. What you need

### Hardware

| Item | Notes |
|---|---|
| ESP32 board running the esp32idf_APRS firmware | Powered and connected to the PC by USB (this USB link is also the serial console). |
| PC with a sound card output | Headphone or line-out. A cheap dedicated USB sound card is a good idea (see section 5). |
| Audio cable, 3.5 mm plug | Tip = left channel, sleeve = ground. Either channel works: the program sends the same audio to both. |
| RV1: 2 kΩ multi-turn trimmer | Sets the level. Multi-turn allows fine adjustment. **Optional** — section 3.1 gives a fixed-resistor alternative if you don't have a trimmer. |
| C1: capacitor, 1 µF or larger | Electrolytic is fine (mind the polarity!), rated 10 V or more. |
| Jumper wires | |

### Software (Linux)

The program is written for **Linux with ALSA** and needs:

| Software | Why | Install (Debian/Ubuntu) |
|---|---|---|
| Python 3.7+ | runs the program | usually already installed |
| pyserial | reads the ESP32 console | `sudo apt install python3-serial` |
| multimon-ng | reference decoder | `sudo apt install multimon-ng` |
| sox (with `play`) | plays the WAV and converts audio | `sudo apt install sox libsox-fmt-all` |
| alsa-utils | `aplay -l` to find the sound card | `sudo apt install alsa-utils` |

Install everything at once:

```bash
sudo apt install python3-serial multimon-ng sox libsox-fmt-all alsa-utils
```

(If you prefer pip: `pip install pyserial`, inside a virtual environment on
recent distributions.)

---

## 3. Build the audio cable (hardware)

![Minimal audio input circuit](esp32_audio_input.png)

*The image is `esp32_audio_input.png` in this directory.*

### What the circuit does

* The PC sound card output is a signal that swings **both above and below 0 V**
  (it can reach roughly ±1 V or more). The ESP32 ADC only accepts **0 to 3.3 V**
  and must never see a negative voltage.
* **RV1** (the trimmer) is a **volume control / voltage divider** across the PC
  output. Its wiper takes only a fraction of the signal.
* **C1** (the capacitor) **blocks DC** and passes the audio. On the ESP32 side it
  connects to GPIO33, whose DC level is set by the ESP32 itself.
* The ESP32 can bias its own ADC pin: it connects the pad's internal **pull-up
  and pull-down resistors** so that the pin rests at about **1.65 V** (half of
  3.3 V). The audio then rides on that DC level. This is the firmware option
  **ADC input self-bias** (section 4). That is why the whole circuit needs no
  resistors of its own.
* **Ground** of the jack (sleeve) goes to ESP32 **GND**.

### Wiring, step by step

Power everything off first.

1. **Jack tip** → top end of **RV1**.
2. **Jack sleeve** → bottom end of **RV1** **and** ESP32 **GND**.
3. **RV1 wiper** (the middle terminal) → **C1, minus (−) side**.
4. **C1, plus (+) side** → ESP32 **GPIO33**.

### Capacitor polarity (important if C1 is an electrolytic)

An electrolytic capacitor has a **polarity**. GPIO33 rests at about 1.65 V and the
PC side rests at about 0 V, so the ESP32 side is the positive one:

| Lead of C1 | Goes to | How to recognize it |
|---|---|---|
| **+** (plus) | **GPIO33** (ESP32) | the **longer** lead of a new part |
| **−** (minus) | **RV1 wiper** (PC side) | the lead under the **stripe with minus signs** printed on the can |

In the drawing the straight plate (marked +) faces GPIO33 and the curved plate
(marked −) faces the trimmer wiper. Connecting it backwards makes it leak and
add noise. A ceramic or film 1 µF has no polarity, if you have one.

### Component rules

* **C1 must be 1 µF or larger.** With a small capacitor (for example 100 nF) the
  circuit becomes a high-pass filter that starts cutting around 700 Hz and
  weakens the 1200 Hz tone, which hurts decoding. Larger is fine (1 µF – 10 µF).
* **The trimmer must be *before* the capacitor**, as drawn. If RV1 were placed
  between the capacitor and the pin, its lower leg would tie GPIO33 to ground and
  destroy the bias.
* **RV1 = 2 kΩ** is a low value. It presents a 2 kΩ load to the PC output, which
  most headphone-type outputs handle easily. If the sound from the PC output
  becomes distorted, use a 10 kΩ trimmer instead (the firmware documentation
  recommends 10 kΩ mainly to keep the load on the source light).
* **The ESP32 pin has no protection diodes here.** RV1 is the only thing limiting
  what reaches GPIO33. Before connecting, set the PC volume low and RV1 near the
  ground end (section 7). A headphone output at full volume can swing well beyond
  the 0 – 3.3 V the pin tolerates.
* The firmware's reference schematic (the full-size RX interface in the project
  documentation) adds two **clamp diodes, D1/D2**, that hold the ADC pin inside
  the supply rails, plus a small R/C snubber. This minimal circuit leaves them out
  on purpose: the firmware then relies on the **Warn on receive over-range**
  option instead. If you will leave the bench connected for a long time, or use a
  strong source, consider adding the clamp diodes as drawn in that schematic.
* Only **GPIO32 and GPIO33** can use the built-in self-bias. The default input of
  the firmware is GPIO33; if you changed the ADC pin in your build, use that pin
  (it must be GPIO32 or GPIO33 for this circuit).

### 3.1 Alternative: fixed resistors instead of the trimmer

If you don't have a 2 kΩ trimmer, RV1 can be replaced with **two fixed
resistors** wired as a permanent voltage divider. You lose the ability to turn a
knob, but section 7.1 shows how the program's own **auto-volume calibration**
makes up for that in software.

![Fixed-resistor alternative to RV1](esp32_audio_input_fixed.png)

*The image is `esp32_audio_input_fixed.png` in this directory: the same circuit
as in section 3, with the trimmer replaced by the fixed pair R1/R2.*

More plainly: **R1** goes from the jack **tip** to a middle node; **R2** goes
from that same middle node to the jack **sleeve** (ground, tied to ESP32 GND).
The middle node — where R1 and R2 meet — replaces the trimmer's wiper and goes
to **C1's minus (−) side**, exactly as in the wiring steps above. Everything
else (C1, the connection to GPIO33, the shared ground) stays the same.

* **Suggested starting values: R1 = 4.7 kΩ, R2 = 1 kΩ.** This divides the PC
  output by about 5.7×, presenting a light ≈5.7 kΩ load. It is only a starting
  point: line-out levels vary a lot between sound cards, so the actual voltage
  reaching GPIO33 still depends on the PC's own volume setting.
* **This divider is fixed — it cannot be nudged like a trimmer.** Use the PC's
  volume control for the coarse setting (as in section 7), and let the
  program's `--volume` software gain (applied automatically by the auto-volume
  calibration described in section 7.1, unless you pass `--no_auto_volume`)
  take care of fine-tuning. That is precisely the situation the calibration
  feature is meant for.
* If, even at low PC volume, the level is always too high (over-range) or
  always too low (raw values glued to 0 or 4095, `--volume` alone can't fix
  it), swap in a divider with more or less attenuation — for example R1 = 10 kΩ
  / R2 = 1 kΩ (more attenuation) or R1 = 2.2 kΩ / R2 = 1 kΩ (less) — and
  re-check with RX LEVEL (section 7) or another auto-volume run.
* A trimmer is still the more convenient choice if you expect to reuse the
  bench with different sound cards or recordings: it lets you fix the analog
  level once, in hardware, rather than depending on software gain every time.

---

## 4. Prepare the ESP32 (firmware settings)

Join the ESP32's Wi-Fi access point (factory default SSID `esp32idf_APRS`,
password `esp32idf_APRS`) and open `http://192.168.4.1/` in a browser (factory
login `admin` / `admin`, unless you changed it). The interface may be shown in
several languages; English labels are given below.

### 4.1 Audio settings — *Radiomodem* page

| Label | Set to |
|---|---|
| Enable audio ADC/DAC modem | **ON** |
| Audio interface → **ADC input self-bias** | **ON** (this is what makes the capacitor-coupled circuit work) |
| Audio interface → **Warn on receive over-range** | **ON** (warns when the signal is too strong; the circuit has no clamp diodes) |

Save (reboot the ESP32 if the page asks for it).

### 4.2 ⚠ Safety settings — do this BEFORE playing any recorded traffic

Recorded off-air traffic contains **real callsigns and real positions**. The
WA8LMF test tracks even contain his own callsign in the beacons, and he
explicitly warns that if they are igated they create false reports of where he
was decades ago. Your ESP32 is an **IGate/digipeater** firmware, so make sure
that nothing decoded from a recording can leave the ESP32:

| Page | Label | Set to |
|---|---|---|
| IGate | **Enable IGate** | **OFF** |
| IGate | **RF to Internet** | **OFF** |
| Digipeater | **Enable Digipeater** | **OFF** |
| Beacon pages | beacon / tracker / weather / telemetry enables | **OFF** |

Extra safety, recommended:

* Leave the ESP32 in its factory **Wi-Fi AP-only** mode (no Station/client
  connection) so it has **no Internet path at all** while testing.
* **Do not connect a transmitter or radio** to the ESP32 during the test.

The receive lines you need on the console (`RX: …`) are still printed with the
IGate disabled: the documentation states that a receive-only station's log is
narrowed by the filters, not emptied. If you ever see no `RX:` lines with the
IGate off, see [Troubleshooting](#15-troubleshooting).

### 4.3 Log settings

| Label | Set to | Why |
|---|---|---|
| IGate page → **Log after filters** | **OFF** (the default) | If ON, the console only prints packets that pass your IGate filters, and every filtered packet would be counted as *NOT DECODED*. |

The console log level must be **INFO** (the firmware default), which is the level
that prints the `RX:` lines.

### 4.4 The console

The ESP32 USB port is also the serial console: **115200 baud, 8 data bits, no
parity, 1 stop bit (8N1)**. Only one program can use the serial port at a time,
so **close** `idf.py monitor`, minicom, screen, PuTTY, etc. before running the test.

---

## 5. Prepare the PC

### 5.1 Serial port permission

On Debian/Ubuntu your user must be in the `dialout` group:

```bash
sudo usermod -aG dialout $USER
# then log out and log in again
```

Find the port of the ESP32:

```bash
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
```

The program defaults to `/dev/ttyUSB0`; use `--serial_port` for another one.

### 5.2 Find the sound card that feeds the ESP32

```bash
./test_aprs_wavs.py --list_audio      # same as: aplay -l
```

Example output:

```
card 0: PCH [HDA Intel PCH], device 0: ALC3246 Analog [ALC3246 Analog]
card 1: Device [USB Audio Device], device 0: USB Audio [USB Audio]
```

The USB sound card above is **card 1, device 0**, which is the ALSA name
`hw:1,0`. You will pass it as `--audio_device hw:1,0`. Without `--audio_device`
the system default output is used.

### 5.3 Sound-card advice (it matters for the results)

* **Use a dedicated USB sound card** if possible. A cheap one is fine. It keeps
  desktop sounds out of the test.
* **Never let system sounds play** during a test: a notification beep goes
  through the same output and into the ESP32 input, and can corrupt a packet.
  Disable notification sounds, or use a dedicated card that the desktop does not use.
* Turn off any **audio "enhancements"**, equalizers, loudness or volume
  normalization in the sound settings.
* Set the mixer volume **once** and don't touch it during a run. The audio level
  at the ESP32 depends on it (section 7). `alsamixer` (choose the sound card
  with F6) is the usual tool.
* Play **lossless** audio only (WAV/FLAC). WA8LMF notes that lossy compression
  such as MP3 mangles the data waveforms, and that low-cost software-based PC
  sound systems can introduce timing errors or a wrong playback sample rate. If
  results look strange, a different sound card is the first thing to try.
* Do not use Bluetooth audio.

---

## 6. Get audio files with real APRS traffic

### 6.1 What files can be used

**Any WAV file that contains APRS traffic at 1200 baud AFSK** (the normal VHF APRS
modulation, Bell 202 tones 1200/2200 Hz). The program accepts any:

* sample rate (8000, 11025, 22050, 44100, 48000 Hz …),
* mono or stereo,
* bit depth (8, 16, 24 bit).

`sox` converts the audio for multimon-ng (22050 Hz, mono, 16 bit) automatically.

Rules:

* If the WAV is **stereo**, only its **left channel** is sent to the ESP32 (the
  reference decoder receives the mix of both). Convert stereo recordings whose two
  channels differ to mono first: `sox in.wav -c 1 mono.wav`.
* The extension must be `.wav` (`.WAV` and `.Wav` are accepted too). The program
  looks only at those files **directly inside** the directory you give it (it does
  not search subdirectories). FLAC, MP3 etc. must be converted first (section 6.3).
* The audio must be **lossless** and have **no clipping**.
* The file can be of any length. Files are processed **one after another, in
  alphabetical order** (upper- and lower-case names sort together), and each is played in **real time** (a 25-minute file takes
  25 minutes).
* Best audio: taken straight from a receiver's **discriminator / "data" output**.
  Audio taken from the **speaker** is de-emphasized; it also works, but decoding
  is somewhat different. Testing both is useful.

### 6.2 Recommended source: the WA8LMF TNC test tracks

**WA8LMF** publishes, on
**<http://www.wa8lmf.net/TNCtest/>**, a free "TNC Test CD" built to compare 1200
baud packet TNCs "under fire" in real conditions. It contains real off-air
recordings, which makes it an ideal test set for this bench.

**Two versions are offered; both have identical audio:**

| Version | File | Size | Format |
|---|---|---|---|
| 2.0 (recommended) | <http://www.wa8lmf.net/TNCtest/TNC_Test_2.iso> | ≈ 250 MB | CD-ROM image with lossless **FLAC** files |
| 1.1 | <http://www.wa8lmf.net/TNCtest/TNC_Test_CD_Ver-1.1.zip> | ≈ 530 MB | Audio-CD image (BIN/CUE) — needs to be burned or ripped |

Use **version 2.0**: the audio is already in files.

**The tracks** (as described on that page):

| Track | What it is | Length | Packets | Use it here? |
|---|---|---|---|---|
| **1** | Real traffic on **144.39 MHz, Los Angeles**, at the afternoon rush hour: the channel is saturated. 40 minutes of activity with pauses removed, compressed to ≈ 25 minutes. Discriminator audio, **not** de-emphasized. Has over- and under-deviated signals, collisions, back-to-back packets with almost no gap, raw NMEA trackers, TinyTraks, CW ID on packet … | ≈ 25 min | many (hundreds) | **Yes — the main stress test** |
| **2** | Same content as track 3 but **de-emphasized** (simulates audio taken from a radio's speaker / volume control). | ≈ 5 min | 100 | **Yes** — compare with track 3 |
| **3** | A **Kenwood D700 Mic-E** position report, clean, from a service monitor, copied 100 times: **20 bursts per minute for 5 minutes = exactly 100 identical packets**. | ≈ 5 min | exactly **100** | **Yes — gives an exact percentage** |
| **4** | 25 minutes of **one mobile D700 beaconing every 12 s** while driving, on a quiet channel, 8–10 miles from the receiver: flutter, multipath, weak signals. The page says several packets are audible but fail to decode with the AGW packet engine. | ≈ 25 min | up to ≈ 125 sent | **Yes — weak-signal test** |
| 5, 6, 7 | KPC3+ "CAL" alternating 1200/2200 Hz tones: flat, de-emphasized, pre-emphasized. For TNC *alignment* (tone level ratio). | ≈ 1 min each | **0** | **No** — they contain no packets. Keep them out of the test directory. |

Also from the page: each track begins and ends with DTMF cue tones (track 1 starts with
"1" and ends with "6", track 2 with "2"/"7", and so on). They are harmless.

> **⚠ WA8LMF's warning:** *do not play these tracks over the air.* His callsign is
> embedded in the beacons and they would be igated, creating false position reports.
> **This bench never transmits**, but keep the ESP32 IGate disabled as described in
> section 4.2 for exactly the same reason.

These tracks are third-party material published by WA8LMF for testing; they are not
distributed with this bench. Credit the source if you publish results.

### 6.3 Download, extract and convert (Version 2.0)

```bash
mkdir -p ~/audio_test && cd ~/audio_test

# 1) download (≈ 250 MB)
wget http://www.wa8lmf.net/TNCtest/TNC_Test_2.iso

# 2) open the ISO — either mount it (needs sudo) ...
mkdir -p TNC_Test_2
sudo mount -o loop,ro TNC_Test_2.iso TNC_Test_2
#    ... or extract it without root:  sudo apt install p7zip-full
# 7z x TNC_Test_2.iso -oTNC_Test_2

# 3) see the FLAC files
find TNC_Test_2 -iname '*.flac'
```

The FLAC files in the ISO are named after the tracks and start with the track
number, so the `find` command above shows their real names (the first one is the
25-minute Los Angeles traffic track). Then convert them all to WAV:

```bash
mkdir -p Audio-Tracks
find TNC_Test_2 -iname '*.flac' | while IFS= read -r f; do
    sox "$f" "Audio-Tracks/$(basename "$f" .flac).wav"
done
ls -l Audio-Tracks
```

Alternatives to `sox`: `ffmpeg -i in.flac out.wav` or `flac -d in.flac`.

Unmount when finished: `sudo umount TNC_Test_2`.

Because files are processed alphabetically and start with the track number, they
run in track order. **Move tracks 5, 6 and 7 out** of `Audio-Tracks/` (for example
into `Audio-Tracks/skip/`, which the program ignores) — they have no packets.

### 6.4 Record your own traffic (optional)

Any recording of real APRS traffic works. Two common ways:

* **From a radio:** connect the radio's discriminator/data output (or speaker
  output through an attenuator) to the PC sound-card input and record with
  Audacity: **mono, 22050 or 44100 Hz, 16 bit**, input level set so peaks stay well
  below the maximum (no clipping), any automatic gain / noise reduction **off**.
  Export as WAV.
* **From an RTL-SDR** (not tested in this project — check the options of your
  `rtl_fm` version). Use the APRS frequency of **your region** (144.390 MHz in
  North America; other regions differ):

  ```bash
  rtl_fm -M fm -f 144.390M -s 22050 - | sox -t raw -r 22050 -e signed -b 16 -c 1 - capture.wav
  ```

Record long sessions (an hour or more) at busy times; the more packets, the more
meaningful the percentages.

---

## 7. Set the audio level

The ESP32 needs the right amount of signal. **Too little** and it cannot decode;
**too much** and the signal clips (and the input has no protection diodes).

**Before connecting the cable:** PC volume low (about 30 %) and RV1 turned fully to
the **ground end** (minimum signal).

You need a second device on the ESP32 web page while the audio plays: a phone or
the PC itself, joined to the ESP32 access point (section 4). The web page works at
the same time as the USB serial connection.

**Step by step**

1. Open the ESP32 **Radiomodem** page. Next to the **LOOP TEST** button you
   will find **RX LEVEL** (Spanish UI: **NIVEL RX**). It **measures without
   transmitting**.
2. Play a real recording **continuously**. Use a busy one, such as WA8LMF track 1
   (its packets are almost back to back, so the level is steady). Either use the
   test program itself (the verdicts do not matter now; stop it with Ctrl-C when
   you finish tuning):
   ```bash
   ./test_aprs_wavs.py --wav_dir one1 --audio_device hw:1,0
   ```
   or a plain player:
   ```bash
   AUDIODRIVER=alsa AUDIODEV=hw:1,0 play -q Audio-Tracks/01_*.wav remix 1 1
   ```
   No real recording yet? Loop the synthetic file (section 8) for a while
   (sox's `repeat 50` plays it 50 extra times, about 5 minutes in total):
   ```bash
   AUDIODRIVER=alsa AUDIODEV=hw:1,0 play -q Synthetic/sample.wav remix 1 1 repeat 50
   ```
3. While it plays, press **RX LEVEL**. The button first **saves the page as it is
   shown** (so the modem uses the settings on screen), then watches the receiver
   for **1 second** and shows the result in green next to the buttons, for example:
   ```
   312 mV RMS (peak 640), DC 1650 mV, AGC 1.00x, raw 1810..2390, DCD no
   ```
   It is a one-second snapshot, so press it **several times**; with bursty audio one
   reading can fall in a pause between packets. Use the highest steady values.
4. Turn RV1 up in small steps (a few turns of the multi-turn trimmer), pressing
   **RX LEVEL** after each, until all three targets are met:

   | Reading | Target |
   |---|---|
   | RX level (`mV RMS`) | **250 – 350 mV RMS** |
   | raw range (`raw min..max`) | comfortably **away from 0 and 4095** (the 12-bit ADC rails) |
   | DC offset (`DC … mV`) | **1200 – 2000 mV** (this confirms the self-bias works; about 1650 mV is ideal) |

5. **Leave the PC volume and RV1 alone** for the rest of the session. If you change
   the sound card, the volume, or play a recording with a very different level
   (for example the de-emphasized track 2), check again.

If the **over-range warning** appears, the level is too high: turn RV1 down (or
lower the PC volume). If the DC offset is near 0 mV or 3300 mV, self-bias is not
enabled (section 4.1) or C1 is wired the wrong way / missing.

The program also has `--volume`, a software gain for the ESP32 leg only
(default 1.0). Prefer RV1 for the main adjustment and use `--volume` for small
corrections (for example `--volume 0.8`); values above 1.0 can clip.

### 7.1 Automatic volume calibration

`--volume` is not just a fixed number you set once: unless you pass
`--no_auto_volume`, **every real test run starts with an automatic search for
the best software gain**, before the packets that end up in your report are
even played. This is on by default, so it happens whether or not you asked for
it — worth knowing, because it adds time before the run you actually wanted to
see:

* It plays through the WAV set (looping back to the first file if needed) in
  short batches of `--auto_volume_batch` packets (default 50, counting both
  multimon-ng packets and ESP32-only "extra" ones), starting at `--volume`
  (default 1.0).
* After each batch it checks the ESP32's own **over-range** warning (the same
  one described in section 4.1) and the percentage decoded — packets the ESP32
  answered, counting **OK** and **DIFFERENT** ones alike, out of multimon-ng's
  total for that try:
  * **Over-range at all** → the level was too high for that try; the volume is
    lowered by 15% for the next try, and that try is not eligible to be
    remembered as the best one.
  * **No over-range** → the decoded percentage is compared with the previous
    try's; the volume is raised by 15% for the next try, and this try becomes
    the new best if it beat every earlier one.
* It keeps spending its full budget of tries — up to `--auto_volume_max_rounds`
  (default 10) — even after reaching 100% or after the percentage stops
  moving between two tries, so a still-better volume later in the search is
  never missed just because an earlier one already looked good.
* Whichever volume scored the highest decoded percentage **among tries that did
  not clip** is the one used for the real test that follows, which then always
  restarts from the first file.
* The volume is kept inside **0.05 – 8.0**, and it changes only between tries:
  a try always plays at one fixed gain.
* If **every** try showed over-range, none of them is eligible, and the run falls
  back to the starting `--volume`. Turn RV1 (or the PC volume) down and run again.
* If a try decodes nothing at all with multimon-ng, the search stops there and
  says so: that is an audio-routing or file problem, not a level problem.
* Interrupting the calibration with **Ctrl-C** does not stop the program: it goes
  on to the real test with the volume of the try that was running when you
  interrupted — which is not necessarily the best-scoring one, so read the value
  printed in the final summary before quoting the result.

The chosen volume is printed as each try completes, and again at the end of
the final summary as `Playback volume used for this test`. Keep that number in
your log alongside the level you set on RV1: if you are comparing runs over
time (section 13) and want the audio chain to be identical between them, pass
the same value back in with `--volume X --no_auto_volume` instead of
recalibrating every time.

`--no_play` (the dry run) skips the calibration as well: it never touches the
sound card.

This is also what makes the fixed-resistor alternative of section 3.1
practical: with no trimmer to turn, the calibration's software gain is what
absorbs the difference between sound cards and PC volume settings.

---

## 8. First run: check the whole chain with synthetic packets

Before spending 25 minutes on a real recording, verify that everything works
(cable, level, ESP32 settings, serial port) with a tiny **clean** test file. The
included `gen_test_wav.py` builds a perfect AFSK 1200 file with 3 known packets:

```bash
mkdir -p Synthetic
python3 gen_test_wav.py Synthetic/sample.wav
```

### 8.1 Dry run — no hardware needed

This uses only multimon-ng and confirms that the software side works:

```bash
./test_aprs_wavs.py --wav_dir Synthetic --no_play
```

Expected: multimon-ng lists the 3 packets and the program finishes with
`DRY RUN finished: multimon-ng decoded 3 packet(s) in 1 file(s).` In this mode
the serial port and the sound card are **not** used, and the auto-volume
calibration is skipped. The packets are listed as `000001 [multimon …]` lines
with no verdict, since there is no ESP32 answer to compare them with. The exit
code is 0 if any packet was decoded, 2 if none was.

pyserial, multimon-ng, sox and `play` must still be installed: the program
imports pyserial and checks for the three programs before it looks at
`--no_play`.

### 8.2 Full run with the ESP32

With the cable connected and the level set (section 7):

```bash
./test_aprs_wavs.py --wav_dir Synthetic --serial_port /dev/ttyUSB0 --audio_device hw:1,0
```

A working chain shows the three packets each tagged **OK**:

```
[1/1] sample.wav  (5.5 s)
000001 [multimon 00:01.1] N0CALL-9>APRS-0,WIDE1-1,WIDE2-1:!4903.50N/07201.75W-Test one
    OK [esp32    00:01.3] N0CALL-9>APRS,WIDE1-1,WIDE2-1:!4903.50N/07201.75W-Test one
000002 [multimon 00:02.8] LU1ABC-0>APDW17-0,WIDE1-1:=3450.12S/05812.34W>Movil en ruta
    OK [esp32    00:03.0] LU1ABC>APDW17,WIDE1-1*:=3450.12S/05812.34W>Movil en ruta
000003 [multimon 00:04.4] EA4XYZ-7>APRS-0::LU1ABC   :Hola que tal{12
    OK [esp32    00:04.6] EA4XYZ-7>APRS::LU1ABC   :Hola que tal{12
  multimon-ng decoded 3 packet(s)
  ESP32 decoded 3 packet(s)
  -> OK: 3   DIFFERENT: 0   NOT DECODED: 0   EXTRA(esp only): 0
```

and a final summary with `Decoded correctly : 3 (100.00%)`. (The times may differ
by a few tenths of a second on your system.)

The two lines of a pair are not expected to read identically: multimon-ng writes
`-0` for a missing SSID and never writes the digipeated `*`, while the firmware
does the opposite. Section 12.1 lists exactly which differences are normalised
away before the comparison.

If they are **NOT DECODED**, the problem is in the chain, not in the decoder: go to
section 15 (troubleshooting). **Do not continue with real traffic until this
works.**

---

## 9. Run the real test

Preflight checklist:

- [ ] IGate, RF to Internet, Digipeater and beacons are **OFF** (section 4.2)
- [ ] Log after filters is **OFF**; audio modem, self-bias and over-range warning are **ON**
- [ ] No other program is using the serial port
- [ ] No system sounds can play on the sound card
- [ ] Level set with RX LEVEL (section 7)
- [ ] The synthetic file of section 8 gave **OK** on all packets
- [ ] Tracks 5–7 are not in the test directory

Run, keeping a log file at the same time:

```bash
./test_aprs_wavs.py --wav_dir Audio-Tracks --serial_port /dev/ttyUSB0 --audio_device hw:1,0 \
    2>&1 | tee results_$(date +%Y-%m-%d_%H%M).log
```

Without options, the program uses the WAV files of the **current directory** and
`/dev/ttyUSB0`:

```bash
cd Audio-Tracks
../test_aprs_wavs.py
```

### What happens during a run

1. The program lists the WAV files it found and opens the serial port
   (115200 8N1).
2. **Opening the port normally resets the ESP32** (the USB serial chip toggles
   DTR/RTS). The program waits `--settle` seconds (default 4) for it to boot.
3. Unless `--no_auto_volume` was given, it then runs the **auto-volume
   calibration** of section 7.1: several short passes through the WAV set to
   find the best software gain, printed as they happen. This adds time before
   the reported test starts; skip it with `--no_auto_volume` if you already
   know the volume you want.
4. For each file, at the same instant it:
   * **plays** the WAV to the sound card → ESP32 (real time), and
   * feeds the same audio to **multimon-ng**, also paced at real time so both
     decoders' packets appear side by side, and
   * **reads the ESP32 console** for `RX:` lines.
5. Each multimon-ng packet is printed with its verdict as soon as it is known
   (section 11).
6. Every 30 seconds a **progress line** is printed, so long files never look
   frozen.
7. After the audio ends the program waits a few seconds so the last packets get
   the same chance as the rest, prints the file's counts, pauses, and continues
   with the next file.
8. At the end it prints the **summary** for all files.

Total time ≈ the auto-volume calibration pass (skip it with `--no_auto_volume`
to avoid this) + the sum of the file lengths + about 6 s per file (+ 4 s at the
start). For tracks 1–4 of the WA8LMF set, without calibration, that is about
**one hour**.

**Ctrl-C** stops the test safely: everything that already has a verdict is kept
and the summary is printed. Packets still waiting for their verdict (the ESP32
had not had its full `--match_window` chance to answer) are silently dropped —
not printed, not counted either way.

---

## 10. Command-line reference

```
./test_aprs_wavs.py [options]
```

| Option | Default | Meaning |
|---|---|---|
| `--wav_dir DIR` | current directory | Directory with the `.wav` files (not recursive). |
| `--serial_port PORT` | `/dev/ttyUSB0` | Serial port of the ESP32 console. |
| `--baud N` | `115200` | Serial speed (8N1 is fixed). |
| `--audio_device DEV` | system default | ALSA device wired to the ESP32, e.g. `hw:1,0` (see `--list_audio`). |
| `--volume X` | `1.0` | Software gain applied only to the audio sent to the ESP32. Also the starting point for auto-volume calibration (see below), unless `--no_auto_volume` is given. During calibration it is kept inside 0.05 – 8.0. |
| `--no_auto_volume` | off | Skip the auto-volume calibration pass (section 7.1) and use `--volume` as-is for the whole run. |
| `--auto_volume_batch N` | `50` | Packets per try during auto-volume calibration (counts multimon-ng packets and ESP32-only "extra" ones together). |
| `--auto_volume_max_rounds N` | `10` | Number of tries used to search for the best volume before the real test run. |
| `--match_window S` | `5` | An ESP32 packet answers a multimon-ng packet only if it arrives within ±S seconds of it. A packet the ESP32 has not reported after S seconds is **NOT DECODED**. See sections 12 and 13. |
| `--tail S` | `3` | Seconds to keep listening after the audio ends. The program always waits at least `--match_window` seconds. |
| `--settle S` | `4` | Seconds to wait after opening the serial port (ESP32 reset/boot). Increase it if the ESP32 boots slowly. |
| `--pause S` | `1` | Pause between files. |
| `--no_play` | off | **Dry run:** no sound, no serial port; only multimon-ng runs. Auto-volume calibration is skipped as well. Packets are listed as `000001 [multimon …]` lines with no verdict. |
| `--mm_args "…"` | none | Extra arguments for multimon-ng, quoted, e.g. `--mm_args "-A"` (rarely needed). |
| `--list_audio` | — | Print the ALSA playback devices (`aplay -l`) and exit. |
| `-h`, `--help` | — | Show the built-in help. |

**Exit code** (useful in scripts):

| Code | Meaning |
|---|---|
| `0` | Every multimon-ng packet was decoded correctly by the ESP32. |
| `1` | At least one packet was DIFFERENT or NOT DECODED. (With real, crowded traffic this is the normal outcome; read the percentages instead.) |
| `2` | Setup problem (missing program, no WAV files, port cannot be opened) or multimon-ng decoded no packets at all. |

Examples:

```bash
# all WAVs in the current directory, default port
./test_aprs_wavs.py

# a directory, another serial port and a USB sound card
./test_aprs_wavs.py --wav_dir ./Audio-Tracks --serial_port /dev/ttyUSB1 --audio_device hw:1,0

# only one file: put it in a directory of its own
mkdir one && cp Audio-Tracks/03_*.wav one/
./test_aprs_wavs.py --wav_dir one --audio_device hw:1,0

# track 3 (identical packets 3 s apart): narrower window
./test_aprs_wavs.py --wav_dir one --audio_device hw:1,0 --match_window 1.5

# software check only, no hardware
./test_aprs_wavs.py --wav_dir Audio-Tracks --no_play

# reuse a volume you already trust, skip the calibration pass
./test_aprs_wavs.py --wav_dir Audio-Tracks --audio_device hw:1,0 --volume 0.85 --no_auto_volume

# let calibration search harder (more tries, bigger batches) on a large set
./test_aprs_wavs.py --wav_dir Audio-Tracks --audio_device hw:1,0 --auto_volume_max_rounds 15 --auto_volume_batch 80

# save the result, then list only the problems
./test_aprs_wavs.py --wav_dir Audio-Tracks --audio_device hw:1,0 2>&1 | tee run.log
grep -E "NOT DECODED|DIFFERENT" run.log
```

---

## 11. Reading the results

### 11.1 Packet lines

Every **multimon-ng** packet is printed, in the order it was heard, immediately
followed by the ESP32's own line for it (if any) and a verdict:

```
000012 [multimon 03:41.2] LU1ABC-0>APDW17-0,WIDE1-1:=3450.12S/05812.34W>Movil
    OK [esp32    03:41.4] LU1ABC>APDW17,WIDE1-1:=3450.12S/05812.34W>Movil
000013 [multimon 03:52.0] LU2XYZ-0>APRS-0:>some status
       [esp32     --:--.-] NOT DECODED
000014 [multimon 04:10.5] LU3AAA-0>APRS-0:>hello
       [esp32    04:11.0] LU3AAA>APRS:>hellX
      ! DECODED BUT DIFFERENT
000015 [multimon  --:--.-] NOT DECODED
       [esp32 only      04:20.1] LU9ZZZ>APRS:>heard only by the ESP32
```

* `000012` — a six-digit **print counter**. It advances by one for every packet
  printed, in the order the verdicts become known, and it numbers EXTRA packets
  too, so it is not multimon-ng's own packet count. It restarts at `000001` for
  each file.
* `03:41.2` — minutes:seconds into the file when multimon-ng decoded it.
* The text is the packet in **TNC2 format**: `SOURCE>DESTINATION,PATH:payload`.
  multimon-ng writes `-0` after callsigns that have no SSID (`LU1ABC-0`) and never
  writes the digipeated `*`; the firmware does the opposite. Those differences are
  normalised before comparing (section 12.1), so the two lines of an **OK** pair
  often look slightly different.
* **OK** is printed at the *start of the ESP32 line*; the other two verdicts are
  printed on a line of their own below the pair.
* A packet **only the ESP32** decoded is printed as a pair too, the other way
  round: a `[multimon  --:--.-] NOT DECODED` line first (multimon-ng is the one
  that missed it), then the ESP32's line as `[esp32 only ...]`. The word EXTRA
  does not appear on these live lines — those packets are counted as
  `EXTRA(esp only)` in the per-file and final counts.

| Verdict | Meaning |
|---|---|
| **OK** | The ESP32 decoded the same packet (same source, destination, path and payload), close in time. |
| **NOT DECODED** | The ESP32 did not report it. This is the "missing" case. |
| **! DECODED BUT DIFFERENT** | The ESP32 decoded a packet with the same source/destination/path but a **different payload**: decoded, but not correctly. |

Timing: an **OK** appears right away (usually within a second or two). **NOT
DECODED** and **! DECODED BUT DIFFERENT** appear about `--match_window` seconds
(5 s by default) after the packet, because the ESP32 might still be about to
report it.

The ESP32's own line and any EXTRA packets are always shown — there is no option
needed to see them.

### 11.2 Progress line

```
       [progress 04:00.0 / 25:49.3] multimon=13  ok=12  not-decoded=1  different=0  (serial lines seen: 240)
```

Time played / file length, and the running counts. **`serial lines seen`** should
keep growing: it proves the serial link is alive. If `multimon` grows but `ok`
stays at 0 and `not-decoded` grows, the ESP32 is not hearing the audio (section 15).

### 11.3 Per-file counts

```
  multimon-ng decoded 14 packet(s)
  ESP32 decoded 13 packet(s)
  -> OK: 12   DIFFERENT: 1   NOT DECODED: 1   EXTRA(esp only): 0
    ! DIFFERENT
        multimon: LU3AAA-0>APRS-0:>hello
        esp32   : LU3AAA>APRS:>hellX
    ! NOT DECODED by ESP32: LU2XYZ-0>APRS-0:>some status
```

After the counts, every DIFFERENT and NOT DECODED packet of that file is listed
again, so the problems of a long recording can be read in one place instead of
being hunted for among the live lines.

### 11.4 Final summary

```
========================================================================
SUMMARY
========================================================================
  file                                 mm     ok   diff  n/dec  extra
  01_40-Mins-Traffic-on-144.39.wav    412    371      2     39      6
  03_D700-Mic-E-100-bursts.wav        100     97      0      3      0
  ----------------------------------------------------------------------
  Playback volume used for this test : 1.150
  Files tested                      : 2
  Total packets (multimon-ng)       : 512
  Packets seen by ESP32             : 476
  Decoded correctly                 : 468  (91.41%)
  Decoded with different content    : 2  (0.39%)
  Missing (not decoded)             : 42  (8.20%)
  Extra (ESP32 only, not an error)  : 6
```

*(the numbers above are only an illustration of the layout)*

How each figure is defined:

| Figure | Definition |
|---|---|
| **Total packets** | packets decoded by multimon-ng (the reference) |
| **Decoded correctly** | OK count, and its percentage of the total |
| **Decoded with different content** | DIFFERENT count, and its percentage |
| **Missing (not decoded)** | NOT DECODED count, and its percentage |
| **Extra** | packets only the ESP32 decoded. **Not** part of the percentages. |

The three percentages add up to 100 %.

`Playback volume used for this test` is the software gain (section 7.1) that was
actually used to play every file in this run: either the value auto-volume
calibration settled on, or `--volume` unchanged if `--no_auto_volume` was given.
Note it down with the rest of the run's details if you plan to compare logs later.

### 11.5 How to interpret it

**Important:** multimon-ng is a *reference*, not the truth. Neither decoder is
perfect. On a crowded channel (WA8LMF track 1) some packets are decoded by one and
not the other. So:

* A high **OK** percentage is good. There is no official pass mark: what matters is
  **comparing runs** — the same files before and after a firmware change, or before
  and after touching the level. Keep the `.log` files and note the date, firmware
  version, RX level and sound card.
* **NOT DECODED** = multimon-ng found it and the ESP32 did not. On a saturated
  channel, clusters of misses around collisions are expected.
* **EXTRA** = the ESP32 found something multimon-ng missed. A decoder that is better
  than the reference shows extras; that is a good sign, not an error.
* **DIFFERENT** should be rare (AX.25 frames carry a CRC). Look at the ESP32's own
  line printed under the packet to see exactly what it decoded; the cause is often
  a truncated or altered payload text in the ESP32 log.

Typical patterns:

| What you see | Likely cause |
|---|---|
| `ok=0` from the start, everything NOT DECODED | The audio is not reaching the ESP32 (cable, sound card, level, modem disabled). |
| A whole file mostly NOT DECODED, but the synthetic file was OK | Level too low/high for that recording, or a de-emphasized recording (track 2) needing a different level. |
| Misses only in dense stretches | Normal on saturated traffic (collisions, back-to-back packets). |
| Sudden burst of NOT DECODED in the middle of a file | Something disturbed the audio (a system sound, a volume change) or the sound card glitched. |
| Many EXTRA | The ESP32 is more sensitive than multimon-ng on this material. |

---

## 12. How packets are compared

You can trust the numbers only if you know how the comparison is done.

### 12.1 What counts as "the same packet"

Both decoders describe the packet in different styles, so the program first
normalizes them. Five differences between multimon-ng and the firmware are
handled:

| Difference | multimon-ng | ESP32 firmware | Treatment |
|---|---|---|---|
| SSID 0 | prints `LU1ABC-0` | prints `LU1ABC` | a trailing `-0` is removed |
| Digipeated marker | never prints `*` | prints `WIDE1-1*` after a digi repeated it | the `*` is ignored |
| Non-printable bytes (Mic-E packets contain control and 8-bit bytes) | shows them as `.` | writes the raw bytes | the ESP32 payload is converted the same way before comparing |
| Trailing carriage return | dropped | written raw | a trailing CR/LF/NUL is ignored |
| Letter case of the addresses | as heard | as heard | source, destination and path are upper-cased before comparing, so case alone never makes a DIFFERENT |

What is **not** ignored: trailing **spaces and dots** are real payload, so a
truncated payload (`>hello.` versus `>hello`) is correctly reported as DIFFERENT.
Real SSIDs (`-9`, `-10`) are always compared.

To be OK, the **source, destination, every path element and the whole payload**
must match.

### 12.2 The time window

A recording can contain the same packet many times (a station beaconing every
30 s). To avoid crediting the ESP32 with the wrong transmission, a pair is made
only if the ESP32 line arrived within **±`--match_window` seconds (default 5)** of
the multimon-ng packet. Every ESP32 packet is used **once**: a packet sent three
times must be decoded three times to score three OKs. When two candidates exist,
the one **closest in time** wins.

### 12.3 What the time window means for identical packets (WA8LMF track 3)

Track 3 has 100 **identical** packets only **3 seconds** apart. This is the hardest
case for the matching, so here is exactly what to expect (verified by simulation):

* The **totals are correct with any sensible window** (for example 97 OK when the
  ESP32 missed 3 of 100).
* With the default window of 5 s, *which packet numbers* get flagged NOT DECODED
  can be off by one when the ESP32 line arrives slightly **before** multimon-ng's
  line. The count is right; the numbering of the flagged packets can shift.
* Rule: choose a window **larger than the real delay** between the two decoders
  and **smaller than half the spacing** between identical packets. For track 3:
  **1.5 s** (`--match_window 1.5`).
* If the window is *smaller* than the real delay, packets that the ESP32 decoded
  correctly are counted wrongly. So **measure the delay first**: run track 3 and
  compare the two time stamps printed for the first OK packets (the `[multimon
  ...]` line and the `[esp32 ...]` line printed just under it). If they differ by
  more than about 1 s, keep a wider window (and remember only the totals are exact).

For all the other tracks (no identical packets closer than 10 s) the default
window of 5 s is fine.

---

## 13. Suggested test plan with the WA8LMF tracks

Do the steps in order; each one builds confidence for the next.

| Step | File | Command (add `--audio_device hw:X,0`) | Purpose / what to look at |
|---|---|---|---|
| 0 | synthetic `sample.wav` | `--wav_dir Synthetic` | Chain check: all 3 packets must be **OK**. |
| 1 | Track 3 (100 Mic-E bursts) | `--wav_dir one3 --match_window 1.5` | **Exact percentage**: the ESP32 should decode close to 100 of 100. Also exercises Mic-E payloads with control characters. Measure the time offset between decoders here. |
| 2 | Track 2 (same 100, de-emphasized) | `--wav_dir one2 --match_window 1.5` | Same 100 packets through the speaker-style curve: compare with step 1. Adjust the level with RX LEVEL if needed (the signal is different). |
| 3 | Track 4 (mobile, weak) | `--wav_dir one4` | Weak-signal, flutter and multipath. Compare misses with what multimon-ng manages. |
| 4 | Track 1 (25 min saturated) | `--wav_dir one1` | The stress test: collisions, back-to-back packets. Expect a lower percentage than tracks 3/4, and a few EXTRA. |
| 5 | All four | `--wav_dir Audio-Tracks` | The full battery, one summary with the grand totals. |

Create the single-file directories once:

```bash
for n in 1 2 3 4; do mkdir -p one$n; cp Audio-Tracks/0${n}_*.wav one$n/; done
```

Repeat the whole plan after each firmware change and compare the logs. To judge
the natural variation, run the same file **three times** first: the ESP32's
automatic gain and the sound card clock make results differ slightly between runs.

Reference values from the source page for orientation: track 3 contains exactly
**100** packets, so "OK count ÷ 100" is directly the ESP32's success rate on
clean audio.

---

## 14. `gen_test_wav.py` in detail

A small generator of **perfect synthetic APRS audio**, used to verify the setup and
to make reproducible test files. It builds real AX.25 frames (CRC-16, bit stuffing,
NRZI) modulated as Bell 202 AFSK 1200 baud at 22050 Hz, mono, 16 bit, with
40 flag bytes of preamble (≈ 0.27 s), 8 flags of tail and 1 s of silence between
packets. The file also opens with 0.5 s of silence, and the same 1 s gap follows
the last packet, so nothing is cut off at either end. The tones are written at
about 60 % of full scale, which leaves headroom and keeps the file free of
clipping.

### Use as a program

```bash
python3 gen_test_wav.py output.wav
```

writes a file with three packets and prints `ok`. With no file name it writes
`sample1.wav` in the current directory. It does not create directories, so make
the target directory first (`mkdir -p Synthetic`). The packets are:

| Source | Destination | Path | Payload |
|---|---|---|---|
| N0CALL-9 | APRS | WIDE1-1, WIDE2-1 | `!4903.50N/07201.75W-Test one` |
| LU1ABC | APDW17 | WIDE1-1* | `=3450.12S/05812.34W>Movil en ruta` |
| EA4XYZ-7 | APRS | (none) | `:LU1ABC   :Hola que tal{12` |

(`N0CALL` is the standard placeholder callsign; the others are examples.)

### Use as a module (your own packets)

```python
from gen_test_wav import write_wav

packets = [
    # (source,     destination, [path],                    payload)
    ("N0CALL-9",  "APRS",   ["WIDE1-1", "WIDE2-1"], "!4903.50N/07201.75W-Test"),
    ("LU1ABC",    "APDW17", ["WIDE1-1*"],           "=3450.12S/05812.34W>Mobile"),
    # a Mic-E style payload with control / 8-bit characters:
    ("LU2XYZ-9",  "T2SP0W", ["WIDE1-1"],            "`c2Bl\x1c>/\x1d]mice test\xe9"),
    # a payload ending in a carriage return:
    ("LU4CR",     "APRS",   [],                     ">status\r"),
]
write_wav("my_test.wav", packets, rate=22050, gap_s=1.0)
```

* `rate` — sample rate of the WAV (22050 Hz recommended).
* `gap_s` — seconds of silence between packets.
* A `*` after a path element (`"WIDE1-1*"`) means "already repeated by that digi".

Uses: prove the chain works (section 8), test special payloads (Mic-E bytes, CR),
and produce a known number of packets for a quick regression run.

---

## 15. Troubleshooting

| Symptom | Cause and what to do |
|---|---|
| `Cannot open serial port … Permission denied` | Your user is not in the `dialout` group (section 5.1). |
| `Cannot open serial port … No such file or directory` | Wrong port. `ls /dev/ttyUSB* /dev/ttyACM*`, check `dmesg \| tail`, use `--serial_port`. |
| `Cannot open serial port … busy` | Another program (idf.py monitor, minicom, screen…) has the port. Close it. |
| `WARNING: no data received from the serial port yet` | Wrong port or speed; ESP32 still booting (raise `--settle 8`); USB cable is power-only. |
| `Missing required program(s): …` | Install the packages of section 2. |
| `No .wav files in …` | Wrong `--wav_dir`, or the files are FLAC/MP3 (convert them, section 6.3). |
| `[audio] player failed` | `play` cannot open the sound device: wrong `--audio_device`, the card is busy (PulseAudio/PipeWire may hold it), or `libsox-fmt-alsa` is missing. Try without `--audio_device`, or run `aplay -l`. |
| multimon-ng decodes packets but **every** one is NOT DECODED | The ESP32 is not hearing the audio. Check in this order: correct sound card (`--audio_device`) → mixer volume/mute (`alsamixer`) → cable and C1/RV1 wiring and polarity → **Enable audio ADC/DAC modem** and **ADC input self-bias** ON → press **RX LEVEL** while playing. |
| ESP32 console shows no `RX:` lines at all, even with good audio | The modem is disabled; **Log after filters** is ON; log level is not INFO; or (rare) the IGate being disabled hides them: enable **Enable IGate** but keep **RF to Internet** OFF and the Wi-Fi in AP-only mode, and test again. |
| Over-range warning on the console / RX level far above 350 mV | Level too high: turn RV1 down or lower the PC volume. Do not leave it like this: the pin has no protection diodes. |
| RX level very low (< 100 mV) | Turn RV1 up, or raise the PC volume a little. |
| DC offset near 0 mV or 3300 mV | Self-bias is off, or C1 is missing or wired backwards, or RV1 is between C1 and the pin. |
| Many NOT DECODED on one file but not on others | Level differs between recordings (especially de-emphasized ones); re-check with RX LEVEL for that file. |
| Results change between runs | Normal to a small degree (automatic gain, sound-card clock). Repeat 3 times and compare. If the change is large, look at USB sound-card stability and system sounds; also consider whether auto-volume calibration (section 7.1) picked a different volume each time — pin it down with `--volume X --no_auto_volume` for a fair comparison. |
| Run takes noticeably longer than the file lengths suggest | Normal: by default every run starts with the auto-volume calibration pass (section 7.1), which plays through the WAV set several times before the reported test begins. Use `--no_auto_volume` to skip it once you know a good `--volume`. |
| Auto-volume calibration reports "no packets decoded by multimon-ng at all" and stops | multimon-ng itself found nothing at any volume — this is a file/audio-routing problem, not a level problem (see the "multimon-ng decoded 0 packets" row above). |
| ESP32 reboots when the test starts | Opening the port resets the board through DTR/RTS. Normal; `--settle` waits for the boot. |
| The program seems stuck | Long files are played in real time; look at the progress line every 30 s. Ctrl-C stops safely. |
| multimon-ng decoded 0 packets in a file | The file has no packets (tracks 5–7 are tones only), is too quiet, or is not AFSK 1200. The program exits with code 2 if *no* file yields packets. |
| Tracks 5–7 in the directory | They contain tones, not packets: they waste time and give nothing. Move them out. |
| DIFFERENT packets | Compare the multimon-ng and ESP32 lines printed for that packet; the payload differs (see section 11.5). The same packets are listed again under the file's counts. |
| Line pairs reading `[multimon  --:--.-] NOT DECODED` followed by `[esp32 only …]` | Not a failure: that is how an EXTRA packet — decoded by the ESP32, missed by multimon-ng — is printed live. It is counted under `EXTRA(esp only)`. |
| The six-digit numbers do not match multimon-ng's packet count | They are a print counter that also numbers EXTRA packets and restarts per file (section 11.1). |
| Track 3: the flagged packet numbers look off by one | Window/timing effect described in section 12.3. Use `--match_window 1.5`; the totals are right anyway. |

---

## 16. Limitations

* **The reference is not the truth.** multimon-ng misses some packets and the ESP32
  may decode them (reported as EXTRA); and vice versa. Percentages measure
  agreement with multimon-ng, not absolute performance.
* **Agreement is by text.** A packet is OK if source, destination, path and payload
  text are equal; the program does not look at the raw bits.
* **Only AFSK 1200 baud** (multimon-ng's `AFSK1200`) is used. Other modes are not tested.
* **The PC sound path is part of the test.** Its filtering, clock accuracy and noise
  add to the result (see the sound-card advice in section 5.3).
* **Receive only.** The transmit chain of the ESP32 is not tested.
* **Auto-volume calibration (section 7.1) is on by default** and can pick a
  slightly different volume from one run to the next when two levels decode
  almost equally well. For strict before/after comparisons, fix the volume with
  `--volume X --no_auto_volume` instead of letting it recalibrate each time.
* **Linux with ALSA** is assumed by the audio options.
* The program was developed and checked against a **simulated ESP32 console** with
  the real multimon-ng, sox and pyserial. On real hardware, expect to adjust the
  level and possibly `--match_window`; section 15 covers the usual problems.

---

## 17. Cheat sheet

```bash
# ── one-time setup ────────────────────────────────────────────────
sudo apt install python3-serial multimon-ng sox libsox-fmt-all alsa-utils
sudo usermod -aG dialout $USER            # then log out / in
./test_aprs_wavs.py --list_audio          # find the sound card → hw:X,0

# ── ESP32 web UI ──────────────────────────────────────────────────
#   Radiomodem  : Enable audio ADC/DAC modem = ON, ADC input self-bias = ON,
#                 Warn on receive over-range = ON
#   IGate       : Enable IGate = OFF, RF to Internet = OFF, Log after filters = OFF
#   Digi        : Enable Digipeater = OFF          (beacons OFF, Wi-Fi AP only)

# ── set the level: play a busy recording (WA8LMF track 1), press RX LEVEL on
#    the Radiomodem page several times; aim for 250–350 mV RMS, raw far from
#    0/4095, DC offset 1200–2000 mV; then don't touch anything
./test_aprs_wavs.py --wav_dir one1 --audio_device hw:1,0     # Ctrl-C when tuned

# ── check the chain (3 clean packets, expect all OK) ─────────────
mkdir -p Synthetic && python3 gen_test_wav.py Synthetic/sample.wav
./test_aprs_wavs.py --wav_dir Synthetic --audio_device hw:1,0

# ── the real test (starts with an auto-volume calibration pass by default,
#    see 7.1; add --no_auto_volume --volume X to skip it and pin a known value) ─
./test_aprs_wavs.py --wav_dir Audio-Tracks --audio_device hw:1,0 2>&1 | tee run.log
grep -E "NOT DECODED|DIFFERENT" run.log
```

| Wiring | |
|---|---|
| Jack tip | → RV1 top (or R1, section 3.1) |
| Jack sleeve | → RV1 bottom **and** ESP32 GND (or R2's far end, section 3.1) |
| RV1 wiper | → C1 **−** (stripe side) (or the R1/R2 junction, section 3.1) |
| C1 **+** | → ESP32 GPIO33 |

---

## 18. Glossary

| Term | Meaning |
|---|---|
| **APRS** | Automatic Packet Reporting System: amateur-radio position/message/telemetry packets. |
| **AFSK 1200** | Audio Frequency Shift Keying at 1200 baud: two audio tones (1200 Hz and 2200 Hz) carry the bits (Bell 202). |
| **AX.25** | The link-layer packet format used by APRS on VHF. |
| **TNC2 format** | The text form of a packet: `SOURCE>DEST,PATH:payload`. |
| **Mic-E** | A compact APRS position encoding that puts data in the destination address and uses control/8-bit characters in the payload. |
| **Discriminator output** | The FM receiver's raw demodulated audio, before de-emphasis: the best source for data. |
| **De-emphasis** | The receiver's high-frequency roll-off applied to the speaker audio; data taken from the speaker is de-emphasized. |
| **IGate** | Gateway that forwards packets heard on radio to the internet APRS-IS network (and back). |
| **Digipeater** | A station that repeats packets on radio to extend range. |
| **APRS-IS** | The internet network that collects APRS packets. |
| **RMS** | Root-mean-square: the effective size of an AC signal; the ESP32 reports the RX level in mV RMS. |
| **DC offset** | The average DC voltage the ADC pin rests at; with self-bias it should be around 1650 mV. |
| **SSID** | The number after a callsign (`-9`) that distinguishes several stations of one operator. |
| **Auto-volume calibration** | The program's default pass, before the reported test, that searches for the software playback gain (`--volume`) giving the best decoded percentage without over-range. See section 7.1. |
