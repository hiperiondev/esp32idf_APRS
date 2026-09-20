#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_aprs_wavs.py - Regression test bench for esp32idf_APRS using real APRS WAVs.

For every .wav file in a directory the program:

  1. plays the WAV to the sound card that is wired to the ESP32 audio input
     (ADC, GPIO33 by default) - the ESP32 demodulates it on its own;
  2. AT THE SAME TIME feeds the very same audio to multimon-ng (AFSK1200),
     which acts as the reference decoder;
  3. AT THE SAME TIME reads the ESP32 console log over the serial port
     (8N1, 115200) and collects every "RX: <tnc2>" line it prints.

At the end of each file (and globally) it cross-checks the two sources:
every packet decoded by multimon-ng must have been decoded by the ESP32, with
the same content. A final summary reports total packets, percent decoded
correctly, percent decoded but with different content, and percent missing.
Packets the ESP32 decoded that multimon-ng did not are reported as "extra"
(not counted as errors: the ESP32 demodulator may simply be better).

Requirements
------------
  pip install pyserial
  multimon-ng   (reference decoder)
  sox           (audio playback / resampling; provides the `sox` and `play` tools)

Usage
-----
  ./test_aprs_wavs.py                          # wavs in cwd, /dev/ttyUSB0
  ./test_aprs_wavs.py --wav_dir ./captures --serial_port /dev/ttyUSB1
  ./test_aprs_wavs.py --audio_device "hw:1,0"  # ALSA device wired to the ESP32
  ./test_aprs_wavs.py --list_audio             # help finding the device

Firmware side: the console log must be at INFO level (the default), which is
what prints "I (t) aprs_service: RX: SRC>DST,PATH:payload" for each frame.
The IGate page option "Log after filters" must be OFF, otherwise frames that
the IGate filters reject are not printed and would be counted as missing.

IMPORTANT firmware requirement: the firmware MUST escape non-printable bytes
(and especially LF, 0x0A) before logging the "RX:" line. The console stream is
split on LF only - splitting on CR would truncate payloads that legitimately
contain CR - so a raw LF inside an information field cuts the console line in
two and the frame is scored as a content mismatch even though the demodulator
was right. Mic-E, telemetry and binary-ish payloads are the ones that hit this.

