.. _es-query:

=================================
Respondedor de consultas APRS
=================================

El componente ``query`` (``components/query/``) responde las consultas APRS
definidas en el capítulo 15 de APRS101. Reconoce las consultas **generales** (de
difusión) que viajan en el tráfico recibido normal y las consultas **dirigidas**
enviadas a esta estación como mensajes APRS, construye la respuesta que
corresponda y la entrega a la misma plomería de TX por RF/APRS-IS que usa el
motor de mensajería. Todo lo que hace está condicionado por ``g_config.query_en``
y el resto de la página *Query* de la administración web.

Dos puntos de entrada
=====================

* ``query_process(tnc2Line, source)`` — recibe cada línea TNC2 decodificada. No
  hace nada salvo que ``query_en`` esté activo, el interruptor de ``source`` esté
  encendido y el campo de información empiece con ``?``, que es lo que convierte
  a una línea en consulta **general**. Reconocer la palabra clave, aplicar el
  límite de frecuencia y encolar el pedido es todo lo que hace.
* ``query_process_directed(fromCall, toCall, text, tnc2Line, source)`` — la llama
  ``handleIncomingAPRS()`` de ``message.c`` cuando el texto de un mensaje
  dirigido empieza con ``?``, así no se duplica aquí el análisis de
  ``:ADDRESSEE:``. No hace nada salvo que ``query_en`` **y** ``query_directed_en``
  estén activos, el interruptor de ``source`` esté encendido y ``toCall`` coincida
  con ``g_config.aprs_mycall`` (indicativo base, sin distinguir SSID).

Origen y canal
==============

A los dos puntos de entrada se les dice de dónde llegó la consulta —
``QUERY_SRC_RF`` para la radio, ``QUERY_SRC_INET`` para el flujo de APRS-IS — y
ese origen decide dos cosas.

Decide **si la consulta se responde**: ``g_config.query_rf`` y ``query_inet``
nombran un origen, no un destino. Y decide **adónde va la respuesta**: una
pregunta escuchada al aire se responde al aire, una pregunta leída del flujo se
responde hacia APRS-IS. Las respuestas se transmiten por el manejador instalado
con ``query_set_tx_handler()`` — ``aprs_service.c`` reutiliza exactamente el
mismo ``messageTxHandler()`` que le da al motor de mensajería — y la máscara que
recibe lleva siempre exactamente uno de ``MSG_CHANNEL_RF`` /
``MSG_CHANNEL_INET``, el que corresponde al origen.

Que las dos cosas vayan juntas es lo que mantiene al flujo de APRS-IS lejos del
transmisor. ``?APRS?`` es tráfico normal de la red troncal y un IGate ve un
goteo constante; con el interruptor de origen APRS-IS apagado — el valor de
fábrica — nada de eso llega al respondedor, y con el interruptor encendido las
respuestas vuelven hacia APRS-IS en vez de salir al aire.

Una respuesta no se construye aquí, y otra no es un solo paquete, pero ninguna
de las dos se escapa del emparejamiento. ``?APRSO`` vuelve a anunciar los
Objetos/Ítems originados por esta estación: la pata por la que llegó la pregunta
se le entrega al transmisor como **cota superior** y allí se intersecta con la
configuración de "enviar por" de cada elemento, de modo que la ronda puede
quitar una pata que el elemento selecciona pero nunca agregar una que no.
``?APRSM`` reenvía mensajes que esta estación ya le debe al operador que
consulta —un puñado acotado de ellos, véase la tabla más abajo—, enrutados por
los propios indicadores de "enviar por" de la página Message; esas tramas iban a
salir igual según la planificación de reintentos del motor de mensajería, así
que una consulta acelera la entrega de tráfico que la estación ya debía en vez
de crear tráfico nuevo.

El resultado es la propiedad que prometen los interruptores de origen: con
**Responder consultas escuchadas en RF** apagado, ninguna secuencia de líneas de
APRS-IS puede hacer que esta estación active el transmisor.

