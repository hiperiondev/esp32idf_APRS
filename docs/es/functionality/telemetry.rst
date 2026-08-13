.. _es-telemetry:

==========
Telemetría
==========

El subsistema ``telemetry`` (``main/telemetry.c``) recoge canales analógicos y
digitales del registro ``sensors_local`` y transmite en baliza un informe de
datos de telemetría APRS estándar (``T#nnn``) por RF y/o APRS-IS, junto con los
mensajes de metadatos PARM/UNIT/EQNS/BITS que etiquetan esos canales para las
estaciones receptoras. Refleja el patrón que usa el subsistema meteorológico,
pero para telemetría.

Almacenamiento separado
=======================

A diferencia de la mayoría de los ajustes, la configuración de telemetría
deliberadamente **no** vive en ``g_config``/``config.json``. Persiste en su
propio pequeño archivo LittleFS, ``/storage/telemetry.json``, del mismo modo que
los boletines y objetos/ítems mantienen sus propios archivos. En el primer
arranque, o cuando el archivo falta, se crea un conjunto vacío por defecto para
que ``/storage/telemetry.json`` siempre exista una vez que el subsistema ha
arrancado. El esquema completo es ``telemetry_config_t``
(``main/include/telemetry.h``).

Canales
=======

Según el capítulo 13 de APRS101, un informe de telemetría lleva:

* **5 canales analógicos** ``A1``–``A5`` (``TLM_CH = 5``).
* **8 bits digitales** ``B1``–``B8`` (``TLM_BIT_NUM = 8``).

Cada canal analógico tiene una bandera de habilitación, un índice de canal de
sensor de origen (``tlm_ana_channel[]``, ``0xFF`` = ninguno), una calibración
cuadrática (``valor = a·x² + b·x + c``), un rango de entrada bruta esperado que
acota el valor transmitido, y un número de decimales. Cada bit digital tiene una bandera de habilitación, un canal
de origen, un sentido (Normal / Invertido), enrutamiento RF/INET por bit, y una
etiqueta orientada al operador usada en el mensaje BITS.

Qué va al aire
==============

``build_tlm_data_packet()`` (en ``telemetry.c``) resuelve cada canal mapeado del
registro (vía ``sensors_local_save_one()``) una vez por segundo y codifica el
informe de datos periódico:

.. code-block:: text

   T#sss,a1,a2,a3,a4,a5,bbbbbbbb

Los campos analógicos llevan la lectura **cruda** del sensor, acotada al rango
crudo declarado del canal y escrita con el ancho de campo y los decimales por
canal; los ocho caracteres ``b`` son los bits digitales. La calibración *no* se
aplica aquí: APRS101 separa el informe de los metadatos, así que el mensaje
``EQNS.`` lleva los coeficientes a/b/c y cada estación receptora recupera por sí
misma el valor de ingeniería. El informe **nunca** lleva nombres de canal —
según la especificación APRS, nombres, unidades y ecuaciones viajan por
separado.

Los mensajes de metadatos
=========================

A una cadencia más lenta (``info_interval``), el módulo emite los mensajes de
definición como mensajes APRS dirigidos a la propia estación:

.. code-block:: text

   :MYCALL   :PARM.<nombres analógicos>,<nombres de bits>
   :MYCALL   :UNIT.<unidades analógicas>,<etiquetas de estado-activo de bits>
   :MYCALL   :EQNS.<a,b,c por canal analógico>
   :MYCALL   :BITS.<mapa de bits de sentido>,<título del proyecto>

La generación de cada uno es conmutable individualmente (``gen_parm``,
``gen_unit``, ``gen_eqns``, ``gen_bits``).

Parámetros del informe
======================

La configuración también lleva opciones de encuadre del capítulo 13 de APRS101:
una ruta de digipeater de texto libre (``report_path``), TOCALL de destino
(``tocall``), número de secuencia autoincremental (``auto_seq``), ancho del campo
analógico (``field_width``), una opción para omitir los canales finales no usados
(``omit_trailing``), un comentario de texto libre al final (``trail_comment``), y
el número de canales analógicos/digitales realmente enviados (``analog_count`` /
``digital_count``).

``report_path`` se aplica solo a la transmisión de radio. La transmisión por
APRS-IS de un reporte de telemetría —datos y definiciones por igual— lleva
``TCPIP*`` como ruta completa, tal como `la guía de conexión de aprs-is.net
<https://www.aprs-is.net/Connecting.aspx>`_ exige del tráfico propio de un
cliente, así que cada pata habilitada se arma como su propia línea. Los
Mensajes de definición salen al aire sin ruta de digipetidores, directo, como
siempre.

Poner ``field_width`` en 3 rellena con ceros cada valor analógico a tres
dígitos, 000-999 - el rango que permite APRS 1.2 para este campo, ampliado
respecto a la ventana original 000-255 de APRS101. Un canal cuya estación
receptora aún espere el rango 0-255 anterior puede mantenerse dentro de él
ajustando ``ana_raw_min``/``ana_raw_max`` de ese canal.

Telemetría en el comentario (APRS 1.2 base-91)
================================================

Junto al informe ``T#nnn``, la opción *Comment Telemetry*
(``comment_telemetry`` / ``cmtTlm``) hace que
``telemetry_build_comment_tlm()`` añada una segunda codificación, compacta, de
la misma muestra al comentario de posición de una estación:

.. code-block:: text

   |ss1122|

El grupo se abre y se cierra con ``|``. El primer par en base-91 es el número
de secuencia; cada par siguiente es un canal analógico, en orden (``A1``
primero). Un par final puede llevar todo el banco digital de 8 bits como un
único número, siendo su bit menos significativo ``B1`` y su octavo bit ``B8``.

Esto no es una baliza propia. Viaja dentro del comentario de posición de la
baliza que esté transmitiendo en ese momento - Tracker, IGate o Digipeater -
bajo el indicativo/SSID configurado en la página *Telemetry*; una baliza de
posición que transmita bajo cualquier otro indicativo/SSID nunca recibe el
grupo añadido, ya que una estación receptora lo interpretaría como telemetría
de esa otra estación. Los informes de estado, objetos e ítems nunca lo llevan:
solo un informe de posición identifica a una única estación que reporta con
la claridad suficiente para que el grupo tenga sentido.

El número de secuencia es el mismo contador que usa el informe ``T#nnn``,
tomado del mismo instante de los valores de canal, de modo que ambos nunca
discrepan sobre qué muestra describen. La codificación base-91 da a ese
contador una ventana de 0-8280 (91×91 valores), que da la vuelta de forma
independiente al campo decimal 0-999 propio del informe.

Cada par analógico solo se emite para un canal habilitado y actualmente
resuelto desde el registro de sensores, y solo mientras todos los canales
anteriores en el orden A1-A5 también lo estén: el grupo no tiene identificador
de canal por par, así que una estación receptora recupera el canal de cada
valor únicamente por su posición en la secuencia. El codificador se detiene en
el primer hueco en vez de saltarlo, manteniendo el grupo como un prefijo
ininterrumpido A1, A2, ... An.

APRS 1.2 exige que la extensión lleve el contador de secuencia *y* al menos un
canal, así que una estación sin ningún canal analógico habilitado y resuelto no
emite grupo alguno en lugar de un ``|ss|`` pelado. Un grupo vacío es una forma
que un analizador estricto puede rechazar con razón, y gastaría cuatro bytes del
presupuesto de comentario en cada baliza sin llevar nada.

El par digital solo es legal después de los cinco pares analógicos - con un
grupo más corto delante, un receptor lo leería como el siguiente canal
analógico -, así que solo se emite cuando todos los canales analógicos se
resolvieron *y* el banco digital está ruteado con al menos un canal configurado.
El grupo es una única cadena añadida a un reporte de posición que sale por las
patas que use esa baliza, de modo que no tiene una forma propia por pata: un
canal digital viaja siempre que el banco y el canal estén ruteados a cualquiera
de las dos. Un canal que deba quedar fuera del aire por completo se deshabilita
en la página *Telemetry* en vez de desrutearse.

Un grupo que no quepa en el búfer de salida propio de
``telemetry_build_comment_tlm()`` se descarta en vez de truncarse - un par
base-91 truncado se decodifica como un valor incorrecto, no como uno ausente.
Una vez resuelto, los bytes del grupo (y los de la extensión ``!DAO!``
final, si está habilitada) se reservan antes que el propio texto del
comentario del operador, de modo que un informe de posición cuyo comentario
desbordaría el campo trunca el *comentario*, nunca el grupo de telemetría ni
la extensión DAO que lo sigue.

Dentro del campo de texto del informe de posición el orden de emisión es
fijo: bloque de frecuencia (si lo hay), comentario del operador, grupo de
telemetría en el comentario y, por último, ``!DAO!`` (si está habilitado) -
de acuerdo con el capítulo 13 de APRS101 y la propia regla de colocación de
la extensión DAO (``aprs12/datum.txt``). Este orden se mantiene tanto en el
formato sin comprimir como en Mic-E.

Selectores de la página web
===========================

La página *Telemetry* (``page_tlm.c``) rellena un desplegable *Source* por cada
canal analógico y un desplegable *Channel* por cada bit digital a partir del
registro ``sensors_local`` en vivo, filtrado por los canales de telemetría
anunciados de cada controlador. Los valores por canal en vivo se muestran vía
``/tlm/values``. Véase :ref:`es-sensor-framework`.
