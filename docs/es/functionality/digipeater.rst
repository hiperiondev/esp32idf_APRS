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

Antes de repetir, el digipeater comprueba la trama contra la misma caché de
detección de duplicados que usa el IGate (``isDuplicatePacket()``), de modo que
una trama ya digipeteada dentro de la ventana de supresión no se retransmite —
la defensa clásica contra el ping-pong entre digipeaters.

Contadores
==========

``digi_get_stats()`` devuelve un ``digi_stats_t``:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Contador
     - Significado
   * - ``rxPkts``
     - Paquetes vistos por el digipeater.
   * - ``txPkts``
     - Paquetes digipeteados (ruta modificada, ``digiProcess()`` devolvió ``2``).
   * - ``dropRx``
     - Paquetes descartados (duplicado, ruta filtrada, no para nosotros, ya
       retransmitido).
   * - ``erPkts``
     - Paquetes mal formados (demasiado cortos / sin ruta).

.. note::

   Estos contadores por-función solo avanzan mientras ``digi_en`` está activo. El
   contador ``digi`` de titular del panel se rastrea por separado en
   ``aprs_service.c`` en el punto en que la trama reescrita se transmite
   realmente, así que refleja la realidad estén o no habilitadas otras funciones.
