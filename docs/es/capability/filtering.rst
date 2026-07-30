.. _es-filtering:

========
Filtrado
========

El firmware aplica varios filtros independientes y componibles para decidir qué
paquetes cruzan entre RF y APRS-IS. Viven principalmente en
``main/aprs_filter.c`` y los usan tanto el IGate (``components/igate/igate.c``)
como el manejador INET→RF (``main/aprs_service.c``). Todos ellos **se componen
con semántica AND**: un paquete debe pasar cada filtro que aplique a su
dirección.

.. important::

   Estos filtros locales son enteramente distintos de la **cadena de filtro de
   servidor de APRS-IS** (``g_config.aprs_filter``), que es texto libre reenviado
   literalmente en la línea de login y aplicado por el servidor APRS-IS a lo que
   envía *hacia* el cliente. Los filtros locales de abajo deciden qué empuja el
   cliente *hacia afuera*, y qué retransmite por RF.

Clasificación por tipo de carga útil
====================================

``aprs_filter_classify_tnc2()`` / ``aprs_filter_classify_info()`` deciden a qué
único bit ``IGATE_FILT_*`` pertenece un paquete, trabajando sobre el
identificador de tipo de dato APRS (DTI) y — donde el DTI por sí solo es ambiguo
— sobre el símbolo que lleva el informe (``_`` → meteo, ``/N`` → boya):

.. list-table::
   :header-rows: 1
   :widths: 30 20 50

   * - Tipo
     - Bit
     - Notas
   * - Mensaje
     - ``1<<0``
     - mensajes APRS
   * - Estado
     - ``1<<1``
     - informes de estado
   * - Telemetría
     - ``1<<2``
     - informes de telemetría
   * - Meteorología
     - ``1<<3``
     - informe de posición con símbolo ``_``
   * - Objeto
     - ``1<<4``
     -
   * - Ítem
     - ``1<<5``
     -
   * - Consulta
     - ``1<<6``
     -
   * - Boya
     - ``1<<7``
     - informe de posición con símbolo ``/N``
   * - Posición
     - ``1<<8``
     - informe de posición simple

Una carga útil inclasificable — el tráfico de terceros (``}``) por encima de todo
— clasifica como ``0``, y ``aprs_filter_pass()`` nunca deja pasar ``0``:
desconocido significa "no retransmitir". Lo mismo ocurre con una máscara toda a
cero (todas las casillas desmarcadas): la máscara es una **lista blanca** de
tipos permitidos, exactamente como leen las casillas web.

Ambas direcciones usan el mismo clasificador y los mismos bits
(``g_config.rf2inetFilter`` para RF→INET, ``g_config.inet2rfFilter`` para
INET→RF), así que las dos nunca pueden divergir.

Guarda de rango local (RF→INET)
===============================

Cuando ``rf2inet_range_en`` está activo y ``rf2inet_range_km`` > 0, un paquete
cuya posición decodifica a más de esos kilómetros de "My Station"
(``my_lat``/``my_lon``) se descarta. La distancia es el círculo máximo
(``aprs_filter_haversine_km()``) entre los dos puntos. Los paquetes cuya posición
no se puede decodificar pasan esta comprobación. ``aprs_filter_decode_position()``
soporta tanto disposiciones sin comprimir (``DDMM.hhN/DDDMM.hhW``) como
comprimidas base-91 para los DTIs que llevan una posición en el campo de
información (``!``/``=``, ``/``/``@``, ``;`` objeto, ``)`` ítem). Los informes
Mic-E llevan la posición en el campo de destino AX.25 y no son decodificables
solo desde el campo de información.

Guarda de prefijo local (RF→INET)
=================================

Cuando ``rf2inet_prefix_en`` está activo, el indicativo de origen debe empezar
por uno de los prefijos separados por comas de ``rf2inet_prefixes`` (p. ej.
``EA,EB,EC``). Insensible a mayúsculas; se ignora el espacio en blanco alrededor
de las entradas. Un indicativo coincide si empieza por cualquier prefijo listado
(``aprs_filter_prefix_match()``).

Lista blanca / negra de indicativos (budlist)
=============================================

Una lista de indicativos compartida (``g_config.budlist[]``, indicativo base, sin
SSID) con un **modo por dirección**:

* ``BUDLIST_OFF`` — el filtro de indicativos está deshabilitado para esta
  dirección.
* ``BUDLIST_WHITELIST`` — solo se permiten pasar los indicativos de la lista.
* ``BUDLIST_BLACKLIST`` — los indicativos de la lista se bloquean; el resto pasa.

``aprs_filter_budlist_pass()`` compara sin distinguir mayúsculas y elimina
internamente cualquier sufijo ``-SSID``, de modo que tanto los llamadores de RF
(solo indicativo base) como de INET (pueden llevar ``-SSID``) pasan su indicativo
directamente.

Desempaquetado selectivo de terceros (solo INET→RF)
===================================================

Las cargas útiles de terceros (``}``) clasifican como ``0`` y nunca pasan por
defecto — re-enrutarlas sin restricción es la causa número uno de bucles de
IGate. Cuando ``inet2rf_3rdparty_unwrap_en`` está activo **y**
``inet2rf_budlist_mode == BUDLIST_WHITELIST``,
``aprs_filter_classify_thirdparty_inner()`` evalúa la carga útil *dentro* de un
nivel de empaquetado ``}`` para que el llamador pueda retransmitirla — pero solo
tras verificar que el origen del paquete interior está a su vez en la lista
blanca. Nunca es un interruptor general de "retransmitir todo lo de terceros".

Validación del filtro de servidor
=================================

``aprs_filter_validate_server_string()`` comprueba la *gramática* de
``g_config.aprs_filter`` antes de enviarse: cada término separado por espacios
debe ser ``<letra>/<args>`` con una letra de filtro conocida y la forma de
argumentos correcta para esa letra (``r`` necesita exactamente 3 args numéricos,
``p`` necesita al menos un prefijo, …). Valida solo la estructura, no si los
valores de coordenada/distancia son sensatos.
