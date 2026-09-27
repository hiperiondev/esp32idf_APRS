#!/usr/bin/env python3
"""rx_diag.py - automatic, end-to-end diagnosis of the 1200 Bd receiver.

Answers, from measurements only, the three questions that decide where
decodes are lost:

  Q1  Which demodulator set decodes more of the given recordings, with the
      firmware's own DSP running on the PC (no ADC, no sound card, no CPU
      limit)?                                              -> host phase
  Q2  How far is the firmware's algorithm from Direwolf on exactly the same
      audio?                                               -> host phase
  Q3  Does the device decode what the PC replay of the same audio, at the
      level the device really receives, says it should - and if not, is it
      the CPU (lost samples, DSP load) or the analog/front end? -> device phase

Host phase (needs gcc, sox; Direwolf's `atest` for Q2):
  builds rx_replay/modem_replay.c against the project's modem sources and
  replays every recording for every demodulator set x prefilter length x
  input level, counting decodes and the frames that match Direwolf.

Device phase (needs the ESP32 on the network, the bench of
test_aprs_wavs.py wired as usual, and the web admin password):
  for every device configuration it sets the demodulator set and prefilter
  length through the web admin (the full Radiomodem form is read and posted
  back, exactly as the browser does), runs test_aprs_wavs.py with a fixed
  playback gain, and records the demodulator input of the ESP32 through
  POST /radio/capture while the file plays (one record per 20 ms block: the
  samples the demodulators get, the raw ADC extremes, the receive task's
  time, the gate state and the gain). The capture is replayed on the PC with
  the same firmware DSP code, with and without the receive gate, and compared
  with the frames the device delivered; the original recording is also
  replayed at the level the capture shows, so every device run is compared
  with its own input and with the clean audio at the same level.

Everything is written to a results directory: report.md (the verdict and
every table), report.json (all raw figures), the bench logs and CSVs, and
the decoded frames of every replay.

Examples:
  # PC only
  ./rx_diag.py --wav one2/02_100-Mic-E-Bursts-DE-emphasized.flac.wav

  # PC and device
  ./rx_diag.py --wav one2/02_100-Mic-E-Bursts-DE-emphasized.flac.wav \\
      --esp-host 192.168.4.1 --user admin --password secret \\
      --serial-port /dev/ttyUSB0 --audio-device hw:1,0 --volume 1.334
"""

import argparse
import base64
import datetime
import html.parser
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_DEFAULT = os.path.dirname(HERE)

PRESET_NAMES = {5: "multislice-3pf", 6: "multislice-2pf", 0: "legacy", 1: "single", 2: "div2", 3: "div3", 4: "custom"}

# Thresholds of the verdict.
DSP_OVERLOAD_PERMILLE = 900      # a single block this close to real time can overflow the pool
RAW_CLIP_LOW = 15                # ADC codes counted as over-range, as AFSK_RAW_CLIP_LOW/HIGH
RAW_CLIP_HIGH = 4080
DEVICE_VS_HOST_FLOOR = 0.80      # device decoding below 80 % of the matched replay is a device-side loss
SET_DIFFERENCE_FLOOR = 0.03      # decode-rate differences below 3 points are within run-to-run scatter


# --------------------------------------------------------------------------
# Small helpers
# --------------------------------------------------------------------------

def log(msg):
    print("[rx_diag] " + msg, flush=True)


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, **kw)


def need(tool):
    if shutil.which(tool) is None:
        sys.exit("rx_diag: '%s' not found in PATH" % tool)


PKT = re.compile(r'([A-Z0-9]{1,6}(?:-\d{1,2})?>[A-Z0-9,*\-]+:.*)$')


def norm_packet(line):
    """TNC2 line in a canonical form, so decoders can be compared."""
    m = PKT.search(line.strip())
    if not m:
        return None
    s = m.group(1).rstrip()
    head, _, info = s.partition(':')
    head = head.replace('*', '')
    head = re.sub(r'-0(?=[,>]|$)', '', head)
    info = re.sub(r'(<0x0d>|\r)+$', '', info).rstrip()
    return head + ':' + info


def load_packets(path):
    out = set()
    with open(path, errors='replace') as f:
        for line in f:
            p = norm_packet(line)
            if p:
                out.add(p)
    return out


def file_peak(wav):
    """Peak of a recording, as a fraction of full scale (sox stats)."""
    r = subprocess.run(["sox", wav, "-n", "stats"], capture_output=True, text=True)
    m = re.search(r'Pk lev dB\s+(-?[\d.]+|-inf)', r.stderr)
    if not m or m.group(1) == '-inf':
        return 1.0
    return min(1.0, 10 ** (float(m.group(1)) / 20.0))


# --------------------------------------------------------------------------
# Host phase
# --------------------------------------------------------------------------

