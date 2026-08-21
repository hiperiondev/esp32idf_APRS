.. _es-architecture:

============
Arquitectura
============

Secuencia de arranque
=====================

``app_main()`` se ejecuta en la tarea principal del sistema, cuya pila la fija
``CONFIG_ESP_MAIN_TASK_STACK_SIZE`` y no está pensada para alojar trabajo pesado
— ``esp_netif`` + ``esp_wifi`` + ``esp_http_server`` + cJSON pueden usar varios
KB de pila entre ellos. Así que ``app_main()`` hace solo las dos cosas que deben
preceder a todo, y luego cede el control a una tarea dedicada:

.. code-block:: text

   app_main()
    ├─ nvs_flash_init()          (borrar+reintentar en NO_FREE_PAGES / NEW_VERSION_FOUND)
    ├─ storage_init()            (montar LittleFS en /storage, autoformato en primer arranque)
    └─ xTaskCreate(app_task, 8192 B, prio 5)   ── y retorna; FreeRTOS recupera la tarea principal

   app_task()
    ├─ app_config_load()                  ← /storage/config.json, o escribir+cargar defaults de fábrica
    ├─ cpu_freq_apply()                   ← 80/160/240 MHz de la página System
    ├─ net_state_init()                   ← "aún no hay internet"
    ├─ wifi_init()                        ← AP / STA / AP+STA según g_config.wifi_mode
    ├─ vTaskDelay(10 ms)                  ← ceder para que corra IDLE; evita un falso disparo del TWDT
    ├─ time_sync_start()                  ← arma la máquina de estados SNTP (no bloqueante)
    ├─ web_server_start()                 ← esp_http_server, ~50 manejadores de URI, pila de 20 KB
    ├─ (confirmar imagen OTA válida si está pendiente-de-verificar)
    ├─ aprs_service_start()               ← ⚠ DEBE preceder a modem_init(): instala el callback de RX
    ├─ if (audio_modem_en) modem_init()   ← ⏳ SE BLOQUEA ~5 s calibrando el reloj real del ADC (una vez por arranque)
    │      └─ aprs_service_notify_modem_ready()
    └─ vTaskDelete(NULL)                  ← devuelve la pila de 8 KB de app_task al heap

Dos reglas de orden son críticas y están comentadas como tal en el código fuente:

#. **``aprs_service_start()`` antes de ``modem_init()``** — el módem empieza a
   entregar tramas *desde dentro de* ``modem_init()``; el callback de RX debe
   estar ya instalado.
#. **Las balizas arrancan antes de que el módem esté listo** — transmiten
   inmediatamente al entrar, así que ``aprs_service_send_tnc2()`` descarta tramas
   con un log de depuración hasta que se activa ``s_modemReady``, en lugar de
   llegar al escritor AX.25 antes de que la capa AX.25 esté inicializada.

Dentro de ``aprs_service_start()``
==================================

.. code-block:: text

   aprs_service_start()
    ├─ trafficlog_init / lastheard_init / message_init
    ├─ message_set_tx_handler / igate_set_inet2rf_handler
    ├─ modem_set_rx_callback(on_rx_frame)
    ├─ igate_start()                 ← siempre arrancado; queda en reposo cuando nada necesita APRS-IS
    ├─ beacon_start() / weather_start() / bulletins_start() / objitems_start() / telemetry_start()
    ├─ beacon_scheduler_start()      ← UNA tarea compartida acciona todo el TX periódico y las respuestas a consultas
    └─ xTaskCreate(serviceTickTask)  ← 1 Hz: refresco de meteo + reintento de mensajes + MdE de sincro horaria

Mapa de tareas
==============

