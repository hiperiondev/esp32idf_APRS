#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_aprs_wavs.py - Regression test bench for esp32idf_APRS using real APRS WAVs.

For every .wav file in a directory the program:

  1. plays the WAV, through PipeWire, to the output that is wired to the ESP32
     audio input (ADC, GPIO33 by default) - the ESP32 demodulates it on its
     own - and, optionally, the same audio to a second output (--monitor_device)
     so the test can be listened to;
  2. AT THE SAME TIME feeds the very same audio to multimon-ng (AFSK1200),
     which acts as the reference decoder;
  3. AT THE SAME TIME reads the ESP32 console log over the serial port
     (8N1, 115200) and collects every "RX: <tnc2>" line it prints;
  4. when Direwolf is installed, AT THE SAME TIME feeds the same audio to
     Direwolf as well - a second, CRC-strict reference decoder (see below).

At the end of each file (and globally) it cross-checks the two sources:
every packet decoded by multimon-ng must have been decoded by the ESP32, with
the same content. A final summary reports total packets, percent decoded
correctly, percent decoded but with different content, and percent missing.
Packets the ESP32 decoded that multimon-ng did not are reported as "extra"
(not counted as errors: the ESP32 demodulator may simply be better).

Direwolf, the second reference
------------------------------
multimon-ng stays exactly as it always was, so every number the bench
reported before (the SUMMARY block, the per-file table, the exit code) is
still computed the same way and old runs remain comparable. Direwolf is added
NEXT TO it:

  * it gets its own sox leg at --dw_rate (48 kHz by default), paced to real
    time like the multimon-ng leg, and a generated receive-only config
    (audio from stdin, no AGW, no PTT, KISS on one TCP port only);
  * its frames are read from its KISS port, i.e. as exact AX.25 bytes, and
    with FIX_BITS 0 (the default) it only reports frames whose FCS was valid;
    the bench recomputes that FCS (CRC-16/X.25) and prints it as fcs=XXXX,
    a stable identity for the frame across runs;
  * every transmission is printed as ONE row with three cells,
    multimon-ng / Direwolf / ESP32:  '=' same as the reference content,
    'D' different payload, 'H' header corrupt, '-' not decoded, '?' only the
    ESP32 saw it (unconfirmed by any CRC-checked reference - a possible false
    positive, checked for APRS plausibility);
  * a REFERENCE CONFLICT (multimon-ng and Direwolf disagree at the same
    moment) is reported loudly: it points at the bench, not at the firmware;
  * a REFERENCE COMPARISON block follows the usual summary: ESP32 decode rate
    against multimon-ng, Direwolf and their union, with 95% intervals.

--direwolf auto|on|off chooses whether it runs; --reference multimon (the
default) | direwolf | union chooses which reference decides the exit code and
the auto-volume score; --report_csv writes every row to a CSV file.

Requirements
------------
  Python 3.7+ (uses dataclasses)
  pip install pyserial
  multimon-ng   (reference decoder)
  direwolf      (optional second reference; sudo apt install direwolf)
  sox           (audio conversion / resampling / gain)
  PipeWire      running, plus its command-line tools pw-cat and pw-dump
                (Debian/Ubuntu: sudo apt install pipewire-bin)

Usage
-----
  ./test_aprs_wavs.py                          # wavs in cwd, /dev/ttyUSB0
  ./test_aprs_wavs.py --wav_dir ./captures --serial_port /dev/ttyUSB1
  ./test_aprs_wavs.py --list_audio             # PipeWire outputs, * = default
  ./test_aprs_wavs.py --audio_device alsa_output.usb-C-Media_USB_Audio-00.analog-stereo \
                      --monitor_device "Headphones"   # ESP32 card + listen on headphones
  ./test_aprs_wavs.py --lang es                # everything in Spanish
  ./test_aprs_wavs.py --no_play --direwolf on  # compare the two references, no hardware
  ./test_aprs_wavs.py --reference direwolf --report_csv run.csv

Languages
---------
Every message, --help text and GUI label exists in English, Spanish and
Italian. The language is taken from --lang (en / es / it) or, when that is not
given, from the system language (LANGUAGE / LC_ALL / LC_MESSAGES / LANG, the
`locale` module, or the Windows UI language); any other system language falls
back to English. In the GUI the language is picked from the selector in the
button bar, which rebuilds the window in the chosen language without losing
what is typed in the form or shown in the consoles.

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
import collections
import csv
import json
import math
import os
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from typing import Callable, List, Optional, Tuple

# --------------------------------------------------------------------------
# Internationalisation (English / Spanish / Italian)
# --------------------------------------------------------------------------
#
# Every message the program shows - console output, warnings, --help texts and
# the whole GUI - goes through T(). The English text IS the catalogue key, so
# any string without a translation falls back to English instead of showing a
# missing-key placeholder, and adding a language is just adding a column.
#
# Format placeholders (%s, %d, %.1f ...) appear inside the translated text, so
# every translation MUST keep the same placeholders, in an order that matches
# the arguments it is given.
#
# The language is chosen in this order:
#   1. --lang en|es|it   (or the GUI's Language selector)
#   2. the system language: LANGUAGE / LC_ALL / LC_MESSAGES / LANG, the
#      `locale` module, or the Windows UI language
#   3. English, whenever the system language is none of the three supported

LANGS = ("en", "es", "it")
LANG_NAMES = {"en": "English", "es": "Espa\u00f1ol", "it": "Italiano"}
DEFAULT_LANG = "en"

LANG = DEFAULT_LANG          # current language; set by set_language()


def detect_system_language() -> str:
    """The system language, if it is one of the supported ones, else English.

    The environment variables are looked at first because they are what a user
    actually changes to run a program in another language; the `locale` module
    and (on Windows) the UI language are the fallbacks.
    """
    candidates = []  # type: List[str]
    for name in ("LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"):
        value = os.environ.get(name, "")
        if value:
            candidates.extend(value.split(":"))
    try:
        import locale
        loc = None
        try:
            loc = locale.getlocale()[0]
        except (TypeError, ValueError):
            loc = None
        if not loc:
            try:
                loc = locale.getdefaultlocale()[0]   # deprecated but still useful
            except (AttributeError, TypeError, ValueError):
                loc = None
        if loc:
            candidates.append(loc)
        if os.name == "nt":
            try:
                import ctypes
                lcid = ctypes.windll.kernel32.GetUserDefaultUILanguage()
                win = locale.windows_locale.get(lcid)
                if win:
                    candidates.append(win)
            except Exception:
                pass
    except Exception:
        pass
    for cand in candidates:
        code = cand.strip().replace("-", "_").split(".")[0].split("_")[0].lower()
        if code in LANGS:
            return code
    return DEFAULT_LANG


def normalise_lang(code: Optional[str]) -> str:
    """Map anything the user may type ('es', 'es_AR', 'Spanish', 'it-IT') to a
    supported code, falling back to English."""
    if not code:
        return DEFAULT_LANG
    code = str(code).strip().replace("-", "_").split(".")[0].lower()
    if code in LANGS:
        return code
    short = code.split("_")[0]
    if short in LANGS:
        return short
    for key, name in LANG_NAMES.items():
        if code == name.lower():
            return key
    aliases = {"english": "en", "ingles": "en", "inglese": "en",
               "spanish": "es", "espanol": "es", "castellano": "es",
               "spagnolo": "es", "italian": "it", "italiano": "it",
               "italien": "it"}
    return aliases.get(code, DEFAULT_LANG)


def set_language(code: Optional[str]) -> str:
    """Select the language used by every later T() call. Returns the code
    actually adopted."""
    global LANG
    LANG = normalise_lang(code) if code else detect_system_language()
    return LANG


def current_language() -> str:
    return LANG


def lang_from_argv(argv: List[str]) -> Optional[str]:
    """Read --lang from a raw argument list.

    Needed before argparse runs: the parser's own --help texts are translated
    when they are built, so the language has to be known first.
    """
    for i, arg in enumerate(argv):
        if arg == "--lang" and i + 1 < len(argv):
            return argv[i + 1]
        if arg.startswith("--lang="):
            return arg.split("=", 1)[1]
    return None


def T(text: str) -> str:
    """Translate `text` into the current language (identity for English, and
    for anything not in the catalogue)."""
    if LANG == "en":
        return text
    return _CATALOG.get(LANG, {}).get(text, text)


