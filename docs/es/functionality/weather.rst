.. _es-weather:

=====================
Informe meteorológico
=====================

El subsistema ``weather`` (``main/weather.c``) es un informe meteorológico APRS
de la propia estación totalmente funcional, no un andamiaje. Es dueño del único
contenedor compartido ``weather_telemetry_data_t`` en el que escribe cada
controlador de sensor local, lo refresca desde el registro ``sensors_local`` una
vez por segundo, y periódicamente codifica y transmite un informe meteorológico
APRS estándar por RF y/o APRS-IS a partir de los campos que el operador mapeó en
la página web *Weather* (``g_config.wx_*``).

Las tres piezas móviles
=======================

``weather_start()`` (llamada una vez al arrancar) configura:

#. **El contenedor compartido.** ``weather_telemetry_data`` se conecta a
   almacenamiento estático de respaldo para un ``aprs_weather_report_t`` y un
   ``aprs_telemetry_report_t``.
#. **El registro.** ``sensors_local_init()`` crea el mutex del registro y
   ``sensors_local_init_all()`` ejecuta el ``init()`` de cada controlador
   autorregistrado.
#. **Dos callbacks de servicio.** ``weather_service_1hz()`` (ejecutada a 1 Hz por
   el tick del servicio APRS) y ``weather_beacon_service()`` (ejecutada por el
   planificador de balizas compartido).

El refresco a 1 Hz
==================

``weather_service_1hz()``:

#. Limpia las banderas "habilitado" del contenedor, para que un controlador que
   deje de reportar un campo este ciclo no deje un valor obsoleto pareciendo
   válido.
#. Refresca la familia de telemetría con una sola llamada agregada,
   ``sensors_local_save(&weather_telemetry_data, SENSOR_LOCAL_DATA_TELEMETRY)``:
   los canales de telemetría no son seleccionables por campo, así que
   contribuye cada controlador con capacidad TELEMETRY.
#. Resuelve **cada campo meteorológico de forma independiente** contra el único
   controlador que el operador eligió para él. Un campo se muestrea solo si está
   tildado (``g_config.wx_sensor_enable[f]``) y tiene un canal de origen
   asignado (``g_config.wx_sensor_ch[f] != SENSOR_LOCAL_CH_NONE``); la lectura
   se toma con ``sensors_local_save_one(ch, &scratch, SENSOR_LOCAL_DATA_WEATHER)``
   sobre un contenedor temporal, y solo el valor de ese campo se copia al
   reporte vivo. Usar un contenedor temporal por campo es lo que evita que un
   segundo controlador WEATHER registrado pise un campo ya resuelto desde otro,
   y es por eso que el paquete al aire siempre coincide con la columna *Channel*
   de cada campo y con la vista previa *Value* en vivo de la página.
#. Acumula cualquier campo marcado como *Averaged* (una casilla por campo en la
   página Weather) en una suma/cuenta corriente.

Campos meteorológicos mapeables
===============================

La lista de campos al aire es el conjunto canónico del capítulo 12 de APRS101 más
las propuestas de inundación de APRS 1.2, enumerados por ``wx_field_id_t``:

.. list-table::
   :header-rows: 1
   :widths: 34 22 44

   * - Campo
     - Token al aire
     - Unidad
   * - Dirección del viento
     - ``ddd/``
     - grados
   * - Velocidad del viento (sostenida)
     - ``/sss``
     - mph
   * - Racha de viento
     - ``gXXX``
     - mph
   * - Temperatura
     - ``tXXX``
     - °F
   * - Lluvia última hora
     - ``rXXX``
     - 1/100 pulg
   * - Lluvia últimas 24 h
     - ``pXXX``
     - 1/100 pulg
   * - Lluvia desde medianoche
     - ``PXXX``
     - 1/100 pulg
   * - Nieve últimas 24 h
     - ``sXXX``
     - 1/10 pulg (APRS 1.2)
   * - Humedad
     - ``hXX``
     - %
   * - Presión barométrica
     - ``bXXXXX``
     - décimas de mb
   * - Luminosidad
     - ``LXXX`` / ``lXXX``
     - W/m² (APRS 1.2)
   * - Altura de inundación (pies)
     - ``FXXXX.X``
     - pies (APRS 1.2)
   * - Altura de inundación (metros)
     - ``fXXXX.X``
     - metros (APRS 1.2)
   * - Contador de lluvia crudo
     - ``#XXXX``
     - cuentas de cazoleta, sin escalar

