.. _es-beacons:

=========================
Balizas y el planificador
=========================

Las balizas de la propia estación son lo que hace que la estación aparezca en
aprs.fi. El IGate y el digipeater por sí solos únicamente retransmiten el tráfico
que oyen; nunca anuncian su propia posición. Existen tres balizas lógicas —
**tracker**, **igate** y **digi** — cada una con sus propias banderas de
habilitación, intervalo, coordenadas, símbolo, comentario y enrutamiento
RF/INET, guardadas por su respectiva página de la administración web
(``g_config.trk_*``, ``g_config.igate_*``, ``g_config.digi_*``).

El planificador de balizas compartido
=====================================

Revisiones anteriores ejecutaban las balizas de tracker, igate y digi, el
informe meteorológico y los boletines cada uno en **su propia tarea FreeRTOS**.
Cada una de esas tareas hacía lo mismo — dormir, despertar, construir un paquete,
recorrer la cadena de TX TNC2/AX.25 compartida (cargada de operaciones en coma
flotante), dormir otra vez — y por tanto cada una tenía que arrastrar una pila
grande (10–14 KB) dimensionada para ese árbol de llamadas, aunque casi nunca se
ejecutan a la vez y el módem semidúplex serializa sus transmisiones de todos
modos.

El componente ``beacon_scheduler`` **fusiona esas cinco tareas en una**. En cada
pasada llama a la función "service" de cada subsistema (``beacon_service()``,
``weather_beacon_service()``, ``bulletins_service()``, y los servicios de
objetos/ítems y telemetría), cada una de las cuales transmite lo que toca y
reporta cuántos segundos faltan hasta que necesite servicio de nuevo; el
planificador entonces duerme hasta el más próximo de ellos. Los subsistemas
conservan sus banderas de habilitación e intervalos independientes — solo se
comparte la tarea (y su pila).

Efecto neto: cinco pilas (~61 KB en total) pasan a ser una (~14 KB), liberando
~46 KB de heap interno en esta compilación sin PSRAM.

Funciones de servicio
=====================

Cada subsistema expone una ``*_service()`` que:

#. Comprueba sus propias banderas de habilitación. Una baliza deshabilitada es
   un no-op barato que devuelve un intervalo de re-comprobación corto, de modo
   que activarla en la web sigue surtiendo efecto sin reiniciar.
#. Transmite cualquier baliza que esté actualmente pendiente, por RF
   (``aprs_service_send_tnc2()``) y/o a APRS-IS (``igate_send_raw()``) según las
   propias banderas ``loc2rf`` / ``loc2inet`` de la página.
#. Devuelve el número de segundos (siempre ≥ 1) hasta el próximo evento
   pendiente más cercano.

``beacon_service()`` gestiona las tres balizas de posición en una sola pasada.

Fluctuación anti-colisión
=========================

La planificación de balizas es por lo demás determinista, así que múltiples
estaciones que eligen todas el mismo intervalo redondo (p. ej. WX cada 600 s)
tienden a sincronizarse en fase y colisionar en un canal RF compartido — una
patología APRS clásica. ``beacon_scheduler_jitter()`` dispersa el momento
pendiente de una baliza en ± un pequeño porcentaje (sembrado con ``esp_random()``,
uniforme), de modo que las balizas de la propia estación se descorrelacionan
tanto entre sí como de las estaciones vecinas, y las balizas pendientes
simultáneamente derivan separándose con el tiempo. La fluctuación se aplica al
intervalo usado para calcular la marca de tiempo del **próximo pendiente** de una
baliza — no meramente al sueño del planificador, que dejaría la rejilla de
tiempos pendientes subyacente determinista y le permitiría re-sincronizarse en el
siguiente ciclo.

Escalonamiento de TX dentro de una pasada
=========================================

Cuando varias balizas de la propia estación caen pendientes juntas, se sirven
consecutivamente en la tarea del planificador, mucho más rápido de lo que una
trama a 1200 Bd libera el aire. Con el valor de fábrica *TX buffers = 1*, la 2.ª
y 3.ª tramas chocarían con un anillo de TX de RF lleno y se descartarían. Para
evitarlo, la tarea del planificador se registra a sí misma vía
``aprs_service_set_beacon_context()``, y **solo en esa tarea** se permite que
``aprs_service_send_tnc2()`` espere brevemente (hasta 4 s) a que el anillo baje
del límite antes de rendirse — así cada baliza pendiente acaba activando la
radio, mientras todos los demás llamadores (RX/digipeat, INET→RF, TX de
mensajes) mantienen el comportamiento no bloqueante de descartar-si-lleno y una
pata de RF ocupada nunca detiene la decodificación de RX ni el socket de APRS-IS.

Las marcas de tiempo son UTC
============================

Las marcas de tiempo de las balizas son zulú/UTC (``051200z``) según la
especificación APRS — por lo que ``time_sync.c`` fija el reloj del sistema a
``TZ=UTC0`` independientemente de ``g_config.timeZone`` (la zona configurada es
solo para visualización).
