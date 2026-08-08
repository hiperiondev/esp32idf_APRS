.. _es-igate:

========================
IGate — pasarela APRS-IS
========================

El componente ``igate`` (``components/igate/``) es una pasarela de Internet
APRS-IS bidireccional completa, construida sobre sockets LWIP. Lee toda su
configuración de ``g_config`` (la página *IGate* de la administración web), de
modo que la administración web es la única fuente de verdad.

La tarea cliente de APRS-IS
===========================

* **Cliente TCP** con failover multiservidor y reconexión automática. Relee
  ``g_config`` en cada reconexión, así que la mayoría de los cambios de la web
  surten efecto tras el siguiente ciclo de reconexión, sin reiniciar.
* **Condicionado a conectividad real**, no simplemente a que "el Wi-Fi está
  arriba": sondea ``net_state_is_connected()``, que solo se vuelve verdadero con
  ``IP_EVENT_STA_GOT_IP`` y falso de nuevo al desconectarse o en modo solo-AP.
* **Línea de login:** ``user <mycall> pass <passcode> vers ESP32APRS 1.0 filter
  <filter>`` — registrada literalmente, para que un filtro mal formado sea
  visible. El banner del servidor y la línea ``# logresp … verified/unverified``
  se muestran; una respuesta ``unverified`` genera una advertencia que nombra
  ``aprs_mycall`` / ``aprs_passcode``.
* **Validación del filtro de servidor.** Antes de enviarse, ``g_config.aprs_filter``
  se comprueba estructuralmente con ``aprs_filter_validate_server_string()`` —
  cada término separado por espacios debe ser ``<letra>/<args>`` con el número de
  argumentos correcto para esa letra de filtro.
* **Enlace de subida compartido.** La tarea siempre se ejecuta, porque el mismo
  socket lo usan el componente de mensajería (``igate_send_raw()``) y la "baliza
  a internet". Queda en reposo de forma barata cuando nada lo necesita.

Failover de servidores
======================

La página IGate almacena ``APRS_SERVER_NUM`` (cuatro) ranuras de servidor en
``g_config.aprs_server[]``, cada una con su propia casilla Habilitar, host y
puerto. Todas las ranuras comparten una única identidad de login — indicativo,
SSID, passcode y cadena de filtro son un solo valor — porque representan la
misma estación conectándose a servidores APRS-IS alternativos.

``connectAprsIs()`` marca la ranura seleccionada en ese momento. Cualquier fallo
— resolución DNS, ``socket()``, ``connect()`` o el envío de la línea de login —
llama a ``advanceServer()``, que mueve la selección a la siguiente ranura
**habilitada** con vuelta circular, y la tarea espera 1 segundo antes del
siguiente intento. La rotación no se detiene nunca: sigue recorriendo todas las
ranuras habilitadas hasta que una acepte la conexión.

Las ranuras deshabilitadas se saltan también en la **primera** selección tras el
arranque, no solo después de un fallo: desmarcar la casilla de una ranura la
retira del servicio de inmediato. Si no hay ninguna ranura habilitada, la tarea
recae en la ranura 1, de modo que siempre tiene un destino concreto que intentar
y registrar.

Una conexión ya establecida no hace rotar la selección: si el servidor cierra el
enlace, la tarea reintenta primero la misma ranura, y solo pasa a la siguiente
cuando ese nuevo intento también falla.

El panel muestra el host y el puerto de la ranura en uso en ese momento
(``igate_get_current_server()``), así que se ve de un vistazo en qué servidor se
acabó tras un failover.

RF → INET (``igateProcess()``)
==============================

Cada trama decodificada por RF que la aplicación despacha (con ``igate_en`` y
``rf2inet`` activos) atraviesa esta tubería, en orden. Una trama que falla
cualquier etapa se descarta, y la *razón* se registra en un contador por-razón,
para que el panel pueda mostrar "N descartadas por X" en lugar de un único
agregado opaco.

#. **Supresión de duplicados.** La trama se comprueba contra la caché de
   duplicados compartida (``isDuplicatePacket()``). Tanto su profundidad
   (``g_config.dup_cache_size``, ``DUP_CACHE_SIZE_MIN``..``DUP_CACHE_SIZE_MAX``
   = 4..40, por defecto 20) como su ventana (``g_config.dup_cache_timeout_ms``,
   1000..120000 ms, por defecto 30000) se editan en la página *IGate* y se
   releen en cada consulta, así que un cambio se aplica sin reiniciar. El
   arreglo siempre se reserva con la capacidad de compilación
   ``DUP_CACHE_SIZE_MAX``; ``dup_cache_size`` solo elige cuánto de ella se usa.
   Los duplicados se cuentan aparte en ``dupCount``.
