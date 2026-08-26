.. _es-configuration:

=========================
El motor de configuración
=========================

Una configuración residente
===========================

Una única instancia ``app_config_t g_config`` (``main/app_config.c`` /
``app_config.h``) es la copia viva que lee cada subsistema. Se carga al arrancar
y se edita campo a campo por los manejadores POST de la web. Sus campos se
agrupan por página de la administración web: sistema/hora, identidad "My Station",
Wi-Fi, IGate, Digipeater, Tracker, Weather, GPS, el módem AFSK,
System/autenticación HTTP, Message y Query.

Los nombres de campo y las claves JSON se mantienen **1:1** con el ``config.h``/
``config.cpp`` del proyecto de referencia original, de modo que cada valor que
muestra la administración web tiene un hogar y los archivos ``config.json``
antiguos cargan sin cambios.

.. note::

   La configuración de telemetría, boletines y objetos/ítems deliberadamente
   **no** vive en ``g_config``. Persiste en sus propios archivos LittleFS
   (``/storage/telemetry.json``, ``bulletins.json``, ``objitems.json``) para
   mantener pequeña la configuración residente — y por tanto cada guardado de
   ``config.json``.

Carga y guardado
================

* **Cargado** con **cJSON**. Si el archivo falta o está corrupto, se aplican los
  valores por defecto **y se guardan inmediatamente**, de modo que el archivo
  siempre existe y es consistente.
* **Cargado en el lugar**: ``config_from_json()`` escribe el conjunto de valores
  por defecto directamente en la estructura de destino y luego lee el valor de
  reserva de cada clave del mismo campo que está por sobrescribir. Cada campo se
  asigna exactamente una vez, siempre después de esa llamada, así que un valor de
  reserva todavía contiene su valor por defecto en el instante en que se lee.
  Esto mantiene una segunda ``app_config_t`` — el tamaño de toda la
  configuración — fuera de la pila de la tarea que carga, que ya tiene a su lado
  el árbol cJSON del archivo entero vivo en el heap.
* **Guardado** por un pequeño escritor JSON token a token (``jw_t``/``jadd_*``)
  que fluye directamente al archivo, evitando la doble asignación de heap que
  necesitarían un árbol cJSON completo más su búfer serializado. Un búfer estático
  de ``setvbuf()`` se instala justo tras ``fopen()`` para que newlib no asigne
  perezosamente un búfer stdio grande a mitad de escritura.
* **Atómico**: escribe ``/storage/config.json.tmp``, luego renombra.

API pública: ``app_config_set_defaults()``, ``app_config_load()``,
``app_config_save()``, ``app_config_factory_reset()``, y la instancia viva
``extern app_config_t g_config``.

Concurrencia: el cerrojo de configuración
=========================================

``g_config`` lo escriben campo a campo los manejadores POST de la web (un solo
guardado de ajustes reescribe muchos campos, varios de ellos cadenas/arrays, uno
a uno) mientras tareas de larga ejecución (constructores de balizas, login del
IGate, digipeater, mensaje, meteo, respondedor de consultas) leen esos mismos
campos. Un lector que muestrea
una cadena a mitad de ``strcpy`` puede ver un valor roto o transitoriamente sin
terminador NUL y salirse del final del búfer. ``app_config_lock()`` /
``app_config_unlock()`` serializan esos dos lados.

Es un estricto **cerrojo de hoja**: se mantiene solo lo suficiente para copiar
los campos necesarios a locales — nunca a través de una llamada bloqueante, E/S,
transmisión u otro cerrojo. Los campos escalares (de una sola palabra) son
atómicos a nivel de palabra en este MCU y pueden leerse sin cerrojo. Es distinto
del mutex de guardado interno (mantenido durante toda la serialización a flash).

Interruptores de módulo en compilación
======================================

``app_config.h`` define un conjunto de macros ``ENABLE_*``; comentar una elimina
su entrada de barra lateral y su página de la imagen:

.. code-block:: c

   ENABLE_DASHBOARD    ENABLE_MSG_CHAT     ENABLE_BULLETINS    ENABLE_OBJECTS_ITEMS
   ENABLE_STATION      ENABLE_RADIO_MODEM  ENABLE_MESSAGE      ENABLE_IGATE
   ENABLE_BRANDMEISTER ENABLE_DIGIPEATER   ENABLE_TRACKER      ENABLE_WEATHER
   ENABLE_TELEMETRY    ENABLE_GPS          ENABLE_SYSTEM       ENABLE_WIRELESS
   ENABLE_FILE_STORAGE ENABLE_ABOUT_FIRMWARE                   ENABLE_QUERY

**No** hay interruptor ``ENABLE_SENSORS``: el marco ``sensors_local`` no tiene
deshabilitación en compilación y siempre se compila (sus controladores
individuales están condicionados por sus propias opciones Kconfig
``CONFIG_SENSORS_LOCAL_*_DRIVER``).

Presets de ruta y máscaras de bits
==================================

Cada servicio (tracker / igate / digi / wx / …) almacena una **máscara de bits**,
no una cadena de ruta. El bit *N* selecciona ``g_config.path[N]``, uno de los
cuatro presets de texto libre editados en la página *Digi*.
``aprs_path_build_suffix()`` concatena cada ranura seleccionada no vacía; las
ranuras seleccionadas-pero-vacías se saltan. Es compartida por las balizas, el
tiempo, la telemetría, los mensajes y las respuestas a consultas, y aplica el
límite AX.25 de 8 vías en el momento de transmitir, de modo que una configuración
que llegó al dispositivo sin pasar por un formulario web no puede poner una ruta
demasiado larga en el aire.

Los Objetos/Ítems son el único servicio que no une las ranuras entre sí: su ruteo
proporcional envía **un** preset por transmisión y rota por la selección, así que
``objitem_paths()`` arma la lista por su cuenta. Allí el límite de saltos rige
por preset y no sobre la selección completa, y se cuenta con la misma
``app_config_path_hop_count()`` que usan el constructor compartido y el recorte
del guardado: un preset que por sí solo supera el límite queda fuera de la
rotación.

Todos los selectores salen de fábrica seleccionando el preset 0 y nada más
(``PATH_PRESET_MASK_DEFAULT``), porque ``g_config.path[0]`` es la única ranura
con una cadena de fábrica (``WIDE1-1,WIDE2-1``) y un bit que apunta a una ranura
vacía emitiría la baliza con el destino pelado.

Los bits del filtro del IGate (compartidos por ``rf2inetFilter`` e
``inet2rfFilter``):

.. code-block:: text

   MESSAGE 1<<0 · STATUS 1<<1 · TELEMETRY 1<<2 · WEATHER 1<<3 · OBJECT 1<<4
   ITEM 1<<5 · QUERY 1<<6 · BUOY 1<<7 · POSITION 1<<8