# Message catalogue: English source text -> {language code: translation}.
_CATALOG = {
    "es": {
        "SUMMARY": "RESUMEN",
        "Normalisation and parsing": "Normalización y análisis",
        "SSID -0, digipeated * and trailing CR all normalise away":
            "el SSID -0, el * de digipeteado y el CR final se normalizan",
        "trailing dot is kept (truncation must not pass as equal)":
            "el punto final se conserva (un truncamiento no debe pasar como igual)",
        "payload LF becomes '.' like multimon-ng prints it":
            "el LF del payload pasa a '.' como lo imprime multimon-ng",
        "colon inside the payload does not break the header split":
            "los dos puntos dentro del payload no rompen la separación del encabezado",
        "ESP console line with a log prefix parses":
            "una línea de consola del ESP con prefijo de log se analiza bien",
        "LiveMatcher verdicts": "Veredictos de LiveMatcher",
        "match inside the window is ok": "una coincidencia dentro de la ventana da ok",
        "match outside the window is missing + extra":
            "una coincidencia fuera de la ventana da missing + extra",
        "header-corrupt frame gives ONE verdict, not missing+extra":
            "una trama con encabezado corrupto da UN veredicto, no missing+extra",
        "payload-corrupt frame is a mismatch only":
            "una trama con payload corrupto es sólo un mismatch",
        "two identical beacons pair with the nearest transmission":
            "dos balizas idénticas se emparejan con la transmisión más cercana",
        "latency skew is learned from confirmed matches":
            "el desfasaje de latencia se aprende de las coincidencias confirmadas",
        "Statistics": "Estadística",
        "Wilson interval at 45/50 is wide enough to swallow 1-packet noise":
            "el intervalo de Wilson en 45/50 es lo bastante ancho para absorber el ruido de 1 paquete",
        "dB round-trip": "ida y vuelta en dB",
        "VolumeSearch convergence (simulated plateau, centre = -10.5 dB)":
            "Convergencia de VolumeSearch (meseta simulada, centro = -10,5 dB)",
        "chosen level keeps margin below the clipping threshold":
            "el nivel elegido mantiene margen por debajo del umbral de recorte",
        "Termination with a wav set that decodes nothing":
            "Terminación con un conjunto de wav que no decodifica nada",
        "SELFTEST OK": "AUTOTEST OK",
        "test_aprs_wavs - esp32idf_APRS regression bench":
            "test_aprs_wavs - banco de pruebas de regresión de esp32idf_APRS",
        "Console  (program output)": "Consola  (salida del programa)",
        "Serial  (raw data from the ESP32, unfiltered)":
            "Serie  (datos crudos del ESP32, sin filtrar)",
        "Language of the messages, the help texts and this window. The default is the system language, or English when it is not one of the three.":
            "Idioma de los mensajes, de los textos de ayuda y de esta ventana. Por omisión, el idioma del sistema, o inglés si no es ninguno de los tres.",
        "%s: gain %.3f (%+.1f dB) on a file peaking at %.3f would clip inside sox (max usable gain %.3f). Use --normalise, or lower the gain and raise the hardware level instead.":
            "%s: una ganancia de %.3f (%+.1f dB) sobre un archivo con pico %.3f recortaría dentro de sox (ganancia máxima utilizable %.3f). Usá --normalise, o bajá la ganancia y subí el nivel de hardware.",
        "  multimon-ng decoded %d packet(s)": "  multimon-ng decodificó %d paquete(s)",
        "  ESP32 decoded %d packet(s)": "  el ESP32 decodificó %d paquete(s)",
        "  -> OK: %d   DIFFERENT: %d   HDR-CORRUPT: %d   NOT DECODED: %d   EXTRA(esp only): %d":
            "  -> OK: %d   DISTINTOS: %d   ENC-CORRUPTO: %d   NO DECODIFICADOS: %d   EXTRA(sólo esp): %d",
        "    ! DIFFERENT": "    ! DISTINTO",
        "    ! HEADER CORRUPT (payload matched)":
            "    ! ENCABEZADO CORRUPTO (el payload coincide)",
        "  Files tested                      : %d": "  Archivos probados                 : %d",
        "  Total packets (multimon-ng)       : %d": "  Paquetes totales (multimon-ng)    : %d",
        "  Packets seen by ESP32             : %d": "  Paquetes vistos por el ESP32      : %d",
        "  Decoded correctly                 : %d  (%.2f%%)":
            "  Decodificados correctamente       : %d  (%.2f%%)",
        "  Decoded with different content    : %d  (%.2f%%)":
            "  Decodificados con otro contenido  : %d  (%.2f%%)",
        "  Decoded with corrupt header       : %d  (%.2f%%)":
            "  Decod. con encabezado corrupto    : %d  (%.2f%%)",
        "  Missing (not decoded)             : %d  (%.2f%%)":
            "  Faltantes (no decodificados)      : %d  (%.2f%%)",
        "  Extra (ESP32 only, not an error)  : %d": "  Extra (sólo ESP32, no es error)   : %d",
        "RESULT: no packets were decoded by multimon-ng - nothing to compare.":
            "RESULTADO: multimon-ng no decodificó ningún paquete; no hay nada que comparar.",
        "WARNING: `stdbuf` not found (package coreutils). multimon-ng's output will be\n  block-buffered on the pipe, so its packets may arrive in bursts and be\n  timestamped late, which shows up as spurious NOT DECODED verdicts.\n":
            "ATENCIÓN: no se encontró `stdbuf` (paquete coreutils). La salida de multimon-ng\n  quedará con buffer por bloques en la tubería, así que sus paquetes pueden llegar\n  en ráfagas y con marca de tiempo tardía, lo que aparece como veredictos\n  NO DECODIFICADO espurios.\n",
        "Waiting %.1f s for the ESP32 to be ready ...":
            "Esperando %.1f s a que el ESP32 esté listo ...",
        "  WARNING: no data received from the serial port yet. The firmware may be quiet until it hears/sends something; continuing.":
            "  ATENCIÓN: todavía no llegaron datos del puerto serie. El firmware puede estar callado hasta que escuche o transmita algo; se continúa.",
        "got %r": "se obtuvo %r",
        "offset=%.3f": "desfasaje=%.3f",
        "width=%.3f": "ancho=%.3f",
        "a probe over a silent wav set terminates instead of looping":
            "un sondeo sobre un conjunto de wav mudo termina en vez de quedar en bucle",
        "the whole search terminates on a silent wav set":
            "la búsqueda completa termina con un conjunto de wav mudo",
        "Options (same flags as the command line)":
            "Opciones (las mismas que en la línea de comandos)",
        "Start": "Iniciar",
        "Stop": "Detener",
        "Clear consoles": "Limpiar consolas",
        "Reset defaults": "Restablecer valores",
        "Idle": "En espera",
        "Language:": "Idioma:",
        "Running ...": "Ejecutando ...",
        "Calibration": "Calibración",
        "Test": "Prueba",
        "Live": "En vivo",
        "Total packets: %d   |   multimon-ng: %d   |   ESP32 decoded: %d   |   ESP32 missed: %d of %d (%s)":
            "Paquetes totales: %d   |   multimon-ng: %d   |   ESP32 decodificados: %d   |   ESP32 perdidos: %d de %d (%s)",
        "wav %d/%d [%s]: %s": "wav %d/%d [%s]: %s",
        "wav [%s]: %s": "wav [%s]: %s",
        "Stopping ...": "Deteniendo ...",
        "Test esp32idf_APRS with a battery of real-APRS WAV files, using multimon-ng as the reference decoder.":
            "Prueba esp32idf_APRS con una batería de archivos WAV de APRS real, usando multimon-ng como decodificador de referencia.",
        "language of the messages, the help and the GUI: en (English), es (Spanish), it (Italian). Default: the system language, or English when the system language is none of these three.":
            "idioma de los mensajes, de la ayuda y de la interfaz gráfica: en (inglés), es (español), it (italiano). Por omisión: el idioma del sistema, o inglés si el idioma del sistema no es ninguno de estos tres.",
        "directory with the .wav files (default: current directory)":
            "directorio con los archivos .wav (por omisión: el directorio actual)",
        "playback gain applied to the ESP32 leg only (default 1.0). Used as the starting point for auto-volume calibration unless --no_auto_volume is given.":
            "ganancia de reproducción aplicada sólo a la rama del ESP32 (por omisión 1.0). Se usa como punto de partida de la calibración automática de volumen salvo que se indique --no_auto_volume.",
        "bring every wav to -1 dBFS in the play chain (sox 'gain -n -1') so one gain is valid across recordings made at different levels, and gains above 1.0 stop meaning 'clip inside sox'":
            "lleva cada wav a -1 dBFS en la cadena de reproducción (sox 'gain -n -1'), de modo que una sola ganancia sirva para grabaciones hechas a distintos niveles y las ganancias mayores que 1.0 dejen de significar 'recorte dentro de sox'",
        "do not estimate the ESP32-vs-multimon-ng latency skew; compare raw timestamps instead":
            "no estimar el desfasaje de latencia entre el ESP32 y multimon-ng; comparar las marcas de tiempo crudas",
        "run the built-in unit tests (no hardware, no audio) and exit":
            "ejecutar las pruebas unitarias internas (sin hardware ni audio) y salir",
        "skip the auto-volume calibration pass and use --volume as-is for the whole run":
            "saltear la pasada de calibración automática de volumen y usar --volume tal cual durante toda la corrida",
        "seconds to wait after opening the serial port (default 4)":
            "segundos de espera tras abrir el puerto serie (por omisión 4)",
        "pause between files in seconds (default 1)":
            "pausa entre archivos, en segundos (por omisión 1)",
        "an ESP32 packet answers a multimon-ng packet only if it arrives within this many seconds of it; a packet the ESP32 has not reported after this time is shown as NOT DECODED (default 5)":
            "un paquete del ESP32 responde a uno de multimon-ng sólo si llega dentro de esta cantidad de segundos; un paquete que el ESP32 no informó pasado ese tiempo se muestra como NO DECODIFICADO (por omisión 5)",
        "do not play audio to the sound card (only run multimon-ng; useful to dry-run the parser)":
            "no reproducir audio por la placa de sonido (sólo ejecutar multimon-ng; útil para probar el parser en seco)",
        "extra multimon-ng arguments, e.g. '-A' (quoted)":
            "argumentos extra para multimon-ng, por ej. '-A' (entre comillas)",
        "open a graphical front-end: every flag in a form at the top, and below it a split console (left: program output, right: raw unfiltered serial data from the ESP32)":
            "abrir la interfaz gráfica: todas las opciones en un formulario arriba y, debajo, una consola dividida (izquierda: salida del programa; derecha: datos crudos del puerto serie del ESP32)",
        "Found %d wav file(s) in %s": "Se encontraron %d archivo(s) wav en %s",
        "DRY RUN (--no_play): only multimon-ng runs; serial port and sound card are NOT used, so ESP32 results below are not meaningful.":
            "PASADA EN SECO (--no_play): sólo se ejecuta multimon-ng; no se usan el puerto serie ni la placa de sonido, así que los resultados del ESP32 que siguen no son significativos.",
        "pyserial is required:  pip install pyserial":
            "se necesita pyserial:  pip install pyserial",
        "  -> measured esp32 latency vs multimon-ng: %+.2f s":
            "  -> latencia medida del esp32 respecto de multimon-ng: %+.2f s",
        "    ! NOT DECODED by ESP32: %s": "    ! NO DECODIFICADO por el ESP32: %s",
        "  -- Packet loss ------------------------------------------------":
            "  -- Pérdida de paquetes ----------------------------------------",
        "  This file  : lost %d of %d (%.2f%%)   not correct %d of %d (%.2f%%)":
            "  Este archivo: perdidos %d de %d (%.2f%%)   incorrectos %d de %d (%.2f%%)",
        "  Cumulative : lost %d of %d (%.2f%%)   not correct %d of %d (%.2f%%)   [%d file(s)]":
            "  Acumulado   : perdidos %d de %d (%.2f%%)   incorrectos %d de %d (%.2f%%)   [%d archivo(s)]",
        "  This file  : multimon-ng decoded no packets - nothing to measure":
            "  Este archivo: multimon-ng no decodificó ningún paquete - nada que medir",
        "file": "arch.",
        "diff": "dist",
        "hdr": "enc",
        "  Playback gain used for this test  : %.3f  (%+.1f dB)":
            "  Ganancia de reproducción usada    : %.3f  (%+.1f dB)",
        "  ESP32 latency vs multimon-ng      : %+.2f s (median of %d file(s))":
            "  Latencia del ESP32 vs multimon-ng : %+.2f s (mediana de %d archivo(s))",
        "Missing required program(s): %s\n": "Falta(n) el/los programa(s) requerido(s): %s\n",
        "  [probe %2d, budget %.1f/%d] gain=%.3f (%+5.1f dB)  mm=%d ok=%d diff=%d hdr=%d miss=%d extra=%d  score=%.1f%%  clip=%.2f/pkt":
            "  [sondeo %2d, presup. %.1f/%d] ganancia=%.3f (%+5.1f dB)  mm=%d ok=%d dist=%d enc=%d falt=%d extra=%d  puntaje=%.1f%%  recorte=%.2f/pqt",
        "  Plateau: %+.1f .. %+.1f dB (%d tied point(s) of %d probed); best raw score %.1f%%":
            "  Meseta: %+.1f .. %+.1f dB (%d punto(s) empatado(s) de %d sondeados); mejor puntaje bruto %.1f%%",
        "  NOTE: more than 6 dB of attenuation was needed. The hardware level into the ESP32 ADC is too hot - turn the RX trimmer (or the radio's volume) down and re-run, so the bench can work near 0 dB.":
            "  NOTA: hizo falta más de 6 dB de atenuación. El nivel de hardware que entra al ADC del ESP32 es demasiado alto: bajá el preset de RX (o el volumen de la radio) y repetí la prueba, para que el banco trabaje cerca de 0 dB.",
        "  serial is alive (%d console line(s) so far).":
            "  el puerto serie responde (%d línea(s) de consola hasta ahora).",
        "start %.2f converges to the plateau centre within budget (%.1f/%d batches, %d probes)":
            "desde %.2f converge al centro de la meseta dentro del presupuesto (%.1f/%d lotes, %d sondeos)",
        "chose %+.1f dB at cost %.1f": "eligió %+.1f dB con un costo de %.1f",
        "%d playback call(s)": "%d llamada(s) de reproducción",
        "SELFTEST FAILED: %d of the checks above did not pass":
            "AUTOTEST FALLIDO: %d de las comprobaciones anteriores no pasaron",
        "--gui needs tkinter.  Debian/Ubuntu: sudo apt install python3-tk\n":
            "--gui necesita tkinter.  Debian/Ubuntu: sudo apt install python3-tk\n",
        "Language": "Idioma",
        "Stop the running test before changing the language.":
            "Detené la prueba en curso antes de cambiar el idioma.",
        "Finished (exit code %s)": "Terminado (código de salida %s)",
        "\n[gui] finished, exit code %s\n": "\n[gui] terminado, código de salida %s\n",
        "ESP32 console serial port (default: %s)":
            "puerto serie de la consola del ESP32 (por omisión: %s)",
        "serial speed, 8N1 (default: %d)": "velocidad del puerto serie, 8N1 (por omisión: %d)",
        "dB below the clipping threshold to fall back to when no plateau could be scored (default %.0f)":
            "dB por debajo del umbral de recorte a los que recurrir cuando no se pudo puntuar ninguna meseta (por omisión %.0f)",
        "lowest gain the search may use (default %.2f)":
            "ganancia mínima que puede usar la búsqueda (por omisión %.2f)",
        "highest gain the search may use (default %.2f)":
            "ganancia máxima que puede usar la búsqueda (por omisión %.2f)",
        "passes over the wav set before a calibration probe gives up (default %d)":
            "pasadas sobre el conjunto de wav antes de que un sondeo de calibración se dé por vencido (por omisión %d)",
        "number of packets to test per volume try during auto-volume calibration - counts both multimon-ng packets and ESP32-only ones multimon-ng missed (default %d)":
            "cantidad de paquetes a probar por cada volumen durante la calibración automática: cuenta tanto los paquetes de multimon-ng como los que sólo vio el ESP32 y multimon-ng perdió (por omisión %d)",
        "search budget for the auto-volume pass, in batches of --auto_volume_batch packets (default %d). Cheap 8-packet clipping probes cost a fraction of a batch, full scoring probes cost one each.":
            "presupuesto de búsqueda de la pasada de volumen automático, en lotes de --auto_volume_batch paquetes (por omisión %d). Los sondeos baratos de recorte, de 8 paquetes, cuestan una fracción de lote; los sondeos completos de puntaje, uno cada uno.",
        "seconds to keep listening after each file (default %.1f)":
            "segundos que se sigue escuchando después de cada archivo (por omisión %.1f)",
        "--auto_volume_batch must be >= 1 (got %d)\n":
            "--auto_volume_batch debe ser >= 1 (se recibió %d)\n",
        "--auto_volume_max_rounds must be >= 1 (got %d)\n":
            "--auto_volume_max_rounds debe ser >= 1 (se recibió %d)\n",
        "--max_passes must be >= 1 (got %d)\n": "--max_passes debe ser >= 1 (se recibió %d)\n",
        "--volume_min must be > 0 and < --volume_max (got %g and %g)\n":
            "--volume_min debe ser > 0 y < --volume_max (se recibieron %g y %g)\n",
        "--volume must be within [%g, %g] (got %g)\n":
            "--volume debe estar dentro de [%g, %g] (se recibió %g)\n",
        "--match_window must be > 0 (got %g)\n":
            "--match_window debe ser > 0 (se recibió %g)\n",
        "wav_dir not found: %s\n": "no se encontró wav_dir: %s\n",
        "No .wav files in %s\n": "No hay archivos .wav en %s\n",
        "None of the selected --wav_files were found as .wav files in %s\n":
            "Ninguno de los --wav_files seleccionados se encontró como "
            "archivo .wav en %s\n",
        "directory with the .wav files (default: current directory)":
            "directorio con los archivos .wav (por defecto: directorio actual)",
        "comma-separated list of .wav file names (relative to "
        "--wav_dir) to test; empty means every .wav file found in "
        "--wav_dir (default: empty). In the GUI this is the file "
        "selector next to --wav_dir.":
            "lista de nombres de archivos .wav separados por comas (relativos "
            "a --wav_dir) a probar; vacío significa todos los archivos .wav "
            "encontrados en --wav_dir (por defecto: vacío). En la GUI esto es "
            "el selector de archivos junto a --wav_dir.",
        "--wav_files": "--wav_files",
        "wav files found in wav_dir (select one or more to test; none = all)":
            "archivos wav encontrados en wav_dir (selecciona uno o más para "
            "probar; ninguno = todos)",
        "%d selected of %d": "%d seleccionado(s) de %d",
        "no .wav files found": "no se encontraron archivos .wav",
        "Refresh": "Actualizar",
        "Select all": "Seleccionar todo",
        "Select none": "No seleccionar nada",
        "Serial: %s @ %d 8N1   Audio: %s": "Serie: %s @ %d 8N1   Audio: %s",
        "\nInterrupted - reporting what has been tested so far.":
            "\nInterrumpido: se informa lo probado hasta ahora.",
        "\nDRY RUN finished: multimon-ng decoded %d packet(s) in %d file(s).":
            "\nPASADA EN SECO terminada: multimon-ng decodificó %d paquete(s) en %d archivo(s).",
        "%06d [multimon  --:--.-] NOT DECODED": "%06d [multimon  --:--.-] NO DECODIFICADO",
        "       [esp32 only      %s] %s": "       [sólo esp32      %s] %s",
        "      ! DECODED BUT DIFFERENT": "      ! DECODIFICADO PERO DISTINTO",
        "\n[mm] multimon-ng did not exit within 60 s - killing it\n":
            "\n[mm] multimon-ng no terminó en 60 s: se lo mata\n",
        "<probe %+.1f dB>": "<sondeo %+.1f dB>",
        "  No clipping seen up to %+.1f dB (gain %.3f) - the hardware level into the ADC may be too low; check the RX trimmer.":
            "  No se observó recorte hasta %+.1f dB (ganancia %.3f): el nivel de hardware que entra al ADC puede ser demasiado bajo; revisá el preset de RX.",
        "  No packets decoded during calibration at any level - falling back to %.3f (%+.1f dB, threshold - %.0f dB).":
            "  No se decodificó ningún paquete durante la calibración en ningún nivel: se recurre a %.3f (%+.1f dB, umbral - %.0f dB).",
        "  Probe budget spent; stopping the plateau sweep (raise it with --auto_volume_max_rounds).":
            "  Se agotó el presupuesto de sondeos; se detiene el barrido de la meseta (ampliálo con --auto_volume_max_rounds).",
        "  No usable score data - using threshold - %.0f dB = %.3f (%+.1f dB).":
            "  No hay datos de puntaje utilizables: se usa umbral - %.0f dB = %.3f (%+.1f dB).",
        "  NOTE: more than 6 dB of boost was needed. The hardware level into the ESP32 ADC is too low - turn the RX trimmer up and re-run. Boosting digitally also amplifies the sound card's own noise floor.":
            "  NOTA: hizo falta más de 6 dB de refuerzo. El nivel de hardware que entra al ADC del ESP32 es demasiado bajo: subí el preset de RX y repetí la prueba. Amplificar en digital también amplifica el ruido propio de la placa de sonido.",
        "  PASS  %s": "  BIEN  %s",
        "  FAIL  %s  %s": "  MAL   %s  %s",
        "run_one_wav called %d times - no pass limit":
            "run_one_wav se llamó %d veces: no hay límite de pasadas",
        "Cannot open a display for --gui: %s\n":
            "No se puede abrir un display para --gui: %s\n",
        "Invalid option": "Opción inválida",
        "Quit": "Salir",
        "A test is still running. Stop it and quit?":
            "Todavía hay una prueba en curso. ¿Detenerla y salir?",
        "      ! PAYLOAD OK BUT HEADER CORRUPT":
            "      ! PAYLOAD CORRECTO PERO ENCABEZADO CORRUPTO",
        "       [esp32     --:--.-] NOT DECODED": "       [esp32     --:--.-] NO DECODIFICADO",
        "\n[audio] player failed (rc=%s): %s\n": "\n[audio] falló el reproductor (rc=%s): %s\n",
        "\n[gui] stopped by user\n": "\n[gui] detenido por el usuario\n",
        "Cannot open serial port %s: %s\n": "No se puede abrir el puerto serie %s: %s\n",
        "       [progress %s / %s] multimon=%d  ok=%d  not-decoded=%d  different=%d  (serial lines seen: %d)":
            "       [avance %s / %s] multimon=%d  ok=%d  no-decodificados=%d  distintos=%d  (líneas de serie vistas: %d)",
        "      probe incomplete: %d/%d packet(s) after %d pass(es) over the wav set - check the audio routing and the files":
            "      sondeo incompleto: %d/%d paquete(s) tras %d pasada(s) sobre el conjunto de wav; revisá el ruteo de audio y los archivos",
        "      score fell %.0f points below the best - the lower knee is past, no need to go quieter":
            "      el puntaje cayó %.0f puntos por debajo del mejor: ya se pasó el codo inferior, no hace falta bajar más",
        "%s: %r is not a valid %s": "%s: %r no es un %s válido",
        "\n[gui] unexpected error:\n": "\n[gui] error inesperado:\n",
        "\n[serial] read error: %s\n": "\n[serie] error de lectura: %s\n",
        "  <- OVER-RANGE":
            "  <- SOBRE-RANGO",
        "  Over-range at %+.1f dB (gain %.3f): no probe will go that high again; stepping down in %.2f dB steps":
            "  Sobre-rango en %+.1f dB (ganancia %.3f): ningún sondeo volverá a subir hasta ahí; se baja en pasos de %.2f dB",
        "  Still over-range at the lowest gain allowed, %.3f (%+.1f dB): the hardware level into the ADC is far too hot - turn the RX trimmer (or the PC volume) down and run again.":
            "  Sigue el sobre-rango con la ganancia más baja permitida, %.3f (%+.1f dB): el nivel de hardware que entra al ADC es demasiado alto; bajá el preset de RX (o el volumen de la PC) y volvé a ejecutar.",
        "  Descent budget spent while still over-range at %+.1f dB - using %+.1f dB (%.0f dB below it). Turn the RX trimmer down, or raise --auto_volume_max_rounds / --clip_step_db.":
            "  Se agotó el presupuesto de descenso con sobre-rango todavía en %+.1f dB: se usa %+.1f dB (%.0f dB por debajo). Bajá el preset de RX, o aumentá --auto_volume_max_rounds / --clip_step_db.",
        "AUTO-VOLUME CALIBRATION (over-range ceiling + plateau centre)":
            "CALIBRACIÓN AUTOMÁTICA DE VOLUMEN (techo de sobre-rango + centro de la meseta)",
        "  Start gain %.3f (%+.1f dB), range %.3f..%.3f, budget %d probe(s), %d packet(s) per scoring probe, %.2f dB steps below over-range":
            "  Ganancia inicial %.3f (%+.1f dB), rango %.3f..%.3f, presupuesto %d sondeo(s), %d paquete(s) por sondeo de puntaje, pasos de %.2f dB por debajo del sobre-rango",
        "  Highest level without over-range: %+.1f dB (gain %.3f)":
            "  Nivel más alto sin sobre-rango: %+.1f dB (ganancia %.3f)",
        "      over-range during a scoring probe at %+.1f dB - this level is dropped and nothing at or above it is played again":
            "      sobre-rango durante un sondeo de puntaje en %+.1f dB: se descarta ese nivel y no se vuelve a reproducir nada en él ni por encima",
        "  Chosen gain: %.3f (%+.1f dB), %.1f dB below the highest level without over-range":
            "  Ganancia elegida: %.3f (%+.1f dB), %.1f dB por debajo del nivel más alto sin sobre-rango",
        "over-range warnings per packet a level may produce and still count as clean (default %.2f: a single warning marks the level as clipping, and no probe goes that high again)":
            "advertencias de sobre-rango por paquete que un nivel puede producir y seguir contando como limpio (por omisión %.2f: una sola advertencia marca el nivel como recorte, y ningún sondeo vuelve a subir hasta ahí)",
        "once over-range has been reported, the search never raises the gain again and steps DOWN by this many dB per probe until the warnings stop (default %.2f)":
            "una vez informado un sobre-rango, la búsqueda no vuelve a subir la ganancia y BAJA esta cantidad de dB por sondeo hasta que cesan las advertencias (por omisión %.2f)",
        "--clip_rate must be in [0, 1) (got %g)\n":
            "--clip_rate debe estar en [0, 1) (se recibió %g)\n",
        "--clip_step_db must be > 0 and <= %g (got %g)\n":
            "--clip_step_db debe ser > 0 y <= %g (se recibió %g)\n",
        "\nAuto-volume calibration interrupted - proceeding with the best level known to be free of over-range so far: %.3f (%+.1f dB).":
            "\nCalibración automática de volumen interrumpida: se continúa con el mejor nivel conocido sin sobre-rango hasta ahora: %.3f (%+.1f dB).",
        "Over-range ceiling":
            "Techo de sobre-rango",
        "start %.2f: no probe is played at or above a level that reported over-range":
            "inicio %.2f: ningún sondeo se reproduce en un nivel que informó sobre-rango ni por encima",
        "history %s":
            "historial %s",
        "below an over-range level the gain steps down by --clip_step_db (%.2f dB)":
            "por debajo de un nivel con sobre-rango la ganancia baja de a --clip_step_db (%.2f dB)",
        "steps %r":
            "pasos %r",
        "one over-range warning in a scoring batch lowers the ceiling and drops that level":
            "una sola advertencia de sobre-rango en un lote de puntaje baja el techo y descarta ese nivel",
        "ceiling %s, chose %+.1f dB":
            "techo %s, eligió %+.1f dB",
        "an interrupted search falls back below the over-range ceiling, not to the start gain":
            "una búsqueda interrumpida vuelve por debajo del techo de sobre-rango, no a la ganancia inicial",
        "safe %+.1f dB, ceiling %+.1f dB":
            "seguro %+.1f dB, techo %+.1f dB",
        "a silent wav set never raises the gain":
            "un conjunto de wav silencioso nunca sube la ganancia",
        "probed %s":
            "sondeado %s",
        "a descent that runs out of budget still ends below the over-range ceiling":
            "un descenso que agota el presupuesto igual termina por debajo del techo de sobre-rango",
        "integer": "entero",
        "number": "número",
        "Every volume probe restarts the wav list":
            "Cada sondeo de volumen reinicia la lista de wav",
        "a new volume replays the list from the first file, in order":
            "un volumen nuevo vuelve a reproducir la lista desde el primer archivo, en orden",
        # PipeWire routing
        "\n[audio] monitor player failed (rc=%s): %s\n":
            "\n[audio] falló el reproductor del monitor (rc=%s): %s\n",
        "\n[audio] the player was still running %.0f s after the end of the file - killed it (was the output unplugged?)\n":
            "\n[audio] el reproductor seguía corriendo %.0f s después del final del archivo: se lo terminó (¿se desconectó la salida?)\n",
        "%r matches more than one PipeWire output: %s":
            "%r coincide con más de una salida de PipeWire: %s",
        "--monitor_device and --audio_device are the same PipeWire output (%s): the monitor stream would be mixed into the ESP32 signal\n":
            "--monitor_device y --audio_device son la misma salida de PipeWire (%s): el flujo del monitor se mezclaría con la señal del ESP32\n",
        "--monitor_volume must be in [0, 1] (got %g)\n":
            "--monitor_volume debe estar en [0, 1] (se recibió %g)\n",
        "Give --audio_device / --monitor_device the node name (it does not change between reboots), the serial, or a unique part of the description.":
            "Pasale a --audio_device / --monitor_device el nombre del nodo (no cambia entre reinicios), el número de serie o una parte única de la descripción.",
        "Monitor: %s   (stream volume %.2f)":
            "Monitor: %s   (volumen del flujo %.2f)",
        "Monitor: none":
            "Monitor: ninguno",
        "PipeWire has no audio output (sink)":
            "PipeWire no tiene ninguna salida de audio (sink)",
        "PipeWire has no default output set - name the output explicitly":
            "PipeWire no tiene salida por defecto: indicá la salida explícitamente",
        "PipeWire output (sink) to listen on while the test runs, e.g. headphones; it plays exactly what the ESP32 gets. Same forms as --audio_device, 'default' for the PipeWire default output (default: no monitor)":
            "salida de PipeWire (sink) para escuchar mientras corre la prueba, por ej. los auriculares; reproduce exactamente lo que recibe el ESP32. Mismas formas que --audio_device, 'default' para la salida por defecto de PipeWire (por omisión: sin monitor)",
        "PipeWire output (sink) wired to the ESP32 audio input: its node name, serial, or a unique part of its description (default: the PipeWire default output). See --list_audio.":
            "salida de PipeWire (sink) conectada a la entrada de audio del ESP32: su nombre de nodo, su número de serie o una parte única de su descripción (por omisión: la salida por defecto de PipeWire). Ver --list_audio.",
        "PipeWire outputs (sinks) - * marks the default:":
            "Salidas de PipeWire (sinks) - * marca la salida por defecto:",
        "PipeWire routing":
            "Ruteo de PipeWire",
        "PipeWire stream volume of the monitor, 0..1 (default 0.5). Only the monitor: the ESP32 level is set by --volume alone.":
            "volumen del flujo de PipeWire del monitor, 0..1 (por omisión 0.5). Sólo el monitor: el nivel del ESP32 lo fija únicamente --volume.",
        "an ambiguous or unknown output is rejected, never guessed":
            "una salida ambigua o desconocida se rechaza, nunca se adivina",
        "an empty device and 'default' both give the PipeWire default output":
            "un dispositivo vacío y 'default' dan ambos la salida por defecto de PipeWire",
        "an output is found by node name, serial, id, GUI label and description":
            "una salida se encuentra por nombre de nodo, número de serie, id, etiqueta de la GUI y descripción",
        "description":
            "descripción",
        "list the PipeWire outputs (sinks) and exit":
            "listar las salidas de PipeWire (sinks) y salir",
        "no PipeWire output matches %r":
            "ninguna salida de PipeWire coincide con %r",
        "node name":
            "nombre del nodo",
        "play chain: sox renders a file, pw-cat plays it pinned to the sink, no fallback":
            "cadena de reproducción: sox genera un archivo y pw-cat lo reproduce fijado al sink, sin salida alternativa",
        "pw-dump did not answer - is PipeWire running?":
            "pw-dump no respondió: ¿está corriendo PipeWire?",
        "pw-dump failed (rc=%s): %s":
            "falló pw-dump (rc=%s): %s",
        "pw-dump not found - install the PipeWire tools (Debian/Ubuntu: sudo apt install pipewire-bin)":
            "no se encontró pw-dump: instalá las herramientas de PipeWire (Debian/Ubuntu: sudo apt install pipewire-bin)",
        "pw-dump printed something that is not JSON: %s":
            "pw-dump imprimió algo que no es JSON: %s",
        "pw-dump: only sinks are listed, and the default output is found":
            "pw-dump: sólo se listan los sinks y se encuentra la salida por defecto",
        "serial":
            "serie",
        "sox could not render %s (rc=%s): %s":
            "sox no pudo generar %s (rc=%s): %s",
        # Direwolf (second reference decoder)
        "no free TCP port found for Direwolf's KISS server":
            'no se encontró un puerto TCP libre para el servidor KISS de Direwolf',
        '%s: %d of %d  (%.2f%%, 95%% CI %.1f..%.1f%%)':
            '%s: %d de %d  (%.2f%%, IC 95%% %.1f..%.1f%%)',
        'REFERENCE COMPARISON (Direwolf)':
            'COMPARACIÓN DE REFERENCIAS (Direwolf)',
        'Direwolf: KISS, AX.25 and FCS':
            'Direwolf: KISS, AX.25 y FCS',
        "CRC-16/X.25 check value of '123456789' is 0x906E":
            "el valor de verificación CRC-16/X.25 de '123456789' es 0x906E",
        'a frame followed by its FCS leaves the X.25 residue 0x0F47':
            'una trama seguida de su FCS deja el residuo X.25 0x0F47',
        "AX.25 address field: SSID, path and the H bit ('*') decode":
            "campo de direcciones AX.25: se decodifican el SSID, la ruta y el bit H ('*')",
        'KISS: escapes, split reads, garbage and empty frames':
            'KISS: escapes, lecturas partidas, basura y tramas vacías',
        'malformed address field and non-APRS frames are rejected':
            'se rechazan los campos de dirección mal formados y las tramas que no son APRS',
        'the same frame from Direwolf, multimon-ng and the ESP32 compares equal':
            'la misma trama de Direwolf, multimon-ng y el ESP32 se compara como igual',
        'APRS plausibility: a real position passes, noise fails':
            'verosimilitud APRS: una posición real pasa, el ruido no',
        'an automatic KISS port is inside the range Direwolf accepts':
            'un puerto KISS automático está dentro del rango que acepta Direwolf',
        'Direwolf config: receive only, CRC-strict, KISS on one port only':
            'configuración de Direwolf: sólo recepción, CRC estricto, KISS en un único puerto',
        'ClusterMatcher (three columns)':
            'ClusterMatcher (tres columnas)',
        'Direwolf and ESP32 latency skews are learned independently':
            'los desfasajes de latencia de Direwolf y del ESP32 se aprenden por separado',
        'ClusterMatcher agrees with LiveMatcher on multimon-ng vs ESP32':
            'ClusterMatcher coincide con LiveMatcher en multimon-ng vs ESP32',
        'statistics: conflicts are not scored, ESP32-only frames are never successes':
            'estadística: los conflictos no se puntúan y las tramas sólo del ESP32 nunca son aciertos',
        'the multimon-ng reference counts packets exactly as before':
            'la referencia multimon-ng cuenta los paquetes exactamente como antes',
        '  References: both %d   mm-only %d   dw-only %d   conflicts %d':
            '  Referencias: ambas %d   sólo mm %d   sólo dw %d   conflictos %d',
        '  Direwolf %s, %d Hz, modem profile %s, FIX_BITS %d':
            '  Direwolf %s, %d Hz, perfil de módem %s, FIX_BITS %d',
        '  Reference for the exit code       : %s':
            '  Referencia del código de salida   : %s',
        '  Packets multimon-ng / Direwolf    : %d / %d':
            '  Paquetes multimon-ng / Direwolf   : %d / %d',
        '  Union / both references           : %d / %d':
            '  Unión / ambas referencias         : %d / %d',
        '  multimon-ng only / Direwolf only  : %d / %d':
            '  Sólo multimon-ng / sólo Direwolf  : %d / %d',
        '  Reference conflicts               : %d':
            '  Conflictos entre referencias      : %d',
        'RESULT: the selected reference decoded no packets - nothing to compare.':
            'RESULTADO: la referencia elegida no decodificó ningún paquete; no hay nada que comparar.',
        'Direwolf as a second reference decoder: auto (use it when it is installed), on (required), off (legacy two-column bench). Default: auto':
            'Direwolf como segundo decodificador de referencia: auto (se usa si está instalado), on (obligatorio), off (banco clásico de dos columnas). Por omisión: auto',
        'reference that decides the exit code and the auto-volume score: multimon (legacy, default), direwolf, or union (a packet either reference decoded)':
            'referencia que decide el código de salida y el puntaje del volumen automático: multimon (clásica, por omisión), direwolf o union (un paquete que decodificó cualquiera de las dos)',
        "Direwolf modem profile appended to 'MODEM 1200', e.g. 'A+' or 'E+' (default: Direwolf's own default)":
            "perfil de módem de Direwolf que se agrega a 'MODEM 1200', por ej. 'A+' o 'E+' (por omisión: el propio de Direwolf)",
        "TCP port of Direwolf's KISS server, 1024..49151 (default 0: pick a free port for every file)":
            'puerto TCP del servidor KISS de Direwolf, 1024..49151 (por omisión 0: se elige un puerto libre para cada archivo)',
        "extra Direwolf arguments, e.g. '-P E+' (quoted)":
            "argumentos extra para Direwolf, por ej. '-P E+' (entre comillas)",
        'write every multimon-ng / Direwolf / ESP32 row to this CSV file (needs Direwolf)':
            'escribir cada fila multimon-ng / Direwolf / ESP32 en este archivo CSV (necesita Direwolf)',
        'NOTE: --report_csv needs Direwolf; no CSV will be written\n':
            'NOTA: --report_csv necesita Direwolf; no se va a escribir ningún CSV\n',
        '! REFERENCE CONFLICT - multimon-ng and Direwolf disagree; check normalisation':
            '! CONFLICTO DE REFERENCIAS - multimon-ng y Direwolf no coinciden; revisá la normalización',
        '! only multimon-ng decoded it (Direwolf did not)':
            '! sólo lo decodificó multimon-ng (Direwolf no)',
        '! ESP32 NOT DECODED':
            '! NO DECODIFICADO por el ESP32',
        '  Direwolf decoded %d packet(s)   (CRC-strict, FIX_BITS=0)':
            '  Direwolf decodificó %d paquete(s)   (CRC estricto, FIX_BITS=0)',
        '  Direwolf decoded %d packet(s)   (NOT CRC-strict, FIX_BITS=%d)':
            '  Direwolf decodificó %d paquete(s)   (CRC NO estricto, FIX_BITS=%d)',
        '  ESP32 vs Direwolf : OK %d  DIFFERENT %d  HDR-CORRUPT %d  NOT DECODED %d':
            '  ESP32 vs Direwolf : OK %d  DISTINTOS %d  ENC-CORRUPTO %d  NO DECODIFICADOS %d',
        '  ESP32-only (unconfirmed): %d   of which implausible: %d':
            '  Sólo ESP32 (sin confirmar): %d   de ellos inverosímiles: %d',
        '  -> measured Direwolf latency vs multimon-ng: %+.2f s':
            '  -> latencia medida de Direwolf respecto de multimon-ng: %+.2f s',
        '  WARNING: Direwolf is NOT a strict CRC reference: FIX_BITS=%d':
            '  ATENCIÓN: Direwolf NO es una referencia de CRC estricta: FIX_BITS=%d',
        'union':
            'unión',
        '  ESP32 correct vs multimon-ng      ':
            '  ESP32 correcto vs multimon-ng     ',
        '  ESP32 correct vs Direwolf         ':
            '  ESP32 correcto vs Direwolf        ',
        '  ESP32 correct vs union            ':
            '  ESP32 correcto vs unión           ',
        '  ESP32-only, unconfirmed           : %d  (implausible: %d)':
            '  Sólo ESP32, sin confirmar         : %d  (inverosímiles: %d)',
        '  Direwolf latency vs multimon-ng   : %+.2f s (median of %d file(s))':
            '  Latencia de Direwolf vs multimon-ng: %+.2f s (mediana de %d archivo(s))',
        'row: %s':
            'fila: %s',
        'Direwolf, when installed, runs as a second, CRC-strict reference and the report shows multimon-ng / Direwolf / ESP32 for every packet.':
            'Direwolf, si está instalado, funciona como segunda referencia con CRC estricto y el informe muestra multimon-ng / Direwolf / ESP32 para cada paquete.',
        'sample rate of the audio fed to Direwolf (default %d)':
            'frecuencia de muestreo del audio que recibe Direwolf (por omisión %d)',
        'Direwolf FIX_BITS (default %d). Anything above 0 lets Direwolf repair frames with a bad CRC, so it stops being a strict reference.':
            'FIX_BITS de Direwolf (por omisión %d). Cualquier valor mayor que 0 le permite reparar tramas con CRC incorrecto, así que deja de ser una referencia estricta.',
        "seconds to wait for Direwolf's KISS port to open (default %.1f)":
            'segundos de espera hasta que se abra el puerto KISS de Direwolf (por omisión %.1f)',
        '--dw_rate must be >= 8000 (got %d)\n':
            '--dw_rate debe ser >= 8000 (se recibió %d)\n',
        '--dw_fix_bits must be in [0, %d] (got %d)\n':
            '--dw_fix_bits debe estar en [0, %d] (se recibió %d)\n',
        '--dw_kiss_port must be 0 or in [%d, %d] (got %d)\n':
            '--dw_kiss_port debe ser 0 o estar en [%d, %d] (se recibió %d)\n',
        '--dw_start_timeout must be > 0 (got %g)\n':
            '--dw_start_timeout debe ser > 0 (se recibió %g)\n',
        '--reference %s needs Direwolf, but --direwolf is off\n':
            '--reference %s necesita Direwolf, pero --direwolf está en off\n',
        '--reference %s needs Direwolf, which is not available\n':
            '--reference %s necesita Direwolf, que no está disponible\n',
        'Direwolf: %d Hz, modem profile %s, FIX_BITS %d, KISS port %s, reference for the exit code: %s':
            'Direwolf: %d Hz, perfil de módem %s, FIX_BITS %d, puerto KISS %s, referencia del código de salida: %s',
        '\nDirewolf failed - reporting what has been tested so far.':
            '\nFalló Direwolf: se informa lo probado hasta ahora.',
        '! ESP32 ONLY - unconfirmed by any CRC-checked reference (plausible: %s)':
            '! SÓLO ESP32 - no lo confirmó ninguna referencia con CRC verificado (verosímil: %s)',
        '! multimon-ng did not decode it (Direwolf did)':
            '! multimon-ng no lo decodificó (Direwolf sí)',
        '(default)':
            '(por omisión)',
        'Total packets: %d   |   multimon-ng: %d   |   Direwolf: %d   |   ESP32 decoded: %d   |   ESP32 missed: %d of %d (%s)   |   conflicts: %d   |   ESP32-only: %d':
            'Paquetes totales: %d   |   multimon-ng: %d   |   Direwolf: %d   |   ESP32 decodificados: %d   |   ESP32 perdidos: %d de %d (%s)   |   conflictos: %d   |   sólo ESP32: %d',
        '--direwolf on, but the direwolf program was not found.  Debian/Ubuntu: sudo apt install direwolf\n':
            '--direwolf on, pero no se encontró el programa direwolf.  Debian/Ubuntu: sudo apt install direwolf\n',
        'NOTE: direwolf not found - running with multimon-ng as the only reference (install it for the three-column report: sudo apt install direwolf)\n':
            'NOTA: no se encontró direwolf; se corre con multimon-ng como única referencia (instalalo para el informe de tres columnas: sudo apt install direwolf)\n',
        '  Direwolf runs too: the two references are compared, both fed at %.0fx real time.':
            '  También corre Direwolf: se comparan las dos referencias, ambas alimentadas a %.0fx tiempo real.',
        'CSV report: %d row(s) written to %s':
            'Informe CSV: %d fila(s) escrita(s) en %s',
        'DRY RUN: Direwolf decoded %d packet(s).':
            'PASADA EN SECO: Direwolf decodificó %d paquete(s).',
        'Cannot start Direwolf: %s':
            'No se puede iniciar Direwolf: %s',
        'Direwolf exited during start-up (rc=%s): %s':
            'Direwolf terminó durante el arranque (rc=%s): %s',
        "Direwolf's KISS port %d did not open within %.1f s (raise --dw_start_timeout): %s":
            'el puerto KISS %d de Direwolf no se abrió en %.1f s (aumentá --dw_start_timeout): %s',
        'yes':
            'sí',
        'NO':
            'NO',
        '! ESP32 DECODED BUT DIFFERENT':
            '! DECODIFICADO POR EL ESP32 PERO DISTINTO',
        '! ESP32 PAYLOAD OK BUT HEADER CORRUPT':
            '! PAYLOAD DEL ESP32 CORRECTO PERO ENCABEZADO CORRUPTO',
        'auto':
            'auto',
        'Cannot write the CSV report %s: %s\n':
            'No se puede escribir el informe CSV %s: %s\n',
        '\n[dw] Direwolf did not exit within %.0f s after the end of the audio - killing it\n':
            '\n[dw] Direwolf no terminó %.0f s después del final del audio: se lo mata\n',
        '       [progress %s / %s] multimon=%d  direwolf=%d  ok=%d  not-decoded=%d  different=%d  (serial lines seen: %d)':
            '       [avance %s / %s] multimon=%d  direwolf=%d  ok=%d  no-decodificados=%d  distintos=%d  (líneas de serie vistas: %d)',
    },
    "it": {
        "SUMMARY": "RIEPILOGO",
        "Normalisation and parsing": "Normalizzazione e parsing",
        "SSID -0, digipeated * and trailing CR all normalise away":
            "l'SSID -0, l'* di digipeating e il CR finale vengono normalizzati",
        "trailing dot is kept (truncation must not pass as equal)":
            "il punto finale viene conservato (un troncamento non deve risultare uguale)",
        "payload LF becomes '.' like multimon-ng prints it":
            "l'LF del payload diventa '.' come lo stampa multimon-ng",
        "colon inside the payload does not break the header split":
            "i due punti dentro il payload non rompono la divisione dell'intestazione",
        "ESP console line with a log prefix parses":
            "una riga di console ESP con prefisso di log viene analizzata",
        "LiveMatcher verdicts": "Verdetti di LiveMatcher",
        "match inside the window is ok": "una corrispondenza dentro la finestra dà ok",
        "match outside the window is missing + extra":
            "una corrispondenza fuori dalla finestra dà missing + extra",
        "header-corrupt frame gives ONE verdict, not missing+extra":
            "una trama con intestazione corrotta dà UN verdetto, non missing+extra",
        "payload-corrupt frame is a mismatch only":
            "una trama con payload corrotto è solo un mismatch",
        "two identical beacons pair with the nearest transmission":
            "due beacon identici si accoppiano con la trasmissione più vicina",
        "latency skew is learned from confirmed matches":
            "lo scarto di latenza viene appreso dalle corrispondenze confermate",
        "Statistics": "Statistica",
        "Wilson interval at 45/50 is wide enough to swallow 1-packet noise":
            "l'intervallo di Wilson a 45/50 è abbastanza ampio da assorbire il rumore di 1 pacchetto",
        "dB round-trip": "andata e ritorno in dB",
        "VolumeSearch convergence (simulated plateau, centre = -10.5 dB)":
            "Convergenza di VolumeSearch (plateau simulato, centro = -10,5 dB)",
        "chosen level keeps margin below the clipping threshold":
            "il livello scelto mantiene margine sotto la soglia di clipping",
        "Termination with a wav set that decodes nothing":
            "Terminazione con un insieme di wav che non decodifica nulla",
        "SELFTEST OK": "AUTOTEST OK",
        "test_aprs_wavs - esp32idf_APRS regression bench":
            "test_aprs_wavs - banco di prova di regressione di esp32idf_APRS",
        "Console  (program output)": "Console  (output del programma)",
        "Serial  (raw data from the ESP32, unfiltered)":
            "Seriale  (dati grezzi dall'ESP32, non filtrati)",
        "Language of the messages, the help texts and this window. The default is the system language, or English when it is not one of the three.":
            "Lingua dei messaggi, dei testi di aiuto e di questa finestra. Per impostazione predefinita, la lingua di sistema, o inglese se non è una delle tre.",
        "%s: gain %.3f (%+.1f dB) on a file peaking at %.3f would clip inside sox (max usable gain %.3f). Use --normalise, or lower the gain and raise the hardware level instead.":
            "%s: un guadagno di %.3f (%+.1f dB) su un file con picco %.3f causerebbe clipping dentro sox (guadagno massimo utilizzabile %.3f). Usa --normalise, oppure abbassa il guadagno e alza il livello hardware.",
        "  multimon-ng decoded %d packet(s)": "  multimon-ng ha decodificato %d pacchetto/i",
        "  ESP32 decoded %d packet(s)": "  l'ESP32 ha decodificato %d pacchetto/i",
        "  -> OK: %d   DIFFERENT: %d   HDR-CORRUPT: %d   NOT DECODED: %d   EXTRA(esp only): %d":
            "  -> OK: %d   DIVERSI: %d   INTEST-CORROTTA: %d   NON DECODIFICATI: %d   EXTRA(solo esp): %d",
        "    ! DIFFERENT": "    ! DIVERSO",
        "    ! HEADER CORRUPT (payload matched)":
            "    ! INTESTAZIONE CORROTTA (il payload coincide)",
        "  Files tested                      : %d": "  File testati                      : %d",
        "  Total packets (multimon-ng)       : %d": "  Pacchetti totali (multimon-ng)    : %d",
        "  Packets seen by ESP32             : %d": "  Pacchetti visti dall'ESP32        : %d",
        "  Decoded correctly                 : %d  (%.2f%%)":
            "  Decodificati correttamente        : %d  (%.2f%%)",
        "  Decoded with different content    : %d  (%.2f%%)":
            "  Decodificati con altro contenuto  : %d  (%.2f%%)",
        "  Decoded with corrupt header       : %d  (%.2f%%)":
            "  Decod. con intestazione corrotta  : %d  (%.2f%%)",
        "  Missing (not decoded)             : %d  (%.2f%%)":
            "  Mancanti (non decodificati)       : %d  (%.2f%%)",
        "  Extra (ESP32 only, not an error)  : %d": "  Extra (solo ESP32, non è errore)  : %d",
        "RESULT: no packets were decoded by multimon-ng - nothing to compare.":
            "RISULTATO: multimon-ng non ha decodificato alcun pacchetto; non c'è nulla da confrontare.",
        "WARNING: `stdbuf` not found (package coreutils). multimon-ng's output will be\n  block-buffered on the pipe, so its packets may arrive in bursts and be\n  timestamped late, which shows up as spurious NOT DECODED verdicts.\n":
            "ATTENZIONE: `stdbuf` non trovato (pacchetto coreutils). L'output di multimon-ng\n  sarà bufferizzato a blocchi sulla pipe, quindi i pacchetti possono arrivare a\n  raffiche e con marca temporale tardiva, il che appare come verdetti\n  NON DECODIFICATO spuri.\n",
        "Waiting %.1f s for the ESP32 to be ready ...":
            "Attesa di %.1f s perché l'ESP32 sia pronto ...",
        "  WARNING: no data received from the serial port yet. The firmware may be quiet until it hears/sends something; continuing.":
            "  ATTENZIONE: nessun dato ricevuto finora dalla porta seriale. Il firmware può restare silenzioso finché non sente o trasmette qualcosa; si continua.",
        "got %r": "ottenuto %r",
        "offset=%.3f": "scarto=%.3f",
        "width=%.3f": "larghezza=%.3f",
        "a probe over a silent wav set terminates instead of looping":
            "un sondaggio su un insieme di wav muto termina invece di ciclare",
        "the whole search terminates on a silent wav set":
            "la ricerca completa termina con un insieme di wav muto",
        "Options (same flags as the command line)":
            "Opzioni (gli stessi flag della riga di comando)",
        "Start": "Avvia",
        "Stop": "Ferma",
        "Clear consoles": "Pulisci console",
        "Reset defaults": "Ripristina predefiniti",
        "Idle": "Inattivo",
        "Language:": "Lingua:",
        "Running ...": "In esecuzione ...",
        "Calibration": "Calibrazione",
        "Test": "Test",
        "Live": "In tempo reale",
        "Total packets: %d   |   multimon-ng: %d   |   ESP32 decoded: %d   |   ESP32 missed: %d of %d (%s)":
            "Pacchetti totali: %d   |   multimon-ng: %d   |   ESP32 decodificati: %d   |   ESP32 persi: %d su %d (%s)",
        "wav %d/%d [%s]: %s": "wav %d/%d [%s]: %s",
        "wav [%s]: %s": "wav [%s]: %s",
        "Stopping ...": "Arresto in corso ...",
        "Test esp32idf_APRS with a battery of real-APRS WAV files, using multimon-ng as the reference decoder.":
            "Testa esp32idf_APRS con una batteria di file WAV di APRS reale, usando multimon-ng come decodificatore di riferimento.",
        "language of the messages, the help and the GUI: en (English), es (Spanish), it (Italian). Default: the system language, or English when the system language is none of these three.":
            "lingua dei messaggi, dell'aiuto e dell'interfaccia grafica: en (inglese), es (spagnolo), it (italiano). Predefinito: la lingua di sistema, o inglese se la lingua di sistema non è una di queste tre.",
        "directory with the .wav files (default: current directory)":
            "directory con i file .wav (predefinito: la directory corrente)",
        "playback gain applied to the ESP32 leg only (default 1.0). Used as the starting point for auto-volume calibration unless --no_auto_volume is given.":
            "guadagno di riproduzione applicato solo al ramo dell'ESP32 (predefinito 1.0). Usato come punto di partenza della calibrazione automatica del volume, salvo che sia indicato --no_auto_volume.",
        "bring every wav to -1 dBFS in the play chain (sox 'gain -n -1') so one gain is valid across recordings made at different levels, and gains above 1.0 stop meaning 'clip inside sox'":
            "porta ogni wav a -1 dBFS nella catena di riproduzione (sox 'gain -n -1'), così un solo guadagno vale per registrazioni fatte a livelli diversi e i guadagni sopra 1.0 non significano più 'clipping dentro sox'",
        "do not estimate the ESP32-vs-multimon-ng latency skew; compare raw timestamps instead":
            "non stimare lo scarto di latenza tra ESP32 e multimon-ng; confronta invece le marche temporali grezze",
        "run the built-in unit tests (no hardware, no audio) and exit":
            "esegue i test unitari interni (senza hardware né audio) ed esce",
        "skip the auto-volume calibration pass and use --volume as-is for the whole run":
            "salta la fase di calibrazione automatica del volume e usa --volume così com'è per tutta l'esecuzione",
        "seconds to wait after opening the serial port (default 4)":
            "secondi di attesa dopo l'apertura della porta seriale (predefinito 4)",
        "pause between files in seconds (default 1)":
            "pausa tra i file, in secondi (predefinito 1)",
        "an ESP32 packet answers a multimon-ng packet only if it arrives within this many seconds of it; a packet the ESP32 has not reported after this time is shown as NOT DECODED (default 5)":
            "un pacchetto dell'ESP32 risponde a uno di multimon-ng solo se arriva entro questi secondi; un pacchetto che l'ESP32 non ha riportato dopo tale tempo viene mostrato come NON DECODIFICATO (predefinito 5)",
        "do not play audio to the sound card (only run multimon-ng; useful to dry-run the parser)":
            "non riprodurre audio sulla scheda audio (esegue solo multimon-ng; utile per provare il parser a vuoto)",
        "extra multimon-ng arguments, e.g. '-A' (quoted)":
            "argomenti extra per multimon-ng, ad es. '-A' (tra virgolette)",
        "open a graphical front-end: every flag in a form at the top, and below it a split console (left: program output, right: raw unfiltered serial data from the ESP32)":
            "apre l'interfaccia grafica: tutti i flag in un modulo in alto e sotto una console divisa (sinistra: output del programma; destra: dati grezzi dalla seriale dell'ESP32)",
        "Found %d wav file(s) in %s": "Trovati %d file wav in %s",
        "DRY RUN (--no_play): only multimon-ng runs; serial port and sound card are NOT used, so ESP32 results below are not meaningful.":
            "PROVA A VUOTO (--no_play): viene eseguito solo multimon-ng; la porta seriale e la scheda audio non sono usate, quindi i risultati dell'ESP32 qui sotto non sono significativi.",
        "pyserial is required:  pip install pyserial":
            "è necessario pyserial:  pip install pyserial",
        "  -> measured esp32 latency vs multimon-ng: %+.2f s":
            "  -> latenza misurata dell'esp32 rispetto a multimon-ng: %+.2f s",
        "    ! NOT DECODED by ESP32: %s": "    ! NON DECODIFICATO dall'ESP32: %s",
        "  -- Packet loss ------------------------------------------------":
            "  -- Perdita di pacchetti ---------------------------------------",
        "  This file  : lost %d of %d (%.2f%%)   not correct %d of %d (%.2f%%)":
            "  Questo file: persi %d su %d (%.2f%%)   non corretti %d su %d (%.2f%%)",
        "  Cumulative : lost %d of %d (%.2f%%)   not correct %d of %d (%.2f%%)   [%d file(s)]":
            "  Cumulativo : persi %d su %d (%.2f%%)   non corretti %d su %d (%.2f%%)   [%d file]",
        "  This file  : multimon-ng decoded no packets - nothing to measure":
            "  Questo file: multimon-ng non ha decodificato pacchetti - niente da misurare",
        "file": "file",
        "diff": "div",
        "hdr": "int",
        "  Playback gain used for this test  : %.3f  (%+.1f dB)":
            "  Guadagno di riproduzione usato    : %.3f  (%+.1f dB)",
        "  ESP32 latency vs multimon-ng      : %+.2f s (median of %d file(s))":
            "  Latenza ESP32 vs multimon-ng      : %+.2f s (mediana di %d file)",
        "Missing required program(s): %s\n": "Programma/i richiesto/i mancante/i: %s\n",
        "  [probe %2d, budget %.1f/%d] gain=%.3f (%+5.1f dB)  mm=%d ok=%d diff=%d hdr=%d miss=%d extra=%d  score=%.1f%%  clip=%.2f/pkt":
            "  [sondaggio %2d, budget %.1f/%d] guadagno=%.3f (%+5.1f dB)  mm=%d ok=%d div=%d int=%d mancanti=%d extra=%d  punteggio=%.1f%%  clip=%.2f/pacch",
        "  Plateau: %+.1f .. %+.1f dB (%d tied point(s) of %d probed); best raw score %.1f%%":
            "  Plateau: %+.1f .. %+.1f dB (%d punto/i a pari merito su %d sondati); miglior punteggio grezzo %.1f%%",
        "  NOTE: more than 6 dB of attenuation was needed. The hardware level into the ESP32 ADC is too hot - turn the RX trimmer (or the radio's volume) down and re-run, so the bench can work near 0 dB.":
            "  NOTA: sono serviti più di 6 dB di attenuazione. Il livello hardware che entra nell'ADC dell'ESP32 è troppo alto: abbassa il trimmer RX (o il volume della radio) e ripeti la prova, così il banco lavora vicino a 0 dB.",
        "  serial is alive (%d console line(s) so far).":
            "  la seriale è attiva (%d riga/righe di console finora).",
        "start %.2f converges to the plateau centre within budget (%.1f/%d batches, %d probes)":
            "da %.2f converge al centro del plateau entro il budget (%.1f/%d lotti, %d sondaggi)",
        "chose %+.1f dB at cost %.1f": "ha scelto %+.1f dB a un costo di %.1f",
        "%d playback call(s)": "%d chiamata/e di riproduzione",
        "SELFTEST FAILED: %d of the checks above did not pass":
            "AUTOTEST FALLITO: %d dei controlli sopra non sono passati",
        "--gui needs tkinter.  Debian/Ubuntu: sudo apt install python3-tk\n":
            "--gui richiede tkinter.  Debian/Ubuntu: sudo apt install python3-tk\n",
        "Language": "Lingua",
        "Stop the running test before changing the language.":
            "Ferma il test in corso prima di cambiare lingua.",
        "Finished (exit code %s)": "Terminato (codice di uscita %s)",
        "\n[gui] finished, exit code %s\n": "\n[gui] terminato, codice di uscita %s\n",
        "ESP32 console serial port (default: %s)":
            "porta seriale della console dell'ESP32 (predefinito: %s)",
        "serial speed, 8N1 (default: %d)": "velocità della seriale, 8N1 (predefinito: %d)",
        "dB below the clipping threshold to fall back to when no plateau could be scored (default %.0f)":
            "dB sotto la soglia di clipping a cui ripiegare quando nessun plateau ha potuto essere valutato (predefinito %.0f)",
        "lowest gain the search may use (default %.2f)":
            "guadagno minimo che la ricerca può usare (predefinito %.2f)",
        "highest gain the search may use (default %.2f)":
            "guadagno massimo che la ricerca può usare (predefinito %.2f)",
        "passes over the wav set before a calibration probe gives up (default %d)":
            "passaggi sull'insieme di wav prima che un sondaggio di calibrazione si arrenda (predefinito %d)",
        "number of packets to test per volume try during auto-volume calibration - counts both multimon-ng packets and ESP32-only ones multimon-ng missed (default %d)":
            "numero di pacchetti da testare per ogni volume durante la calibrazione automatica: conta sia i pacchetti di multimon-ng sia quelli visti solo dall'ESP32 che multimon-ng ha perso (predefinito %d)",
        "search budget for the auto-volume pass, in batches of --auto_volume_batch packets (default %d). Cheap 8-packet clipping probes cost a fraction of a batch, full scoring probes cost one each.":
            "budget di ricerca della fase di volume automatico, in lotti di --auto_volume_batch pacchetti (predefinito %d). I sondaggi economici di clipping da 8 pacchetti costano una frazione di lotto, quelli completi di punteggio uno ciascuno.",
        "seconds to keep listening after each file (default %.1f)":
            "secondi di ascolto dopo ogni file (predefinito %.1f)",
        "--auto_volume_batch must be >= 1 (got %d)\n":
            "--auto_volume_batch deve essere >= 1 (ricevuto %d)\n",
        "--auto_volume_max_rounds must be >= 1 (got %d)\n":
            "--auto_volume_max_rounds deve essere >= 1 (ricevuto %d)\n",
        "--max_passes must be >= 1 (got %d)\n": "--max_passes deve essere >= 1 (ricevuto %d)\n",
        "--volume_min must be > 0 and < --volume_max (got %g and %g)\n":
            "--volume_min deve essere > 0 e < --volume_max (ricevuti %g e %g)\n",
        "--volume must be within [%g, %g] (got %g)\n":
            "--volume deve essere compreso in [%g, %g] (ricevuto %g)\n",
        "--match_window must be > 0 (got %g)\n":
            "--match_window deve essere > 0 (ricevuto %g)\n",
        "wav_dir not found: %s\n": "wav_dir non trovata: %s\n",
        "No .wav files in %s\n": "Nessun file .wav in %s\n",
        "None of the selected --wav_files were found as .wav files in %s\n":
            "Nessuno dei --wav_files selezionati è stato trovato come file "
            ".wav in %s\n",
        "directory with the .wav files (default: current directory)":
            "cartella con i file .wav (predefinito: cartella corrente)",
        "comma-separated list of .wav file names (relative to "
        "--wav_dir) to test; empty means every .wav file found in "
        "--wav_dir (default: empty). In the GUI this is the file "
        "selector next to --wav_dir.":
            "elenco di nomi di file .wav separati da virgole (relativi a "
            "--wav_dir) da testare; vuoto significa tutti i file .wav "
            "trovati in --wav_dir (predefinito: vuoto). Nella GUI è il "
            "selettore file accanto a --wav_dir.",
        "--wav_files": "--wav_files",
        "wav files found in wav_dir (select one or more to test; none = all)":
            "file wav trovati in wav_dir (selezionane uno o più da testare; "
            "nessuno = tutti)",
        "%d selected of %d": "%d selezionato/i su %d",
        "no .wav files found": "nessun file .wav trovato",
        "Refresh": "Aggiorna",
        "Select all": "Seleziona tutto",
        "Select none": "Deseleziona tutto",
        "Serial: %s @ %d 8N1   Audio: %s": "Seriale: %s @ %d 8N1   Audio: %s",
        "\nInterrupted - reporting what has been tested so far.":
            "\nInterrotto: viene riportato quanto testato finora.",
        "\nDRY RUN finished: multimon-ng decoded %d packet(s) in %d file(s).":
            "\nPROVA A VUOTO terminata: multimon-ng ha decodificato %d pacchetto/i in %d file.",
        "%06d [multimon  --:--.-] NOT DECODED": "%06d [multimon  --:--.-] NON DECODIFICATO",
        "       [esp32 only      %s] %s": "       [solo esp32      %s] %s",
        "      ! DECODED BUT DIFFERENT": "      ! DECODIFICATO MA DIVERSO",
        "\n[mm] multimon-ng did not exit within 60 s - killing it\n":
            "\n[mm] multimon-ng non è uscito entro 60 s: viene terminato\n",
        "<probe %+.1f dB>": "<sondaggio %+.1f dB>",
        "  No clipping seen up to %+.1f dB (gain %.3f) - the hardware level into the ADC may be too low; check the RX trimmer.":
            "  Nessun clipping osservato fino a %+.1f dB (guadagno %.3f): il livello hardware verso l'ADC potrebbe essere troppo basso; controlla il trimmer RX.",
        "  No packets decoded during calibration at any level - falling back to %.3f (%+.1f dB, threshold - %.0f dB).":
            "  Nessun pacchetto decodificato durante la calibrazione a nessun livello: si ripiega su %.3f (%+.1f dB, soglia - %.0f dB).",
        "  Probe budget spent; stopping the plateau sweep (raise it with --auto_volume_max_rounds).":
            "  Budget dei sondaggi esaurito; la scansione del plateau si ferma (aumentalo con --auto_volume_max_rounds).",
        "  No usable score data - using threshold - %.0f dB = %.3f (%+.1f dB).":
            "  Nessun dato di punteggio utilizzabile: si usa soglia - %.0f dB = %.3f (%+.1f dB).",
        "  NOTE: more than 6 dB of boost was needed. The hardware level into the ESP32 ADC is too low - turn the RX trimmer up and re-run. Boosting digitally also amplifies the sound card's own noise floor.":
            "  NOTA: sono serviti più di 6 dB di guadagno. Il livello hardware verso l'ADC dell'ESP32 è troppo basso: alza il trimmer RX e ripeti la prova. Amplificare in digitale amplifica anche il rumore della scheda audio.",
        "  PASS  %s": "  OK    %s",
        "  FAIL  %s  %s": "  FALL  %s  %s",
        "run_one_wav called %d times - no pass limit":
            "run_one_wav chiamata %d volte: nessun limite di passaggi",
        "Cannot open a display for --gui: %s\n":
            "Impossibile aprire un display per --gui: %s\n",
        "Invalid option": "Opzione non valida",
        "Quit": "Esci",
        "A test is still running. Stop it and quit?":
            "Un test è ancora in corso. Fermarlo e uscire?",
        "      ! PAYLOAD OK BUT HEADER CORRUPT": "      ! PAYLOAD OK MA INTESTAZIONE CORROTTA",
        "       [esp32     --:--.-] NOT DECODED": "       [esp32     --:--.-] NON DECODIFICATO",
        "\n[audio] player failed (rc=%s): %s\n": "\n[audio] il player è fallito (rc=%s): %s\n",
        "\n[gui] stopped by user\n": "\n[gui] fermato dall'utente\n",
        "Cannot open serial port %s: %s\n": "Impossibile aprire la porta seriale %s: %s\n",
        "       [progress %s / %s] multimon=%d  ok=%d  not-decoded=%d  different=%d  (serial lines seen: %d)":
            "       [avanzamento %s / %s] multimon=%d  ok=%d  non-decodificati=%d  diversi=%d  (righe seriali viste: %d)",
        "      probe incomplete: %d/%d packet(s) after %d pass(es) over the wav set - check the audio routing and the files":
            "      sondaggio incompleto: %d/%d pacchetto/i dopo %d passaggio/i sull'insieme di wav; controlla il routing audio e i file",
        "      score fell %.0f points below the best - the lower knee is past, no need to go quieter":
            "      il punteggio è sceso di %.0f punti sotto il migliore: il ginocchio inferiore è superato, inutile scendere ancora",
        "%s: %r is not a valid %s": "%s: %r non è un %s valido",
        "\n[gui] unexpected error:\n": "\n[gui] errore imprevisto:\n",
        "\n[serial] read error: %s\n": "\n[seriale] errore di lettura: %s\n",
        "  <- OVER-RANGE":
            "  <- FUORI SCALA",
        "  Over-range at %+.1f dB (gain %.3f): no probe will go that high again; stepping down in %.2f dB steps":
            "  Fuori scala a %+.1f dB (guadagno %.3f): nessun sondaggio risalirà fin lì; si scende a passi di %.2f dB",
        "  Still over-range at the lowest gain allowed, %.3f (%+.1f dB): the hardware level into the ADC is far too hot - turn the RX trimmer (or the PC volume) down and run again.":
            "  Ancora fuori scala al guadagno minimo consentito, %.3f (%+.1f dB): il livello hardware in ingresso all'ADC è troppo alto; abbassa il trimmer RX (o il volume del PC) e rilancia.",
        "  Descent budget spent while still over-range at %+.1f dB - using %+.1f dB (%.0f dB below it). Turn the RX trimmer down, or raise --auto_volume_max_rounds / --clip_step_db.":
            "  Budget di discesa esaurito con fuori scala ancora a %+.1f dB: si usa %+.1f dB (%.0f dB sotto). Abbassa il trimmer RX, o aumenta --auto_volume_max_rounds / --clip_step_db.",
        "AUTO-VOLUME CALIBRATION (over-range ceiling + plateau centre)":
            "CALIBRAZIONE AUTOMATICA DEL VOLUME (tetto di fuori scala + centro del plateau)",
        "  Start gain %.3f (%+.1f dB), range %.3f..%.3f, budget %d probe(s), %d packet(s) per scoring probe, %.2f dB steps below over-range":
            "  Guadagno iniziale %.3f (%+.1f dB), intervallo %.3f..%.3f, budget %d sondaggio/i, %d pacchetto/i per sondaggio di punteggio, passi di %.2f dB sotto il fuori scala",
        "  Highest level without over-range: %+.1f dB (gain %.3f)":
            "  Livello più alto senza fuori scala: %+.1f dB (guadagno %.3f)",
        "      over-range during a scoring probe at %+.1f dB - this level is dropped and nothing at or above it is played again":
            "      fuori scala durante un sondaggio di punteggio a %+.1f dB: il livello viene scartato e nulla viene più riprodotto a quel livello o sopra",
        "  Chosen gain: %.3f (%+.1f dB), %.1f dB below the highest level without over-range":
            "  Guadagno scelto: %.3f (%+.1f dB), %.1f dB sotto il livello più alto senza fuori scala",
        "over-range warnings per packet a level may produce and still count as clean (default %.2f: a single warning marks the level as clipping, and no probe goes that high again)":
            "avvisi di fuori scala per pacchetto che un livello può produrre restando pulito (predefinito %.2f: un solo avviso marca il livello come clipping, e nessun sondaggio risale fin lì)",
        "once over-range has been reported, the search never raises the gain again and steps DOWN by this many dB per probe until the warnings stop (default %.2f)":
            "una volta segnalato il fuori scala, la ricerca non alza più il guadagno e SCENDE di questi dB a ogni sondaggio finché gli avvisi cessano (predefinito %.2f)",
        "--clip_rate must be in [0, 1) (got %g)\n":
            "--clip_rate deve essere in [0, 1) (ricevuto %g)\n",
        "--clip_step_db must be > 0 and <= %g (got %g)\n":
            "--clip_step_db deve essere > 0 e <= %g (ricevuto %g)\n",
        "\nAuto-volume calibration interrupted - proceeding with the best level known to be free of over-range so far: %.3f (%+.1f dB).":
            "\nCalibrazione automatica del volume interrotta: si prosegue con il miglior livello finora noto senza fuori scala: %.3f (%+.1f dB).",
        "Over-range ceiling":
            "Tetto di fuori scala",
        "start %.2f: no probe is played at or above a level that reported over-range":
            "partenza %.2f: nessun sondaggio viene riprodotto a un livello che ha segnalato fuori scala o sopra",
        "history %s":
            "storico %s",
        "below an over-range level the gain steps down by --clip_step_db (%.2f dB)":
            "sotto un livello in fuori scala il guadagno scende a passi di --clip_step_db (%.2f dB)",
        "steps %r":
            "passi %r",
        "one over-range warning in a scoring batch lowers the ceiling and drops that level":
            "un solo avviso di fuori scala in un lotto di punteggio abbassa il tetto e scarta quel livello",
        "ceiling %s, chose %+.1f dB":
            "tetto %s, scelto %+.1f dB",
        "an interrupted search falls back below the over-range ceiling, not to the start gain":
            "una ricerca interrotta ripiega sotto il tetto di fuori scala, non sul guadagno iniziale",
        "safe %+.1f dB, ceiling %+.1f dB":
            "sicuro %+.1f dB, tetto %+.1f dB",
        "a silent wav set never raises the gain":
            "un insieme di wav silenzioso non alza mai il guadagno",
        "probed %s":
            "sondato %s",
        "a descent that runs out of budget still ends below the over-range ceiling":
            "una discesa che esaurisce il budget finisce comunque sotto il tetto di fuori scala",
        "integer": "intero",
        "number": "numero",
        "Every volume probe restarts the wav list":
            "Ogni sondaggio di volume riparte dall'inizio della lista dei wav",
        "a new volume replays the list from the first file, in order":
            "un nuovo volume riproduce la lista dal primo file, in ordine",
        # PipeWire routing
        "\n[audio] monitor player failed (rc=%s): %s\n":
            "\n[audio] il riproduttore del monitor è fallito (rc=%s): %s\n",
        "\n[audio] the player was still running %.0f s after the end of the file - killed it (was the output unplugged?)\n":
            "\n[audio] il riproduttore era ancora attivo %.0f s dopo la fine del file: terminato (l'uscita è stata scollegata?)\n",
        "%r matches more than one PipeWire output: %s":
            "%r corrisponde a più di un'uscita PipeWire: %s",
        "--monitor_device and --audio_device are the same PipeWire output (%s): the monitor stream would be mixed into the ESP32 signal\n":
            "--monitor_device e --audio_device sono la stessa uscita PipeWire (%s): il flusso del monitor verrebbe mixato nel segnale dell'ESP32\n",
        "--monitor_volume must be in [0, 1] (got %g)\n":
            "--monitor_volume deve essere in [0, 1] (ricevuto %g)\n",
        "Give --audio_device / --monitor_device the node name (it does not change between reboots), the serial, or a unique part of the description.":
            "Passa a --audio_device / --monitor_device il nome del nodo (non cambia tra un riavvio e l'altro), il numero di serie o una parte univoca della descrizione.",
        "Monitor: %s   (stream volume %.2f)":
            "Monitor: %s   (volume del flusso %.2f)",
        "Monitor: none":
            "Monitor: nessuno",
        "PipeWire has no audio output (sink)":
            "PipeWire non ha nessuna uscita audio (sink)",
        "PipeWire has no default output set - name the output explicitly":
            "PipeWire non ha un'uscita predefinita: indica l'uscita esplicitamente",
        "PipeWire output (sink) to listen on while the test runs, e.g. headphones; it plays exactly what the ESP32 gets. Same forms as --audio_device, 'default' for the PipeWire default output (default: no monitor)":
            "uscita PipeWire (sink) su cui ascoltare durante il test, ad es. le cuffie; riproduce esattamente ciò che riceve l'ESP32. Stesse forme di --audio_device, 'default' per l'uscita predefinita di PipeWire (predefinito: nessun monitor)",
        "PipeWire output (sink) wired to the ESP32 audio input: its node name, serial, or a unique part of its description (default: the PipeWire default output). See --list_audio.":
            "uscita PipeWire (sink) collegata all'ingresso audio dell'ESP32: il suo nome di nodo, il numero di serie o una parte univoca della descrizione (predefinito: l'uscita predefinita di PipeWire). Vedi --list_audio.",
        "PipeWire outputs (sinks) - * marks the default:":
            "Uscite PipeWire (sink) - * indica quella predefinita:",
        "PipeWire routing":
            "Instradamento PipeWire",
        "PipeWire stream volume of the monitor, 0..1 (default 0.5). Only the monitor: the ESP32 level is set by --volume alone.":
            "volume del flusso PipeWire del monitor, 0..1 (predefinito 0.5). Solo il monitor: il livello dell'ESP32 è fissato unicamente da --volume.",
        "an ambiguous or unknown output is rejected, never guessed":
            "un'uscita ambigua o sconosciuta viene rifiutata, mai indovinata",
        "an empty device and 'default' both give the PipeWire default output":
            "un dispositivo vuoto e 'default' danno entrambi l'uscita predefinita di PipeWire",
        "an output is found by node name, serial, id, GUI label and description":
            "un'uscita si trova per nome del nodo, numero di serie, id, etichetta della GUI e descrizione",
        "description":
            "descrizione",
        "list the PipeWire outputs (sinks) and exit":
            "elenca le uscite PipeWire (sink) ed esce",
        "no PipeWire output matches %r":
            "nessuna uscita PipeWire corrisponde a %r",
        "node name":
            "nome del nodo",
        "play chain: sox renders a file, pw-cat plays it pinned to the sink, no fallback":
            "catena di riproduzione: sox genera un file e pw-cat lo riproduce vincolato al sink, senza ripiego",
        "pw-dump did not answer - is PipeWire running?":
            "pw-dump non ha risposto: PipeWire è in esecuzione?",
        "pw-dump failed (rc=%s): %s":
            "pw-dump è fallito (rc=%s): %s",
        "pw-dump not found - install the PipeWire tools (Debian/Ubuntu: sudo apt install pipewire-bin)":
            "pw-dump non trovato: installa gli strumenti di PipeWire (Debian/Ubuntu: sudo apt install pipewire-bin)",
        "pw-dump printed something that is not JSON: %s":
            "pw-dump ha stampato qualcosa che non è JSON: %s",
        "pw-dump: only sinks are listed, and the default output is found":
            "pw-dump: vengono elencati solo i sink e si trova l'uscita predefinita",
        "serial":
            "seriale",
        "sox could not render %s (rc=%s): %s":
            "sox non è riuscito a generare %s (rc=%s): %s",
        # Direwolf (second reference decoder)
        "no free TCP port found for Direwolf's KISS server":
            'nessuna porta TCP libera trovata per il server KISS di Direwolf',
        '%s: %d of %d  (%.2f%%, 95%% CI %.1f..%.1f%%)':
            '%s: %d su %d  (%.2f%%, IC 95%% %.1f..%.1f%%)',
        'REFERENCE COMPARISON (Direwolf)':
            'CONFRONTO DEI RIFERIMENTI (Direwolf)',
        'Direwolf: KISS, AX.25 and FCS':
            'Direwolf: KISS, AX.25 e FCS',
        "CRC-16/X.25 check value of '123456789' is 0x906E":
            "il valore di verifica CRC-16/X.25 di '123456789' è 0x906E",
        'a frame followed by its FCS leaves the X.25 residue 0x0F47':
            'una trama seguita dal suo FCS lascia il residuo X.25 0x0F47',
        "AX.25 address field: SSID, path and the H bit ('*') decode":
            "campo indirizzi AX.25: vengono decodificati SSID, percorso e bit H ('*')",
        'KISS: escapes, split reads, garbage and empty frames':
            'KISS: escape, letture spezzate, dati spuri e trame vuote',
        'malformed address field and non-APRS frames are rejected':
            'i campi indirizzo malformati e le trame non APRS vengono scartati',
        'the same frame from Direwolf, multimon-ng and the ESP32 compares equal':
            "la stessa trama da Direwolf, multimon-ng e l'ESP32 risulta uguale",
        'APRS plausibility: a real position passes, noise fails':
            'plausibilità APRS: una posizione reale passa, il rumore no',
        'an automatic KISS port is inside the range Direwolf accepts':
            "una porta KISS automatica è nell'intervallo accettato da Direwolf",
        'Direwolf config: receive only, CRC-strict, KISS on one port only':
            'configurazione di Direwolf: solo ricezione, CRC rigoroso, KISS su una sola porta',
        'ClusterMatcher (three columns)':
            'ClusterMatcher (tre colonne)',
        'Direwolf and ESP32 latency skews are learned independently':
            "gli scarti di latenza di Direwolf e dell'ESP32 vengono appresi separatamente",
        'ClusterMatcher agrees with LiveMatcher on multimon-ng vs ESP32':
            'ClusterMatcher concorda con LiveMatcher su multimon-ng vs ESP32',
        'statistics: conflicts are not scored, ESP32-only frames are never successes':
            "statistica: i conflitti non vengono valutati e le trame solo dell'ESP32 non sono mai successi",
        'the multimon-ng reference counts packets exactly as before':
            'il riferimento multimon-ng conta i pacchetti esattamente come prima',
        '  References: both %d   mm-only %d   dw-only %d   conflicts %d':
            '  Riferimenti: entrambi %d   solo mm %d   solo dw %d   conflitti %d',
        '  Direwolf %s, %d Hz, modem profile %s, FIX_BITS %d':
            '  Direwolf %s, %d Hz, profilo modem %s, FIX_BITS %d',
        '  Reference for the exit code       : %s':
            "  Riferimento del codice d'uscita   : %s",
        '  Packets multimon-ng / Direwolf    : %d / %d':
            '  Pacchetti multimon-ng / Direwolf  : %d / %d',
        '  Union / both references           : %d / %d':
            '  Unione / entrambi i riferimenti   : %d / %d',
        '  multimon-ng only / Direwolf only  : %d / %d':
            '  Solo multimon-ng / solo Direwolf  : %d / %d',
        '  Reference conflicts               : %d':
            '  Conflitti tra riferimenti         : %d',
        'RESULT: the selected reference decoded no packets - nothing to compare.':
            "RISULTATO: il riferimento scelto non ha decodificato alcun pacchetto; non c'è nulla da confrontare.",
        'Direwolf as a second reference decoder: auto (use it when it is installed), on (required), off (legacy two-column bench). Default: auto':
            'Direwolf come secondo decodificatore di riferimento: auto (usato se installato), on (obbligatorio), off (banco classico a due colonne). Predefinito: auto',
        'reference that decides the exit code and the auto-volume score: multimon (legacy, default), direwolf, or union (a packet either reference decoded)':
            "riferimento che decide il codice d'uscita e il punteggio del volume automatico: multimon (classico, predefinito), direwolf o union (un pacchetto decodificato da uno qualsiasi dei due)",
        "Direwolf modem profile appended to 'MODEM 1200', e.g. 'A+' or 'E+' (default: Direwolf's own default)":
            "profilo modem di Direwolf aggiunto a 'MODEM 1200', ad es. 'A+' o 'E+' (predefinito: quello di Direwolf)",
        "TCP port of Direwolf's KISS server, 1024..49151 (default 0: pick a free port for every file)":
            'porta TCP del server KISS di Direwolf, 1024..49151 (predefinito 0: viene scelta una porta libera per ogni file)',
        "extra Direwolf arguments, e.g. '-P E+' (quoted)":
            "argomenti extra per Direwolf, ad es. '-P E+' (tra virgolette)",
        'write every multimon-ng / Direwolf / ESP32 row to this CSV file (needs Direwolf)':
            'scrive ogni riga multimon-ng / Direwolf / ESP32 in questo file CSV (richiede Direwolf)',
        'NOTE: --report_csv needs Direwolf; no CSV will be written\n':
            'NOTA: --report_csv richiede Direwolf; non verrà scritto alcun CSV\n',
        '! REFERENCE CONFLICT - multimon-ng and Direwolf disagree; check normalisation':
            '! CONFLITTO DI RIFERIMENTI - multimon-ng e Direwolf non concordano; controlla la normalizzazione',
        '! only multimon-ng decoded it (Direwolf did not)':
            '! lo ha decodificato solo multimon-ng (Direwolf no)',
        '! ESP32 NOT DECODED':
            "! NON DECODIFICATO dall'ESP32",
        '  Direwolf decoded %d packet(s)   (CRC-strict, FIX_BITS=0)':
            '  Direwolf ha decodificato %d pacchetto/i   (CRC rigoroso, FIX_BITS=0)',
        '  Direwolf decoded %d packet(s)   (NOT CRC-strict, FIX_BITS=%d)':
            '  Direwolf ha decodificato %d pacchetto/i   (CRC NON rigoroso, FIX_BITS=%d)',
        '  ESP32 vs Direwolf : OK %d  DIFFERENT %d  HDR-CORRUPT %d  NOT DECODED %d':
            '  ESP32 vs Direwolf : OK %d  DIVERSI %d  INTEST-CORROTTA %d  NON DECODIFICATI %d',
        '  ESP32-only (unconfirmed): %d   of which implausible: %d':
            '  Solo ESP32 (non confermati): %d   di cui non plausibili: %d',
        '  -> measured Direwolf latency vs multimon-ng: %+.2f s':
            '  -> latenza misurata di Direwolf rispetto a multimon-ng: %+.2f s',
        '  WARNING: Direwolf is NOT a strict CRC reference: FIX_BITS=%d':
            '  ATTENZIONE: Direwolf NON è un riferimento CRC rigoroso: FIX_BITS=%d',
        'union':
            'unione',
        '  ESP32 correct vs multimon-ng      ':
            '  ESP32 corretto vs multimon-ng     ',
        '  ESP32 correct vs Direwolf         ':
            '  ESP32 corretto vs Direwolf        ',
        '  ESP32 correct vs union            ':
            '  ESP32 corretto vs unione          ',
        '  ESP32-only, unconfirmed           : %d  (implausible: %d)':
            '  Solo ESP32, non confermati        : %d  (non plausibili: %d)',
        '  Direwolf latency vs multimon-ng   : %+.2f s (median of %d file(s))':
            '  Latenza Direwolf vs multimon-ng   : %+.2f s (mediana di %d file)',
        'row: %s':
            'riga: %s',
        'Direwolf, when installed, runs as a second, CRC-strict reference and the report shows multimon-ng / Direwolf / ESP32 for every packet.':
            'Direwolf, se installato, funge da secondo riferimento con CRC rigoroso e il rapporto mostra multimon-ng / Direwolf / ESP32 per ogni pacchetto.',
        'sample rate of the audio fed to Direwolf (default %d)':
            "frequenza di campionamento dell'audio inviato a Direwolf (predefinito %d)",
        'Direwolf FIX_BITS (default %d). Anything above 0 lets Direwolf repair frames with a bad CRC, so it stops being a strict reference.':
            'FIX_BITS di Direwolf (predefinito %d). Qualsiasi valore sopra 0 gli permette di riparare trame con CRC errato, quindi smette di essere un riferimento rigoroso.',
        "seconds to wait for Direwolf's KISS port to open (default %.1f)":
            "secondi di attesa per l'apertura della porta KISS di Direwolf (predefinito %.1f)",
        '--dw_rate must be >= 8000 (got %d)\n':
            '--dw_rate deve essere >= 8000 (ricevuto %d)\n',
        '--dw_fix_bits must be in [0, %d] (got %d)\n':
            '--dw_fix_bits deve essere in [0, %d] (ricevuto %d)\n',
        '--dw_kiss_port must be 0 or in [%d, %d] (got %d)\n':
            '--dw_kiss_port deve essere 0 o in [%d, %d] (ricevuto %d)\n',
        '--dw_start_timeout must be > 0 (got %g)\n':
            '--dw_start_timeout deve essere > 0 (ricevuto %g)\n',
        '--reference %s needs Direwolf, but --direwolf is off\n':
            '--reference %s richiede Direwolf, ma --direwolf è off\n',
        '--reference %s needs Direwolf, which is not available\n':
            '--reference %s richiede Direwolf, che non è disponibile\n',
        'Direwolf: %d Hz, modem profile %s, FIX_BITS %d, KISS port %s, reference for the exit code: %s':
            "Direwolf: %d Hz, profilo modem %s, FIX_BITS %d, porta KISS %s, riferimento per il codice d'uscita: %s",
        '\nDirewolf failed - reporting what has been tested so far.':
            '\nDirewolf ha fallito: viene riportato quanto testato finora.',
        '! ESP32 ONLY - unconfirmed by any CRC-checked reference (plausible: %s)':
            '! SOLO ESP32 - non confermato da alcun riferimento con CRC verificato (plausibile: %s)',
        '! multimon-ng did not decode it (Direwolf did)':
            "! multimon-ng non l'ha decodificato (Direwolf sì)",
        '(default)':
            '(predefinito)',
        'Total packets: %d   |   multimon-ng: %d   |   Direwolf: %d   |   ESP32 decoded: %d   |   ESP32 missed: %d of %d (%s)   |   conflicts: %d   |   ESP32-only: %d':
            'Pacchetti totali: %d   |   multimon-ng: %d   |   Direwolf: %d   |   ESP32 decodificati: %d   |   ESP32 persi: %d su %d (%s)   |   conflitti: %d   |   solo ESP32: %d',
        '--direwolf on, but the direwolf program was not found.  Debian/Ubuntu: sudo apt install direwolf\n':
            '--direwolf on, ma il programma direwolf non è stato trovato.  Debian/Ubuntu: sudo apt install direwolf\n',
        'NOTE: direwolf not found - running with multimon-ng as the only reference (install it for the three-column report: sudo apt install direwolf)\n':
            'NOTA: direwolf non trovato; si esegue con multimon-ng come unico riferimento (installalo per il rapporto a tre colonne: sudo apt install direwolf)\n',
        '  Direwolf runs too: the two references are compared, both fed at %.0fx real time.':
            '  Gira anche Direwolf: i due riferimenti vengono confrontati, entrambi alimentati a %.0fx il tempo reale.',
        'CSV report: %d row(s) written to %s':
            'Rapporto CSV: %d riga/righe scritte in %s',
        'DRY RUN: Direwolf decoded %d packet(s).':
            'PROVA A VUOTO: Direwolf ha decodificato %d pacchetto/i.',
        'Cannot start Direwolf: %s':
            'Impossibile avviare Direwolf: %s',
        'Direwolf exited during start-up (rc=%s): %s':
            "Direwolf è uscito durante l'avvio (rc=%s): %s",
        "Direwolf's KISS port %d did not open within %.1f s (raise --dw_start_timeout): %s":
            'la porta KISS %d di Direwolf non si è aperta entro %.1f s (aumenta --dw_start_timeout): %s',
        'yes':
            'sì',
        'NO':
            'NO',
        '! ESP32 DECODED BUT DIFFERENT':
            "! DECODIFICATO DALL'ESP32 MA DIVERSO",
        '! ESP32 PAYLOAD OK BUT HEADER CORRUPT':
            "! PAYLOAD DELL'ESP32 OK MA INTESTAZIONE CORROTTA",
        'auto':
            'auto',
        'Cannot write the CSV report %s: %s\n':
            'Impossibile scrivere il rapporto CSV %s: %s\n',
        '\n[dw] Direwolf did not exit within %.0f s after the end of the audio - killing it\n':
            "\n[dw] Direwolf non è uscito entro %.0f s dalla fine dell'audio: viene terminato\n",
        '       [progress %s / %s] multimon=%d  direwolf=%d  ok=%d  not-decoded=%d  different=%d  (serial lines seen: %d)':
            '       [avanzamento %s / %s] multimon=%d  direwolf=%d  ok=%d  non-decodificati=%d  diversi=%d  (righe seriali viste: %d)',
    },
}

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover
    sys.stderr.write(T("pyserial is required:  pip install pyserial") + "\n")
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
# The ADC input has no clamp diodes and "RX audio is over-range" is the
# firmware saying the level is wrong, so by default ONE warning is enough to
# mark a level as clipping. --clip_rate can relax that (warnings per packet
# tolerated), but 0 is the safe default.
CLIP_RATE_THRESHOLD = 0.0
COARSE_STEP_DB = 6.0          # upward step, used ONLY while no over-range has been seen
CLIP_STEP_DB = 0.5            # downward step once over-range has been seen
HUNT_BUDGET_SHARE = 0.5       # at most this share of the budget goes to the threshold hunt
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
# Direwolf (second, CRC-strict reference decoder)
# --------------------------------------------------------------------------

