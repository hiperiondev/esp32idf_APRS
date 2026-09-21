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
     (8N1, 115200) and collects every "RX: <tnc2>" line it prints.

At the end of each file (and globally) it cross-checks the two sources:
every packet decoded by multimon-ng must have been decoded by the ESP32, with
the same content. A final summary reports total packets, percent decoded
correctly, percent decoded but with different content, and percent missing.
Packets the ESP32 decoded that multimon-ng did not are reported as "extra"
(not counted as errors: the ESP32 demodulator may simply be better).

Requirements
------------
  Python 3.7+ (uses dataclasses)
  pip install pyserial
  multimon-ng   (reference decoder)
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
import json
import math
import os
import re
import shutil
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
#   resolved  = multimon packets whose verdict is already known; the missed
#               percentage is taken over these, so packets still inside the
#               match window do not make the ESP32 look better than it is.

class LiveStats:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self.reset("")

    def reset(self, phase: str) -> None:
        with self._lock:
            self.phase = phase
            self.mm = self.ok = self.diff = self.missing = self.extra = 0
            self.gen = getattr(self, "gen", 0) + 1

    def add(self, mm: int = 0, ok: int = 0, diff: int = 0,
            missing: int = 0, extra: int = 0) -> None:
        with self._lock:
            self.mm = max(0, self.mm + mm)
            self.ok += ok
            self.diff += diff
            self.missing += missing
            self.extra += extra
            self.gen += 1

    def snapshot(self) -> dict:
        with self._lock:
            resolved = self.ok + self.diff + self.missing
            return {"gen": self.gen, "phase": self.phase,
                    "total": self.mm + self.extra, "mm": self.mm,
                    "esp": self.ok + self.diff + self.extra,
                    "missed": self.missing, "resolved": resolved}


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


def render_for_play(wav: str, volume: float, normalise: bool) -> str:
    """Render the play chain into a temporary WAV and return its path. The
    caller deletes it. Raises RuntimeError when sox fails."""
    global _RENDER_DIR
    import tempfile
    import atexit
    if _RENDER_DIR is None or not os.path.isdir(_RENDER_DIR):
        _RENDER_DIR = tempfile.mkdtemp(prefix="test_aprs_wavs-")
        atexit.register(shutil.rmtree, _RENDER_DIR, True)
    fd, out = tempfile.mkstemp(suffix=".wav", dir=_RENDER_DIR)
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


