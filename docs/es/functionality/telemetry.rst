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

Selectores de la página web
===========================

La página *Telemetry* (``page_tlm.c``) rellena un desplegable *Source* por cada
canal analógico y un desplegable *Channel* por cada bit digital a partir del
registro ``sensors_local`` en vivo, filtrado por los canales de telemetría
anunciados de cada controlador. Los valores por canal en vivo se muestran vía
``/tlm/values``. Véase :ref:`es-sensor-framework`.
