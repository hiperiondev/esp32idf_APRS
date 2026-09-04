.. _es-telegram:

================
Bot de Telegram
================

La estación puede ejecutar, de forma opcional, un bot de Telegram junto con
sus servicios APRS, de modo que un operador pueda consultar y controlar
ligeramente la estación desde un teléfono sin abrir el panel web. Está
construido a partir de dos componentes superpuestos:

* ``esp_telegram_bot`` — el transporte HTTPS: el token del bot, las URL de la
  API de Bots de Telegram, el cliente TLS y la subida multipart.
* ``telegram_service`` — long polling, despacho de comandos, autorización por
  usuario/chat, alertas y parámetros remotos, construido sobre ese transporte.

``main/telegram_app.c`` es el pegamento entre esos dos componentes y este
firmware: es dueño del almacén JSON propio del bot, lo pone en marcha y lo
detiene de forma supervisada, y publica un diagnóstico que la página web
*Telegram* muestra como una frase traducida.

Su propio archivo de configuración
====================================

Todo lo que el bot necesita vive en ``/storage/telegram.json``, no en
``config.json``:

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Campo
     - Significado
   * - ``enabled``
     - El interruptor que dibuja la página Telegram. La única clave que este
       firmware añade al almacén; un archivo escrito a mano que la omita se
       carga con el bot apagado.
   * - ``routeStationMessages``
     - El interruptor "Reenviar mensajes de la estacion" de la página
       Telegram. Encendido, todo mensaje APRS dirigido al indicativo propio de
       un usuario autorizado también se envía al chat de Telegram de ese
       usuario, sea recibido de la red u originado aquí en la página *Snd/Rcv
       Msg*; ver `Reenvío de mensajes de la estación a Telegram`_ más abajo.
       Ausente, igual que ``enabled``, se carga apagado.
   * - ``routeBulletins``
     - El interruptor "Reenviar boletines" de la página Telegram. Encendido,
       todo boletín APRS que esta estación maneja —recibido de la red o
       transmitido por su propia página *Bulletins*— se envía a todos los
       usuarios autorizados, al administrador y a todos los chats de grupo
       permitidos; ver `Reenvío de boletines a Telegram`_ más abajo. Ausente,
       igual que ``enabled``, se carga apagado.
   * - ``bulletinWindowSeconds``
     - El campo "Ventana de repeticion de boletines" de la página Telegram:
       cuántos segundos un boletín ya reenviado impide que se reenvíen también
       sus repeticiones, de 0 a 86400. 0 reenvía todas las copias. A
       diferencia de los interruptores de arriba, una clave ausente se carga
       con el valor por omisión de 900 s y no como 0, porque aquí 0 es un
       ajuste válido y significa lo contrario de "déjalo como está".
   * - Token del bot
     - El token emitido por `@BotFather <https://t.me/BotFather>`__.
   * - Identificador del administrador
     - El identificador numérico de usuario de Telegram del administrador de
       la estación. ``telegram_init()`` lo añade por sí mismo a la lista de
       usuarios autorizados.
   * - Dirección de la Mini App
     - Dirección HTTPS opcional de una Mini App de Telegram que abre el botón
       de menú del bot.
   * - Usuarios autorizados / chats de grupo permitidos
     - Hasta 8 usuarios autorizados y 4 chats de grupo permitidos, cada uno con
       un identificador y un nombre para mostrar.

Todos los campos anteriores son editables desde la página *Telegram*, que
carga la estructura completa antes de guardar y la escribe de vuelta con los
cambios del formulario aplicados, de modo que la configuración completa
también es un único archivo que puede descargarse, editarse a mano y volver
a subirse desde la página *File Storage* (:ref:`es-storage-ota`).

Los identificadores de Telegram son de 64 bits y con signo (el identificador
de un supergrupo es un número negativo grande), así que tanto el identificador
del administrador como cada entrada de usuario/chat se manejan como
``int64_t``, enviados desde el formulario web como texto precisamente por eso.

Arranque supervisado
=====================

Activar el interruptor no llama directamente al servicio. En su lugar,
``telegram_app_apply_config()`` inicia una pequeña tarea supervisora, porque
un arranque de Telegram puede fallar por una razón que hay que poder
distinguir del resto:

#. **Conectividad de red.** Se comprueba primero y de forma barata, antes de
   reservar nada más, de modo que una estación sin ruta a internet lo informa
   de inmediato.
#. **Certificado raíz.** Cuando el transporte no se compila con el paquete de
   certificados de ESP-IDF, necesita un archivo PEM en la partición de
   almacenamiento (``/storage/telegram_certificate.pem`` por defecto, hasta
   8 KB); un archivo ausente o inválido se informa antes de cualquier intento
   de red.
#. **Forma del token.** El token debe tener la forma ``<dígitos>:<secreto>``
   antes de enviarse a ningún sitio.
#. **Memoria.** Un búfer, una cola o una sesión TLS que no caben se distinguen
   de cualquier otro fallo, ya que un diagnóstico equivocado hace que el
   operador busque memoria que nunca fue el problema. El mínimo de heap libre
   se comprueba junto con un cerrojo compartido que también protege el propio
   ``connect()`` TCP del enlace APRS-IS (:ref:`es-igate`), de modo que las dos
   operaciones de red más pesadas de este firmware nunca se ejecutan en el
   mismo instante; la que encuentra el cerrojo ya tomado simplemente reintenta
   en su propio ciclo en lugar de competir por la misma memoria.
#. **DNS y TCP.** Resolver ``api.telegram.org`` y abrir la conexión TLS hacia
   él se comprueban por separado.
#. **La propia respuesta de Telegram.** Un token con forma correcta pero
   inválido solo se detecta cuando la propia API lo rechaza.

Cada intento empieza tomando, bajo el lock, una instantánea de la copia en
memoria de ``telegram.json`` — token, identificador del administrador,
usuarios autorizados y chats permitidos — en una copia local de pila desde la
que el trabajador corre el resto del intento. Un guardado hecho desde la
página *Telegram* mientras un handshake ya lleva decenas de segundos en
curso nunca llega así al token ni a las tablas que un intento en marcha está
usando; queda recogido con normalidad por el siguiente intento.

Tanto el arranque como el apagado ocurren fuera de la tarea que los invoca,
entregados a ``telegram_app_tick_1hz()`` (llamada una vez por segundo), que
solo lanza una tarea trabajadora de corta vida cuando realmente corresponde.
Eso mantiene la pila del tamaño que exige un handshake TLS (la propia guía de
Telegram: unos 8 KB es el mínimo práctico) fuera tanto del manejador de
guardado del servidor web como de cualquier tarea permanente, de modo que
activar el interruptor en la página *Telegram* nunca bloquea el navegador ni
mantiene esa pila reservada mientras el bot simplemente está en marcha.

Hay una sola tarea trabajadora, no una por cada clase de trabajo. Un arranque,
un apagado y la entrega de las notificaciones reenviadas (descrita más abajo)
están dimensionados todos para un handshake TLS, y los tres los realiza la
misma tarea en lugar de una por clase, porque más de una de esas pilas a la
vez es más de lo que esta placa puede sostener junto a la tarea de sondeo del
propio servicio, la tarea del servidor web y el handshake mismo. La que se
lanza primero corre sola, y en cuanto termina esa misma tarea vacía la cola de
notificaciones antes de salir, en vez de lanzarse una segunda tarea para ello.
Un arranque o un apagado que encuentra la tarea trabajadora ya en marcha queda
pendiente y se vuelve a ofrecer en el tick siguiente, y una notificación
puesta en cola mientras la tarea está ocupada deja su línea en su sitio hasta
el lanzamiento que dispare la próxima línea reenviada, o hasta el vaciado que
la tarea en marcha hace al salir. No se pierde nada en ninguno de los dos
casos; lo único que cuesta es un retraso de un segundo o poco más.

Mientras el bot funciona, el mismo tick de 1 Hz vuelve a publicar sus
contadores y detecta un enlace a internet que desapareció o una tarea de
sondeo que terminó, de modo que una estación que pierde su conexión informa
"esperando una ruta de red" en lugar de un recuento de errores de sondeo
creciente y sin explicación.