DW_RATE = 48000               # Direwolf's demodulators are tuned for 44.1/48 kHz
DW_FIX_BITS = 0               # 0 = only frames with a valid FCS are reported
DW_FIX_BITS_MAX = 5           # highest FIX_BITS value accepted by --dw_fix_bits
DW_START_TIMEOUT = 5.0        # seconds to wait for Direwolf's KISS port
DW_EXIT_TIMEOUT = 10.0        # seconds Direwolf may take to exit after stdin EOF
DW_LOG_LINES = 200            # Direwolf console lines kept for error messages
# In a --no_play dry run with Direwolf the two reference legs are paced to
# this many times real time on a common clock: fed flat out, each decoder
# would print at its own speed and the timestamps could not be compared.
DRY_RUN_SPEED = 4.0

REFERENCES = ("multimon", "direwolf", "union")
DW_MODES = ("auto", "on", "off")

# Cell symbols of the three-column report.
CELL_OK, CELL_DIFF, CELL_HDR, CELL_NONE, CELL_UNCONF = "=", "D", "H", "-", "?"
# Row classes of the three-column report.
ROW_ALL = "ALL"
ROW_ESP_MISS = "ESP_MISS"
ROW_ESP_DIFF = "ESP_DIFF"
ROW_ESP_HDR = "ESP_HDR"
ROW_MM_ONLY = "MM_ONLY_REF"
ROW_DW_ONLY = "DW_ONLY_REF"
ROW_CONFLICT = "REF_CONFLICT"
ROW_ESP_ONLY = "ESP_ONLY"

# --------------------------------------------------------------------------
# Live output
# --------------------------------------------------------------------------

_print_lock = threading.Lock()

# --------------------------------------------------------------------------
# Cooperative stop (used by the GUI's Stop button)
# --------------------------------------------------------------------------
#
# On the command line Ctrl-C raises KeyboardInterrupt in the main thread. The
# GUI runs the test in a worker thread, where nothing can raise that exception
# from outside *while the thread is blocked in a C-level wait*: a pending
# asynchronous exception is only delivered when the interpreter next executes
# Python bytecode, so `player.wait()` or a long `time.sleep()` would swallow it
# until they returned - a whole WAV file, or the --tail / --pause interval,
# later. Instead, Stop sets this event and kills the child processes; every
# blocking site below polls it (sleep_or_stop) or is woken by the kill, and
# raises KeyboardInterrupt ITSELF, so the existing Ctrl-C clean-up and the
# "Interrupted - reporting what has been tested so far" summary run unchanged.

