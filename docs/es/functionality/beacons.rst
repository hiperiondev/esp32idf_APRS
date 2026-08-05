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

Las respuestas a consultas viajan en la misma tarea
===================================================

Una respuesta a una consulta APRS es una baliza en todo salvo en su disparador:
``?APRS?`` y ``?APRSP`` ejecutan el constructor de posición, ``?APRSS`` el de
estado, ``?WX?`` el de meteorología, y todas terminan en la misma cadena de TX
TNC2/AX.25 cargada de punto flotante. Por eso también se responden aquí.
``query_process()`` y ``query_process_directed()``, que corren en las tareas que
reciben tráfico, solo encolan el pedido; el planificador llama a
``query_service()`` al comienzo de cada pasada y hace la construcción y la
transmisión sobre la pila dimensionada para ese árbol de llamadas (ver
:ref:`es-query`).

Como una consulta no es periódica, esperar a que venza la próxima baliza se
notaría como una respuesta tardía. Por eso encolar un pedido llama a
``beacon_scheduler_wake()``, que acorta el sueño del planificador — ese sueño es
un ``ulTaskNotifyTake()`` con el próximo vencimiento como timeout y no un retardo
simple. Un despertar levantado mientras la tarea está en plena pasada queda
retenido por FreeRTOS y lo toma el siguiente sueño, así que nada encolado se
duerme sin atender.

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

Extensiones de datos (PHG / RNG / DFS)
======================================

La baliza de posición del IGate puede llevar una de las tres extensiones de
datos APRS de estación fija en la ranura de 7 bytes que sigue al código de
símbolo — la misma ranura que una estación en movimiento usa para rumbo y
velocidad, y por eso solo se emite una de ellas. *Habilitar extensión de datos*
en la página IGate abre la ranura, y *Tipo de extensión* elige cuál de las tres
la llena:

.. list-table::
   :header-rows: 1
   :widths: 12 18 70

   * - Tipo
     - Al aire
     - Significado
   * - PHG
     - ``PHG5132``
     - Potencia de transmisión, altura de antena sobre el terreno promedio,
       ganancia y directividad. El software receptor dibuja la cobertura
       estimada resultante como un círculo (o un lóbulo, con antena
       direccional).
   * - RNG
     - ``RNG0025``
     - Un único alcance de radio omnidireccional precalculado en millas
       terrestres, para un operador que ya conoce su radio de cobertura real y
       prefiere declararlo antes que dejar que se infiera del PHG.
   * - DFS
     - ``DFS3364``
     - Intensidad de señal omni-DF: los mismos códigos de altura/ganancia/
       directividad que PHG, pero informando la intensidad de señal *recibida*
       en puntos S en lugar de la potencia transmitida. Una intensidad de 0
       significa que esta estación **no** oye la señal, y el software de
       graficado lo representa como un círculo de exclusión en vez de uno de
       cobertura.

PHG usa los cuatro subcampos, DFS todos menos la potencia de transmisión, y RNG
ninguno; la página deshabilita las entradas que el tipo seleccionado no usa.
Como un control deshabilitado no se envía en el POST, los valores guardados del
*otro* tipo sobreviven al ir y volver entre ellos.

Habilitar cualquier extensión fuerza el formato de posición sin comprimir. El
formato comprimido no tiene sitio para la ranura de 7 bytes (APRS101 cap.9 dice
que no admite PHG), así que emitir esos bytes dentro de un reporte comprimido
sería simplemente dato erróneo, y descartar la extensión para conservar la
compresión perdería en silencio un campo que el operador habilitó
explícitamente.

La capacidad de mensajería va en el identificador de tipo de datos
==================================================================

El primer byte del campo de información de un reporte de posición declara dos
cosas a la vez (APRS101 cap.6): si sigue una marca de tiempo, y si la estación
puede aceptar mensajes APRS.