El reinicio queda deliberadamente deshabilitado (``allow_reboot = false``):
esta estación lleva un transmisor y un planificador con compromisos horarios,
y ya existe un reinicio disponible, tras el propio inicio de sesión del panel
web, en la página *System*.

El aviso propio del servicio al arrancar está apagado
=====================================================

Dos comodidades que ofrece el servicio —publicar su lista de comandos en la
propia interfaz de Telegram, y anunciar al arrancar que el bot ya está en
marcha— están ambas desactivadas (``publish_commands = false``,
``announce_start = false``). Las dos envían justo en el instante en que
arranca la conexión de sondeo, abriendo una segunda sesión TLS mientras los
búferes de la primera siguen reservados; en una estación cuya memoria también
lleva los búferes DMA del módem de radio, esa segunda sesión no cabe, y el
único efecto visible sería un par de errores en el registro en cada arranque.
La lista de comandos no se pierde en ningún sentido funcional —cada comando
funciona se le haya avisado o no a Telegram sobre él— y el anuncio de arranque
queda reemplazado por el aviso que envía este propio firmware, descrito a
continuación, que viaja en un lote de transmisión en vez de en una sesión
propia.

Aviso de arranque
=================

El primer arranque del bot que alcanza Telegram tras un encendido o un reinicio
envía un mensaje a cada usuario autorizado, al administrador y a cada chat de
grupo permitido:

.. code-block:: text

   START
   Reason: Power-on
   Station: LU3VEA-10
   Firmware: esp32_APRS_igate 1.0.0

La línea de causa es ``esp_reset_reason()``, redactada con la misma tabla que
lee la franja System Info del panel (``main/include/reset_reason.h``), así que
las dos nunca escriben distinto la misma causa. Una estación que volvió de un
pánico, de un perro guardián o de una caída de tensión dice cuál, y no solo que
volvió: un operador que no está mirando el panel web se entera tanto de que una
estación que dejó andando se reinició como de qué la reinició.

Se envía una vez por arranque, no una vez por puesta en marcha del bot. El bot
se vuelve a levantar cada vez que se guarda la página *Telegram*, y otra vez
cuando se reconstruye una conexión que se cayó, y un aviso en cada una de esas
ocasiones informaría de un reinicio que nunca ocurrió. El pestillo que lo
impide es RAM común, así que se limpia exactamente en el evento que el aviso
informa y queda puesto por el resto del arranque.

No se envía como parte de la puesta en marcha que lo armó, sino unos quince
segundos después. Una puesta en marcha termina con la tarea de sondeo abriendo
su propia sesión TLS, y los búferes de registro de ese saludo son la
asignación contigua más grande que hace este firmware; enviar en ese instante
libera otra vez la conexión de sondeo y hace que el sondeo siguiente pague un
segundo saludo mientras la memoria del primero sigue tomada, que en esta placa
es justo el momento en que no cabe. Un cuarto de minuto después las
asignaciones de la puesta en marcha ya se devolvieron y un envío cuesta lo que
cuesta una línea encaminada. Antes de pedir la sesión se comprueban de nuevo
los mismos dos pisos de memoria que comprueba una puesta en marcha, así que una
estación que está justa por un momento simplemente espera un segundo más.

No tiene interruptor, y no se guarda nada indefinidamente cuando no se puede
enviar. Una estación con el bot deshabilitado, o cuyo bot nunca alcanza
Telegram, no envía ninguno, y un aviso que en dos minutos no encuentra un
momento con lugar para una sesión se descarta con una línea en el registro: el
aviso describe un arranque que ya terminó para cuando alguien podría actuar
sobre él, así que una copia que llegara mucho después diría menos que lo que ya
muestra el estado en vivo de la página *Telegram*. La entrega usa la misma
tarea trabajadora de vida corta y el mismo lote de transmisión que usa una
notificación encaminada, así que no cuesta una sesión TLS propia, y va primera
en ese lote, por delante de todo lo demás que lleve esa misma pasada.

Una sola sesión TLS, compartida por un lote
===========================================