#. **Guarda de trama demasiado corta.** Las tramas cuyo campo de información está
   por debajo de la longitud mínima utilizable se descartan (``DROP_TOO_SHORT``).
#. **Filtro de token de ruta.** Las tramas cuya ruta lleva ``RFONLY``, ``TCPIP``,
   ``qA*`` o ``NOGATE`` nunca se enrutan (``DROP_PATH_TOKEN``).
#. **Regla de gate por satélite.** Una trama repetida vía una pasarela satelital
   conocida cuyo indicativo no está marcado como usado (``*``) se descarta
   (``DROP_SAT_NOT_USED``).
#. **Desempaquetado de terceros (``}``).** Una trama cuyo campo de información
   empieza por ``}`` lleva su propia línea interior completa
   ``SRC>DST,PATH:carga``. Si esa ruta interior ya lleva ``TCPIP`` o
   ``TCPXX``, la trama ya llegó a APRS-IS una vez y se descarta como bucle
   (``DROP_3RDPARTY_LOOP``). En caso contrario se descarta por completo la
   cabecera RF exterior y el resto de las etapas — desde el filtro por tipo de
   carga útil en adelante — se ejecutan contra el paquete interior: su propio
   origen, destino, ruta y carga útil, exactamente como si esa estación se
   hubiera escuchado directamente. Esto es lo que permite que una pasarela
   cruzada de banda o HF reenrute una estación que no tiene otra ruta a
   Internet.
#. **Guarda de consulta genérica.** Una carga útil cuyo primer byte es ``?``
   (``?APRS?``, ``?WX?``, ``?IGATE?``, …) se descarta incondicionalmente
   (``DROP_GENERIC_QUERY``), sin importar ``g_config.rf2inetFilter`` ni
   ninguna otra casilla. Véase :ref:`es-filtering`.
#. **Filtro por tipo de carga útil.** La carga útil (posiblemente
   desempaquetada) se clasifica con ``aprs_filter_classify_info()`` y se
   prueba contra ``g_config.rf2inetFilter`` (``DROP_TYPE_FILTER``). Véase
   :ref:`es-filtering`.
#. **Guarda de rango local.** Si está habilitada, se decodifica la posición del
   paquete y su distancia de círculo máximo (haversine) desde "My Station" se
   compara con ``g_config.rf2inet_range_km``; los paquetes demasiado lejanos se
   descartan (``DROP_RANGE_FILTER``). Los paquetes cuya posición no se puede
   decodificar pasan esta comprobación.
#. **Guarda de prefijo local.** Si está habilitada, el indicativo de origen debe
   empezar por uno de los prefijos separados por comas de
   ``g_config.rf2inet_prefixes`` (p. ej. ``EA,EB,EC``), o se descarta
   (``DROP_PREFIX_FILTER``).
#. **Budlist.** El indicativo de origen se prueba contra la lista
   blanca/negra local en ``g_config.rf2inet_budlist_mode`` (``DROP_BUDLIST``).

Una trama que sobrevive a todas las etapas recibe una cabecera
``,qAR,<mycall>-<ssid>`` — o ``,qAO,<mycall>-<ssid>`` cuando esta IGate no
puede pasar mensajes de vuelta a RF para la estación que se está pasando a
APRS-IS (``aprs_service_can_gate_to_rf()``, es decir, no se puede transmitir,
``igate_en`` está desactivado, o ``inet2rf`` está desactivado) — y se escribe
en APRS-IS. El indicativo-SSID que sigue al q construct es siempre la propia
identidad de login de esta estación, según QCON.

INET → RF (``inet2rfHandler()``)
================================

Cada línea distinta de ``#`` leída del socket incrementa ``isRxCount`` y se
entrega al motor de mensajería (``handleIncomingAPRS()``) cuando la mensajería
está activa. Luego se considera para retransmisión por RF solo si ``inet2rf``
está activo, y solo tras pasar:

#. **Guarda de consulta genérica.** Una línea cuya carga útil empieza por
   ``?`` se descarta incondicionalmente (``DROP_GENERIC_QUERY``), sin
   importar ``g_config.inet2rfFilter`` ni ninguna otra casilla — la imagen
   especular de la guarda de consulta genérica RF→INET de arriba, y se
   comprueba antes que cualquier otra etapa siguiente. Véase
   :ref:`es-filtering`.
#. **Supresión de eco de informes propios.** Cada informe que esta estación sube
   con su bandera ``*_2inet`` es devuelto como eco directamente por el servidor
   APRS-IS. ``inet_line_is_own_report()`` reconoce esos ecos (comparando el
   indicativo base de origen contra cada indicativo de informe de la propia
   estación) y nunca los reenruta de vuelta a RF. Los informes propios llegan a
   RF exclusivamente a través de sus propias banderas "Send via RF" (``*_2rf``).
#. **Filtro por tipo de carga útil.** La línea se clasifica con
   ``aprs_filter_classify_tnc2()`` y se prueba contra ``g_config.inet2rfFilter``.
#. **Desempaquetado selectivo de terceros (opcional).** El tráfico de terceros
   (``}``) — el clásico origen de bucles de IGate — clasifica como 0 y nunca se
   reenvía por defecto. Con ``inet2rf_3rdparty_unwrap_en`` activo **y**
   ``inet2rf_budlist_mode == BUDLIST_WHITELIST``, se puede desenvolver un nivel
   del empaquetado ``}`` y el paquete interior se reclasifica y reenvía, pero
   *solo* cuando el origen del paquete interior está a su vez en la lista blanca.
   Nunca es un interruptor general de "reenviar todo lo de terceros".
#. **Budlist.** El indicativo de origen (que aquí puede llevar un ``-SSID``) se
   prueba contra ``g_config.inet2rf_budlist_mode``.
#. **Filtrado de mensajes.** Se aplica solo al tipo ``MESSAGE``; los demás tipos
   se retransmiten a criterio del sysop, que es lo que ya expresan el filtro de
   tipos y la budlist de arriba. Ver abajo.

Una línea que supera todas las etapas nunca se transmite por RF con su
cabecera de APRS-IS intacta. ``build_thirdparty_frame()`` descarta esa
cabecera por completo y envuelve el ``SRC>DST`` original y el campo de
información, sin modificar, tras un ``}`` como carga útil de la cabecera
propia de esta estación (``MYCALL[-SSID]>APE32L,<ruta igate>:}SRC>DST,TCPIP,
MYCALL[-SSID]*:info``) — la forma de terceros que exige la especificación
APRS para el tráfico reenrutado. Esto mantiene los constructos ``qA`` y un
``TCPIP`` sin envolver fuera del aire, y permite que cualquier otro IGate que
escuche el paquete lo reconozca como ya reenrutado en lugar de reenviarlo de
vuelta.

.. warning::

   Reenrutar tráfico de terceros sin restricción es la causa número uno de
   bucles de IGate. El desempaquetado de terceros está deliberadamente
   condicionado a una opción explícita *y* a una lista blanca por exactamente
   esta razón.

Filtrado de mensajes
====================

Un IGate está sentado sobre un flujo de datos enorme y no debe retransmitir de
forma indiscriminada. Con ``igate_msg_gate_en`` activo (el valor de fábrica), un
mensaje APRS leído de APRS-IS sale al aire solo si se cumplen **las cuatro**
condiciones a la vez:

.. list-table::
   :header-rows: 1
   :widths: 46 54

   * - Condición
     - Motivo de descarte cuando falla
   * - La cabecera del remitente no lleva ``TCPXX``, ``NOGATE`` ni ``RFONLY``
     - ``DROP_MSG_NOGATE``
   * - El destinatario fue escuchado por RF dentro de ``igate_local_window_sec``
     - ``DROP_MSG_NOT_LOCAL``
   * - El destinatario no está a su vez conectado a Internet
     - ``DROP_MSG_ADDRESSEE_INET``
   * - El remitente **no** fue escuchado por RF dentro de la misma ventana
     - ``DROP_MSG_SENDER_LOCAL``

Cada fallo tiene su propio motivo, así el *Drop Breakdown* del panel dice qué
condición detuvo un mensaje — la pregunta de soporte más frecuente sobre un
IGate. Solo se busca en la cabecera los tokens ``TCPXX``/``NOGATE``/``RFONLY``,
de modo que un mensaje cuyo *texto* mencione alguno no se confunde con uno
ruteado con él.

