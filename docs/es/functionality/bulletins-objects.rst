.. _es-bulletins-objects:

==========================
Boletines, Objetos e Ítems
==========================

Dos subsistemas permiten a la estación transmitir anuncios permanentes y puntos
de mapa con nombre propios. Ambos mantienen su estado en archivos LittleFS
dedicados en lugar de en ``g_config``, para mantener pequeña la configuración
residente, y ambos los acciona el planificador de balizas compartido.

Boletines
=========

``main/bulletins.c`` transmite hasta cinco boletines APRS. Cada boletín tiene:

* su propio texto,
* un **identificador** y un nombre de **grupo** de destinatario,
* una habilitación **RF** y/o **APRS-IS**,
* un **intervalo** de transmisión,
* una ventana opcional de **"caducar tras N horas"**.

El identificador y el grupo juntos seleccionan cuál de las tres formas de
destinatario que define el capítulo 14 de APRS101 sale al aire:

.. list-table::
   :header-rows: 1
   :widths: 25 20 55

   * - Forma
     - Destinatario
     - Cuándo
   * - Boletín general
     - ``BLN1``
     - Identificador ``0``–``9``, sin nombre de grupo. Los boletines que
       comparten identificador se reemplazan entre sí en el receptor, así que el
       identificador funciona además como número de ranura para un boletín de
       varias líneas.
   * - Boletín de grupo
     - ``BLN1WX``
     - Identificador ``0``–``9`` más un nombre de grupo de hasta cinco
       caracteres. Solo lo muestran las estaciones suscritas a ese grupo.
   * - Anuncio
     - ``BLNQ``
     - Identificador ``A``–``Z`` y sin nombre de grupo. La mayoría del software
       cliente conserva y vuelve a mostrar los anuncios mucho más tiempo que los
       boletines, y por eso la especificación les da su propio espacio de
       identificadores.

``bulletins_build_addressee()`` normaliza mientras construye, de modo que nada
que el campo de destinatario de 9 caracteres no pueda llevar llega al aire: un
identificador fuera de ``0``–``9``/``A``–``Z`` cae al dígito de la propia
ranura, el nombre de grupo se pasa a mayúsculas y se le quita todo lo que no sea
``A``–``Z``/``0``–``9``, y un identificador de anuncio suprime el grupo por
completo.

Un boletín caducado limpia automáticamente su bandera de habilitación y sale del
aire. Los boletines persisten en su propio ``/storage/bulletins.json``. La página
está condicionada por el interruptor de compilación ``ENABLE_BULLETINS``.

.. note::

   Los radiogramas NTS, también descritos en el capítulo 14, son un formato de
   mensaje de tráfico y no una forma de destinatario de boletín, y no se
   producen aquí.

Objetos e Ítems
===============

``main/objects_items.c`` transmite hasta cinco Objetos/Ítems APRS, cada uno con:

* un **nombre**, **posición** y **símbolo**,
* **rumbo/velocidad** y **comentario** opcionales,
* una habilitación **RF** y/o **APRS-IS**,
* un **intervalo de repetición** con decaimiento de intervalo opcional,
* un control de **Tipo**: Objeto (con marca de tiempo, ``;``) o Ítem (sin
  marca de tiempo, ``)``).

**Matar** un objeto lo transmite unas cuantas veces extra (para que los oyentes
lo eliminen de sus mapas), luego lo deshabilita automáticamente. Los
objetos/ítems persisten en su propio ``/storage/objitems.json``. La página está
condicionada por el interruptor de compilación ``ENABLE_OBJECTS_ITEMS``.

Objetos permanentes
--------------------

Un Objeto también puede marcarse como **permanente**. Un Objeto permanente se
transmite con la marca de tiempo ficticia fija ``111111z`` que define
``freqspec.txt``, en lugar de la hora ``DDHHMMz`` en vivo. Esta es la
convención recomendada para objetos de frecuencia de repetidores de voz y
anuncios recurrentes similares propios de la estación: una estación receptora
interpreta la marca ``111111z`` como indicación de que el Objeto no debe ser
reemplazado por ningún Objeto homónimo de otra estación, y que solo la propia
estación de origen puede actualizarlo o moverlo.

La casilla Permanente solo se aplica a un Objeto; no tiene ningún efecto sobre
un Ítem, que nunca lleva marca de tiempo de ningún tipo.

Por qué archivos JSON separados
===============================

Ambos subsistemas, como la telemetría, mantienen estado específico de página que
agrandaría significativamente el ``app_config_t`` residente (y por tanto cada
guardado de ``config.json``, que se ejecuta contra un heap pequeño y
fragmentado). Mantenerlos en sus propios archivos significa que la configuración
residente se mantiene ligera y que el guardado de cada subsistema solo toca sus
propios datos. Cada archivo se escribe con el mismo escritor JSON en flujo,
byte a byte, que usa la configuración principal, bajo su propio mutex, con un
``setvbuf()`` explícito para que newlib no asigne perezosamente un búfer stdio
grande a mitad de escritura sobre un heap fragmentado.