El servicio mantiene dos clientes HTTP, uno para el sondeo largo y otro para
las peticiones salientes, y entre ambos nunca hay más de una sesión TLS viva.
Un saludo TLS necesita sus búferes de registro en bloques contiguos que una
estación que además lleva el módem de radio, la pila Wi-Fi y el servidor web
no puede dar dos veces, así que una petición saliente libera la sesión de
sondeo antes de abrir la suya, y el siguiente ciclo de sondeo la recupera.

Liberarla en cada mensaje haría que una estación con el reenvío activo pagara
ese intercambio de forma continua: cada línea reenviada costaría un saludo TLS
a la ida y otro a la vuelta al sondeo. Por eso los envíos que van juntos se
agrupan en un *lote de transmisión*. La sesión de sondeo se libera una sola
vez, con el primer mensaje del grupo, y todos los demás reutilizan la sesión
que ese primero abrió: vaciar la cola entera —incluido un boletín repartido a
ocho usuarios, el administrador y cuatro chats de grupo— cuesta un saludo TLS
y no uno por destinatario. Un ciclo de sondeo que venza mientras un lote sigue
en curso espera a que termine, hasta ocho segundos, antes de recuperar la
conexión, de modo que un reparto nunca se corta por la mitad; pasado ese
límite el sondeo recupera su conexión igualmente, porque un envío atascado en
un socket que no responde no puede impedir que el bot lea sus novedades.

El agrupamiento se aplica al vaciado de las notificaciones reenviadas, a un
ciclo de alertas y a ``telegram_broadcast()``. Un envío suelto es un grupo de
uno: libera la sesión de sondeo, abre la suya y la deja para que la recupere
el siguiente sondeo.

Reenvío de mensajes de la estación a Telegram
===============================================

El interruptor "Reenviar mensajes de la estacion" de la página *Telegram*,
junto al propio interruptor de encendido del bot y encima del interruptor
"Reenviar boletines" descrito más abajo, entrega los mensajes APRS al chat de
Telegram de su destinatario, de modo que cada operador lee en su teléfono lo
que le enviaron sin necesidad de abrir la página *Snd/Rcv Msg*.

Ambas procedencias se reenvían en los mismos términos. Un mensaje recibido por
el aire o desde el flujo APRS-IS se reenvía al decodificarlo, y un mensaje que
esta estación origina desde la página *Snd/Rcv Msg* se reenvía al
transmitirlo, así que enviar a un indicativo listado en la página *Telegram*
deja la línea en el chat de ese usuario además de ponerla al aire. En el
segundo caso lo que se reenvía es el texto que realmente salió, después del
saneamiento que aplica la ruta de salida, y sin el sufijo de número de
mensaje, que atañe al protocolo de radio y no a la persona que lee la línea.
Solo se reenvía la primera transmisión: los reintentos automáticos llevan el
mismo texto al mismo destinatario y llegarían como duplicados.

El conjunto de destinatarios es la tabla de usuarios autorizados de la página
*Telegram* y nada más. Cada tarjeta de usuario lleva el Indicativo propio de
ese operador, y un mensaje recibido se entrega a todo usuario cuyo Indicativo
coincide exactamente con el destinatario del mensaje, SSID incluido. El
indicativo propio de esta estación —el My Callsign de la página *Station*— no
participa de la decisión, así un mensaje dirigido a un usuario listado se
reenvía a ese usuario lea o no esta estación la trama por cuenta propia, y
varios usuarios que comparten un mismo indicativo base con distinto SSID
reciben solo lo que se envió al suyo. Un destinatario que no coincide con el
Indicativo de ningún usuario no se reenvía a nadie.

Una respuesta ``ackNN``/``rejNN`` no lleva texto legible y nunca se reenvía, y
tampoco lo hace un mensaje dirigido a un grupo (``ALL``, ``QST``, ``CQ`` o uno
de los nombres de grupo propios del operador), ya que un nombre de grupo no es
el indicativo de un usuario. El reenvío es independiente de lo que esta
estación haga después con la misma trama: la confirmación automática, el pulso
de alarma de ``/status`` y el historial de *Snd/Rcv Msg* siguen aplicándose
solo a los mensajes dirigidos al indicativo propio de esta estación. También
es independiente del interruptor de habilitación de la página *Message*. El
reenvío a Telegram es un consumidor de la trama recibida por derecho propio,
así que un mensaje o un boletín entrante se decodifica y se reenvía incluso en
una estación que corre el bot con la mensajería APRS apagada; todo lo que está
detrás de ese interruptor —la regla de aceptación, la confirmación, el pulso
de alarma y el historial del chat— sigue detrás de él.