class Host:
    def __init__(self, project, out, noise, flat):
        self.project = project
        self.out = out
        self.noise = noise
        self.flat = flat
        self.bin = os.path.join(out, "modem_replay")
        self.raw = {}
        self.ref = {}

    def build(self):
        need("gcc")
        m = os.path.join(self.project, "components", "esp32idf_radioamateur_modem")
        # build.sh compiles rx_replay/modem_replay.c against the modem sources
        # with the firmware's own defines and the host stubs of the component,
        # and copies the decimation table out of afsk.c next to the binary.
        log("building the replay tool against %s" % m)
        run(["sh", os.path.join(HERE, "rx_replay", "build.sh"), self.bin, self.project])

    def prepare(self, wav):
        need("sox")
        base = os.path.splitext(os.path.basename(wav))[0]
        raw = os.path.join(self.out, base + ".raw")
        if not os.path.exists(raw):
            log("converting %s to 76800 Hz mono" % base)
            run(["sox", wav, "-t", "raw", "-e", "signed", "-b", "16", "-c", "1", "-r", "76800", raw, "remix", "1"])
        self.raw[wav] = raw
        if wav not in self.ref:
            self.ref[wav] = None
            if shutil.which("atest"):
                log("Direwolf atest reference for %s" % base)
                path = os.path.join(self.out, base + ".atest.txt")
                with open(path, "w") as f:
                    subprocess.run(["atest", "-B", "1200", wav], stdout=f, stderr=subprocess.STDOUT)
                self.ref[wav] = load_packets(path)
            else:
                log("atest (Direwolf) not found: the host phase reports absolute counts only")
        return base

    def replay_capture(self, cap, preset, taps, gate=True):
        """Replay a /radio/capture stream; frames plus the capture statistics."""
        tag = os.path.splitext(os.path.basename(cap))[0] + (".replay" if gate else ".replay-nogate")
        tnc2 = os.path.join(self.out, tag + ".tnc2")
        errp = os.path.join(self.out, tag + ".log")
        cmd = [self.bin, "--preset", str(preset), "--taps", str(taps), "--flat", str(self.flat), "--capture", cap]
        if not gate:
            cmd.append("--no-gate")
        with open(tnc2, "w") as fo, open(errp, "w") as fe:
            subprocess.run(cmd, stdout=fo, stderr=fe, check=True)
        err = open(errp).read()

        def num(rx, cast=float, group=1):
            m = re.search(rx, err)
            return cast(m.group(group)) if m else None

        return {
            "frames": load_packets(tnc2),
            "records": num(r'capture: (\d+) records', int),
            "missing": num(r'with (\d+) record\(s\) missing', int),
            "clip_blocks": num(r'ADC rails: (\d+)', int),
            "gate_closed_pct": num(r'gate closed: \d+ \(([\d.]+) %\)'),
            "impulsive_pct": num(r'impulsive blocks \(peak > 6x RMS\): \d+ \(([\d.]+) %\)'),
            "blanked_pct": num(r'impulse blanker: \d+ \(([\d.]+) %\)'),
            "busy_mean_us": num(r'mean ([\d.]+) us'),
            "busy_max_us": num(r'max (\d+) us', int),
            "rms_p50_mv": num(r'p50 ([\d.]+) p90'),
            "closed_p50_mv": num(r'gate-closed blocks p50 ([\d.]+)'),
            "closed_p90_mv": num(r'gate-closed blocks p50 [\d.]+ p90 ([\d.]+)'),
        }

    def replay(self, wav, preset, taps, gain):
        base = self.prepare(wav)
        tag = "%s.p%d.t%d.g%d" % (base, preset, taps, int(round(gain)))
        tnc2 = os.path.join(self.out, tag + ".tnc2")
        errp = os.path.join(self.out, tag + ".log")
        with open(self.raw[wav], "rb") as fin, open(tnc2, "w") as fo, open(errp, "w") as fe:
            subprocess.run([self.bin, "--preset", str(preset), "--taps", str(taps), "--gain", str(gain),
                            "--noise", str(self.noise), "--flat", str(self.flat)], stdin=fin, stdout=fo, stderr=fe, check=True)
        err = open(errp).read()
        ours = load_packets(tnc2)
        ref = self.ref.get(wav)
        rails = re.search(r'\(([\d.]+) %\)', err)
        cpu = re.search(r'CPU on this PC: ([\d.]+)', err)
        per = re.search(r'demodulator:([\d ]+)', err)
        return {
            "wav": base, "preset": preset, "set": PRESET_NAMES.get(preset, str(preset)), "taps": taps, "gain": round(gain),
            "frames": len(ours), "ref": len(ref) if ref is not None else None,
            "match": len(ours & ref) if ref is not None else None,
            "rails_pct": float(rails.group(1)) if rails else None,
            "cpu_s_per_s": float(cpu.group(1)) if cpu else None,
            "per_demod": [int(x) for x in per.group(1).split()] if per else [],
        }


# --------------------------------------------------------------------------
# Device phase: web admin
# --------------------------------------------------------------------------

