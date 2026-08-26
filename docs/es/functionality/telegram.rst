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
   operador busque memoria que nunca fue el problema.
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

Mientras el bot funciona, el mismo tick de 1 Hz vuelve a publicar sus
contadores y detecta un enlace a internet que desapareció o una tarea de
sondeo que terminó, de modo que una estación que pierde su conexión informa
"esperando una ruta de red" en lugar de un recuento de errores de sondeo
creciente y sin explicación.

El reinicio queda deliberadamente deshabilitado (``allow_reboot = false``):
esta estación lleva un transmisor y un planificador con compromisos horarios,
y ya existe un reinicio disponible, tras el propio inicio de sesión del panel
web, en la página *System*.

No se publica al arrancar
===========================

Dos comodidades que ofrece el servicio —publicar su lista de comandos en la
propia interfaz de Telegram, y anunciar al arrancar que el bot ya está en
marcha— están ambas desactivadas (``publish_commands = false``,
``announce_start = false``). Las dos abrirían una segunda sesión TLS justo en
el instante en que arranca la conexión de sondeo, mientras los búferes de la
primera siguen reservados; en una estación cuya memoria también lleva los
búferes DMA del módem de radio, esa segunda sesión no cabe, y el único efecto
visible sería un par de errores en el registro en cada arranque. Ni los
comandos ni el aviso de arranque se pierden en ningún sentido funcional: cada
comando funciona se le haya avisado o no a Telegram sobre él, y el estado en
vivo de la página *Telegram* ya muestra a un administrador que el bot está
activo.

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