Un mensaje reenviado llega a su usuario como una sola línea:

.. code-block:: text

   msg from <indicativo del remitente> to <indicativo del destinatario> :: <texto del mensaje>

El envío solo ocurre cuando el interruptor está encendido, el bot mismo está
habilitado y el bot está actualmente conectado; un mensaje que llega mientras
alguna de esas condiciones no se cumple simplemente no se reenvía, no queda
en espera para cuando el bot vuelva a estar disponible.
``telegram_app_notify_station_message()`` (declarada en ``telegram_app.h``)
es el punto de entrada que llama message.c; solo arma la línea, busca los
usuarios a los que corresponde y entrega un elemento por usuario a una pequeña
cola, ya que la ruta de decodificación de tramas que la llama corre en la
propia tarea de recepción del módem, que no lleva la pila que un llamado de
red a Telegram necesita para su handshake TLS. La misma tarea trabajadora de
corta vida que hace un arranque o un apagado vacía esa cola a través de
``telegram_send_message()`` como lo último que hace antes de salir.

La cola en sí se crea al aplicar la configuración —es decir, al arrancar y en
cada guardado de la página *Telegram*— y solo para un bot habilitado con al
menos uno de los dos interruptores de reenvío encendido. Una estación que los
deja ambos apagados nunca la reserva, y una que enciende alguno tiene la cola
en su sitio antes de que pueda reenviarse la primera línea, de modo que
ninguno de los dos puntos de entrada reserva nada en la tarea que lo llama.

Reenvío de boletines a Telegram
=================================

El interruptor "Reenviar boletines" de la página *Telegram*, justo debajo del
anterior, entrega a Telegram los boletines APRS. Un boletín está
dirigido a toda la red y no a una estación, así que no hay destinatario que
comparar ni indicativo por el que elegir a quién entregarlo: cada boletín va a
todos los usuarios autorizados de la tabla de usuarios, al administrador si
hay uno configurado y a todos los chats de grupo permitidos. Un administrador
que además figura en la tabla de usuarios recibe una copia, no dos.

Un boletín se reconoce por su destinatario, según el capítulo 14 de APRS101:
``BLN`` seguido de un solo dígito es un boletín general, ``BLN`` seguido de una
sola letra mayúscula es un anuncio, y cualquiera de los dos puede llevar un
nombre de grupo de hasta cinco caracteres más (``BLN1``, ``BLNA``,
``BLN1WX``). Cuentan todas las procedencias: un boletín escuchado por el aire,
uno llegado desde el flujo APRS-IS y uno que esta estación transmite por sí
misma se reenvían igual, de modo que los operadores que leen el bot ven los
anuncios propios de la estación en el mismo chat y con la misma forma que los
de cualquier otra.

Un boletín originado por esta estación lo reenvía el planificador que lo
transmite, una vez por pasada de transmisión y no una por canal, así que un
boletín enviado por RF e Internet a la vez llega igual una sola vez a cada
chat. Se reenvía haya tenido éxito o no cada transmisión, porque lo que viaja
a Telegram es el anuncio y no un informe sobre la radio, y lleva el texto tal
como sale al aire, marcador de no archivar incluido, en lugar del borrador
almacenado. Una ranura de boletín sin RF ni Internet marcados no se transmite
en absoluto y por lo tanto tampoco se reenvía.

Un boletín llega a sus destinatarios como una sola línea:

.. code-block:: text

   bulletin from <indicativo del remitente> to <destinatario del boletín> :: <texto del boletín>