Las pruebas de localidad leen ``lastheard_heard_rf_within()`` y
``lastheard_heard_inet_within()``, que guardan una marca de tiempo por canal:
una estación puede ser audible localmente y estar conectada a Internet a la vez,
y cada condición prueba la suya. Una trama escuchada al aire también cuenta como
avistamiento por Internet cuando su ruta lleva ``TCPIP`` o ``TCPXX`` — la firma
al aire de un paquete que ya pasó por una pasarela.

*Ventana de escucha local (s)* es ``igate_local_window_sec``, 60–3600 s, una hora
por omisión, que es la cota superior que recomiendan las notas de diseño de
IGate de APRS-IS.

Desactivar el filtrado de mensajes transmite **todo** mensaje que permita el
filtro de tipos, a destinatarios de cualquier parte del mundo, haya o no en el
canal local alguien capaz de escucharlos.

Posición asociada
=================

En vez de repetir los reportes de posición históricos de una estación, la
pasarela anota las estaciones a las que **le** retransmitió un mensaje — un
anillo de ocho entradas — y reenvía el siguiente reporte de posición simple o de
boya que ve para cada una, diga lo que diga el filtro de tipos, para que el
operador local tenga algo que ubicar del otro extremo de la conversación. Ese
único reporte libera la ranura, que es lo que lo hace un seguimiento y no una
suscripción; un reporte de clima u objeto se retransmite bajo su propio bit de
tipo, por sus propios méritos.

Contadores y razones de descarte
================================

La instantánea ``igate_stats_t`` (``igate_get_stats()``) lleva:

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Contador
     - Significado
   * - ``rxCount``
     - Tramas consideradas para enrutamiento (RF→INET).
   * - ``txCount``
     - Tramas realmente enviadas a APRS-IS como resultado del enrutamiento.
   * - ``msgCount``
     - Paquetes de mensaje APRS (identificador de tipo de dato ``:``) enrutados
       en cualquiera de los dos sentidos — RF→INET por ``igateProcess()``,
       INET→RF por ``igate_note_message_gated()`` desde ``aprs_service.c``. Es la
       cifra ``MSG_CNT`` que informa la respuesta a ``?IGATE?``, así que cuenta
       solo mensajes y no el resto del tráfico enrutado.
   * - ``dupCount``
     - Tramas duplicadas suprimidas.
   * - ``isRxCount``
     - **Todas** las líneas leídas del socket (superconjunto de lo que llega al
       manejador INET→RF).
   * - ``isTxCount``
     - **Todas** las escrituras al socket: tramas enrutadas, mensajes salientes y
       envíos de "baliza a internet" del digi por igual.
   * - ``dropByReason[]``
     - Contadores de descarte por-razón, indexados por ``drop_reason_t``. Las
       etapas RF→INET anteriores cubren ``DROP_TOO_SHORT``, ``DROP_PATH_TOKEN``,
       ``DROP_SAT_NOT_USED``, ``DROP_3RDPARTY_LOOP``, ``DROP_GENERIC_QUERY``,
       ``DROP_TYPE_FILTER``, ``DROP_RANGE_FILTER``, ``DROP_PREFIX_FILTER``,
       ``DROP_BUDLIST`` y ``DROP_TX_FAIL``; el arreglo
       también lleva razones incrementadas en otras partes del firmware (ruta
       de TX de RF, digipeater, decodificación AX.25) — ver ``drop_reason_t``
       en ``components/igate/include/igate.h`` para la lista completa y
       autorizada. No existe una razón genérica/opaca de "otros": cada
       descarte se atribuye a una causa específica y nombrada.
       ``igate_stats_total_drop()`` suma las razones que no son de error;
       ``igate_stats_total_err()`` suma las dos razones de error de
       decodificación/envío por separado.

``igate_note_drop()`` se expone para que otros componentes que comparten los
mismos conceptos de filtrado — actualmente el manejador INET→RF de
``aprs_service.c``, para sus comprobaciones de filtro por tipo y budlist —
contribuyan al mismo desglose por-razón.

Indicador de conectividad
=========================

``igate_is_connected()`` es verdadero mientras el socket TCP de APRS-IS está
abierto, con sesión iniciada y bombeando el lector de líneas RX. El panel
*Network Status* del panel web (la píldora de APRS-IS) lo lee.
