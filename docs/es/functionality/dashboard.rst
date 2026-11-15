.. _es-dashboard:

===============
Panel y tráfico
===============

El *Panel* es la página con la que abre la administración web. No configura
nada: es el estado de la estación en vivo, que el navegador actualiza mientras
la página está abierta, y el único lugar donde se ve en el momento el tráfico
recibido, digipetido, pasado por la pasarela y transmitido.

.. figure:: /_static/webadmin/dashboard_top.png
   :alt: La página Panel al abrirse.
   :align: center
   :width: 100%

   La página *Panel* al abrirse.

Información del sistema
=======================

.. figure:: /_static/webadmin/dashboard_fs00.png
   :alt: Información del sistema — fecha y hora en la zona horaria elegida en la página Sistema, tiempo en marcha, heap libre y su mínimo histórico, uso de LittleFS, frecuencia de la CPU y motivo del último reinicio. La franja se actualiza una vez por segundo desde /dashinfo.
   :align: center
   :width: 90%

   **Información del sistema** — fecha y hora en la zona horaria elegida en la página *Sistema*, tiempo en marcha, heap libre y su mínimo histórico, uso de LittleFS, frecuencia de la CPU y motivo del último reinicio. La franja se actualiza una vez por segundo desde ``/dashinfo``.

El reloj siempre funciona en UTC; la zona horaria solo cambia cómo se muestra
la fecha aquí (ver :ref:`es-administration`). Un mínimo de heap que baja día
tras día es la primera señal de una fuga; el *Motivo de reinicio* distingue
un corte de energía (*Power-on*) de un watchdog o un pánico.

Modos y red
===========

.. figure:: /_static/webadmin/dashboard_fs01.png
   :alt: Modos Activos — una píldora por servicio - IGate, digipetidor, tracker y clima - en verde mientras el servicio está activo.
   :align: center
   :width: 90%

   **Modos Activos** — una píldora por servicio - IGate, digipetidor, tracker y clima - en verde mientras el servicio está activo.

.. figure:: /_static/webadmin/dashboard_fs02.png
   :alt: Estado de Red — asociación Wi-Fi, conexión a APRS-IS (igate_is_connected()) y si FX.25 está activado en recepción.
   :align: center
   :width: 90%

   **Estado de Red** — asociación Wi-Fi, conexión a APRS-IS (``igate_is_connected()``) y si FX.25 está activado en recepción.

Estadísticas
============

.. figure:: /_static/webadmin/dashboard_fs03.png
   :alt: ESTADÍSTICAS — los contadores de la estación: tramas recibidas y transmitidas por RF, pasadas en cada dirección, digipetidas, descartadas o no decodificables, la cola de transmisión RF, las transmisiones CSMA forzadas y el ciclo de trabajo de transmisión.
   :align: center
   :width: 90%

   **ESTADÍSTICAS** — los contadores de la estación: tramas recibidas y transmitidas por RF, pasadas en cada dirección, digipetidas, descartadas o no decodificables, la cola de transmisión RF, las transmisiones CSMA forzadas y el ciclo de trabajo de transmisión.

Qué mide cada contador, y por qué el IGate y el módem llevan contadores
propios, está descrito en :ref:`es-web-admin` (*Las estadísticas del panel*).

.. figure:: /_static/webadmin/dashboard_fs04.png
   :alt: Detalle de Descartes — una línea por motivo de descarte. Un contador que crece indica qué filtro o límite está rechazando tráfico, sin abrir la consola.
   :align: center
   :width: 90%

   **Detalle de Descartes** — una línea por motivo de descarte. Un contador que crece indica qué filtro o límite está rechazando tráfico, sin abrir la consola.

Radio, servidor y Wi-Fi
=======================

.. figure:: /_static/webadmin/dashboard_fs05.png
   :alt: Info de Radio — el perfil de módem activo y el estado de FX.25.
   :align: center
   :width: 90%

   **Info de Radio** — el perfil de módem activo y el estado de FX.25.

.. figure:: /_static/webadmin/dashboard_fs06.png
   :alt: SERVIDOR APRS-IS — el servidor APRS-IS en uso, que con el respaldo puede ser cualquiera de los cuatro configurados en la página IGate.
   :align: center
   :width: 90%

   **SERVIDOR APRS-IS** — el servidor APRS-IS en uso, que con el respaldo puede ser cualquiera de los cuatro configurados en la página *IGate*.

.. figure:: /_static/webadmin/dashboard_fs07.png
   :alt: WiFi — modo Wi-Fi, la red a la que está asociada la estación y su intensidad de señal.
   :align: center
   :width: 90%

   **WiFi** — modo Wi-Fi, la red a la que está asociada la estación y su intensidad de señal.

Tráfico en vivo
===============

.. figure:: /_static/webadmin/dashboard_fs08.png
   :alt: Tráfico IGate — la tabla de tráfico: tiempo desde el arranque, tipo (RX, TX, DIGI, RX-IS…), símbolo, estación, el paquete crudo, lo decodificado de la carga y el nivel de audio recibido en mV RMS.
   :align: center
   :width: 90%

   **Tráfico IGate** — la tabla de tráfico: tiempo desde el arranque, tipo (``RX``, ``TX``, ``DIGI``, ``RX-IS``…), símbolo, estación, el paquete crudo, lo decodificado de la carga y el nivel de audio recibido en mV RMS.

La tabla se llena consultando ``/igate_traffic?since=<seq>`` por número de
secuencia, así que no se pierde nada entre dos consultas, y una página recién
cargada empieza con las entradas que todavía guarda el anillo de la estación.
*Pausar* congela la vista sin detener la estación; *Limpiar* vacía solo la
copia del navegador. La columna *AUDIO* es el nivel que el módem midió en el
pin del ADC para esa trama, y es la forma más rápida de ajustar el nivel de
recepción (ver :ref:`es-hardware`). Con *Registrar después de los filtros*
activado en la página *IGate*, la tabla muestra solo lo que aceptan los
filtros locales.

Los íconos de símbolo los carga el navegador desde ``aprs.dprns.com``; si ese
sitio no está accesible la columna queda vacía y todo lo demás funciona.

Escuchados recientemente
========================

Cada estación escuchada se guarda además en una tabla *Last heard* por
estación de hasta 30 entradas, la más reciente primero, con la ruta por la que
llegó, si llegó por RF o por APRS-IS, los saltos de digipetidor usados y un
historial de actividad de 18 horas. El panel no la dibuja: se sirve como JSON
en ``/lastheard`` y la usan el filtro de mensajes (*escuchado localmente*),
las respuestas a ``?APRSD`` / ``?APRSH``, el filtro INET→RF *escuchado por
RF* y el contador de estado de BrandMeister.