Los boletines se repiten, que es lo que los hace boletines: el emisor los
retransmite por temporizador, cada digipetidor al alcance repite lo que oye y
además vuelve una copia igateada desde APRS-IS. Por eso un boletín cuyo
remitente, destinatario y texto coinciden con uno reenviado dentro de la
"Ventana de repeticion de boletines" de la página *Telegram* se descarta en
lugar de enviarse de nuevo, así un boletín periódico llega una sola vez a cada
chat en vez de llenarlo de copias de sí mismo. Editar el texto, o que lo envíe
otra estación, lo convierte en un boletín nuevo y se reenvía de inmediato. Se
recuerdan los ocho boletines reenviados más recientemente, como un hash de
esos tres campos y el momento en que se vieron, sea cual sea la ventana. Eso
es también lo que mantiene en una sola copia un boletín propio de esta
estación cuando la trama digipetida vuelve dentro de la
ventana: lleva el mismo remitente, destinatario y texto, así que la copia que
regresa se reconoce como la repetición que es. El destinatario se compara sin
sus espacios finales, porque los dos puntos de entrada no lo escriben igual
—el planificador entrega el campo de nueve caracteres rellenado con espacios
que acaba de poner en el aire (``BLN1     ``), y el decodificador de tramas el
recortado (``BLN1``)—, y sin eso la copia que regresa sería otro boletín.

La ventana la arma una entrega, nunca un intento. Un boletín que no se pudo
entregar la deja intacta y se reenvía en su siguiente transmisión, lo que
importa sobre todo con el intervalo más corto que admite la página
*Boletines*: un boletín que se repite cada 30 s frente a la ventana por
omisión perdería si no sus siguientes veintinueve transmisiones por un único
armado al que no siguió ninguna entrega, y las perdería todas mientras
persista lo que bloqueó la primera.

La ventana es el campo situado justo debajo del interruptor, en segundos, de 0
a 86400 (24 h), y por omisión vale 900 s. Póngala más larga que el intervalo
con que se transmiten los boletines que se oyen en el canal, así cada uno
llega a los chats una vez por edición y no una vez por transmisión; una
estación cuyos vecinos repiten sus boletines cada diez minutos quiere aquí más
de 600 s. Ponerla a 0 apaga la comprobación por completo y reenvía todas las
copias, incluidas las que vuelven por los digipetidores y desde el flujo
APRS-IS, que es lo que quiere quien vigila las retransmisiones de un canal
congestionado y lo que no quiere nadie que lea un chat. El valor vive en
``bulletinWindowSeconds`` y surte efecto al guardar, sin reiniciar.

El envío está sujeto a las mismas tres condiciones que un mensaje de estación
reenviado —interruptor encendido, bot habilitado, bot conectado— y no queda
nada en espera para cuando alguna de ellas vuelva a cumplirse. Cuál de ellas
falta, o qué otro motivo se aplicó, se escribe en el registro cada vez que
cambia y no una vez por boletín: un chat que queda en silencio dice por qué
una sola vez, en el momento en que empieza el motivo y otra vez cuando
termina, en lugar de callarlo o repetirlo en cada retransmisión.
``telegram_app_notify_bulletin()`` (declarada en ``telegram_app.h``) es el
punto de entrada que llama message.c para un boletín recibido y bulletins.c
para uno propio de esta estación, ambos por el mismo razonamiento sobre la
pila de la tarea que llama. Encola un único elemento para todo el boletín en lugar de uno por
destinatario: el elemento lleva el texto una sola vez y la tarea que vacía la
cola lo reparte, y es ahí donde se lee la lista de destinatarios, de modo que
un guardado que caiga entre ambos momentos se refleja en la entrega.

Se pide vaciar la cola cada vez que se encola una línea, y esa petición se
rechaza mientras la única tarea trabajadora breve del bot ya está ejecutando
un arranque, una parada u otro vaciado. Por eso la pregunta se vuelve a hacer
en el punto en que esa tarea termina y libera su lugar, de modo que un
vaciado rechazado queda aplazado y no perdido: un boletín encolado mientras se
reconstruía el bot sale en cuanto termina la reconstrucción, sin esperar otra
línea que la ventana de duplicados habría descartado igualmente.