El contador de lluvia crudo es el caso distinto: es la cuenta corrida de vuelcos
de cazoleta del propio pluviómetro, no una medición en centésimas de pulgada, y
la estación nunca lo reinicia. Un receptor obtiene la lluvia restando dos
reportes, que es lo que lo vuelve útil en un sitio sin atención cuyos otros
campos de lluvia dependen de que la estación haya estado encendida el tiempo
suficiente para acumularlos. Se transmite sin escalar, con cuatro dígitos, y da
la vuelta en el ancho del campo igual que el propio contador.

La baliza WX
============

``weather_beacon_service()`` transmite cada ``g_config.wx_interval`` segundos
(solo cuando ``wx_en`` está activo):

#. **Resolver campos.** Por cada token WX al aire, lee bien el valor en vivo
   directamente del contenedor, bien el valor promediado acumulado por el
   refresco a 1 Hz, según la casilla *Averaged* de ese campo — de modo que un
   reportador intermitente sigue contribuyendo con una media razonable.
#. **Construir el paquete.** Renderiza la línea TNC2 estándar
   ``!lat/lon_WIND/SPDgGUSTtTTTrRRRhHHbBBBBB…``.
#. **Transmitirlo** por RF y/o APRS-IS según ``wx_2rf`` / ``wx_2inet``.

Comentario e identificador de software
======================================

Todo reporte termina con el identificador de tipo de software / unidad
meteorológica de APRS101 cap.12, ``xESP``: la letra de tipo de software seguida
de la cadena que nombra la familia de sensores basada en ESP32 de este firmware.

La especificación define ese identificador como el token que termina los datos
meteorológicos, y no define ningún comentario de texto libre para un reporte
meteorológico, así que ambos no pueden ocupar a la vez su lugar nominal. Este
firmware pone el comentario del operador (``g_config.wx_comment``) entre los
datos meteorológicos y el identificador, de modo que el orden al aire es:

.. code-block:: text

   =DDMM.mmN/DDDMM.mmW_<tokens meteorológicos><comentario>xESP

Así un decodificador que lea la cadena de unidad hasta el fin de línea no puede
absorber el comentario dentro de ella, y uno que explore desde el final sigue
encontrando el identificador donde lo espera. Los cuatro formatos de reporte
(objeto, posición con marca de tiempo, posición sin marca de tiempo y sin
posición) usan el mismo orden, y el identificador aparece exactamente una vez
por reporte.

El comentario se filtra igual que cualquier otro campo de texto libre propio,
en los cuatro formatos: se quitan ``|`` y ``~``, porque ambos están reservados
para el grupo de telemetría de comentario en base 91 (:ref:`es-telemetry`) que
este firmware emite por su cuenta, y se antepone el marcador de no archivar
``!x!`` cuando la casilla a nivel de estación de la página Estación lo pide
(:ref:`es-beacons`). El texto guardado no cambia; solo se filtra la
representación que sale al aire.

Bloqueo
=======

Como un controlador puede estar actualizando el contenedor concurrentemente
mientras la baliza lo lee, todo acceso pasa por ``weather_lock()`` /
``weather_unlock()``. Trata ``weather_telemetry_data`` como solo-lectura fuera de
``weather.c``.

.. seealso::

   :ref:`es-sensor-framework` — cómo conectar un sensor real (BME280/BMP280,
   DS18B20…) para que sus lecturas alimenten estos campos.
