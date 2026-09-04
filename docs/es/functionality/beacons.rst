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

Una baliza habilitada en ambas patas se arma **dos veces**, una por pata, y las
dos líneas difieren exactamente en un punto: la ruta. La transmisión de radio
lleva la selección de digipetidores hecha en la página de esa baliza; la
transmisión por APRS-IS lleva ``TCPIP*`` y nada más, que es lo que `la guía de
conexión de aprs-is.net <https://www.aprs-is.net/Connecting.aspx>`_ exige de un
paquete originado en el cliente: un alias ``WIDEn-N`` enviado allí describiría
repetidores por los que el paquete nunca pasó. Ambas líneas salen del mismo
constructor y del mismo snapshot de configuración tomado bajo lock, así que
nada más puede divergir entre ellas, y cada pata se registra por lo que
realmente hizo en vez de con una sola línea incondicional.

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

Extensiones de datos (PHG / RNG / DFS / DF)
===========================================

Las balizas de posición del IGate y del digipetidor pueden llevar cada una
una de las extensiones de datos APRS de estación fija en la ranura de 7 bytes
que sigue al código de símbolo — la misma ranura que una estación en movimiento
usa para rumbo y velocidad, y por eso solo se emite una de ellas. *Habilitar
extensión de datos* en la página IGate o Digi abre la ranura de ese rol, y
*Tipo de extensión* elige cuál la llena. Cada rol guarda sus propios ajustes,
así que un IGate y un digipetidor sobre la misma estación con distintos SSID
pueden publicar coberturas diferentes:

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
   * - DF
     - ``000/000/270/735``
     - El reporte DF del cap.8 de APRS101: la marcación hacia una señal,
       seguida del trío NRQ que la califica — detecciones por período de muestreo
       (``N``, donde 0 dice que el trío no tiene significado), el código de
       alcance (``R``, que representa 2\ :sup:`R` millas) y la precisión de la
       marcación (``Q``, siendo 9 mejor que un grado). Es la forma que el
       capítulo describe para una estación de radiogoniometría que informa su
       propia marcación. Estas balizas son de estación fija y no tienen fuente de
       rumbo ni velocidad, así que el par inicial es el ``000/000`` que la
       especificación usa para decir exactamente eso. El mismo codificador arma
       el token para objetos e ítems, donde informa una marcación tomada sobre
       otra estación.

El reporte DF es la única extensión con un requisito de símbolo propio. Su token
mide quince bytes donde la ranura tiene siete, y el capítulo 8 establece que la
marcación y el NRQ sólo tienen sentido cuando el reporte lleva el símbolo DF —
tabla de símbolos ``/`` y código de símbolo ``\``. Un receptor que ve cualquier
otro símbolo no tiene motivo para mirar más allá de la ranura: lee ``000/000``
como un par rumbo/velocidad común y toma ``/270/735`` como los primeros ocho
caracteres del campo de comentario. Por eso DF se transmite únicamente con ese
par de símbolo; con cualquier otro la ranura queda vacía, el log nombra el
símbolo que suprimió el reporte y la página muestra una nota junto al tipo
de extensión apenas los dos no coinciden. La misma regla vale para objetos e
ítems y en recepción: una continuación DF entrante siempre se saltea para que
nunca caiga en el comentario, pero su marcación sólo se lee cuando el símbolo
del emisor es el símbolo DF.

PHG usa los cuatro subcampos, DFS todos menos la potencia de transmisión, y RNG
y DF ninguno — DF tiene en cambio sus propias entradas de marcación y NRQ; la
página deshabilita las entradas que el tipo seleccionado no usa. Como un control
deshabilitado no se envía en el POST, los valores guardados de los *otros* tipos
sobreviven al ir y volver entre ellos.