El reparto se ejecuta como un solo lote de transmisión, así que toda la lista
de destinatarios comparte la única sesión TLS descrita más arriba en vez de
pagar un saludo TLS por chat, y está acotado por el número de destinatarios
que el propio archivo puede nombrar —ocho usuarios, cuatro chats de grupo y el
administrador—, de modo que un reparto retiene esa sesión durante un tiempo
que fija el firmware y no una configuración. Cada pasada informa en INFO a
cuántos destinatarios llegó y cuánto tardó, que es lo que le dice a un
operador que lee el registro que lo que está gastando memoria es una ráfaga de
boletines y no otra cosa.

Comandos incorporados
=======================

``telegram_service`` registra un único conjunto de comandos compartido en
todo dispositivo que lo incorpora; este firmware no añade comandos, sensores
ni parámetros remotos propios por encima de él.

.. list-table::
   :header-rows: 1
   :widths: 20 12 68

   * - Comando
     - Acceso
     - Qué hace
   * - ``/start``
     - cualquiera
     - Saluda y muestra los puntos de entrada del bot.
   * - ``/help``
     - cualquiera
     - Lista los comandos disponibles.
   * - ``/status``
     - cualquiera
     - Informa qué servicios de este firmware están activos o apagados
       (IGate, digirepetidor, tracker, meteorología, telemetría, mensajería,
       respondedor de consultas, BrandMeister, receptor GNSS, módem AFSK,
       limitador de ciclo de trabajo TX, sincronización SNTP) y la memoria
       libre actual.
   * - ``/sensors``
     - cualquiera
     - Informa cada campo meteorológico y canal de telemetría que esté
       habilitado y además tenga un sensor asignado, con su valor o "sin
       lectura". Los campos habilitados sin sensor asignado no se muestran.
   * - ``/uptime``
     - cualquiera
     - Informa cuánto tiempo lleva encendido el dispositivo.
   * - ``/whoami``
     - cualquiera
     - Informa los identificadores propios de usuario y chat de quien
       pregunta — los valores para añadir a mano a la lista de usuarios o
       chats de ``telegram.json``.
   * - ``/menu``
     - cualquiera
     - Muestra la interfaz de botones (teclado en línea de Telegram) para los
       comandos anteriores.
   * - ``/stats``
     - cualquiera
     - Informa los contadores propios del servicio: actualizaciones
       recibidas, comandos despachados, mensajes enviados, actualizaciones
       rechazadas, errores de sondeo.
   * - ``/config``, ``/get``, ``/set``
     - solo admin
     - Listan, leen y cambian parámetros registrados remotamente. Este
       firmware no registra ninguno, así que hoy no listan nada, pero quedan
       disponibles para que un futuro controlador o función registre uno.
   * - ``/users``
     - solo admin
     - Lista los usuarios autorizados.
   * - ``/alerts``
     - solo admin
     - Activa o desactiva las alertas push del servicio hacia el
       administrador.
   * - ``/reboot``
     - solo admin, deshabilitado aquí
     - Presente en la biblioteca; no registrado por este firmware (ver
       arriba).

"Solo admin" significa que el identificador de quien llama debe coincidir con
``admin_id`` o con uno de los usuarios autorizados añadidos con marca de
administrador; el intento de cualquier otro se cuenta como una actualización
rechazada en lugar de responderse.

Quién puede hablar con el bot
==============================

La autorización es una lista, y la lista está cerrada. Un remitente se acepta
solo cuando su identificador está en ella —sembrada desde ``admin_id`` por
``telegram_init()`` y ampliada con los usuarios autorizados de
``telegram.json``— o cuando el servicio corre en modo de acceso abierto, que
este firmware nunca habilita. Una lista vacía no es una excepción a esa
regla: rechaza a todos.

Eso importa en una estación cuyo token se configura antes que su
administrador, que es el caso normal de una imagen clonada o de un
``telegram.json`` escrito una vez y copiado a varios equipos. Una lista que
se abriera sola mientras está vacía dejaría a ese equipo respondiendo al
primer desconocido que encuentre el bot, durante todo el tiempo que el campo
del identificador siga en 0.