Recommended companion tool: `stdbuf` (coreutils). Without it multimon-ng's
stdout is block-buffered on the pipe, its packets arrive in bursts, and the
arrival timestamps this bench uses drift out of the match window - producing
NOT DECODED verdicts that are bench artefacts, not firmware faults.
"""

import argparse
import math
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover
    sys.stderr.write("pyserial is required:  pip install pyserial\n")
    sys.exit(2)

# --------------------------------------------------------------------------
# Constants
# --------------------------------------------------------------------------

MM_RATE = 22050          # multimon-ng native sample rate (AFSK1200 demod)
SERIAL_BAUD = 115200     # 8N1 as required by the firmware console
DEFAULT_SERIAL = "/dev/ttyUSB0"
PROGRESS_SECONDS = 30    # progress line interval while a file plays

# Time to keep listening after playback ends: the ESP32 may still be
# finishing a frame, and its RX task / console output is slightly delayed.
DEFAULT_TAIL_SECONDS = 3.0

# ESP-IDF log line:  "I (12345) aprs_service: RX: <tnc2>"
# Colors are disabled in this firmware's sdkconfig, but strip ANSI anyway so a
# rebuilt firmware with CONFIG_LOG_COLORS=y still works.
ANSI_RE = re.compile(rb"\x1b\[[0-9;]*[A-Za-z]")
ESP_RX_RE = re.compile(rb"[IWED] \(\d+\) aprs_service: RX: (.*)$", re.DOTALL)

# ESP-IDF log line emitted by the AFSK demodulator when the ADC input clips:
#   "W (12345) afsk: RX audio is over-range (raw 0..4095 of 0..4095) - lower
#    the receive level trimmer or the transceiver volume"
# Used by the auto-volume calibration to detect that the level is too HIGH.
ESP_OVERRANGE_RE = re.compile(rb"afsk: RX audio is over-range")

# --------------------------------------------------------------------------
# Auto-volume calibration
# --------------------------------------------------------------------------

AUTO_VOLUME_BATCH = 50        # packets scored per plateau probe
AUTO_VOLUME_MAX_ROUNDS = 10   # budget of probes for the whole search
AUTO_VOLUME_MIN = 0.02
AUTO_VOLUME_MAX = 4.0         # >1.0 only makes sense with --normalise (see build_play_cmd)

# Clipping is a binary, fast signal: the firmware itself reports it. Probing
# for it needs far fewer packets than scoring a decode rate does, which is what
# makes the two-phase search cheap.
CLIP_PROBE_PACKETS = 8
# A single transient over-range on one loud packet is not "the level is wrong".
# A level counts as clipping only above this many warnings per packet.
CLIP_RATE_THRESHOLD = 0.10
COARSE_STEP_DB = 6.0          # bracketing step while hunting for the clip threshold
BISECT_ITERS = 3              # 6 dB / 2^3 => +/-0.75 dB on the threshold
# Where to score the plateau, relative to the clipping threshold.
PLATEAU_OFFSETS_DB = (-3.0, -6.0, -9.0, -12.0, -18.0)
HEADROOM_DB = 6.0             # fallback margin below the threshold when scoring fails
MIN_CLIP_MARGIN_DB = 3.0      # never return a volume closer than this to clipping
KNEE_DROP_PCT = 15.0          # a score this far below the best means the lower knee is past
MAX_WAV_PASSES = 3            # passes over the wav set before giving up on a batch

# Number of confirmed matches before the ESP<->multimon latency offset is
# estimated, and the cap applied to it (a larger apparent offset is a symptom
# of something else, not of sound-card latency).
OFFSET_MIN_SAMPLES = 10
OFFSET_MAX_SECONDS = 3.0

# multimon-ng header:  "AFSK1200: fm SRC to DST [via P1,P2] UI  pid=F0"
MM_HDR_RE = re.compile(
    r"^AFSK1200: fm (?P<src>\S+) to (?P<dst>\S+)(?: via (?P<via>\S+))?\s+(?P<ctl>UI\S*)\s+pid=(?P<pid>[0-9A-Fa-f]{2})"
)

# --------------------------------------------------------------------------
# Live output
# --------------------------------------------------------------------------

_print_lock = threading.Lock()


def say(msg: str) -> None:
    """Thread-safe print that flushes immediately, so packets show up the
    moment they are decoded even when stdout is a pipe or a log file."""
    with _print_lock:
        print(msg)
        sys.stdout.flush()


def mmss(seconds: float) -> str:
    seconds = max(0.0, seconds)
    return "%02d:%02d.%d" % (int(seconds // 60), int(seconds % 60), int((seconds * 10) % 10))


# --------------------------------------------------------------------------
# Level maths
# --------------------------------------------------------------------------


def to_db(v: float) -> float:
    """Linear gain -> dB. Levels are searched multiplicatively, so every step
    of the search is a constant number of dB, not a constant linear amount."""
    return 20.0 * math.log10(max(v, 1e-9))


def to_lin(db: float) -> float:
    return 10.0 ** (db / 20.0)


def wilson(k: int, n: int, z: float = 1.96) -> Tuple[float, float]:
    """95% confidence interval for a proportion.

    Used to decide whether two volumes really decoded differently or whether
    the difference is just sampling noise: at n=50 packets the half-width near
    90% is about 8 percentage points, so anything smaller than that must not be
    treated as a signal.
    """
    if n <= 0:
        return (0.0, 1.0)
    p = k / float(n)
    d = 1.0 + z * z / n
    centre = (p + z * z / (2.0 * n)) / d
    half = (z * math.sqrt(p * (1.0 - p) / n + z * z / (4.0 * n * n))) / d
    return (max(0.0, centre - half), min(1.0, centre + half))


def median(values: List[float]) -> float:
    if not values:
        return 0.0
    s = sorted(values)
    n = len(s)
    return s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])


# --------------------------------------------------------------------------
# Data model
# --------------------------------------------------------------------------


@dataclass
class Packet:
    """A packet in a normalised, comparable form."""
    src: str
    dst: str
    path: Tuple[str, ...]
    info: bytes
    raw: str = ""          # original text, for printing

    def key_header(self) -> str:
        return "%s>%s,%s" % (self.src, self.dst, ",".join(self.path))


@dataclass
class FileResult:
    name: str
    mm_packets: List[Packet] = field(default_factory=list)
    esp_packets: List[Packet] = field(default_factory=list)
    ok: int = 0
    mismatch: List[Tuple[Packet, Optional[Packet]]] = field(default_factory=list)
    missing: List[Packet] = field(default_factory=list)
    extra: List[Packet] = field(default_factory=list)
    # Frames whose payload the ESP32 got right but whose header came out
    # corrupted. Kept apart from `mismatch` for reporting, but counted with it
    # in the totals: both mean "decoded, but not correctly".
    corrupt: List[Tuple[Packet, Packet]] = field(default_factory=list)
    duration: float = 0.0
    offset: float = 0.0        # measured ESP32 - multimon-ng latency skew, seconds


# --------------------------------------------------------------------------
# Normalisation helpers
# --------------------------------------------------------------------------


def norm_call(call: str) -> str:
    """Normalise a callsign: multimon-ng prints SSID -0, the firmware omits it,
    and the firmware appends '*' to digipeaters that already repeated the
    frame while multimon-ng never prints it."""
    call = call.strip().rstrip("*").upper()
    if call.endswith("-0"):
        call = call[:-2]
    return call


def info_to_mm_view(info: bytes) -> bytes:
    """Render an information field the way multimon-ng prints it.

    multimon-ng replaces every non-printable byte (< 0x20 or >= 0x7f) with '.'
    and drops a trailing CR. Applying the same transformation to the ESP32
    payload makes the two directly comparable, including Mic-E frames whose
    payload contains control bytes and 8-bit characters.
    """
    out = bytearray()
    for b in info:
        out.append(b if 0x20 <= b < 0x7F else 0x2E)
    return bytes(out)


def normalise_info(info: bytes) -> bytes:
    """Canonical comparison form of a payload.

    Only trailing CR, LF and NUL are removed - the two things that really
    differ between the tools (multimon-ng drops a trailing CR; the firmware
    writes it raw and the console adds its own line ending). This MUST happen
    on the raw bytes, before non-printables are mapped to '.', otherwise a
    trailing CR would become a '.' and could not be told apart from a genuine
    trailing dot.

    Trailing spaces and dots are deliberately KEPT: they are legitimate
    payload content, and stripping them would hide a real truncation as a
    pass (">hello." would compare equal to ">hello").
    """
    return info_to_mm_view(info.rstrip(b"\r\n\x00"))


def make_packet(src: str, dst: str, path: List[str], info: bytes, raw: str) -> Packet:
    return Packet(
        src=norm_call(src),
        dst=norm_call(dst),
        path=tuple(norm_call(p) for p in path if p),
        info=normalise_info(info),
        raw=raw,
    )


# --------------------------------------------------------------------------
# Parsers
# --------------------------------------------------------------------------


def parse_tnc2(line: bytes) -> Optional[Packet]:
    """Parse 'SRC>DST,P1,P2*:payload' (the firmware's TNC2 rendering)."""
    colon = line.find(b":")
    gt = line.find(b">")
    if gt < 1 or colon < gt:
        return None
    header = line[:colon].decode("latin-1")
    info = line[colon + 1:]
    src, rest = header.split(">", 1)
    parts = rest.split(",")
    dst, path = parts[0], parts[1:]
    if not src or not dst:
        return None
    return make_packet(src, dst, path, info, raw=line.decode("latin-1", "replace"))


def parse_esp_line(raw: bytes) -> Optional[Packet]:
    """Extract a packet from one ESP32 console line, or None."""
    raw = ANSI_RE.sub(b"", raw).rstrip(b"\r\n")
    m = ESP_RX_RE.search(raw)
    if not m:
        return None
    return parse_tnc2(m.group(1))


class MultimonParser:
    """Stateful parser for multimon-ng's two-line records (header + payload)."""

    # Lines other multimon-ng decoders/options emit, which must never be
    # mistaken for the payload line that follows an AFSK1200 header. With
    # `--mm_args -A` multimon-ng also prints a parsed "APRS: ..." line; it
    # normally comes after the payload, but this makes the parser independent
    # of that ordering.
    OTHER_DECODER_RE = re.compile(
        r"^(APRS|AFSK[0-9]+|AX25|POCSAG[0-9]*|FLEX|EAS|MORSE|DTMF|ZVEI[0-9]*|"
        r"UFSK[0-9]*|CLIPFSK|FMSFSK|SCOPE|DUMPCSV|X10|EOT)\b\s*:",
        re.IGNORECASE)

    def __init__(self) -> None:
        self._hdr = None  # type: Optional[re.Match]

    def feed_line(self, line: str) -> Optional[Packet]:
        line = line.rstrip("\\r\\n")
        m = MM_HDR_RE.match(line)
        if m:
            self._hdr = m
            return None
        if self._hdr is None:
            return None  # banner or noise
        if self.OTHER_DECODER_RE.match(line):
            # Another decoder spoke before the payload arrived: drop the
            # pending header rather than storing this line as its payload.
            self._hdr = None
            return None
        h = self._hdr
        self._hdr = None
        path = h.group("via").split(",") if h.group("via") else []
        info = line.encode("latin-1", "replace")
        raw = "%s>%s%s:%s" % (
            h.group("src"), h.group("dst"),
            ("," + ",".join(path)) if path else "", line)
        return make_packet(h.group("src"), h.group("dst"), path, info, raw)


# --------------------------------------------------------------------------
# Serial reader (runs in a thread for the whole test)
# --------------------------------------------------------------------------


class SerialCollector(threading.Thread):
    """Reads the ESP32 console continuously and stores RX packets with the
    monotonic time they were seen at.

    The port is read as *bytes* and split on LF only. The payload is written
    raw by the firmware, so it may hold CR, 8-bit bytes and control characters
    (Mic-E, telemetry); decoding as UTF-8 or splitting on CR would corrupt or
    truncate those frames.
    """

    def __init__(self, port: str, baud: int = SERIAL_BAUD) -> None:
        super().__init__(daemon=True)
        # Opening the port toggles DTR/RTS on most ESP32 dev boards, which
        # resets the chip (they are wired to EN/IO0). Both lines are therefore
        # de-asserted BEFORE the port is opened and again right after, because
        # some drivers assert them as part of open(). The firmware then gets
        # time to come up via the caller's wait_ready().
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = baud
        self.ser.bytesize = serial.EIGHTBITS
        self.ser.parity = serial.PARITY_NONE
        self.ser.stopbits = serial.STOPBITS_ONE
        self.ser.timeout = 0.2
        try:
            self.ser.dtr = False
            self.ser.rts = False
        except (OSError, ValueError, AttributeError):
            pass          # not all platforms allow setting the lines before open()
        self.ser.open()
        try:
            self.ser.dtr = False
            self.ser.rts = False
        except (OSError, ValueError, AttributeError):
            pass
        self.lock = threading.Lock()
        self.packets: List[Tuple[float, Packet]] = []
        self.lines_seen = 0
        self.alive = threading.Event()
        self._halt = threading.Event()
        self._buf = bytearray()
        self.last_rx_time = 0.0
        self.file_t0 = time.monotonic()   # set per file by run_one_wav()
        # Count of "RX audio is over-range" warnings seen, and the time of
        # the most recent one. Used by the auto-volume calibration to tell
        # a too-low volume (few/no packets decoded) apart from a too-high
        # one (ADC clipping, reported by the firmware itself).
        self.overrange_count = 0
        self.last_overrange_time = 0.0

    def run(self) -> None:
        while not self._halt.is_set():
            try:
                chunk = self.ser.read(4096)
            except (serial.SerialException, OSError) as exc:
                sys.stderr.write("\n[serial] read error: %s\n" % exc)
                break
            if not chunk:
                continue
            self.alive.set()
            self._buf.extend(chunk)
            while True:
                nl = self._buf.find(b"\n")
                if nl < 0:
                    break
                line = bytes(self._buf[:nl])
                del self._buf[:nl + 1]
                self._handle_line(line)
            if len(self._buf) > 8192:  # runaway garbage guard
                del self._buf[:-1024]

    def _handle_line(self, line: bytes) -> None:
        self.lines_seen += 1
        clean = ANSI_RE.sub(b"", line)
        if ESP_OVERRANGE_RE.search(clean):
            now = time.monotonic()
            with self.lock:
                self.overrange_count += 1
                self.last_overrange_time = now
        pkt = parse_esp_line(line)
        if pkt is not None:
            now = time.monotonic()
            with self.lock:
                self.packets.append((now, pkt))
                self.last_rx_time = now

    def snapshot_overrange(self) -> Tuple[int, float]:
        """(count, time of last occurrence) of over-range warnings so far."""
        with self.lock:
            return self.overrange_count, self.last_overrange_time

    def overrange_since(self, count_before: int) -> int:
        """How many over-range warnings arrived since a prior snapshot."""
        with self.lock:
            return self.overrange_count - count_before

    def snapshot_index(self) -> int:
        with self.lock:
            return len(self.packets)

    def since(self, index: int) -> List[Packet]:
        with self.lock:
            return [p for _, p in self.packets[index:]]

    def items_between(self, t_start: float, t_end: float) -> List[Tuple[float, Packet]]:
        """(arrival time, packet) pairs received in [t_start, t_end)."""
        with self.lock:
            return [(t, p) for t, p in self.packets if t_start <= t < t_end]

    def items_from(self, index: int, t_start: float) -> Tuple[List[Tuple[float, Packet]], int]:
        """Packets stored at or after `index` whose arrival is >= t_start, plus
        the new index to resume from.

        The ticker asks for this every 0.25 s, so scanning the whole run each
        time made a long session quadratic in the number of packets. Packets
        are appended in arrival order, so resuming from an index is both
        cheaper and equivalent."""
        with self.lock:
            tail = self.packets[index:]
            return [(t, p) for t, p in tail if t >= t_start], len(self.packets)

    def between(self, t_start: float, t_end: float) -> List[Packet]:
        """Packets whose console line was received in [t_start, t_end)."""
        with self.lock:
            return [p for t, p in self.packets if t_start <= t < t_end]

    def stop(self) -> None:
        self._halt.set()
        try:
            self.ser.close()
        except Exception:
            pass
        if self.is_alive():
            self.join(timeout=2)


# --------------------------------------------------------------------------
# Audio: play to the sound card AND feed multimon-ng at the same time
# --------------------------------------------------------------------------


def wav_duration(path: str) -> float:
    import wave
    try:
        with wave.open(path, "rb") as w:
            return w.getnframes() / float(w.getframerate())
    except Exception:
        pass
    # Not a PCM wave the stdlib understands (24 bit, ADPCM, ...). Ask sox,
    # which understands everything libsox was built for, so the progress line
    # does not end up showing "/ 00:00.0".
    try:
        p = subprocess.run(["soxi", "-D", path], capture_output=True, timeout=20)
        return float(p.stdout.decode("latin-1", "replace").strip())
    except Exception:
        return 0.0


_PEAK_CACHE = {}  # type: dict


def wav_peak(path: str) -> float:
    """Peak amplitude (0..1) of a file, from `sox <wav> -n stat`.

    Needed because digital gain above 1.0 on a file that already peaks near
    full scale clips INSIDE sox, before the sound card ever sees it - and
    `-V0` hides sox's own clip warning. Knowing the peak is what lets the
    bench tell "the analog level is too hot" apart from "I am asking sox to
    produce samples it cannot represent".
    """
    if path in _PEAK_CACHE:
        return _PEAK_CACHE[path]
    peak = 1.0
    try:
        p = subprocess.run(["sox", path, "-n", "stat"], capture_output=True, timeout=60)
        for line in p.stderr.decode("latin-1", "replace").splitlines():
            if "Maximum amplitude" in line:
                peak = abs(float(line.split(":", 1)[1].strip()))
                break
    except Exception:
        pass
    peak = max(peak, 1e-6)
    _PEAK_CACHE[path] = peak
    return peak


def build_play_cmd(wav: str, volume: float, normalise: bool) -> List[str]:
    """Build the `play` command for the ESP32 leg.

    Differences from the earlier version:
      * no "-c 2": `remix 1 1` already produces two output channels, and a
        format option placed after the input file is parsed differently by
        different sox builds;
      * the level is applied with `gain <dB>` instead of the linear `vol`,
        so the amount of gain is explicit in the unit the problem is actually
        posed in, and sox's headroom handling applies;
      * with `normalise`, every file is brought to -1 dBFS first, so one
        volume is valid across a set of recordings made at different levels.
    """
    cmd = ["play", "-q", "-V0", wav, "remix", "1", "1"]
    if normalise:
        cmd += ["gain", "-n", "-1"]
    cmd += ["gain", "%.2f" % to_db(volume)]
    return cmd


def clip_warning(wav: str, volume: float, normalise: bool) -> Optional[str]:
    """Return a warning when `volume` would clip this file inside sox."""
    if normalise:
        headroom = to_lin(-1.0)            # every file leaves the chain at -1 dBFS
    else:
        headroom = wav_peak(wav)
    if volume * headroom > 1.0:
        return ("%s: gain %.3f (%+.1f dB) on a file peaking at %.3f would clip "
                "inside sox (max usable gain %.3f). Use --normalise, or lower the "
                "gain and raise the hardware level instead." %
                (os.path.basename(wav), volume, to_db(volume), headroom, 1.0 / headroom))
    return None


def run_one_wav(res: FileResult, wav: str, audio_device: Optional[str],
                volume: float, tail: float, window: float,
                collector: SerialCollector, mm_extra: List[str],
                mute_local: bool,
                stop_at_mm_packets: Optional[int] = None,
                normalise: bool = False,
                offset_auto: bool = True,
                offset_seed: float = 0.0) -> bool:
    """Play `wav` once while decoding it with multimon-ng and reading the
    ESP32 console. Every multimon-ng packet is printed together with the
    ESP32's answer to it (or NOT DECODED) as soon as that is known, and `res`
    is filled in as the verdicts come.

    If `stop_at_mm_packets` is given, playback is cut short - mid-file, not
    just at the end of it - the instant `len(res.mm_packets) + len(res.extra)`
    reaches that count: the player/sox/multimon-ng processes are killed
    right then instead of being left to run to the end of a (possibly very
    long) WAV. A packet the ESP32 decoded that multimon-ng never saw at all
    (an "extra") counts towards this target too, not just ones multimon-ng
    also decoded - but "extra" packets are only known once the match window
    has elapsed with no multimon-ng match, so they may be confirmed slightly
    after the count would otherwise have been reached from multimon-ng
    packets alone. This is what lets auto-volume calibration actually stop
    at exactly N packets even when a single WAV holds far more than N.
    Returns True if that cutoff was hit, False if the file simply played to
    its natural end (or was interrupted by Ctrl-C)."""
    # ESP32 lines are attributed to this file by the moment they arrive. The
    # window opens right now, before playback starts, so nothing the previous
    # file already resolved is re-counted - but a frame the previous file was
    # still demodulating when it ended CAN arrive after this t0 and will be
    # ingested here, normally as an "extra". That is why the inter-file
    # `--pause` and `--tail` matter: they drain the ESP32 before the next file
    # opens its window.
    t0 = time.monotonic()
    t_window_start = t0

    # Both legs are produced from the same source file by sox so that the two
    # decoders receive identical audio, whatever the WAV's own format is
    # (stereo, 8/16/24 bit, 44.1/48 kHz ...).
    #
    #   leg A: play <wav>  -> sound card (ESP32 ADC)      [real time]
    #   leg B: sox <wav> -> raw 22050 Hz s16 mono -> multimon-ng
    #                                                     [paced to real time]
    #
    # `play` is sox's playback front-end. Without --audio_device it uses the
    # system default output. With one, sox is told to use ALSA on that device
    # through the AUDIODRIVER / AUDIODEV environment variables (the documented
    # way to select an output for `play`).
    #
    # The audio is sent to both channels of the sound card ("remix 1 1"), so
    # a left-only or right-only cable to the ESP32 hears the same signal.
    env = dict(os.environ)
    if audio_device:
        env["AUDIODRIVER"] = "alsa"
        env["AUDIODEV"] = audio_device
    play_cmd = build_play_cmd(wav, volume, normalise)
    warn = clip_warning(wav, volume, normalise)
    if warn and not mute_local:
        say("  ! " + warn)

    sox_raw = ["sox", "-q", "-V0", wav, "-t", "raw", "-r", str(MM_RATE),
               "-e", "signed", "-b", "16", "-c", "1", "-"]
    # Line-buffer multimon-ng's stdout. On a pipe libc block-buffers it (4 KiB),
    # so dozens of packets can sit in the buffer and then arrive in one burst;
    # they would be timestamped at read time, drift outside the match window and
    # be reported as NOT DECODED even though the ESP32 and multimon-ng both got
    # them right.
    mm_cmd = ["multimon-ng", "-t", "raw", "-a", "AFSK1200", "-q"] + mm_extra + ["-"]
    if _HAVE_STDBUF:
        mm_cmd = ["stdbuf", "-oL"] + mm_cmd

    player = None  # type: Optional[subprocess.Popen]
    sox_p = None   # type: Optional[subprocess.Popen]
    mm_p = None    # type: Optional[subprocess.Popen]
    gt = None      # type: Optional[threading.Thread]
    done = threading.Event()
    duration = wav_duration(wav)
    matcher = LiveMatcher(window, offset_auto=offset_auto, offset=offset_seed)
    interrupted = False
    # Resume point in the collector's packet list (not a count of this file's
    # packets): everything already stored belongs to an earlier file.
    ingested = [collector.snapshot_index()]
    collector.file_t0 = t0

    # `res` is written from two threads: read_mm appends multimon packets as
    # they are decoded, while emit records verdicts from the ticker. The lock
    # keeps the tallies and the list appends consistent with each other.
    res_lock = threading.Lock()
    resolved = {}        # type: dict   # idx -> event, waiting for its turn
    next_idx = [1]       # next multimon-ng packet number to print
    print_idx = [0]       # single sequence number shared by every printed line
    target_hit = [False]  # set True if stop_at_mm_packets cut playback short

    def kill_pipeline() -> None:
        """Stop the player/sox/multimon-ng processes right now, instead of
        letting them run to the natural end of the file. Used the instant
        stop_at_mm_packets is reached, so a long WAV does not keep playing
        past the packet count the caller asked for."""
        for p in (player, sox_p, mm_p):
            if p is not None and p.poll() is None:
                try:
                    p.kill()
                except Exception:
                    pass

    def check_stop() -> None:
        """Cut playback short the instant len(res.mm_packets) + len(res.extra)
        reaches stop_at_mm_packets. A packet the ESP32 decoded that
        multimon-ng never saw at all (an "extra") counts towards the target
        just as much as one multimon-ng did see - it is still one more
        packet accounted for in this batch. Called both from read_mm (every
        new multimon-ng packet) and from emit (every resolved "extra" - those
        are only known once the match window has elapsed, so they trickle in
        from the ticker thread, not from read_mm)."""
        with res_lock:
            reached = len(res.mm_packets) + len(res.extra)
        if (stop_at_mm_packets is not None and not target_hit[0] and
                reached >= stop_at_mm_packets):
            target_hit[0] = True
            kill_pipeline()

    def show(ev: tuple) -> None:
        """Print verdicts in multimon-ng packet order. A packet whose verdict
        is already known waits (at most `window` seconds) behind an earlier
        one that is still waiting for its ESP32 answer, so the list reads
        top to bottom in the order the packets were heard."""
        if ev[0] == "extra":
            emit(ev)
            return
        resolved[ev[1]] = ev
        while next_idx[0] in resolved:
            emit(resolved.pop(next_idx[0]))
            next_idx[0] += 1

    def flush_resolved() -> None:
        """Print whatever is still held back, in order (end of file, Ctrl-C)."""
        for i in sorted(resolved):
            emit(resolved.pop(i))

    def emit(ev: tuple) -> None:
        """Print one verdict and record it in `res`."""
        kind, idx, t_mm, mp, t_esp, ep = ev
        print_idx[0] += 1
        n = print_idx[0]
        if kind == "extra":
            with res_lock:
                res.extra.append(ep)
            say("%06d [multimon  --:--.-] NOT DECODED" % n)
            say("       [esp32 only      %s] %s" % (mmss(t_esp - t0), ep.raw))
            check_stop()
            return
        say("%06d [multimon %s] %s" % (n, mmss(t_mm - t0), mp.raw))
        if kind == "ok":
            with res_lock:
                res.ok += 1
            say("    OK [esp32    %s] %s" % (mmss(t_esp - t0), ep.raw))
        elif kind == "mismatch":
            with res_lock:
                res.mismatch.append((mp, ep))
            say("       [esp32    %s] %s" % (mmss(t_esp - t0), ep.raw))
            say("      ! DECODED BUT DIFFERENT")
        elif kind == "corrupt":
            with res_lock:
                res.corrupt.append((mp, ep))
            say("       [esp32    %s] %s" % (mmss(t_esp - t0), ep.raw))
            say("      ! PAYLOAD OK BUT HEADER CORRUPT")
        else:
            with res_lock:
                res.missing.append(mp)
            say("       [esp32     --:--.-] NOT DECODED")

    def feed_and_step(now: float, final: bool = False) -> None:
        items, ingested[0] = collector.items_from(ingested[0], t_window_start)
        for t, p in items:
            matcher.add_esp(t, p)
        for ev in matcher.step(now, final):
            show(ev)

    try:
        mm_p = subprocess.Popen(mm_cmd, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        sox_p = subprocess.Popen(sox_raw, stdout=subprocess.PIPE,
                                 stderr=subprocess.DEVNULL)

        # Feed multimon-ng from sox in REAL TIME (unless dry-running). This
        # keeps its decodes in step with what the ESP32 is hearing, so the
        # two decoders' packets are printed side by side.
        realtime = not mute_local

        def pump() -> None:
            assert sox_p is not None and sox_p.stdout is not None
            assert mm_p is not None and mm_p.stdin is not None
            bytes_per_s = MM_RATE * 2
            sent = 0
            start_t = time.monotonic()
            try:
                while True:
                    chunk = sox_p.stdout.read(bytes_per_s // 10)   # 0.1 s of audio
                    if not chunk:
                        break
                    mm_p.stdin.write(chunk)
                    mm_p.stdin.flush()
                    sent += len(chunk)
                    if realtime:
                        ahead = sent / float(bytes_per_s) - (time.monotonic() - start_t)
                        if ahead > 0:
                            time.sleep(ahead)
            except (BrokenPipeError, OSError):
                pass
            finally:
                try:
                    mm_p.stdin.close()
                except Exception:
                    pass
                # Also close the read end of sox's pipe. Without this, a
                # multimon-ng that died (or was killed by the packet-count
                # cutoff) leaves sox blocked writing into a pipe nobody
                # drains, and sox_p.wait() below would never return.
                try:
                    sox_p.stdout.close()
                except Exception:
                    pass

        # Reader thread: takes every multimon-ng packet as it appears.
        parser = MultimonParser()

        def read_mm() -> None:
            assert mm_p is not None and mm_p.stdout is not None
            for raw in iter(mm_p.stdout.readline, b""):
                pkt = parser.feed_line(raw.decode("latin-1"))
                if pkt is None:
                    continue
                with res_lock:
                    res.mm_packets.append(pkt)
                now = time.monotonic()
                if mute_local:      # dry run: no ESP32, just list the packet
                    say("%06d [multimon %s] %s" %
                        (len(res.mm_packets), mmss(now - t0), pkt.raw))
                else:               # printed with the ESP32's answer
                    matcher.add_mm(now, pkt)
                check_stop()
                if target_hit[0]:
                    # Stop right here, mid-file, instead of playing the rest
                    # of a possibly much longer WAV.
                    break

        # Ticker: resolves pairs, prints the verdicts and, every
        # PROGRESS_SECONDS, a progress line, so a long recording never looks
        # frozen.
        def ticker() -> None:
            last_progress = time.monotonic()
            while not done.wait(0.25):
                now = time.monotonic()
                if not mute_local:
                    feed_and_step(now)
                if now - last_progress >= PROGRESS_SECONDS:
                    last_progress = now
                    say("       [progress %s / %s] multimon=%d  ok=%d  "
                        "not-decoded=%d  different=%d  (serial lines seen: %d)" %
                        (mmss(now - t0), mmss(duration), len(res.mm_packets),
                         res.ok, len(res.missing), len(res.mismatch),
                         collector.lines_seen))

        rt = threading.Thread(target=read_mm, daemon=True)
        pt = threading.Thread(target=pump, daemon=True)
        gt = threading.Thread(target=ticker, daemon=True)
        rt.start()
        gt.start()

        # Start the real-time playback to the ESP32 and the multimon-ng feed
        # at the same moment.
        player_err = []  # type: List[bytes]

        def drain_player_err() -> None:
            """Read the player's stderr continuously. Reading it only after
            wait() would deadlock the moment sox writes more than one pipe
            buffer of warnings."""
            if player is None or player.stderr is None:
                return
            try:
                for chunk in iter(lambda: player.stderr.read(4096), b""):
                    player_err.append(chunk)
            except Exception:
                pass

        if not mute_local:
            player = subprocess.Popen(play_cmd, env=env,
                                      stdout=subprocess.DEVNULL,
                                      stderr=subprocess.PIPE)
            et = threading.Thread(target=drain_player_err, daemon=True)
            et.start()
        pt.start()

        if player is not None:
            player.wait()
            et.join(timeout=2)
            if player.returncode not in (0, None) and not target_hit[0]:
                err = b"".join(player_err).decode("latin-1", "replace")
                sys.stderr.write("\n[audio] player failed (rc=%s): %s\n" %
                                 (player.returncode, err.strip()))
        pt.join(timeout=30)
        try:
            sox_p.wait(timeout=10)
        except subprocess.TimeoutExpired:
            sox_p.kill()
            try:
                sox_p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        try:
            mm_p.wait(timeout=60)
        except subprocess.TimeoutExpired:
            # Never let this escape: it used to propagate out of run_one_wav
            # and abort the whole test run (and the calibration with it).
            sys.stderr.write("\n[mm] multimon-ng did not exit within 60 s - killing it\n")
            mm_p.kill()
            try:
                mm_p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        rt.join(timeout=5)

        # Let the ESP32 finish the last frame and flush its console. At least
        # `window` seconds, so the last packets get the same chance to be
        # matched as every other one.
        time.sleep(max(tail, window))
    except KeyboardInterrupt:
        interrupted = True
        raise
    finally:
        done.set()
        for p in (player, sox_p, mm_p):
            if p is not None and p.poll() is None:
                try:
                    p.kill()
                except Exception:
                    pass
        if gt is not None:
            gt.join(timeout=2)
        if not mute_local:
            if interrupted:
                # Packets still waiting for their verdict are not counted:
                # the ESP32 has not had its full chance to answer them.
                dropped = matcher.drop_pending()
                dropped_ids = set(id(d) for d in dropped)
                res.mm_packets = [p for p in res.mm_packets
                                  if id(p) not in dropped_ids]
            else:
                feed_and_step(time.monotonic(), final=True)
            flush_resolved()
            res.esp_packets = list(matcher.esp_seen)
            res.offset = matcher.offset
        res.duration = time.monotonic() - t0
    return target_hit[0]


# --------------------------------------------------------------------------
# Comparison
# --------------------------------------------------------------------------


class LiveMatcher:
    """Pairs every multimon-ng packet with the ESP32 packet that answers it,
    while the audio is still playing.

    A pair is made only when BOTH the content and the time agree:

      * content - same source, destination, path and payload (see
        normalise_info() for what counts as "same");
      * time    - the ESP32 console line arrived within +/- `window` seconds
        of the moment multimon-ng printed the packet. This keeps a repeated
        beacon ("same station, same text, every 30 s") from being credited to
        the wrong transmission, which would hide a real miss.

    Verdict for each multimon-ng packet, given once it is known:
      ok        an ESP32 packet with the same content arrived in the window
      mismatch  none did, but one with the same header (src, dst, path) but a
                different payload did: decoded, but not correctly
      missing   nothing corresponding arrived: printed as NOT DECODED
    ESP32 packets that no multimon-ng packet claims become "extra".

    A packet is resolved as soon as its exact match shows up. Only the
    negative verdicts (mismatch / missing) have to wait `window` seconds,
    because the ESP32 may still be about to report it.

    Times are plain floats supplied by the caller (monotonic seconds), so the
    logic can be tested without any hardware or real waiting.
    """

    def __init__(self, window: float, offset_auto: bool = True,
                 offset: float = 0.0) -> None:
        self.window = window
        self._lock = threading.Lock()
        self._mm = []       # type: list   # [idx, t, Packet], unresolved
        self._esp = []      # type: list   # [t, Packet, used], unclaimed
        self._n_mm = 0
        self.esp_seen = []  # type: List[Packet]
        # Latency skew. The multimon-ng leg is paced tight to real time, while
        # the ESP32 leg goes through ALSA output buffering, the ADC, the demod
        # and the UART console - commonly a few hundred ms, sometimes over a
        # second. Left uncorrected it eats the match window and manufactures
        # NOT DECODED verdicts. It is estimated from confirmed matches and then
        # applied to every later comparison.
        self.offset_auto = offset_auto
        self.offset = offset
        self._offset_samples = []  # type: List[float]
        self.offset_locked = not offset_auto

    def add_mm(self, t: float, pkt: Packet) -> int:
        with self._lock:
            self._n_mm += 1
            self._mm.append([self._n_mm, t, pkt])
            return self._n_mm

    def add_esp(self, t: float, pkt: Packet) -> None:
        with self._lock:
            self._esp.append([t, pkt, False])
            self.esp_seen.append(pkt)

    def pending(self) -> int:
        with self._lock:
            return len(self._mm)

    def drop_pending(self) -> List[Packet]:
        """Forget unresolved multimon-ng packets (used when interrupted, so
        they are not reported as missing before the ESP32 had its chance)."""
        with self._lock:
            dropped = [m[2] for m in self._mm]
            self._mm = []
            return dropped

    def step(self, now: float, final: bool = False) -> List[tuple]:
        """Resolve what can be resolved. Returns events, in order:
             ("ok",       idx, t_mm, mm,  t_esp, esp)
             ("mismatch", idx, t_mm, mm,  t_esp, esp)
             ("missing",  idx, t_mm, mm,  None,  None)
             ("extra",    None, None, None, t_esp, esp)
        `final=True` treats every remaining packet as past its deadline."""
        w = self.window
        events = []  # type: List[tuple]
        with self._lock:
            off = self.offset

            def skew(te: float, tm: float) -> float:
                """Time distance, with the measured latency skew removed."""
                return abs(te - tm - off)

            # Pass 1 - exact content matches, closest in time first, so that
            # when the same packet was sent twice in a row the ESP32 packet
            # goes to the transmission it is nearest to.
            cands = []
            for mi, (idx, tm, mp) in enumerate(self._mm):
                for ei, (te, ep, used) in enumerate(self._esp):
                    if used or skew(te, tm) > w:
                        continue
                    if ep.key_header() == mp.key_header() and ep.info == mp.info:
                        cands.append((skew(te, tm), mi, ei))
            cands.sort()
            mm_done = set()
            for _, mi, ei in cands:
                if mi in mm_done or self._esp[ei][2]:
                    continue
                idx, tm, mp = self._mm[mi]
                te, ep, _u = self._esp[ei]
                self._esp[ei][2] = True
                mm_done.add(mi)
                self._note_offset(te - tm)
                events.append(("ok", idx, tm, mp, te, ep))

            # Pass 2 - packets whose deadline passed. Exactly ONE verdict is
            # produced per multimon-ng packet, and any ESP32 packet used to
            # reach that verdict is marked used so it cannot ALSO be reported
            # as an "extra" afterwards:
            #   same header, different payload -> mismatch
            #   same payload, different header -> corrupt (header damaged)
            #   nothing at all                 -> missing
            for mi, (idx, tm, mp) in enumerate(self._mm):
                if mi in mm_done:
                    continue
                if not final and now < tm + w + abs(off):
                    continue
                best_hdr = None
                best_info = None
                for ei, (te, ep, used) in enumerate(self._esp):
                    if used or skew(te, tm) > w:
                        continue
                    if ep.key_header() == mp.key_header():
                        if (best_hdr is None or
                                skew(te, tm) < skew(self._esp[best_hdr][0], tm)):
                            best_hdr = ei
                    elif ep.info == mp.info:
                        if (best_info is None or
                                skew(te, tm) < skew(self._esp[best_info][0], tm)):
                            best_info = ei
                mm_done.add(mi)
                if best_hdr is not None:
                    te, ep, _u = self._esp[best_hdr]
                    self._esp[best_hdr][2] = True
                    events.append(("mismatch", idx, tm, mp, te, ep))
                elif best_info is not None:
                    te, ep, _u = self._esp[best_info]
                    self._esp[best_info][2] = True
                    events.append(("corrupt", idx, tm, mp, te, ep))
                else:
                    events.append(("missing", idx, tm, mp, None, None))

            self._mm = [m for mi, m in enumerate(self._mm) if mi not in mm_done]

            # ESP32 packets nobody can claim any more (their window closed
            # with no multimon-ng packet, and none is still pending).
            keep = []
            for te, ep, used in self._esp:
                if used:
                    continue
                if final or now > te + w + abs(off):
                    events.append(("extra", None, None, None, te, ep))
                else:
                    keep.append([te, ep, used])
            self._esp = keep
        return events

    def _note_offset(self, delta: float) -> None:
        """Feed one confirmed (ESP32 - multimon-ng) time difference into the
        latency estimate. Called with the lock held. Once enough samples are
        in, the median is adopted as the offset: the median, not the mean, so
        one late console line cannot drag the whole estimate."""
        if self.offset_locked or not self.offset_auto:
            return
        if abs(delta) > OFFSET_MAX_SECONDS:
            return          # not sound-card latency; do not let it skew the estimate
        self._offset_samples.append(delta)
        if len(self._offset_samples) >= OFFSET_MIN_SAMPLES:
            self.offset = median(self._offset_samples)
            self.offset_locked = True


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------


def pct(n: int, total: int) -> float:
    return (100.0 * n / total) if total else 0.0


def print_file_report(res: FileResult, dry_run: bool = False) -> None:
    say("  multimon-ng decoded %d packet(s)" % len(res.mm_packets))
    if dry_run:
        return
    say("  ESP32 decoded %d packet(s)" % len(res.esp_packets))
    say("  -> OK: %d   DIFFERENT: %d   HDR-CORRUPT: %d   NOT DECODED: %d   "
        "EXTRA(esp only): %d" %
        (res.ok, len(res.mismatch), len(res.corrupt), len(res.missing), len(res.extra)))
    if res.offset:
        say("  -> measured esp32 latency vs multimon-ng: %+.2f s" % res.offset)
    for mp, ep in res.mismatch:
        say("    ! DIFFERENT")
        say("        multimon: %s" % mp.raw)
        say("        esp32   : %s" % ep.raw)
    for mp, ep in res.corrupt:
        say("    ! HEADER CORRUPT (payload matched)")
        say("        multimon: %s" % mp.raw)
        say("        esp32   : %s" % ep.raw)
    for mp in res.missing:
        say("    ! NOT DECODED by ESP32: %s" % mp.raw)


def print_summary(results: List[FileResult], volume: Optional[float] = None) -> int:
    total = sum(len(r.mm_packets) for r in results)
    ok = sum(r.ok for r in results)
    mism = sum(len(r.mismatch) for r in results)
    corr = sum(len(r.corrupt) for r in results)
    miss = sum(len(r.missing) for r in results)
    extra = sum(len(r.extra) for r in results)
    esp_total = sum(len(r.esp_packets) for r in results)
    offsets = [r.offset for r in results if r.offset]

    bar = "=" * 72
    print("\n" + bar)
    print("SUMMARY")
    print(bar)
    print("  %-30s %6s %6s %6s %6s %6s %6s" %
          ("file", "mm", "ok", "diff", "hdr", "n/dec", "extra"))
    for r in results:
        print("  %-30s %6d %6d %6d %6d %6d %6d" %
              (r.name[:30], len(r.mm_packets), r.ok, len(r.mismatch),
               len(r.corrupt), len(r.missing), len(r.extra)))
    print("  " + "-" * 70)
    if volume is not None:
        print("  Playback gain used for this test  : %.3f  (%+.1f dB)" %
              (volume, to_db(volume)))
    if offsets:
        print("  ESP32 latency vs multimon-ng      : %+.2f s (median of %d file(s))" %
              (median(offsets), len(offsets)))
    print("  Files tested                      : %d" % len(results))
    print("  Total packets (multimon-ng)       : %d" % total)
    print("  Packets seen by ESP32             : %d" % esp_total)
    print("  Decoded correctly                 : %d  (%.2f%%)" % (ok, pct(ok, total)))
    print("  Decoded with different content    : %d  (%.2f%%)" % (mism, pct(mism, total)))
    print("  Decoded with corrupt header       : %d  (%.2f%%)" % (corr, pct(corr, total)))
    print("  Missing (not decoded)             : %d  (%.2f%%)" % (miss, pct(miss, total)))
    print("  Extra (ESP32 only, not an error)  : %d" % extra)
    print(bar)
    if total == 0:
        print("RESULT: no packets were decoded by multimon-ng - nothing to compare.")
        return 2
    return 0 if (mism == 0 and corr == 0 and miss == 0) else 1


# --------------------------------------------------------------------------
# Startup helpers
# --------------------------------------------------------------------------


_HAVE_STDBUF = False


def check_tools() -> None:
    global _HAVE_STDBUF
    missing = [t for t in ("multimon-ng", "sox", "play") if shutil.which(t) is None]
    if missing:
        sys.stderr.write("Missing required program(s): %s\n" % ", ".join(missing))
        sys.stderr.write("  Debian/Ubuntu: sudo apt install multimon-ng sox libsox-fmt-all\n")
        sys.exit(2)
    _HAVE_STDBUF = shutil.which("stdbuf") is not None
    if not _HAVE_STDBUF:
        sys.stderr.write(
            "WARNING: `stdbuf` not found (package coreutils). multimon-ng's output will be\n"
            "  block-buffered on the pipe, so its packets may arrive in bursts and be\n"
            "  timestamped late, which shows up as spurious NOT DECODED verdicts.\n")


def find_wavs(directory: str) -> List[str]:
    """Every .wav in the directory, whatever the case of the extension.

    glob() is case-sensitive on Linux and case-insensitive on macOS, so
    matching on the lower-cased name is both complete and duplicate-free."""
    try:
        entries = os.listdir(directory)
    except OSError:
        return []
    files = [os.path.join(directory, e) for e in entries
             if e.lower().endswith(".wav") and
             os.path.isfile(os.path.join(directory, e))]
    return sorted(set(files), key=lambda s: s.lower())


class VolumeSearch:
    """Searches for the best playback level before the real, reported test run.

    Why this is not a hill climb
    ----------------------------
    Decode rate against input level is not a peak, it is a plateau: too quiet
    and the demodulator is fighting the noise floor and the ADC's own
    quantisation; too loud and the ADC clips and the firmware says so; and
    between those two knees the rate is flat. Taking the argmax of a flat,
    noisy curve just picks noise - at 50 packets the 95% interval around 90%
    is about +/-8 percentage points, so a "2% better" volume is half a packet
    of luck. What is worth finding is the CENTRE of the plateau, because that
    is the level with the most margin on both sides.

    The search therefore works like this:

      Phase 1  bracket the clipping threshold, stepping +/-6 dB. Clipping is a
               binary signal the firmware reports itself ("RX audio is
               over-range"), so it can be probed with tiny 8-packet batches
               instead of full ones.
      Phase 2  bisect that bracket 3 times: +/-0.75 dB on the threshold.
      Phase 3  score full batches at 3, 6, 9, 12 and 18 dB below the
               threshold, stopping as soon as the lower knee is clearly past.
      Phase 4  return the geometric centre of the plateau - every point whose
               Wilson interval still overlaps the best point's - clamped to at
               least MIN_CLIP_MARGIN_DB below the clipping threshold.

    Scoring:  success = ok + extra,  trials = multimon packets + extra.
      * `mismatch` and `corrupt` are FAILURES, not successes. The whole point
        of the bench is content equality, so a volume that yields corrupted
        payloads must not score like one that yields correct ones.
      * `extra` (the ESP32 decoded a frame multimon-ng missed entirely) is a
        SUCCESS. It is the strongest evidence a level can give: the firmware
        beat the reference decoder on that frame.

    Everything measured is cached by rounded dB, so no volume is ever probed
    twice, and probes advance through the wav set round-robin instead of
    always restarting at the first file - otherwise a set whose first file
    holds more than one batch would have the level tuned on a single
    recording.

    Runs with its own throwaway FileResult objects; nothing here is counted in
    the final report other than the resulting volume.
    """

    def __init__(self, wavs: List[str], audio_device: Optional[str],
                 tail: float, window: float, collector: SerialCollector,
                 mm_extra: List[str], start_volume: float,
                 batch_size: int = AUTO_VOLUME_BATCH,
                 max_rounds: int = AUTO_VOLUME_MAX_ROUNDS,
                 headroom_db: float = HEADROOM_DB,
                 clip_rate: float = CLIP_RATE_THRESHOLD,
                 vol_min: float = AUTO_VOLUME_MIN,
                 vol_max: float = AUTO_VOLUME_MAX,
                 max_passes: int = MAX_WAV_PASSES,
                 normalise: bool = False,
                 offset_auto: bool = True) -> None:
        self.wavs = wavs
        self.audio_device = audio_device
        self.tail = tail
        self.window = window
        self.collector = collector
        self.mm_extra = mm_extra
        self.volume = start_volume
        self.batch_size = batch_size
        self.max_rounds = max_rounds
        self.headroom_db = headroom_db
        self.clip_rate = clip_rate
        self.vol_min = vol_min
        self.vol_max = vol_max
        self.max_passes = max_passes
        self.normalise = normalise
        self.offset_auto = offset_auto
        self.cache = {}        # type: dict   # rounded dB -> measurement
        self.probes_used = 0
        # Budget is counted in BATCHES OF PACKETS, not in calls: an 8-packet
        # clip probe costs 8/batch_size of a round, not a whole one. Charging
        # every probe as a full round would let the cheap threshold hunt
        # starve the expensive plateau sweep that actually picks the level.
        self.budget_used = 0.0
        self._cursor = 0       # round-robin position in the wav set
        self.offset = 0.0      # latency skew learned during calibration
        self.clip_db = None    # type: Optional[float]

    # ---------------------------------------------------------------- probe
    def _clamp(self, volume: float) -> float:
        return max(self.vol_min, min(self.vol_max, volume))

    def _measure(self, volume: float, target: int) -> dict:
        """Play `target` packets at `volume` and return the measurement.

        Overridable: --selftest replaces this with a simulated device so the
        search logic can be tested without any hardware."""
        key = round(to_db(volume), 2)
        cached = self.cache.get(key)
        if cached is not None and cached["trials"] >= target:
            return cached

        batch = FileResult(name="<probe %+.1f dB>" % key)
        overrange_before, _ = self.collector.snapshot_overrange()
        passes = 0
        while len(batch.mm_packets) + len(batch.extra) < target:
            if self._cursor >= len(self.wavs):
                self._cursor = 0
                passes += 1
                if passes >= self.max_passes:
                    # Without this the loop restarts the wav set for ever when
                    # nothing decodes at all (silent files, muted card, wrong
                    # ALSA device) and only Ctrl-C can end the run.
                    say("      probe incomplete: %d/%d packet(s) after %d pass(es) "
                        "over the wav set - check the audio routing and the files" %
                        (len(batch.mm_packets) + len(batch.extra), target, passes))
                    break
            wav = self.wavs[self._cursor]
            self._cursor += 1
            remaining = target - (len(batch.mm_packets) + len(batch.extra))
            res = FileResult(name=os.path.basename(wav))
            run_one_wav(res, wav, self.audio_device, volume, self.tail,
                        self.window, self.collector, self.mm_extra,
                        mute_local=False, stop_at_mm_packets=remaining,
                        normalise=self.normalise, offset_auto=self.offset_auto,
                        offset_seed=self.offset)
            if res.offset:
                self.offset = res.offset
            batch.mm_packets.extend(res.mm_packets)
            batch.esp_packets.extend(res.esp_packets)
            batch.ok += res.ok
            batch.mismatch.extend(res.mismatch)
            batch.corrupt.extend(res.corrupt)
            batch.missing.extend(res.missing)
            batch.extra.extend(res.extra)

        n_mm = len(batch.mm_packets)
        n_extra = len(batch.extra)
        warns = self.collector.overrange_since(overrange_before)
        seen = max(1, n_mm + n_extra)
        m = {
            "volume": volume,
            "db": key,
            "ok": batch.ok,
            "mismatch": len(batch.mismatch),
            "corrupt": len(batch.corrupt),
            "missing": len(batch.missing),
            "extra": n_extra,
            "mm": n_mm,
            "success": batch.ok + n_extra,
            "trials": n_mm + n_extra,
            "clip_rate": warns / float(seen),
        }
        m["score"] = 100.0 * m["success"] / max(1, m["trials"])
        self.cache[key] = m
        self.probes_used += 1
        self.budget_used += target / float(max(1, self.batch_size))
        say("  [probe %2d, budget %.1f/%d] gain=%.3f (%+5.1f dB)  mm=%d ok=%d diff=%d hdr=%d "
            "miss=%d extra=%d  score=%.1f%%  clip=%.2f/pkt" %
            (self.probes_used, self.budget_used, self.max_rounds, volume, key, n_mm, batch.ok,
             len(batch.mismatch), len(batch.corrupt), len(batch.missing),
             n_extra, m["score"], m["clip_rate"]))
        return m

    def _clips(self, volume: float) -> bool:
        return self._measure(volume, CLIP_PROBE_PACKETS)["clip_rate"] > self.clip_rate

    def _budget_left(self, cost: float = 0.0) -> bool:
        """Is there budget for one more probe costing `cost` batches?"""
        return (self.budget_used + cost) < self.max_rounds

    # ---------------------------------------------------------- phases 1+2
    def _find_clip_threshold(self, start_db: float) -> Tuple[float, bool]:
        """Bracket, then bisect, the level at which the ADC starts clipping.

        Returns (threshold in dB, True if it was actually observed). Unlike
        the previous algorithm this moves in BOTH directions: it goes down
        when it clips and up when it does not, so a starting volume that is
        already far too high is not a dead end."""
        lo_db = None   # highest level known to be clean
        hi_db = None   # lowest level known to clip
        db = start_db
        floor_db, ceil_db = to_db(self.vol_min), to_db(self.vol_max)

        # Reserve most of the budget for the scoring sweep: the threshold
        # hunt is cheap, but it must not be allowed to eat the rounds that
        # actually decide the level.
        clip_cost = CLIP_PROBE_PACKETS / float(max(1, self.batch_size))
        hunt_cap = max(clip_cost, 0.25 * self.max_rounds)
        while self.budget_used + clip_cost < hunt_cap:
            if self._clips(to_lin(db)):
                hi_db = db if hi_db is None else min(hi_db, db)
                if lo_db is not None:
                    break
                db -= COARSE_STEP_DB
                if db < floor_db:
                    return floor_db, True
            else:
                lo_db = db if lo_db is None else max(lo_db, db)
                if hi_db is not None:
                    break
                db += COARSE_STEP_DB
                if db > ceil_db:
                    return ceil_db, False

        if hi_db is None:
            return ceil_db, False           # never clipped anywhere we looked
        if lo_db is None:
            return floor_db, True           # clipped all the way down

        while (self.budget_used + clip_cost < hunt_cap and
               (hi_db - lo_db) > (COARSE_STEP_DB / 2 ** BISECT_ITERS)):
            mid = 0.5 * (lo_db + hi_db)
            if self._clips(to_lin(mid)):
                hi_db = mid
            else:
                lo_db = mid
        return lo_db, True

    # ------------------------------------------------------------- phase 3+4
    def run(self) -> float:
        if not self.wavs:
            return self.volume

        say("\n" + "=" * 72)
        say("AUTO-VOLUME CALIBRATION (clip threshold + plateau centre)")
        say("=" * 72)
        say("  Start gain %.3f (%+.1f dB), range %.3f..%.3f, budget %d probe(s), "
            "%d packet(s) per scoring probe" %
            (self.volume, to_db(self.volume), self.vol_min, self.vol_max,
             self.max_rounds, self.batch_size))

        clip_db, observed = self._find_clip_threshold(to_db(self._clamp(self.volume)))
        self.clip_db = clip_db
        if observed:
            say("  Clipping threshold: %+.1f dB (gain %.3f)" % (clip_db, to_lin(clip_db)))
        else:
            say("  No clipping seen up to %+.1f dB (gain %.3f) - the hardware level "
                "into the ADC may be too low; check the RX trimmer." %
                (clip_db, to_lin(clip_db)))

        # Sanity check: with no packets at all there is nothing to calibrate.
        probed = [m for m in self.cache.values() if m["mm"] > 0 or m["extra"] > 0]
        if not probed:
            self.volume = self._clamp(to_lin(clip_db - self.headroom_db))
            say("  No packets decoded during calibration at any level - falling back "
                "to %.3f (%+.1f dB, threshold - %.0f dB)." %
                (self.volume, to_db(self.volume), self.headroom_db))
            say("=" * 72)
            return self.volume

        points = []  # type: List[Tuple[float, dict]]
        best = None  # type: Optional[Tuple[float, dict]]
        for off in PLATEAU_OFFSETS_DB:
            if not self._budget_left(1.0):
                say("  Probe budget spent; stopping the plateau sweep "
                    "(raise it with --auto_volume_max_rounds).")
                break
            db = clip_db + off
            if db < to_db(self.vol_min):
                break
            m = self._measure(self._clamp(to_lin(db)), self.batch_size)
            if m["trials"] == 0:
                continue
            points.append((db, m))
            if best is None or m["score"] > best[1]["score"]:
                best = (db, m)
            elif m["score"] < best[1]["score"] - KNEE_DROP_PCT:
                say("      score fell %.0f points below the best - the lower knee is "
                    "past, no need to go quieter" % (best[1]["score"] - m["score"]))
                break

        if not points or best is None:
            self.volume = self._clamp(to_lin(clip_db - self.headroom_db))
            say("  No usable score data - using threshold - %.0f dB = %.3f (%+.1f dB)." %
                (self.headroom_db, self.volume, to_db(self.volume)))
            say("=" * 72)
            return self.volume

        # Phase 4: every point statistically tied with the best one forms the
        # plateau; its geometric centre (arithmetic centre in dB) is the level
        # with the most margin on both sides.
        best_lo, _ = wilson(best[1]["success"], max(1, best[1]["trials"]))
        tied = [db for db, m in points
                if wilson(m["success"], max(1, m["trials"]))[1] >= best_lo]
        if not tied:
            tied = [best[0]]
        centre_db = 0.5 * (min(tied) + max(tied))
        centre_db = min(centre_db, clip_db - MIN_CLIP_MARGIN_DB)
        self.volume = self._clamp(to_lin(centre_db))

        say("  Plateau: %+.1f .. %+.1f dB (%d tied point(s) of %d probed); "
            "best raw score %.1f%%" %
            (min(tied), max(tied), len(tied), len(points), best[1]["score"]))
        say("  Chosen gain: %.3f (%+.1f dB), %.1f dB below the clipping threshold" %
            (self.volume, to_db(self.volume), clip_db - centre_db))
        self._advise()
        say("=" * 72)
        return self.volume

    def _advise(self) -> None:
        """The firmware's over-range message names the real remedy: the RX
        trimmer or the transceiver volume. A calibrated gain far from unity in
        EITHER direction is a statement about the interface hardware, not just
        a number to hand to `play`."""
        db = to_db(self.volume)
        if db < -6.0:
            say("  NOTE: more than 6 dB of attenuation was needed. The hardware level "
                "into the ESP32 ADC is too hot - turn the RX trimmer (or the radio's "
                "volume) down and re-run, so the bench can work near 0 dB.")
        elif db > 6.0:
            say("  NOTE: more than 6 dB of boost was needed. The hardware level into "
                "the ESP32 ADC is too low - turn the RX trimmer up and re-run. "
                "Boosting digitally also amplifies the sound card's own noise floor.")


# Backwards-compatible alias: older invocations and notes refer to the class
# by its previous name.
AutoVolumeCalibrator = VolumeSearch


def wait_ready(col: SerialCollector, settle: float) -> None:
    """Opening the port usually resets an ESP32 (DTR/RTS wired to EN/IO0).
    Wait for console traffic and for the boot to complete."""
    say("Waiting %.1f s for the ESP32 to be ready ..." % settle)
    end = time.monotonic() + settle
    while time.monotonic() < end:
        time.sleep(0.2)
    if not col.alive.is_set():
        say("  WARNING: no data received from the serial port yet. The firmware "
            "may be quiet until it hears/sends something; continuing.")
    else:
        say("  serial is alive (%d console line(s) so far)." % col.lines_seen)


class _SimulatedVolumeSearch(VolumeSearch):
    """VolumeSearch driven by a simulated device instead of real hardware.

    The model is the plateau the real system exhibits: nothing decodes below
    -20 dB, everything decodes from -18 dB to -3 dB, and above 0 dB the ADC
    clips. A correct search must land near the centre of that plateau
    (about -10.5 dB) no matter where it starts."""

    def __init__(self, *a, **kw):
        self.knee_low_db = kw.pop("knee_low_db", -18.0)
        self.knee_high_db = kw.pop("knee_high_db", -3.0)
        self.clip_at_db = kw.pop("clip_at_db", 0.0)
        super().__init__(*a, **kw)

    def _measure(self, volume: float, target: int) -> dict:
        key = round(to_db(volume), 2)
        cached = self.cache.get(key)
        if cached is not None and cached["trials"] >= target:
            return cached
        db = key
        if db >= self.clip_at_db:
            rate, clip = 0.55, 1.0
        elif db >= self.knee_low_db:
            rate, clip = 1.0, 0.0
        elif db >= self.knee_low_db - 8.0:
            span = (db - (self.knee_low_db - 8.0)) / 8.0
            rate, clip = max(0.0, span), 0.0
        else:
            rate, clip = 0.0, 0.0
        success = int(round(rate * target))
        m = {"volume": volume, "db": key, "ok": success, "mismatch": 0, "corrupt": 0,
             "missing": target - success, "extra": 0, "mm": target,
             "success": success, "trials": target, "clip_rate": clip}
        m["score"] = 100.0 * success / max(1, target)
        self.cache[key] = m
        self.probes_used += 1
        self.budget_used += target / float(max(1, self.batch_size))
        return m


def selftest() -> int:
    """Unit tests that need no hardware, no audio and no waiting."""
    failures = []

    def check(name: str, cond: bool, detail: str = "") -> None:
        if cond:
            print("  PASS  %s" % name)
        else:
            failures.append(name)
            print("  FAIL  %s  %s" % (name, detail))

    print("Normalisation and parsing")
    p_mm = make_packet("LU1ABC-0", "APRS", ["WIDE1-1"], b"hello\r", "mm")
    p_esp = make_packet("LU1ABC", "APRS", ["WIDE1-1*"], b"hello", "esp")
    check("SSID -0, digipeated * and trailing CR all normalise away",
          p_mm.key_header() == p_esp.key_header() and p_mm.info == p_esp.info)
    check("trailing dot is kept (truncation must not pass as equal)",
          make_packet("A", "B", [], b"hi.", "").info !=
          make_packet("A", "B", [], b"hi", "").info)
    check("payload LF becomes '.' like multimon-ng prints it",
          make_packet("A", "B", [], b"a\nb", "").info == b"a.b")
    check("colon inside the payload does not break the header split",
          parse_tnc2(b"LU1ABC>APRS::LU2DEF   :hi{01") is not None)
    check("ESP console line with a log prefix parses",
          parse_esp_line(b"I (12345) aprs_service: RX: LU1ABC>APRS,WIDE1-1:test") is not None)

    print("LiveMatcher verdicts")
    lm = LiveMatcher(5.0, offset_auto=False)
    lm.add_mm(10.0, p_mm)
    lm.add_esp(11.0, p_esp)
    check("match inside the window is ok",
          [e[0] for e in lm.step(20.0, final=True)] == ["ok"])

    lm = LiveMatcher(5.0, offset_auto=False)
    lm.add_mm(10.0, p_mm)
    lm.add_esp(16.0, p_esp)
    check("match outside the window is missing + extra",
          sorted(e[0] for e in lm.step(30.0, final=True)) == ["extra", "missing"])

    lm = LiveMatcher(5.0, offset_auto=False)
    bad_hdr = make_packet("LU1XYZ", "APRS", ["WIDE1-1"], b"hello", "esp-bad-hdr")
    lm.add_mm(10.0, p_mm)
    lm.add_esp(10.2, bad_hdr)
    verdicts = [e[0] for e in lm.step(20.0, final=True)]
    check("header-corrupt frame gives ONE verdict, not missing+extra",
          verdicts == ["corrupt"], "got %r" % (verdicts,))

    lm = LiveMatcher(5.0, offset_auto=False)
    bad_pl = make_packet("LU1ABC", "APRS", ["WIDE1-1"], b"hellX", "esp-bad-payload")
    lm.add_mm(10.0, p_mm)
    lm.add_esp(10.2, bad_pl)
    check("payload-corrupt frame is a mismatch only",
          [e[0] for e in lm.step(20.0, final=True)] == ["mismatch"])

    lm = LiveMatcher(5.0, offset_auto=False)
    lm.add_mm(10.0, p_mm)
    lm.add_mm(11.0, p_mm)
    lm.add_esp(11.05, p_esp)
    lm.add_esp(10.05, p_esp)
    ev = [e for e in lm.step(30.0, final=True) if e[0] == "ok"]
    check("two identical beacons pair with the nearest transmission",
          len(ev) == 2 and all(abs(e[4] - e[2]) < 0.2 for e in ev))

    lm = LiveMatcher(1.0, offset_auto=True)
    for i in range(OFFSET_MIN_SAMPLES):
        pk = make_packet("LU1ABC", "APRS", [], ("beacon %d" % i).encode(), "")
        lm.add_mm(float(i), pk)
        lm.add_esp(float(i) + 0.8, pk)
        lm.step(float(i) + 0.9)
    check("latency skew is learned from confirmed matches",
          lm.offset_locked and abs(lm.offset - 0.8) < 0.05,
          "offset=%.3f" % lm.offset)

    print("Statistics")
    lo, hi = wilson(45, 50)
    check("Wilson interval at 45/50 is wide enough to swallow 1-packet noise",
          (hi - lo) > 0.10, "width=%.3f" % (hi - lo))
    check("dB round-trip", abs(to_lin(to_db(0.37)) - 0.37) < 1e-9)

    print("VolumeSearch convergence (simulated plateau, centre = -10.5 dB)")
    for start in (0.05, 1.0, 4.0):
        vs = _SimulatedVolumeSearch(
            ["a.wav", "b.wav"], None, 0.0, 5.0, None, [], start,
            batch_size=50, max_rounds=12)
        chosen = to_db(vs.run())
        # Cost is measured in batches of packets (what the run actually pays
        # in wall-clock time), not in probe calls: the threshold hunt makes
        # many cheap 8-packet probes on purpose.
        check("start %.2f converges to the plateau centre within budget "
              "(%.1f/%d batches, %d probes)" %
              (start, vs.budget_used, vs.max_rounds, vs.probes_used),
              abs(chosen - (-10.5)) <= 1.5 and vs.budget_used <= vs.max_rounds,
              "chose %+.1f dB at cost %.1f" % (chosen, vs.budget_used))

    vs = _SimulatedVolumeSearch(["a.wav"], None, 0.0, 5.0, None, [], 1.0,
                                batch_size=50, max_rounds=12)
    vs.run()
    check("chosen level keeps margin below the clipping threshold",
          to_db(vs.volume) <= vs.clip_db - MIN_CLIP_MARGIN_DB + 1e-6)

    print("Termination with a wav set that decodes nothing")
    # The old _run_batch_until restarted the wav iterator unconditionally, so
    # a silent or misrouted set looped for ever and only Ctrl-C ended the run.
    global run_one_wav
    real_run_one_wav = run_one_wav
    calls = [0]

    def _decodes_nothing(res, wav, *a, **kw):
        calls[0] += 1
        if calls[0] > 200:                      # the guard failed; stop the test
            raise AssertionError("run_one_wav called %d times - no pass limit" % calls[0])
        return False

    class _NoSerial:
        lines_seen = 0
        file_t0 = 0.0
        def snapshot_overrange(self): return (0, 0.0)
        def overrange_since(self, n): return 0

    try:
        run_one_wav = _decodes_nothing
        vs = VolumeSearch(["a.wav", "b.wav"], None, 0.0, 5.0, _NoSerial(), [], 1.0,
                          batch_size=50, max_rounds=4, max_passes=MAX_WAV_PASSES)
        m = vs._measure(1.0, 50)
        check("a probe over a silent wav set terminates instead of looping",
              m["trials"] == 0 and calls[0] <= len(["a.wav", "b.wav"]) * MAX_WAV_PASSES,
              "%d playback call(s)" % calls[0])
        vs2 = VolumeSearch(["a.wav"], None, 0.0, 5.0, _NoSerial(), [], 1.0,
                           batch_size=50, max_rounds=4)
        check("the whole search terminates on a silent wav set",
              vs2.run() > 0.0)
    except AssertionError as exc:
        check("a probe over a silent wav set terminates instead of looping",
              False, str(exc))
    finally:
        run_one_wav = real_run_one_wav

    print("")
    if failures:
        print("SELFTEST FAILED: %d of the checks above did not pass" % len(failures))
        return 1
    print("SELFTEST OK")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Test esp32idf_APRS with a battery of real-APRS WAV files, "
                    "using multimon-ng as the reference decoder.")
    ap.add_argument("--wav_dir", default=".",
                    help="directory with the .wav files (default: current directory)")
    ap.add_argument("--serial_port", default=DEFAULT_SERIAL,
                    help="ESP32 console serial port (default: %s)" % DEFAULT_SERIAL)
    ap.add_argument("--baud", type=int, default=SERIAL_BAUD,
                    help="serial speed, 8N1 (default: %d)" % SERIAL_BAUD)
    ap.add_argument("--audio_device", default=None,
                    help="ALSA device wired to the ESP32 audio input, e.g. hw:1,0 "
                         "(default: system default output)")
    ap.add_argument("--volume", type=float, default=1.0,
                    help="playback gain applied to the ESP32 leg only (default 1.0). "
                         "Used as the starting point for auto-volume calibration "
                         "unless --no_auto_volume is given.")
    ap.add_argument("--normalise", "--normalize", dest="normalise", action="store_true",
                    help="bring every wav to -1 dBFS in the play chain (sox 'gain -n -1') "
                         "so one gain is valid across recordings made at different levels, "
                         "and gains above 1.0 stop meaning 'clip inside sox'")
    ap.add_argument("--headroom_db", type=float, default=HEADROOM_DB,
                    help="dB below the clipping threshold to fall back to when no "
                         "plateau could be scored (default %.0f)" % HEADROOM_DB)
    ap.add_argument("--clip_rate", type=float, default=CLIP_RATE_THRESHOLD,
                    help="over-range warnings per packet above which a level counts "
                         "as clipping (default %.2f); a single transient warning is "
                         "not enough" % CLIP_RATE_THRESHOLD)
    ap.add_argument("--volume_min", type=float, default=AUTO_VOLUME_MIN,
                    help="lowest gain the search may use (default %.2f)" % AUTO_VOLUME_MIN)
    ap.add_argument("--volume_max", type=float, default=AUTO_VOLUME_MAX,
                    help="highest gain the search may use (default %.2f)" % AUTO_VOLUME_MAX)
    ap.add_argument("--max_passes", type=int, default=MAX_WAV_PASSES,
                    help="passes over the wav set before a calibration probe gives up "
                         "(default %d)" % MAX_WAV_PASSES)
    ap.add_argument("--no_offset_auto", action="store_true",
                    help="do not estimate the ESP32-vs-multimon-ng latency skew; "
                         "compare raw timestamps instead")
    ap.add_argument("--selftest", action="store_true",
                    help="run the built-in unit tests (no hardware, no audio) and exit")
    ap.add_argument("--no_auto_volume", action="store_true",
                    help="skip the auto-volume calibration pass and use --volume as-is "
                         "for the whole run")
    ap.add_argument("--auto_volume_batch", type=int, default=AUTO_VOLUME_BATCH,
                    help="number of packets to test per volume try during "
                         "auto-volume calibration - counts both multimon-ng "
                         "packets and ESP32-only ones multimon-ng missed "
                         "(default %d)" % AUTO_VOLUME_BATCH)
    ap.add_argument("--auto_volume_max_rounds", type=int, default=AUTO_VOLUME_MAX_ROUNDS,
                    help="search budget for the auto-volume pass, in batches of "
                         "--auto_volume_batch packets (default %d). Cheap 8-packet "
                         "clipping probes cost a fraction of a batch, full scoring "
                         "probes cost one each." % AUTO_VOLUME_MAX_ROUNDS)
    ap.add_argument("--tail", type=float, default=DEFAULT_TAIL_SECONDS,
                    help="seconds to keep listening after each file (default %.1f)" % DEFAULT_TAIL_SECONDS)
    ap.add_argument("--settle", type=float, default=4.0,
                    help="seconds to wait after opening the serial port (default 4)")
    ap.add_argument("--pause", type=float, default=1.0,
                    help="pause between files in seconds (default 1)")
    ap.add_argument("--match_window", type=float, default=5.0,
                    help="an ESP32 packet answers a multimon-ng packet only if it "
                         "arrives within this many seconds of it; a packet the ESP32 "
                         "has not reported after this time is shown as NOT DECODED "
                         "(default 5)")
    ap.add_argument("--no_play", action="store_true",
                    help="do not play audio to the sound card (only run multimon-ng; "
                         "useful to dry-run the parser)")
    ap.add_argument("--mm_args", default="",
                    help="extra multimon-ng arguments, e.g. '-A' (quoted)")
    ap.add_argument("--list_audio", action="store_true",
                    help="list ALSA playback devices and exit")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    if args.list_audio:
        if shutil.which("aplay") is None:
            sys.stderr.write("aplay not found - install alsa-utils "
                             "(Debian/Ubuntu: sudo apt install alsa-utils)\n")
            return 2
        return subprocess.call(["aplay", "-l"])

    if args.auto_volume_batch < 1:
        sys.stderr.write("--auto_volume_batch must be >= 1 (got %d)\n" % args.auto_volume_batch)
        return 2
    if args.auto_volume_max_rounds < 1:
        sys.stderr.write("--auto_volume_max_rounds must be >= 1 (got %d)\n" %
                          args.auto_volume_max_rounds)
        return 2
    if args.max_passes < 1:
        sys.stderr.write("--max_passes must be >= 1 (got %d)\n" % args.max_passes)
        return 2
    if not (0.0 < args.volume_min < args.volume_max):
        sys.stderr.write("--volume_min must be > 0 and < --volume_max (got %g and %g)\n" %
                         (args.volume_min, args.volume_max))
        return 2
    if not (args.volume_min <= args.volume <= args.volume_max):
        sys.stderr.write("--volume must be within [%g, %g] (got %g)\n" %
                         (args.volume_min, args.volume_max, args.volume))
        return 2
    if not (0.0 < args.clip_rate <= 1.0):
        sys.stderr.write("--clip_rate must be in (0, 1] (got %g)\n" % args.clip_rate)
        return 2
    if args.match_window <= 0:
        sys.stderr.write("--match_window must be > 0 (got %g)\n" % args.match_window)
        return 2

    check_tools()

    if not os.path.isdir(args.wav_dir):
        sys.stderr.write("wav_dir not found: %s\n" % args.wav_dir)
        return 2
    wavs = find_wavs(args.wav_dir)
    if not wavs:
        sys.stderr.write("No .wav files in %s\n" % os.path.abspath(args.wav_dir))
        return 2

    print("Found %d wav file(s) in %s" % (len(wavs), os.path.abspath(args.wav_dir)))
    if args.no_play:
        print("DRY RUN (--no_play): only multimon-ng runs; serial port and sound "
              "card are NOT used, so ESP32 results below are not meaningful.")
    else:
        print("Serial: %s @ %d 8N1   Audio: %s" %
              (args.serial_port, args.baud, args.audio_device or "system default"))
    sys.stdout.flush()

    col = None  # type: Optional[SerialCollector]
    if not args.no_play:
        try:
            col = SerialCollector(args.serial_port, args.baud)
        except (serial.SerialException, OSError) as exc:
            sys.stderr.write("Cannot open serial port %s: %s\n" % (args.serial_port, exc))
            return 2
    else:
        # dry-run: a dummy collector object that never sees any ESP32 output
        class _Dummy:
            alive = threading.Event()
            lines_seen = 0
            file_t0 = 0.0
            def snapshot_index(self): return 0
            def since(self, i): return []
            def between(self, a, b): return []
            def items_between(self, a, b): return []
            def items_from(self, i, t): return ([], 0)
            def snapshot_overrange(self): return (0, 0.0)
            def overrange_since(self, n): return 0
            def start(self): pass
            def stop(self): pass
        col = _Dummy()  # type: ignore
    col.start()
    if not args.no_play:
        wait_ready(col, args.settle)

    mm_extra = args.mm_args.split() if args.mm_args else []

    # -- Auto-volume calibration --------------------------------------
    # Searches for the optimal playback volume BEFORE the real, reported
    # test: it plays through the WAV set (looping it if needed), trying up
    # to args.auto_volume_max_rounds (10 by default; --auto_volume_max_rounds
    # to change it) different volumes, evaluating every AUTO_VOLUME_BATCH
    # multimon-ng packets. It raises the volume when little is decoded,
    # lowers it the moment the firmware itself reports over-range (clipped)
    # audio, and remembers whichever volume decoded the highest percentage.
    # That best volume is then used for the complete run below, which starts
    # over from the first file.
    final_volume = args.volume
    offset_seed = 0.0
    offset_auto = not args.no_offset_auto
    if not args.no_play and not args.no_auto_volume:
        calibrator = VolumeSearch(
            wavs, args.audio_device, args.tail, args.match_window, col,
            mm_extra, args.volume,
            batch_size=args.auto_volume_batch,
            max_rounds=args.auto_volume_max_rounds,
            headroom_db=args.headroom_db,
            clip_rate=args.clip_rate,
            vol_min=args.volume_min,
            vol_max=args.volume_max,
            max_passes=args.max_passes,
            normalise=args.normalise,
            offset_auto=offset_auto)
        try:
            final_volume = calibrator.run()
            offset_seed = calibrator.offset
        except KeyboardInterrupt:
            print("\nAuto-volume calibration interrupted - "
                  "proceeding with the volume found so far.")
            final_volume = calibrator.volume
            offset_seed = calibrator.offset

    results = []  # type: List[FileResult]
    try:
        for n, wav in enumerate(wavs, 1):
            print("\n[%d/%d] %s  (%.1f s)" % (n, len(wavs), os.path.basename(wav), wav_duration(wav)))
            sys.stdout.flush()
            res = FileResult(name=os.path.basename(wav))
            results.append(res)     # appended first: an interrupted file still counts
            run_one_wav(res, wav, args.audio_device, final_volume, args.tail,
                        args.match_window, col, mm_extra, args.no_play,
                        normalise=args.normalise, offset_auto=offset_auto,
                        offset_seed=offset_seed)
            print_file_report(res, dry_run=args.no_play)
            time.sleep(args.pause)
    except KeyboardInterrupt:
        print("\nInterrupted - reporting what has been tested so far.")
    finally:
        col.stop()

    if args.no_play:
        n = sum(len(r.mm_packets) for r in results)
        print("\nDRY RUN finished: multimon-ng decoded %d packet(s) in %d file(s)." % (n, len(results)))
        return 0 if n else 2
    return print_summary(results, final_volume) if results else 2


if __name__ == "__main__":
    sys.exit(main())
