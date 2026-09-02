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
     - El ``app_config_t`` residente (sistema, estación, Wi-Fi, IGate, BrandMeister, digi,
       tracker, meteo, GPS, módem, mensaje).
   * - ``/storage/telemetry.json``
     - Configuración de telemetría (``telemetry_config_t``): analógicos A1–A5,
       digitales B1–B8, parámetros del informe, conmutadores de mensajes de
       definición.
   * - ``/storage/bulletins.json``
     - Los cinco boletines APRS (identificador y grupo de destinatario, texto,
       RF/INET, intervalo inicial, rampa de decaimiento, caducidad).
   * - ``/storage/objitems.json``
     - Los cinco objetos/ítems APRS (nombre, posición, símbolo, rumbo/velocidad,
       comentario, intervalo, bandera permanente).
   * - ``/storage/telegram.json``
     - Toda la configuración del bot de Telegram: el interruptor de
       habilitación, el token del bot, el identificador del administrador, la
       dirección de la Mini App y las listas de usuarios y chats de grupo
       autorizados.

Los cinco usan el mismo escritor en flujo, cada uno bajo su propio mutex, cada
uno con un ``setvbuf()`` explícito para evitar una asignación perezosa de búfer
stdio grande a mitad de escritura. Ese búfer es un único objeto estático
compartido por los cinco almacenes, ya que la compuerta de escritura de todo el
sistema de archivos impide que dos guardados se solapen.

Reset de fábrica
================

``POST /default`` (el botón de *factory reset* de la página System) llama a
``app_config_factory_reset()``, que borra la configuración de vuelta a
``app_config_set_defaults()`` y la persiste. Por sí solo no elimina los archivos
separados de telemetría/boletines/objitems — esos regeneran valores por defecto
en el siguiente acceso si se borran vía la página Storage.

Claves de la interconexión BrandMeister
=======================================

.. list-table::
   :header-rows: 1
   :widths: 25 15 60

   * - Clave
     - Tipo
     - Significado
   * - ``bmEn``
     - bool
     - Interruptor principal de la interconexión BrandMeister. Apagado por
       omisión.
   * - ``bmMonitor``
     - bool
     - Intención de correr la suscripción mundial ``u/APBM*``. Se fuerza a
       apagado al cargar cuando ``inet2rf`` está activo e ``inet2rfRangeEn``
       apagado, así un archivo editado a mano no puede saltear el
       enclavamiento.
   * - ``bmMsgInetOnly``
     - bool
     - Rutear los mensajes a destinatarios BrandMeister solo por APRS-IS.
       Habilitado por omisión; solo puede quitar la pata de RF.
   * - ``bmGateways``
     - arreglo de 4 cadenas
     - Indicativos opcionales de estación de entrada para la tercera prueba del
       clasificador. Un ``*`` final compara por prefijo.
   * - ``inet2rfRangeEn``
     - bool
     - Habilita el filtro de rango INET→RF. Apagado por omisión.
   * - ``inet2rfRangeKm``
     - número
     - Radio del filtro de rango INET→RF en km, 0 = sin límite. Se acota a
       0…20038 al cargar.