class FormParser(html.parser.HTMLParser):
    """Collects the fields of <form id='radioForm'> the way a browser submits them."""

    def __init__(self):
        super().__init__()
        self.inside = False
        self.fields = []
        self.select = None
        self.selectFirst = None
        self.selectChosen = None

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "form" and a.get("id") == "radioForm":
            self.inside = True
        if not self.inside:
            return
        if tag == "input":
            name = a.get("name")
            if not name or a.get("disabled") is not None:
                return
            t = (a.get("type") or "text").lower()
            if t in ("checkbox", "radio"):
                if "checked" in a:
                    self.fields.append((name, a.get("value", "on")))
            elif t not in ("submit", "button", "reset", "file", "image"):
                self.fields.append((name, a.get("value", "")))
        elif tag == "select":
            self.select = a.get("name")
            self.selectFirst = None
            self.selectChosen = None
        elif tag == "option" and self.select:
            v = a.get("value", "")
            if self.selectFirst is None:
                self.selectFirst = v
            if "selected" in a:
                self.selectChosen = v
        elif tag == "textarea":
            self.fields.append((a.get("name"), ""))

    def handle_endtag(self, tag):
        if tag == "select" and self.select:
            v = self.selectChosen if self.selectChosen is not None else self.selectFirst
            if v is not None:
                self.fields.append((self.select, v))
            self.select = None
        if tag == "form" and self.inside:
            self.inside = False


class Esp:
    def __init__(self, host, user, password):
        self.base = "http://" + host
        tok = base64.b64encode(("%s:%s" % (user, password)).encode()).decode()
        self.headers = {"Authorization": "Basic " + tok, "Origin": self.base, "Referer": self.base + "/radio"}

    def request(self, path, data=None, timeout=15):
        req = urllib.request.Request(self.base + path, data=data, headers=self.headers, method="POST" if data is not None else "GET")
        if data is not None:
            req.add_header("Content-Type", "application/x-www-form-urlencoded")
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.read().decode("utf-8", "replace")

    def wait_up(self, limit_s=120):
        t0 = time.time()
        while time.time() - t0 < limit_s:
            try:
                self.request("/radio", timeout=5)
                return True
            except (urllib.error.URLError, OSError):
                time.sleep(2)
        return False

    def radio_form(self):
        p = FormParser()
        p.feed(self.request("/radio"))
        if not p.fields:
            raise RuntimeError("the Radiomodem form was not found on /radio")
        return p.fields

    def set_radio(self, overrides):
        fields = self.radio_form()
        # None switches a checkbox off: the field is left out of the post, as
        # a browser leaves out an unticked box. A checkbox that is ticked but
        # absent from the page's current state is added with its usual value.
        out = [(k, str(overrides[k]) if k in overrides else v) for k, v in fields if not (k in overrides and overrides[k] is None)]
        for k, v in overrides.items():
            if v is not None and k not in dict(fields):
                out.append((k, str(v)))
        self.request("/radio", data=urllib.parse.urlencode(out).encode())
        now = dict(self.radio_form())
        for k, v in overrides.items():
            if v is None:
                if k in now:
                    raise RuntimeError("field '%s' is still set after switching it off" % k)
            elif str(now.get(k)) != str(v):
                raise RuntimeError("field '%s' reads back %r, expected %r" % (k, now.get(k), v))

    def capture(self, seconds, path, stop):
        """Stream POST /radio/capture into path until stop is set or it ends."""
        req = urllib.request.Request(self.base + "/radio/capture?s=%d" % int(seconds), data=b"", headers=self.headers, method="POST")
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
        n = 0
        with urllib.request.urlopen(req, timeout=60) as r, open(path, "wb") as f:
            while not stop.is_set():
                chunk = r.read(396 * 25)
                if not chunk:
                    break
                f.write(chunk)
                n += len(chunk)
        return n

    def rx_level(self):
        return json.loads(self.request("/radio/level", data=b"", timeout=20))


# --------------------------------------------------------------------------
# Device phase: one run
# --------------------------------------------------------------------------

RATE = re.compile(r'ESP32 correct vs (multimon-ng|Direwolf|union)\s*:\s*(\d+) of (\d+)')


def wav_seconds(wav):
    r = subprocess.run(["sox", "--i", "-D", wav], capture_output=True, text=True)
    try:
        return float(r.stdout.strip())
    except ValueError:
        return 3600.0