.. list-table::
   :header-rows: 1
   :widths: 20 12 8 10 22 28

   * - Tarea
     - Pila
     - Prio
     - Núcleo
     - Creada por
     - Rol
   * - ``app_task``
     - 8192 B
     - 5
     - cualquiera
     - ``app_main``
     - arranque, luego se autoelimina
   * - RX DSP del módem
     - 4096 B
     - 10
     - **0**
     - ``AFSK_init()``
     - drena el anillo del ADC, ejecuta los demoduladores
   * - ``modem_svc``
     - 6144 B
     - 5
     - cualquiera
     - ``modem_init()``
     - acciona el TX, entrega tramas RX al callback
   * - ISR DMA del ADC
     - —
     - —
     - **0**
     - controlador
     - tramas de conversión → búfer de anillo
   * - Reloj de muestreo del DAC (GPTimer, nivel 3)
     - —
     - —
     - **1**
     - ``AFSK_init()``
     - una muestra del DAC cada 1/38400 s
   * - ``igate_task``
     - —
     - —
     - cualquiera
     - ``igate_start()``
     - socket APRS-IS, login, bombeo de RX, reconexión
   * - ``beacon_sched``
     - 14336 B
     - 4
     - cualquiera
     - ``beacon_scheduler_start()``
     - UNA tarea compartida: todo el TX periódico de la propia estación, más las
       respuestas a consultas APRS que se le difieren
   * - ``aprs_svc_tick``
     - 10240 B
     - 4
     - cualquiera
     - ``aprs_service_start()``
     - 1 Hz: refresco de meteo + reintento de mensajes + sincro horaria
   * - ``httpd``
     - 20480 B
     - —
     - cualquiera
     - ``web_server_start()``
     - administración web
   * - ``loop_diag``
     - 3072 B
     - 7
     - cualquiera
     - ``aprs_loop_test_run()``
     - transitoria: engancha los diagnósticos del módem mientras dura un LOOP TEST
   * - ``esp_timer``
     - —
     - —
     - —
     - IDF
     - retroceso de reconexión Wi-Fi

Flujo de datos
==============

.. image:: /_static/dataflow/dataflow_es.png
   :alt: Diagrama de la arquitectura de flujo de datos del ESP32 APRS iGate
   :align: center
   :width: 100%

El tope de backlog de TX de RF
==============================

``aprs_service_send_tnc2()`` permite un pequeño backlog en lugar de descartar en
cuanto una trama está en vuelo: hasta ``g_config.rf_tx_buffers`` tramas pueden
sentarse en el anillo antes de que un paquete nuevo se descarte. El valor se lee
fresco en cada llamada (así que el ajuste *TX buffers* aplica al siguiente
paquete, sin reinicio), y se fija a ``RF_TX_BUFFERS_MIN..RF_TX_BUFFERS_MAX`` —
siendo el máximo derivado de ``AX25_TX_FRAME_RING_MAX``, la profundidad usable
real del anillo, de modo que la capa de configuración nunca puede aceptar un
valor que el anillo no podría sostener. Solo a la tarea del planificador de
balizas se le permite *esperar* a que el anillo se drene (véase :ref:`es-beacons`);
todos los demás llamadores descartan inmediatamente, así que una pata de RF
ocupada nunca detiene la decodificación de RX ni el socket de APRS-IS.

Construcción de líneas TNC2
============================

Todos los módulos que ensamblan una línea de texto TNC2 — ``beacon.c``,
``weather.c``, ``objects_items.c``, ``query.c`` y ``telemetry.c`` — siguen la
misma convención: la línea se construye en un buffer de tamaño
``APRS_TNC2_BUF_SIZE`` (``main/include/aprs_service.h``), y un resultado igual
o mayor que ese tamaño, o mayor que ``APRS_TNC2_MAX_LEN``, se rechaza con un
aviso en el log en lugar de transmitirse truncado. Una línea a medio escribir
es indistinguible en el aire de una bien formada, así que rechazarla por
completo es el único resultado que nunca entrega a una estación receptora un
informe verosímil pero erróneo. Un módulo nuevo que construya líneas TNC2
debe seguir la misma convención.
