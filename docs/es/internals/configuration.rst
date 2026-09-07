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
Wi-Fi, IGate, BrandMeister, Digipeater, Tracker, Weather, GPS, el módem AFSK,
System/autenticación HTTP, Message, Query y la cuenta Winlink.

Los nombres de campo y las claves JSON se mantienen **1:1** con el ``config.h``/
``config.cpp`` del proyecto de referencia original, de modo que cada valor que
muestra la administración web tiene un hogar y un operador que se mueva entre
ambos proyectos reconoce las claves.

Un archivo por funcionalidad
============================

La estructura residente es una cosa; dónde se guarda es otra. Cada página de la
administración web con ajustes persistentes tiene **un archivo propio** bajo
``/storage``, con el nombre de la página — el menú lateral es el índice de la
lista de archivos:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Archivo
     - Página
   * - ``system.json``
     - Sistema: reloj de CPU, servidores SNTP y resincronización, zona horaria,
       credenciales de la administración web.
   * - ``station.json``
     - My Station: indicativo, posición, datos de antena PHG, ambigüedad,
       opciones del informe de estado, no-archivo y DAO.
   * - ``wireless.json``
     - Wireless: selección de interfaz, potencia de TX, canal/SSID/clave del
       SoftAP, los cinco perfiles de estación.
   * - ``radio.json``
     - Radiomodem: modulación AFSK/FSK, FX.25, preámbulo, búferes de TX,
       temporización CSMA, retención de PTT, limitador de ciclo de trabajo.
   * - ``igate.json``
     - IGate: ambos sentidos de pasarela y sus filtros, listas de amigos y de
       satélites, caché de duplicados, filtros de rango y prefijo, pasarela de
       mensajes, las cuatro ranuras de servidor APRS-IS, la baliza y su
       extensión de datos.
   * - ``brandmeister.json``
     - BrandMeister: los interruptores de interconexión y la lista de
       indicativos de pasarela.
   * - ``digi.json``
     - Digipeater: la tabla de alias y las reglas de repetición, la baliza **y
       los cuatro presets de ruta compartidos**, que se editan en esta página.
   * - ``tracker.json``
     - Tracker: la baliza, las opciones Mic-E y los parámetros de
       SmartBeaconing.
   * - ``weather.json``
     - Weather: el informe y el mapeo de sensores por campo.
   * - ``gps.json``
     - GPS: el interruptor del receptor.
   * - ``message.json``
     - Message: el servicio de mensajería, reintentos, GPIO de alarma y grupos
       de mensajes.
   * - ``winlink.json``
     - Winlink: los ajustes de cuenta y sesión (las respuestas van a un archivo
       aparte, ``winlink_mail.json``).
   * - ``query.json``
     - Query: los interruptores del respondedor y la baliza de capacidades.

Cada fila es un valor de ``app_config_section_t``, y la tabla ``SECTIONS`` de
``app_config.c`` nombra el archivo y las dos mitades de su códec. El manejador
de guardado de una página llama a ``app_config_save_section()`` con su propia
sección, así que guardar el digipeater nunca reescribe el archivo del IGate y
una escritura que falle no puede llevarse por delante los ajustes de otra
funcionalidad. La página *My Station* es la única que nombra varias: sus
espejos de "Use My Station Data" alcanzan campos de otros cinco servicios, así
que pasa a ``app_config_save_sections()`` todas las secciones que tocó — un
guardado parcial dejaría la identidad de la estación repartida entre archivos
que ya no coinciden.

El orden de lectura importa exactamente en un punto: el lector de BrandMeister
vuelve a aplicar el enclavamiento del monitor mundial contra la pasarela
INET→RF que lleva el archivo del IGate, así que la sección IGate se lee
primero.

.. note::

   Las páginas sin ajustes persistentes propios — Dashboard, Snd/Rcv Msg,
   Console Logs, File Storage, About — no tienen archivo. Cuatro subsistemas
   mantienen estructuras propias en lugar de campos de ``app_config_t``, y
   poseen sus archivos directamente: ``/storage/telemetry.json``,
   ``bulletins.json``, ``objitems.json`` y ``telegram.json``. La razón es el
   tamaño — esas tablas agrandarían significativamente la estructura residente
   — pero el resultado es la misma regla: una funcionalidad, un archivo.

