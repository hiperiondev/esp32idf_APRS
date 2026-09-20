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
"""

import argparse
import glob
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

AUTO_VOLUME_BATCH = 50        # re-evaluate the playback volume every N multimon-ng packets
AUTO_VOLUME_MAX_ROUNDS = 10   # default number of tries to search for the optimal volume
AUTO_VOLUME_STEP = 0.15       # relative gain step applied per adjustment (+/- 15%)
AUTO_VOLUME_MIN = 0.05
AUTO_VOLUME_MAX = 8.0
# If the decoded-percentage does not move by at least this much between two
# consecutive evaluations, the volume is considered to have converged.
AUTO_VOLUME_STABLE_DELTA_PCT = 1.0

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
    duration: float = 0.0


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

    def __init__(self) -> None:
        self._hdr = None  # type: Optional[re.Match]

    def feed_line(self, line: str) -> Optional[Packet]:
        line = line.rstrip("\r\n")
        m = MM_HDR_RE.match(line)
        if m:
            self._hdr = m
            return None
        if self._hdr is None:
            return None  # banner or noise
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
        self.ser = serial.Serial(
            port=port, baudrate=baud,
            bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE, timeout=0.2,
        )
        # Opening the port toggles DTR/RTS on most ESP32 dev boards, which
        # resets the chip. Keep both de-asserted, then give the firmware time
        # to come up (done by the caller via wait_ready()).
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


# --------------------------------------------------------------------------
# Audio: play to the sound card AND feed multimon-ng at the same time
# --------------------------------------------------------------------------


def wav_duration(path: str) -> float:
    import wave
    try:
        with wave.open(path, "rb") as w:
            return w.getnframes() / float(w.getframerate())
    except Exception:
        return 0.0


def run_one_wav(res: FileResult, wav: str, audio_device: Optional[str],
                volume: float, tail: float, window: float,
                collector: SerialCollector, mm_extra: List[str],
                mute_local: bool,
                stop_at_mm_packets: Optional[int] = None) -> bool:
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
    # ESP32 lines are attributed to this file by the moment they arrive: the
    # window opens right now, before playback starts, so a late frame from
    # the previous file (its window already closed) cannot leak in here.
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
    play_cmd = ["play", "-q", "-V0", wav, "-c", "2", "remix", "1", "1",
                "vol", str(volume)]

    sox_raw = ["sox", "-q", "-V0", wav, "-t", "raw", "-r", str(MM_RATE),
               "-e", "signed", "-b", "16", "-c", "1", "-"]
    mm_cmd = ["multimon-ng", "-t", "raw", "-a", "AFSK1200", "-q"] + mm_extra + ["-"]

    player = None  # type: Optional[subprocess.Popen]
    sox_p = None   # type: Optional[subprocess.Popen]
    mm_p = None    # type: Optional[subprocess.Popen]
    gt = None      # type: Optional[threading.Thread]
    done = threading.Event()
    duration = wav_duration(wav)
    matcher = LiveMatcher(window)
    interrupted = False
    ingested = [0]   # how many ESP32 items of this file the matcher has seen
    collector.file_t0 = t0

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
        if (stop_at_mm_packets is not None and not target_hit[0] and
                len(res.mm_packets) + len(res.extra) >= stop_at_mm_packets):
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
            res.extra.append(ep)
            say("%06d [multimon  --:--.-] NOT DECODED" % n)
            say("       [esp32 only      %s] %s" % (mmss(t_esp - t0), ep.raw))
            check_stop()
            return
        say("%06d [multimon %s] %s" % (n, mmss(t_mm - t0), mp.raw))
        if kind == "ok":
            res.ok += 1
            say("    OK [esp32    %s] %s" % (mmss(t_esp - t0), ep.raw))
        elif kind == "mismatch":
            res.mismatch.append((mp, ep))
            say("       [esp32    %s] %s" % (mmss(t_esp - t0), ep.raw))
            say("      ! DECODED BUT DIFFERENT")
        else:
            res.missing.append(mp)
            say("       [esp32     --:--.-] NOT DECODED")

    def feed_and_step(now: float, final: bool = False) -> None:
        items = collector.items_between(t_window_start, float("inf"))
        for t, p in items[ingested[0]:]:
            matcher.add_esp(t, p)
        ingested[0] = len(items)
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

        # Reader thread: takes every multimon-ng packet as it appears.
        parser = MultimonParser()

        def read_mm() -> None:
            assert mm_p is not None and mm_p.stdout is not None
            for raw in iter(mm_p.stdout.readline, b""):
                pkt = parser.feed_line(raw.decode("latin-1"))
                if pkt is None:
                    continue
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
        if not mute_local:
            player = subprocess.Popen(play_cmd, env=env,
                                      stdout=subprocess.DEVNULL,
                                      stderr=subprocess.PIPE)
        pt.start()

        if player is not None:
            player.wait()
            if player.returncode not in (0, None) and not target_hit[0]:
                err = (player.stderr.read() if player.stderr else b"").decode("latin-1", "replace")
                sys.stderr.write("\n[audio] player failed (rc=%s): %s\n" %
                                 (player.returncode, err.strip()))
        pt.join()
        sox_p.wait()
        mm_p.wait(timeout=60)
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
                res.mm_packets = [p for p in res.mm_packets
                                  if not any(p is d for d in dropped)]
            else:
                feed_and_step(time.monotonic(), final=True)
            flush_resolved()
            res.esp_packets = list(matcher.esp_seen)
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

    def __init__(self, window: float) -> None:
        self.window = window
        self._lock = threading.Lock()
        self._mm = []       # type: list   # [idx, t, Packet], unresolved
        self._esp = []      # type: list   # [t, Packet, used], unclaimed
        self._n_mm = 0
        self.esp_seen = []  # type: List[Packet]

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
            # Pass 1 - exact content matches, closest in time first, so that
            # when the same packet was sent twice in a row the ESP32 packet
            # goes to the transmission it is nearest to.
            cands = []
            for mi, (idx, tm, mp) in enumerate(self._mm):
                for ei, (te, ep, used) in enumerate(self._esp):
                    if used or abs(te - tm) > w:
                        continue
                    if ep.key_header() == mp.key_header() and ep.info == mp.info:
                        cands.append((abs(te - tm), mi, ei))
            cands.sort()
            mm_done = set()
            for _, mi, ei in cands:
                if mi in mm_done or self._esp[ei][2]:
                    continue
                idx, tm, mp = self._mm[mi]
                te, ep, _u = self._esp[ei]
                self._esp[ei][2] = True
                mm_done.add(mi)
                events.append(("ok", idx, tm, mp, te, ep))

            # Pass 2 - packets whose deadline passed: same-header/different
            # payload is a mismatch, otherwise the packet is missing.
            for mi, (idx, tm, mp) in enumerate(self._mm):
                if mi in mm_done:
                    continue
                if not final and now < tm + w:
                    continue
                best = None
                for ei, (te, ep, used) in enumerate(self._esp):
                    if used or abs(te - tm) > w:
                        continue
                    if ep.key_header() == mp.key_header():
                        if best is None or abs(te - tm) < abs(self._esp[best][0] - tm):
                            best = ei
                mm_done.add(mi)
                if best is None:
                    events.append(("missing", idx, tm, mp, None, None))
                else:
                    te, ep, _u = self._esp[best]
                    self._esp[best][2] = True
                    events.append(("mismatch", idx, tm, mp, te, ep))

            self._mm = [m for mi, m in enumerate(self._mm) if mi not in mm_done]

            # ESP32 packets nobody can claim any more (their window closed
            # with no multimon-ng packet, and none is still pending).
            keep = []
            for te, ep, used in self._esp:
                if used:
                    continue
                if final or now > te + w:
                    events.append(("extra", None, None, None, te, ep))
                else:
                    keep.append([te, ep, used])
            self._esp = keep
        return events


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
    say("  -> OK: %d   DIFFERENT: %d   NOT DECODED: %d   EXTRA(esp only): %d" %
        (res.ok, len(res.mismatch), len(res.missing), len(res.extra)))
    for mp, ep in res.mismatch:
        say("    ! DIFFERENT")
        say("        multimon: %s" % mp.raw)
        say("        esp32   : %s" % ep.raw)
    for mp in res.missing:
        say("    ! NOT DECODED by ESP32: %s" % mp.raw)


def print_summary(results: List[FileResult], volume: Optional[float] = None) -> int:
    total = sum(len(r.mm_packets) for r in results)
    ok = sum(r.ok for r in results)
    mism = sum(len(r.mismatch) for r in results)
    miss = sum(len(r.missing) for r in results)
    extra = sum(len(r.extra) for r in results)
    esp_total = sum(len(r.esp_packets) for r in results)

    bar = "=" * 72
    print("\n" + bar)
    print("SUMMARY")
    print(bar)
    print("  %-34s %6s %6s %6s %6s %6s" % ("file", "mm", "ok", "diff", "n/dec", "extra"))
    for r in results:
        print("  %-34s %6d %6d %6d %6d %6d" %
              (r.name[:34], len(r.mm_packets), r.ok, len(r.mismatch),
               len(r.missing), len(r.extra)))
    print("  " + "-" * 70)
    if volume is not None:
        print("  Playback volume used for this test : %.3f" % volume)
    print("  Files tested                      : %d" % len(results))
    print("  Total packets (multimon-ng)       : %d" % total)
    print("  Packets seen by ESP32             : %d" % esp_total)
    print("  Decoded correctly                 : %d  (%.2f%%)" % (ok, pct(ok, total)))
    print("  Decoded with different content    : %d  (%.2f%%)" % (mism, pct(mism, total)))
    print("  Missing (not decoded)             : %d  (%.2f%%)" % (miss, pct(miss, total)))
    print("  Extra (ESP32 only, not an error)  : %d" % extra)
    print(bar)
    if total == 0:
        print("RESULT: no packets were decoded by multimon-ng - nothing to compare.")
        return 2
    return 0 if (mism == 0 and miss == 0) else 1


# --------------------------------------------------------------------------
# Startup helpers
# --------------------------------------------------------------------------


def check_tools() -> None:
    missing = [t for t in ("multimon-ng", "sox", "play") if shutil.which(t) is None]
    if missing:
        sys.stderr.write("Missing required program(s): %s\n" % ", ".join(missing))
        sys.stderr.write("  Debian/Ubuntu: sudo apt install multimon-ng sox libsox-fmt-all\n")
        sys.exit(2)


def find_wavs(directory: str) -> List[str]:
    files = []
    for pattern in ("*.wav", "*.WAV", "*.Wav"):
        files.extend(glob.glob(os.path.join(directory, pattern)))
    return sorted(set(files), key=lambda s: s.lower())


class AutoVolumeCalibrator:
    """Searches for the optimal `play` volume before the real, reported test
    run, trying up to `max_rounds` (10 by default, configurable with
    --auto_volume_max_rounds) different volumes.

    Algorithm:

      * Each try replays the WAV set FROM THE FIRST FILE (looping over the
        set again within the same try if it runs out before the batch
        target is reached), and counts every multimon-ng packet, until it
        has seen AUTO_VOLUME_BATCH (50, configurable with
        --auto_volume_batch) of them. A try is never a continuation of the
        previous one's playback position - every try starts over.
      * Once a try reaches its packet target it is evaluated, and the
        previous volume together with the new volume chosen for the next
        try (which will again start from the first wav file) is printed:
          - if the firmware logged "afsk: RX audio is over-range" during the
            try, the level is too HIGH: the volume is lowered for the next
            try;
          - otherwise the try's decoded percentage (ESP32 packets that
            matched a multimon-ng one, out of multimon-ng's total) is used:
            a low percentage means the volume is probably too LOW, so it is
            raised for the next try.
      * The best-performing volume (highest decoded percentage among tries
        that did not clip) is remembered across the whole search.
      * The search stops nudging the volume - without necessarily stopping
        before `max_rounds` tries - either once decoding reaches 100%, or
        once the decoded percentage stops changing between two consecutive
        tries (moves by less than AUTO_VOLUME_STABLE_DELTA_PCT): at that
        point trying further volumes is not expected to improve things, so
        the remaining budget is not spent.
      * Whichever volume scored the best decoded percentage across every try
        actually run is the one used for the real test, which is then
        started fresh from the first file.

    Runs with its own throwaway FileResult objects; nothing here is counted
    in the final report other than the resulting volume.
    """

    def __init__(self, wavs: List[str], audio_device: Optional[str],
                 tail: float, window: float, collector: SerialCollector,
                 mm_extra: List[str], start_volume: float,
                 batch_size: int = AUTO_VOLUME_BATCH,
                 max_rounds: int = AUTO_VOLUME_MAX_ROUNDS) -> None:
        self.wavs = wavs
        self.audio_device = audio_device
        self.tail = tail
        self.window = window
        self.collector = collector
        self.mm_extra = mm_extra
        self.volume = start_volume
        self.batch_size = batch_size
        self.max_rounds = max_rounds

    def _run_batch_until(self, wav_iter, current: FileResult,
                          target: int, volume: float) -> Tuple[FileResult, object]:
        """Keep playing files (advancing `wav_iter`) into `current` until it
        holds at least `target` packets - counting BOTH multimon-ng packets
        AND "extra" ones (packets the ESP32 decoded that multimon-ng missed
        entirely) - or the calibration set runs dry. If the WAV set is
        exhausted before the target is reached, it loops back to the first
        file so the current try can still gather the full batch. The caller
        starts a NEW `wav_iter` (from the first file) at the beginning of
        every try - this method does not decide when a try starts. `volume`
        is fixed for the whole call: every wav played within this
        round/batch uses this exact value, never `self.volume`, so a volume
        change is never applied mid-round - it can only take effect on the
        next round. Returns the possibly-replaced FileResult and the
        iterator to resume from within this try."""
        while len(current.mm_packets) + len(current.extra) < target:
            try:
                wav = next(wav_iter)
            except StopIteration:
                if not self.wavs:
                    break
                wav_iter = iter(self.wavs)
                try:
                    wav = next(wav_iter)
                except StopIteration:
                    break
            res = FileResult(name=os.path.basename(wav))
            # Only ask for as many packets as still missing from this batch,
            # so a single WAV that by itself holds more than that (e.g. one
            # long multi-packet recording) gets cut short mid-file the
            # instant the batch target is reached, instead of playing all
            # the way to its own end and blowing past `target`.
            remaining = target - (len(current.mm_packets) + len(current.extra))
            run_one_wav(res, wav, self.audio_device, volume, self.tail,
                        self.window, self.collector, self.mm_extra,
                        mute_local=False, stop_at_mm_packets=remaining)
            # Merge into the running batch instead of replacing it, so a
            # batch can span more than one file.
            current.mm_packets.extend(res.mm_packets)
            current.esp_packets.extend(res.esp_packets)
            current.ok += res.ok
            current.mismatch.extend(res.mismatch)
            current.missing.extend(res.missing)
            current.extra.extend(res.extra)
        return current, wav_iter

    def run(self) -> float:
        if not self.wavs:
            return self.volume

        say("\n" + "=" * 72)
        say("AUTO-VOLUME CALIBRATION")
        say("=" * 72)
        say("  Starting volume: %.3f  (batches of %d packets [multimon-ng + "
              "ESP32-only], up to %d tries)" %
              (self.volume, self.batch_size, self.max_rounds))

        prev_pct = None  # type: Optional[float]

        # Best volume seen so far (highest decoded%% among rounds that did
        # NOT clip). This is what gets used for the real test at the end,
        # even if the last round tried was worse - the search always spends
        # its full budget of `max_rounds` tries looking for the optimum,
        # it does not just stop at the first working volume.
        best_volume = self.volume
        best_pct = -1.0

        for round_no in range(1, self.max_rounds + 1):
            # Every try re-plays the WAV set from the very first file, so
            # each volume is judged on exactly the same material as every
            # other try - never a continuation of where the previous try's
            # playback happened to stop.
            wav_iter = iter(self.wavs)
            batch = FileResult(name="<calibration batch %d>" % round_no)
            overrange_before, _ = self.collector.snapshot_overrange()
            volume_used = self.volume

            batch, wav_iter = self._run_batch_until(wav_iter, batch, self.batch_size, volume_used)

            mm_total = len(batch.mm_packets)
            if mm_total == 0:
                say("  [try %2d/%d] no packets decoded by multimon-ng at all "
                      "(check the WAV files / audio routing) - stopping search "
                      "at volume %.3f" % (round_no, self.max_rounds, self.volume))
                break

            decoded_pct = pct(batch.ok + len(batch.mismatch), mm_total)
            overrange_hits = self.collector.overrange_since(overrange_before)

            say("  [try %2d/%d] volume=%.3f  multimon=%d  extra=%d  esp_ok=%d  "
                  "decoded=%.1f%%  over-range warnings=%d" %
                  (round_no, self.max_rounds, volume_used, mm_total, len(batch.extra),
                   batch.ok, decoded_pct, overrange_hits))

            if overrange_hits == 0 and decoded_pct > best_pct:
                best_pct = decoded_pct
                best_volume = volume_used
                say("      new best so far: volume=%.3f (%.1f%% decoded)" %
                      (best_volume, best_pct))

            if overrange_hits > 0:
                # Too loud: the ADC is clipping. This round can't be the
                # optimum (clipped audio, unreliable percentage) - turn it
                # down and keep trying with the remaining budget.
                new_volume = max(AUTO_VOLUME_MIN, self.volume * (1.0 - AUTO_VOLUME_STEP))
                say("      over-range detected: volume %.3f -> %.3f for the "
                      "next try (batch %d/%d, restarting from the first wav)" %
                      (volume_used, new_volume, round_no + 1, self.max_rounds))
                self.volume = new_volume
                prev_pct = decoded_pct
                continue

            # Unlike before, reaching 100% or the percentage barely moving
            # between two tries no longer stops the search early: the full
            # `max_rounds` budget is always spent trying volumes, so a
            # volume further along (more margin, or one that only shows its
            # advantage after this one) is never missed. The best volume
            # seen across every try (tracked above via best_pct/best_volume)
            # is what is kept for the real test at the end.
            if decoded_pct >= 99.5:
                say("      decoded=100%% and no over-range - still trying "
                      "further volumes with the remaining budget")
            elif (prev_pct is not None and
                  abs(decoded_pct - prev_pct) < AUTO_VOLUME_STABLE_DELTA_PCT):
                say("      decoded%% stable (%.1f%% -> %.1f%%, < %.1f%% change) - "
                      "still trying further volumes with the remaining budget" %
                      (prev_pct, decoded_pct, AUTO_VOLUME_STABLE_DELTA_PCT))

            # Not over-range: raise the volume for the next try so the
            # remaining budget keeps searching for an even better volume.
            new_volume = min(AUTO_VOLUME_MAX, self.volume * (1.0 + AUTO_VOLUME_STEP))
            say("      decoded=%.1f%%: volume %.3f -> %.3f for the "
                  "next try (batch %d/%d, restarting from the first wav)" %
                  (decoded_pct, volume_used, new_volume, round_no + 1, self.max_rounds))
            self.volume = new_volume
            prev_pct = decoded_pct
        else:
            say("  Reached the %d-try limit." % self.max_rounds)

        self.volume = best_volume
        say("  Calibration finished after up to %d tries: using volume %.3f "
              "(best decoded rate seen: %.1f%%) for the full test run." %
              (self.max_rounds, self.volume, best_pct if best_pct >= 0 else 0.0))
        say("=" * 72)
        return self.volume


def wait_ready(col: SerialCollector, settle: float) -> None:
    """Opening the port usually resets an ESP32 (DTR/RTS wired to EN/IO0).
    Wait for console traffic and for the boot to complete."""
    print("Waiting %.1f s for the ESP32 to be ready ..." % settle)
    sys.stdout.flush()
    end = time.monotonic() + settle
    while time.monotonic() < end:
        time.sleep(0.2)
    if not col.alive.is_set():
        print("  WARNING: no data received from the serial port yet. The firmware "
              "may be quiet until it hears/sends something; continuing.")
    else:
        print("  serial is alive (%d console line(s) so far)." % col.lines_seen)
    sys.stdout.flush()


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
    ap.add_argument("--no_auto_volume", action="store_true",
                    help="skip the auto-volume calibration pass and use --volume as-is "
                         "for the whole run")
    ap.add_argument("--auto_volume_batch", type=int, default=AUTO_VOLUME_BATCH,
                    help="number of packets to test per volume try during "
                         "auto-volume calibration - counts both multimon-ng "
                         "packets and ESP32-only ones multimon-ng missed "
                         "(default %d)" % AUTO_VOLUME_BATCH)
    ap.add_argument("--auto_volume_max_rounds", type=int, default=AUTO_VOLUME_MAX_ROUNDS,
                    help="number of tries used to search for the optimal playback "
                         "volume before the real test run (default %d)" %
                         AUTO_VOLUME_MAX_ROUNDS)
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

    if args.list_audio:
        subprocess.call(["aplay", "-l"])
        return 0

    if args.auto_volume_batch < 1:
        sys.stderr.write("--auto_volume_batch must be >= 1 (got %d)\n" % args.auto_volume_batch)
        return 2
    if args.auto_volume_max_rounds < 1:
        sys.stderr.write("--auto_volume_max_rounds must be >= 1 (got %d)\n" %
                          args.auto_volume_max_rounds)
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
            def snapshot_index(self): return 0
            def since(self, i): return []
            def between(self, a, b): return []
            def items_between(self, a, b): return []
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
    if not args.no_play and not args.no_auto_volume:
        calibrator = AutoVolumeCalibrator(
            wavs, args.audio_device, args.tail, args.match_window, col,
            mm_extra, args.volume,
            batch_size=args.auto_volume_batch,
            max_rounds=args.auto_volume_max_rounds)
        try:
            final_volume = calibrator.run()
        except KeyboardInterrupt:
            print("\nAuto-volume calibration interrupted - "
                  "proceeding with the volume found so far.")
            final_volume = calibrator.volume

    results = []  # type: List[FileResult]
    try:
        for n, wav in enumerate(wavs, 1):
            print("\n[%d/%d] %s  (%.1f s)" % (n, len(wavs), os.path.basename(wav), wav_duration(wav)))
            sys.stdout.flush()
            res = FileResult(name=os.path.basename(wav))
            results.append(res)     # appended first: an interrupted file still counts
            run_one_wav(res, wav, args.audio_device, final_volume, args.tail,
                        args.match_window, col, mm_extra, args.no_play)
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