_STOP = threading.Event()
_LIVE_PROCS = []            # type: List[subprocess.Popen]
_LIVE_LOCK = threading.Lock()


def track_proc(p: "subprocess.Popen") -> "subprocess.Popen":
    """Register a child process so request_stop() can kill it."""
    with _LIVE_LOCK:
        _LIVE_PROCS[:] = [q for q in _LIVE_PROCS if q.poll() is None]
        _LIVE_PROCS.append(p)
    if _STOP.is_set():                 # stop requested while it was starting
        try:
            p.kill()
        except Exception:
            pass
    return p


def check_cancel() -> None:
    """Raise KeyboardInterrupt if a stop was requested."""
    if _STOP.is_set():
        raise KeyboardInterrupt()


def sleep_or_stop(seconds: float) -> None:
    """time.sleep() that returns at once - by raising KeyboardInterrupt - when
    a stop is requested, instead of finishing the whole interval."""
    if _STOP.wait(max(0.0, seconds)):
        raise KeyboardInterrupt()


def request_stop() -> None:
    """Ask the running test to stop NOW: flag it and kill every child process
    (player, sox, multimon-ng) so anything blocked waiting on them wakes up."""
    _STOP.set()
    with _LIVE_LOCK:
        procs = list(_LIVE_PROCS)
    for p in procs:
        if p.poll() is None:
            try:
                p.kill()
            except Exception:
                pass


# --------------------------------------------------------------------------
# Live packet counters (read by the GUI status line)
# --------------------------------------------------------------------------
#
# run_one_wav() bumps these the moment each packet / verdict is known, so the
# GUI can show running totals without parsing the console text. They are
# reset at the start of each phase (auto-volume calibration, then the real
# test), so the numbers shown always belong to the phase that is running.
# Definitions, consistent with print_summary() / print_loss_resume():
#   multimon  = packets multimon-ng decoded (the reference)
#   extra     = packets only the ESP32 decoded
#   total     = multimon + extra (every distinct packet either decoder heard)
#   esp       = ok + different + hdr-corrupt + extra (ESP32 produced a frame)
#   missed    = multimon packets the ESP32 did NOT decode
#   missed %  = missed / total, i.e. over every packet either decoder heard.
# With Direwolf also running:
#   dw        = packets Direwolf decoded
#   conflict  = rows where multimon-ng and Direwolf disagree (REF_CONFLICT)
#   esp_only  = rows only the ESP32 decoded (unconfirmed)

class LiveStats:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self.reset("")

    def reset(self, phase: str) -> None:
        with self._lock:
            self.phase = phase
            self.mm = self.ok = self.diff = self.missing = self.extra = 0
            self.dw = self.conflict = self.esp_only = 0
            self.dw_active = getattr(self, "dw_active", False)
            self.file_n = self.file_total = 0
            self.file_name = ""
            self.file_duration = self.file_elapsed = 0.0
            self.gen = getattr(self, "gen", 0) + 1

    def add(self, mm: int = 0, ok: int = 0, diff: int = 0,
            missing: int = 0, extra: int = 0, dw: int = 0,
            conflict: int = 0, esp_only: int = 0) -> None:
        with self._lock:
            self.dw += dw
            self.conflict += conflict
            self.esp_only += esp_only
            self.mm = max(0, self.mm + mm)
            self.ok += ok
            self.diff += diff
            self.missing += missing
            self.extra += extra
            self.gen += 1

    def set_dw_active(self, active: bool) -> None:
        with self._lock:
            self.dw_active = active
            self.gen += 1

    def set_file(self, n: int, total: int, name: str) -> None:
        """Records which wav is currently playing, so the GUI status bar can
        show it. During the real test pass n/total is the 1-based position in
        the wav set (progress through the list); during auto-volume
        calibration there is no such position - probes revisit files in any
        order and possibly several times - so n and total are both passed as
        0 and only the filename is shown. Resets the elapsed/duration clock
        for the new file; run_one_wav fills those in as it plays."""
        with self._lock:
            self.file_n = n
            self.file_total = total
            self.file_name = name
            self.file_duration = self.file_elapsed = 0.0
            self.gen += 1

    def set_duration(self, duration: float) -> None:
        """The total length of the wav now playing, in seconds."""
        with self._lock:
            self.file_duration = duration
            self.gen += 1

    def set_elapsed(self, elapsed: float) -> None:
        """How far into the current wav playback is, in seconds. Called
        repeatedly (from run_one_wav's ticker) while the file plays."""
        with self._lock:
            self.file_elapsed = elapsed
            self.gen += 1

    def snapshot(self) -> dict:
        with self._lock:
            resolved = self.ok + self.diff + self.missing
            return {"gen": self.gen, "phase": self.phase,
                    "total": self.mm + self.extra, "mm": self.mm,
                    "esp": self.ok + self.diff + self.extra,
                    "missed": self.missing, "resolved": resolved,
                    "file_n": self.file_n, "file_total": self.file_total,
                    "file_name": self.file_name,
                    "file_duration": self.file_duration,
                    "file_elapsed": self.file_elapsed,
                    "dw_active": self.dw_active, "dw": self.dw,
                    "conflict": self.conflict, "esp_only": self.esp_only}


_LIVE_STATS = LiveStats()


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
    # FCS of the frame as it was on the air (CRC-16/X.25), when the source
    # gives exact bytes (Direwolf over KISS). Never part of comparisons.
    fcs: Optional[int] = field(default=None, compare=False)

    def key_header(self) -> str:
        return "%s>%s,%s" % (self.src, self.dst, ",".join(self.path))

    def same_content(self, other: "Packet") -> bool:
        return self.key_header() == other.key_header() and self.info == other.info


@dataclass
class Row:
    """One physical transmission as seen by the three decoders: the unit of
    the three-column (multimon-ng / Direwolf / ESP32) report."""
    idx: int
    t: float                       # anchor time (multimon-ng clock), monotonic s
    members: dict                  # source -> (arrival time, Packet)
    cells: dict                    # source -> CELL_* symbol
    cls: str                       # ROW_* class
    consensus: Packet              # the content the row is judged against
    fcs: Optional[int] = None      # on-air FCS, when Direwolf has the frame


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
    # Direwolf (three-column mode only; empty / zero otherwise).
    dw_packets: List[Packet] = field(default_factory=list)
    rows: List[Row] = field(default_factory=list)
    dw_offset: float = 0.0     # measured Direwolf - multimon-ng latency skew, seconds
    t0: float = 0.0            # monotonic start of this file's timing window


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
        line = line.rstrip("\r\n")
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
# Direwolf: KISS, AX.25, FCS and plausibility (pure functions)
# --------------------------------------------------------------------------
#
# Direwolf is the second reference decoder. It hears the same audio as
# multimon-ng, but its frames are taken from its KISS TCP port, which gives
# the exact AX.25 bytes: no pretty-printing to undo, no escaped characters,
# and - with FIX_BITS 0 - only frames whose FCS was valid on the air.

KISS_FEND, KISS_FESC, KISS_TFEND, KISS_TFESC = 0xC0, 0xDB, 0xDC, 0xDD
KISS_MAX_FRAME = 4096          # runaway guard: longer than any AX.25 frame


class KissDeframer:
    """Turns a KISS byte stream (as read from TCP, in arbitrary pieces) into
    complete frames: (port, command, payload). Handles FESC escapes, frames
    split across reads, back-to-back FENDs and garbage before the first FEND."""

    def __init__(self) -> None:
        self._buf = bytearray()
        self._in = False
        self._esc = False

    def feed(self, data: bytes) -> List[Tuple[int, int, bytes]]:
        out = []  # type: List[Tuple[int, int, bytes]]
        for b in bytearray(data):
            if b == KISS_FEND:
                if self._in and self._buf:
                    first = self._buf[0]
                    out.append((first >> 4, first & 0x0F, bytes(self._buf[1:])))
                self._buf = bytearray()
                self._in = True
                self._esc = False
                continue
            if not self._in:
                continue                      # garbage before the first FEND
            if self._esc:
                self._esc = False
                if b == KISS_TFEND:
                    b = KISS_FEND
                elif b == KISS_TFESC:
                    b = KISS_FESC
                self._buf.append(b)           # (a bad escape keeps the byte as is)
            elif b == KISS_FESC:
                self._esc = True
            else:
                self._buf.append(b)
            if len(self._buf) > KISS_MAX_FRAME:
                self._buf = bytearray()
                self._in = False
        return out


def kiss_encode(payload: bytes, port: int = 0, cmd: int = 0) -> bytes:
    """KISS-frame one payload (used by --selftest)."""
    body = bytearray([((port & 0x0F) << 4) | (cmd & 0x0F)])
    for b in bytearray(payload):
        if b == KISS_FEND:
            body += bytes([KISS_FESC, KISS_TFEND])
        elif b == KISS_FESC:
            body += bytes([KISS_FESC, KISS_TFESC])
        else:
            body.append(b)
    return bytes([KISS_FEND]) + bytes(body) + bytes([KISS_FEND])


def _fcs_table() -> List[int]:
    table = []
    for i in range(256):
        crc = i
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
        table.append(crc)
    return table


_FCS_TABLE = _fcs_table()


def fcs16_x25(data: bytes) -> int:
    """CRC-16/X.25, the AX.25 frame check sequence (poly 0x1021 reflected,
    init 0xFFFF, xorout 0xFFFF). fcs16_x25(b"123456789") == 0x906E."""
    crc = 0xFFFF
    for b in bytearray(data):
        crc = (crc >> 8) ^ _FCS_TABLE[(crc ^ b) & 0xFF]
    return crc ^ 0xFFFF


def ax25_decode(frame: bytes) -> Optional[Tuple[str, str, List[str], int, int, bytes]]:
    """(src, dst, path, control, pid, info) from an AX.25 frame WITHOUT its
    FCS (which is what KISS carries), or None when the address field is
    malformed. A path entry whose H bit is set gets a trailing '*'. pid is -1
    for frames that have none."""
    frame = bytes(frame)
    addrs = []  # type: List[Tuple[str, int, bool]]
    i = 0
    while True:
        if i + 7 > len(frame):
            return None
        a = bytearray(frame[i:i + 7])
        i += 7
        chars = [c >> 1 for c in a[:6]]
        if any(c < 0x20 or c >= 0x7F for c in chars):
            return None
        call = "".join(chr(c) for c in chars).rstrip(" ")
        if not call or " " in call:
            return None
        addrs.append((call, (a[6] >> 1) & 0x0F, bool(a[6] & 0x80)))
        if a[6] & 0x01:
            break
        if len(addrs) >= 10:                 # dst + src + 8 digipeaters
            return None
    if len(addrs) < 2 or i >= len(frame):
        return None
    control = frame[i]
    if (control & 0xEF) == 0x03:            # UI frame: control, PID, info
        if i + 1 >= len(frame):
            return None
        pid, info = frame[i + 1], frame[i + 2:]
    else:
        pid, info = -1, frame[i + 1:]

    def name(call: str, ssid: int) -> str:
        return "%s-%d" % (call, ssid) if ssid else call

    dst = name(addrs[0][0], addrs[0][1])
    src = name(addrs[1][0], addrs[1][1])
    path = [name(c, s) + ("*" if h else "") for c, s, h in addrs[2:]]
    return src, dst, path, control, pid, info


def ax25_encode(src: str, dst: str, path: List[str], info: bytes,
                control: int = 0x03, pid: int = 0xF0) -> bytes:
    """Build an AX.25 UI frame without FCS (used by --selftest)."""
    calls = [dst, src] + list(path)
    out = bytearray()
    for n, c in enumerate(calls):
        h = c.endswith("*")
        c = c.rstrip("*")
        call, _sep, ssid = c.partition("-")
        field6 = (call.upper() + "      ")[:6]
        out += bytes(ord(ch) << 1 for ch in field6)
        last = 0x01 if n == len(calls) - 1 else 0x00
        out.append(0x60 | ((int(ssid or 0) & 0x0F) << 1) | (0x80 if h else 0x00) | last)
    out.append(control)
    out.append(pid)
    return bytes(out) + bytes(info)


def kiss_to_packet(frame: bytes) -> Optional[Packet]:
    """A Packet from one AX.25 frame received over KISS, or None if it is not
    an APRS-style UI frame with PID 0xF0 (the same scope multimon-ng's parser
    has). The on-air FCS is recomputed and stored in Packet.fcs."""
    dec = ax25_decode(frame)
    if dec is None:
        return None
    src, dst, path, control, pid, info = dec
    if (control & 0xEF) != 0x03 or pid != 0xF0:
        return None
    raw = "%s>%s%s:%s" % (src, dst, ("," + ",".join(path)) if path else "",
                          info_to_mm_view(info.rstrip(b"\r\n\x00")).decode("latin-1"))
    pkt = make_packet(src, dst, path, info, raw)
    pkt.fcs = fcs16_x25(frame)
    return pkt


# APRS data type identifiers (APRS 1.0.1 ch. 5, plus ',' = test data).
APRS_DTI = frozenset(b"!\"#$%'()*+,./:;<=>?@T[\\]_`{}")
CALL_RE = re.compile(r"^[A-Z0-9]{1,6}(-([0-9]|1[0-5]))?$")


def aprs_plausible(pkt: Packet) -> bool:
    """Cheap sanity check for frames only the ESP32 decoded. A frame that
    fails it is almost certainly a CRC-16 collision on noise: about 1 in
    65 536 random bit strings passes the FCS, and a multi-slicer demodulator
    offers the check many candidates."""
    calls = [pkt.src, pkt.dst] + list(pkt.path)
    if not all(CALL_RE.match(c) for c in calls):
        return False
    if len(pkt.path) > 8 or not pkt.info:
        return False
    return pkt.info[0] in APRS_DTI


def fmt_fcs(fcs: Optional[int]) -> str:
    return "----" if fcs is None else "%04X" % fcs


@dataclass
class DwSetup:
    """How Direwolf is run (from the command line), plus what it reported."""
    rate: int = DW_RATE
    profile: str = ""
    fix_bits: int = DW_FIX_BITS
    kiss_port: int = 0             # 0 = pick a free port for every file
    start_timeout: float = DW_START_TIMEOUT
    extra: List[str] = field(default_factory=list)
    version: str = ""              # read from Direwolf's banner


def build_dw_conf(setup: DwSetup, port: int) -> str:
    """Direwolf configuration: receive only, audio from stdin, no AGW, KISS
    on one TCP port only. 'KISSPORT 0' first removes the built-in 8001, which
    would otherwise clash with a Direwolf already running on the machine."""
    return "\n".join([
        "# test_aprs_wavs - generated, receive only",
        "ADEVICE stdin null",
        "ACHANNELS 1",
        "ARATE %d" % setup.rate,
        "CHANNEL 0",
        ("MODEM 1200 %s" % setup.profile).strip(),
        "FIX_BITS %d" % setup.fix_bits,
        "AGWPORT 0",
        "KISSPORT 0",
        "KISSPORT %d" % port,
        ""])


def build_dw_cmd(conf_path: str, setup: DwSetup) -> List[str]:
    """Direwolf reading raw s16 mono audio from stdin ('-' last)."""
    return (["direwolf", "-c", conf_path, "-r", str(setup.rate), "-n", "1", "-b", "16",
             "-t", "0", "-q", "hd"] + list(setup.extra) + ["-"])


def build_dw_sox_cmd(wav: str, rate: int) -> List[str]:
    """Leg D: the source wav as raw s16 mono at Direwolf's rate."""
    return ["sox", "-q", "-V0", wav, "-t", "raw", "-r", str(rate),
            "-e", "signed", "-b", "16", "-c", "1", "-"]


# Direwolf only accepts KISS ports in the registered range; the kernel's
# ephemeral ports (usually 32768..60999) are partly outside it, so "bind to
# port 0" cannot be used to find one.
DW_PORT_MIN, DW_PORT_MAX = 1024, 49151


def pick_free_port() -> int:
    """A TCP port in Direwolf's accepted range that was free a moment ago
    (there is a tiny race between this check and Direwolf binding it)."""
    import random
    rng = random.Random()
    for _ in range(200):
        port = rng.randint(20000, DW_PORT_MAX)
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            s.bind(("0.0.0.0", port))
            return port
        except OSError:
            continue
        finally:
            s.close()
    raise DirewolfError(T("no free TCP port found for Direwolf's KISS server"))


# --------------------------------------------------------------------------
# Serial reader (runs in a thread for the whole test)
# --------------------------------------------------------------------------


# Set by the GUI before a run; every SerialCollector created afterwards
# forwards each raw chunk it reads to this callable (bytes -> None).
_RAW_SERIAL_SINK = None  # type: Optional[Callable[[bytes], None]]


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
        # Optional callback fed with EVERY chunk read from the port, exactly as
        # received (no line splitting, no ANSI stripping, no parsing, no
        # filtering). Used by the GUI's right-hand "serial" pane.
        self.raw_sink = _RAW_SERIAL_SINK

    def run(self) -> None:
        while not self._halt.is_set():
            try:
                chunk = self.ser.read(4096)
            except (serial.SerialException, OSError) as exc:
                if not self._halt.is_set():
                    sys.stderr.write(T("\n[serial] read error: %s\n") % exc)
                break
            except (TypeError, ValueError):
                # stop() closed the port under a read() that was already in
                # flight: pyserial then fails with TypeError (fd is None).
                # That is a normal shutdown, not an error - but if we were NOT
                # asked to halt it is a genuine fault and must not be hidden.
                if self._halt.is_set():
                    break
                raise
            if not chunk:
                continue
            self.alive.set()
            sink = self.raw_sink
            if sink is not None:
                try:
                    sink(chunk)
                except Exception:
                    pass          # a broken viewer must never kill the reader
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


# --------------------------------------------------------------------------
# PipeWire output routing
# --------------------------------------------------------------------------
#
# Audio goes to PipeWire directly, with pw-cat (package pipewire-bin), one
# stream per output:
#
#   sox <wav> remix/normalise/gain -> temporary WAV
#       pw-cat --target <ESP32 sink>   <temporary WAV>      (test leg)
#       pw-cat --target <monitor sink> <temporary WAV>      (monitor leg, optional)
#
# Both legs play the SAME rendered file, so the monitor hears exactly what the
# ESP32 hears; only the monitor's PipeWire stream volume (--monitor_volume)
# differs. The ESP32 leg always runs at stream volume 1.0: its level is set
# only by the sox gain, which the calibration controls. Each leg is its own
# pw-cat, so a monitor that stalls or dies never holds up the ESP32 leg.
#
# Why a file and not a pipe: pw-cat reads its input through libsndfile, and on
# a pipe (PipeWire 1.0) it plays part of the container header as audio - a
# near full-scale click at the start of every file, which the firmware reports
# as over-range and which would corrupt the auto-volume calibration. pw-cat
# 1.0 has no raw mode either. Rendering costs about half a second per 10 min
# of audio and ~5 MB per minute in the temp directory, deleted after each play.
#
# Outputs are found with pw-dump (the whole graph as JSON) and ALWAYS resolved
# to a concrete node before anything plays. pw-cat is told not to fall back:
# without node.dont-fallback PipeWire would silently send the audio to the
# default output when the target is missing - which, for a test bench, would
# mean feeding the monitor device instead of the ESP32 without any error.

PLAYER_GRACE_SECONDS = 20.0   # pw-cat still running this long after the file: stuck

_PW_STREAM_PROPS = (
    "{ node.dont-fallback = true node.dont-reconnect = true node.dont-move = true "
    "state.restore-props = false state.restore-target = false "
    "application.name = \"test_aprs_wavs\" media.name = \"%s\" }")


@dataclass
class PwSink:
    """One PipeWire audio output (media.class Audio/Sink)."""
    id: int
    serial: int
    name: str                 # node.name: the stable identifier
    description: str          # node.description: what mixers show
    nick: str = ""

    def label(self) -> str:
        """How the GUI lists it; resolve_sink() accepts this text back."""
        return "%s [%s]" % (self.description or self.name, self.name)


def parse_pw_dump(objs: list) -> Tuple[List[PwSink], Optional[str]]:
    """Pick the audio sinks and the default sink name out of pw-dump's JSON.

    Pure function (no PipeWire needed), so --selftest can check it."""
    sinks = []                # type: List[PwSink]
    default_name = None       # type: Optional[str]
    for o in objs if isinstance(objs, list) else []:
        if not isinstance(o, dict):
            continue
        typ = o.get("type", "")
        if typ == "PipeWire:Interface:Node":
            props = (o.get("info") or {}).get("props") or {}
            if props.get("media.class") != "Audio/Sink":
                continue
            try:
                serial = int(props.get("object.serial", -1))
            except (TypeError, ValueError):
                serial = -1
            sinks.append(PwSink(id=int(o.get("id", -1)), serial=serial,
                                name=str(props.get("node.name", "")),
                                description=str(props.get("node.description", "") or ""),
                                nick=str(props.get("node.nick", "") or "")))
        elif typ == "PipeWire:Interface:Metadata":
            if (o.get("props") or {}).get("metadata.name") != "default":
                continue
            # The configured default wins over the one currently in use.
            found = {}
            for m in o.get("metadata") or []:
                if m.get("key") in ("default.audio.sink", "default.configured.audio.sink"):
                    val = m.get("value")
                    if isinstance(val, str):
                        try:
                            val = json.loads(val)
                        except ValueError:
                            val = {"name": val}
                    if isinstance(val, dict) and val.get("name"):
                        found[m["key"]] = str(val["name"])
            default_name = (found.get("default.audio.sink") or
                            found.get("default.configured.audio.sink") or default_name)
    sinks.sort(key=lambda k: k.serial)
    return sinks, default_name


def pw_list_sinks() -> Tuple[List[PwSink], Optional[str]]:
    """Ask the running PipeWire for its outputs. Raises RuntimeError."""
    if shutil.which("pw-dump") is None:
        raise RuntimeError(T("pw-dump not found - install the PipeWire tools "
                             "(Debian/Ubuntu: sudo apt install pipewire-bin)"))
    try:
        p = subprocess.run(["pw-dump", "-N"], capture_output=True, timeout=15)
    except subprocess.TimeoutExpired:
        raise RuntimeError(T("pw-dump did not answer - is PipeWire running?"))
    if p.returncode != 0:
        raise RuntimeError(T("pw-dump failed (rc=%s): %s") %
                           (p.returncode, p.stderr.decode("latin-1", "replace").strip()))
    try:
        objs = json.loads(p.stdout.decode("utf-8", "replace") or "[]")
    except ValueError as exc:
        raise RuntimeError(T("pw-dump printed something that is not JSON: %s") % exc)
    return parse_pw_dump(objs)


def resolve_sink(spec: Optional[str], sinks: List[PwSink],
                 default_name: Optional[str]) -> PwSink:
    """Turn what the user typed into one sink. Raises ValueError.

    Accepted, in this order: "default" / "auto" / "" (the PipeWire default
    output); the exact node.name; a serial or object id; the GUI's
    "description [node.name]" label; the exact description or nick (any
    case); and finally any UNIQUE case-insensitive fragment of the name,
    description or nick. An ambiguous fragment is an error, never a guess."""
    spec = (spec or "").strip()
    if not sinks:
        raise ValueError(T("PipeWire has no audio output (sink)"))
    if spec.lower() in ("", "default", "auto", "@default_sink@"):
        for k in sinks:
            if k.name == default_name:
                return k
        raise ValueError(T("PipeWire has no default output set - name the output explicitly"))
    for k in sinks:
        if k.name == spec:
            return k
    if spec.isdigit():
        n = int(spec)
        hits = [k for k in sinks if k.serial == n] or [k for k in sinks if k.id == n]
        if hits:
            return hits[0]
    m = re.match(r"^.*\[([^\[\]]+)\]\s*$", spec)
    if m:
        for k in sinks:
            if k.name == m.group(1):
                return k
    low = spec.lower()
    hits = [k for k in sinks if low in (k.description.lower(), k.nick.lower())]
    if len(hits) == 1:
        return hits[0]
    hits = [k for k in sinks
            if low in k.name.lower() or low in k.description.lower() or low in k.nick.lower()]
    if len(hits) == 1:
        return hits[0]
    if not hits:
        raise ValueError(T("no PipeWire output matches %r") % spec)
    raise ValueError(T("%r matches more than one PipeWire output: %s") %
                     (spec, ", ".join(k.name for k in hits)))


@dataclass
class AudioRoute:
    """Where the audio goes: the ESP32 sink and, optionally, a monitor sink."""
    test: PwSink
    monitor: Optional[PwSink] = None
    monitor_volume: float = 0.5


def build_play_cmd(wav: str, volume: float, normalise: bool, out: str) -> List[str]:
    """Build the sox command that renders what the ESP32 (and the monitor)
    will hear into the stereo s16 WAV `out`, for pw-cat to play.

      * `remix 1 1` puts the signal on both channels, so a left-only or
        right-only cable to the ESP32 hears the same thing;
      * the level is applied with `gain <dB>` instead of the linear `vol`,
        so the amount of gain is explicit in the unit the problem is actually
        posed in, and sox's headroom handling applies;
      * with `normalise`, every file is brought to -1 dBFS first, so one
        volume is valid across a set of recordings made at different levels;
      * the sample rate is left as it is: PipeWire resamples to the output.
    """
    cmd = ["sox", "-q", "-V0", wav,
           "-t", "wav", "-e", "signed", "-b", "16", "-c", "2", out,
           "remix", "1", "1"]
    if normalise:
        cmd += ["gain", "-n", "-1"]
    cmd += ["gain", "%.2f" % to_db(volume)]
    return cmd


def build_pwcat_cmd(sink: PwSink, stream_volume: float, media_name: str,
                    path: str) -> List[str]:
    """pw-cat playing one file, pinned to one sink."""
    return ["pw-cat", "--playback", "--target", sink.name,
            "--media-role", "Production",
            "--volume", "%.3f" % stream_volume,
            "-P", _PW_STREAM_PROPS % media_name,
            path]


_RENDER_DIR = None  # type: Optional[str]


def render_dir() -> str:
    """The run's private temp directory (rendered wavs, Direwolf configs);
    removed at exit."""
    global _RENDER_DIR
    import tempfile
    import atexit
    if _RENDER_DIR is None or not os.path.isdir(_RENDER_DIR):
        _RENDER_DIR = tempfile.mkdtemp(prefix="test_aprs_wavs-")
        atexit.register(shutil.rmtree, _RENDER_DIR, True)
    return _RENDER_DIR


def render_for_play(wav: str, volume: float, normalise: bool) -> str:
    """Render the play chain into a temporary WAV and return its path. The
    caller deletes it. Raises RuntimeError when sox fails."""
    import tempfile
    fd, out = tempfile.mkstemp(suffix=".wav", dir=render_dir())
    os.close(fd)
    p = track_proc(subprocess.Popen(build_play_cmd(wav, volume, normalise, out),
                                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE))
    _o, err = p.communicate()
    if p.returncode != 0:
        try:
            os.unlink(out)
        except OSError:
            pass
        check_cancel()                  # killed by Stop, not a sox failure
        raise RuntimeError(T("sox could not render %s (rc=%s): %s") %
                           (os.path.basename(wav), p.returncode,
                            err.decode("latin-1", "replace").strip()))
    return out


def start_player(path: str, sink: PwSink, stream_volume: float,
                 media_name: str) -> "subprocess.Popen":
    return track_proc(subprocess.Popen(build_pwcat_cmd(sink, stream_volume, media_name, path),
                                       stdout=subprocess.DEVNULL,
                                       stderr=subprocess.PIPE))


# --------------------------------------------------------------------------
# Direwolf process and KISS reader (one of each per file)
# --------------------------------------------------------------------------

DW_VERSION_RE = re.compile(r"Dire ?Wolf\b.*?\bversion\s+(\S+)", re.IGNORECASE)


class DirewolfError(RuntimeError):
    """Direwolf could not be started (or its KISS port never opened)."""


class DirewolfCollector(threading.Thread):
    """Reads Direwolf's KISS TCP port and stores every APRS UI frame with the
    monotonic time it arrived at - the Direwolf counterpart of
    SerialCollector."""

    def __init__(self, sock: "socket.socket") -> None:
        super().__init__(daemon=True)
        self.sock = sock
        self.sock.settimeout(0.2)
        self.lock = threading.Lock()
        self.packets = []   # type: List[Tuple[float, Packet]]
        self.frames_seen = 0
        self._halt = threading.Event()
        self._deframer = KissDeframer()

    def run(self) -> None:
        while not self._halt.is_set():
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                break
            if not data:
                break                        # Direwolf closed the connection
            now = time.monotonic()
            for port, cmd, payload in self._deframer.feed(data):
                if cmd != 0 or port != 0:
                    continue                 # not a data frame of radio channel 0
                self.frames_seen += 1
                pkt = kiss_to_packet(payload)
                if pkt is not None:
                    with self.lock:
                        self.packets.append((now, pkt))

    def items_from(self, index: int) -> Tuple[List[Tuple[float, Packet]], int]:
        with self.lock:
            return list(self.packets[index:]), len(self.packets)

    def stop(self, drain: float = 0.0) -> None:
        """Stop reading. With `drain` > 0, first give the reader that long to
        see the end of the connection by itself (frames still in flight)."""
        if drain > 0 and self.is_alive():
            self.join(timeout=drain)
        self._halt.set()
        try:
            self.sock.close()
        except Exception:
            pass
        if self.is_alive():
            self.join(timeout=2)


class DirewolfRun:
    """One Direwolf process for one file: generated config, the process
    itself (audio on stdin), a thread that keeps its console output drained
    (the last lines are kept for error messages, and the banner gives the
    version) and the KISS reader.

    start() returns only once the KISS port accepts connections, so the
    first audio sample is never fed to a decoder that is not listening yet."""

    def __init__(self, setup: DwSetup) -> None:
        self.setup = setup
        self.proc = None       # type: Optional[subprocess.Popen]
        self.col = None        # type: Optional[DirewolfCollector]
        self.port = 0
        self.log = collections.deque(maxlen=DW_LOG_LINES)
        self._drainer = None   # type: Optional[threading.Thread]

    def tail(self, n: int = 6) -> str:
        lines = [l for l in list(self.log) if l.strip()]
        return " | ".join(lines[-n:]) if lines else "-"

    def _drain(self) -> None:
        proc = self.proc
        if proc is None or proc.stdout is None:
            return
        try:
            for raw in iter(proc.stdout.readline, b""):
                line = ANSI_RE.sub(b"", raw).decode("latin-1", "replace").rstrip()
                if not line:
                    continue
                self.log.append(line)
                if not self.setup.version:
                    m = DW_VERSION_RE.search(line)
                    if m:
                        self.setup.version = m.group(1)
        except Exception:
            pass

    def start(self) -> None:
        try:
            self._start()
        except BaseException:
            self.kill()
            raise

    def _start(self) -> None:
        self.port = self.setup.kiss_port or pick_free_port()
        conf = os.path.join(render_dir(), "direwolf-%d.conf" % self.port)
        with open(conf, "w") as f:
            f.write(build_dw_conf(self.setup, self.port))
        try:
            self.proc = track_proc(subprocess.Popen(
                build_dw_cmd(conf, self.setup), stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT))
        except OSError as exc:
            raise DirewolfError(T("Cannot start Direwolf: %s") % exc)
        self._drainer = threading.Thread(target=self._drain, daemon=True)
        self._drainer.start()
        deadline = time.monotonic() + self.setup.start_timeout
        while True:
            check_cancel()
            if self.proc.poll() is not None:
                self._drainer.join(timeout=1)
                raise DirewolfError(T("Direwolf exited during start-up (rc=%s): %s") %
                                    (self.proc.returncode, self.tail()))
            try:
                sock = socket.create_connection(("127.0.0.1", self.port), timeout=0.2)
                break
            except OSError:
                pass
            if time.monotonic() > deadline:
                raise DirewolfError(T("Direwolf's KISS port %d did not open within %.1f s "
                                      "(raise --dw_start_timeout): %s") %
                                    (self.port, self.setup.start_timeout, self.tail()))
            sleep_or_stop(0.05)
        self.col = DirewolfCollector(sock)
        self.col.start()

    def finish(self) -> None:
        """Normal end of file: stdin has been closed by the feeder, so wait
        for Direwolf to decode what is left and exit, then let the KISS reader
        see the end of the connection. Never raises for a stuck process."""
        if self.proc is not None:
            try:
                self.proc.wait(timeout=DW_EXIT_TIMEOUT)
            except subprocess.TimeoutExpired:
                sys.stderr.write(T("\n[dw] Direwolf did not exit within %.0f s after the "
                                   "end of the audio - killing it\n") % DW_EXIT_TIMEOUT)
                self.proc.kill()
                try:
                    self.proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
        if self.col is not None:
            self.col.stop(drain=2.0)
        if self._drainer is not None:
            self._drainer.join(timeout=2)

    def kill(self) -> None:
        """Abnormal end (Stop, Ctrl-C, packet-count cut-off): kill everything.
        Idempotent; packets already collected stay available."""
        if self.proc is not None and self.proc.poll() is None:
            try:
                self.proc.kill()
            except Exception:
                pass
            try:
                self.proc.wait(timeout=5)
            except Exception:
                pass
        if self.col is not None:
            self.col.stop()


def print_sink_list(sinks: List[PwSink], default_name: Optional[str]) -> None:
    print(T("PipeWire outputs (sinks) - * marks the default:"))
    print("    %6s  %-44s %s" % (T("serial"), T("node name"), T("description")))
    for k in sinks:
        print("  %s %6d  %-44s %s" % ("*" if k.name == default_name else " ",
                                      k.serial, k.name, k.description))
    print(T("Give --audio_device / --monitor_device the node name (it does not change "
            "between reboots), the serial, or a unique part of the description."))


def clip_warning(wav: str, volume: float, normalise: bool) -> Optional[str]:
    """Return a warning when `volume` would clip this file inside sox."""
    if normalise:
        headroom = to_lin(-1.0)            # every file leaves the chain at -1 dBFS
    else:
        headroom = wav_peak(wav)
    if volume * headroom > 1.0:
        return (T("%s: gain %.3f (%+.1f dB) on a file peaking at %.3f would clip "
                  "inside sox (max usable gain %.3f). Use --normalise, or lower the "
                  "gain and raise the hardware level instead.") %
                (os.path.basename(wav), volume, to_db(volume), headroom, 1.0 / headroom))
    return None


