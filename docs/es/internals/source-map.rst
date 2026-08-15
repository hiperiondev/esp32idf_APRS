.. _es-source-map:

======================
Mapa del código fuente
======================

Un recorrido por el repositorio, para que sepas dónde mirar. Los tamaños son
aproximados. El C de primera parte suma ~42 k líneas entre ``main/`` +
``components/`` (excluyendo ``managed_components/``), de las cuales ~6,8 k son el
componente del módem y ~11 k la administración web.

Disposición del repositorio
===========================

.. code-block:: text

   workspace-APRS/esp32_APRS_igate/
   ├── CMakeLists.txt          ← definición de placa (pines ADC/DAC/PTT/LED) + project()
   ├── partitions.csv          ← nvs / otadata / phy_init / ota_0 / ota_1 / storage (LittleFS)
   ├── sdkconfig               ← target=esp32, flash 4MB, particiones personalizadas
   ├── dependencies.lock       ← idf 5.5.4, littlefs, esp-idf-lib bmp180/i2cdev/helpers
   ├── LICENSE                 ← GPL-3.0
   ├── schematics/             ← esquema KiCad de interfaz de radio + PCB
   │
   ├── main/                                   (la aplicación)
   │   ├── main.c              ← app_main, puesta en marcha/reconexión Wi-Fi, orden de arranque
   │   ├── app_config.c/.h     ← app_config_t, defaults de fábrica, carga/guardado JSON
   │   ├── storage.c           ← montaje/formato/uso LittleFS
   │   ├── aprs_service.c/.h   ← el pegamento: despacho RX, ayudante TX, cfg módem, stats, loop test
   │   ├── aprs_filter.c/.h    ← clasificador de carga útil + filtros rango/prefijo/budlist/terceros
   │   ├── aprs_coord.c/.h     ← lat/lon ↔ texto APRS, ambigüedad, extracción de símbolo
   │   ├── include/aprs_free_text.h ← constructor de campos de texto libre propios: quita reservados + marcador "!x!" de no archivar
   │   ├── include/aprs_path.h ← bitmask de presets de ruta → sufijo ",WIDE1-1,WIDE2-1"
   │   ├── include/str_append.h ← ayudante de append snprintf acotado, compartido por los constructores
   │   ├── include/json_store.h / json_escape.h ← escritor JSON en streaming + escapado
   │   ├── include/sched_time.h ← segundos monotónicos usados por cada planificador
   │   ├── beacon.c/.h         ← balizas de posición propia (trk / igate / digi)
   │   ├── aprs_dao.c/.h       ← extensión de precisión/datum "!DAO!" (aprs12/datum.txt), usada por beacon.c
   │   ├── weather.c/.h        ← informe WX propio: refresco sensors_local + baliza WX
   │   ├── telemetry.c/.h      ← telemetría propia: A1–A5 + B1–B8, baliza T#nnn + metadatos
   │   ├── beacon_scheduler.c/.h ← UNA tarea compartida que acciona TODO el TX periódico + respuestas a consultas
   │   ├── bulletins.c/.h      ← boletines APRS BLN1..BLN5 (bulletins.json propio)
   │   ├── objects_items.c/.h  ← Objetos/Ítems APRS (objitems.json propio)
   │   ├── net_state.c/.h      ← bandera "¿tenemos internet de verdad?"
   │   ├── time_sync.c/.h      ← SNTP (siempre UTC), máquina de estados no bloqueante, tabla de zonas horarias (solo visualización)
   │   └── cpu_freq.c/.h       ← esp_pm_configure() de la página System
   │
   ├── components/
   │   ├── esp32idf_radioamateur_modem/    (el módem por software — el corazón del proyecto)
   │   │   ├── esp32idf_radioamateur_modem.h  ← API pública (config, callback RX, ayudantes TX)
   │   │   ├── include/…_config.h             ← TODAS las constantes de placa/DSP en compilación
   │   │   ├── src/afsk.c                      ← ingesta DMA ADC, AGC, FIR diezmado, ISR DAC, PTT
   │   │   ├── src/modem.c                     ← correladores, DPLL, tablas de tonos, DCD, calibración
   │   │   ├── src/ax25.c                      ← encuadrador HDLC, NRZI, bit-stuffing, códec AX.25, cola TX
   │   │   ├── src/fx25.c, lwfec/rs.c, gf.c    ← FEC Reed–Solomon FX.25
   │   │   └── src/crc_ccit.c                  ← FCS
   │   │
   │   ├── igate/          ← cliente TCP APRS-IS, login, filtros, dedup, RF→INET / INET→RF
   │   ├── digirepeater/   ← lógica de ruta n-N guiada por la tabla de alias del operador, más el ruteo heredado por SSID de destino, opcional
   │   ├── message/        ← mensajería APRS, ack/reintento, la cola de conversación RX/TX compartida
   │   ├── query/          ← respondedor de consultas APRS (?APRS?/?WX?/?IGATE? + dirigidas), respondidas desde la tarea del planificador
   │   ├── lastheard/      ← tabla en RAM de estaciones oídas, una por indicativo → JSON del panel
   │   ├── trafficlog/     ← anillo en RAM de líneas de tráfico → JSON del panel (long-poll por seq)
   │   ├── weather_telemetry/  ← solo estructuras de nivel de protocolo (campos WX + Telemetría APRS101)
   │   ├── sensors_local/      ← EL marco de controladores de sensores
   │   │   ├── sensors_local.c              ← el registro dinámico
   │   │   ├── include/sensors_local.h      ← API pública
   │   │   ├── include/sensor_local_properties.h ← descriptor de capacidad por controlador
   │   │   └── drivers/<name>/              ← una carpeta por controlador (autorregistrado)
   │   │       ├── example/…_weather_example.c    ← esqueleto WEATHER de datos aleatorios
   │   │       ├── example/…_telemetry_example.c  ← esqueleto TELEMETRY de datos aleatorios
   │   │       └── bmp180/bmp180.c                ← controlador I2C real de temperatura/presión
   │   └── webconfig/      ← administración esp_http_server
   │       ├── web_server.c            ← tabla de rutas
   │       ├── web_common.c            ← auth, análisis de formularios, shell HTML, ayudantes de campo
   │       ├── pages/*.c               ← un archivo por página de administración
   │       └── translations/           ← translations.h + lang_en/es/it.h
   │
   └── managed_components/                     (obtenidos por el gestor de componentes)
       ├── joltwallet__littlefs/
       ├── esp-idf-lib__bmp180/
       ├── esp-idf-lib__i2cdev/
       └── esp-idf-lib__esp_idf_lib_helpers/

Por dónde empezar a leer
========================

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Si quieres entender…
     - Empieza en…
   * - El orden de arranque y la disposición de tareas
     - ``main/main.c``, luego ``main/aprs_service.c``
   * - Cómo se despacha una trama recibida
     - ``aprs_msg_callback()`` en ``main/aprs_service.c``
   * - El DSP / por qué se eligen las tasas de muestreo
     - ``…_modem_config.h``, luego ``src/modem.c`` / ``src/afsk.c``
   * - Gatewaying y filtrado
     - ``components/igate/igate.c`` + ``main/aprs_filter.c``
   * - Conectar un sensor
     - ``components/sensors_local/`` y :ref:`es-sensor-framework`
   * - El esquema de configuración
     - ``main/include/app_config.h``
   * - Una página web específica
     - el ``components/webconfig/pages/page_*.c`` correspondiente

Ambigüedad de posición y el filtro de rango
============================================

``main/aprs_filter.c`` decodifica la posición de un paquete entrante para el
filtro de rango local RF→INET de forma independiente a ``main/aprs_coord.c``,
ya que solo necesita un par latitud/longitud, no el codificador/decodificador
de texto APRS completo. Cuando la posición lleva ambigüedad (APRS101 capítulo
6: los dígitos de minutos menos significativos sustituidos por espacios), el
decodificador resuelve los dígitos en blanco al centro de la caja de
ambigüedad resultante en lugar de a su esquina inferior, ya que el centro es
la mejor estimación disponible de la posición real de la estación y es lo
que alimenta la comprobación de distancia por gran círculo en
``components/igate/igate.c``.