.. list-table::
   :header-rows: 1
   :widths: 25 25 25 25

   * - Marca de tiempo
     - ``msg_enable`` apagado
     - ``msg_enable`` encendido
     - Significado
   * - No
     - ``!``
     - ``=``
     - Posición, sin marca de tiempo
   * - Sí
     - ``/``
     - ``@``
     - Posición con marca de tiempo

La distinción no es decorativa: es como un cliente receptor decide si le ofrece
a su operador la acción *enviar mensaje* para esa estación. Las radios Kenwood
TH-D7/D700/D710, APRSISCE/32, Xastir, YAAC y aprs.fi leen todas este bit, y una
estación que declara no aceptar mensajes se muestra sin ninguna vía de
respuesta.

Esta estación corre un motor de mensajería completo, responde consultas
dirigidas y acusa recibo de los mensajes que recibe, así que con *Habilitar
mensajería* activo las tres balizas de posición lo declaran. El identificador se
elige en ``buildPositionPacket()`` desde la misma copia bajo lock de la que
salen los demás campos de la baliza. Los objetos e ítems no se ven afectados —
llevan sus propios identificadores ``;`` y ``)`` — y Mic-E tiene su propio
formato fijo.

Ambigüedad de posición
======================

*Ambigüedad de posición*, en la página Estación, es de toda la estación: se
aplica a las tres balizas de posición, porque con cuánta precisión una estación
está dispuesta a decir dónde está es una propiedad de la estación y no de una
baliza concreta. Los niveles 0–4 blanquean los dígitos de minuto menos
significativos al aire (APRS101 cap.6) — el punto decimal, el carácter de
hemisferio y los anchos de campo nunca cambian, que es lo que mantiene el
reporte analizable:

.. list-table::
   :header-rows: 1
   :widths: 10 25 25 40

   * - Nivel
     - Latitud
     - Longitud
     - Precisión
   * - 0
     - ``4903.50N``
     - ``07201.75W``
     - Centésimas de minuto (total).
   * - 1
     - ``4903.5 N``
     - ``07201.7 W``
     - Al 1/10 de minuto.
   * - 2
     - ``4903.  N``
     - ``07201.  W``
     - Al minuto.
   * - 3
     - ``490 .  N``
     - ``0720 .  W``
     - A los 10 minutos.
   * - 4
     - ``49  .  N``
     - ``072  .  W``
     - Al grado.

Los dígitos se blanquean, nunca se redondean fuera, igual que los decodificadores
de referencia que leen un dígito blanqueado como "desconocido". El acarreo de
redondeo se aplica primero, así que una coordenada que redondea al grado
siguiente se informa en ese grado y no en el anterior.

Un nivel distinto de cero también fuerza el formato sin comprimir, por el mismo
tipo de razón que una extensión de datos: el formato comprimido no tiene dígitos
decimales que blanquear, así que respetar una marca de *comprimido* junto con la
ambigüedad transmitiría la posición exacta que el operador pidió ocultar. Mic-E,
en cambio, lleva la ambigüedad de forma nativa y no necesita ese repliegue.

Localizador Maidenhead en los reportes de estado
================================================

*Localizador Maidenhead en los reportes de estado*, también de toda la estación
en la página Estación, antepone a cada reporte de estado el localizador de la
posición propia de esa baliza, su byte de tabla de símbolo y su código de
símbolo — la forma ``>IO91SX/G`` de APRS101 cap.16 — seguido de un espacio y el
texto de estado configurado. Los receptores que entienden la forma sitúan la
estación solo con el localizador; el resto muestran todo como texto de estado. El
texto configurado nunca se interpreta.

Las marcas de tiempo son UTC
============================

Las marcas de tiempo de las balizas son zulú/UTC (``051200z``) según la
especificación APRS — por lo que ``time_sync.c`` fija el reloj del sistema a
``TZ=UTC0``. No existe ningún desfase de hora local en el firmware.
