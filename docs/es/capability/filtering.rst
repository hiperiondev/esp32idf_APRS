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
   * - Otro
     - ``1<<9``
     - Capacidades de estación, formatos definidos por el usuario, radiogonio-
       metría Agrelo, balizas de locator Maidenhead y la característica de
       mapa reservada — los tipos de carga útil sin bit propio, agrupados
       aquí para que puedan retransmitirse en vez de descartarse en silencio.

Una carga útil inclasificable — el tráfico de terceros (``}``) por encima de todo
— clasifica como ``0``, y ``aprs_filter_pass()`` nunca deja pasar ``0``:
desconocido significa "no retransmitir". Lo mismo ocurre con una máscara toda a
cero (todas las casillas desmarcadas): la máscara es una **lista blanca** de
tipos permitidos, exactamente como leen las casillas web.

Ambas direcciones usan el mismo clasificador y los mismos bits
(``g_config.rf2inetFilter`` para RF→INET, ``g_config.inet2rfFilter`` para
INET→RF), así que las dos nunca pueden divergir.

Guarda de consulta genérica (obligatoria, ambas direcciones)
=============================================================

Una carga útil cuyo primer byte es ``?`` — una consulta genérica como
``?APRS?``, ``?WX?`` o ``?IGATE?`` — nunca se enruta, en ninguna dirección
(``DROP_GENERIC_QUERY``). Esta comprobación se ejecuta antes del filtro por
tipo de carga útil y **no** es uno de los bits componibles ``IGATE_FILT_*``:
no se puede desactivar, y ningún estado de
``rf2inetFilter``/``inet2rfFilter`` deja pasar una consulta genérica. Enrutar
una permitiría que una sola estación de RF desencadenara una respuesta de
respondedor de consultas de cada estación conectada a APRS-IS que implemente
uno, atribuyendo el indicativo de esta estación a la inundación resultante a
través del constructo ``qAR`` — lo mismo ocurre a la inversa para una
consulta genérica enrutada hacia RF.

Una consulta **dirigida** (``:CALLSIGN :?APRSD``, identificador de tipo de
dato ``:``) no empieza por ``?`` y no se ve afectada por esta guarda; se
clasifica como ``IGATE_FILT_MESSAGE`` y solo está sujeta al filtro ordinario
por tipo de carga útil de abajo, igual que cualquier otro mensaje.

``IGATE_FILT_QUERY`` en sí sigue existiendo como salida de
``aprs_filter_classify_info()`` / ``aprs_filter_classify_tnc2()`` y en
``aprs_filter_type_name()``, para la contabilidad propia del respondedor de
consultas local — pero ninguna casilla web se corresponde con él, ya que una
consulta que llega al filtro por tipo ya ha sobrevivido, por construcción, a
la guarda obligatoria de arriba.

Guarda de rango local (RF→INET)
===============================

Cuando ``rf2inet_range_en`` está activo y ``rf2inet_range_km`` > 0, un paquete
cuya posición decodifica a más de esos kilómetros de "My Station"
(``my_lat``/``my_lon``) se descarta. La distancia es el círculo máximo
(``aprs_filter_haversine_km()``) entre los dos puntos. Los paquetes cuya posición
no se puede decodificar pasan esta comprobación. ``aprs_filter_decode_position()``
soporta disposiciones sin comprimir (``DDMM.hhN/DDDMM.hhW``) y comprimidas
base-91 para los DTIs que llevan una posición únicamente en el campo de
información (``!``/``=``, ``/``/``@``, ``;`` objeto, ``)`` ítem), además de los
informes Mic-E (`` ` ``/``'``/0x1c/0x1d), cuya posición se reparte entre el
campo de información y el campo de destino AX.25 y es reconstruida por
``aprs_mice_decode()`` antes de aplicar la misma comprobación de rango.

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


Filtrado del registro de tráfico
================================

``igate_log_after_filters`` (*Registrar después de los filtros* en la página
IGate, desactivado por defecto) reutiliza los filtros anteriores como filtro de
**visualización** de la tabla de tráfico web y de las líneas correspondientes de
la consola serie: con la opción activa, solo se emite una entrada ``RX`` para
una trama que acepta
``igate_log_accepts_frame()`` (Lista de Satélites Digipetidores,
``rf2inetFilter``, los filtros de rango y prefijo RF→INET, el filtro de
indicativos RF→INET) y una entrada ``RX-IS`` solo para una línea que acepta
``igate_log_accepts_line()`` (la excepción de posición asociada, el filtro de
rango INET→RF, la máscara ``inet2rfFilter`` incluido el desempaquetado selectivo
de terceros, el filtro de indicativos INET→RF). El lado RF comparte
implementación con la ruta de pasarela; el lado INET→RF aplica las mismas
comprobaciones y en el mismo orden que ``inet2rfHandler()``, posiciones de
seguimiento incluidas, así que el registro y la pasarela no pueden discrepar. No cambia nada de lo que se
pasarela, repite o transmite y ningún contador de descartes se mueve: una trama
omitida del registro se sigue tratando igual que antes.