def run_one_wav(res: FileResult, wav: str, route: Optional[AudioRoute],
                volume: float, tail: float, window: float,
                collector: SerialCollector, mm_extra: List[str],
                dry_run: bool,
                stop_at_mm_packets: Optional[int] = None,
                normalise: bool = False,
                offset_auto: bool = True,
                offset_seed: float = 0.0,
                abort_on_overrange: bool = False) -> bool:
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
    If `abort_on_overrange` is set, playback is also cut short within about
    a quarter of a second of the firmware reporting "RX audio is over-range":
    the calibration uses it so a level that is already known to be too hot
    does not keep over-driving the ADC for the rest of the file.

    Returns True if a cutoff was hit, False if the file simply played to
    its natural end (or was interrupted by Ctrl-C)."""
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
    # ESP32 lines are attributed to this file by the moment they arrive. The
    # window opens right now, before playback starts, so nothing the previous
    # file already resolved is re-counted - but a frame the previous file was
    # still demodulating when it ended CAN arrive after this t0 and will be
    # ingested here, normally as an "extra". That is why the inter-file
    # `--pause` and `--tail` matter: they drain the ESP32 before the next file
    # opens its window.
    t0 = time.monotonic()
    t_window_start = t0

    # Every leg is produced from the same source file by sox so that the
    # decoders receive identical audio, whatever the WAV's own format is
    # (stereo, 8/16/24 bit, 44.1/48 kHz ...).
    #
    #   leg A: rendered WAV -> pw-cat --target <ESP32 sink>   [real time]
    #   leg M: rendered WAV -> pw-cat --target <monitor sink> [real time, optional]
    #   leg B: sox <wav> -> raw 22050 Hz s16 mono -> multimon-ng
    #                                                     [paced to real time]
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
    gt = None      # type: Optional[threading.Thread]
    done = threading.Event()
    duration = wav_duration(wav)
    matcher = LiveMatcher(window, offset_auto=offset_auto, offset=offset_seed)
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
        for p in play_procs + [sox_p, mm_p]:
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
            say(T("%06d [multimon  --:--.-] NOT DECODED") % n)
            say(T("       [esp32 only      %s] %s") % (mmss(t_esp - t0), ep.raw))
            check_stop()
            return
        say(T("%06d [multimon %s] %s") % (n, mmss(t_mm - t0), mp.raw))
        if kind == "ok":
            with res_lock:
                res.ok += 1
            _LIVE_STATS.add(ok=1)
            say(T("    OK [esp32    %s] %s") % (mmss(t_esp - t0), ep.raw))
        elif kind == "mismatch":
            with res_lock:
                res.mismatch.append((mp, ep))
            _LIVE_STATS.add(diff=1)
            say(T("       [esp32    %s] %s") % (mmss(t_esp - t0), ep.raw))
            say(T("      ! DECODED BUT DIFFERENT"))
        elif kind == "corrupt":
            with res_lock:
                res.corrupt.append((mp, ep))
            _LIVE_STATS.add(diff=1)
            say(T("       [esp32    %s] %s") % (mmss(t_esp - t0), ep.raw))
            say(T("      ! PAYLOAD OK BUT HEADER CORRUPT"))
        else:
            with res_lock:
                res.missing.append(mp)
            _LIVE_STATS.add(missing=1)
            say(T("       [esp32     --:--.-] NOT DECODED"))

    def feed_and_step(now: float, final: bool = False) -> None:
        items, ingested[0] = collector.items_from(ingested[0], t_window_start)
        for t, p in items:
            matcher.add_esp(t, p)
        for ev in matcher.step(now, final):
            show(ev)

    try:
        check_cancel()
        mm_p = track_proc(subprocess.Popen(mm_cmd, stdin=subprocess.PIPE,
                                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL))
        sox_p = track_proc(subprocess.Popen(sox_raw, stdout=subprocess.PIPE,
                                            stderr=subprocess.DEVNULL))

        # Feed multimon-ng from sox in REAL TIME (unless dry-running). This
        # keeps its decodes in step with what the ESP32 is hearing, so the
        # two decoders' packets are printed side by side.
        realtime = not dry_run

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
                            if _STOP.wait(ahead):     # stop requested: quit feeding
                                break
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
                _LIVE_STATS.add(mm=1)
                now = time.monotonic()
                if dry_run:      # dry run: no ESP32, just list the packet
                    say(T("%06d [multimon %s] %s") %
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
                if not dry_run:
                    feed_and_step(now)
                if (abort_on_overrange and not target_hit[0] and
                        collector.overrange_since(overrange_base) > 0):
                    target_hit[0] = True
                    kill_pipeline()
                if now - last_progress >= PROGRESS_SECONDS:
                    last_progress = now
                    say(T("       [progress %s / %s] multimon=%d  ok=%d  "
                          "not-decoded=%d  different=%d  (serial lines seen: %d)") %
                        (mmss(now - t0), mmss(duration), len(res.mm_packets),
                         res.ok, len(res.missing), len(res.mismatch),
                         collector.lines_seen))

        rt = threading.Thread(target=read_mm, daemon=True)
        pt = threading.Thread(target=pump, daemon=True)
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
        pt.start()

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
            sys.stderr.write(T("\n[mm] multimon-ng did not exit within 60 s - killing it\n"))
            mm_p.kill()
            try:
                mm_p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        rt.join(timeout=5)

        # Let the ESP32 finish the last frame and flush its console. At least
        # `window` seconds, so the last packets get the same chance to be
        # matched as every other one.
        sleep_or_stop(max(tail, window))
    except KeyboardInterrupt:
        interrupted = True
        raise
    finally:
        done.set()
        for p in play_procs + [sox_p, mm_p]:
            if p is not None and p.poll() is None:
                try:
                    p.kill()
                except Exception:
                    pass
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


# --------------------------------------------------------------------------
# Startup helpers
# --------------------------------------------------------------------------


_HAVE_STDBUF = False


def check_tools(need_play: bool = True) -> None:
    global _HAVE_STDBUF
    required = ("multimon-ng", "sox") + (("pw-cat", "pw-dump") if need_play else ())
    missing = [t for t in required if shutil.which(t) is None]
    if missing:
        sys.stderr.write(T("Missing required program(s): %s\n") % ", ".join(missing))
        sys.stderr.write(T("  Debian/Ubuntu: sudo apt install multimon-ng sox libsox-fmt-all "
                           "pipewire-bin\n"))
        sys.exit(2)
    _HAVE_STDBUF = shutil.which("stdbuf") is not None
    if not _HAVE_STDBUF:
        sys.stderr.write(T(
            "WARNING: `stdbuf` not found (package coreutils). multimon-ng's output will be\n"
            "  block-buffered on the pipe, so its packets may arrive in bursts and be\n"
            "  timestamped late, which shows up as spurious NOT DECODED verdicts.\n"))


def find_wavs(directory: str) -> List[str]:
    """Every .wav in the directory, whatever the case of the extension.

    Matching on the lower-cased name (rather than a case-sensitive suffix
    check) picks up ".WAV", ".Wav", etc. os.listdir() entries are already
    unique, so no de-duplication is needed."""
    try:
        entries = os.listdir(directory)
    except OSError:
        return []
    files = [os.path.join(directory, e) for e in entries
             if e.lower().endswith(".wav") and
             os.path.isfile(os.path.join(directory, e))]
    return sorted(files, key=lambda s: s.lower())


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
                 quiet: bool = False) -> None:
        self.wavs = wavs
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
        while len(batch.mm_packets) + len(batch.extra) < target:
            if self._cursor >= len(self.wavs):
                self._cursor = 0
                passes += 1
                if passes >= self.max_passes:
                    # Without this the loop restarts the wav set for ever when
                    # nothing decodes at all (silent files, muted card, wrong
                    # PipeWire output) and only Ctrl-C can end the run.
                    self._say(T("      probe incomplete: %d/%d packet(s) after %d pass(es) "
                                "over the wav set - check the audio routing and the files") %
                              (len(batch.mm_packets) + len(batch.extra), target, passes))
                    break
            wav = self.wavs[self._cursor]
            self._cursor += 1
            remaining = target - (len(batch.mm_packets) + len(batch.extra))
            res = FileResult(name=os.path.basename(wav))
            run_one_wav(res, wav, self.route, volume, self.tail,
                        self.window, self.collector, self.mm_extra,
                        dry_run=False, stop_at_mm_packets=remaining,
                        normalise=self.normalise, offset_auto=self.offset_auto,
                        offset_seed=self.offset,
                        abort_on_overrange=strict)
            if res.offset:
                self.offset = res.offset
            batch.mm_packets.extend(res.mm_packets)
            batch.esp_packets.extend(res.esp_packets)
            batch.ok += res.ok
            batch.mismatch.extend(res.mismatch)
            batch.corrupt.extend(res.corrupt)
            batch.missing.extend(res.missing)
            batch.extra.extend(res.extra)
            if strict and self.collector.overrange_since(overrange_before) > 0:
                # With the strict default one warning already settles it: this
                # level clips. Playing on would only keep the ADC over-driven.
                break

        n_mm = len(batch.mm_packets)
        n_extra = len(batch.extra)
        warns = self.collector.overrange_since(overrange_before)
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
        probed = [m for m in self.cache.values() if m["mm"] > 0 or m["extra"] > 0]
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

    for i, (act, kind) in enumerate(fields):
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
            ttk.Label(cell, text=field_label(act)).pack(side="left")
            if act.dest in ("audio_device", "monitor_device"):
                ttk.Button(cell, text="\u21bb", width=3,
                           command=refresh_sinks).pack(side="right", padx=(4, 0))
                w = ttk.Combobox(cell, textvariable=var, width=18)
                sink_boxes.append(w)
            else:
                w = ttk.Entry(cell, textvariable=var, width=18)
            w.pack(side="right", fill="x", expand=True, padx=(6, 0))
            if act.dest == "wav_dir":
                def browse(v=var):
                    d = filedialog.askdirectory(initialdir=v.get() or ".")
                    if d:
                        v.set(d)
                ttk.Button(cell, text="...", width=3, command=browse).pack(side="right", padx=(4, 0))
        vars_[act.dest] = var
        attach_tip(w, flag_name(act) + ": " + helptxt)

    refresh_sinks()

    # Coming back from a language change: put every field back as it was.
    if initial_values:
        for dest, val in initial_values.items():
            if dest in vars_:
                try:
                    vars_[dest].set(val)
                except Exception:
                    pass

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
        pct_txt = ("%.2f%%" % pct(snap["missed"], snap["resolved"])
                   if snap["resolved"] else "--")
        stats_var.set("[%s]  " % (snap["phase"] or T("Live")) +
                      T("Total packets: %d   |   multimon-ng: %d   |   ESP32 decoded: %d"
                        "   |   ESP32 missed: %d of %d (%s)") %
                      (snap["total"], snap["mm"], snap["esp"],
                       snap["missed"], snap["resolved"], pct_txt))

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
    lbl_lang.pack(side="left", padx=(14, 4))
    lang_box = ttk.Combobox(bar, textvariable=lang_var, state="readonly",
                            width=14, values=[lang_show[c] for c in LANGS])
    lang_box.pack(side="left")
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
                      "using multimon-ng as the reference decoder."))
    ap.add_argument("--lang", choices=LANGS, default=None,
                    help=T("language of the messages, the help and the GUI: "
                           "en (English), es (Spanish), it (Italian). Default: "
                           "the system language, or English when the system "
                           "language is none of these three."))
    ap.add_argument("--wav_dir", default=".",
                    help=T("directory with the .wav files (default: current directory)"))
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

    check_tools(need_play=not args.no_play)

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
    wavs = find_wavs(args.wav_dir)
    if not wavs:
        sys.stderr.write(T("No .wav files in %s\n") % os.path.abspath(args.wav_dir))
        return 2

    print(T("Found %d wav file(s) in %s") % (len(wavs), os.path.abspath(args.wav_dir)))
    if args.no_play:
        print(T("DRY RUN (--no_play): only multimon-ng runs; serial port and sound "
                "card are NOT used, so ESP32 results below are not meaningful."))
    else:
        assert route is not None
        print(T("Serial: %s @ %d 8N1   Audio: %s") %
              (args.serial_port, args.baud, route.test.label()))
        if route.monitor is not None:
            print(T("Monitor: %s   (stream volume %.2f)") %
                  (route.monitor.label(), route.monitor_volume))
        else:
            print(T("Monitor: none"))
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
            clip_step_db=args.clip_step_db)
        try:
            final_volume = calibrator.run()
            offset_seed = calibrator.offset
        except KeyboardInterrupt:
            final_volume = calibrator.safe_volume()
            print(T("\nAuto-volume calibration interrupted - proceeding with the best "
                    "level known to be free of over-range so far: %.3f (%+.1f dB).") %
                  (final_volume, to_db(final_volume)))
            offset_seed = calibrator.offset

    results = []  # type: List[FileResult]
    _LIVE_STATS.reset(T("Test"))
    try:
        for n, wav in enumerate(wavs, 1):
            print("\n[%d/%d] %s  (%.1f s)" % (n, len(wavs), os.path.basename(wav), wav_duration(wav)))
            sys.stdout.flush()
            res = FileResult(name=os.path.basename(wav))
            results.append(res)     # appended first: an interrupted file still counts
            run_one_wav(res, wav, route, final_volume, args.tail,
                        args.match_window, col, mm_extra, args.no_play,
                        normalise=args.normalise, offset_auto=offset_auto,
                        offset_seed=offset_seed)
            print_file_report(res, dry_run=args.no_play)
            if not args.no_play:
                print_loss_resume(res, results)
            sleep_or_stop(args.pause)
    except KeyboardInterrupt:
        print(T("\nInterrupted - reporting what has been tested so far."))
    finally:
        col.stop()

    if args.no_play:
        n = sum(len(r.mm_packets) for r in results)
        print(T("\nDRY RUN finished: multimon-ng decoded %d packet(s) in %d file(s).") % (n, len(results)))
        return 0 if n else 2
    return print_summary(results, final_volume) if results else 2


if __name__ == "__main__":
    sys.exit(main())