Carga y guardado
================

* **Primero los valores por defecto, una sola vez.** ``app_config_load()``
  rellena toda la estructura desde ``app_config_set_defaults()`` antes de abrir
  el primer archivo, y luego lee cada sección encima. Cada lector de sección
  toma el valor de reserva de cada clave del mismo campo que está por
  sobrescribir, así que una clave que un archivo no lleva — y una sección cuyo
  archivo no existe siquiera — conserva su valor por defecto documentado. Esto
  además mantiene una segunda ``app_config_t`` fuera de la pila de la tarea que
  carga, que ya tiene a su lado el árbol cJSON de una sección vivo en el heap.
* **Cargado** con **cJSON**, un archivo a la vez, así que el pico de asignación
  es la sección más grande y no la configuración entera.
* **Ausente, vacío o corrupto** → esa sección se reescribe desde los valores por
  defecto antes de que la carga retorne. Esto es lo que garantiza que cada
  funcionalidad tenga un archivo en flash desde el primer arranque, sin que un
  operador tenga que visitar su página.
* **Sin memoria** → no se escribe nada en absoluto y la carga entera informa
  fallo. ``cJSON_Parse()`` devuelve ``NULL`` tanto para un archivo malo como
  para una asignación fallida, así que ``json_store_read()`` reescanea el texto
  con una comprobación que no asigna nada antes de decidir cuál de los dos
  ocurrió; confundirlos borraría una configuración buena en un arranque
  cargado. Reescribir las secciones genuinamente ausentes mientras otra sigue
  sin leerse sería aún peor — persistiría una configuración armada mitad desde
  flash y mitad desde valores por defecto — así que la pasada no escribe nada y
  ``main.c`` reintenta una vez antes de recurrir al conjunto de fábrica.
* **Guardado** por un pequeño escritor JSON token a token (``jw_t``/``jadd_*``)
  que fluye directamente al archivo, evitando la doble asignación de heap que
  necesitarían un árbol cJSON completo más su búfer serializado. Un búfer estático
  de ``setvbuf()`` se instala justo tras ``fopen()`` para que newlib no asigne
  perezosamente un búfer stdio grande a mitad de escritura.
* **Atómico**, por archivo: escribe ``<nombre>.json.tmp``, luego renombra. Un
  guardado de varias secciones toma una sola vez el cerrojo de escritura de
  todo el sistema de archivos alrededor de la tanda, e intenta cada archivo
  seleccionado incluso después de que uno haya fallado, así que un sistema de
  archivos lleno no deja al resto con ajustes que el operador ya reemplazó.

API pública: ``app_config_set_defaults()``, ``app_config_load()``,
``app_config_save_section()``, ``app_config_save_sections()``,
``app_config_save()`` (todas las secciones), ``app_config_section_path()``,
``app_config_factory_reset()``, y la instancia viva
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
   ENABLE_BRANDMEISTER ENABLE_QUERY        ENABLE_DIGIPEATER   ENABLE_TRACKER
   ENABLE_WEATHER      ENABLE_TELEMETRY    ENABLE_GPS          ENABLE_TELEGRAM
   ENABLE_WINLINK      ENABLE_LOGS         ENABLE_SYSTEM       ENABLE_WIRELESS
   ENABLE_FILE_STORAGE ENABLE_ABOUT_FIRMWARE

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
   ITEM 1<<5 · QUERY 1<<6 · BUOY 1<<7 · POSITION 1<<8 · OTHER 1<<9

``IGATE_FILT_OTHER`` es el único bit que cubre varios tipos de carga útil a la
vez — capacidades de estación, formatos definidos por el usuario,
radiogoniometría Agrelo, balizas de localizador Maidenhead y la función de mapa
reservada —, y por eso los conjuntos *Filter* muestran nueve casillas para diez
bits. ``IGATE_FILT_QUERY`` tampoco tiene casilla propia: las consultas se
clasifican para que el código de gating pueda nombrarlas, pero el operador las
gobierna desde la página Query. El tráfico de terceros y los datos de prueba
quedan fuera de todos los bits y nunca se retransmiten por lo que diga la
máscara.