Dónde se construye la respuesta
===============================

Ninguno de los dos puntos de entrada construye ni transmite nada. Ambos
reconocen la palabra clave, aplican el límite de frecuencia y **registran** el
pedido: su tipo, la estación que consulta y, a lo sumo, un texto — el indicativo
por el que pregunta un ``?APRSH``, o la ruta por la que llegó un ``?APRST``,
leída ahí mismo mientras la línea recibida todavía está a mano. La respuesta la
construye y la pone al aire ``query_service()``, que la tarea del
**planificador de balizas** llama al comienzo de cada pasada; encolar un pedido
también llama a ``beacon_scheduler_wake()``, así que esa pasada ocurre enseguida
y no cuando vence la próxima baliza.

El motivo es el stack. Una respuesta a ``?APRS?`` *es* una baliza: ejecuta
``beacon_build_igate_position_packet()``, varios ``snprintf()`` de newlib con
soporte de punto flotante, ``lat_lon_to_aprs()``, el constructor de path y luego
toda la cadena ``aprs_service_send_tnc2()`` → ``modem_send_tnc2()`` →
``ax25_encode()``, apilando en cada nivel su propio buffer de 300–450 bytes —
justamente el árbol de llamadas para el que ``beacon_scheduler.c`` dimensiona su
stack de 14336 bytes. Las consultas, en cambio, llegan por ``modem_svc`` (RF) e
``igate_task`` (APRS-IS), cuyos stacks son una fracción de aquel. Por eso el
trabajo corre en la tarea cuyo presupuesto lo cubre y no en la que haya recibido
la pregunta, y los constructores pueden crecer sin obligar a revisar de nuevo
esos dos caminos.

De dónde corre se desprenden dos cosas. Responder nunca ocupa una tarea de RX
durante toda una ráfaga de transmisión — la propiedad que ``?APRSO`` siempre
tuvo — y la respuesta hereda el contexto de baliza del planificador, así que
``aprs_service_send_tnc2()`` espera un instante si el anillo de TX de RF está
lleno en vez de descartar la respuesta (ver :ref:`es-beacons`).

La cola guarda ``QUERY_PENDING_MAX`` (8) pedidos y se atiende del más viejo al
más nuevo. Un pedido idéntico a otro que ya está esperando se funde con él —
cada respuesta informa el estado vivo en el momento de enviarse, así que un
duplicado solo pondría dos veces la misma información al aire — y con la cola
llena los pedidos siguientes se descartan con una advertencia en lugar de
desplazar una respuesta que ya se le debe a alguien.

Consultas generales
===================

.. list-table::
   :header-rows: 1
   :widths: 16 16 68

   * - Consulta
     - Habilitación
     - Respuesta
   * - ``?APRS?``
     - ``query_aprs_en``
     - El reporte de posición propio, construido con
       ``beacon_build_igate_position_packet()`` — byte a byte lo que envía la
       baliza de posición del IGate por ese mismo canal.
   * - ``?WX?``
     - ``query_wx_en``
     - El último Reporte Meteorológico en caché, construido con
       ``weather_build_report_packet()``. No se responde si todavía no hay
       lectura en caché o no hay indicativo WX/APRS configurado.
   * - ``?IGATE?``
     - ``query_igate_en``
     - La línea de Capacidades de Estación que define APRS101,
       ``<IGATE,MSG_CNT=n,LOC_CNT=n>``, con las dos cifras que el capítulo 15 les
       asigna. ``MSG_CNT`` es la cuenta acumulada de paquetes de mensaje APRS que
       esta pasarela pasó en cualquiera de los dos sentidos
       (``igate_stats_t::msgCount``, no un total de todo el tráfico enrutado).
       ``LOC_CNT`` es una cifra viva y no un acumulado: la cantidad de estaciones
       distintas en la lista de escuchadas locales, de
       ``lastheard_station_count(true)``, contando solo las filas cuya trama más
       reciente se escuchó al aire. Se ignora en silencio mientras ``igate_en``
       esté apagado.
   * - ``?QRU?``
     - ``query_ext_en``
     - El pase de lista de membresía de grupo que define APRS101 cap.15: un
       paquete de estado que enumera cada Objeto/Item propio que lleve una
       etiqueta QRU no vacía, como pares ``<etiqueta>:<nombre>``, de modo que
       cualquier estación que esté escuchando el pase de lista vea la
       respuesta, no solo la que preguntó. Un resultado vacío igual se
       responde con ``none`` en lugar de quedar en silencio. Se controla con
       el mismo interruptor *Consultas dirigidas extendidas* que el conjunto
       dirigido de más abajo, ya que comparte esa configuración en lugar de
       tener una propia.

