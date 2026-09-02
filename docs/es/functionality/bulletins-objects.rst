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
* un **intervalo** inicial de transmisión, con una **rampa de decaimiento**
  opcional: una cadencia lenta y la razón por la que se multiplica el hueco
  tras cada transmisión,
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

El capítulo 14 describe un boletín como algo que se repite a menudo en su
primera hora y luego cada vez menos a lo largo de las horas siguientes, y un
anuncio como algo que se repite mucho más despacio todavía. Cada casilla lleva
ese afinamiento como una rampa de decaimiento sobre su intervalo inicial: tras
cada transmisión el intervalo vivo se multiplica por la **razón de
decaimiento** de la casilla hasta alcanzar su **cadencia lenta**, donde se
mantiene. Un boletín enviado cada diez minutos con una razón de 2.0 y una
cadencia lenta de dos horas sale así tres veces en su primera hora y se asienta
en dos horas, lo que llega al mismo público con una fracción del tiempo de aire
en un canal compartido.

Dejar la cadencia lenta en 0, o la razón por debajo de 1.0, mantiene el
intervalo plano, que es también como se comporta un boletín almacenado que no
lleva ninguno de los dos campos. La rampa es estado de ejecución y no
configuración: no se persiste, y un reinicio o cualquier edición de la casilla
la reinicia en el
intervalo inicial, de modo que un boletín reescrito o retemporizado vuelve a
oírse pronto en lugar de al espaciado al que había decaído el texto anterior.

La caducidad y la rampa son complementarias. La rampa adelgaza las repeticiones
mientras el boletín es vigente; la caducidad decide cuándo deja de serlo. Un
boletín caducado limpia automáticamente su bandera de habilitación y sale del
aire. Los boletines persisten en su propio ``/storage/bulletins.json``. La página
está condicionada por el interruptor de compilación ``ENABLE_BULLETINS``.

Un boletín que esta estación transmite también se entrega al bot de Telegram,
en los mismos términos que uno escuchado de otra estación, de modo que los
operadores que leen el bot ven los anuncios propios de la estación junto a los
de todos los demás. Lo gobierna el interruptor "Reenviar boletines" de la
página *Telegram*; ver :ref:`es-telegram`.

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

Objetos de área
---------------

Un elemento cuyo símbolo es el símbolo de área (``\\l``, la letra L minúscula
de la tabla alternativa) dibuja una figura en el mapa receptor en lugar de un
punto. La ranura de extensión de datos de 7 bytes lleva entonces el descriptor
``Tyy/Cxx`` del capítulo 11 de APRS101 en vez de rumbo/velocidad:

* **forma** — uno de los diez dígitos: círculo, línea abajo/derecha, elipse,
  triángulo, caja, y luego el círculo relleno, la línea abajo/izquierda, la
  elipse rellena, el triángulo relleno y la caja rellena,
* **color** — de 0 a 15. Los valores de diez en adelante reemplazan la barra
  por un ``1`` y escriben el dígito de las unidades, así que el campo mide
  siete bytes fijos en cualquier caso,
* **desplazamientos de latitud y longitud** — la distancia en grados desde la
  posición reportada, que es la esquina superior izquierda de la figura, hasta
  su esquina inferior derecha (o hasta el centro, en el caso de un círculo).

Cada desplazamiento se transmite como un código de dos dígitos, la raíz
cuadrada del desplazamiento expresado en 1500-avos de grado; el receptor lo
recupera como ``código × código ÷ 1500``. La especificación usaba
originalmente un factor de 100 y fue corregida a 1500 por
``aprs.org/aprs11/areaobjects.txt``, que es la escala con la que decodifican
las aplicaciones actuales. Dos dígitos alcanzan entonces 6,534 grados por eje,
y ambos campos de desplazamiento se limitan a ese valor al guardar, para que el
valor almacenado y la figura transmitida describan siempre la misma área.

Las dos formas de línea pueden además declarar un **corredor**: una franja del
ancho indicado en millas a cada lado de la línea, transmitida como un token
``{www}`` al frente del texto del comentario, exactamente donde lo ubica el
ejemplo de la propia especificación. Un ancho de cero omite el token, y el
campo se ignora para las ocho formas cerradas.

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
