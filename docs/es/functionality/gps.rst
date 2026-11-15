.. _es-gps:

=============
Receptor GNSS
=============

Al ESP32 se le puede conectar un módulo GNSS (GPS) serie que da a la estación
posición, velocidad, rumbo y hora UTC en vivo. Sirve cualquier receptor que
entregue sentencias NMEA 0183 estándar a 9600 bit/s (las placas comunes NEO-6M,
ATGM336H y HW-248 lo hacen de fábrica); el firmware solo escucha y nunca envía
comandos de configuración al módulo.

Cableado
========

.. list-table::
   :header-rows: 1
   :widths: 30 25 45

   * - Pin del módulo
     - Pin del ESP32
     - Nota
   * - TX (salida NMEA)
     - GPIO16 (RX de UART2)
     - la única línea que se usa de verdad
   * - RX
     - GPIO17 (TX de UART2)
     - reservado; nunca se envía nada
   * - VCC
     - 3V3 o 5V
     - según lo que pida el módulo
   * - GND
     - GND
     -

El puerto, los pines y la velocidad 9600 8N1 son constantes de compilación en
``main/include/gps.h``. GPIO16/17 no están disponibles en un ESP32-WROVER,
donde pertenecen a la PSRAM; ahí hay que cambiar los dos pines (ver
:ref:`es-hardware`).

La página GPS
=============

.. figure:: /_static/webadmin/gps_top.png
   :alt: La página Receptor GPS al abrirse.
   :align: center
   :width: 100%

   La página *Receptor GPS* al abrirse.

.. figure:: /_static/webadmin/gps_fs00.png
   :alt: Receptor GPS — el único interruptor que el resto del firmware consulta antes de usar el receptor. Apagado, la UART no se instala y la tarea lectora no corre; el cambio tiene efecto de inmediato, sin reiniciar.
   :align: center
   :width: 90%

   **Receptor GPS** — el único interruptor que el resto del firmware consulta antes de usar el receptor. Apagado, la UART no se instala y la tarea lectora no corre; el cambio tiene efecto de inmediato, sin reiniciar.

.. figure:: /_static/webadmin/gps_fs01.png
   :alt: Estado del Receptor — la insignia Estado del módulo y el enlace, el estado de navegación, la calidad y el modo de la posición.
   :align: center
   :width: 90%

   **Estado del Receptor** — la insignia *Estado del módulo* y el enlace, el estado de navegación, la calidad y el modo de la posición.

La insignia resume la página en un solo diagnóstico:

* rojo **Deshabilitado** - el interruptor está apagado, o no se pudo instalar la
  UART;
* rojo **Sin datos (revisar cableado)** - activado, pero no llegó ninguna
  sentencia válida por GPIO16 en 10 segundos;
* ámbar **Buscando (sin fijación)** - llegan sentencias pero el receptor
  todavía no tiene solución de navegación;
* verde **Fijación OK** - se está informando una posición válida.

.. figure:: /_static/webadmin/gps_fs02.png
   :alt: Posición — latitud, longitud, altitud y separación del geoide.
   :align: center
   :width: 90%

   **Posición** — latitud, longitud, altitud y separación del geoide.

.. figure:: /_static/webadmin/gps_fs03.png
   :alt: Movimiento — velocidad sobre el suelo, rumbo y variación magnética.
   :align: center
   :width: 90%

   **Movimiento** — velocidad sobre el suelo, rumbo y variación magnética.

.. figure:: /_static/webadmin/gps_fs04.png
   :alt: Fecha y Hora (UTC) — fecha y hora UTC de la última posición.
   :align: center
   :width: 90%

   **Fecha y Hora (UTC)** — fecha y hora UTC de la última posición.

.. figure:: /_static/webadmin/gps_fs05.png
   :alt: Satélites y Precisión — satélites usados y a la vista y los valores HDOP / PDOP / VDOP.
   :align: center
   :width: 90%

   **Satélites y Precisión** — satélites usados y a la vista y los valores HDOP / PDOP / VDOP.

.. figure:: /_static/webadmin/gps_fs06.png
   :alt: Enlace Serie — sentencias aceptadas y descartadas (errores de suma de control o de formato) y la antigüedad de la última sentencia y de la última posición.
   :align: center
   :width: 90%

   **Enlace Serie** — sentencias aceptadas y descartadas (errores de suma de control o de formato) y la antigüedad de la última sentencia y de la última posición.

.. figure:: /_static/webadmin/gps_fs07.png
   :alt: Cableado (tiempo de compilación) — el cableado de compilación, a modo de referencia.
   :align: center
   :width: 90%

   **Cableado (tiempo de compilación)** — el cableado de compilación, a modo de referencia.

Los valores en vivo vienen de ``/gps/values``, consultado cada segundo.

Quién usa la posición
=====================

* **Usar GPS** en las páginas *Estación*, *IGate*, *Digirepetidor*, *Rastreador* y
  *Clima* llena los campos de posición de esa página desde ``/gps/live``
  mientras está marcado.
* **Usar posición GPS en vivo** en la página *Rastreador* lee el receptor en cada
  transmisión, y SmartBeaconing toma de ahí la velocidad y el rumbo.
* El ``/status`` del bot de Telegram informa el interruptor del receptor.

Todo lo de esta página se guarda en ``/storage/gps.json``.