Como las respuestas de posición, estado y meteorología reutilizan los
constructores de baliza existentes, una respuesta nunca puede desviarse de lo que
transmiten las balizas periódicas.

Baliza periódica de capacidades
===============================

El capítulo 15 permite que una estación mande su línea de capacidades en
cualquier momento, no solo cuando se la consulta, y muchas pasarelas la balizan
para que los vecinos sepan que existe una pasarela sin tener que preguntar.
*Enviar capacidades periódicamente* (``query_cap_beacon_en``) lo habilita; viene
deshabilitada, y la línea se sigue mandando en respuesta a ``?IGATE?`` en
cualquiera de los dos casos.

La baliza tiene intervalo propio (``query_cap_interval_sec``, acotado al rango
``QUERY_CAP_INTERVAL_S_MIN``..``QUERY_CAP_INTERVAL_S_MAX`` tanto en el manejador
POST como en el lector de JSON) y selección de canal propia
(``query_cap_rf`` / ``query_cap_inet``), en vez de heredar los dos selectores de
fuente: esos dicen dónde se escucha una *pregunta*, mientras que esto keyea el
transmisor con un temporizador propio. También requiere ``igate_en``, porque la
línea anuncia una pasarela.

*Elementos de capacidad adicionales* (``query_cap_extra``) se agrega después de
los dos obligatorios, porque la lista de capacidades es abierta. Al texto se le
quitan CR, LF y los bytes ``,`` y ``>`` que delimitan la propia línea, en el
manejador POST y otra vez al leer la configuración guardada, así que un elemento
escrito en el campo no puede inventar un token ni cerrar la lista antes de
tiempo.

Se arma un paquete por cada pata habilitada, porque el path difiere entre ellas,
y ambos salen del mismo constructor que usa la respuesta a ``?IGATE?``. La
transmisión corre en la tarea del planificador de balizas, junto a los demás
originadores periódicos.

Consultas dirigidas
===================

