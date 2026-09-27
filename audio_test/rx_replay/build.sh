#!/bin/sh
# Builds modem_replay against the firmware's own modem sources.
#
#   ./build.sh [OUTPUT] [PROJECT_ROOT]
#
# OUTPUT defaults to ./modem_replay, PROJECT_ROOT to the parent of audio_test.
# The decimation coefficients are copied out of afsk.c into
# resample_coeffs.inc next to OUTPUT on every build, so the replay always
# filters with the table the firmware was built with.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-"$HERE/modem_replay"}
ROOT=${2:-"$(dirname "$(dirname "$HERE")")"}
M="$ROOT/components/esp32idf_radioamateur_modem"
GEN=$(dirname "$OUT")
mkdir -p "$GEN"

python3 - "$M/src/afsk.c" "$GEN/resample_coeffs.inc" <<'PY'
import re, sys
src = open(sys.argv[1]).read()
m = re.search(r'#if MODEM_RESAMPLE_RATIO == 8\s*\n#define FILTER_TAPS 48\s*\n(static const float resample_coeffs\[FILTER_TAPS\] = \{.*?\};)', src, re.S)
if not m:
    sys.exit("build.sh: the 48-tap decimation table was not found in " + sys.argv[1])
open(sys.argv[2], "w").write(m.group(1) + "\n")
PY

gcc -O2 -w -DENABLE_FX25 \
    -I"$GEN" -I"$M/test/host/stubs" -I"$M" -I"$M/include" -I"$M/lwfec" \
    "$HERE/modem_replay.c" \
    "$M/src/modem.c" "$M/src/ax25.c" "$M/src/crc_ccit.c" "$M/src/fx25.c" \
    "$M/lwfec/rs.c" "$M/lwfec/gf.c" \
    -lm -o "$OUT"
