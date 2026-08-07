.. _en-source-map:

==========
Source Map
==========

A tour of the repository, so you know where to look. Sizes are approximate.
First-party C totals ~37 k lines across ``main/`` + ``components/`` (excluding
``managed_components/``), of which ~6.8 k is the modem component and ~10 k is the
web admin.

Repository layout
=================

.. code-block:: text

   workspace-APRS/esp32_APRS_igate/
   ├── CMakeLists.txt          ← board definition (ADC/DAC/PTT/LED pins) + project()
   ├── partitions.csv          ← nvs / otadata / phy_init / ota_0 / ota_1 / storage (LittleFS)
   ├── sdkconfig               ← target=esp32, 4MB flash, custom partitions
   ├── dependencies.lock       ← idf 5.5.4, littlefs, esp-idf-lib bmp180/i2cdev/helpers
   ├── LICENSE                 ← GPL-3.0
   ├── schematics/             ← KiCad radio-interface schematic + PCB
   │
   ├── main/                                   (the application)
   │   ├── main.c              ← app_main, Wi-Fi bring-up/reconnect, boot order
   │   ├── app_config.c/.h     ← app_config_t, factory defaults, JSON load/save
   │   ├── storage.c           ← LittleFS mount/format/usage
   │   ├── aprs_service.c/.h   ← the glue: RX dispatch, TX helper, modem cfg, stats, loop test
   │   ├── aprs_filter.c/.h    ← payload classifier + range/prefix/budlist/3rd-party filters
   │   ├── aprs_coord.c/.h     ← lat/lon ↔ APRS text, ambiguity, symbol extraction
   │   ├── include/aprs_path.h ← path-preset bitmask → ",WIDE1-1,WIDE2-1" suffix builder
   │   ├── include/str_append.h ← bounded snprintf-append helper shared by the builders
   │   ├── include/json_store.h / json_escape.h ← streaming JSON writer + escaping
   │   ├── include/sched_time.h ← monotonic seconds used by every scheduler
   │   ├── beacon.c/.h         ← own-position beacons (trk / igate / digi)
   │   ├── weather.c/.h        ← own-station WX report: sensors_local refresh + WX beacon
   │   ├── telemetry.c/.h      ← own-station telemetry: A1–A5 + B1–B8, T#nnn beacon + metadata
   │   ├── beacon_scheduler.c/.h ← ONE shared task driving ALL periodic TX + query answers
   │   ├── bulletins.c/.h      ← APRS bulletins BLN1..BLN5 (own bulletins.json)
   │   ├── objects_items.c/.h  ← APRS Objects/Items (own objitems.json)
   │   ├── net_state.c/.h      ← "do we actually have internet?" flag
   │   ├── time_sync.c/.h      ← SNTP (UTC always), non-blocking state machine, timezone table (display only)
   │   └── cpu_freq.c/.h       ← esp_pm_configure() from the System page
   │
   ├── components/
   │   ├── esp32idf_radioamateur_modem/    (the soft-modem — the heart of the project)
   │   │   ├── esp32idf_radioamateur_modem.h  ← public API (config, RX callback, TX helpers)
   │   │   ├── include/…_config.h             ← ALL compile-time board/DSP constants
   │   │   ├── src/afsk.c                      ← ADC DMA ingest, AGC, decimation FIR, DAC ISR, PTT
   │   │   ├── src/modem.c                     ← correlators, DPLL, tone tables, DCD, calibration
   │   │   ├── src/ax25.c                      ← HDLC framer, NRZI, bit-stuffing, AX.25 codec, TX queue
   │   │   ├── src/fx25.c, lwfec/rs.c, gf.c    ← FX.25 Reed–Solomon FEC
   │   │   └── src/crc_ccit.c                  ← FCS
   │   │
   │   ├── igate/          ← APRS-IS TCP client, login, filters, dedup, RF→INET / INET→RF
   │   ├── digirepeater/   ← WIDEn-N / TRACEn-N / RELAY / ECHO / GATE path logic
   │   ├── message/        ← APRS messaging, ack/retry, the shared RX/TX conversation queue
   │   ├── query/          ← APRS query responder (?APRS?/?WX?/?IGATE? + directed set), answered from the scheduler task
   │   ├── lastheard/      ← in-RAM table of heard stations, one per callsign → dashboard JSON
   │   ├── trafficlog/     ← in-RAM ring of traffic lines → dashboard JSON (seq long-poll)
   │   ├── weather_telemetry/  ← protocol-level structs only (APRS101 WX + Telemetry fields)
   │   ├── sensors_local/      ← THE sensor driver framework
   │   │   ├── sensors_local.c              ← the dynamic registry
   │   │   ├── include/sensors_local.h      ← public API
   │   │   ├── include/sensor_local_properties.h ← per-driver capability descriptor
   │   │   └── drivers/<name>/              ← one folder per driver (auto-registered)
   │   │       ├── example/…_weather_example.c    ← random-data WEATHER skeleton
   │   │       ├── example/…_telemetry_example.c  ← random-data TELEMETRY skeleton
   │   │       └── bmp180/bmp180.c                ← real I2C temperature/pressure driver
   │   └── webconfig/      ← esp_http_server admin
   │       ├── web_server.c            ← route table
   │       ├── web_common.c            ← auth, form parsing, HTML shell, field helpers
   │       ├── pages/*.c               ← one file per admin page
   │       └── translations/           ← translations.h + lang_en/es/it.h
   │
   └── managed_components/                     (fetched by the component manager)
       ├── joltwallet__littlefs/
       ├── esp-idf-lib__bmp180/
       ├── esp-idf-lib__i2cdev/
       └── esp-idf-lib__esp_idf_lib_helpers/

Where to start reading
======================

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - If you want to understand…
     - Start in…
   * - The boot order and task layout
     - ``main/main.c``, then ``main/aprs_service.c``
   * - How a received frame is dispatched
     - ``aprs_msg_callback()`` in ``main/aprs_service.c``
   * - The DSP / why the sample rates are chosen
     - ``…_modem_config.h``, then ``src/modem.c`` / ``src/afsk.c``
   * - Gatewaying and filtering
     - ``components/igate/igate.c`` + ``main/aprs_filter.c``
   * - Attaching a sensor
     - ``components/sensors_local/`` and :ref:`en-sensor-framework`
   * - The configuration schema
     - ``main/include/app_config.h``
   * - A specific web page
     - the matching ``components/webconfig/pages/page_*.c``
