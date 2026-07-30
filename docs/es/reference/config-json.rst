.. _es-config-json:

===============================
Almacenamiento de configuración
===============================

La configuración residente persiste en ``/storage/config.json`` sobre LittleFS.
Esta referencia resume la mecánica de almacenamiento; para los grupos de campos
véase :ref:`es-configuration`.

Mecánica
========

* **Ruta:** ``/storage/config.json``.
* **Cargado** con cJSON; **guardado** por un escritor en flujo token a token.
* **Guardado atómico:** escribe ``config.json.tmp``, luego renombra.
* Faltante o corrupto → se aplican defaults y se guardan inmediatamente, de modo
  que el archivo siempre existe y es consistente.
* Los nombres de campo / claves JSON se mantienen 1:1 con el proyecto de
  referencia, así que los archivos antiguos cargan sin cambios; las claves
  desconocidas se ignoran.

Otros archivos persistentes
===========================

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - Archivo
     - Contenido
   * - ``/storage/config.json``
     - El ``app_config_t`` residente (sistema, estación, Wi-Fi, IGate, digi,
       tracker, meteo, módem, mensaje).
   * - ``/storage/telemetry.json``
     - Configuración de telemetría (``telemetry_config_t``): analógicos A1–A5,
       digitales B1–B8, parámetros del informe, conmutadores de mensajes de
       definición.
   * - ``/storage/bulletins.json``
     - Los cinco boletines APRS (texto, RF/INET, intervalo, caducidad).
   * - ``/storage/objitems.json``
     - Los cinco objetos/ítems APRS (nombre, posición, símbolo, rumbo/velocidad,
       comentario, intervalo, bandera permanente).

Los cuatro usan el mismo escritor en flujo, cada uno bajo su propio mutex, cada
uno con un ``setvbuf()`` explícito para evitar una asignación perezosa de búfer
stdio grande a mitad de escritura.

Reset de fábrica
================

``POST /default`` (el botón de *factory reset* de la página System) llama a
``app_config_factory_reset()``, que borra la configuración de vuelta a
``app_config_set_defaults()`` y la persiste. Por sí solo no elimina los archivos
separados de telemetría/boletines/objitems — esos regeneran valores por defecto
en el siguiente acceso si se borran vía la página Storage.
