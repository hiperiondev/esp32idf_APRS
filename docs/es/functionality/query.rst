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

* ``query_process(tnc2Line)`` — recibe cada línea TNC2 decodificada. No hace nada
  salvo que ``query_en`` esté activo y el campo de información empiece con ``?``,
  que es lo que convierte a una línea en consulta **general**.
* ``query_process_directed(fromCall, toCall, text, tnc2Line)`` — la llama
  ``handleIncomingAPRS()`` de ``message.c`` cuando el texto de un mensaje
  dirigido empieza con ``?``, así no se duplica aquí el análisis de
  ``:ADDRESSEE:``. No hace nada salvo que ``query_en`` **y** ``query_directed_en``
  estén activos y ``toCall`` coincida con ``g_config.aprs_mycall`` (indicativo
  base, sin distinguir SSID).

Las respuestas se transmiten por el manejador instalado con
``query_set_tx_handler()``. ``aprs_service.c`` reutiliza exactamente el mismo
``messageTxHandler()`` que le da al motor de mensajería, así que los bits de
enrutado son el par conocido ``MSG_CHANNEL_RF`` / ``MSG_CHANNEL_INET``,
seleccionado por ``g_config.query_rf`` / ``query_inet``.

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
       ``beacon_build_igate_position_packet()`` — byte a byte lo que habría
       enviado la baliza de posición del IGate.
   * - ``?WX?``
     - ``query_wx_en``
     - El último Reporte Meteorológico en caché, construido con
       ``weather_build_report_packet()``. No se responde si todavía no hay
       lectura en caché o no hay indicativo WX/APRS configurado.
   * - ``?IGATE?``
     - ``query_igate_en``
     - La línea de Capacidades de Estación que define APRS101,
       ``<IGATE,MSG_CNT=n,LOC_CNT=n>``, construida con los mismos contadores de
       ``igate_get_stats()`` que lee el panel (``MSG_CNT`` = ``txCount``,
       ``LOC_CNT`` = ``rxCount``). Se ignora en silencio mientras ``igate_en``
       esté apagado.

Como las respuestas de posición, estado y meteorología reutilizan los
constructores de baliza existentes, una respuesta nunca puede desviarse de lo que
transmiten las balizas periódicas.

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
     - Reporte de estado, byte a byte lo que enviaría la baliza de estado del
       IGate — incluido el bloque de locator Maidenhead cuando esa opción de la
       página Station está activa.
   * - ``?APRSD``
     - Las estaciones oídas **directo** (no vía digipeater), como mensaje de
       texto APRS de vuelta al que pregunta. Se descartan indicativos enteros en
       vez de truncar cuando la lista excedería el largo de mensaje al aire.
   * - ``?APRSH <call>``
     - El gráfico de 18 horas de escucha de una estación: ``Hrd: h0 h1 … h17``,
       seis conteos por período separados por ``.``, siendo la hora 0 la hora de
       reloj actual. El histograma vive en ``components/lastheard`` (véanse
       ``lastheard_heard_history()`` y ``LASTHEARD_HEARD_HOURS``). Sin argumento
       de indicativo, el respondedor lo indica.
   * - ``?APRSM``
     - Reenvía los mensajes pendientes de esta estación para el operador que
       consulta.
   * - ``?APRSO``
     - Reanuncia los Objetos/Ítems originados aquí. Llama a
       ``objitems_request_transmit_all()``, así que los elementos salen desde la
       tarea del **planificador de balizas** y no desde la tarea de RX —
       responder una consulta nunca ocupa RX durante toda una ráfaga de
       transmisión. Compilado sin ``ENABLE_OBJECTS_ITEMS``, responde
       ``No objects``.
   * - ``?APRST`` / ``?PING?``
     - La ruta que tomó la propia consulta, reconstruida desde la línea TNC2
       recibida. Sin línea disponible responde con ruta desconocida.

Las respuestas de tipo lista salen como mensajes de texto APRS dirigidos de vuelta
a la estación que consulta, con el campo de destinatario fijo de 9 caracteres
rellenado con espacios y **sin** número de mensaje — una respuesta a consulta es
informativa, así que no solicita ack. El indicativo de destino usado para el
tráfico de consultas es ``APE32L``, y la ruta es la máscara de ruta de la propia
página IGate.

Limitación de tasa
==================

Dos limitadores independientes evitan que el respondedor se vuelva un problema de
tiempo al aire o la mitad de un bucle de realimentación con otro
auto-respondedor:

* **Limitador de difusión** — por *tipo* de consulta, como máximo una respuesta
  cada ``g_config.query_min_interval_sec`` (30 s por defecto; el mínimo de la
  página *Query* es 5 s). Al ser por tipo, un canal ocupado preguntando
  ``?APRS?`` no puede suprimir una respuesta a ``?WX?``.
* **Limitador de dirigidas** — las consultas dirigidas se saltan el limitador de
  difusión (están explícitamente dirigidas a esta estación) pero tienen su propio
  límite **por origen**, más estricto, de ``QUERY_DIRECTED_MIN_INTERVAL_SEC``
  (5 s), rastreado en una tabla fija de ``QUERY_DIRECTED_TRACK_MAX`` (8) entradas.
  Las consultas dirigidas son tráfico raro, así que una tabla llena simplemente
  recicla el origen más antiguo.

Configuración
=============

La página *Query* (``page_query.c``) expone, en orden: **Habilitar**, **Enviar
por RF**, **Enviar por Internet**, los tres interruptores de consulta general
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
     - responder por RF / hacia APRS-IS. La pata de Internet está apagada por
       defecto para que la estación no responda hacia APRS-IS sin pedirlo.
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