Se responden solo con ``query_directed_en`` activo. ``?APRSP`` y ``?APRSS`` están
siempre disponibles en ese conjunto; las restantes, de tipo lista, requieren
además *Consultas dirigidas extendidas* (``query_ext_en``).

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Consulta
     - Respuesta
   * - ``?APRSP``
     - Reporte de posición (mismo constructor que ``?APRS?``).
   * - ``?APRSS``
     - Reporte de estado, byte a byte lo que envía la baliza de estado del
       IGate por ese mismo canal — incluido el bloque de locator Maidenhead
       cuando esa opción de la página Station está activa.
   * - ``?APRSD``
     - Las estaciones oídas **directo** (no vía digipeater), como mensaje de
       texto APRS de vuelta al que pregunta. Se descartan indicativos enteros en
       vez de truncar cuando la lista excedería el largo de mensaje al aire.
   * - ``?APRSH <call>``
     - El gráfico de 18 horas de escucha de una estación: ``Hrd: h0 h1 … h17``,
       seis conteos por período separados por ``.``, siendo la hora 0 la hora de
       reloj actual. El histograma vive en ``components/lastheard`` (véanse
       ``lastheard_heard_history()`` y ``LASTHEARD_HEARD_HOURS``). Leer el
       gráfico no es tráfico: la respuesta adelanta el histograma hasta la hora
       de reloj actual, de modo que un silencio transcurrido se ve como el hueco
       que es, pero no se cuenta ninguna hora, y la misma estación consultada
       repetidas veces informa las mismas cifras. Sin argumento de indicativo el
       respondedor contesta ``Usage: ?APRSH <call>`` — la redacción mantiene la
       palabra clave fuera del primer carácter a propósito, ya que un mensaje
       cuyo texto empieza con ``?`` es en sí mismo una consulta dirigida y un
       par que corra un respondedor leería la respuesta como una pregunta
       nueva. Identificar una hora exige un reloj de pared real, así que
       mientras NTP no haya sincronizado desde el arranque — en una estación sin
       ruta hacia un servidor de tiempo, toda su vida en marcha — el gráfico
       lleva en la hora 0 todo lo escuchado de esa estación desde el arranque y
       0 en las demás ranuras. No se pierde nada: esos conteos se conservan
       cuando por fin se ajusta el reloj, la hora 0 pasa a ser la hora en que
       llega la primera trama posterior a la sincronización, y a partir de ahí
       el gráfico envejece con normalidad.
   * - ``?APRSM``
     - Reenvía los mensajes pendientes de esta estación para el operador que
       consulta, hasta ``MSG_QUERY_BURST_MAX`` (3) tramas por consulta. Lo que
       quede en cola conserva su estado de reintento y sale según la
       planificación propia del motor de mensajería, de modo que una sola
       pregunta no puede mantener el transmisor activo durante toda la cola.
   * - ``?APRSO``
     - Reanuncia los Objetos/Ítems originados aquí. Llama a
       ``objitems_request_transmit_all()`` con la pata por la que llegó la
       consulta (``OBJITEM_TX_RF`` u ``OBJITEM_TX_INET``) y, como se atiende
       dentro de la pasada del planificador, los elementos salen más adelante en
       esa misma pasada. La ronda informa el estado actual de cada elemento y no
       toca ningún estado de planificación: no mueve el próximo vencimiento de
       un elemento, no avanza la rampa de decaimiento ni la rotación de rutas
       proporcionales, y no consume una repetición de kill — así que una
       consulta no puede correr el momento en que salen los informes periódicos.
       Compilado sin ``ENABLE_OBJECTS_ITEMS``, responde ``No objects``.
   * - ``?APRST`` / ``?PING?``
     - La ruta que tomó la propia consulta, leída de la línea TNC2 recibida en
       el momento de encolar el pedido. Sin línea disponible responde con ruta
       desconocida.

Las respuestas de tipo lista salen como mensajes de texto APRS dirigidos de vuelta
a la estación que consulta, con el campo de destinatario fijo de 9 caracteres
rellenado con espacios y **sin** número de mensaje — una respuesta a consulta es
informativa, así que no solicita ack. El indicativo de destino usado para el
tráfico de consultas es ``APE32I``, y la ruta es la máscara de ruta de la propia
página IGate.

Limitación de tasa
==================

Tres limitadores evitan que el respondedor se vuelva un problema de tiempo al
aire o la mitad de un bucle de realimentación con otro auto-respondedor:

* **Limitador de difusión** — por *tipo* de consulta **y origen**, como máximo
  una respuesta cada ``g_config.query_min_interval_sec`` (30 s por defecto; el
  mínimo de la página *Query* es 5 s). Al ser por tipo, un canal ocupado
  preguntando ``?APRS?`` no puede suprimir una respuesta a ``?WX?``; al ser
  además por origen, un flujo de APRS-IS hablador no puede gastar el cupo que
  necesita una pregunta escuchada al aire.
