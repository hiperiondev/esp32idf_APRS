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

Esquemas de ruta soportados
===========================

* **WIDEn-N** — el alias de inundación estándar. El contador de saltos *N* se
  decrementa y se inserta el propio indicativo del digipeater (marcado como
  usado con ``*``) cuando se consume el alias.
* **TRACEn-N** — como WIDEn-N pero cada salto inserta su indicativo, construyendo
  una traza explícita de la ruta seguida.
* **RELAY / GATE / ECHO** — los alias genéricos heredados, cada uno sustituido
  por el indicativo del digipeater.
* **WIDEn-N codificado en el campo SSID de destino** — la convención más antigua
  donde el contador de saltos vive en el nibble SSID de la dirección de destino
  AX.25 también se reconoce y maneja.

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
