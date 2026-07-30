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
#. Llama a ``sensors_local_save(&weather_telemetry_data, SENSOR_LOCAL_DATA_ALL)``,
   que recorre el registro y, por cada controlador capaz, lo inicializa
   perezosamente si hace falta y llama a su ``save()`` — el controlador escribe
   directamente en ``aprs_weather_report_t`` / ``aprs_telemetry_report_t``.
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

Bloqueo
=======

Como un controlador puede estar actualizando el contenedor concurrentemente
mientras la baliza lo lee, todo acceso pasa por ``weather_lock()`` /
``weather_unlock()``. Trata ``weather_telemetry_data`` como solo-lectura fuera de
``weather.c``.

.. seealso::

   :ref:`es-sensor-framework` — cómo conectar un sensor real (BME280, DS18B20,
   BMP180…) para que sus lecturas alimenten estos campos.