Habilitar PHG, DFS o un reporte DF que el símbolo permita fuerza el formato de
posición sin comprimir. El formato
comprimido no tiene sitio para la ranura de 7 bytes (APRS101 cap.9 dice que no
admite PHG), y un reporte DF es todavía más ancho que la ranura, así que emitir
esos bytes dentro de un reporte comprimido sería simplemente dato erróneo, y
descartar la extensión para conservar la compresión perdería un campo que el
operador habilitó explícitamente. El firmware deja un warning en el log nombrando
cuál de las dos opciones cedió, en vez de dejarlo para que se descubra al aire.
Un reporte DF que el símbolo suprime no pone bytes en la ranura, así que no le
cuesta la compresión a la baliza.

La ambigüedad de posición es otra cosa y viaja con cualquiera de ellas: blanquea
dígitos decimales del formato sin comprimir, que conserva su ranura de
extensión, así que ninguna de las dos opciones tiene que ceder ante la otra.

RNG es la excepción, porque el formato comprimido lleva un alcance de radio
precalculado de forma nativa: los dos bytes ``cs`` contienen ``{`` seguido de un
dígito de alcance, que se decodifica como ``2 × 1,08^s`` millas. Una baliza con
RNG seleccionado y la compresión marcada sigue por tanto comprimida, con el
alcance plegado en esos dos bytes y sin ningún token ``RNGrrrr`` en el campo de
información. La forma comprimida cuantiza el alcance en pasos de alrededor del
8 por ciento y arranca en un piso de 2 millas, así que un alcance configurado
por debajo se transmite como 2.

PHG es la extensión que se espera de un digipetidor. El capítulo 7 la presenta
como la forma en que una estación declara el círculo de cobertura con el que sus
vecinos razonan al elegir una ruta, y los clientes de mapa dibujan ese círculo
para los digipetidores antes que para nadie — por eso la página Digi ofrece los
mismos cuatro tipos que la página IGate, sobre sus propios ajustes, en vez de
dejar vacía la ranura de ese rol.

La baliza Tracker lleva PHG y nada más, que se activa con *Incluir extensión de
datos PHG* en su propia página. Ahí no hay subcampos que completar: los cuatro
valores son los datos de antena de la propia estación, que se editan una sola
vez en el bloque PHG de la página Estación. Un tracker que baliza en Mic-E
conserva el token — Mic-E no tiene ranura de 7 bytes después del código de
símbolo, pero APRS 1.2 establece que su campo de texto puede llevar un campo de
comentario de posición normal, PHG incluido, y ahí es donde va: detrás del
bloque de frecuencia, para que una radio siga sintonizando automáticamente a
partir de los primeros bytes, y delante del comentario del operador.

Posición GPS en vivo y rumbo/velocidad
========================================

La posición de cada baliza toma por defecto la latitud/longitud/altitud fija
guardada en su propia página — el único modo que ofrecen las balizas de IGate
y Digipeater. La página de la baliza Tracker suma un interruptor más, *Usar
posición GPS en vivo* (``g_config.trk_use_live_gps``), independiente de la
casilla *Usar GPS* descrita arriba: mientras *Usar GPS* copia la posición del
receptor GNSS a los campos fijos una sola vez, al guardar, *Usar posición GPS
en vivo* hace que ``trackerBeaconService()`` (``main/beacon.c``) vuelva a leer
el receptor — mediante ``gps_snapshot()`` (``main/gps.c``) — en cada
transmisión, y transmita esa latitud/longitud/altitud en vivo en lugar de los
valores fijos.

Una posición en vivo solo se usa cuando está realmente vigente:
``gps_snapshot()`` debe reportar ``valid`` y ``has_position`` (el receptor
tiene una solución RMC activa, no vencida más allá de ``GPS_LINK_TIMEOUT_S``).
Cualquier cosa por debajo de eso — receptor apagado, todavía adquiriendo
posición, o enlace silencioso — deja los parámetros de la baliza exactamente
como se leyeron de ``g_config.trk_lat``/``trk_lon``/``trk_alt``, así que el
Tracker sigue balizando su posición fija de respaldo en vez de omitir una
transmisión o enviar una posición vencida. La altitud solo se reemplaza cuando
el propio indicador ``has_altitude`` del receptor está activo, ya que una
posición 2D no trae altitud que dar.

Cuando el receptor también reporta rumbo y velocidad para esa misma lectura
(``has_course`` y ``has_speed`` activos ambos), la baliza Tracker los lleva
también, según el formato elegido:

* **Sin comprimir** — la extensión de datos estándar ``CSE/SPD``
  (``"%03u/%03u"``, grados verdaderos y nudos), en la misma ranura de 7 bytes
  que ocupan PHG/RNG/DFS/DF. Una extensión PHG activa sigue teniendo prioridad
  sobre CSE/SPD por esa ranura, la misma precedencia que Objetos/Ítems dan a
  PHG sobre el CSE/SPD propio de un elemento en movimiento.
* **Comprimido** — plegado en la ranura de dos bytes propia del campo
  comprimido (``cs/T``) mediante ``aprs_compressed_cs_from_course_speed()``,
  el mismo codificador que usan Objetos/Ítems para un elemento en movimiento.
  Un alcance de radio precalculado sigue teniendo prioridad sobre
  rumbo/velocidad por esa ranura, porque RNG es un ajuste que el operador
  activó explícitamente y no tiene otro lugar adonde ir; rumbo/velocidad cede
  ante él igual que la altitud.
* **Mic-E** — el par real de rumbo/velocidad, en lugar del "desconocido"
  ``000/000`` que envía toda baliza de posición fija (y una posición en vivo
  sin rumbo/velocidad reportados en ese ciclo).

La velocidad se convierte de los km/h del receptor (``gps_data_t::speed_kmh``)
a los nudos en que están definidos todos estos campos, usando el mismo factor
1.852 que ``gps.c`` ya aplica en el sentido inverso al interpretar una
velocidad NMEA en nudos hacia el snapshot.

Smart Beaconing
================

*SmartBeaconing* (``g_config.trk_sb_enable``, página Tracker) acorta o
alarga automáticamente el intervalo de la baliza Tracker según la
velocidad actual, y fuerza una baliza anticipada ante un cambio de rumbo
suficientemente pronunciado, siguiendo el algoritmo estándar publicado por
Hans-Gunnar Lundahl e implementado por HamHUD y la mayoría de los
trackers modernos. Solo tiene efecto junto con *Usar posición GPS en vivo*
mencionado arriba: SmartBeaconing no tiene velocidad ni rumbo con los que
trabajar sin una posición en vivo, y la baliza Tracker vuelve al intervalo
fijo ``trk_interval`` cada vez que no hay una disponible - el mismo
respaldo que *Usar posición GPS en vivo* ya aplica a la posición.

Tasa dinámica
-------------

Entre los dos umbrales de velocidad siguientes, ``smartBeaconingInterval()``
(``main/beacon.c``) interpola linealmente el intervalo de la baliza desde
el valor lento hasta el valor rápido a medida que aumenta la velocidad:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Ajuste
     - Significado
   * - Intervalo lento
     - Período de baliza usado en o por debajo de *Velocidad baja* - la
       cadencia para una estación detenida o de movimiento lento
       (valor de fábrica: 600 s).
   * - Intervalo rápido
     - Período de baliza usado en o por encima de *Velocidad alta*
       (valor de fábrica: 60 s).
   * - Velocidad baja / Velocidad alta
     - Los dos umbrales de velocidad, km/h, que acotan la interpolación.
       Una posición en vivo sin rumbo/velocidad reportados en ese ciclo
       (todavía adquiriendo, o un receptor que omite el rumbo a velocidad
       cero) se trata como la tasa lenta.

Corner-pegging
---------------

Independientemente de la tasa anterior, ``trackerBeaconService()`` sondea
la posición en vivo cada pocos segundos en busca de un cambio de rumbo
respecto a la última baliza transmitida y, en cuanto uno supera el umbral
de giro, adelanta la próxima transmisión a ese ciclo en lugar de esperar a
que transcurra el intervalo anterior - el comportamiento clásico de
SmartBeaconing de un tracker que reporta visiblemente el giro que está
tomando, en lugar de reportar una posición recién más adelante en la nueva
calle. El umbral en sí se ensancha a baja velocidad y se estrecha a alta
velocidad:

.. code-block:: text

   umbral (grados) = Ángulo de giro + Pendiente de giro / velocidad (km/h)

de modo que una estación rápida activa el corte con un cambio de rumbo
mucho menor que una lenta - *Pendiente de giro* es lo que hace inofensivo
el ruido de rumbo habitual a baja velocidad (maniobras en un
estacionamiento, una lectura de rumbo GPS que fluctúa estando casi
detenida) sin por eso embotar la detección de giros a velocidad. *Tiempo
mínimo entre giros* además rechaza una segunda baliza por corner-pegging
dentro de esa cantidad de segundos desde la última, sin importar cuán
pronunciado parezca el nuevo cambio de rumbo, de modo que una estación
lenta o momentáneamente detenida con una lectura de rumbo ruidosa no pueda
volver a disparar corner-pegging en cada ciclo.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Ajuste
     - Significado
   * - Ángulo de giro
     - Cambio de rumbo mínimo, en grados, que activa un corte de esquina en
       o por encima del umbral de velocidad alta (valor de fábrica: 25°).
   * - Pendiente de giro
     - Grados sumados a *Ángulo de giro*, escalados inversamente con la
       velocidad actual, ensanchando el umbral efectivo a baja velocidad
       (valor de fábrica: 255°).
   * - Tiempo mínimo entre giros
     - Intervalo mínimo, en segundos, permitido entre dos balizas por
       corner-pegging (valor de fábrica: 15 s).

Una transmisión que sale por cualquier motivo - el intervalo de tasa, o
corner-pegging - renueva la referencia de corner-pegging, de modo que una
baliza que acaba de reportar un rumbo no vuelva a activar el corte
inmediatamente sobre ese mismo rumbo.

Altitud comprimida
==================

Un reporte de posición comprimido no tiene token de comentario para la altitud,
pero no lo necesita. Los mismos dos bytes ``cs`` que llevan curso/velocidad o un
alcance de radio llevan una altitud cuando el byte de tipo declara GGA como
fuente NMEA, que se decodifica como ``1,002^(c × 91 + s)`` pies. Una baliza con
*Incluir altitud* y *Comprimir posición* marcadas usa esa forma, y el token
``/A=`` se omite del comentario, así que la altitud se enuncia una sola vez —
nueve bytes de comentario ahorrados a cambio de nada, con un paso de alrededor
del 0,2 %.

Los dos bytes llevan una sola cosa por vez, así que una baliza que además tenga
RNG seleccionado se los cede al alcance: el alcance no tiene otro lugar donde
ir, mientras que la altitud todavía cuenta con ``/A=`` como respaldo, y eso es
lo que una baliza así emite.

Superposiciones numéricas en un reporte comprimido
==================================================

Una superposición de símbolo se escribe en la posición de tabla del par de
símbolo, y APRS 1.2 cap.21 admite que sea una letra ``A``-``Z`` o un dígito
``0``-``9``: las numéricas son la forma en que un digipetidor anuncia su
política de ruteo, el "círculo numerado" de la tabla alternativa. El formato
comprimido no puede llevar el dígito en sí: el primer byte de un campo de
posición es justamente lo que le dice a un receptor cuál de los dos formatos
está leyendo, y un dígito inicial significa no comprimido.

Por eso una superposición numérica viaja como la letra minúscula
correspondiente, ``a`` para ``0`` hasta ``j`` para ``9``, y el receptor la
vuelve a mapear al dígito. El firmware aplica ese mapeo al construir el campo,
así que la superposición se configura una sola vez, como el dígito, y la casilla
*Comprimir posición* no cambia nada de cómo se ingresa ni de cómo se grafica.

Los dos bytes del par también se acotan a la entrada — en el formulario y otra
vez al leer el archivo de configuración — porque ninguno es cosmético: un
identificador de tabla fuera de ``/``, ``\``, ``A``-``Z`` y ``0``-``9`` cae de
vuelta a la tabla primaria, y un código fuera del rango imprimible cae a ``&``.

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
llevan sus propios identificadores ``;`` y ``)``. Mic-E declara lo mismo en
otro lugar: su posición viaja en la dirección de destino AX.25, así que no le
sobra identificador de tipo de dato, y el indicador de capacidad de mensajería
lo lleva el byte TYPE (`` ` `` capaz de mensajes, ``'`` rastreador de una sola
vía) que sigue al byte de tabla de símbolos. Ambos formatos leen la misma marca
*Habilitar mensajería*, así que no pueden contradecirse sobre lo que esta
estación declara.

Comentario de posición Mic-E
============================

Un reporte Mic-E lleva, en los bits A/B/C de su dirección de destino, uno de
quince *comentarios de posición*: los siete valores estándar M0 *Off Duty*, M1
*En Route*, M2 *In Service*, M3 *Returning*, M4 *Committed*, M5 *Special* y M6
*Priority*; siete valores personalizados de definición local C0–C6; y
*Emergency*, que es el patrón con los tres bits en cero. Toda radio APRS
Kenwood y Yaesu lo expone en un menú del panel frontal, y los clientes
receptores lo muestran junto a la estación.

*Comentario de posición Mic-E*, en la página Tracker, elige entre los catorce
valores estándar y personalizados. Solo se aplica cuando *Codificación de
posición Mic-E* está activa, porque ningún otro formato tiene un campo para
él, y el valor de fábrica es M0 *Off Duty*: el habitual para una estación que
no se mueve, que es lo que este firmware baliza.

Emergency queda deliberadamente fuera de esa lista. Transmitirlo pide a otros
operadores, y en algunas regiones a servicios de emergencia, que respondan a
una emergencia real; eso no es algo que una página de configuración deba poder
activar con un clic equivocado y dejar activado para todas las balizas
siguientes. En recepción se trata por completo: ver
:ref:`es-filtering` para el decodificador, y el registro de tráfico
para la línea de advertencia que genera una emergencia recibida.

Texto de estado Mic-E
======================

La cola de texto libre del campo de información Mic-E — todo lo que sigue al
bloque de frecuencia, al token PHG/extensión de datos y al campo de
altitud — lleva, byte a byte, lo que el operador haya escrito como comentario
de la baliza. La única excepción es el primer byte de esa cola: APRS12c
cap.10 reserva un ``,`` o ``0x1d`` inicial para el subformato Mic-E Telemetry
Data (ya obsoleto), de modo que un comentario que empezara con cualquiera de
esos dos bytes se leería como telemetría en lugar de como texto.
``aprs_mice_encode()`` se protege de esto insertando un único espacio antes
de cualquiera de esos caracteres antes de añadir el comentario; un comentario
que empiece con cualquier otro byte llega al aire sin cambios. El espacio
insertado no aporta información propia y un cliente receptor lo muestra como
un espacio inicial ordinario dentro del comentario.

Rumbo de antena y PRE en los reportes de estado
===============================================

Un reporte de estado puede terminar con dos caracteres detrás de un ``^``: el
rumbo de antena en unidades de diez grados, y un código que representa la
potencia radiada efectiva. La operación de meteor scatter es la razón de ser del
par — las dos cifras que un corresponsal necesita para saber si vale la pena
esperar una ráfaga — y APRS101 cap.16 lo fija como el *último* campo del texto
de estado, que es el único lugar donde puede reconocerse.

Las dos mitades se fijan en la página Estación y valen para toda la estación,
como la opción Maidenhead: *Rumbo de antena en los reportes de estado* avanza de
a diez grados de 0 a 350, y *PRE en los reportes de estado* ofrece la tabla de la
propia especificación, de 10 W a 7290 W en los pasos que siguen al cuadrado del
dígito del código. Un rumbo sin potencia, o una potencia sin rumbo, no dice
nada, así que el bloque se emite solo cuando ambas están fijadas — dejar
cualquiera de las dos en *Off* es lo que hace una estación que no trabaja meteor
scatter, y entonces transmite exactamente el mismo reporte de estado que
transmitía antes.

El campo de información de estado tiene un tope de 63 bytes, y el armado va
descartando sus bloques opcionales en orden hasta que entre: primero el campo
inicial, después el bloque de frecuencia. El par rumbo/PRE nunca se descarta.
Son tres bytes, y una estación que transmite un reporte de estado durante una
cita de meteor scatter lo transmite justamente por esos tres bytes.

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
grado descrito arriba se aplica primero, así que una coordenada cuyos minutos
dan un 60.00 completo por error de punto flotante en el borde de un grado se
informa en el grado siguiente y no en el anterior.

Un nivel distinto de cero también fuerza el formato sin comprimir, por el mismo
tipo de razón que una extensión de datos: el formato comprimido no tiene dígitos
decimales que blanquear, así que respetar una marca de *comprimido* junto con la
ambigüedad transmitiría la posición exacta que el operador pidió ocultar. Mic-E,
en cambio, lleva la ambigüedad de forma nativa y no necesita ese repliegue.

La extensión de precisión !DAO!
================================

*Extensión DAO* en la página Station es un segundo ajuste de precisión, de
alcance estación, que se suma a la ambigüedad en lugar de reemplazarla.
Cuando está activada, ``aprs_dao_build()`` añade la extensión de
precisión/datum ``!DAO!`` en su forma legible (WGS-84, ``aprs12/datum.txt``)
al comentario de todo reporte de posición sin comprimir y al campo de texto
Mic-E, donde el estándar reserva esa misma posición final para ella. Los
cinco bytes recuperan, como un dígito decimal extra por eje, el tercer
dígito de minuto que los campos planos ``DDMM.mmN``/``DDDMM.mmW`` truncan y
descartan — un orden de magnitud más de precisión que la que el formato sin
comprimir lleva por sí solo, igualando la resolución propia en punto
flotante de latitud/longitud de este firmware. El campo base y este dígito
extra son dos vistas de una única medición de la coordenada — ambos leen la
división grados/minutos de ``main/include/aprs_minutes.h``, igual que los
bytes de posición Mic-E —, así que un receptor que vuelve a sumar el dígito
siempre queda a medio milésimo de minuto, unos 0,9 m, de la posición real, y
nunca más lejos de ella que el campo base por sí solo.

Como restituye precisión, ``!DAO!`` solo se aplica cuando *Position
ambiguity* es 0 y el formato no es el comprimido: una estación que oculta su
posición deliberadamente, o que ya envía coordenadas comprimidas de
resolución completa, no debe recuperar esa resolución a través de esta
extensión. Un receptor que no reconoce la extensión simplemente ve cinco
bytes extra de texto de comentario, así que siempre es seguro activarla.

El marcador de no archivar !x!
===============================

*Solicitar a APRS-IS que no archive mis paquetes*, en la página Estación, es
una opción de privacidad de toda la estación, desactivada por defecto. Al
activarla, cada campo de texto libre propio se antepone con el marcador de no
archivar de APRS-IS, ``!x!``, seguido de un espacio y luego el texto propio
del operador, si lo hay. El marcador se dirige a las bases de datos detrás de
APRS-IS, no a ninguna pasarela: les pide que no almacenen el paquete, pero no
lo retiene de RF ni de APRS-IS mismo, y un receptor que no lo reconoce
simplemente ve tres bytes extra de texto de comentario.

Los campos a los que llega son:

* los comentarios de posición de Tracker, IGate y Digipeater,
* los textos de estado de Tracker, IGate y Digipeater,
* el comentario del reporte meteorológico, en sus cuatro formas (objeto,
  posicionada con y sin marca de tiempo, y sin posición),
* los comentarios de objetos e ítems,
* el texto de boletines y anuncios.

Tres clases de paquete quedan fuera deliberadamente. El texto de un mensaje
no es texto descriptivo sobre esta estación: es una palabra que el
corresponsal lee en el cuerpo de un mensaje dirigido a él. Los paquetes de
definición de telemetría ``PARM``/``UNIT``/``EQNS``/``BITS`` son metadatos de
formato fijo, sin ranura de texto libre y con un presupuesto que la propia
definición necesita. Las respuestas a consultas contestan la pregunta de otra
estación en vez de reportar la posición o el estado de ésta.

Todos estos campos los arma un único constructor compartido,
``aprs_free_text_build()`` en ``main/include/aprs_free_text.h``, que aplica el
marcador y quita los caracteres que APRS reserva para el grupo de telemetría
de comentario en base 91 (``|`` y ``~``) en el mismo lugar. Un campo en el que
el operador ya escribió el marcador se deja como está, así nunca se envía dos
veces.

Esta opción solo afecta a los paquetes que esta estación origina. Un paquete
que esta estación retransmite, ya sea de IGate a RF, de RF a IGate o
digipetido, se pasa sin cambios; el marcador, si la estación de origen ya lo
puso ahí, viaja con él de todas formas, porque retransmitir nunca reescribe
los bytes de carga.

Localizador Maidenhead en los reportes de estado
================================================

*Localizador Maidenhead en los reportes de estado*, también de toda la estación
en la página Estación, antepone a cada reporte de estado el localizador de la
posición propia de esa baliza, su byte de tabla de símbolo y su código de
símbolo — la forma ``>IO91SX/G`` de APRS101 cap.16 — seguido de un espacio y el
texto de estado configurado. Los receptores que entienden la forma sitúan la
estación solo con el localizador; el resto muestran todo como texto de estado. El
texto configurado nunca se interpreta. El localizador es siempre el campo fijo
de 6 caracteres, en mayúsculas.

APRS101 cap.16 permite un único campo inicial en el campo de información de un
reporte de estado: la marca de tiempo DHM o el localizador Maidenhead, nunca
ambos a la vez — un receptor lee lo que sigue inmediatamente al DTI ``>`` como
el localizador, así que una marca de tiempo en esa posición se leería mal como
tal. Cuando *Marca de tiempo de estado* y *Localizador Maidenhead en los
reportes de estado* están activas a la vez en la misma baliza, el localizador
tiene prioridad y la marca de tiempo se omite en los reportes de estado de esa
baliza, ya que el localizador lleva la posición de la estación, algo que la
marca de tiempo no lleva.

Presupuesto de longitud del reporte de estado
=============================================

APRS101 cap.16 limita el campo de información de un reporte de estado a 63
bytes: el DTI ``>``, seguido de una marca de tiempo DHM opcional de 7
caracteres y como máximo 55 caracteres de texto de estado, o bien sin marca de
tiempo y como máximo 62 caracteres de texto de estado. Todo lo que el reporte
puede llevar además de las palabras del operador sale de ese mismo presupuesto
— el campo inicial (la marca de tiempo, o el localizador Maidenhead cuando
tiene prioridad) y el bloque de frecuencia — y un texto de estado completo de
49 caracteres más los dos bloques opcionales pide más de lo que entra.

Cuando eso ocurre se descartan los bloques opcionales, en este orden, hasta que
el campo entra:

#. el campo inicial (el localizador Maidenhead, o la marca de tiempo cuando no
   se usa localizador), que solo repite información que esta estación ya
   baliza por otro medio — su posición o la hora actual;
#. el bloque de frecuencia, la única parte del reporte sobre la que una radio
   receptora puede actuar.

El texto de estado configurado nunca se recorta: es lo que el reporte existe
para llevar. Si no entra ni siquiera por sí solo, se rechaza el reporte completo
y se registra el motivo, en vez de poner al aire una línea de estado truncada —
y por lo tanto malformada.

El espacio separador que precede al texto de estado pertenece al bloque que va
antes, así que se descarta junto con ese bloque: un reporte que queda sin campo
inicial y sin bloque de frecuencia — porque no se configuró ninguno, o porque el
presupuesto de arriba se llevó ambos — sale como ``>Mi texto de estado``, con las
palabras del operador inmediatamente después del DTI ``>``, que es la forma que
define APRS101 cap.16. El espacio solo aparece entre dos elementos que están
ambos presentes.

Bloque de frecuencia
====================

Cuando una baliza tiene configurada una frecuencia de monitoreo, tanto su
comentario de posición como su reporte de estado empiezan con el campo fijo de
10 bytes de frecuencia de ``freqspec.txt``, seguido del tono
(``Tnnn``/``Toff``) y, para un repetidor dúplex, del desplazamiento en unidades
de 10 kHz. Cuál de las tres formas que define la especificación se usa depende
solo de la frecuencia:

.. list-table::
   :header-rows: 1
   :widths: 26 24 50

   * - Frecuencia
     - Emitido
     - Forma
   * - Menor a 100 MHz
     - ``  50.62 MHz``
     - Forma de 10 kHz ``FFF.FF MHz``, justificada a la derecha contra su espacio
   * - 100.000-999.999 MHz
     - ``146.520MHz``
     - Forma de 1 kHz ``FFF.FFFMHz``
   * - Mayor a 999.999 MHz
     - ``A96.000MHz``
     - Designación de letra de microondas, una letra por bloque de 100 MHz

La tabla de letras cubre solo las bandas que enumera ``freqspec.txt``: A (1200),
B (2300), C (2400), D (3400), E (5600), F (5700), G (5800), H (10100),
I (10200), J (10300), K (10400), L (10500), M (24000), N (24100) y O (24200),
cada una abarcando su base más 99 MHz. Una frecuencia por encima de 999.999 MHz
fuera de todas ellas no tiene ninguna forma de 10 bytes, así que no se emite
bloque y se registra la omisión — un campo de 11 bytes correría todos los bytes
que un receptor lee después de él.

Las balizas de la propia estación (esta sección) siempre emiten la forma
simple de tono CTCSS, ya que ``g_config`` no tiene una fuente de DCS/banda
estrecha/RX dividido; el bloque de repetidor de Objetos/Ítems, más abajo, es
donde el operador puede elegir esos tres sub-campos adicionales.

Sub-campos de repetidor de Objetos/Ítems
=========================================

El bloque de repetidor de la página "Objetos e Ítems" construye el mismo
campo fijo de 10 bytes de frecuencia descrito arriba, más otros tres
sub-campos de ``freqspec.txt`` que un elemento puede activar:

.. list-table::
   :header-rows: 1
   :widths: 26 24 50

   * - Sub-campo
     - Ejemplo
     - Significado
   * - Código DCS
     - ``D023``
     - Reemplaza el sub-campo de tono CTCSS por un código DCS (tres dígitos
       octales); ambos comparten un solo espacio y son mutuamente
       excluyentes.
   * - Bandera de banda estrecha
     - ``t077`` / ``d023``
     - Pone en minúscula la letra inicial del sub-campo de tono/DCS ('T'/'D'
       pasa a 't'/'d') para señalar modulación de banda estrecha; no tiene
       ningún otro efecto en el aire.
   * - Frecuencia de recepción dividida
     - ``146.000rx``
     - Un segundo campo de 10 bytes independiente (solo definido en la forma
       100.000-999.999 MHz) emitido justo después de la frecuencia primaria
       (de transmisión), para un repetidor cuya frecuencia de recepción no
       es el desplazamiento dúplex estándar - por ejemplo, un repetidor
       cruzado de banda.

Los tres están desactivados por defecto, en cuyo caso el bloque es, byte a
byte, la misma forma de 10 bytes de frecuencia más tono CTCSS que construyen
las balizas de la propia estación descritas arriba.

Las marcas de tiempo son UTC
============================

Las marcas de tiempo de las balizas son zulú/UTC (``051200z``) según la
especificación APRS — por lo que ``time_sync.c`` fija el reloj del sistema a
``TZ=UTC0``. No existe ningún desfase de hora local en el firmware.
