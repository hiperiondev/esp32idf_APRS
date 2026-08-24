.. _es-storage-ota:

====================
Almacenamiento y OTA
====================

LittleFS
========

La partición ``storage`` de 512 KB se monta como **LittleFS** en ``/storage``
(``main/storage.c``). En el primer arranque se autoformatea. Contiene cada
archivo persistente que el firmware escribe:

* ``/storage/config.json`` — la configuración residente (véase
  :ref:`es-configuration`).
* ``/storage/telemetry.json`` — configuración del canal de telemetría.
* ``/storage/bulletins.json`` — los cinco boletines.
* ``/storage/objitems.json`` — los cinco objetos/ítems.
* ``/storage/telegram.json`` — toda la configuración del bot de Telegram.

La página web *Storage* es un navegador LittleFS completo: lista archivos con
tamaños, descarga (``GET /download?file=…``), borra (``POST /delete`` con el
nombre de archivo en el cuerpo del formulario), acepta
subidas multipart (``/upload``), reporta el uso y puede reformatear el volumen
(``/format``).

Por qué LittleFS y no SPIFFS
============================

Aunque el subtipo de partición sea ``spiffs`` (una etiqueta de tabla de
particiones), el volumen se monta con el componente ``joltwallet/littlefs``.
LittleFS es resiliente a cortes de energía y tiene nivelación de desgaste, lo que
importa para un dispositivo que escribe su configuración en cada guardado de
ajustes.

Guardados atómicos y amables con el heap
========================================

Cada archivo JSON lo escribe un pequeño escritor en flujo, token a token, en
lugar de construir un árbol cJSON completo y luego serializarlo — porque eso
necesitaría el árbol **y** su búfer serializado vivos a la vez sobre un heap
pequeño y fragmentado. En su lugar, el escritor fluye directamente al archivo.
Cada guardado es **atómico**: escribe ``<archivo>.tmp`` y luego renombra. Cada
escritor también llama a ``setvbuf()`` inmediatamente tras ``fopen()`` para que
newlib no asigne perezosamente un búfer stdio grande a mitad de escritura, algo
que sobre un heap fragmentado es una fuente sutil de fallos intermitentes de
doble excepción. El búfer que instalan es un único objeto estático de 512 bytes
definido en ``main/json_store.c``: la compuerta de escritura de todo el sistema
de archivos (``storage_write_lock()``) hace que solo un guardado esté en curso a
la vez, así que un único búfer sirve a los cinco almacenes.

La carga se hace con **cJSON**; los archivos faltantes o corruptos recurren a
valores por defecto que luego se guardan inmediatamente, de modo que cada archivo
siempre existe y es consistente. Las claves desconocidas en un archivo existente
se ignoran, así que los archivos de configuración antiguos siguen cargando.

Actualización de firmware OTA
=============================

La tabla de particiones proporciona dos ranuras de app (``ota_0`` / ``ota_1``),
lo que habilita la actualización OTA desde la página **About / Firmware** de la
administración web:

#. El operador elige un ``.bin`` y lo sube (``POST /ota_update``).
#. Se transmite en flujo directamente a la ranura inactiva vía
   ``esp_ota_write()`` — nunca se almacena entera en RAM — con una barra de
   progreso.
#. Una vez escrita y verificada, el dispositivo reinicia en la nueva ranura.

**Reversión automática.** ``CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`` está activo,
así que una imagen recién grabada arranca en estado "pendiente de verificar". El
firmware confirma la imagen solo tras montar NVS/LittleFS, levantar Wi-Fi y con
la administración web escuchando
(``esp_ota_mark_app_valid_cancel_rollback()`` en ``main.c``). Una imagen mala que
nunca alcanza ese listón se revierte automáticamente a la ranura anterior en el
siguiente reset.


