.. _es-digipeater:

==========
Digipeater
==========

El componente ``digirepeater`` (``components/digirepeater/``) implementa el
digipeating APRS con reescritura de ruta. Su único punto de entrada,
``digiProcess()``, se llama desde el despacho de RX cuando ``g_config.digi_en``
está activo. Lee el indicativo/SSID del digipeater de ``g_config.digi_mycall`` /
``digi_ssid``, de modo que la página *Digi* de la administración web es la única
fuente de verdad.

El contrato de reescritura
==========================

``digiProcess(ax25_msg_t *packet)`` reescribe la ruta **en el sitio** y devuelve
uno de tres valores:

.. list-table::
   :header-rows: 1
   :widths: 12 88

   * - Retorno
     - Significado
   * - ``0``
     - No repetir (descartar / no es para nosotros / ya retransmitido / mal
       formado).
   * - ``1``
     - Repetir tal cual — la ruta ya lleva nuestro indicativo usado (p. ej. un
       ``*`` de bypass); el llamador retransmite la trama sin cambios.
   * - ``2``
     - Repetir con ruta modificada — el llamador recodifica la cabecera
       reescrita y la transmite por RF.

Cuando ``digiProcess()`` devuelve ``2``, el despacho de ``aprs_service.c``
re-renderiza la trama a TNC2, llama a ``aprs_service_send_tnc2()`` y, si tiene
éxito, incrementa el contador ``digi`` del panel y registra una entrada de
tráfico ``DIGI``.

La tabla de alias
=================

El digipeater no reconoce alias propios. Cada alias que atiende es una fila de
``g_config.digi_alias``, editada en *Alias de Ruta n-N* de la página *Digi*, y
la fila dice cómo se repite ese alias. Eso es lo que convierte las convenciones
locales del Nuevo Paradigma n-N — un ``WIDE1-1`` de relleno, un ``WIDE2-2`` de
dos saltos, un ``SSn-N`` regional — en un ajuste del operador y no en una
constante del firmware.

.. list-table::
   :header-rows: 1
   :widths: 16 12 72

   * - Campo
     - Rango
     - Significado
   * - Alias
     - 6 caracteres
     - El indicativo del repetidor **sin** su SSID; el SSID es el contador de
       saltos *N* y se maneja aparte. ``#`` equivale exactamente a un dígito
       decimal, así una fila cubre toda una familia: ``WIDE#`` reclama
       ``WIDE1`` hasta ``WIDE9``, pero nunca ``WIDE``, ``WIDEN`` ni
       ``WIDE12``, porque la coincidencia también exige igual longitud. Un
       alias vacío deshabilita la fila.
   * - N máximo
     - 1–7
     - El mayor contador de saltos que se atiende para este alias. Un *N* mayor
       recibido al aire queda *atrapado*.
   * - Modo
     - Apagado / Traza / Inundación
     - **Traza** inserta el indicativo de esta estación delante del alias
       restante y lo marca como usado, de modo que cada salto de la ruta puede
       atribuirse después. **Inundación** decrementa el contador de saltos y no
       deja constancia de quién lo hizo. **Apagado** ignora la fila por
       completo.

Las filas se consultan en orden de tabla y gana la primera coincidencia, así
que un alias específico colocado encima de una fila comodín conserva su propio
límite de saltos. La tabla de fábrica es ``WIDE1`` (1 salto), ``WIDE2``
(2 saltos) y ``WIDE#`` (2 saltos), todas en modo traza, con la cuarta fila
libre para un alias regional.

``WIDEn-N`` está obligado a trazar. El paradigma lo movió del mecanismo de
inundación no rastreable al de trazado precisamente para que cada salto de cada
ruta ``WIDEn-N`` sea identificable — por eso *Inundación* solo es apropiado
para un alias regional que el operador decida usar sin traza.

