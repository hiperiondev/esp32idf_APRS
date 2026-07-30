.. _es-networking:

===
Red
===

La puesta en marcha de Wi-Fi (``main/main.c``) es una de las partes más
instrumentadas del firmware, porque "cambié a modo Station y no pasó nada" era un
fallo silencioso recurrente en revisiones anteriores. Ahora cada ruta registra lo
que hizo.

Modos Wi-Fi
===========

``g_config.wifi_mode`` selecciona la configuración de interfaz, coincidiendo con
la página Wireless:

* ``0`` = off
* ``1`` = STA (estación)
* ``2`` = AP (punto de acceso) — el valor por defecto más seguro; el dispositivo
  siempre es accesible
* ``3`` = AP+STA

Se almacenan hasta cinco perfiles STA (``WIFI_STA_NUM = 5``), cada uno con su
propia casilla Enable. La **primera entrada habilitada con un SSID no vacío** se
empuja al controlador; el failover multi-AP se anota como "se puede añadir más
adelante".

Conexión de estación robusta
============================

Varias correcciones deliberadas hacen fiable la ruta de estación:

* **Conectar desde ``WIFI_EVENT_STA_START``, no de inmediato.**
  ``esp_wifi_connect()`` solo es legal una vez que la interfaz de estación ha
  arrancado realmente, lo que el controlador señala con ``WIFI_EVENT_STA_START``.
  Llamarlo justo tras ``esp_wifi_start()`` pierde esa carrera y devuelve
  ``ESP_ERR_WIFI_NOT_STARTED`` — sin asociación, sin evento de desconexión, sin
  reintento. La conexión se emite desde el manejador de STA_START y cada intento
  registra su resultado.
* **Retroceso de reconexión creciente, armado en un temporizador.** Las
  reconexiones usan un retroceso que crece 500 ms por fallo consecutivo, topado a
  8 s, armado en un ``esp_timer`` — **no** un ``vTaskDelay()`` dentro del
  manejador de eventos, que detendría el bucle de eventos compartido (incluido el
  propio ``STA_GOT_IP`` que espera) y, en un bucle de desconexión ajustado,
  mataría de hambre a la tarea idle hasta que saltara el watchdog de tareas.
* **Capacidad PMF anunciada.** Un ``wifi_config_t`` a cero deja
  ``pmf_cfg.capable = false``, y los AP WPA3 / WPA2-con-PMF-requerido
  simplemente rechazan tal estación. El firmware pone *capable, no required*, que
  funciona contra AP antiguos y nuevos.
* **Recurso a AP+STA.** Solo-STA sin nada a lo que unirse dejaría el dispositivo
  inaccesible, así que recurre a AP+STA y lo dice — la administración web sigue
  arriba.
* **Volcados de diagnóstico.** Si ninguna ranura STA está habilitada con un SSID,
  el firmware vuelca cada ranura y te dice cuál es el error ("habilitada, pero el
  SSID está VACÍO" vs "tiene un SSID, pero 'Enable' no está marcado").

Los códigos de razón de desconexión se registran (antes se descartaban):

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Razón
     - Significado
   * - 15 (4WAY_HANDSHAKE_TIMEOUT), 204 (NOT_AUTHED)
     - contraseña equivocada
   * - 201 (NO_AP_FOUND)
     - SSID no visible: nombre equivocado, fuera de rango, o solo 5 GHz
   * - 2 / 8 / 200
     - roaming ordinario / caídas del lado del AP

La bandera de "¿tenemos internet?"
==================================

``net_state.c`` mantiene un único booleano que se vuelve verdadero solo con
``IP_EVENT_STA_GOT_IP`` y falso al desconectarse o en modo solo-AP. El IGate lo
sondea y espera una **IP real** antes de intentar siquiera una conexión con
APRS-IS — estar meramente asociado a un AP no basta.

Escaneo Wi-Fi
=============

El escaneo de la página Wireless conmuta temporalmente una radio solo-AP a
AP+STA. Una bandera ``s_staEnabled`` condiciona cada ``esp_wifi_connect()``
automático para que el manejador de eventos no pelee con el escaneo.

Potencia TX
===========

La potencia TX (dBm) de la página Wireless se convierte ×4 a cuartos de dBm para
``esp_wifi_set_max_tx_power()``. Esto antes se almacenaba y mostraba pero nunca
llegaba a la radio.

Sincronización horaria
======================

``time_sync.c`` ejecuta SNTP contra tres hosts. Ahora es una máquina de estados
no bloqueante plegada en el tick de servicio de 1 Hz (en lugar de una tarea
bloqueante dedicada), y fija el reloj del sistema a UTC (``TZ=UTC0``)
independientemente de la zona horaria de visualización configurada — las marcas
de tiempo zulú de la especificación APRS lo requieren.

Frecuencia de CPU
=================

``cpu_freq.c`` aplica la selección de 80/160/240 MHz de la página System vía
``esp_pm_configure()``. Sin esto, el ajuste se almacenaba y mostraba pero nunca
cambiaba el reloj.