def device_run(args, esp, host, wav, preset, taps, out, blank=None):
    base = os.path.splitext(os.path.basename(wav))[0]
    tag = "%s.dev.p%d.t%d%s" % (base, preset, taps, "" if blank is None else (".b1" if blank else ".b0"))
    log("device: %s, set %s, %d taps%s" % (base, PRESET_NAMES.get(preset, preset), taps,
                                         "" if blank is None else (", impulse blanker %s" % ("on" if blank else "off"))))
    overrides = {"rxEqPreset": preset, "rxBpfTaps": taps}
    if blank is not None:
        overrides["rxBlank"] = "on" if blank else None
    for kv in args.set or []:
        k, _, v = kv.partition("=")
        overrides[k] = v
    esp.set_radio(overrides)

    bench = [sys.executable, os.path.join(HERE, "test_aprs_wavs.py"),
             "--wav_dir", os.path.dirname(os.path.abspath(wav)), "--wav_files", os.path.basename(wav),
             "--audio_device", args.audio_device, "--serial_port", args.serial_port,
             "--volume", str(args.volume), "--no_auto_volume", "--reset", "--lang", "en",
             "--report_csv", os.path.join(out, tag + ".csv")]
    if args.match_window:
        bench += ["--match_window", str(args.match_window)]
    bench += args.bench_arg or []

    polls = []
    stop = threading.Event()
    cap_path = os.path.join(out, tag + ".capture.bin")
    baseline = {}
    cap_error = []

    def watcher():
        # The bench resets the chip first; wait for the web server, take the
        # baseline of the loss counters, then either capture the demodulator
        # input for the whole run or poll RX LEVEL.
        time.sleep(15)
        esp.wait_up()
        try:
            baseline.update(esp.rx_level())
        except (urllib.error.URLError, OSError, ValueError):
            pass
        if args.capture:
            try:
                n = esp.capture(wav_seconds(wav) + 600, cap_path, stop)
                if n >= 396:
                    return
                cap_error.append("the capture stream ended with %d bytes" % n)
            except urllib.error.HTTPError as e:
                detail = e.read().decode("utf-8", "replace").strip()
                cap_error.append("HTTP %d: %s" % (e.code, detail or e.reason))
            except (urllib.error.URLError, OSError) as e:
                cap_error.append(str(e))
            log("  capture failed (%s); polling RX LEVEL instead" % cap_error[-1])
        while not stop.wait(args.poll_s):
            try:
                polls.append(esp.rx_level())
            except (urllib.error.URLError, OSError, ValueError):
                pass

    th = threading.Thread(target=watcher, daemon=True)
    th.start()
    logp = os.path.join(out, tag + ".bench.log")
    with open(logp, "w") as lf:
        p = subprocess.Popen(bench, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=HERE)
        for line in p.stdout:
            lf.write(line)
            if args.verbose:
                sys.stdout.write(line)
        p.wait()
    stop.set()
    th.join(timeout=90)
    esp.wait_up()
    final = esp.rx_level()

    text = open(logp).read()
    rates = {m.group(1): (int(m.group(2)), int(m.group(3))) for m in RATE.finditer(text)}
    device_frames = set()
    csvp = os.path.join(out, tag + ".csv")
    if os.path.exists(csvp):
        import csv
        with open(csvp, newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                n = norm_packet(row.get("esp", "") or "")
                if n:
                    device_frames.add(n)

    ok = [q for q in polls if q.get("ok")]
    lost_base = (baseline.get("fifo_drops", 0) or 0) + (baseline.get("pool_ovf", 0) or 0) if baseline else None
    lost_run = (((final.get("fifo_drops", 0) or 0) + (final.get("pool_ovf", 0) or 0)) - lost_base) if lost_base is not None else None
    res = {
        "wav": base, "preset": preset, "set": PRESET_NAMES.get(preset, str(preset)), "taps": taps, "blank": blank,
        "bench_exit": p.returncode,
        "vs_direwolf": rates.get("Direwolf"), "vs_multimon": rates.get("multimon-ng"),
        "device_frames": len(device_frames),
        "polls": len(ok),
        "clip_polls": sum(1 for q in ok if q["raw_min"] <= RAW_CLIP_LOW or q["raw_max"] >= RAW_CLIP_HIGH),
        "lost_during_run": lost_run,
        "halfswing_counts": max(((q["raw_max"] - q["raw_min"]) / 2.0 for q in ok), default=None),
        "tones_peak_mv": max((q.get("band_peak_mVrms", 0) for q in ok), default=None),
        "wide_peak_mv": max((q.get("peak_mVrms", 0) for q in ok), default=None),
        "dsp_peak_permille": max((q.get("dsp_peak", 0) for q in ok), default=None),
        "dsp_mean_permille": (sum(q.get("dsp_mean", 0) for q in ok) / len(ok)) if ok else None,
        "final": final,
        "capture": None,
        "capture_error": cap_error[0] if cap_error else None,
        "bench_tail": text.splitlines()[-15:] if not rates else None,
    }

    # The capture: replay exactly what the demodulators were given, with and
    # without the receive gate, and compare with what the device delivered.
    if args.capture and os.path.exists(cap_path) and os.path.getsize(cap_path) >= 396:
        rc = host.replay_capture(cap_path, preset, taps, gate=True)
        rn = host.replay_capture(cap_path, preset, taps, gate=False)
        ref = host.ref.get(wav)
        res["capture"] = {k: v for k, v in rc.items() if k != "frames"}
        res["capture"].update({
            "replay_frames": len(rc["frames"]),
            "replay_nogate_frames": len(rn["frames"]),
            "replay_match": len(rc["frames"] & ref) if ref is not None else None,
            "replay_nogate_match": len(rn["frames"] & ref) if ref is not None else None,
            "device_match": len(device_frames & ref) if ref is not None else None,
            "device_in_replay": len(device_frames & rc["frames"]),
            "replay_not_device": len(rc["frames"] - device_frames),
            "ref": len(ref) if ref is not None else None,
        })
        # Raw swing from the capture itself: the 99.9th percentile of the
        # per-block half-swing, so a single pop does not set it.
        halves = []
        with open(cap_path, "rb") as f:
            while True:
                rec = f.read(396)
                if len(rec) < 396:
                    break
                lo, hi = int.from_bytes(rec[4:6], "little", signed=True), int.from_bytes(rec[6:8], "little", signed=True)
                halves.append((hi - lo) / 2.0)
        if halves:
            halves.sort()
            res["halfswing_counts"] = halves[int(0.999 * (len(halves) - 1))]
        res["clip_polls"] = rc["clip_blocks"]
        res["polls"] = rc["records"]
        log("  capture: %s records, %s missing; replay %d frames (%s match), without gate %d; device %d frames, %d of them in the replay"
            % (rc["records"], rc["missing"], len(rc["frames"]), res["capture"]["replay_match"], len(rn["frames"]), len(device_frames),
               res["capture"]["device_in_replay"]))

    if not rates.get("Direwolf"):
        log("  WARNING: no 'ESP32 correct vs Direwolf' line in the bench output (is Direwolf installed?)")
    return res


# --------------------------------------------------------------------------
# Verdict and report
# --------------------------------------------------------------------------

def rate(k_n):
    if not k_n or not k_n[1]:
        return None
    return k_n[0] / float(k_n[1])


def verdict(host_rows, dev_rows, matched):
    lines = []

    # Q1 / Q2 from the host rows at the reference level.
    def best(rows, preset, taps):
        c = [r for r in rows if r["preset"] == preset and r["taps"] == taps and r["match"] is not None]
        return max(c, key=lambda r: r["match"]) if c else None

    presets = sorted(set(r["preset"] for r in host_rows))
    for wav in sorted(set(r["wav"] for r in host_rows)):
        rows = [r for r in host_rows if r["wav"] == wav]
        lines.append("### %s" % wav)
        refn = next((r["ref"] for r in rows if r["ref"]), None)
        if refn:
            for p in presets:
                b = best(rows, p, 31)
                if b:
                    lines.append("- PC replay, %s, 31 taps: best %d of %d Direwolf frames (%.1f %%) at gain %d."
                                 % (b["set"], b["match"], refn, 100.0 * b["match"] / refn, b["gain"]))
            if len(presets) >= 2:
                a, b = best(rows, 5, 31), best(rows, 6, 31)
                if a and b:
                    d = (a["match"] - b["match"]) / float(refn)
                    if abs(d) < SET_DIFFERENCE_FLOOR:
                        lines.append("- **Q1**: the two multi-slicer sets decode the same on this audio (difference %.1f points)." % (100 * d))
                    else:
                        lines.append("- **Q1**: the %s set decodes %.1f points %s than the %s set on this audio."
                                     % (a["set"], abs(100 * d), "more" if d > 0 else "less", b["set"]))
            top = max((r for r in rows if r["match"] is not None), key=lambda r: r["match"], default=None)
            if top:
                share = top["match"] / float(refn)
                lines.append("- **Q2**: the algorithm's best case on this audio is %.1f %% of Direwolf (%s, %d taps, gain %d)%s"
                             % (100.0 * share, top["set"], top["taps"], top["gain"],
                                "." if share >= 0.97 else "; the gap to Direwolf is in the demodulator itself, since this replay "
                                "involves no ADC hardware, sound card or CPU limit."))
        else:
            lines.append("- No Direwolf reference (atest missing): Q1/Q2 rest on absolute frame counts only.")

    # Q3 from the device rows.
    for d in dev_rows:
        lines.append("### Device: %s, %s, %d taps%s" % (d["wav"], d["set"], d["taps"],
                                                     "" if d.get("blank") is None else (", impulse blanker %s" % ("on" if d["blank"] else "off"))))
        if d.get("bench_tail"):
            lines.append("- **The bench produced no results** (exit code %s), so there is nothing to compare. Its last lines:" % d.get("bench_exit"))
            lines.append("")
            lines.append("```")
            lines.extend(d["bench_tail"])
            lines.append("```")
            continue
        dv = rate(d["vs_direwolf"])
        fin = d["final"] or {}
        lost_run = d.get("lost_during_run")
        dpeak = d.get("dsp_peak_permille")
        clips = d.get("clip_polls") or 0
        cap = d.get("capture")
        m = matched.get((d["wav"], d["preset"], d["taps"], d.get("blank")))
        hv = (m["match"] / float(m["ref"])) if (m and m.get("ref")) else None
        lines.append("- Device: %s of the bench's Direwolf; PC replay of the original recording at the device's level: %s of atest."
                     % ("%.1f %%" % (100 * dv) if dv is not None else "n/a", "%.1f %%" % (100 * hv) if hv is not None else "n/a"))
        lines.append("- Samples lost while the file played: %s (FIFO drops %s, pool overflows %s since boot)."
                     % (fmt(lost_run), fin.get("fifo_drops"), fin.get("pool_ovf")))

        if cap:
            ref = cap.get("ref")
            lines.append("- Capture: %s blocks (%s missing from the stream); blocks at the ADC rails %s; gate closed %.1f %% of blocks; "
                         "impulsive blocks %.3f %%; blocks with glitches repaired by the impulse blanker %s %%; receive task per block "
                         "mean %.0f us, max %s us of 20000."
                         % (cap["records"], cap["missing"], cap["clip_blocks"], cap["gate_closed_pct"] or 0, cap["impulsive_pct"] or 0,
                            fmt(cap.get("blanked_pct")), cap["busy_mean_us"] or 0, cap["busy_max_us"]))
            lines.append("- Demodulator input level (block RMS, mV): median %s; with the gate closed (between packets) median %s, p90 %s."
                         % (fmt(cap["rms_p50_mv"]), fmt(cap["closed_p50_mv"]), fmt(cap["closed_p90_mv"])))
            lines.append("- Frames: device %d; PC replay of the capture %d (without the gate %d); device frames also in the replay %d; "
                         "replay frames the device did not deliver %d%s."
                         % (d["device_frames"], cap["replay_frames"], cap["replay_nogate_frames"], cap["device_in_replay"],
                            cap["replay_not_device"],
                            ("; vs atest: replay %s, device %s of %s" % (cap["replay_match"], cap["device_match"], ref)) if ref else ""))
            dev_n, rep_n, nog_n = d["device_frames"], cap["replay_frames"], cap["replay_nogate_frames"]
            if (cap["clip_blocks"] or 0) > 0:
                lines.append("- **Q3 verdict: input clipping** in %s blocks. Lower the playback or receiver level until none reach the rails." % cap["clip_blocks"])
            if (lost_run or 0) > 0 or (cap["busy_max_us"] or 0) >= 18000:
                lines.append("- **Q3 verdict: CPU / real time.** Samples were lost or a block took nearly its whole 20 ms while the file played.")
            elif rep_n and dev_n < DEVICE_VS_HOST_FLOOR * rep_n:
                lines.append("- **Q3 verdict: the device loses frames that were in its own input.** The PC decodes the captured "
                             "demodulator input far better than the device did in real time, with the same firmware DSP code: the loss "
                             "is between the demodulator input and the delivered frame on the device (task timing, frame queue, "
                             "delivery), not in the audio.")
            elif hv is not None and ref and rep_n is not None and (cap["replay_match"] or 0) < DEVICE_VS_HOST_FLOOR * hv * ref:
                lines.append("- **Q3 verdict: the audio reaching the demodulators is degraded.** The device and the PC replay of its "
                             "captured input agree, and both decode far less than the same recording replayed cleanly: the damage is "
                             "in front of the demodulators - ADC noise and impulses (see the gate-closed level and the impulsive "
                             "blocks), level, or the sound card.")
            else:
                lines.append("- **Q3 verdict: device matches the PC replay.** Nothing is lost on the device side.")
            if rep_n and nog_n > rep_n * 1.05:
                lines.append("- The receive gate costs frames: the capture decodes %d frames without it against %d with it. "
                             "Set *Receive gate* to 0." % (nog_n, rep_n))
            continue

        # Without a capture: RX LEVEL polls only.
        if d.get("capture_error"):
            lines.append("- Capture failed: %s. RX LEVEL was polled instead." % d["capture_error"])
        lines.append("- Input at the ADC rails in %d of %d RX LEVEL polls; DSP load peak %s."
                     % (clips, d.get("polls", 0), "%.1f %%" % (dpeak / 10.0) if dpeak is not None else "n/a"))
        if clips > 0:
            lines.append("- **Q3 verdict: input clipping.** Lower the playback or receiver level until no poll reaches the rails.")
        elif (lost_run or 0) > 0 or (dpeak is not None and dpeak >= DSP_OVERLOAD_PERMILLE):
            lines.append("- **Q3 verdict: CPU / real time.** Samples were lost or a block took nearly its whole audio duration.")
        elif dv is not None and hv is not None and dv < DEVICE_VS_HOST_FLOOR * hv:
            lines.append("- **Q3 verdict: front end / analog.** Nothing was lost, yet the device decodes well below the clean replay. "
                         "Run with the capture (the default) to see why.")
        elif dv is not None and hv is not None:
            lines.append("- **Q3 verdict: device matches the PC replay.**")
        else:
            lines.append("- Q3: incomplete data.")
    return lines


def md_table(rows, cols):
    out = ["| " + " | ".join(c for c, _ in cols) + " |", "|" + "---|" * len(cols)]
    for r in rows:
        out.append("| " + " | ".join(f(r) for _, f in cols) + " |")
    return out


def fmt(v, f="%s"):
    return "-" if v is None else (f % v)


def write_report(out, args, host_rows, dev_rows, matched):
    data = {"when": datetime.datetime.now().isoformat(timespec="seconds"), "args": vars(args),
            "host": host_rows, "device": dev_rows,
            "matched": [dict(v, key=list(k)) for k, v in matched.items()]}
    with open(os.path.join(out, "report.json"), "w") as f:
        json.dump(data, f, indent=2, default=str)

    L = ["# rx_diag report", "", "Generated %s." % data["when"], "", "## Verdict", ""]
    L += verdict(host_rows, dev_rows, matched)
    if host_rows:
        L += ["", "## PC replay", ""]
        L += md_table(sorted(host_rows, key=lambda r: (r["wav"], r["preset"], -r["taps"], r["gain"])), [
            ("recording", lambda r: r["wav"]), ("set", lambda r: r["set"]), ("taps", lambda r: str(r["taps"])),
            ("gain", lambda r: str(r["gain"])), ("frames", lambda r: str(r["frames"])),
            ("Direwolf", lambda r: fmt(r["ref"])), ("match", lambda r: fmt(r["match"])),
            ("% of Direwolf", lambda r: fmt(100.0 * r["match"] / r["ref"] if r["ref"] else None, "%.1f")),
            ("rails %", lambda r: fmt(r["rails_pct"], "%.4f")), ("first deliveries per demod", lambda r: " ".join(map(str, r["per_demod"]))),
            ("PC CPU s/s", lambda r: fmt(r["cpu_s_per_s"], "%.4f"))])
    if dev_rows:
        L += ["", "## Device runs", ""]
        L += md_table(dev_rows, [
            ("recording", lambda r: r["wav"]), ("set", lambda r: r["set"]), ("taps", lambda r: str(r["taps"])),
            ("vs Direwolf", lambda r: "%d/%d" % tuple(r["vs_direwolf"]) if r["vs_direwolf"] else "-"),
            ("vs multimon", lambda r: "%d/%d" % tuple(r["vs_multimon"]) if r["vs_multimon"] else "-"),
            ("ADC half-swing", lambda r: fmt(r["halfswing_counts"], "%.0f")),
            ("at rails (blocks or polls)", lambda r: "%s/%s" % (r.get("clip_polls"), r.get("polls"))),
            ("lost in run", lambda r: fmt(r.get("lost_during_run"))),
            ("tones peak mV", lambda r: fmt(r["tones_peak_mv"])), ("wideband peak mV", lambda r: fmt(r["wide_peak_mv"])),
            ("DSP mean %", lambda r: fmt(r["dsp_mean_permille"] / 10.0 if r["dsp_mean_permille"] is not None else None, "%.1f")),
            ("DSP peak %", lambda r: fmt(r["dsp_peak_permille"] / 10.0 if r["dsp_peak_permille"] is not None else None, "%.1f")),
            ("DSP max %", lambda r: fmt((r["final"] or {}).get("dsp_max", None) / 10.0 if (r["final"] or {}).get("dsp_max") is not None else None, "%.1f")),
            ("FIFO drops", lambda r: fmt((r["final"] or {}).get("fifo_drops"))), ("pool ovf", lambda r: fmt((r["final"] or {}).get("pool_ovf"))),
            ("device frames", lambda r: str(r.get("device_frames", "-"))),
            ("capture replay", lambda r: fmt((r.get("capture") or {}).get("replay_frames"))),
            ("replay no gate", lambda r: fmt((r.get("capture") or {}).get("replay_nogate_frames"))),
            ("gate closed %", lambda r: fmt((r.get("capture") or {}).get("gate_closed_pct"))),
            ("impulsive %", lambda r: fmt((r.get("capture") or {}).get("impulsive_pct"))),
            ("idle p90 mV", lambda r: fmt((r.get("capture") or {}).get("closed_p90_mv"))),
            ("block max us", lambda r: fmt((r.get("capture") or {}).get("busy_max_us"))),
            ("decoded per demod", lambda r: "/".join(map(str, (r["final"] or {}).get("decoded", [])))),
            ("only per demod", lambda r: "/".join(map(str, (r["final"] or {}).get("unique", []))))])
        if matched:
            L += ["", "## PC replay at the device's own level", ""]
            L += md_table([dict(v, k=k) for k, v in matched.items()], [
                ("recording", lambda r: r["wav"]), ("set", lambda r: r["set"]), ("taps", lambda r: str(r["taps"])),
                ("gain (from device swing)", lambda r: str(r["gain"])), ("match", lambda r: fmt(r["match"])),
                ("% of Direwolf", lambda r: fmt(100.0 * r["match"] / r["ref"] if r["ref"] else None, "%.1f"))])
    L += ["", "## Files", "", "- `report.json`: every figure above, raw.",
          "- `*.bench.log`, `*.csv`: the bench output and its per-frame rows for each device run.",
          "- `*.tnc2`, `*.log`: the frames and summary of each PC replay; `*.atest.txt`: Direwolf's decodes."]
    with open(os.path.join(out, "report.md"), "w") as f:
        f.write("\n".join(L) + "\n")
    print("\n".join(L[:L.index("## PC replay")] if "## PC replay" in L else L))


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def ints(s):
    return [int(x) for x in s.split(",") if x.strip()]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--wav", nargs="+", required=True, help="recordings to test")
    ap.add_argument("--project", default=PROJECT_DEFAULT, help="project root (default: the parent of this directory)")
    ap.add_argument("--out", default=None, help="results directory (default: rx_diag_<date-time>)")
    ap.add_argument("--no-host", action="store_true", help="skip the PC replay phase")
    ap.add_argument("--presets", default="5,6", help="demodulator sets (modem_rx_eq_preset_t values), default 5,6")
    ap.add_argument("--taps", default="31,21", help="prefilter lengths for the PC phase, default 31,21")
    ap.add_argument("--gains", default="300,600,1200,2000", help="PC replay input levels, ADC counts per full-scale sample")
    ap.add_argument("--noise", type=float, default=1.2, help="converter noise added in the replay, RMS ADC counts (default 1.2, ~1 mV)")
    ap.add_argument("--flat", type=int, default=0, help="1 = flat/discriminator demodulator set in the replay (match the device)")
    ap.add_argument("--esp-host", help="ESP32 address; enables the device phase")
    ap.add_argument("--user", default="admin", help="web admin user (default admin)")
    ap.add_argument("--password", help="web admin password")
    ap.add_argument("--serial-port", help="serial port for test_aprs_wavs.py")
    ap.add_argument("--audio-device", help="audio device for test_aprs_wavs.py")
    ap.add_argument("--volume", type=float, default=1.0, help="fixed bench playback gain (default 1.0)")
    ap.add_argument("--match-window", type=float, default=None, help="passed to the bench (1.5 for burst recordings)")
    ap.add_argument("--device-configs", default="6:31,5:31,5:21",
                    help="device runs as preset:taps, or preset:taps:blank with blank 1/0 to switch the impulse blanker "
                         "(default 6:31,5:31,5:21, blanker left as it is)")
    ap.add_argument("--set", action="append", metavar="FIELD=VALUE",
                    help="extra Radiomodem form field for every device run, e.g. rxGateMv=0 (repeatable)")
    ap.add_argument("--poll-s", type=float, default=20.0, help="RX LEVEL polling interval when --no-capture is given (default 20 s)")
    ap.add_argument("--no-capture", dest="capture", action="store_false",
                    help="poll RX LEVEL during device runs instead of capturing the demodulator input (firmware without /radio/capture)")
    ap.add_argument("--bench-arg", action="append", help="extra argument passed to test_aprs_wavs.py (repeatable)")
    ap.add_argument("--verbose", action="store_true", help="echo the bench output")
    args = ap.parse_args()

    for w in args.wav:
        if not os.path.exists(w):
            sys.exit("rx_diag: no such recording: %s" % w)
    out = args.out or os.path.join(os.getcwd(), "rx_diag_" + datetime.datetime.now().strftime("%Y%m%d_%H%M%S"))
    os.makedirs(out, exist_ok=True)
    log("results in %s" % out)

    host = Host(args.project, out, args.noise, args.flat)
    host_rows, dev_rows, matched = [], [], {}
    presets = ints(args.presets)
    need_host = (not args.no_host) or bool(args.esp_host)
    if need_host:
        host.build()

    if not args.no_host:
        for wav in args.wav:
            for p in presets:
                for t in ints(args.taps):
                    for g in ints(args.gains):
                        r = host.replay(wav, p, t, g)
                        host_rows.append(r)
                        log("PC %s %s t%d g%d: %d frames, match %s" % (r["wav"], r["set"], t, g, r["frames"], fmt(r["match"])))

    if args.esp_host:
        for opt in ("password", "serial_port", "audio_device"):
            if not getattr(args, opt):
                sys.exit("rx_diag: the device phase needs --%s" % opt.replace("_", "-"))
        esp = Esp(args.esp_host, args.user, args.password)
        if not esp.wait_up(30):
            sys.exit("rx_diag: no answer from http://%s/radio" % args.esp_host)
        configs = [tuple(int(x) for x in c.split(":")) for c in args.device_configs.split(",") if c.strip()]
        configs = [c if len(c) == 3 else (c[0], c[1], None) for c in configs]
        for wav in args.wav:
            host.prepare(wav)
            peak = file_peak(wav)
            for (p, t, bl) in configs:
                d = device_run(args, esp, host, wav, p, t, out, None if bl is None else bool(bl))
                dev_rows.append(d)
                log("device %s %s t%d: vs Direwolf %s, lost %s/%s, DSP max %s"
                    % (d["wav"], d["set"], t, d["vs_direwolf"], d["final"].get("fifo_drops"), d["final"].get("pool_ovf"),
                       d["final"].get("dsp_max")))
                # Replay the same audio at the level the device really received.
                if d["halfswing_counts"] and not d["clip_polls"]:
                    g = d["halfswing_counts"] / max(1e-6, min(1.0, peak * args.volume))
                    m = host.replay(wav, p, t, g)
                    matched[(d["wav"], p, t, None if bl is None else bool(bl))] = m
                    log("  PC replay at the device's level (gain %d): match %s" % (m["gain"], fmt(m["match"])))

    write_report(out, args, host_rows, dev_rows, matched)
    log("report: %s" % os.path.join(out, "report.md"))


if __name__ == "__main__":
    main()