Cerrar la lista no hace más difícil averiguar el primer identificador, porque
averiguarlo nunca dependió de estar dentro. Un comando privado de un
remitente no listado se rechaza con una respuesta que nombra el identificador
numérico de ese mismo remitente, y ese número también queda escrito en el
log; ingresarlo como identificador del administrador en la página *Telegram*
es toda la puesta en marcha. Mientras no haya nadie autorizado, la página lo
advierte debajo de las credenciales y el servicio lo anota una vez en el log
al empezar a consultar, de modo que un bot que no responde a nadie nunca se
confunde con un bot que no logra conectarse.

Esa respuesta es lo único que el dispositivo envía en nombre de quien no ha
autorizado, así que está limitada en frecuencia: como mucho un rechazo por
identificador de remitente cada ``TELEGRAM_SERVICE_UNKNOWN_REPLY_INTERVAL_S``
segundos (300 por omisión), con los remitentes ya respondidos guardados en una
tabla de ``TELEGRAM_SERVICE_MAX_UNKNOWN_SENDERS`` entradas (4 por omisión) que
descarta primero al respondido hace más tiempo. Ambas son opciones de
compilación bajo *Telegram bot service* en ``menuconfig``. El motivo es la
memoria, no el ancho de banda: las conexiones de sondeo y de transmisión se
turnan una única sesión TLS, porque un solo saludo a la vez es todo lo que esta
placa admite junto a un módem de radio y un servidor web, de modo que una
respuesta sin medida dejaría que un desconocido gastara el presupuesto con el
que funciona el sondeo de la propia estación. Los comandos que quedan sin
responder se siguen contando como actualizaciones rechazadas y se siguen
anotando en el log con el identificador del remitente, y un remitente
descartado de la tabla vuelve a recibir respuesta en su siguiente comando: a
quien está poniendo en marcha una estación nunca le falta el número que vino a
buscar.

Una actualización que no nombra a ningún remitente —el total agregado de
reacciones que Telegram entrega en los chats que ocultan quién reaccionó— se
acepta solo dentro de un grupo de la lista de chats permitidos, que es la única
puerta que puede responder por ella. Fuera de un grupo no hay ni lista de chats
ni remitente, así que no hay nada que autorizar y se descarta.

La página Telegram
====================

``GET``/``POST /telegram`` (:ref:`es-http-routes`) expone todos los campos de
``telegram.json``: el interruptor de habilitación, el token del bot
(enmascarado, con un control *mostrar contraseña*), el identificador del
administrador, la dirección de la Mini App y las tablas de usuarios
autorizados y chats de grupo permitidos. Debajo del formulario de guardado,
una tabla de estado en vivo (``GET /telegram/status``, JSON, consultada cada
2 segundos) muestra el estado general, la razón precisa, cualquier detalle
sin traducir que Telegram o la pila de red hayan devuelto, el nombre de
usuario propio del bot una vez conocido, su tiempo activo y sus contadores.

Las tablas de usuarios autorizados y chats de grupo permitidos tienen tamaño
fijo — hasta 8 usuarios y 4 chats de grupo, según ``TELEGRAM_APP_USERS_MAX``
y ``TELEGRAM_APP_CHATS_MAX`` — y cada entrada se muestra como una tarjeta
plegable con un campo de identificador y un campo de nombre para mostrar.
Una cuenta que ya está en la lista obtiene su propio identificador con
``/whoami``; una que no lo está recibe el mismo número en el rechazo con que
el bot responde a cualquier comando, y el identificador de un grupo se lee
enviando ``/whoami`` dentro del grupo. Dejar vacío (o en 0) el
identificador de una tarjeta deja esa ranura sin usar, y al guardar la tabla
se compacta para que una ranura vaciada en medio no deje un hueco.

Todo lo que muestra la página sigue siendo el mismo ``telegram.json``
descrito arriba, así que también puede descargarse, editarse a mano y
volver a subirse desde la página *File Storage* — útil para editar varias
entradas a la vez, o para restaurar una configuración conocida.

.. seealso::

   :ref:`es-web-admin` — el resto de páginas y rutas del panel web.

   :ref:`es-storage-ota` — cómo se almacenan ``telegram.json`` y el archivo de
   certificado, y cómo el subir/descargar de la página File Storage permite a
   un operador editar las partes que la página Telegram no muestra.