* **Limitador de dirigidas por indicativo** — las consultas dirigidas se saltan
  el limitador de difusión (están explícitamente dirigidas a esta estación) pero
  tienen su propio límite, más estricto, de
  ``QUERY_DIRECTED_MIN_INTERVAL_SEC`` (5 s) por indicativo que pregunta,
  rastreado en una tabla fija de ``QUERY_DIRECTED_TRACK_MAX`` (8) entradas. Las
  consultas dirigidas son tráfico raro, así que una tabla llena simplemente
  recicla la entrada más antigua.
* **Techo global de dirigidas** — como máximo una respuesta dirigida por origen
  cada ``QUERY_DIRECTED_GLOBAL_MIN_INTERVAL_SEC`` (10 s), sin importar cuántos
  indicativos pregunten. La tabla por indicativo es un límite de equidad y no
  puede ser por sí sola un límite de tiempo al aire: el indicativo con el que se
  indexa lo elige quien pregunta y, en la pata de APRS-IS, no está autenticado en
  absoluto, así que rotar más indicativos de los que caben en la tabla reciclaría
  entradas y compraría cupo nuevo cada vez. Este techo se indexa por algo que
  quien pregunta no controla, así que rotar indicativos no compra nada. Una
  ráfaga de *N* consultas dirigidas desde *N* indicativos distintos produce
  entonces como mucho una respuesta por intervalo.

Los dos límites de dirigidas se aplican en serie y un pedido tiene que superar
ambos. El techo se comprueba primero y se estampa último, de modo que un pedido
que el origen todavía no puede responder tampoco gasta el cupo propio del
indicativo que pregunta.

Los limitadores corren en la tarea que recibió la consulta, y hay dos: la tarea
del módem para RF y la tarea del IGate para APRS-IS. Las dos tablas de marcas de
tiempo se indexan por origen, así que cada tarea sólo alcanza sus propias
entradas y no necesitan lock. La tabla por indicativo se indexa sólo por el
indicativo que pregunta, así que ambas tareas alcanzan las mismas ocho ranuras;
se toma bajo su propio mutex durante el recorrido, que son unas pocas
comparaciones de cadenas sin E/S. Una consulta que no puede tomar ese lock se
trata como limitada por tasa y queda sin responder, lo que se equivoca hacia no
transmitir.

Configuración
=============

La página *Query* (``page_query.c``) expone, en orden: **Habilitar**,
**Responder consultas escuchadas en RF**, **Responder consultas recibidas de
APRS-IS**, los tres interruptores de consulta general
(``?APRS?``, ``?WX?``, ``?IGATE?`` — los dos últimos solo aparecen en
compilaciones que incluyen las funciones de meteorología e IGate), **Habilitar
consultas dirigidas**, **Consultas dirigidas extendidas**, y el **Intervalo
mínimo de respuesta** en segundos.

.. list-table::
   :header-rows: 1
   :widths: 30 20 50

   * - Clave JSON
     - Por defecto
     - Campo
   * - ``queryEn``
     - ``false``
     - habilitación maestra (opt-in, como la mensajería)
   * - ``queryRf`` / ``queryInet``
     - ``true`` / ``false``
     - qué origen se responde: consultas escuchadas en RF / leídas del flujo de
       APRS-IS. El origen APRS-IS está apagado por defecto, así que el tráfico
       de la red troncal no puede activar el transmisor tal como viene.
   * - ``queryAprsEn`` / ``queryWxEn`` / ``queryIgateEn``
     - ``true``
     - habilitaciones por consulta general
   * - ``queryDirectedEn`` / ``queryExtEn``
     - ``true``
     - conjunto dirigido / conjunto dirigido extendido
   * - ``queryMinInterval``
     - ``30``
     - límite de tasa de difusión, en segundos

Toda la página está condicionada por el interruptor de compilación
``ENABLE_QUERY``.

.. note::

   ``query_init()`` debe ejecutarse después de ``message_init()``/``igate_start()``
   y antes de que ``aprs_service_start()`` termine de cablear la cadena de
   despacho de RX, que es exactamente donde lo llama ``aprs_service.c``.