def make_pump(src: "subprocess.Popen", dst: "subprocess.Popen", bytes_per_s: int,
              speed: Optional[float],
              common_start: Optional[List[Optional[float]]] = None) -> Callable[[], None]:
    """A thread body that copies raw audio from `src` (sox) into `dst` (a
    decoder's stdin), 0.1 s at a time.

    speed None  : as fast as the decoder takes it (legacy dry run);
    speed 1.0   : real time, so the decoder's output lines up with what the
                  ESP32 hears;
    speed N     : N x real time.
    Pacing uses its own start time unless `common_start[0]` holds a shared
    one, which is how two legs are kept on one clock. Both pipe ends are
    closed at the end: a decoder that died (or was killed by the packet-count
    cut-off) must not leave sox blocked writing into a pipe nobody drains."""
    def pump() -> None:
        assert src.stdout is not None and dst.stdin is not None
        sent = 0
        start_t = (common_start[0] if common_start and common_start[0] is not None
                   else time.monotonic())
        try:
            while True:
                chunk = src.stdout.read(bytes_per_s // 10)   # 0.1 s of audio
                if not chunk:
                    break
                dst.stdin.write(chunk)
                dst.stdin.flush()
                sent += len(chunk)
                if speed:
                    ahead = (sent / float(bytes_per_s)) / speed - (time.monotonic() - start_t)
                    if ahead > 0:
                        if _STOP.wait(ahead):     # stop requested: quit feeding
                            break
        except (BrokenPipeError, OSError, ValueError):
            pass
        finally:
            try:
                dst.stdin.close()
            except Exception:
                pass
            try:
                src.stdout.close()
            except Exception:
                pass
    return pump


def run_one_wav(res: FileResult, wav: str, route: Optional[AudioRoute],
                volume: float, tail: float, window: float,
                collector: SerialCollector, mm_extra: List[str],
                dry_run: bool,
                stop_at_mm_packets: Optional[int] = None,
                normalise: bool = False,
                offset_auto: bool = True,
                offset_seed: float = 0.0,
                abort_on_overrange: bool = False,
                dw: Optional[DwSetup] = None,
                reference: str = "multimon",
                dw_offset_seed: float = 0.0) -> bool:
    """Play `wav` once while decoding it with multimon-ng (and, when `dw` is
    given, with Direwolf too) and reading the ESP32 console. Every multimon-ng packet is printed together with the
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
    If `abort_on_overrange` is set, playback is also cut short within about
    a quarter of a second of the firmware reporting "RX audio is over-range":
    the calibration uses it so a level that is already known to be too hot
    does not keep over-driving the ADC for the rest of the file.

    With `dw`, Direwolf decodes the same source audio on its own leg, a
    ClusterMatcher groups the three decoders' frames into rows, and the live
    output shows one three-column row per transmission instead of the
    two-column lines. LiveMatcher still runs and still fills every legacy
    field of `res`, silently. `reference` selects which packets count
    towards `stop_at_mm_packets` (see ref_packet_count()).

    Returns True if a cutoff was hit, False if the file simply played to
    its natural end (or was interrupted by Ctrl-C).
    Raises DirewolfError when Direwolf cannot be started."""
    # Render what the ESP32 will hear BEFORE the timing window opens, so the
    # few hundred ms sox needs are not charged to this file.
    rendered = None  # type: Optional[str]
    if not dry_run:
        if route is None:
            raise ValueError("run_one_wav: no audio route")
        try:
            rendered = render_for_play(wav, volume, normalise)
        except RuntimeError as exc:
            sys.stderr.write("\n[audio] %s\n" % exc)
            res.duration = 0.0
            return False
    # Direwolf is started - and its KISS port confirmed open - BEFORE the
    # timing window opens, so its start-up time is not charged to this file.
    dw_run = None  # type: Optional[DirewolfRun]
    if dw is not None:
        dw_run = DirewolfRun(dw)
        try:
            dw_run.start()
        except BaseException:
            if rendered is not None:
                try:
                    os.unlink(rendered)
                except OSError:
                    pass
            raise
    # ESP32 lines are attributed to this file by the moment they arrive. The
    # window opens right now, before playback starts, so nothing the previous
    # file already resolved is re-counted - but a frame the previous file was
    # still demodulating when it ended CAN arrive after this t0 and will be
    # ingested here, normally as an "extra". That is why the inter-file
    # `--pause` and `--tail` matter: they drain the ESP32 before the next file
    # opens its window.
    t0 = time.monotonic()
    t_window_start = t0
    res.t0 = t0

    # Every leg is produced from the same source file by sox so that the
    # decoders receive identical audio, whatever the WAV's own format is
    # (stereo, 8/16/24 bit, 44.1/48 kHz ...).
    #
    #   leg A: rendered WAV -> pw-cat --target <ESP32 sink>   [real time]
    #   leg M: rendered WAV -> pw-cat --target <monitor sink> [real time, optional]
    #   leg B: sox <wav> -> raw 22050 Hz s16 mono -> multimon-ng
    #                                                     [paced to real time]
    #   leg D: sox <wav> -> raw --dw_rate s16 mono -> Direwolf stdin
    #          Direwolf -> KISS over TCP -> DirewolfCollector   [optional]
    #
    # Legs A and M are independent players (see "PipeWire output routing"):
    # the monitor can lag, stall or fail without touching the ESP32 leg.
    warn = clip_warning(wav, volume, normalise)
    if warn and not dry_run:
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

    player = None  # type: Optional[subprocess.Popen]   # pw-cat, ESP32 leg
    monitor = None  # type: Optional[subprocess.Popen]  # pw-cat, monitor leg
    play_procs = []  # type: List[subprocess.Popen]    # the pw-cat of A and M
    sox_p = None   # type: Optional[subprocess.Popen]
    mm_p = None    # type: Optional[subprocess.Popen]
    sox_dw = None  # type: Optional[subprocess.Popen]   # leg D feeder
    gt = None      # type: Optional[threading.Thread]
    done = threading.Event()
    duration = wav_duration(wav)
    _LIVE_STATS.set_duration(duration)
    matcher = LiveMatcher(window, offset_auto=offset_auto, offset=offset_seed)
    # Three-column matcher, only when Direwolf runs. In a dry run there is no
    # ESP32, so it compares the two references only.
    cluster = None  # type: Optional[ClusterMatcher]
    if dw_run is not None:
        cluster = ClusterMatcher(window, offset_auto=offset_auto, esp_offset=offset_seed,
                                 dw_offset=dw_offset_seed,
                                 sources=("mm", "dw") if dry_run else ("mm", "dw", "esp"))
    # Legacy two-column lines are printed only without Direwolf; with it the
    # same verdicts are still recorded, but the rows below are what is shown.
    lsay = say if cluster is None else (lambda _m: None)
    dw_ingested = [0]
    interrupted = False
    # Resume point in the collector's packet list (not a count of this file's
    # packets): everything already stored belongs to an earlier file.
    ingested = [collector.snapshot_index()]
    collector.file_t0 = t0
    overrange_base = collector.snapshot_overrange()[0] if abort_on_overrange else 0

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
        for p in play_procs + [sox_p, mm_p, sox_dw,
                               dw_run.proc if dw_run is not None else None]:
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
            reached = ref_packet_count(res, reference)
            should_stop = (stop_at_mm_packets is not None and not target_hit[0] and
                           reached >= stop_at_mm_packets)
            if should_stop:
                target_hit[0] = True
        if should_stop:
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
            _LIVE_STATS.add(extra=1)
            lsay(T("%06d [multimon  --:--.-] NOT DECODED") % n)
            lsay(T("       [esp32 only      %s] %s") % (mmss(t_esp - t0), ep.raw))
            check_stop()
            return
        lsay(T("%06d [multimon %s] %s") % (n, mmss(t_mm - t0), mp.raw))
        if kind == "ok":
            with res_lock:
                res.ok += 1
            _LIVE_STATS.add(ok=1)
            lsay(T("    OK [esp32    %s] %s") % (mmss(t_esp - t0), ep.raw))
        elif kind == "mismatch":
            with res_lock:
                res.mismatch.append((mp, ep))
            _LIVE_STATS.add(diff=1)
            lsay(T("       [esp32    %s] %s") % (mmss(t_esp - t0), ep.raw))
            lsay(T("      ! DECODED BUT DIFFERENT"))
        elif kind == "corrupt":
            with res_lock:
                res.corrupt.append((mp, ep))
            _LIVE_STATS.add(diff=1)
            lsay(T("       [esp32    %s] %s") % (mmss(t_esp - t0), ep.raw))
            lsay(T("      ! PAYLOAD OK BUT HEADER CORRUPT"))
        else:
            with res_lock:
                res.missing.append(mp)
            _LIVE_STATS.add(missing=1)
            lsay(T("       [esp32     --:--.-] NOT DECODED"))

    def emit_row(row: Row) -> None:
        """Print one three-column row and record it in `res`."""
        with res_lock:
            res.rows.append(row)
        _LIVE_STATS.add(conflict=int(row.cls == ROW_CONFLICT),
                        esp_only=int(row.cls == ROW_ESP_ONLY))
        print_row(row, t0, esp_used=not dry_run)

    def feed_and_step(now: float, final: bool = False) -> None:
        if not dry_run:
            items, ingested[0] = collector.items_from(ingested[0], t_window_start)
            for t, p in items:
                matcher.add_esp(t, p)
                if cluster is not None:
                    cluster.add("esp", t, p)
            for ev in matcher.step(now, final):
                show(ev)
        if cluster is not None and dw_run is not None and dw_run.col is not None:
            items, dw_ingested[0] = dw_run.col.items_from(dw_ingested[0])
            for t, p in items:
                with res_lock:
                    res.dw_packets.append(p)
                _LIVE_STATS.add(dw=1)
                cluster.add("dw", t, p)
            if items:
                check_stop()
            for row in cluster.step(now, final):
                emit_row(row)

    try:
        check_cancel()
        mm_p = track_proc(subprocess.Popen(mm_cmd, stdin=subprocess.PIPE,
                                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL))
        sox_p = track_proc(subprocess.Popen(sox_raw, stdout=subprocess.PIPE,
                                            stderr=subprocess.DEVNULL))

        # Feed multimon-ng from sox in REAL TIME (unless dry-running). This
        # keeps its decodes in step with what the ESP32 is hearing, so the
        # two decoders' packets are printed side by side. In a dry run with
        # Direwolf both reference legs are paced to DRY_RUN_SPEED on one
        # common clock, so their timestamps stay comparable.
        if not dry_run:
            speed = 1.0          # type: Optional[float]
        elif cluster is not None:
            speed = DRY_RUN_SPEED
        else:
            speed = None
        common_start = [None]    # type: List[Optional[float]]
        pump = make_pump(sox_p, mm_p, MM_RATE * 2, speed,
                         common_start if dry_run else None)
        pump_dw = None  # type: Optional[Callable[[], None]]
        if dw_run is not None:
            assert dw is not None
            sox_dw = track_proc(subprocess.Popen(build_dw_sox_cmd(wav, dw.rate),
                                                 stdout=subprocess.PIPE,
                                                 stderr=subprocess.DEVNULL))
            pump_dw = make_pump(sox_dw, dw_run.proc, dw.rate * 2, speed,
                                common_start if dry_run else None)

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
                _LIVE_STATS.add(mm=1)
                now = time.monotonic()
                if not dry_run:     # printed with the ESP32's answer
                    matcher.add_mm(now, pkt)
                elif cluster is None:   # dry run: no ESP32, just list the packet
                    say(T("%06d [multimon %s] %s") %
                        (len(res.mm_packets), mmss(now - t0), pkt.raw))
                if cluster is not None:
                    cluster.add("mm", now, pkt)
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
                _LIVE_STATS.set_elapsed(now - t0)
                if not dry_run or cluster is not None:
                    feed_and_step(now)
                if (abort_on_overrange and not target_hit[0] and
                        collector.overrange_since(overrange_base) > 0):
                    target_hit[0] = True
                    kill_pipeline()
                if now - last_progress >= PROGRESS_SECONDS:
                    last_progress = now
                    if cluster is None:
                        say(T("       [progress %s / %s] multimon=%d  ok=%d  "
                              "not-decoded=%d  different=%d  (serial lines seen: %d)") %
                            (mmss(now - t0), mmss(duration), len(res.mm_packets),
                             res.ok, len(res.missing), len(res.mismatch),
                             collector.lines_seen))
                    else:
                        say(T("       [progress %s / %s] multimon=%d  direwolf=%d  ok=%d  "
                              "not-decoded=%d  different=%d  (serial lines seen: %d)") %
                            (mmss(now - t0), mmss(duration), len(res.mm_packets),
                             len(res.dw_packets), res.ok, len(res.missing),
                             len(res.mismatch), collector.lines_seen))

        rt = threading.Thread(target=read_mm, daemon=True)
        pt = threading.Thread(target=pump, daemon=True)
        pdt = (threading.Thread(target=pump_dw, daemon=True)
               if pump_dw is not None else None)
        gt = threading.Thread(target=ticker, daemon=True)
        rt.start()
        gt.start()

        # Start the real-time playback to the ESP32 (and the monitor) and the
        # multimon-ng feed at the same moment.
        player_err = []   # type: List[bytes]
        monitor_err = []  # type: List[bytes]

        def drain(proc: "subprocess.Popen", sink: List[bytes]) -> None:
            """Read a player's stderr continuously. Reading it only after
            wait() would deadlock the moment it writes more than one pipe
            buffer of warnings."""
            if proc is None or proc.stderr is None:
                return
            try:
                for chunk in iter(lambda: proc.stderr.read(4096), b""):
                    sink.append(chunk)
            except Exception:
                pass

        drainers = []  # type: List[threading.Thread]
        if not dry_run:
            assert route is not None
            player = start_player(rendered, route.test, 1.0, "ESP32 test leg")
            play_procs.append(player)
            drainers.append(threading.Thread(target=drain, args=(player, player_err),
                                             daemon=True))
            if route.monitor is not None:
                try:
                    monitor = start_player(rendered, route.monitor,
                                           route.monitor_volume, "monitor")
                    play_procs.append(monitor)
                    drainers.append(threading.Thread(target=drain,
                                                     args=(monitor, monitor_err),
                                                     daemon=True))
                except OSError as exc:        # never let the monitor stop the test
                    sys.stderr.write(T("\n[audio] monitor player failed (rc=%s): %s\n") %
                                     ("-", exc))
            for d in drainers:
                d.start()
        common_start[0] = time.monotonic()
        pt.start()
        if pdt is not None:
            pdt.start()

        if player is not None:
            # Watchdog: pw-cat must end with the file. An output unplugged in
            # mid-run leaves its stream unlinked (node.dont-reconnect), and
            # without this limit the bench would wait for it for ever.
            limit = (duration + PLAYER_GRACE_SECONDS) if duration > 0 else None
            try:
                player.wait(timeout=limit)
            except subprocess.TimeoutExpired:
                sys.stderr.write(T("\n[audio] the player was still running %.0f s after "
                                   "the end of the file - killed it (was the output "
                                   "unplugged?)\n") % PLAYER_GRACE_SECONDS)
                kill_pipeline()
                player.wait()
            check_cancel()              # Stop killed the player: unwind now
            if monitor is not None:
                # Let the monitor finish the last few hundred ms by itself.
                try:
                    monitor.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    monitor.kill()
                    monitor.wait()
            for d in drainers:
                d.join(timeout=2)
            if player.returncode not in (0, None) and not target_hit[0]:
                err = b"".join(player_err).decode("latin-1", "replace")
                sys.stderr.write(T("\n[audio] player failed (rc=%s): %s\n") %
                                 (player.returncode, err.strip()))
            if (monitor is not None and monitor.returncode not in (0, None, -9)
                    and not target_hit[0]):
                err = b"".join(monitor_err).decode("latin-1", "replace")
                sys.stderr.write(T("\n[audio] monitor player failed (rc=%s): %s\n") %
                                 (monitor.returncode, err.strip()))
        # A paced dry run lasts as long as the audio / DRY_RUN_SPEED, so no
        # fixed limit there; the pump itself quits on Stop.
        pump_limit = None if (dry_run and speed) else 30   # type: Optional[float]
        pt.join(timeout=pump_limit)
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
            sys.stderr.write(T("\n[mm] multimon-ng did not exit within 60 s - killing it\n"))
            mm_p.kill()
            try:
                mm_p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        rt.join(timeout=5)
        if pdt is not None and dw_run is not None:
            pdt.join(timeout=pump_limit)
            if sox_dw is not None:
                try:
                    sox_dw.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    sox_dw.kill()
            dw_run.finish()             # waits for Direwolf to drain and exit

        # Let the ESP32 finish the last frame and flush its console. At least
        # `window` seconds, so the last packets get the same chance to be
        # matched as every other one.
        sleep_or_stop(max(tail, window))
    except KeyboardInterrupt:
        interrupted = True
        raise
    finally:
        done.set()
        for p in play_procs + [sox_p, mm_p, sox_dw]:
            if p is not None and p.poll() is None:
                try:
                    p.kill()
                except Exception:
                    pass
        if dw_run is not None:
            dw_run.kill()               # no-op after a normal finish()
        if gt is not None:
            gt.join(timeout=2)
        if rendered is not None:
            try:
                os.unlink(rendered)
            except OSError:
                pass
        if not dry_run:
            if interrupted:
                # Packets still waiting for their verdict are not counted:
                # the ESP32 has not had its full chance to answer them.
                dropped = matcher.drop_pending()
                dropped_ids = set(id(d) for d in dropped)
                res.mm_packets = [p for p in res.mm_packets
                                  if id(p) not in dropped_ids]
                # Keep the live counters in step with what the report counts.
                _LIVE_STATS.add(mm=-len(dropped))
            else:
                feed_and_step(time.monotonic(), final=True)
            flush_resolved()
            res.esp_packets = list(matcher.esp_seen)
            res.offset = matcher.offset
        elif cluster is not None and not interrupted:
            feed_and_step(time.monotonic(), final=True)
        if cluster is not None:
            if interrupted:
                # Same rule as the legacy matcher: rows whose window had not
                # closed yet are not counted.
                cluster.drop_pending()
            res.dw_offset = cluster.offset["dw"]
        res.duration = time.monotonic() - t0
        _LIVE_STATS.set_elapsed(min(res.duration, duration) if duration else res.duration)
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
        # the ESP32 leg goes through PipeWire output buffering, the ADC, the demod
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


class ClusterMatcher:
    """Groups what the three decoders reported into one Row per physical
    transmission, for the three-column (multimon-ng / Direwolf / ESP32)
    report.

    It runs NEXT TO LiveMatcher, never instead of it: LiveMatcher still
    decides every legacy (ESP32 vs multimon-ng) verdict and number, so runs
    with Direwolf stay comparable with historical ones.

      * add(): a frame joins the open cluster with IDENTICAL content that has
        no frame from that source yet and whose time is within +/- window
        (closest first); otherwise it opens a cluster of its own. Times are
        compared on the multimon-ng clock: each source's latency skew
        (offset) is learned from exact matches against multimon-ng, like
        LiveMatcher does for the ESP32.
      * step(): a cluster closes once its window has passed. Before closing,
        a source it still lacks may be taken from a lone frame of that source
        nearby that is the same frame damaged - same header with another
        payload (cell D) or same payload with another header (cell H) - just
        like LiveMatcher's mismatch / corrupt rules.
      * The row is judged against Direwolf's content when Direwolf has it
        (exact bytes, CRC-strict), else multimon-ng's, else the ESP32's alone
        (unconfirmed).
    """

    def __init__(self, window: float, offset_auto: bool = True,
                 esp_offset: float = 0.0, dw_offset: float = 0.0,
                 sources: Tuple[str, ...] = ("mm", "dw", "esp")) -> None:
        self.window = window
        self.sources = tuple(sources)
        self._lock = threading.Lock()
        self.offset = {"mm": 0.0, "dw": dw_offset, "esp": esp_offset}
        self.offset_auto = offset_auto
        self._samples = {"dw": [], "esp": []}   # type: dict
        self.locked = {"dw": not offset_auto, "esp": not offset_auto}
        self._open = []      # type: list   # clusters: {"t", "members", "gone"}
        self._n = 0

    # ------------------------------------------------------------ helpers
    def _note(self, src: str, delta: float) -> None:
        if src == "mm" or self.locked.get(src, True):
            return
        if abs(delta) > OFFSET_MAX_SECONDS:
            return
        self._samples[src].append(delta)
        if len(self._samples[src]) >= OFFSET_MIN_SAMPLES:
            self.offset[src] = median(self._samples[src])
            self.locked[src] = True

    @staticmethod
    def _content(c: dict) -> Packet:
        return next(iter(c["members"].values()))[1]

    def _anchor(self, members: dict) -> float:
        for s in ("mm", "dw", "esp"):
            if s in members:
                return members[s][0] - self.offset[s]
        return 0.0

    # --------------------------------------------------------------- input
    def add(self, src: str, t: float, pkt: Packet) -> None:
        with self._lock:
            ta = t - self.offset[src]
            best, bd = None, None
            for c in self._open:
                if src in c["members"]:
                    continue
                if not self._content(c).same_content(pkt):
                    continue
                d = abs(ta - c["t"])
                if d > self.window:
                    continue
                if bd is None or d < bd:
                    best, bd = c, d
            if best is None:
                self._open.append({"t": ta, "members": {src: (t, pkt)}, "gone": False})
                return
            m = best["members"]
            if src == "mm":
                for s, (ts, _p) in m.items():
                    self._note(s, ts - t)
            elif "mm" in m:
                self._note(src, t - m["mm"][0])
            m[src] = (t, pkt)
            best["t"] = self._anchor(m)

    def pending(self) -> int:
        with self._lock:
            return len(self._open)

    def drop_pending(self) -> int:
        """Forget every cluster not closed yet (used when interrupted)."""
        with self._lock:
            n = len(self._open)
            self._open = []
            return n

    # ------------------------------------------------------------- closing
    def _absorb_near(self, c: dict) -> None:
        cons = self._consensus(c["members"])
        for s in self.sources:
            if s in c["members"]:
                continue
            best_hdr, best_info = None, None
            for d in self._open:
                if d is c or d["gone"] or list(d["members"]) != [s]:
                    continue
                dist = abs(d["t"] - c["t"])
                if dist > self.window:
                    continue
                p = d["members"][s][1]
                if p.key_header() == cons.key_header():
                    if best_hdr is None or dist < abs(best_hdr["t"] - c["t"]):
                        best_hdr = d
                elif p.info == cons.info:
                    if best_info is None or dist < abs(best_info["t"] - c["t"]):
                        best_info = d
            d = best_hdr if best_hdr is not None else best_info
            if d is not None:
                c["members"][s] = d["members"][s]
                d["gone"] = True
                cons = self._consensus(c["members"])

    @staticmethod
    def _consensus(members: dict) -> Packet:
        for s in ("dw", "mm", "esp"):
            if s in members:
                return members[s][1]
        raise ValueError("empty cluster")

    def _finalise(self, c: dict) -> Row:
        m = c["members"]
        cons = self._consensus(m)
        cells = {}
        for s in self.sources:
            if s not in m:
                cells[s] = CELL_NONE
                continue
            p = m[s][1]
            if p.same_content(cons):
                cells[s] = CELL_OK
            elif p.key_header() == cons.key_header():
                cells[s] = CELL_DIFF
            else:
                cells[s] = CELL_HDR
        has_mm, has_dw = "mm" in m, "dw" in m
        dw_used = "dw" in self.sources
        esp_cell = cells.get("esp", CELL_NONE)
        if not has_mm and not has_dw:
            cls = ROW_ESP_ONLY
            cells["esp"] = CELL_UNCONF
        elif has_mm and has_dw and cells["mm"] != CELL_OK:
            cls = ROW_CONFLICT
        elif dw_used and has_mm and not has_dw:
            cls = ROW_MM_ONLY
        elif has_dw and not has_mm:
            cls = ROW_DW_ONLY
        elif "esp" not in self.sources or esp_cell == CELL_OK:
            cls = ROW_ALL
        else:
            cls = {CELL_NONE: ROW_ESP_MISS, CELL_DIFF: ROW_ESP_DIFF,
                   CELL_HDR: ROW_ESP_HDR}[esp_cell]
        return Row(idx=0, t=self._anchor(m), members=dict(m), cells=cells, cls=cls,
                   consensus=cons, fcs=m["dw"][1].fcs if has_dw else None)

    def step(self, now: float, final: bool = False) -> List[Row]:
        """Close every cluster whose window has passed (all of them when
        `final`) and return their rows in time order."""
        with self._lock:
            w = self.window
            slack = max(abs(v) for v in self.offset.values())
            due = [c for c in self._open if final or now >= c["t"] + w + slack]
            due.sort(key=lambda c: c["t"])
            for c in due:
                if not c["gone"]:
                    self._absorb_near(c)
            closed = [c for c in due if not c["gone"]]
            self._open = [c for c in self._open
                          if not c["gone"] and all(c is not d for d in closed)]
            rows = [self._finalise(c) for c in closed]
            rows.sort(key=lambda r: r.t)
            for r in rows:
                self._n += 1
                r.idx = self._n
            return rows


def row_legacy_verdict(row: Row) -> str:
    """Map a two-source (mm + esp) row onto LiveMatcher's verdict names."""
    return {ROW_ALL: "ok", ROW_ESP_MISS: "missing", ROW_ESP_DIFF: "mismatch",
            ROW_ESP_HDR: "corrupt", ROW_ESP_ONLY: "extra"}.get(row.cls, row.cls)


def row_stats(rows: List[Row]) -> dict:
    """Totals of the three-column report. ESP32 scores are [ok, different,
    header-corrupt, not-decoded] against Direwolf (`vs_dw`) and against the
    union of both references (`vs_union`); reference conflicts are excluded
    from both, and ESP32-only frames are never successes here."""
    st = {"mm": 0, "dw": 0, "union": 0, "both": 0, "mm_only": 0, "dw_only": 0,
          "conflict": 0, "esp_only": 0, "implausible": 0,
          "vs_dw": [0, 0, 0, 0], "vs_union": [0, 0, 0, 0]}
    slot = {CELL_OK: 0, CELL_DIFF: 1, CELL_HDR: 2, CELL_NONE: 3}
    for r in rows:
        has_mm, has_dw = "mm" in r.members, "dw" in r.members
        if r.cls == ROW_ESP_ONLY:
            st["esp_only"] += 1
            if not aprs_plausible(r.members["esp"][1]):
                st["implausible"] += 1
            continue
        st["union"] += 1
        st["mm"] += has_mm
        st["dw"] += has_dw
        st["both"] += has_mm and has_dw
        st["mm_only"] += has_mm and not has_dw
        st["dw_only"] += has_dw and not has_mm
        if r.cls == ROW_CONFLICT:
            st["conflict"] += 1
            continue
        k = slot.get(r.cells.get("esp", CELL_NONE), 3)
        st["vs_union"][k] += 1
        if has_dw:
            st["vs_dw"][k] += 1
    return st


def ref_packet_count(res: FileResult, reference: str) -> int:
    """How many packets of the selected reference a (partial) result holds.
    'multimon' is exactly the legacy count (multimon-ng packets + ESP32
    extras). The union cannot be known before the rows close, so while a
    file plays it is approximated by the larger of the two references."""
    if reference == "direwolf":
        return len(res.dw_packets)
    if reference == "union":
        return max(len(res.mm_packets), len(res.dw_packets))
    return len(res.mm_packets) + len(res.extra)


def _tcell(row: Row, src: str, t0: float) -> str:
    if src not in row.members:
        return "--:--.-"
    return mmss(row.members[src][0] - t0)


def print_row(row: Row, t0: float, esp_used: bool = True) -> None:
    """One transmission in the three-column report, with the notes it needs."""
    order = ("mm", "dw", "esp") if esp_used else ("mm", "dw")
    cells = " ".join(row.cells.get(s, CELL_NONE) for s in order)
    times = "  ".join("%s %s" % (s, _tcell(row, s, t0)) for s in order)
    say("%06d  %s  [%s]  fcs=%s  %s" % (row.idx, times, cells, fmt_fcs(row.fcs),
                                        row.consensus.raw))
    ind = "        "

    def show_pair(label_a: str, pa: Packet, label_b: str, pb: Packet) -> None:
        say("%s%s: %s" % (ind, label_a, pa.raw))
        say("%s%s: %s" % (ind, label_b, pb.raw))

    if row.cls == ROW_CONFLICT:
        show_pair("mm ", row.members["mm"][1], "dw ", row.members["dw"][1])
        say(ind + T("! REFERENCE CONFLICT - multimon-ng and Direwolf disagree; "
                    "check normalisation"))
        return
    if row.cls == ROW_ESP_ONLY:
        ok = aprs_plausible(row.members["esp"][1])
        say(ind + T("! ESP32 ONLY - unconfirmed by any CRC-checked reference "
                    "(plausible: %s)") % (T("yes") if ok else T("NO")))
        return
    if row.cls == ROW_MM_ONLY:
        say(ind + T("! only multimon-ng decoded it (Direwolf did not)"))
    elif row.cls == ROW_DW_ONLY:
        say(ind + T("! multimon-ng did not decode it (Direwolf did)"))
    if not esp_used:
        return
    esp = row.cells.get("esp", CELL_NONE)
    if esp == CELL_NONE:
        say(ind + T("! ESP32 NOT DECODED"))
    elif esp in (CELL_DIFF, CELL_HDR):
        show_pair("ref", row.consensus, "esp", row.members["esp"][1])
        say(ind + (T("! ESP32 DECODED BUT DIFFERENT") if esp == CELL_DIFF else
                   T("! ESP32 PAYLOAD OK BUT HEADER CORRUPT")))


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------


def pct(n: int, total: int) -> float:
    return (100.0 * n / total) if total else 0.0


def print_file_report(res: FileResult, dry_run: bool = False) -> None:
    say(T("  multimon-ng decoded %d packet(s)") % len(res.mm_packets))
    if dry_run:
        return
    say(T("  ESP32 decoded %d packet(s)") % len(res.esp_packets))
    say(T("  -> OK: %d   DIFFERENT: %d   HDR-CORRUPT: %d   NOT DECODED: %d   "
          "EXTRA(esp only): %d") %
        (res.ok, len(res.mismatch), len(res.corrupt), len(res.missing), len(res.extra)))
    if res.offset:
        say(T("  -> measured esp32 latency vs multimon-ng: %+.2f s") % res.offset)
    for mp, ep in res.mismatch:
        say(T("    ! DIFFERENT"))
        say(T("        multimon: %s") % mp.raw)
        say(T("        esp32   : %s") % ep.raw)
    for mp, ep in res.corrupt:
        say(T("    ! HEADER CORRUPT (payload matched)"))
        say(T("        multimon: %s") % mp.raw)
        say(T("        esp32   : %s") % ep.raw)
    for mp in res.missing:
        say(T("    ! NOT DECODED by ESP32: %s") % mp.raw)


def print_loss_resume(res: FileResult, results: List[FileResult]) -> None:
    """Short packet-loss recap printed as soon as one WAV is finished, before
    the next one starts: this file alone, then the running total of every
    file tested so far. The reference is what multimon-ng decoded.
      lost        = NOT DECODED by the ESP32 (res.missing)
      not correct = lost + DIFFERENT + HDR-CORRUPT, i.e. every multimon-ng
                    packet the ESP32 did not deliver exactly right."""
    def counts(rs: List[FileResult]) -> Tuple[int, int, int]:
        total = sum(len(r.mm_packets) for r in rs)
        lost = sum(len(r.missing) for r in rs)
        bad = lost + sum(len(r.mismatch) + len(r.corrupt) for r in rs)
        return total, lost, bad

    say(T("  -- Packet loss ------------------------------------------------"))
    total, lost, bad = counts([res])
    if total:
        say(T("  This file  : lost %d of %d (%.2f%%)   not correct %d of %d (%.2f%%)") %
            (lost, total, pct(lost, total), bad, total, pct(bad, total)))
    else:
        say(T("  This file  : multimon-ng decoded no packets - nothing to measure"))
    total, lost, bad = counts(results)
    say(T("  Cumulative : lost %d of %d (%.2f%%)   not correct %d of %d (%.2f%%)   [%d file(s)]") %
        (lost, total, pct(lost, total), bad, total, pct(bad, total), len(results)))


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
    print(T("SUMMARY"))
    print(bar)
    print("  %-30s %6s %6s %6s %6s %6s %6s" %
          (T("file"), T("mm"), T("ok"), T("diff"), T("hdr"), T("n/dec"), T("extra")))
    for r in results:
        print("  %-30s %6d %6d %6d %6d %6d %6d" %
              (r.name[:30], len(r.mm_packets), r.ok, len(r.mismatch),
               len(r.corrupt), len(r.missing), len(r.extra)))
    print("  " + "-" * 70)
    if volume is not None:
        print(T("  Playback gain used for this test  : %.3f  (%+.1f dB)") %
              (volume, to_db(volume)))
    if offsets:
        print(T("  ESP32 latency vs multimon-ng      : %+.2f s (median of %d file(s))") %
              (median(offsets), len(offsets)))
    print(T("  Files tested                      : %d") % len(results))
    print(T("  Total packets (multimon-ng)       : %d") % total)
    print(T("  Packets seen by ESP32             : %d") % esp_total)
    print(T("  Decoded correctly                 : %d  (%.2f%%)") % (ok, pct(ok, total)))
    print(T("  Decoded with different content    : %d  (%.2f%%)") % (mism, pct(mism, total)))
    print(T("  Decoded with corrupt header       : %d  (%.2f%%)") % (corr, pct(corr, total)))
    print(T("  Missing (not decoded)             : %d  (%.2f%%)") % (miss, pct(miss, total)))
    print(T("  Extra (ESP32 only, not an error)  : %d") % extra)
    print(bar)
    if total == 0:
        print(T("RESULT: no packets were decoded by multimon-ng - nothing to compare."))
        return 2
    return 0 if (mism == 0 and corr == 0 and miss == 0) else 1


def print_dw_file_report(res: FileResult, setup: DwSetup, dry_run: bool = False) -> None:
    """Direwolf part of the per-file report, printed after the legacy lines."""
    st = row_stats(res.rows)
    if setup.fix_bits == 0:
        say(T("  Direwolf decoded %d packet(s)   (CRC-strict, FIX_BITS=0)") % st["dw"])
    else:
        say(T("  Direwolf decoded %d packet(s)   (NOT CRC-strict, FIX_BITS=%d)") %
            (st["dw"], setup.fix_bits))
    say(T("  References: both %d   mm-only %d   dw-only %d   conflicts %d") %
        (st["both"], st["mm_only"], st["dw_only"], st["conflict"]))
    if not dry_run:
        say(T("  ESP32 vs Direwolf : OK %d  DIFFERENT %d  HDR-CORRUPT %d  NOT DECODED %d") %
            tuple(st["vs_dw"]))
        say(T("  ESP32-only (unconfirmed): %d   of which implausible: %d") %
            (st["esp_only"], st["implausible"]))
    if res.dw_offset:
        say(T("  -> measured Direwolf latency vs multimon-ng: %+.2f s") % res.dw_offset)


def _rate_line(label: str, k: int, n: int) -> str:
    lo, hi = wilson(k, n)
    return (T("%s: %d of %d  (%.2f%%, 95%% CI %.1f..%.1f%%)") %
            (label, k, n, pct(k, n), 100.0 * lo, 100.0 * hi))


def print_dw_summary(results: List[FileResult], setup: DwSetup, reference: str,
                     dry_run: bool = False) -> int:
    """The REFERENCE COMPARISON block printed after the legacy summary.
    Returns the exit code the Direwolf / union reference would give:
    0 all correct, 1 any ESP32 miss / difference / reference conflict,
    2 nothing to compare."""
    rows = [r for res in results for r in res.rows]
    st = row_stats(rows)
    bar = "=" * 72
    print("\n" + bar)
    print(T("REFERENCE COMPARISON (Direwolf)"))
    print(bar)
    print(T("  Direwolf %s, %d Hz, modem profile %s, FIX_BITS %d") %
          (setup.version or "?", setup.rate, setup.profile or T("(default)"), setup.fix_bits))
    if setup.fix_bits > 0:
        print(T("  WARNING: Direwolf is NOT a strict CRC reference: FIX_BITS=%d") %
              setup.fix_bits)
    print(T("  Reference for the exit code       : %s") % reference)
    print("  %-30s %6s %6s %6s %6s %6s %6s" %
          (T("file"), T("mm"), T("dw"), T("union"), T("esp=dw"), T("esp1"), T("confl")))
    for r in results:
        fs = row_stats(r.rows)
        print("  %-30s %6d %6d %6d %6d %6d %6d" %
              (r.name[:30], fs["mm"], fs["dw"], fs["union"], fs["vs_dw"][0],
               fs["esp_only"], fs["conflict"]))
    print("  " + "-" * 70)
    print(T("  Packets multimon-ng / Direwolf    : %d / %d") % (st["mm"], st["dw"]))
    print(T("  Union / both references           : %d / %d") % (st["union"], st["both"]))
    print(T("  multimon-ng only / Direwolf only  : %d / %d") % (st["mm_only"], st["dw_only"]))
    print(T("  Reference conflicts               : %d") % st["conflict"])
    if not dry_run:
        mm_total = sum(len(r.mm_packets) for r in results)
        mm_ok = sum(r.ok for r in results)
        print(_rate_line(T("  ESP32 correct vs multimon-ng      "), mm_ok, mm_total))
        print(_rate_line(T("  ESP32 correct vs Direwolf         "), st["vs_dw"][0],
                         sum(st["vs_dw"])))
        print(_rate_line(T("  ESP32 correct vs union            "), st["vs_union"][0],
                         sum(st["vs_union"])))
        print(T("  ESP32-only, unconfirmed           : %d  (implausible: %d)") %
              (st["esp_only"], st["implausible"]))
    offsets = [r.dw_offset for r in results if r.dw_offset]
    if offsets:
        print(T("  Direwolf latency vs multimon-ng   : %+.2f s (median of %d file(s))") %
              (median(offsets), len(offsets)))
    print(bar)
    if dry_run:
        return 0 if st["union"] else 2
    key = "vs_dw" if reference == "direwolf" else "vs_union"
    total = sum(st[key])
    if total == 0:
        print(T("RESULT: the selected reference decoded no packets - nothing to compare."))
        return 2
    bad = st[key][1] + st[key][2] + st[key][3] + st["conflict"]
    return 1 if bad else 0


CSV_COLUMNS = ("file", "row", "t_mm", "t_dw", "t_esp", "cell_mm", "cell_dw", "cell_esp",
               "class", "fcs", "consensus", "esp")


def write_rows_csv(path: str, results: List[FileResult]) -> int:
    """One CSV line per three-column row, so runs can be diffed over time
    (the FCS is the stable key of a frame). Returns the number of rows."""
    n = 0
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(CSV_COLUMNS)
        for res in results:
            for r in res.rows:
                def tt(src: str) -> str:
                    if src not in r.members:
                        return ""
                    return "%.3f" % (r.members[src][0] - res.t0)
                esp = r.members["esp"][1].raw if "esp" in r.members else ""
                w.writerow([res.name, r.idx, tt("mm"), tt("dw"), tt("esp"),
                            r.cells.get("mm", ""), r.cells.get("dw", ""),
                            r.cells.get("esp", ""), r.cls,
                            "" if r.fcs is None else "%04X" % r.fcs,
                            r.consensus.raw, esp])
                n += 1
    return n


# --------------------------------------------------------------------------
# Startup helpers
# --------------------------------------------------------------------------


_HAVE_STDBUF = False
_HAVE_DIREWOLF = False


def check_tools(need_play: bool = True) -> None:
    global _HAVE_STDBUF, _HAVE_DIREWOLF
    required = ("multimon-ng", "sox") + (("pw-cat", "pw-dump") if need_play else ())
    missing = [t for t in required if shutil.which(t) is None]
    if missing:
        sys.stderr.write(T("Missing required program(s): %s\n") % ", ".join(missing))
        sys.stderr.write(T("  Debian/Ubuntu: sudo apt install multimon-ng sox libsox-fmt-all "
                           "pipewire-bin\n"))
        sys.exit(2)
    _HAVE_DIREWOLF = shutil.which("direwolf") is not None
    _HAVE_STDBUF = shutil.which("stdbuf") is not None
    if not _HAVE_STDBUF:
        sys.stderr.write(T(
            "WARNING: `stdbuf` not found (package coreutils). multimon-ng's output will be\n"
            "  block-buffered on the pipe, so its packets may arrive in bursts and be\n"
            "  timestamped late, which shows up as spurious NOT DECODED verdicts.\n"))


def find_wavs(directory: str, select: Optional[List[str]] = None) -> List[str]:
    """Every .wav in the directory, whatever the case of the extension.

    Matching on the lower-cased name (rather than a case-sensitive suffix
    check) picks up ".WAV", ".Wav", etc. os.listdir() entries are already
    unique, so no de-duplication is needed.

    `select`, when given, is a list of file names (as returned by this same
    function's basenames, i.e. relative to `directory`); only those are kept,
    in the order requested. Names in `select` that are not .wav files present
    in `directory` are silently ignored - the GUI's listbox and the
    --wav_files parser both build `select` from a listing of the same
    directory, so a mismatch only happens if the directory changed on disk
    between the two, and skipping is safer than aborting the whole run."""
    try:
        entries = os.listdir(directory)
    except OSError:
        return []
    files = [os.path.join(directory, e) for e in entries
             if e.lower().endswith(".wav") and
             os.path.isfile(os.path.join(directory, e))]
    files.sort(key=lambda s: s.lower())
    if select:
        wanted = set(select)
        files = [f for f in files if os.path.basename(f) in wanted]
    return files


def parse_wav_files_option(raw: str) -> List[str]:
    """--wav_files "a.wav, b.wav" -> ["a.wav", "b.wav"]; "" -> []."""
    return [s.strip() for s in raw.split(",") if s.strip()]


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

    The over-range rule
    -------------------
    The ADC input of the bench has no clamp diodes, and "RX audio is
    over-range" is the firmware itself saying the level is wrong. So the
    moment ANY probe reports it (by default a single warning is enough, see
    --clip_rate), that level becomes a hard CEILING:

      * no probe is ever played again at or above the ceiling - _measure()
        refuses to, whatever phase asks for it;
      * the search only moves DOWN from there, in small --clip_step_db steps
        (0.5 dB by default), until a probe comes back clean;
      * a scoring probe that reports over-range later lowers the ceiling again
        and is dropped from the plateau.

    The search therefore works like this:

      Phase 1  while nothing has clipped yet, climb in coarse 6 dB steps with
               cheap 8-packet probes (clipping is a binary signal the firmware
               reports itself). A probe that decodes nothing at all stops the
               climb: there is no evidence that the level is safe to raise.
      Phase 2  as soon as over-range appears: never go up again, walk down in
               --clip_step_db steps until a probe is clean. That level is the
               highest clean level (the "threshold" below).
      Phase 3  score full batches at 3, 6, 9, 12 and 18 dB below it, stopping
               as soon as the lower knee is clearly past.
      Phase 4  return the geometric centre of the plateau - every point whose
               Wilson interval still overlaps the best point's - clamped to at
               least MIN_CLIP_MARGIN_DB below the threshold and the ceiling.

    Scoring:  success = ok + extra,  trials = multimon packets + extra.
      * `mismatch` and `corrupt` are FAILURES, not successes. The whole point
        of the bench is content equality, so a volume that yields corrupted
        payloads must not score like one that yields correct ones.
      * `extra` (the ESP32 decoded a frame multimon-ng missed entirely) is a
        SUCCESS. It is the strongest evidence a level can give: the firmware
        beat the reference decoder on that frame.

    Everything measured is cached by rounded dB, so no volume is ever probed
    twice. Every probe - every new volume - restarts the wav list from the
    FIRST file, in order, so all levels are compared on the same audio: a
    difference in score is then the level's doing, not the recording's. (If
    the first file alone holds more than one batch, every level is judged on
    that file only; put a varied file first if that matters.)

    Runs with its own throwaway FileResult objects; nothing here is counted in
    the final report other than the resulting volume.
    """

    def __init__(self, wavs: List[str], route: Optional[AudioRoute],
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
                 offset_auto: bool = True,
                 clip_step_db: float = CLIP_STEP_DB,
                 quiet: bool = False,
                 dw: Optional[DwSetup] = None,
                 reference: str = "multimon") -> None:
        self.wavs = wavs
        # Direwolf: when given it runs during calibration too, and with
        # reference 'direwolf' / 'union' the score is taken from the
        # three-column rows (ESP32 frames no CRC-checked reference confirmed
        # are then never counted as successes).
        self.dw = dw
        self.reference = reference
        self.dw_offset = 0.0
        self.route = route
        self.tail = tail
        self.window = window
        self.collector = collector
        self.mm_extra = mm_extra
        self.volume = start_volume
        self.start_volume = start_volume
        self.batch_size = batch_size
        self.max_rounds = max_rounds
        self.headroom_db = headroom_db
        self.clip_rate = clip_rate
        self.vol_min = vol_min
        self.vol_max = vol_max
        self.max_passes = max_passes
        self.normalise = normalise
        self.offset_auto = offset_auto
        self.clip_step_db = clip_step_db
        self.quiet = quiet
        self.cache = {}        # type: dict   # rounded dB -> measurement
        self.probes_used = 0
        # Budget is counted in BATCHES OF PACKETS, not in calls: an 8-packet
        # clip probe costs 8/batch_size of a round, not a whole one. Charging
        # every probe as a full round would let the cheap threshold hunt
        # starve the expensive plateau sweep that actually picks the level.
        self.budget_used = 0.0
        self._cursor = 0       # position in the wav set; reset by every probe
        self.offset = 0.0      # latency skew learned during calibration
        self.clip_db = None    # type: Optional[float]   # highest clean level found
        # Lowest level that has EVER reported over-range. Nothing is played at
        # or above it again. None until the firmware first complains.
        self.ceiling_db = None  # type: Optional[float]
        # Every probe actually played, in order: (dB, clipped). Kept for the
        # self-test, which checks the ceiling rule on it.
        self.history = []      # type: List[Tuple[float, bool]]

    # ---------------------------------------------------------------- probe
    def _say(self, msg: str) -> None:
        if not self.quiet:
            say(msg)

    def _clamp(self, volume: float) -> float:
        return max(self.vol_min, min(self.vol_max, volume))

    def _cap_db(self, db: float) -> float:
        """Keep a level MIN_CLIP_MARGIN_DB below the over-range ceiling."""
        if self.ceiling_db is not None:
            db = min(db, self.ceiling_db - MIN_CLIP_MARGIN_DB)
        return db

    def _probe(self, volume: float, key: float, target: int) -> dict:
        """Play `target` packets at `volume` and return the raw measurement.

        Overridable: --selftest replaces this with a simulated device so the
        search logic can be tested without any hardware."""
        batch = FileResult(name=T("<probe %+.1f dB>") % key)
        overrange_before, _ = self.collector.snapshot_overrange()
        passes = 0
        # Stop at the first warning only when one warning is decisive anyway;
        # with a tolerant --clip_rate the rate has to be measured in full.
        strict = self.clip_rate <= 0.0
        # Every probe starts over from the first file of the list.
        self._cursor = 0
        while ref_packet_count(batch, self.reference) < target:
            if self._cursor >= len(self.wavs):
                self._cursor = 0
                passes += 1
                if passes >= self.max_passes:
                    # Without this the loop restarts the wav set for ever when
                    # nothing decodes at all (silent files, muted card, wrong
                    # PipeWire output) and only Ctrl-C can end the run.
                    self._say(T("      probe incomplete: %d/%d packet(s) after %d pass(es) "
                                "over the wav set - check the audio routing and the files") %
                              (ref_packet_count(batch, self.reference), target, passes))
                    break
            wav = self.wavs[self._cursor]
            self._cursor += 1
            remaining = target - ref_packet_count(batch, self.reference)
            res = FileResult(name=os.path.basename(wav))
            _LIVE_STATS.set_file(0, 0, os.path.basename(wav))
            run_one_wav(res, wav, self.route, volume, self.tail,
                        self.window, self.collector, self.mm_extra,
                        dry_run=False, stop_at_mm_packets=remaining,
                        normalise=self.normalise, offset_auto=self.offset_auto,
                        offset_seed=self.offset,
                        abort_on_overrange=strict,
                        dw=self.dw, reference=self.reference,
                        dw_offset_seed=self.dw_offset)
            if res.offset:
                self.offset = res.offset
            if res.dw_offset:
                self.dw_offset = res.dw_offset
            batch.mm_packets.extend(res.mm_packets)
            batch.esp_packets.extend(res.esp_packets)
            batch.ok += res.ok
            batch.mismatch.extend(res.mismatch)
            batch.corrupt.extend(res.corrupt)
            batch.missing.extend(res.missing)
            batch.extra.extend(res.extra)
            batch.dw_packets.extend(res.dw_packets)
            batch.rows.extend(res.rows)
            if strict and self.collector.overrange_since(overrange_before) > 0:
                # With the strict default one warning already settles it: this
                # level clips. Playing on would only keep the ADC over-driven.
                break

        n_mm = len(batch.mm_packets)
        n_extra = len(batch.extra)
        warns = self.collector.overrange_since(overrange_before)
        if self.reference != "multimon":
            st = row_stats(batch.rows)
            ok, diff, hdr, miss = st["vs_dw" if self.reference == "direwolf" else "vs_union"]
            trials = ok + diff + hdr + miss
            seen = max(1, trials)
            return {
                "volume": volume, "db": key, "ok": ok, "mismatch": diff, "corrupt": hdr,
                "missing": miss, "extra": st["esp_only"], "mm": n_mm,
                "success": ok, "trials": trials,
                "warns": warns, "clip_rate": warns / float(seen),
            }
        seen = max(1, n_mm + n_extra)
        return {
            "volume": volume, "db": key, "ok": batch.ok,
            "mismatch": len(batch.mismatch), "corrupt": len(batch.corrupt),
            "missing": len(batch.missing), "extra": n_extra, "mm": n_mm,
            "success": batch.ok + n_extra, "trials": n_mm + n_extra,
            "warns": warns, "clip_rate": warns / float(seen),
        }

    def _measure(self, volume: float, target: int) -> dict:
        """Measure a level, enforcing the over-range ceiling and the cache."""
        key = round(to_db(volume), 2)
        if self.ceiling_db is not None and key >= self.ceiling_db - 1e-6:
            # The hard rule: never play at or above a level that has already
            # made the firmware report over-range. Answer from what is known.
            cached = self.cache.get(key)
            if cached is not None:
                return cached
            return {"volume": volume, "db": key, "ok": 0, "mismatch": 0, "corrupt": 0,
                    "missing": 0, "extra": 0, "mm": 0, "success": 0, "trials": 0,
                    "warns": 1, "clip_rate": 1.0, "score": 0.0, "clipped": True}
        cached = self.cache.get(key)
        if cached is not None and cached["trials"] >= target:
            return cached

        m = self._probe(volume, key, target)
        m["score"] = 100.0 * m["success"] / max(1, m["trials"])
        # With the default --clip_rate of 0 a single warning is enough.
        m["clipped"] = m.get("warns", 0) > 0 and m["clip_rate"] > self.clip_rate
        self.cache[key] = m
        self.probes_used += 1
        self.budget_used += target / float(max(1, self.batch_size))
        self.history.append((key, m["clipped"]))
        if m["clipped"]:
            self.ceiling_db = key if self.ceiling_db is None else min(self.ceiling_db, key)
        self._say(T("  [probe %2d, budget %.1f/%d] gain=%.3f (%+5.1f dB)  mm=%d ok=%d diff=%d hdr=%d "
                    "miss=%d extra=%d  score=%.1f%%  clip=%.2f/pkt") %
                  (self.probes_used, self.budget_used, self.max_rounds, volume, key, m["mm"],
                   m["ok"], m["mismatch"], m["corrupt"], m["missing"], m["extra"],
                   m["score"], m["clip_rate"]) +
                  (T("  <- OVER-RANGE") if m["clipped"] else ""))
        return m

    def _clips(self, volume: float) -> bool:
        return self._measure(volume, CLIP_PROBE_PACKETS)["clipped"]

    def _budget_left(self, cost: float = 0.0) -> bool:
        """Is there budget for one more probe costing `cost` batches?"""
        return (self.budget_used + cost) < self.max_rounds

    def safe_volume(self) -> float:
        """The best level known to be safe right now. Used when the search is
        interrupted: it must never hand back a level that reported over-range
        (the starting volume, for instance, may well be one)."""
        if self.ceiling_db is None:
            return self._clamp(self.volume)
        clean = [db for db, m in self.cache.items()
                 if not m.get("clipped") and db < self.ceiling_db]
        db = max(clean) if clean else self.ceiling_db - self.headroom_db
        return self._clamp(to_lin(self._cap_db(db)))

    # ---------------------------------------------------------- phases 1+2
    def _find_clip_threshold(self, start_db: float) -> Tuple[float, bool]:
        """Find the highest level that does NOT report over-range.

        Returns (that level in dB, True if over-range was actually observed).
        Climbs in coarse steps only while the firmware has never complained;
        from the first over-range on it never goes up again and walks down
        in --clip_step_db steps until a probe comes back clean."""
        floor_db, ceil_db = to_db(self.vol_min), to_db(self.vol_max)
        clip_cost = CLIP_PROBE_PACKETS / float(max(1, self.batch_size))
        # The descent may use up to half of the budget; the rest is kept for
        # the plateau sweep that actually decides the level.
        hunt_cap = max(clip_cost, HUNT_BUDGET_SHARE * self.max_rounds)

        # Phase 1: climb only while nothing has clipped.
        db = start_db
        lo_db = None   # highest level known to be clean
        while self.ceiling_db is None and self.budget_used + clip_cost <= hunt_cap:
            m = self._measure(to_lin(db), CLIP_PROBE_PACKETS)
            if m["clipped"]:
                break
            lo_db = db if lo_db is None else max(lo_db, db)
            if m["trials"] == 0:
                # Nothing decoded: no evidence the level is safe to raise, and
                # a muted or misrouted chain would otherwise be driven to the
                # maximum gain before anything is plugged back in.
                return lo_db, False
            if db >= ceil_db - 1e-6:
                return ceil_db, False
            db = min(db + COARSE_STEP_DB, ceil_db)
        if self.ceiling_db is None:
            return (lo_db if lo_db is not None else start_db), False

        # Phase 2: over-range seen. Never up again; small steps down.
        self._say(T("  Over-range at %+.1f dB (gain %.3f): no probe will go that high again; "
                    "stepping down in %.2f dB steps") %
                  (self.ceiling_db, to_lin(self.ceiling_db), self.clip_step_db))
        db = self.ceiling_db - self.clip_step_db
        while True:
            if lo_db is not None and db <= lo_db + 1e-6:
                return lo_db, True          # already known to be clean
            if db < floor_db - 1e-6:
                self._say(T("  Still over-range at the lowest gain allowed, %.3f (%+.1f dB): the "
                            "hardware level into the ADC is far too hot - turn the RX trimmer "
                            "(or the PC volume) down and run again.") %
                          (self.vol_min, floor_db))
                return floor_db, True
            if self.budget_used + clip_cost > hunt_cap:
                fallback = max(floor_db, self.ceiling_db - self.headroom_db)
                self._say(T("  Descent budget spent while still over-range at %+.1f dB - using "
                            "%+.1f dB (%.0f dB below it). Turn the RX trimmer down, or raise "
                            "--auto_volume_max_rounds / --clip_step_db.") %
                          (self.ceiling_db, fallback, self.headroom_db))
                return fallback, True
            if not self._clips(to_lin(db)):
                return db, True
            db = self.ceiling_db - self.clip_step_db

    # ------------------------------------------------------------- phase 3+4
    def run(self) -> float:
        if not self.wavs:
            return self.volume

        self._say("\n" + "=" * 72)
        self._say(T("AUTO-VOLUME CALIBRATION (over-range ceiling + plateau centre)"))
        self._say("=" * 72)
        self._say(T("  Start gain %.3f (%+.1f dB), range %.3f..%.3f, budget %d probe(s), "
                    "%d packet(s) per scoring probe, %.2f dB steps below over-range") %
                  (self.volume, to_db(self.volume), self.vol_min, self.vol_max,
                   self.max_rounds, self.batch_size, self.clip_step_db))

        clip_db, observed = self._find_clip_threshold(to_db(self._clamp(self.volume)))
        self.clip_db = clip_db
        if observed:
            self._say(T("  Highest level without over-range: %+.1f dB (gain %.3f)") %
                      (clip_db, to_lin(clip_db)))
        else:
            self._say(T("  No clipping seen up to %+.1f dB (gain %.3f) - the hardware level "
                        "into the ADC may be too low; check the RX trimmer.") %
                      (clip_db, to_lin(clip_db)))

        # Sanity check: with no packets at all there is nothing to calibrate.
        probed = [m for m in self.cache.values()
                  if m["mm"] > 0 or m["extra"] > 0 or m["trials"] > 0]
        if not probed:
            self.volume = self._clamp(to_lin(self._cap_db(clip_db - self.headroom_db)))
            self._say(T("  No packets decoded during calibration at any level - falling back "
                        "to %.3f (%+.1f dB, threshold - %.0f dB).") %
                      (self.volume, to_db(self.volume), self.headroom_db))
            self._say("=" * 72)
            return self.volume

        points = []  # type: List[Tuple[float, dict]]
        best = None  # type: Optional[Tuple[float, dict]]
        for off in PLATEAU_OFFSETS_DB:
            if not self._budget_left(1.0):
                self._say(T("  Probe budget spent; stopping the plateau sweep "
                            "(raise it with --auto_volume_max_rounds)."))
                break
            db = clip_db + off
            if db < to_db(self.vol_min):
                break
            if self.ceiling_db is not None and db >= self.ceiling_db - 1e-6:
                continue            # a later over-range already ruled this level out
            m = self._measure(self._clamp(to_lin(db)), self.batch_size)
            if m["clipped"]:
                self._say(T("      over-range during a scoring probe at %+.1f dB - this level "
                            "is dropped and nothing at or above it is played again") % db)
                continue
            if m["trials"] == 0:
                continue
            points.append((db, m))
            if best is None or m["score"] > best[1]["score"]:
                best = (db, m)
            elif m["score"] < best[1]["score"] - KNEE_DROP_PCT:
                self._say(T("      score fell %.0f points below the best - the lower knee is "
                            "past, no need to go quieter") % (best[1]["score"] - m["score"]))
                break

        # A point measured before a later over-range lowered the ceiling is no
        # longer eligible.
        if self.ceiling_db is not None:
            points = [(db, m) for db, m in points if db < self.ceiling_db - 1e-6]
            if best is not None and best[0] >= self.ceiling_db - 1e-6:
                best = max(points, key=lambda p: p[1]["score"]) if points else None

        if not points or best is None:
            self.volume = self._clamp(to_lin(self._cap_db(clip_db - self.headroom_db)))
            self._say(T("  No usable score data - using threshold - %.0f dB = %.3f (%+.1f dB).") %
                      (self.headroom_db, self.volume, to_db(self.volume)))
            self._say("=" * 72)
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
        centre_db = self._cap_db(min(centre_db, clip_db - MIN_CLIP_MARGIN_DB))
        self.volume = self._clamp(to_lin(centre_db))

        self._say(T("  Plateau: %+.1f .. %+.1f dB (%d tied point(s) of %d probed); "
                    "best raw score %.1f%%") %
                  (min(tied), max(tied), len(tied), len(points), best[1]["score"]))
        self._say(T("  Chosen gain: %.3f (%+.1f dB), %.1f dB below the highest level "
                    "without over-range") %
                  (self.volume, to_db(self.volume), clip_db - to_db(self.volume)))
        self._advise()
        self._say("=" * 72)
        return self.volume

    def _advise(self) -> None:
        """The firmware's over-range message names the real remedy: the RX
        trimmer or the transceiver volume. A calibrated gain far from unity in
        EITHER direction is a statement about the interface hardware, not just
        a number to hand to `play`."""
        db = to_db(self.volume)
        if db < -6.0:
            self._say(T("  NOTE: more than 6 dB of attenuation was needed. The hardware level "
                        "into the ESP32 ADC is too hot - turn the RX trimmer (or the radio's "
                        "volume) down and re-run, so the bench can work near 0 dB."))
        elif db > 6.0:
            self._say(T("  NOTE: more than 6 dB of boost was needed. The hardware level into "
                        "the ESP32 ADC is too low - turn the RX trimmer up and re-run. "
                        "Boosting digitally also amplifies the sound card's own noise floor."))


# Backwards-compatible alias: older invocations and notes refer to the class
# by its previous name.
AutoVolumeCalibrator = VolumeSearch


def wait_ready(col: SerialCollector, settle: float) -> None:
    """Opening the port usually resets an ESP32 (DTR/RTS wired to EN/IO0).
    Wait for console traffic and for the boot to complete."""
    say(T("Waiting %.1f s for the ESP32 to be ready ...") % settle)
    end = time.monotonic() + settle
    while time.monotonic() < end:
        sleep_or_stop(0.2)
    if not col.alive.is_set():
        say(T("  WARNING: no data received from the serial port yet. The firmware "
              "may be quiet until it hears/sends something; continuing."))
    else:
        say(T("  serial is alive (%d console line(s) so far).") % col.lines_seen)


class _SimulatedVolumeSearch(VolumeSearch):
    """VolumeSearch driven by a simulated device instead of real hardware.

    The model is the plateau the real system exhibits: nothing decodes below
    -26 dB, the rate ramps up to 100% at -18 dB, stays there up to the
    clipping point (0 dB by default) and the firmware reports over-range at
    and above it. A correct search must land near the centre of that plateau
    (about -10.5 dB) no matter where it starts.

    Two optional knobs model the awkward cases:
      * `clip_warn_rate` - over-range warnings per packet at and above the
        clipping point (1.0 = on every packet; 0.02 = one in fifty);
      * `soft_zone_db` / `soft_warn_rate` - a band just below the clipping
        point where warnings are too rare to show in an 8-packet probe but do
        show in a full scoring batch.
    """

    def __init__(self, *a, **kw):
        self.knee_low_db = kw.pop("knee_low_db", -18.0)
        self.clip_at_db = kw.pop("clip_at_db", 0.0)
        self.clip_warn_rate = kw.pop("clip_warn_rate", 1.0)
        self.soft_zone_db = kw.pop("soft_zone_db", 0.0)
        self.soft_warn_rate = kw.pop("soft_warn_rate", 0.0)
        kw.setdefault("quiet", True)
        super().__init__(*a, **kw)

    def _probe(self, volume: float, key: float, target: int) -> dict:
        db = key
        warn_rate = 0.0
        if db >= self.clip_at_db:
            rate, warn_rate = 0.55, self.clip_warn_rate
        elif db >= self.knee_low_db:
            rate = 1.0
            if db >= self.clip_at_db - self.soft_zone_db:
                warn_rate = self.soft_warn_rate
        elif db >= self.knee_low_db - 8.0:
            rate = max(0.0, (db - (self.knee_low_db - 8.0)) / 8.0)
        else:
            rate = 0.0
        success = int(round(rate * target))
        warns = int(math.floor(warn_rate * target + 1e-9))
        return {"volume": volume, "db": key, "ok": success, "mismatch": 0, "corrupt": 0,
                "missing": target - success, "extra": 0, "mm": target,
                "success": success, "trials": target,
                "warns": warns, "clip_rate": warns / float(max(1, target))}


def selftest() -> int:
    """Unit tests that need no hardware, no audio and no waiting."""
    failures = []

    def check(name: str, cond: bool, detail: str = "") -> None:
        if cond:
            print(T("  PASS  %s") % name)
        else:
            failures.append(name)
            print(T("  FAIL  %s  %s") % (name, detail))

    print(T("Normalisation and parsing"))
    p_mm = make_packet("LU1ABC-0", "APRS", ["WIDE1-1"], b"hello\r", "mm")
    p_esp = make_packet("LU1ABC", "APRS", ["WIDE1-1*"], b"hello", "esp")
    check(T("SSID -0, digipeated * and trailing CR all normalise away"),
          p_mm.key_header() == p_esp.key_header() and p_mm.info == p_esp.info)
    check(T("trailing dot is kept (truncation must not pass as equal)"),
          make_packet("A", "B", [], b"hi.", "").info !=
          make_packet("A", "B", [], b"hi", "").info)
    check(T("payload LF becomes '.' like multimon-ng prints it"),
          make_packet("A", "B", [], b"a\nb", "").info == b"a.b")
    check(T("colon inside the payload does not break the header split"),
          parse_tnc2(b"LU1ABC>APRS::LU2DEF   :hi{01") is not None)
    check(T("ESP console line with a log prefix parses"),
          parse_esp_line(b"I (12345) aprs_service: RX: LU1ABC>APRS,WIDE1-1:test") is not None)

    print(T("LiveMatcher verdicts"))
    lm = LiveMatcher(5.0, offset_auto=False)
    lm.add_mm(10.0, p_mm)
    lm.add_esp(11.0, p_esp)
    check(T("match inside the window is ok"),
          [e[0] for e in lm.step(20.0, final=True)] == ["ok"])

    lm = LiveMatcher(5.0, offset_auto=False)
    lm.add_mm(10.0, p_mm)
    lm.add_esp(16.0, p_esp)
    check(T("match outside the window is missing + extra"),
          sorted(e[0] for e in lm.step(30.0, final=True)) == ["extra", "missing"])

    lm = LiveMatcher(5.0, offset_auto=False)
    bad_hdr = make_packet("LU1XYZ", "APRS", ["WIDE1-1"], b"hello", "esp-bad-hdr")
    lm.add_mm(10.0, p_mm)
    lm.add_esp(10.2, bad_hdr)
    verdicts = [e[0] for e in lm.step(20.0, final=True)]
    check(T("header-corrupt frame gives ONE verdict, not missing+extra"),
          verdicts == ["corrupt"], T("got %r") % (verdicts,))

    lm = LiveMatcher(5.0, offset_auto=False)
    bad_pl = make_packet("LU1ABC", "APRS", ["WIDE1-1"], b"hellX", "esp-bad-payload")
    lm.add_mm(10.0, p_mm)
    lm.add_esp(10.2, bad_pl)
    check(T("payload-corrupt frame is a mismatch only"),
          [e[0] for e in lm.step(20.0, final=True)] == ["mismatch"])

    lm = LiveMatcher(5.0, offset_auto=False)
    lm.add_mm(10.0, p_mm)
    lm.add_mm(11.0, p_mm)
    lm.add_esp(11.05, p_esp)
    lm.add_esp(10.05, p_esp)
    ev = [e for e in lm.step(30.0, final=True) if e[0] == "ok"]
    check(T("two identical beacons pair with the nearest transmission"),
          len(ev) == 2 and all(abs(e[4] - e[2]) < 0.2 for e in ev))

    lm = LiveMatcher(1.0, offset_auto=True)
    for i in range(OFFSET_MIN_SAMPLES):
        pk = make_packet("LU1ABC", "APRS", [], ("beacon %d" % i).encode(), "")
        lm.add_mm(float(i), pk)
        lm.add_esp(float(i) + 0.8, pk)
        lm.step(float(i) + 0.9)
    check(T("latency skew is learned from confirmed matches"),
          lm.offset_locked and abs(lm.offset - 0.8) < 0.05,
          T("offset=%.3f") % lm.offset)

    print(T("Statistics"))
    lo, hi = wilson(45, 50)
    check(T("Wilson interval at 45/50 is wide enough to swallow 1-packet noise"),
          (hi - lo) > 0.10, T("width=%.3f") % (hi - lo))
    check(T("dB round-trip"), abs(to_lin(to_db(0.37)) - 0.37) < 1e-9)

    print(T("VolumeSearch convergence (simulated plateau, centre = -10.5 dB)"))
    for start in (0.05, 1.0, 4.0):
        vs = _SimulatedVolumeSearch(
            ["a.wav", "b.wav"], None, 0.0, 5.0, None, [], start,
            batch_size=50, max_rounds=12)
        chosen = to_db(vs.run())
        # Cost is measured in batches of packets (what the run actually pays
        # in wall-clock time), not in probe calls: the threshold hunt makes
        # many cheap 8-packet probes on purpose.
        check(T("start %.2f converges to the plateau centre within budget "
                "(%.1f/%d batches, %d probes)") %
              (start, vs.budget_used, vs.max_rounds, vs.probes_used),
              abs(chosen - (-10.5)) <= 1.5 and vs.budget_used <= vs.max_rounds,
              T("chose %+.1f dB at cost %.1f") % (chosen, vs.budget_used))

    vs = _SimulatedVolumeSearch(["a.wav"], None, 0.0, 5.0, None, [], 1.0,
                                batch_size=50, max_rounds=12)
    vs.run()
    check(T("chosen level keeps margin below the clipping threshold"),
          to_db(vs.volume) <= vs.clip_db - MIN_CLIP_MARGIN_DB + 1e-6)

    print(T("Over-range ceiling"))

    def ceiling_respected(history) -> bool:
        ceiling = None
        for db, clipped in history:
            if ceiling is not None and db >= ceiling - 1e-6:
                return False
            if clipped:
                ceiling = db if ceiling is None else min(ceiling, db)
        return True

    for start in (0.05, 1.0, 4.0):
        vs = _SimulatedVolumeSearch([], None, 0.0, 5.0, None, [], start,
                                    batch_size=50, max_rounds=12)
        vs.wavs = ["a.wav"]
        vs.run()
        check(T("start %.2f: no probe is played at or above a level that reported over-range") % start,
              ceiling_respected(vs.history), T("history %s") % (vs.history,))

    for start, step in ((4.0, CLIP_STEP_DB), (1.0, 0.25)):
        vs = _SimulatedVolumeSearch(["a.wav"], None, 0.0, 5.0, None, [], start,
                                    batch_size=50, max_rounds=12, clip_step_db=step)
        vs.run()
        first = next((i for i, (_db, c) in enumerate(vs.history) if c), None)
        desc = []
        for db, c in (vs.history[first:] if first is not None else []):
            desc.append(db)
            if not c:
                break
        steps = [round(a - b, 2) for a, b in zip(desc, desc[1:])]
        check(T("below an over-range level the gain steps down by --clip_step_db (%.2f dB)") % step,
              bool(steps) and all(abs(s - step) < 0.02 for s in steps),
              T("steps %r") % (steps,))

    # Very small steps from a very hot start run out of descent budget. The
    # fallback must still land below the ceiling and never probe above it.
    vs = _SimulatedVolumeSearch(["a.wav"], None, 0.0, 5.0, None, [], 4.0,
                                batch_size=50, max_rounds=12, clip_step_db=0.25)
    chosen = to_db(vs.run())
    check(T("a descent that runs out of budget still ends below the over-range ceiling"),
          ceiling_respected(vs.history) and
          chosen <= vs.ceiling_db - MIN_CLIP_MARGIN_DB + 1e-6,
          T("ceiling %s, chose %+.1f dB") % (vs.ceiling_db, chosen))

    # Warnings too rare to show in an 8-packet probe (1 in 50) but present
    # just below the hard clipping point: the first scoring batch sees ONE.
    vs = _SimulatedVolumeSearch(["a.wav"], None, 0.0, 5.0, None, [], 1.0,
                                batch_size=50, max_rounds=12,
                                soft_zone_db=4.0, soft_warn_rate=0.02)
    chosen = to_db(vs.run())
    check(T("one over-range warning in a scoring batch lowers the ceiling and drops that level"),
          vs.ceiling_db is not None and vs.ceiling_db <= -3.0 and
          chosen <= vs.ceiling_db - MIN_CLIP_MARGIN_DB + 1e-6 and
          ceiling_respected(vs.history),
          T("ceiling %s, chose %+.1f dB") % (vs.ceiling_db, chosen))

    vs = _SimulatedVolumeSearch(["a.wav"], None, 0.0, 5.0, None, [], 4.0,
                                batch_size=50, max_rounds=12)
    vs._find_clip_threshold(to_db(4.0))       # interrupted right after phase 2
    safe = to_db(vs.safe_volume())
    check(T("an interrupted search falls back below the over-range ceiling, not to the start gain"),
          vs.ceiling_db is not None and safe <= vs.ceiling_db - MIN_CLIP_MARGIN_DB + 1e-6,
          T("safe %+.1f dB, ceiling %+.1f dB") % (safe, vs.ceiling_db or 0.0))

    print(T("PipeWire routing"))
    dump = [
        {"id": 30, "type": "PipeWire:Interface:Node", "info": {"props": {
            "media.class": "Audio/Sink", "object.serial": 35,
            "node.name": "alsa_output.usb-C-Media_USB_Audio-00.analog-stereo",
            "node.description": "USB Audio Analog Stereo", "node.nick": "USB Audio"}}},
        {"id": 32, "type": "PipeWire:Interface:Node", "info": {"props": {
            "media.class": "Audio/Sink", "object.serial": 37,
            "node.name": "alsa_output.pci-0000_00_1f.3.analog-stereo",
            "node.description": "Built-in Audio Analog Stereo"}}},
        {"id": 33, "type": "PipeWire:Interface:Node", "info": {"props": {
            "media.class": "Audio/Source", "object.serial": 38,
            "node.name": "alsa_input.usb-C-Media_USB_Audio-00.mono-fallback",
            "node.description": "USB Audio Mono"}}},
        {"id": 34, "type": "PipeWire:Interface:Metadata",
         "props": {"metadata.name": "default"},
         "metadata": [{"subject": 0, "key": "default.audio.sink", "type": "Spa:String:JSON",
                       "value": {"name": "alsa_output.pci-0000_00_1f.3.analog-stereo"}}]},
    ]
    sinks, dflt = parse_pw_dump(dump)
    check(T("pw-dump: only sinks are listed, and the default output is found"),
          [k.serial for k in sinks] == [35, 37] and
          dflt == "alsa_output.pci-0000_00_1f.3.analog-stereo")
    usb = sinks[0]
    try:
        forms = [resolve_sink(x, sinks, dflt).name for x in (
            usb.name, "35", "30", usb.label(), "* " + usb.label(), "usb audio", "C-Media")]
        ok_forms = forms == [usb.name] * len(forms)
    except ValueError as exc:
        ok_forms, forms = False, str(exc)
    check(T("an output is found by node name, serial, id, GUI label and description"),
          ok_forms, str(forms))
    check(T("an empty device and 'default' both give the PipeWire default output"),
          resolve_sink("", sinks, dflt).serial == 37 and
          resolve_sink("default", sinks, dflt).serial == 37)
    rejected = 0
    for bad in ("analog-stereo", "nosuchcard", "38"):   # ambiguous, unknown, a source
        try:
            resolve_sink(bad, sinks, dflt)
        except ValueError:
            rejected += 1
    check(T("an ambiguous or unknown output is rejected, never guessed"), rejected == 3)
    pc = build_pwcat_cmd(usb, 0.5, "monitor", "/tmp/r.wav")
    sc = build_play_cmd("x.wav", 1.0, True, "/tmp/r.wav")
    check(T("play chain: sox renders a file, pw-cat plays it pinned to the sink, no fallback"),
          sc[sc.index("-t") + 1] == "wav" and sc[sc.index("-c") + 2] == "/tmp/r.wav" and
          pc[pc.index("--target") + 1] == usb.name and pc[-1] == "/tmp/r.wav" and
          "node.dont-fallback = true" in pc[pc.index("-P") + 1])

    print(T("Termination with a wav set that decodes nothing"))
    # The old _run_batch_until restarted the wav iterator unconditionally, so
    # a silent or misrouted set looped for ever and only Ctrl-C ended the run.
    global run_one_wav
    real_run_one_wav = run_one_wav
    calls = [0]

    def _decodes_nothing(res, wav, *a, **kw):
        calls[0] += 1
        if calls[0] > 200:                      # the guard failed; stop the test
            raise AssertionError(T("run_one_wav called %d times - no pass limit") % calls[0])
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
        check(T("a probe over a silent wav set terminates instead of looping"),
              m["trials"] == 0 and calls[0] <= len(["a.wav", "b.wav"]) * MAX_WAV_PASSES,
              T("%d playback call(s)") % calls[0])
        vs2 = VolumeSearch(["a.wav"], None, 0.0, 5.0, _NoSerial(), [], 1.0,
                           batch_size=50, max_rounds=4)
        check(T("the whole search terminates on a silent wav set"),
              vs2.run() > 0.0)
        check(T("a silent wav set never raises the gain"),
              all(db <= to_db(1.0) + 1e-6 for db, _c in vs2.history),
              T("probed %s") % (vs2.history,))
    except AssertionError as exc:
        check(T("a probe over a silent wav set terminates instead of looping"),
              False, str(exc))
    finally:
        run_one_wav = real_run_one_wav

    print(T("Every volume probe restarts the wav list"))
    played = []

    def _one_packet_per_file(res, wav, *a, **kw):
        played.append(wav)
        res.mm_packets.append(make_packet("A", "B", [], wav.encode(), ""))
        res.ok += 1
        return False

    try:
        run_one_wav = _one_packet_per_file
        vs = VolumeSearch(["a.wav", "b.wav", "c.wav"], None, 0.0, 5.0, _NoSerial(), [],
                          1.0, batch_size=2, max_rounds=4)
        vs._measure(1.0, 2)
        first = list(played)
        del played[:]
        vs._measure(0.5, 2)
        check(T("a new volume replays the list from the first file, in order"),
              first == ["a.wav", "b.wav"] and played == ["a.wav", "b.wav"],
              "%s / %s" % (first, played))
    finally:
        run_one_wav = real_run_one_wav

    print(T("Direwolf: KISS, AX.25 and FCS"))
    check(T("CRC-16/X.25 check value of '123456789' is 0x906E"),
          fcs16_x25(b"123456789") == 0x906E, "%04X" % fcs16_x25(b"123456789"))
    frame = ax25_encode("LU1ABC-9", "APRS", ["WIDE1-1*", "WIDE2-1"],
                        b"`abc\x1cdef\xc0\xdb")
    f = fcs16_x25(frame)
    check(T("a frame followed by its FCS leaves the X.25 residue 0x0F47"),
          fcs16_x25(frame + bytes([f & 0xFF, f >> 8])) == 0x0F47)
    dec = ax25_decode(frame)
    check(T("AX.25 address field: SSID, path and the H bit ('*') decode"),
          dec is not None and dec[0] == "LU1ABC-9" and dec[1] == "APRS" and
          dec[2] == ["WIDE1-1*", "WIDE2-1"] and dec[3] == 0x03 and dec[4] == 0xF0,
          T("got %r") % (dec,))
    stream = b"junk" + kiss_encode(frame) + kiss_encode(b"\x00\x01") + b"\xc0\xc0"
    got = []
    dfr = KissDeframer()
    for i in range(0, len(stream), 3):              # split across "TCP reads"
        got.extend(dfr.feed(stream[i:i + 3]))
    check(T("KISS: escapes, split reads, garbage and empty frames"),
          len(got) == 2 and got[0] == (0, 0, frame) and got[1] == (0, 0, b"\x00\x01"),
          T("got %r") % (got,))
    check(T("malformed address field and non-APRS frames are rejected"),
          ax25_decode(b"\x82\xa0") is None and
          kiss_to_packet(ax25_encode("A", "B", [], b"x", control=0x00)) is None and
          kiss_to_packet(ax25_encode("A", "B", [], b"x", pid=0xCF)) is None)
    p_dw = kiss_to_packet(frame)
    mp = MultimonParser()
    mp.feed_line("AFSK1200: fm LU1ABC-9 to APRS-0 via WIDE1-1,WIDE2-1 UI  pid=F0")
    p_mm2 = mp.feed_line("`abc.def..")
    p_esp2 = parse_esp_line(b"I (5) aprs_service: RX: LU1ABC-9>APRS,WIDE1-1*,WIDE2-1:"
                            b"`abc\x1cdef\xc0\xdb")
    check(T("the same frame from Direwolf, multimon-ng and the ESP32 compares equal"),
          p_dw is not None and p_mm2 is not None and p_esp2 is not None and
          p_dw.same_content(p_mm2) and p_dw.same_content(p_esp2) and p_dw.fcs == f)
    ok_p = make_packet("LU1ABC-9", "APRS", ["WIDE1-1"], b"!3854.00S/06801.00W-", "")
    bad_p = make_packet("L%U1", "AP?S", [], b"\x07\x01zz", "")
    check(T("APRS plausibility: a real position passes, noise fails"),
          aprs_plausible(ok_p) and not aprs_plausible(bad_p))
    ds = DwSetup(rate=44100, fix_bits=0)
    conf = build_dw_conf(ds, 12345)
    cmd = build_dw_cmd("/tmp/dw.conf", ds)
    fp = pick_free_port()
    check(T("an automatic KISS port is inside the range Direwolf accepts"),
          DW_PORT_MIN <= fp <= DW_PORT_MAX, str(fp))
    check(T("Direwolf config: receive only, CRC-strict, KISS on one port only"),
          "FIX_BITS 0" in conf and "ADEVICE stdin null" in conf and
          conf.index("KISSPORT 0") < conf.index("KISSPORT 12345") and
          "AGWPORT 0" in conf and "PTT" not in conf and
          cmd[-1] == "-" and cmd[cmd.index("-r") + 1] == "44100")

    print(T("ClusterMatcher (three columns)"))

    def pk(src: str, info: bytes, path=("WIDE1-1",)) -> Packet:
        return make_packet(src, "APRS", list(path), info, "%s:%s" % (src, info))

    X = pk("LU1ABC", b">hello")
    X_bad = pk("LU1ABC", b">hellX")
    X_hdr = pk("LU1XYZ", b">hello")

    def rows_of(feed, sources=("mm", "dw", "esp")) -> List[Row]:
        cm = ClusterMatcher(5.0, offset_auto=False, sources=sources)
        for src, t, p in feed:
            cm.add(src, t, p)
        return cm.step(100.0, final=True)

    cases = [
        ("all three agree", [("mm", 10.0, X), ("dw", 10.0, X), ("esp", 10.5, X)],
         [(ROW_ALL, "===")]),
        ("ESP32 missed it", [("mm", 10.0, X), ("dw", 10.0, X)], [(ROW_ESP_MISS, "==-")]),
        ("ESP32 payload differs", [("mm", 10.0, X), ("dw", 10.0, X), ("esp", 10.4, X_bad)],
         [(ROW_ESP_DIFF, "==D")]),
        ("ESP32 header corrupt", [("mm", 10.0, X), ("dw", 10.0, X), ("esp", 10.4, X_hdr)],
         [(ROW_ESP_HDR, "==H")]),
        ("only multimon-ng", [("mm", 10.0, X), ("esp", 10.4, X)], [(ROW_MM_ONLY, "=-=")]),
        ("only Direwolf", [("dw", 10.0, X), ("esp", 10.4, X)], [(ROW_DW_ONLY, "-==")]),
        ("ESP32 only", [("esp", 10.0, X)], [(ROW_ESP_ONLY, "--?")]),
        ("references disagree", [("mm", 10.0, X_bad), ("dw", 10.0, X), ("esp", 10.3, X)],
         [(ROW_CONFLICT, "D==")]),
    ]
    for name, feed, want in cases:
        rows = rows_of(feed)
        got_rows = [(r.cls, "".join(r.cells[s] for s in ("mm", "dw", "esp"))) for r in rows]
        check(T("row: %s") % name, got_rows == want, T("got %r") % (got_rows,))

    rows = rows_of([("mm", 10.0, X), ("mm", 11.0, X), ("dw", 10.0, X), ("dw", 11.0, X),
                    ("esp", 11.05, X), ("esp", 10.05, X)])
    check(T("two identical beacons pair with the nearest transmission"),
          len(rows) == 2 and all(r.cls == ROW_ALL and
                                 abs(r.members["esp"][0] - r.members["mm"][0]) < 0.2
                                 for r in rows))
    cm = ClusterMatcher(1.0, offset_auto=True)
    for i in range(OFFSET_MIN_SAMPLES):
        pki = pk("LU1ABC", ("beacon %d" % i).encode())
        cm.add("mm", float(i), pki)
        cm.add("dw", float(i) + 0.1, pki)
        cm.add("esp", float(i) + 0.8, pki)
    check(T("Direwolf and ESP32 latency skews are learned independently"),
          cm.locked["dw"] and cm.locked["esp"] and abs(cm.offset["dw"] - 0.1) < 0.02 and
          abs(cm.offset["esp"] - 0.8) < 0.02,
          T("offset=%.3f") % cm.offset["esp"])

    # Cross-check: on the two sources LiveMatcher sees, ClusterMatcher must
    # reach the same verdicts, or the three-column rows would contradict the
    # legacy numbers printed under them.
    scenarios = [
        [("mm", 10.0, p_mm), ("esp", 11.0, p_esp)],
        [("mm", 10.0, p_mm), ("esp", 16.0, p_esp)],
        [("mm", 10.0, p_mm), ("esp", 10.2, bad_hdr)],
        [("mm", 10.0, p_mm), ("esp", 10.2, bad_pl)],
        [("mm", 10.0, p_mm), ("mm", 11.0, p_mm), ("esp", 11.05, p_esp), ("esp", 10.05, p_esp)],
        [("esp", 3.0, p_esp), ("mm", 20.0, X), ("esp", 20.5, X), ("mm", 30.0, X_bad)],
    ]
    same = True
    for sc in scenarios:
        lm = LiveMatcher(5.0, offset_auto=False)
        for src, t, p in sc:
            (lm.add_mm if src == "mm" else lm.add_esp)(t, p)
        legacy = sorted(e[0] for e in lm.step(100.0, final=True))
        new = sorted(row_legacy_verdict(r) for r in rows_of(sc, sources=("mm", "esp")))
        if legacy != new:
            same = False
            print("      %r != %r" % (legacy, new))
    check(T("ClusterMatcher agrees with LiveMatcher on multimon-ng vs ESP32"), same)

    st = row_stats(rows_of([("mm", 10.0, X_bad), ("dw", 10.0, X), ("esp", 10.3, X)]) +
                   rows_of([("esp", 10.0, bad_p)]) +
                   rows_of([("dw", 10.0, X), ("esp", 10.3, X)]))
    check(T("statistics: conflicts are not scored, ESP32-only frames are never successes"),
          st["conflict"] == 1 and st["vs_dw"] == [1, 0, 0, 0] and
          st["esp_only"] == 1 and st["implausible"] == 1 and st["union"] == 2,
          T("got %r") % (st,))
    fr = FileResult(name="x", mm_packets=[p_mm, p_mm], extra=[p_esp])
    check(T("the multimon-ng reference counts packets exactly as before"),
          ref_packet_count(fr, "multimon") == 3)

    print("")
    if failures:
        print(T("SELFTEST FAILED: %d of the checks above did not pass") % len(failures))
        return 1
    print(T("SELFTEST OK"))
    return 0


# --------------------------------------------------------------------------
# Graphical front-end (--gui)
# --------------------------------------------------------------------------
#
# Layout:
#   +--------------------------------------------------------------+
#   |  every command-line flag, as a form  (built from the parser) |
#   |  [ Start ] [ Stop ] [ Clear ] [ Reset ]  [ Language v ]     |
#   +------------------------------+-------------------------------+
#   |  CONSOLE (left)              |  SERIAL (right)               |
#   |  everything the program      |  every byte read from the     |
#   |  prints (stdout + stderr)    |  ESP32 port, unfiltered       |
#   +------------------------------+-------------------------------+
#
# The form is generated from build_parser(), so it can never disagree with the
# command line. tkinter is in the Python standard library: no new dependency.
# Worker threads never touch a widget - they only put items on a queue that
# the Tk main loop drains, because tkinter is not thread-safe.

class _QueueWriter:
    """File-like object that forwards everything written to it to a queue."""

    def __init__(self, q, tag: str) -> None:
        self.q = q
        self.tag = tag
        self.encoding = "utf-8"

    def write(self, text) -> int:
        if isinstance(text, bytes):
            text = text.decode("utf-8", "replace")
        if text:
            self.q.put(("console", self.tag, text))
        return len(text)

    def flush(self) -> None:
        pass

    def isatty(self) -> bool:
        return False


def _visible_serial_text(chunk: bytes) -> str:
    """Render raw serial bytes losslessly for the right-hand pane.

    Nothing is dropped or reinterpreted: bytes are mapped 1:1 through latin-1
    (so 8-bit Mic-E / binary payloads are still shown, one glyph per byte), CR
    and LF are kept as they arrive, and the remaining control characters -
    including the ESC of ANSI colour codes - are made visible as ^X / \\xNN
    instead of being swallowed or interpreted by the widget."""
    out = []
    for b in chunk:
        if b == 0x0A or b == 0x09:
            out.append(chr(b))
        elif b == 0x0D:
            # A bare CR would be drawn by Tk as a tiny "CR" glyph box. Show it
            # explicitly instead so nothing is hidden and nothing is ambiguous.
            out.append("\\r")
        elif b < 0x20:
            out.append("^" + chr(b + 0x40))
        elif b == 0x7F:
            out.append("^?")
        else:
            out.append(chr(b))
    return "".join(out)


def run_gui(ap: argparse.ArgumentParser, initial_values: Optional[dict] = None,
            initial_logs: Optional[dict] = None):
    """Open the window. Returns an exit code, or ("restart", values, logs) when
    the language was changed and run_gui_loop() must rebuild the window."""
    global _RAW_SERIAL_SINK
    try:
        import tkinter as tk
        from tkinter import ttk, filedialog, messagebox
        import tkinter.font as tkfont
    except ImportError:
        sys.stderr.write(T("--gui needs tkinter.  Debian/Ubuntu: sudo apt install python3-tk\n"))
        return 2
    import queue
    import shlex

    try:
        root = tk.Tk()
    except tk.TclError as exc:
        sys.stderr.write(T("Cannot open a display for --gui: %s\n") % exc)
        return 2
    root.title(T("test_aprs_wavs - esp32idf_APRS regression bench"))
    root.geometry("1280x820")
    root.minsize(900, 560)

    # ---- UI scale (the + / - buttons, top right) ----------------------
    # Every widget takes its size from a named Tk font, so changing those
    # fonts rescales the whole window - labels, entries, buttons, tooltips and
    # both consoles - without touching each widget. Padding in ttk follows the
    # font, and the window is resized by the same factor.
    SCALE_STEPS = (0.75, 0.9, 1.0, 1.15, 1.3, 1.5, 1.75, 2.0, 2.5, 3.0)
    base_fonts = {}
    for fname in ("TkDefaultFont", "TkTextFont", "TkFixedFont", "TkMenuFont",
                  "TkHeadingFont", "TkCaptionFont"):
        try:
            f = tkfont.nametofont(fname)
        except tk.TclError:
            continue
        size = f.cget("size")
        # Negative sizes are pixels, positive are points: keep the sign.
        base_fonts[fname] = (f, size if size != 0 else 10)
    scale_idx = [SCALE_STEPS.index(1.0)]
    scale_var = tk.StringVar(value="100%")
    scale_widgets = {}

    def apply_scale(idx: int) -> None:
        idx = max(0, min(len(SCALE_STEPS) - 1, idx))
        scale_idx[0] = idx
        factor = SCALE_STEPS[idx]
        for f, size in base_fonts.values():
            f.configure(size=(-1 if size < 0 else 1) * max(6, int(round(abs(size) * factor))))
        # Check indicators and scrollbars are drawn at a fixed pixel size by
        # the theme, not from a font, so they need scaling explicitly. On
        # themes that ignore a given option this is a harmless no-op.
        st = ttk.Style(root)
        st.configure("TCheckbutton", indicatorsize=int(round(12 * factor)))
        st.configure("TScrollbar", arrowsize=int(round(14 * factor)),
                     width=int(round(14 * factor)))
        st.configure("TPanedwindow", sashthickness=int(round(6 * factor)))
        # Entry/Combobox boxes (option values, --lang selector) default to
        # asymmetric top/bottom padding on several ttk themes, which draws
        # the text a few pixels above centre instead of centred in the box.
        # An explicit, symmetric vertical padding fixes that at every zoom
        # level; the horizontal padding matches the theme's usual look.
        vpad = max(1, int(round(3 * factor)))
        st.configure("TEntry", padding=(4, vpad, 4, vpad))
        st.configure("TCombobox", padding=(4, vpad, 4, vpad))
        # Window size is derived from the ORIGINAL size, not from the current
        # one, so + then - (or Ctrl-0) lands exactly where it started instead
        # of drifting. It is capped to the screen, and so is minsize: at 300 %
        # an uncapped minimum would force a window bigger than the monitor.
        max_w = root.winfo_screenwidth() - 40
        max_h = root.winfo_screenheight() - 80
        root.minsize(min(int(900 * factor), max_w), min(int(560 * factor), max_h))
        if root.winfo_width() > 1:          # not before the first layout
            if idx == SCALE_STEPS.index(1.0):
                w, h = 1280, 820
            else:
                w, h = int(1280 * factor), int(820 * factor)
            root.geometry("%dx%d" % (min(w, max_w), min(h, max_h)))
        scale_var.set("%d%%" % round(factor * 100))
        if "minus" in scale_widgets:
            scale_widgets["minus"].configure(state="normal" if idx > 0 else "disabled")
            scale_widgets["plus"].configure(
                state="normal" if idx < len(SCALE_STEPS) - 1 else "disabled")

    q = queue.Queue()

    # ---- collect the options from the parser --------------------------
    fields = []   # (action, kind, tk variable)
    for act in ap._actions:
        if not act.option_strings or act.dest in ("help", "gui", "lang"):
            continue                # --lang has its own selector in the button bar
        if isinstance(act, argparse._StoreTrueAction):
            kind = "flag"
        elif act.choices:
            kind = "choice"
        elif act.type is int:
            kind = "int"
        elif act.type is float:
            kind = "float"
        else:
            kind = "str"
        fields.append((act, kind))

    # ---- top: the form ------------------------------------------------
    top = ttk.LabelFrame(root, text=T("Options (same flags as the command line)"))
    top.pack(side="top", fill="x", padx=6, pady=(6, 3))

    vars_ = {}          # dest -> tk variable
    defaults = {}       # dest -> default (for Reset)
    COLS = 3            # option groups per row

    def flag_name(act) -> str:
        longs = [o for o in act.option_strings if o.startswith("--")]
        return (longs or act.option_strings)[0]

    def field_label(act) -> str:
        """Text shown on the form: the option name without its leading
        dashes ('--wav_dir' -> 'wav_dir'). The tooltip and the error
        messages keep the real command-line spelling via flag_name()."""
        return flag_name(act).lstrip("-")

    tips = {}

    def attach_tip(widget, text: str) -> None:
        """Hover tooltip with the flag's --help text."""
        tip = {"w": None}

        def show(_e):
            if tip["w"] or not text:
                return
            w = tk.Toplevel(widget)
            w.wm_overrideredirect(True)
            w.wm_geometry("+%d+%d" % (widget.winfo_rootx() + 12, widget.winfo_rooty() + widget.winfo_height() + 4))
            ttk.Label(w, text=text, justify="left", wraplength=int(460 * SCALE_STEPS[scale_idx[0]]), relief="solid",
                      borderwidth=1, padding=4, background="#ffffe0").pack()
            tip["w"] = w

        def hide(_e):
            if tip["w"]:
                tip["w"].destroy()
                tip["w"] = None
        widget.bind("<Enter>", show)
        widget.bind("<Leave>", hide)

    # PipeWire outputs for the --audio_device / --monitor_device pickers. The
    # boxes stay editable: anything typed is resolved exactly like on the
    # command line, and an empty box means the flag's default.
    sink_boxes = []

    def sink_labels() -> List[str]:
        try:
            sinks, default_name = pw_list_sinks()
        except RuntimeError:
            return []
        return [("* " if k.name == default_name else "") + k.label() for k in sinks]

    def refresh_sinks() -> None:
        labels = sink_labels()
        for box in sink_boxes:
            box.configure(values=[""] + labels)

    grid = ttk.Frame(top)
    grid.pack(fill="x", padx=6, pady=4)
    for c in range(COLS):
        grid.columnconfigure(c, weight=1, uniform="col")

    wav_dir_var_holder = {}   # filled in below, read by the file-selector panel
    wav_files_var_holder = {}

    for i, (act, kind) in enumerate(fields):
        if act.dest == "wav_files":
            # Driven entirely by the file-selector listbox built below, not
            # by a form entry: still register a StringVar (build_args() and
            # the language-change carry-over both go through vars_ generically)
            # but do not give it a grid cell or a widget of its own.
            var = tk.StringVar(value="" if act.default is None else str(act.default))
            defaults[act.dest] = "" if act.default is None else str(act.default)
            vars_[act.dest] = var
            wav_files_var_holder["var"] = var
            continue
        cell = ttk.Frame(grid)
        cell.grid(row=i // COLS, column=i % COLS, sticky="ew", padx=4, pady=2)
        helptxt = (act.help or "").replace("%%", "%")
        if kind == "flag":
            var = tk.BooleanVar(value=bool(act.default))
            defaults[act.dest] = bool(act.default)
            w = ttk.Checkbutton(cell, text=field_label(act), variable=var)
            w.pack(anchor="w")
        else:
            default = "" if act.default is None else str(act.default)
            var = tk.StringVar(value=default)
            defaults[act.dest] = default
            ttk.Label(cell, text=field_label(act)).pack(side="left", anchor="center")
            if act.dest in ("audio_device", "monitor_device"):
                ttk.Button(cell, text="\u21bb", width=3,
                           command=refresh_sinks).pack(side="right", anchor="center", padx=(4, 0))
                w = ttk.Combobox(cell, textvariable=var, width=18)
                sink_boxes.append(w)
            elif kind == "choice":
                w = ttk.Combobox(cell, textvariable=var, width=18, state="readonly",
                                 values=[str(c) for c in act.choices])
            else:
                w = ttk.Entry(cell, textvariable=var, width=18)
            w.pack(side="right", fill="x", expand=True, anchor="center", padx=(6, 0))
            if act.dest == "wav_dir":
                def browse(v=var):
                    d = filedialog.askdirectory(initialdir=v.get() or ".")
                    if d:
                        v.set(d)
                        refresh_wav_files(reset_selection=True)
                ttk.Button(cell, text="...", width=3, command=browse).pack(side="right", anchor="center", padx=(4, 0))
                w.bind("<KeyRelease>", lambda _e: refresh_wav_files())
                w.bind("<FocusOut>", lambda _e: refresh_wav_files())
                w.bind("<Return>", lambda _e: refresh_wav_files())
                wav_dir_var_holder["var"] = var
        vars_[act.dest] = var
        attach_tip(w, flag_name(act) + ": " + helptxt)

    refresh_sinks()

    # ---- .wav file selector --------------------------------------------
    # Shows every .wav file found in --wav_dir and lets the user pick which
    # ones to test (multiple selection). No selection = every file found,
    # exactly like the command line default (--wav_files empty). The list is
    # kept in sync with --wav_dir: on typing/Return/focus-out in that box, on
    # "Browse ...", and via the refresh button here (e.g. after dropping new
    # files into the folder without changing the path).
    wav_panel = ttk.LabelFrame(top, text=T("wav files found in wav_dir "
                                           "(select one or more to test; none = all)"))
    wav_panel.pack(fill="x", padx=6, pady=(0, 4))
    wav_inner = ttk.Frame(wav_panel)
    wav_inner.pack(fill="x", padx=6, pady=4)

    wav_list_var = tk.StringVar(value=[])
    wav_listbox = tk.Listbox(wav_inner, listvariable=wav_list_var, selectmode="extended",
                             height=6, exportselection=False, activestyle="dotbox")
    wav_scroll = ttk.Scrollbar(wav_inner, orient="vertical", command=wav_listbox.yview)
    wav_listbox.configure(yscrollcommand=wav_scroll.set)
    wav_listbox.pack(side="left", fill="both", expand=True)
    wav_scroll.pack(side="left", fill="y")
    attach_tip(wav_listbox, T("--wav_files") + ": " +
               T("comma-separated list of .wav file names (relative to "
                 "--wav_dir) to test; empty means every .wav file found in "
                 "--wav_dir (default: empty). In the GUI this is the file "
                 "selector next to --wav_dir."))

    wav_side = ttk.Frame(wav_inner)
    wav_side.pack(side="left", fill="y", padx=(8, 0))
    wav_count_var = tk.StringVar(value="")
    ttk.Label(wav_side, textvariable=wav_count_var, anchor="w").pack(anchor="w", pady=(0, 4))

    def on_wav_select(_e=None) -> None:
        names = [wav_listbox.get(i) for i in wav_listbox.curselection()]
        if "var" in wav_files_var_holder:
            wav_files_var_holder["var"].set(", ".join(names))
        n = wav_listbox.size()
        wav_count_var.set(T("%d selected of %d") % (len(names), n) if n
                          else T("no .wav files found"))

    def refresh_wav_files(reset_selection: bool = False) -> None:
        """Re-list wav_dir's .wav files. Keeps the current selection (by
        file name) unless reset_selection, which is used after Browse picks
        a brand new folder - an old selection from a different directory
        would otherwise silently carry over and could match nothing, or
        worse, a same-named but different file."""
        d = wav_dir_var_holder.get("var")
        d = d.get().strip() if d else ""
        prev_selected = set(wav_listbox.get(i) for i in wav_listbox.curselection())
        names = [os.path.basename(p) for p in find_wavs(d)] if d and os.path.isdir(d) else []
        wav_list_var.set(names)
        if not reset_selection and prev_selected:
            for idx, name in enumerate(names):
                if name in prev_selected:
                    wav_listbox.selection_set(idx)
        on_wav_select()

    ttk.Button(wav_side, text=T("Refresh"), command=lambda: refresh_wav_files()).pack(anchor="w")
    ttk.Button(wav_side, text=T("Select all"),
              command=lambda: (wav_listbox.selection_set(0, "end"), on_wav_select())).pack(anchor="w", pady=(4, 0))
    ttk.Button(wav_side, text=T("Select none"),
              command=lambda: (wav_listbox.selection_clear(0, "end"), on_wav_select())).pack(anchor="w", pady=(4, 0))
    wav_listbox.bind("<<ListboxSelect>>", on_wav_select)

    # Coming back from a language change (or on first build, from --wav_files
    # given on the command line): pre-select whatever --wav_files names, once
    # the directory listing is in.
    def preselect_from_var() -> None:
        var = wav_files_var_holder.get("var")
        wanted = set(parse_wav_files_option(var.get())) if var else set()
        if not wanted:
            return
        for idx in range(wav_listbox.size()):
            if wav_listbox.get(idx) in wanted:
                wav_listbox.selection_set(idx)
        on_wav_select()

    refresh_wav_files(reset_selection=True)
    preselect_from_var()

    # Coming back from a language change: put every field back as it was.
    if initial_values:
        for dest, val in initial_values.items():
            if dest in vars_:
                try:
                    vars_[dest].set(val)
                except Exception:
                    pass
        # wav_dir may have just changed above; re-list it, then re-apply the
        # carried-over --wav_files selection against the fresh listing.
        refresh_wav_files(reset_selection=True)
        preselect_from_var()

    # ---- buttons ------------------------------------------------------
    bar = ttk.Frame(top)
    bar.pack(fill="x", padx=6, pady=(0, 6))
    btn_start = ttk.Button(bar, text=T("Start"))
    btn_stop = ttk.Button(bar, text=T("Stop"), state="disabled")
    btn_clear = ttk.Button(bar, text=T("Clear consoles"))
    btn_reset = ttk.Button(bar, text=T("Reset defaults"))
    for b in (btn_start, btn_stop, btn_clear, btn_reset):
        b.pack(side="left", padx=(0, 6))
    # Top-right zoom controls. Packed right-to-left: "+", percentage, "-".
    btn_plus = ttk.Button(bar, text="+", width=3, command=lambda: apply_scale(scale_idx[0] + 1))
    btn_plus.pack(side="right")
    ttk.Label(bar, textvariable=scale_var, width=5, anchor="center").pack(side="right", padx=2)
    btn_minus = ttk.Button(bar, text="\u2212", width=3, command=lambda: apply_scale(scale_idx[0] - 1))
    btn_minus.pack(side="right")
    scale_widgets["plus"], scale_widgets["minus"] = btn_plus, btn_minus
    status = tk.StringVar(value=T("Idle"))
    ttk.Label(bar, textvariable=status).pack(side="right", padx=(0, 12))

    # ---- very bottom: live packet counters -----------------------------
    stats_var = tk.StringVar()
    stats_bar = ttk.Frame(root, relief="sunken", borderwidth=1)
    stats_bar.pack(side="bottom", fill="x", padx=6, pady=(0, 6))
    ttk.Label(stats_bar, textvariable=stats_var, font="TkFixedFont",
              anchor="w").pack(side="left", fill="x", expand=True, padx=6, pady=2)
    stats_gen = [None]

    def refresh_stats(force: bool = False) -> None:
        snap = _LIVE_STATS.snapshot()
        if not force and snap["gen"] == stats_gen[0]:
            return                              # nothing changed: no redraw
        stats_gen[0] = snap["gen"]
        pct_txt = ("%.2f%%" % pct(snap["missed"], snap["total"])
                   if snap["total"] else "--")
        if snap["dw_active"]:
            line = ("[%s]  " % (snap["phase"] or T("Live")) +
                    T("Total packets: %d   |   multimon-ng: %d   |   Direwolf: %d   |   "
                      "ESP32 decoded: %d   |   ESP32 missed: %d of %d (%s)   |   "
                      "conflicts: %d   |   ESP32-only: %d") %
                    (snap["total"], snap["mm"], snap["dw"], snap["esp"],
                     snap["missed"], snap["total"], pct_txt,
                     snap["conflict"], snap["esp_only"]))
        else:
            line = ("[%s]  " % (snap["phase"] or T("Live")) +
                    T("Total packets: %d   |   multimon-ng: %d   |   ESP32 decoded: %d"
                      "   |   ESP32 missed: %d of %d (%s)") %
                    (snap["total"], snap["mm"], snap["esp"],
                     snap["missed"], snap["total"], pct_txt))
        if snap["file_name"]:
            dur = snap["file_duration"]
            elapsed = min(snap["file_elapsed"], dur) if dur else snap["file_elapsed"]
            time_txt = "%s/%s" % (mmss(elapsed), mmss(dur))
            if snap["file_total"]:
                line += "   |   " + T("wav %d/%d [%s]: %s") % (
                    snap["file_n"], snap["file_total"], time_txt, snap["file_name"])
            else:
                line += "   |   " + T("wav [%s]: %s") % (time_txt, snap["file_name"])
        stats_var.set(line)

    refresh_stats(force=True)

    # ---- bottom: split console ---------------------------------------
    paned = ttk.PanedWindow(root, orient="horizontal")
    paned.pack(side="top", fill="both", expand=True, padx=6, pady=(3, 6))

    def make_pane(title: str):
        frame = ttk.LabelFrame(paned, text=title)
        txt = tk.Text(frame, wrap="none", undo=False, state="disabled",
                      background="#101418", foreground="#d8dee9",
                      insertbackground="#d8dee9", font="TkFixedFont")
        ys = ttk.Scrollbar(frame, orient="vertical", command=txt.yview)
        xs = ttk.Scrollbar(frame, orient="horizontal", command=txt.xview)
        txt.configure(yscrollcommand=ys.set, xscrollcommand=xs.set)
        xs.pack(side="bottom", fill="x")
        ys.pack(side="right", fill="y")
        txt.pack(side="left", fill="both", expand=True)
        txt.tag_configure("stderr", foreground="#ff8080")
        paned.add(frame, weight=1)
        return txt

    console = make_pane(T("Console  (program output)"))
    serial_txt = make_pane(T("Serial  (raw data from the ESP32, unfiltered)"))

    MAX_LINES = 20000       # keep the widgets bounded on very long runs

    def append(widget, text: str, tag=None) -> None:
        # Follow the tail only if the user has not scrolled up to read.
        at_end = widget.yview()[1] >= 0.999
        widget.configure(state="normal")
        widget.insert("end", text, tag) if tag else widget.insert("end", text)
        lines = int(widget.index("end-1c").split(".")[0])
        if lines > MAX_LINES:
            widget.delete("1.0", "%d.0" % (lines - MAX_LINES))
        widget.configure(state="disabled")
        if at_end:
            widget.see("end")

    # ---- worker -------------------------------------------------------
    state = {"thread": None, "saved_out": None, "saved_err": None,
             "restart": None, "after": None}

    # Coming back from a language change: put both consoles back as well, so
    # the log of a run already finished is not lost by switching language.
    if initial_logs:
        if initial_logs.get("console"):
            append(console, initial_logs["console"])
        if initial_logs.get("serial"):
            append(serial_txt, initial_logs["serial"])

    # ---- language selector (plain language name), in the button bar ----
    # Only the name is shown: Tk cannot draw the regional-indicator emoji
    # used for flags, so they turned into empty boxes / garbage glyphs.
    # Widgets cannot be re-translated in place, so picking another language
    # closes this window and run_gui_loop() builds it again, with the values
    # of the form carried over.
    lang_show = dict((c, LANG_NAMES[c]) for c in LANGS)
    lang_code = dict((v, k) for k, v in lang_show.items())
    lang_var = tk.StringVar(value=lang_show[current_language()])
    lbl_lang = ttk.Label(bar, text=T("Language:"))
    lbl_lang.pack(side="left", anchor="center", padx=(14, 4))
    lang_box = ttk.Combobox(bar, textvariable=lang_var, state="readonly",
                            width=14, values=[lang_show[c] for c in LANGS])
    lang_box.pack(side="left", anchor="center")
    attach_tip(lang_box, T("Language of the messages, the help texts and this "
                           "window. The default is the system language, or "
                           "English when it is not one of the three."))

    def cancel_pump() -> None:
        """Drop the pending pump() callback before the window goes away, or Tk
        prints an 'invalid command name' error when it fires into nothing."""
        if state["after"] is not None:
            try:
                root.after_cancel(state["after"])
            except Exception:
                pass
            state["after"] = None

    def on_lang(_e=None) -> None:
        code = lang_code.get(lang_var.get(), current_language())
        if code == current_language():
            return
        t = state["thread"]
        if t is not None and t.is_alive():
            messagebox.showinfo(T("Language"),
                                T("Stop the running test before changing the language."))
            lang_var.set(lang_show[current_language()])
            return
        set_language(code)
        state["restart"] = (
            dict((dest, var.get()) for dest, var in vars_.items()),
            {"console": console.get("1.0", "end-1c"),
             "serial": serial_txt.get("1.0", "end-1c")})
        cancel_pump()
        root.destroy()

    lang_box.bind("<<ComboboxSelected>>", on_lang)

    def build_args():
        """Turn the form into an argparse.Namespace, validating types."""
        ns = ap.parse_args([])                    # start from the true defaults
        for act, kind in fields:
            raw = vars_[act.dest].get()
            if kind == "flag":
                setattr(ns, act.dest, bool(raw))
                continue
            raw = raw.strip()
            if raw == "":
                setattr(ns, act.dest, act.default)     # blank = default / None
                continue
            try:
                val = int(raw) if kind == "int" else float(raw) if kind == "float" else raw
            except ValueError:
                raise ValueError(T("%s: %r is not a valid %s") %
                                 (flag_name(act), raw,
                                  T("integer") if kind == "int" else T("number")))
            setattr(ns, act.dest, val)
        ns.lang = current_language()      # so the echoed command line repeats it
        return ns

    def worker(ns):
        rc = 1
        try:
            rc = run_with_args(ns)
        except KeyboardInterrupt:
            q.put(("console", "stdout", T("\n[gui] stopped by user\n")))
            rc = 130
        except SystemExit as exc:
            rc = exc.code if isinstance(exc.code, int) else 1
        except BaseException as exc:                 # never die silently
            import traceback
            q.put(("console", "stderr", T("\n[gui] unexpected error:\n") + traceback.format_exc()))
            rc = 1
        finally:
            q.put(("done", rc))

    def start() -> None:
        global _RAW_SERIAL_SINK
        if state["thread"] and state["thread"].is_alive():
            return
        try:
            ns = build_args()
        except ValueError as exc:
            messagebox.showerror(T("Invalid option"), str(exc))
            return
        _STOP.clear()
        with _LIVE_LOCK:
            _LIVE_PROCS[:] = []
        _LIVE_STATS.reset("")
        refresh_stats(force=True)
        state["saved_out"], state["saved_err"] = sys.stdout, sys.stderr
        sys.stdout = _QueueWriter(q, "stdout")
        sys.stderr = _QueueWriter(q, "stderr")
        _RAW_SERIAL_SINK = lambda chunk: q.put(("serial", chunk))
        argv = " ".join(shlex.quote(a) for a in _namespace_to_argv(ap, ns))
        q.put(("console", "stdout", "$ test_aprs_wavs.py %s\n" % argv))
        t = threading.Thread(target=worker, args=(ns,), daemon=True)
        state["thread"] = t
        btn_start.configure(state="disabled")
        btn_stop.configure(state="normal")
        status.set(T("Running ..."))
        t.start()

    def stop() -> None:
        """Ask the worker to stop right now. This does NOT use an asynchronous
        exception (those are not delivered while the worker is blocked waiting
        on the audio player or in a sleep - see request_stop). It sets the
        stop flag and kills the child processes; the worker then raises
        KeyboardInterrupt itself, so the script's own Ctrl-C clean-up and the
        "reporting what has been tested so far" summary run unchanged."""
        t = state["thread"]
        if t is None or not t.is_alive():
            return
        btn_stop.configure(state="disabled")      # one click is enough
        status.set(T("Stopping ..."))
        request_stop()

    def finish(rc) -> None:
        global _RAW_SERIAL_SINK
        _RAW_SERIAL_SINK = None
        if state["saved_out"] is not None:
            sys.stdout, sys.stderr = state["saved_out"], state["saved_err"]
            state["saved_out"] = state["saved_err"] = None
        btn_start.configure(state="normal")
        btn_stop.configure(state="disabled")
        status.set(T("Finished (exit code %s)") % rc)
        append(console, T("\n[gui] finished, exit code %s\n") % rc)

    def clear() -> None:
        for w in (console, serial_txt):
            w.configure(state="normal")
            w.delete("1.0", "end")
            w.configure(state="disabled")

    def reset() -> None:
        for dest, var in vars_.items():
            var.set(defaults[dest])

    def pump() -> None:
        """Drain the queue in bounded batches so a flood of serial data cannot
        freeze the window, and coalesce consecutive items per pane so each
        batch costs one widget update instead of thousands."""
        con_parts, ser_parts = [], []          # (tag, text) / text
        done_rc = None
        for _ in range(4000):
            try:
                item = q.get_nowait()
            except queue.Empty:
                break
            if item[0] == "console":
                con_parts.append((item[1], item[2]))
            elif item[0] == "serial":
                ser_parts.append(_visible_serial_text(item[1]))
            elif item[0] == "done":
                done_rc = item[1]
        if con_parts:
            run_tag, buf = None, []
            for tag, text in con_parts:
                if tag != run_tag and buf:
                    append(console, "".join(buf), "stderr" if run_tag == "stderr" else None)
                    buf = []
                run_tag = tag
                buf.append(text)
            if buf:
                append(console, "".join(buf), "stderr" if run_tag == "stderr" else None)
        if ser_parts:
            append(serial_txt, "".join(ser_parts))
        refresh_stats()
        if done_rc is not None:
            finish(done_rc)
        try:
            state["after"] = root.after(50, pump)
        except tk.TclError:
            state["after"] = None          # the window is being destroyed

    for seq in ("<Control-plus>", "<Control-equal>", "<Control-KP_Add>"):
        root.bind(seq, lambda _e: apply_scale(scale_idx[0] + 1))
    for seq in ("<Control-minus>", "<Control-KP_Subtract>"):
        root.bind(seq, lambda _e: apply_scale(scale_idx[0] - 1))
    root.bind("<Control-0>", lambda _e: apply_scale(SCALE_STEPS.index(1.0)))
    apply_scale(scale_idx[0])

    btn_start.configure(command=start)
    btn_stop.configure(command=stop)
    btn_clear.configure(command=clear)
    btn_reset.configure(command=reset)

    def on_close() -> None:
        t = state["thread"]
        if t is not None and t.is_alive():
            if not messagebox.askyesno(T("Quit"),
                                       T("A test is still running. Stop it and quit?")):
                return
            stop()
            t.join(timeout=8)
        cancel_pump()
        root.destroy()
    root.protocol("WM_DELETE_WINDOW", on_close)

    pump()
    root.mainloop()
    if state["restart"] is not None:
        values, logs = state["restart"]
        return ("restart", values, logs)
    return 0


def _namespace_to_argv(ap: argparse.ArgumentParser, ns: argparse.Namespace) -> List[str]:
    """Reconstruct the equivalent command line for the log (values that differ
    from the default only), so a GUI run can be repeated from a shell."""
    argv = []  # type: List[str]
    for act in ap._actions:
        if not act.option_strings or act.dest in ("help", "gui"):
            continue
        val = getattr(ns, act.dest, act.default)
        if val == act.default:
            continue
        name = [o for o in act.option_strings if o.startswith("--")][0]
        if isinstance(act, argparse._StoreTrueAction):
            if val:
                argv.append(name)
        else:
            argv += [name, str(val)]
    return argv



def build_parser() -> argparse.ArgumentParser:
    """The one and only definition of the command-line flags. The GUI builds
    its form from this parser, so a flag added here shows up there by itself."""
    ap = argparse.ArgumentParser(
        description=T("Test esp32idf_APRS with a battery of real-APRS WAV files, "
                      "using multimon-ng as the reference decoder.") + " " +
        T("Direwolf, when installed, runs as a second, CRC-strict reference and "
          "the report shows multimon-ng / Direwolf / ESP32 for every packet."))
    ap.add_argument("--lang", choices=LANGS, default=None,
                    help=T("language of the messages, the help and the GUI: "
                           "en (English), es (Spanish), it (Italian). Default: "
                           "the system language, or English when the system "
                           "language is none of these three."))
    ap.add_argument("--wav_dir", default=".",
                    help=T("directory with the .wav files (default: current directory)"))
    ap.add_argument("--wav_files", default="",
                    help=T("comma-separated list of .wav file names (relative to "
                           "--wav_dir) to test; empty means every .wav file found in "
                           "--wav_dir (default: empty). In the GUI this is the file "
                           "selector next to --wav_dir."))
    ap.add_argument("--serial_port", default=DEFAULT_SERIAL,
                    help=T("ESP32 console serial port (default: %s)") % DEFAULT_SERIAL)
    ap.add_argument("--baud", type=int, default=SERIAL_BAUD,
                    help=T("serial speed, 8N1 (default: %d)") % SERIAL_BAUD)
    ap.add_argument("--audio_device", default=None,
                    help=T("PipeWire output (sink) wired to the ESP32 audio input: its "
                           "node name, serial, or a unique part of its description "
                           "(default: the PipeWire default output). See --list_audio."))
    ap.add_argument("--monitor_device", default=None,
                    help=T("PipeWire output (sink) to listen on while the test runs, "
                           "e.g. headphones; it plays exactly what the ESP32 gets. Same "
                           "forms as --audio_device, 'default' for the PipeWire default "
                           "output (default: no monitor)"))
    ap.add_argument("--monitor_volume", type=float, default=0.5,
                    help=T("PipeWire stream volume of the monitor, 0..1 (default 0.5). "
                           "Only the monitor: the ESP32 level is set by --volume alone."))
    ap.add_argument("--volume", type=float, default=1.0,
                    help=T("playback gain applied to the ESP32 leg only (default 1.0). "
                           "Used as the starting point for auto-volume calibration "
                           "unless --no_auto_volume is given."))
    ap.add_argument("--normalise", "--normalize", dest="normalise", action="store_true",
                    help=T("bring every wav to -1 dBFS in the play chain (sox 'gain -n -1') "
                           "so one gain is valid across recordings made at different levels, "
                           "and gains above 1.0 stop meaning 'clip inside sox'"))
    ap.add_argument("--headroom_db", type=float, default=HEADROOM_DB,
                    help=T("dB below the clipping threshold to fall back to when no "
                           "plateau could be scored (default %.0f)") % HEADROOM_DB)
    ap.add_argument("--clip_rate", type=float, default=CLIP_RATE_THRESHOLD,
                    help=T("over-range warnings per packet a level may produce and still "
                           "count as clean (default %.2f: a single warning marks the level "
                           "as clipping, and no probe goes that high again)") % CLIP_RATE_THRESHOLD)
    ap.add_argument("--clip_step_db", type=float, default=CLIP_STEP_DB,
                    help=T("once over-range has been reported, the search never raises the "
                           "gain again and steps DOWN by this many dB per probe until the "
                           "warnings stop (default %.2f)") % CLIP_STEP_DB)
    ap.add_argument("--volume_min", type=float, default=AUTO_VOLUME_MIN,
                    help=T("lowest gain the search may use (default %.2f)") % AUTO_VOLUME_MIN)
    ap.add_argument("--volume_max", type=float, default=AUTO_VOLUME_MAX,
                    help=T("highest gain the search may use (default %.2f)") % AUTO_VOLUME_MAX)
    ap.add_argument("--max_passes", type=int, default=MAX_WAV_PASSES,
                    help=T("passes over the wav set before a calibration probe gives up "
                           "(default %d)") % MAX_WAV_PASSES)
    ap.add_argument("--no_offset_auto", action="store_true",
                    help=T("do not estimate the ESP32-vs-multimon-ng latency skew; "
                           "compare raw timestamps instead"))
    ap.add_argument("--selftest", action="store_true",
                    help=T("run the built-in unit tests (no hardware, no audio) and exit"))
    ap.add_argument("--no_auto_volume", action="store_true",
                    help=T("skip the auto-volume calibration pass and use --volume as-is "
                           "for the whole run"))
    ap.add_argument("--auto_volume_batch", type=int, default=AUTO_VOLUME_BATCH,
                    help=T("number of packets to test per volume try during "
                           "auto-volume calibration - counts both multimon-ng "
                           "packets and ESP32-only ones multimon-ng missed "
                           "(default %d)") % AUTO_VOLUME_BATCH)
    ap.add_argument("--auto_volume_max_rounds", type=int, default=AUTO_VOLUME_MAX_ROUNDS,
                    help=T("search budget for the auto-volume pass, in batches of "
                           "--auto_volume_batch packets (default %d). Cheap 8-packet "
                           "clipping probes cost a fraction of a batch, full scoring "
                           "probes cost one each.") % AUTO_VOLUME_MAX_ROUNDS)
    ap.add_argument("--tail", type=float, default=DEFAULT_TAIL_SECONDS,
                    help=T("seconds to keep listening after each file (default %.1f)") % DEFAULT_TAIL_SECONDS)
    ap.add_argument("--settle", type=float, default=4.0,
                    help=T("seconds to wait after opening the serial port (default 4)"))
    ap.add_argument("--pause", type=float, default=1.0,
                    help=T("pause between files in seconds (default 1)"))
    ap.add_argument("--match_window", type=float, default=5.0,
                    help=T("an ESP32 packet answers a multimon-ng packet only if it "
                           "arrives within this many seconds of it; a packet the ESP32 "
                           "has not reported after this time is shown as NOT DECODED "
                           "(default 5)"))
    ap.add_argument("--no_play", action="store_true",
                    help=T("do not play audio to the sound card (only run multimon-ng; "
                           "useful to dry-run the parser)"))
    ap.add_argument("--mm_args", default="",
                    help=T("extra multimon-ng arguments, e.g. '-A' (quoted)"))
    ap.add_argument("--direwolf", choices=DW_MODES, default="auto",
                    help=T("Direwolf as a second reference decoder: auto (use it when it "
                           "is installed), on (required), off (legacy two-column bench). "
                           "Default: auto"))
    ap.add_argument("--reference", choices=REFERENCES, default="multimon",
                    help=T("reference that decides the exit code and the auto-volume "
                           "score: multimon (legacy, default), direwolf, or union (a "
                           "packet either reference decoded)"))
    ap.add_argument("--dw_rate", type=int, default=DW_RATE,
                    help=T("sample rate of the audio fed to Direwolf (default %d)") % DW_RATE)
    ap.add_argument("--dw_profile", default="",
                    help=T("Direwolf modem profile appended to 'MODEM 1200', e.g. 'A+' or "
                           "'E+' (default: Direwolf's own default)"))
    ap.add_argument("--dw_fix_bits", type=int, default=DW_FIX_BITS,
                    help=T("Direwolf FIX_BITS (default %d). Anything above 0 lets Direwolf "
                           "repair frames with a bad CRC, so it stops being a strict "
                           "reference.") % DW_FIX_BITS)
    ap.add_argument("--dw_kiss_port", type=int, default=0,
                    help=T("TCP port of Direwolf's KISS server, 1024..49151 (default 0: "
                           "pick a free port for every file)"))
    ap.add_argument("--dw_start_timeout", type=float, default=DW_START_TIMEOUT,
                    help=T("seconds to wait for Direwolf's KISS port to open (default %.1f)") %
                    DW_START_TIMEOUT)
    ap.add_argument("--dw_args", default="",
                    help=T("extra Direwolf arguments, e.g. '-P E+' (quoted)"))
    ap.add_argument("--report_csv", default=None,
                    help=T("write every multimon-ng / Direwolf / ESP32 row to this CSV "
                           "file (needs Direwolf)"))
    ap.add_argument("--list_audio", action="store_true",
                    help=T("list the PipeWire outputs (sinks) and exit"))
    ap.add_argument("--gui", action="store_true",
                    help=T("open a graphical front-end: every flag in a form at the "
                           "top, and below it a split console (left: program output, "
                           "right: raw unfiltered serial data from the ESP32)"))
    return ap


def main() -> int:
    # The language has to be known BEFORE the parser is built, because every
    # --help text is translated while it is being built.
    set_language(lang_from_argv(sys.argv[1:]))
    ap = build_parser()
    args = ap.parse_args()
    set_language(args.lang)        # None here means "use the system language"
    if args.gui:
        return run_gui_loop()
    return run_with_args(args)


def run_gui_loop() -> int:
    """Open the GUI and, whenever the Language selector changes, rebuild it in
    the new language while keeping whatever is typed in the form.

    Tk has no way to re-translate widgets that already exist, so the window is
    recreated: run_gui() returns ("restart", values) instead of an exit code,
    and the form values are handed back to the new window."""
    values, logs = None, None
    while True:
        ap = build_parser()        # help texts in the current language
        rc = run_gui(ap, values, logs)
        if isinstance(rc, tuple) and rc and rc[0] == "restart":
            values, logs = rc[1], rc[2]
            continue
        return rc


def run_with_args(args: argparse.Namespace) -> int:
    """Everything main() used to do after parsing. Shared by the CLI and the
    GUI worker thread."""
    if args.selftest:
        return selftest()

    if args.list_audio:
        try:
            sinks, default_name = pw_list_sinks()
        except RuntimeError as exc:
            sys.stderr.write("%s\n" % exc)
            return 2
        print_sink_list(sinks, default_name)
        return 0

    if args.auto_volume_batch < 1:
        sys.stderr.write(T("--auto_volume_batch must be >= 1 (got %d)\n") % args.auto_volume_batch)
        return 2
    if args.auto_volume_max_rounds < 1:
        sys.stderr.write(T("--auto_volume_max_rounds must be >= 1 (got %d)\n") %
                          args.auto_volume_max_rounds)
        return 2
    if args.max_passes < 1:
        sys.stderr.write(T("--max_passes must be >= 1 (got %d)\n") % args.max_passes)
        return 2
    if not (0.0 < args.volume_min < args.volume_max):
        sys.stderr.write(T("--volume_min must be > 0 and < --volume_max (got %g and %g)\n") %
                         (args.volume_min, args.volume_max))
        return 2
    if not (args.volume_min <= args.volume <= args.volume_max):
        sys.stderr.write(T("--volume must be within [%g, %g] (got %g)\n") %
                         (args.volume_min, args.volume_max, args.volume))
        return 2
    if not (0.0 <= args.clip_rate < 1.0):
        sys.stderr.write(T("--clip_rate must be in [0, 1) (got %g)\n") % args.clip_rate)
        return 2
    if not (0.0 < args.clip_step_db <= COARSE_STEP_DB):
        sys.stderr.write(T("--clip_step_db must be > 0 and <= %g (got %g)\n") %
                         (COARSE_STEP_DB, args.clip_step_db))
        return 2
    if args.match_window <= 0:
        sys.stderr.write(T("--match_window must be > 0 (got %g)\n") % args.match_window)
        return 2
    if not (0.0 <= args.monitor_volume <= 1.0):
        sys.stderr.write(T("--monitor_volume must be in [0, 1] (got %g)\n") %
                         args.monitor_volume)
        return 2
    if args.dw_rate < 8000:
        sys.stderr.write(T("--dw_rate must be >= 8000 (got %d)\n") % args.dw_rate)
        return 2
    if not (0 <= args.dw_fix_bits <= DW_FIX_BITS_MAX):
        sys.stderr.write(T("--dw_fix_bits must be in [0, %d] (got %d)\n") %
                         (DW_FIX_BITS_MAX, args.dw_fix_bits))
        return 2
    if args.dw_kiss_port != 0 and not (DW_PORT_MIN <= args.dw_kiss_port <= DW_PORT_MAX):
        sys.stderr.write(T("--dw_kiss_port must be 0 or in [%d, %d] (got %d)\n") %
                         (DW_PORT_MIN, DW_PORT_MAX, args.dw_kiss_port))
        return 2
    if args.dw_start_timeout <= 0:
        sys.stderr.write(T("--dw_start_timeout must be > 0 (got %g)\n") %
                         args.dw_start_timeout)
        return 2
    if args.reference != "multimon" and args.direwolf == "off":
        sys.stderr.write(T("--reference %s needs Direwolf, but --direwolf is off\n") %
                         args.reference)
        return 2

    check_tools(need_play=not args.no_play)

    # Direwolf: second, CRC-strict reference. Optional unless asked for.
    dw_setup = None  # type: Optional[DwSetup]
    if args.direwolf != "off":
        if _HAVE_DIREWOLF:
            import shlex
            dw_setup = DwSetup(rate=args.dw_rate, profile=(args.dw_profile or "").strip(),
                               fix_bits=args.dw_fix_bits, kiss_port=args.dw_kiss_port,
                               start_timeout=args.dw_start_timeout,
                               extra=shlex.split(args.dw_args) if args.dw_args else [])
        elif args.direwolf == "on":
            sys.stderr.write(T("--direwolf on, but the direwolf program was not found.  "
                               "Debian/Ubuntu: sudo apt install direwolf\n"))
            return 2
        else:
            sys.stderr.write(T("NOTE: direwolf not found - running with multimon-ng as the "
                               "only reference (install it for the three-column report: "
                               "sudo apt install direwolf)\n"))
    if args.reference != "multimon" and dw_setup is None:
        sys.stderr.write(T("--reference %s needs Direwolf, which is not available\n") %
                         args.reference)
        return 2
    if args.report_csv and dw_setup is None:
        sys.stderr.write(T("NOTE: --report_csv needs Direwolf; no CSV will be written\n"))
    _LIVE_STATS.set_dw_active(dw_setup is not None)

    # Resolve both outputs to concrete PipeWire nodes BEFORE anything plays,
    # so a typo is an error here and not audio sent to the wrong device.
    route = None  # type: Optional[AudioRoute]
    if not args.no_play:
        try:
            sinks, default_name = pw_list_sinks()
            test_sink = resolve_sink(args.audio_device, sinks, default_name)
            mon_sink = None
            if args.monitor_device and args.monitor_device.strip():
                mon_sink = resolve_sink(args.monitor_device, sinks, default_name)
        except RuntimeError as exc:
            sys.stderr.write("%s\n" % exc)
            return 2
        except ValueError as exc:
            sys.stderr.write("%s\n\n" % exc)
            print_sink_list(sinks, default_name)
            return 2
        if mon_sink is not None and mon_sink.name == test_sink.name:
            sys.stderr.write(T("--monitor_device and --audio_device are the same PipeWire "
                               "output (%s): the monitor stream would be mixed into the "
                               "ESP32 signal\n") % test_sink.name)
            return 2
        route = AudioRoute(test=test_sink, monitor=mon_sink,
                           monitor_volume=args.monitor_volume)

    if not os.path.isdir(args.wav_dir):
        sys.stderr.write(T("wav_dir not found: %s\n") % args.wav_dir)
        return 2
    selected = parse_wav_files_option(getattr(args, "wav_files", "") or "")
    wavs = find_wavs(args.wav_dir, select=selected or None)
    if not wavs:
        if selected:
            sys.stderr.write(T("None of the selected --wav_files were found as .wav "
                               "files in %s\n") % os.path.abspath(args.wav_dir))
        else:
            sys.stderr.write(T("No .wav files in %s\n") % os.path.abspath(args.wav_dir))
        return 2

    print(T("Found %d wav file(s) in %s") % (len(wavs), os.path.abspath(args.wav_dir)))
    if args.no_play:
        print(T("DRY RUN (--no_play): only multimon-ng runs; serial port and sound "
                "card are NOT used, so ESP32 results below are not meaningful."))
        if dw_setup is not None:
            print(T("  Direwolf runs too: the two references are compared, both fed at "
                    "%.0fx real time.") % DRY_RUN_SPEED)
    else:
        assert route is not None
        print(T("Serial: %s @ %d 8N1   Audio: %s") %
              (args.serial_port, args.baud, route.test.label()))
        if route.monitor is not None:
            print(T("Monitor: %s   (stream volume %.2f)") %
                  (route.monitor.label(), route.monitor_volume))
        else:
            print(T("Monitor: none"))
    if dw_setup is not None:
        print(T("Direwolf: %d Hz, modem profile %s, FIX_BITS %d, KISS port %s, "
                "reference for the exit code: %s") %
              (dw_setup.rate, dw_setup.profile or T("(default)"), dw_setup.fix_bits,
               dw_setup.kiss_port or T("auto"), args.reference))
        if dw_setup.fix_bits > 0:
            print(T("  WARNING: Direwolf is NOT a strict CRC reference: FIX_BITS=%d") %
                  dw_setup.fix_bits)
    sys.stdout.flush()

    col = None  # type: Optional[SerialCollector]
    if not args.no_play:
        try:
            col = SerialCollector(args.serial_port, args.baud)
        except (serial.SerialException, OSError) as exc:
            sys.stderr.write(T("Cannot open serial port %s: %s\n") % (args.serial_port, exc))
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
    dw_offset_seed = 0.0
    offset_auto = not args.no_offset_auto
    if not args.no_play and not args.no_auto_volume:
        _LIVE_STATS.reset(T("Calibration"))
        calibrator = VolumeSearch(
            wavs, route, args.tail, args.match_window, col,
            mm_extra, args.volume,
            batch_size=args.auto_volume_batch,
            max_rounds=args.auto_volume_max_rounds,
            headroom_db=args.headroom_db,
            clip_rate=args.clip_rate,
            vol_min=args.volume_min,
            vol_max=args.volume_max,
            max_passes=args.max_passes,
            normalise=args.normalise,
            offset_auto=offset_auto,
            clip_step_db=args.clip_step_db,
            dw=dw_setup, reference=args.reference)
        try:
            final_volume = calibrator.run()
            offset_seed = calibrator.offset
            dw_offset_seed = calibrator.dw_offset
        except KeyboardInterrupt:
            final_volume = calibrator.safe_volume()
            print(T("\nAuto-volume calibration interrupted - proceeding with the best "
                    "level known to be free of over-range so far: %.3f (%+.1f dB).") %
                  (final_volume, to_db(final_volume)))
            offset_seed = calibrator.offset
            dw_offset_seed = calibrator.dw_offset
        except DirewolfError as exc:
            sys.stderr.write("\n[dw] %s\n" % exc)
            col.stop()
            return 2

    results = []  # type: List[FileResult]
    dw_failed = False
    _LIVE_STATS.reset(T("Test"))
    try:
        for n, wav in enumerate(wavs, 1):
            print("\n[%d/%d] %s  (%.1f s)" % (n, len(wavs), os.path.basename(wav), wav_duration(wav)))
            sys.stdout.flush()
            res = FileResult(name=os.path.basename(wav))
            results.append(res)     # appended first: an interrupted file still counts
            _LIVE_STATS.set_file(n, len(wavs), os.path.basename(wav))
            run_one_wav(res, wav, route, final_volume, args.tail,
                        args.match_window, col, mm_extra, args.no_play,
                        normalise=args.normalise, offset_auto=offset_auto,
                        offset_seed=offset_seed, dw=dw_setup,
                        reference=args.reference, dw_offset_seed=dw_offset_seed)
            if res.dw_offset:
                dw_offset_seed = res.dw_offset
            print_file_report(res, dry_run=args.no_play)
            if dw_setup is not None:
                print_dw_file_report(res, dw_setup, dry_run=args.no_play)
            if not args.no_play:
                print_loss_resume(res, results)
            sleep_or_stop(args.pause)
    except KeyboardInterrupt:
        print(T("\nInterrupted - reporting what has been tested so far."))
    except DirewolfError as exc:
        dw_failed = True
        sys.stderr.write("\n[dw] %s\n" % exc)
        print(T("\nDirewolf failed - reporting what has been tested so far."))
    finally:
        col.stop()

    if args.report_csv and dw_setup is not None and results:
        try:
            n_rows = write_rows_csv(args.report_csv, results)
            print(T("CSV report: %d row(s) written to %s") % (n_rows, args.report_csv))
        except OSError as exc:
            sys.stderr.write(T("Cannot write the CSV report %s: %s\n") % (args.report_csv, exc))

    if args.no_play:
        n = sum(len(r.mm_packets) for r in results)
        print(T("\nDRY RUN finished: multimon-ng decoded %d packet(s) in %d file(s).") % (n, len(results)))
        rc = 0 if n else 2
        if dw_setup is not None and results:
            print(T("DRY RUN: Direwolf decoded %d packet(s).") %
                  sum(row_stats(r.rows)["dw"] for r in results))
            dw_rc = print_dw_summary(results, dw_setup, args.reference, dry_run=True)
            if args.reference != "multimon":
                rc = dw_rc
        return 2 if dw_failed else rc
    if not results:
        return 2
    rc = print_summary(results, final_volume)
    if dw_setup is not None:
        dw_rc = print_dw_summary(results, dw_setup, args.reference)
        if args.reference != "multimon":
            rc = dw_rc
    return 2 if dw_failed else rc


if __name__ == "__main__":
    sys.exit(main())