``RELAY``, ``GATE``, ``ECHO`` y ``TRACEn-N`` no están incorporados: fueron
abandonados como rutas y ya no están en el firmware. Un operador que aún
necesite alguno para un vecino heredado lo agrega como una fila más.

Atrapado y el rol de relleno
============================

Un contador de saltos por encima del *N máximo* de la fila coincidente queda
atrapado, y *Saltos por encima del N máximo* elige cómo: **Limitar al N máximo**
(el valor por omisión) lo baja al límite y repite la trama, **Descartar la
trama** la rechaza de plano y cuenta ``DROP_DIGI_N_TRAPPED``. Limitar mantiene
la trama en movimiento y a la vez impide que inunde más allá de lo que permiten
las condiciones locales, y por eso es el valor por omisión; cada salto adicional
multiplica por unas tres veces la carga que una trama impone a la red.

*Digipetidor de relleno (un solo salto)* restringe la estación a las filas cuyo
límite de saltos es 1. Ese es todo el rol de relleno: levanta el tráfico de los
vecinos que no alcanzan la red troncal directamente y deja todo lo ruteado para
más saltos a los digipeaters amplios, que es lo que evita que una estación
doméstica en un valle agregue una copia redundante de cada paquete de la región.

Dos tramas se rechazan antes de consultar la tabla: la que ya lleva el
indicativo de esta estación marcado como usado, sin importar lo que aún tenga su
ruta, y la que coincide con la ventana de supresión de duplicados de abajo.

* **WIDEn-N codificado en el campo SSID de destino** — la convención más antigua
  donde el contador de saltos vive en el nibble SSID de la dirección de destino
  AX.25 también se reconoce y maneja, antes que la tabla de alias.

Supresión de duplicados
=======================

Antes de cualquier trabajo sobre la ruta, el digipeater comprueba la trama con
``isDuplicatePacketScoped(packet, DUP_SCOPE_DIGI)``. La clave se construye solo
con la dirección de origen y el campo de información — nunca con la ruta — así
que todas las copias de una misma transmisión producen el mismo hash sin
importar por dónde llegaron. Una trama que coincide con otra repetida dentro de
``g_config.dup_cache_timeout_ms`` (30 s por defecto, editable en la página
*IGate*) se descarta, que es lo que evita que dos
digipeaters dentro de la cobertura mutua se reboten la misma trama, y lo que
absorbe un eco de RF de una trama que esta estación acaba de repetir.

La caché se comparte con el IGate pero las ventanas no: cada entrada lleva el
ámbito que la insertó y solo coincide con búsquedas de ese mismo ámbito. Ambos
consumidores ven las mismas tramas desde el mismo despacho de RX, y el
digipeater corre primero, así que una única ventana compartida haría que
consumiera todas las tramas y el IGate las tratara a todas como duplicadas.

Contadores
==========

El digipeater no lleva contadores propios. Todo lo que el operador puede ver
sobre él viene de dos lugares que avanzan estén o no activos ``digi_en`` e
``igate_en``:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Cifra
     - De dónde sale
   * - Contador ``digi`` de titular
     - ``aprs_service.c``, incrementado en el punto en que la trama reescrita
       se transmite realmente. Solo avanza mientras ``digi_en`` está activo,
       porque con él apagado no hay nada que digipetear.
   * - Cada descarte y trama mal formada
     - ``igate_note_drop(DROP_DIGI_…)``, que alimenta la tabla por motivo que
       el panel muestra como *Drop Breakdown*. Cada motivo es una fila
       distinta, así que un duplicado, una ruta llena y un indicativo de
       relleno se distinguen en vez de fundirse en un único total.

.. note::

   Los motivos ``DROP_DIGI_*`` se cuentan dentro de ``digiProcess()``, así que
   solo avanzan mientras el digipeater corre. Las tramas descartadas antes del
   despacho, o de salida hacia RF, se cuentan a nivel de servicio en
   ``aprs_service.c`` y aparecen esté o no habilitada cualquier función.
