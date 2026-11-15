#!/bin/bash
# Regenerate the web admin screenshots embedded in the documentation.
#
# The real page handlers of components/webconfig are compiled on the host
# against the stub ESP-IDF headers in stubs/, together with the firmware's own
# configuration, storage and sensor code. Each language build renders every GET
# route of the web admin into a directory; a small HTTP server replays those
# responses to a headless Chromium, which screenshots every page and fieldset.
#
# Requirements: gcc, git, python3 with Pillow and Playwright (and a Chromium for
# Playwright, or CHROME=/path/to/chrome). Run as a user that may create
# /storage, the LittleFS mount point the firmware code writes to.
#
# usage: docs/tools/webadmin_shots/make_shots.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$HERE/../../.." && pwd)
DOCS="$R/docs"
CACHE="$HERE/cache"
mkdir -p "$CACHE"
[ -d "$CACHE/cJSON" ] || git clone --depth 1 https://github.com/DaveGamble/cJSON "$CACHE/cJSON"
[ -d "$CACHE/aprs-symbols" ] || git clone --depth 1 https://github.com/hessu/aprs-symbols "$CACHE/aprs-symbols"
[ -d "$CACHE/icons" ] || python3 "$HERE/make_icons.py" "$CACHE/aprs-symbols" "$CACHE/icons"
python3 "$HERE/gen_sources.py" "$R" "$CACHE/gen"

INC="-I$HERE/stubs -I$CACHE/gen -include $CACHE/gen/sdkconfig.h -I$CACHE/cJSON -I$R/main/include"
INC="$INC -I$R/components/webconfig/include -I$R/components/webconfig/translations"
for d in "$R"/components/*/include; do INC="$INC -I$d"; done
M=$R/components/esp32idf_radioamateur_modem
S=$R/components/sensors_local
INC="$INC -I$M -I$M/include -I$M/lwfec -I$S/drivers -I$S/drivers/bme280 -I$S/drivers/bmp180 -I$S/drivers/example"
DEFS=$(grep -o '"MODEM_[A-Z_]*=[-0-9]*"' "$R/CMakeLists.txt" | tr -d '"' | sed 's/^/-D/' | tr '\n' ' ')
CFLAGS="-std=gnu17 -O0 -w $INC $DEFS -DENABLE_FX25"

SRCS="$R/main/app_config.c $R/main/json_store.c $R/main/telemetry.c $R/main/bulletins.c $R/main/objects_items.c
      $R/main/aprs_filter.c $R/main/aprs_coord.c $R/main/aprs_dao.c $S/sensors_local.c $S/drivers/bme280/bme280.c
      $R/components/trafficlog/trafficlog.c $R/components/lastheard/lastheard.c $R/components/weather_telemetry/mice.c
      $CACHE/cJSON/cJSON.c $CACHE/gen/tz.c $CACHE/gen/dropnames.c $HERE/stubs_impl.c $HERE/hmain.c $HERE/sample.c"

mkdir -p /storage
PORT=8801
for L in 0:en 1:es 2:it; do
    N=${L%%:*}; LANG_ID=${L##*:}; B="$CACHE/build_$LANG_ID"; mkdir -p "$B"
    for f in "$R"/components/webconfig/*.c "$R"/components/webconfig/pages/*.c $SRCS; do
        b=$(basename "$f" .c); [ "$b" = web_server ] && continue
        gcc $CFLAGS -DLANGUAGE=$N -c "$f" -o "$B/$b.o"
    done
    gcc "$B"/*.o -lm -Wl,--wrap=readdir -o "$B/render"
    rm -rf /storage/* "$CACHE/site_$LANG_ID"
    # Two passes: the first creates every settings file, so the second
    # renders the File Storage page with all of them listed.
    "$B/render" "$CACHE/site_$LANG_ID"
    "$B/render" "$CACHE/site_$LANG_ID"
    python3 "$HERE/serve.py" "$CACHE/site_$LANG_ID" $PORT &
    SRV=$!
    sleep 1
    APRS_ICONS="$CACHE/icons" python3 "$HERE/shoot_all.py" $PORT "$CACHE/shots_$LANG_ID"
    kill $SRV 2>/dev/null || true
    # Refresh only the images the documentation actually embeds.
    for img in $(grep -oh '_static/webadmin/[a-z0-9_]*\.png' "$DOCS/$LANG_ID"/functionality/*.rst | sort -u | sed 's#_static/webadmin/##'); do
        cp "$CACHE/shots_$LANG_ID/$img" "$DOCS/$LANG_ID/_static/webadmin/"
    done
    PORT=$((PORT + 1))
done
echo "Screenshots refreshed in docs/{en,es,it}/_static/webadmin/"
