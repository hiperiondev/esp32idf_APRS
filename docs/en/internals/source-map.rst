.. _en-source-map:

==========
Source Map
==========

A tour of the repository, so you know where to look. Sizes are approximate.
First-party C totals ~69 k lines across ``main/`` + ``components/`` (excluding
``managed_components/``), of which ~7.1 k is the modem component and ~18.9 k is
the web admin.

Repository layout
=================

.. code-block:: text

   workspace-APRS/esp32_APRS_igate/
   ├── CMakeLists.txt          ← board definition (ADC/DAC/PTT/LED pins) + project()
   ├── partitions.csv          ← nvs / otadata / phy_init / ota_0 / ota_1 / storage (LittleFS)
   ├── sdkconfig               ← target=esp32, 4MB flash, custom partitions
   ├── dependencies.lock       ← idf 6.0.2, littlefs, esp-idf-lib bmp280/bmp180/i2cdev/helpers
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
   │   ├── include/aprs_minutes.h ← the one degrees/minutes quantisation: base position field, Mic-E bytes and "!DAO!" digit all read it
   │   ├── include/aprs_free_text.h ← own-station free-text field builder: reserved-char strip + "!x!" no-archive marker
   │   ├── include/aprs_df.h  ← "CSE/SPD/BRG/NRQ" DF report encoder shared by beacon.c and objects_items.c
   │   ├── include/aprs_bm.h  ← BrandMeister classifier for APRS-IS lines: APBMxx tocall, DMR path alias, entry station
   │   ├── include/aprs_path.h ← path-preset bitmask → ",WIDE1-1,WIDE2-1" suffix builder
   │   ├── include/str_append.h ← bounded snprintf-append helper shared by the builders
   │   ├── json_store.c + include/json_store.h / json_escape.h ← shared JSON-file store scaffolding (one stdio buffer) + streaming writer/escaping
   │   ├── include/must_check.h ← the "caller must read this return value" attribute
   │   ├── include/app_version.h ← firmware version string shown on the About page
   │   ├── include/reset_reason.h ← boot cause as a label, shared by the dashboard strip and the Telegram start-up notice
   │   ├── include/sched_time.h ← monotonic seconds used by every scheduler
   │   ├── beacon.c/.h         ← own-position beacons (trk / igate / digi)
   │   ├── aprs_dao.c/.h       ← "!DAO!" precision/datum extension (aprs12/datum.txt), consumed by beacon.c
   │   ├── weather.c/.h        ← own-station WX report: sensors_local refresh + WX beacon
   │   ├── telemetry.c/.h      ← own-station telemetry: A1–A5 + B1–B8, T#nnn beacon + metadata
   │   ├── gps.c/.h            ← NMEA GNSS receiver on its own UART: sentence parser + snapshot
   │   ├── telegram_app.c/.h  ← Telegram bot store (own telegram.json) + supervised bring-up + diagnosis + /status and /sensors answers
   │   ├── beacon_scheduler.c/.h ← ONE shared task driving ALL periodic TX + query answers
   │   ├── bulletins.c/.h      ← APRS bulletins BLN1..BLN5 (own bulletins.json)
   │   ├── objects_items.c/.h  ← APRS Objects/Items (own objitems.json)
   │   ├── net_state.c/.h      ← "do we actually have internet?" flag
   │   ├── time_sync.c/.h      ← SNTP (UTC always), non-blocking state machine, timezone table (display only)
   │   ├── cpu_freq.c/.h       ← esp_pm_configure() from the System page
   │   └── heap_monitor.c/.h   ← periodic free/largest/minimum heap line + optional integrity sweep + shared heavy-network-op lock
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
   │   ├── digirepeater/   ← n-N path logic driven by the operator's alias table, plus optional legacy destination-SSID routing
   │   ├── message/        ← APRS messaging, ack/retry, the shared RX/TX conversation queue
   │   ├── query/          ← APRS query responder (?APRS?/?WX?/?IGATE? + directed set), answered from the scheduler task
   │   ├── esp_telegram_bot/   ← Telegram Bot HTTPS transport: token, URLs, TLS clients, multipart upload
   │   ├── telegram_service/  ← long polling, command dispatch, authorization, alerts, remote parameters
   │   ├── winlink/        ← Winlink radio e-mail over APRSLink: session state machine, command queue, mailbox (own winlink.json)
   │   ├── lastheard/      ← in-RAM table of heard stations, one per callsign → dashboard JSON
   │   ├── trafficlog/     ← in-RAM ring of traffic lines → dashboard JSON (seq long-poll)
   │   ├── weather_telemetry/  ← APRS101 WX + Telemetry protocol structs, plus mice.c: the
   │   │                          full Mic-E encoder/decoder (aprs_mice_encode()/_decode()),
   │   │                          used by main/beacon.c (TX) and main/aprs_filter.c (RX)
   │   ├── sensors_local/      ← THE sensor driver framework
   │   │   ├── sensors_local.c              ← the dynamic registry
   │   │   ├── include/sensors_local.h      ← public API
   │   │   ├── include/sensor_local_properties.h ← per-driver capability descriptor
   │   │   ├── include/sensors_local_i2c.h  ← shared I2C bus pins (reserved GPIOs)
   │   │   └── drivers/<name>/              ← one folder per driver (auto-registered)
   │   │       ├── example/…_weather_example.c    ← random-data WEATHER skeleton
   │   │       ├── example/…_telemetry_example.c  ← random-data TELEMETRY skeleton
   │   │       ├── bme280/bme280.c                ← real I2C BME280/BMP280 driver (default)
   │   │       └── bmp180/bmp180.c                ← same, older BMP180 (off by default)
   │   └── webconfig/      ← esp_http_server admin
   │       ├── web_server.c            ← route table
   │       ├── web_common.c            ← auth, form parsing, HTML shell, field helpers
   │       ├── web_help.c              ← option-label → help-text table behind the question
   │       │                             mark that closes every label
   │       ├── logcapture.c            ← on-demand esp_log_set_vprintf() mirror of the serial
   │       │                             console into an in-RAM ring → Logs page JSON (seq
   │       │                             poll), with an idle timeout
   │       ├── pages/*.c               ← one file per admin page
   │       └── translations/           ← translations.h + lang_en/es/it.h
   │
   └── managed_components/                     (fetched by the component manager)
       ├── joltwallet__littlefs/
       ├── esp-idf-lib__bmp280/
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

Position ambiguity and the range gate
======================================

``main/aprs_filter.c`` decodes an incoming packet's position for the local
RF→INET range gate independently of ``main/aprs_coord.c``, since it only
needs a latitude/longitude pair, not the full APRS text encoder/decoder.
When the position carries ambiguity (APRS101 chapter 6: the least
significant minute digits replaced with spaces), the decoder resolves the
blanked digits to the centre of the resulting ambiguity box rather than to
its low corner, since the centre is the best available estimate of the
station's true position and it is what feeds the great-circle distance
check in ``components/igate/igate.c``.
