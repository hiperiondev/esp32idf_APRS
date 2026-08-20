.. _en-architecture:

============
Architecture
============

Boot sequence
=============

``app_main()`` runs on the system main task, whose stack is set by
``CONFIG_ESP_MAIN_TASK_STACK_SIZE`` and is not meant to host heavy work —
``esp_netif`` + ``esp_wifi`` + ``esp_http_server`` + cJSON can use several KB of
stack between them. So ``app_main()`` does only the two things that must precede
everything, then hands off to a dedicated task:

.. code-block:: text

   app_main()
    ├─ nvs_flash_init()          (erase+retry on NO_FREE_PAGES / NEW_VERSION_FOUND)
    ├─ storage_init()            (mount LittleFS at /storage, auto-format on first boot)
    └─ xTaskCreate(app_task, 8192 B, prio 5)   ── and returns; FreeRTOS reclaims the main task

   app_task()
    ├─ app_config_load()                  ← /storage/config.json, or write+load factory defaults
    ├─ cpu_freq_apply()                   ← 80/160/240 MHz from the System page
    ├─ net_state_init()                   ← "no internet yet"
    ├─ wifi_init()                        ← AP / STA / AP+STA per g_config.wifi_mode
    ├─ vTaskDelay(10 ms)                  ← yield so IDLE runs; avoids a false TWDT trip
    ├─ time_sync_start()                  ← arms the SNTP state machine (non-blocking)
    ├─ web_server_start()                 ← esp_http_server, ~50 URI handlers, 20 KB stack
    ├─ (confirm OTA image valid if pending-verify)
    ├─ aprs_service_start()               ← ⚠ MUST precede modem_init(): installs the RX callback
    ├─ if (audio_modem_en) modem_init()   ← ⏳ BLOCKS ~5 s calibrating the real ADC clock (once per boot)
    │      └─ aprs_service_notify_modem_ready()
    └─ vTaskDelete(NULL)                  ← returns app_task's 8 KB stack to the heap

Two ordering rules are load-bearing and commented as such in the source:

#. **``aprs_service_start()`` before ``modem_init()``** — the modem starts
   delivering frames *from inside* ``modem_init()``; the RX callback must
   already be installed.
#. **Beacons start before the modem is ready** — they transmit immediately on
   entry, so ``aprs_service_send_tnc2()`` drops frames with a debug log until
   ``s_modemReady`` is set, rather than reaching the AX.25 writer before the
   AX.25 layer is initialised.

Inside ``aprs_service_start()``
===============================

.. code-block:: text

   aprs_service_start()
    ├─ trafficlog_init / lastheard_init / message_init
    ├─ message_set_tx_handler / igate_set_inet2rf_handler
    ├─ modem_set_rx_callback(on_rx_frame)
    ├─ igate_start()                 ← always started; self-idles when nothing needs APRS-IS
    ├─ beacon_start() / weather_start() / bulletins_start() / objitems_start() / telemetry_start()
    ├─ beacon_scheduler_start()      ← ONE shared task drives all periodic TX and query answers
    └─ xTaskCreate(serviceTickTask)  ← 1 Hz: weather refresh + message retry + time-sync SM

Task map
========

.. list-table::
   :header-rows: 1
   :widths: 20 12 8 10 22 28

   * - Task
     - Stack
     - Prio
     - Core
     - Created by
     - Role
   * - ``app_task``
     - 8192 B
     - 5
     - any
     - ``app_main``
     - boot, then deletes itself
   * - modem RX DSP
     - 4096 B
     - 10
     - **0**
     - ``AFSK_init()``
     - drains the ADC ring, runs the demodulators
   * - ``modem_svc``
     - 6144 B
     - —
     - any
     - ``modem_init()``
     - drives TX, delivers RX frames to the callback
   * - ADC DMA ISR
     - —
     - —
     - **0**
     - driver
     - conversion frames → ring buffer
   * - DAC sample clock (GPTimer, lvl 3)
     - —
     - —
     - **1**
     - ``AFSK_init()``
     - one DAC sample every 1/38400 s
   * - ``igate_task``
     - —
     - —
     - any
     - ``igate_start()``
     - APRS-IS socket, login, RX pump, reconnect
   * - ``beacon_sched``
     - 14336 B
     - 4
     - any
     - ``beacon_scheduler_start()``
     - ONE shared task: all periodic own-station TX, plus the APRS query answers
       deferred to it
   * - ``aprs_svc_tick``
     - 10240 B
     - 4
     - any
     - ``aprs_service_start()``
     - 1 Hz: weather refresh + message retry + time sync
   * - ``httpd``
     - 20480 B
     - —
     - any
     - ``web_server_start()``
     - web admin
   * - ``loop_diag``
     - 3072 B
     - 7
     - any
     - ``aprs_loop_test_run()``
     - transient: latches modem diagnostics for the duration of a LOOP TEST
   * - ``esp_timer``
     - —
     - —
     - —
     - IDF
     - Wi-Fi reconnect back-off

Data flow
=========

.. image:: /_static/dataflow/dataflow_en.png
   :alt: ESP32 APRS iGate data flow architecture diagram
   :align: center
   :width: 100%

The RF TX backlog cap
=====================

``aprs_service_send_tnc2()`` allows a small backlog rather than discarding the
moment one frame is in flight: up to ``g_config.rf_tx_buffers`` frames may sit in
the ring before a new packet is dropped. The value is read fresh on every call
(so the *TX buffers* setting applies to the very next packet, no reboot), and is
clamped to ``RF_TX_BUFFERS_MIN..RF_TX_BUFFERS_MAX`` — the max being derived from
``AX25_TX_FRAME_RING_MAX``, the ring's true usable depth, so the config layer can
never accept a value the ring could not hold. Only the beacon scheduler task is
allowed to *wait* for the ring to drain (see :ref:`en-beacons`); every other
caller drops immediately, so a busy RF leg never stalls RX decode or the APRS-IS
socket.

Building TNC2 lines
====================

Every module that assembles a TNC2 text line — ``beacon.c``, ``weather.c``,
``objects_items.c``, ``query.c`` and ``telemetry.c`` — follows the same
convention: the line is built into a buffer sized ``APRS_TNC2_BUF_SIZE``
(``main/include/aprs_service.h``), and a result at or past that size, or past
``APRS_TNC2_MAX_LEN``, is refused with a warning log rather than transmitted
truncated. A half-written line is indistinguishable on air from a well-formed
one, so refusing it outright is the only outcome that never hands a receiving
station a plausible but wrong report. A new module that builds TNC2 lines
should follow the same convention.
