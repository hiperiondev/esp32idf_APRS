.. _es-web-admin:

==================
Administración web
==================

El componente ``webconfig`` (``components/webconfig/``) es una administración
basada en ``esp_http_server`` construida con un archivo por página
(``pages/*.c``), una tabla de rutas (``web_server.c``) y un conjunto de ayudantes
compartidos (``web_common.c``). Usa **autenticación HTTP Basic** contra
``g_config.http_username`` / ``http_password`` en cada página — con la única
excepción del ``/style.css`` y el ``/logo.png`` estáticos, que no llevan datos de
configuración ni de tráfico —, además de coincidencia de URI con comodines, una
pila de manejador de 20 KB y purga LRU.

El logo a la izquierda de la barra superior es un PNG embebido en el firmware
como un array ``const`` (``components/webconfig/include/web_logo.h``) y servido
por ``web_handle_logo()`` en ``GET /logo.png``. Guardarlo en la imagen de la
aplicación y no en la partición LittleFS hace que el marco de cada página no
dependa de una partición que la página de Almacenamiento puede formatear,
sobrescribir y llenar. La hoja de estilos lo escala sólo por altura
(``height:32px;width:auto``, 26 px por debajo de 600 px), así que las
proporciones de la propia imagen deciden su ancho y sustituirla por otra de
forma distinta no requiere tocar la hoja de estilos; la etiqueta lleva el tamaño
intrínseco del PNG para que la barra reserve el ancho correcto antes de que la
imagen llegue. La respuesta se puede cachear un día, ya que sólo una
actualización OTA puede cambiarla.

Diseño adaptable
================

Un solo árbol de marcado y una sola hoja de estilos sirven para escritorio,
tableta y teléfono. No hay una página móvil aparte, ni detección de
user-agent, ni script de disposición: cada página lleva una declaración de
viewport ``width=device-width``, y ``web_handle_css()`` adapta los mismos
componentes en tres puntos de corte.

.. list-table::
   :header-rows: 1
   :widths: 26 74

   * - Punto de corte
     - Qué cambia
   * - **Por encima de 900 px**
     - Una barra lateral fija de 220 px junto a una columna de contenido
       limitada a 1000 px.
   * - **900 px o menos**
     - La barra lateral pasa a ser un cajón lateral que abre un botón de menú
       en la barra superior, y la columna de contenido ocupa todo el ancho. La
       barra superior queda fija arriba, de modo que se puede llegar al menú
       desde cualquier punto de una página de ajustes larga sin volver a subir.
   * - **600 px o menos**
     - Las tarjetas, los títulos y los márgenes se ajustan, y cada ``.row`` de
       campos de formulario se reduce a una sola columna.
   * - **Puntero grueso**
     - Con independencia del ancho: los botones y las entradas del menú crecen
       hasta un objetivo táctil de 44 px, las casillas hasta 20 px, y los
       campos de texto toman un cuerpo de 16 px — por debajo de eso, un
       navegador móvil amplía la página al enfocar un campo y la deja ampliada.

El cajón es solo CSS. ``web_send_header()`` emite una casilla oculta como
hermana tanto de la barra superior como de la disposición, más el botón de menú
y un fondo atenuado que son ambos etiquetas de esa casilla, de modo que la hoja
de estilos alcanza los tres desde el estado ``:checked`` de la casilla. No hay
nada que inicializar, nada se rompe si un script no carga, y una carga de página
normal vuelve a dejar el menú cerrado.

Dos detalles sostienen casi todo el comportamiento horizontal:

* La columna de contenido es un elemento flex con ``min-width: 0``. Sin eso, un
  elemento flex se niega a encogerse por debajo del ancho de su hijo más ancho,
  así que una sola tabla ancha ensancharía la página entera en vez de
  desplazarse dentro de sí misma.
* Cada tabla de cada página se emite dentro de un marco ``table-wrap``. Una
  tabla se dimensiona según sus propias columnas y no se puede estrechar sin
  plegar sus celdas, así que el marco absorbe la diferencia: las tablas de
  telemetría y meteorología, de ocho columnas, conservan su ancho completo y se
  desplazan lateralmente dentro de la página. La tabla de tráfico del panel se
  desplaza en ambos ejes, ya que además contiene más filas de las que caben en
  pantalla.

Las respuestas que se muestran fuera del marco de administración — el
intersticial de guardado, los cuerpos ``401`` / ``403`` / ``429`` y el rechazo a
nivel de página de un manejador — pasan por ``web_send_standalone_page()``, que
envuelve un fragmento en un documento mínimo propio. Ese documento lleva su
propia declaración de viewport y sus propias reglas en línea en vez de enlazar
``/style.css``: tiene que representarse igual cuando la hoja de estilos no se
puede servir, que es justamente lo que algunas de esas respuestas están
informando.

Por qué ayudantes por campo
===========================

El HTML se emite a través de pequeños ayudantes por campo (``web_field_text``,
``web_field_int``, ``web_field_checkbox``, ``web_select_*``, ``web_field_symbol``,
…) en lugar de un único ``snprintf`` gigante — deliberadamente, para evitar
``-Werror=format-truncation`` y mantener cada página legible.

Los ayudantes numéricos (``web_field_int``, ``web_field_float``) reciben el
rango aceptado del campo y siempre lo emiten como los atributos HTML
``min``/``max`` del input, de modo que cada campo numérico de cada página queda
validado por el navegador antes de enviar el formulario. Esa es la primera
línea de defensa frente a un error de tipeo; el manejador POST sigue acotando
lo que guarda, que es lo que resiste ante una petición manipulada. Los dominios
que se repiten (SSID, intervalo de transmisión, latitud, longitud, altitud)
provienen de las constantes ``WEB_RANGE_*`` de ``web_common.h``, así un límite
se define una sola vez para todas las páginas que lo comparten.

Ayuda contextual
================

Cada opción de cada página termina su etiqueta con un pequeño signo de
interrogación naranja dentro de un círculo. Al posar el puntero sobre él — o al
darle foco de teclado, o al tocarlo en una pantalla táctil — se abre un globo
con una explicación breve de lo que hace esa opción, acotada por
``WEB_HELP_MAX_BYTES`` (253 bytes) para que se lea de un vistazo sobre el
control que explica.

``web_help_markup()`` emite un ``span.hlp`` enfocable con el glifo y un
``span.hlp-box`` anidado, y ``web_handle_css()`` dibuja el círculo, lo colorea y
revela el globo desde el ``:hover`` y el ``:focus`` del propio marcador. Ningún
estado sobrevive a una carga de página, y la mitad de ``:focus`` es lo que hace
la ayuda alcanzable sin ratón. Un toque en el marcador no activa además la
etiqueta que lo contiene, así que preguntar qué hace una casilla nunca la
conmuta.

El globo se dibuja como una capa fija sobre toda la página, no como una caja
dentro de la tarjeta del propio campo. Los marcadores viven dentro de tarjetas,
acordeones y marcos de tabla, y varios de ellos recortan lo que se sale — el
acordeón oculta su desbordamiento para que sus esquinas redondeadas queden
limpias, y un marco de tabla que se desplaza en horizontal también recorta en
vertical —, así que un globo maquetado dentro de uno de ellos quedaría cortado
en su borde en cuanto el texto fuera más largo que el espacio libre encima del
campo. Sacarlo del flujo es lo que permite leer cada globo entero, por encima de
cualquier tarjeta, tabla o control de la página.

Las coordenadas son lo único que la hoja de estilos no puede aportar entonces, y
las da el script que ``web_send_footer()`` emite en cada página: mide el
marcador, centra el globo sobre él, lo devuelve dentro del borde de pantalla que
fuera a cruzar, lo voltea bajo el campo cuando no hay sitio arriba y desliza la
flecha por su borde para que siga apuntando al marcador después de todo eso. Un
globo abierto se vuelve a colocar al desplazar y al redimensionar, con el
oyente de desplazamiento en fase de captura para que también siga a los marcos
de registro, chat y tabla que se desplazan por dentro. Los manejadores se
enlazan al documento y no a cada marcador, de modo que cubren igualmente las
filas que el script propio de una página añade después de la carga.

El texto de ayuda se **busca a partir de la etiqueta, no se pasa como
argumento**. Las páginas llaman a ``web_field_int(req, TR_F_SSID, …)`` igual que
antes de que existiera esta función; ``web_help_for_label()`` compara esa
etiqueta con la tabla de ``web_help.c``, que empareja cada macro de etiqueta
``TR_xxx`` con su macro de ayuda ``TR_H_xxx``. Eso deja intactos los 359 puntos
de llamada y hace que una etiqueta compartida por varias páginas se explique
una sola vez y se lea igual en todas. Una opción cuya etiqueta no tiene fila en
la tabla simplemente se representa sin marcador.

Unas pocas etiquetas se construyen en tiempo de ejecución — ``Alias 2``,
``Indicativo 3``, un filtro de tipo de contenido, un preajuste de ruta numerado
— y por eso no coinciden con nada. Esos puntos de llamada representan el
marcador una vez con ``web_help_markup()`` fuera de su bucle y se lo pasan a
cada fila mediante la variante ``_h()`` del ayudante (``web_field_text_h``,
``web_field_int_h``, ``web_field_checkbox_plain_h``, ``web_select_open_h``).

Añadir una opción implica, por tanto, añadir su cadena ``TR_H_xxx`` a los tres
archivos ``lang_*.h`` y una fila a ``web_help.c``. Omitir la fila no es un error
de compilación — la opción se representa sin signo de interrogación —, así que
conviene revisar la tabla siempre que una página gane un campo.


Las páginas
===========

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Página
     - Qué hace
   * - **Dashboard**
     - Píldoras de Network Status (Wi-Fi, APRS-IS vía ``igate_is_connected()``),
       un panel de STATISTICS, una tabla LAST HEARD con iconos de símbolo, y una
       tabla de tráfico en vivo (DX / PACKET / DECODIFICADO / AUDIO) alimentada
       por long-poll basado en secuencia. DECODIFICADO trae lo que se leyó de la
       carga en sí — la marca de tiempo propia del paquete, rumbo, velocidad,
       altitud, alcance de radio, PHG o DFS, y la marcación y el NRQ de un
       reporte DF — y queda vacío para una carga que no lleva ninguno de esos
       campos. La columna guarda la línea de resumen completa sea cual sea la
       carga: el anillo dimensiona su campo con la misma constante en la que
       escribe el formateador, así que un reporte que llena todos los campos se
       muestra entero y no recortado.
   * - **Station**
     - La identidad compartida de la propia estación que leen cada baliza,
       objeto y mensaje: indicativo, latitud, longitud, altitud
       (``g_config.my_*``), más las opciones al aire de toda la estación:
       ambigüedad de posición, prefijo de localizador Maidenhead en los reportes
       de estado, y el rumbo de antena y la PRE de meteor scatter que los
       cierran. La posición puede escribirse o tomarse en vivo del receptor
       GNSS mediante *Usar GPS*, que deshabilita los tres campos y los rellena
       desde ``GET /gps/live`` una vez por segundo mientras está marcado,
       redondeando latitud y longitud a 4 decimales y la altitud a 1 decimal
       para que los valores rellenados siempre pasen la validación de los
       campos.
   * - **IGate**
     - Habilitar, RF→INET / INET→RF, ambas máscaras de filtro, budlist y guardas
       de rango/prefijo, indicativo/SSID/passcode, cuatro recuadros *APRS-IS
       Server* (cada uno con casilla Habilitar más host y puerto, usados como
       rotación de failover), cadena de filtro
       de servidor, el interruptor *Registrar después de los filtros* que acota la
       tabla de tráfico y la consola serie a lo que aceptan los filtros locales, nueve casillas de tipo de carga por dirección (la novena,
       *Otros*, cubre capacidades de estación, formatos definidos por el
       usuario, radiogoniometría Agrelo, balizas de localizador Maidenhead y el
       elemento de mapa reservado), baliza on/off, posición, intervalo, selector de símbolo,
       objeto, comentario, estado, PHG. *Filtrado de Mensajes* lleva el
       interruptor de criterios de mensajes INET→RF, el límite de saltos del
       destinatario y la ventana de escucha
       local. La posición puede escribirse, reflejar *Usar Datos de Mi
       Estación* o tomarse en vivo del receptor GNSS mediante *Usar GPS*; las
       tres opciones son mutuamente excluyentes.
   * - **BrandMeister**
     - Interruptor de la interconexión, la suscripción de monitor mundial, el
       interruptor de ruteo de mensajes solo por Internet para destinatarios
       BrandMeister, y cuatro indicativos de pasarela opcionales. El
       interruptor de monitor se rechaza mientras el reenvío INET→RF está
       activo y el filtro de rango INET→RF de la página *IGate* está apagado,
       porque los términos del filtro APRS-IS se combinan con O y no quedaría
       nada entre un flujo mundial y el transmisor. Una tabla de estado de solo
       lectura informa el estado de la interconexión, si ``u/APBM*`` está
       presente en el filtro de servidor, el ajuste del filtro de rango y
       cuántas estaciones BrandMeister hay en LAST HEARD. No interviene ninguna
       conexión DMR; ver :ref:`es-brandmeister`.
   * - **Digi**
     - Habilitar digipeater, indicativo/SSID y ajustes de baliza (posición,
       símbolo, intervalo, comentario, estado, ruta). *Extensión de Datos*
       elige qué lleva la baliza de posición en la ranura posterior al código
       de símbolo — PHG, RNG, DFS o un reporte DF — con los mismos subcampos y
       el mismo espejo *Usar Datos de Mi Estación* que ofrece la página
       *IGate*. *Alias de Ruta n-N* lleva
       las cuatro filas de {alias, N máximo, modo} con las que repite el
       digipeater, el interruptor de solo relleno, la elección de qué hacer con
       un contador de saltos atrapado y el interruptor *Digipetir por SSID de
       destino (heredado)*, apagado por omisión. Lleva también los cuatro presets de ruta
       compartidos ``path[0..3]`` entre los que elige cada servicio que
       transmite. La ventana de supresión de duplicados es un único control, en
       la página *IGate*. La posición también puede tomarse en vivo del
       receptor GNSS mediante *Usar GPS*, mutuamente excluyente con *Usar
       Datos de Mi Estación*.
   * - **Tracker**
     - Habilitar tracker, indicativo/SSID, intervalo fijo, posición, símbolo de
       estación, comentario, opciones de posición comprimida, posición Mic-E
       (con su selector de comentario de posición), PHG y altitud. La posición
       fija puede escribirse, reflejar *Usar Datos de Mi Estación* o tomarse en
       vivo del receptor GNSS mediante *Usar GPS*; las tres opciones son
       mutuamente excluyentes. *Usar posición GPS en vivo* es independiente de
       las tres: deja la posición fija como respaldo y hace que cada
       transmisión lea el receptor. El fieldset *SmartBeaconing* (intervalo
       lento/rápido, velocidad baja/alta, ángulo de giro, pendiente de giro,
       tiempo mínimo de giro) vuelve ese intervalo adaptativo a la velocidad;
       necesita la posición en vivo para tener algo de donde leer. Cierran la
       página un fieldset de baliza de estado (intervalo y texto) y otro de
       frecuencia/repetidora (frecuencia, dúplex, desplazamiento, tono).
   * - **Weather**
     - Habilitar, enviar-por-RF/-INET, marca de tiempo, indicativo/SSID/ruta WX,
       posición, nombre de objeto, comentario, casillas *Averaged* por campo, y
       — por cada campo WX al aire — un **desplegable de canal** rellenado en
       vivo desde el registro ``sensors_local`` y filtrado por las capacidades
       publicadas de cada controlador. Valores en vivo vía ``/wx/values``. La
       posición puede escribirse, reflejar *Usar Datos de Mi Estación* o
       tomarse en vivo del receptor GNSS mediante *Usar GPS*; las tres
       opciones son mutuamente excluyentes.
   * - **Telemetry**
     - Parámetros de baliza/informe, conmutadores de mensajes de definición,
       analógicos A1–A5 con selectores de origen y calibración, digitales B1–B8
       con selectores de origen y sentido. Valores en vivo vía ``/tlm/values``.
   * - **GPS**
     - *Habilitar Receptor GPS* es el único conmutador que el resto del
       firmware consulta antes de usar nada de lo que informa el módulo; con él
       apagado la UART ni siquiera se instala y la tarea lectora no corre.
       Moverlo surte efecto de inmediato, sin reiniciar. Debajo, una vista en
       vivo de solo lectura del receptor, encabezada por una insignia
       *Estado del Módulo* con código de color que convierte una página de
       números en un único diagnóstico: rojo *Deshabilitado* cuando el
       conmutador está apagado o la UART no pudo inicializarse, rojo *Sin
       datos (revisar cableado)* cuando el módulo está habilitado pero no ha
       llegado nada por el pin de recepción dentro del plazo de enlace, ámbar
       *Buscando (sin fijación)* cuando llegan sentencias pero aún no se ha
       informado una solución de navegación válida, y verde *Fijación OK* una
       vez que sí. Debajo de la insignia, estado del
       enlace, estado de navegación, calidad del fix y modo 2D/3D, posición,
       altitud y separación del geoide, velocidad sobre el suelo, rumbo y
       variación magnética, fecha y hora UTC, satélites usados y a la vista,
       HDOP/PDOP/VDOP, los contadores de sentencias aceptadas y descartadas y
       la antigüedad de la última sentencia y del último fix. El puerto serie
       y sus pines son cableado de placa fijado en compilación y se muestran
       como texto. Valores en vivo vía ``/gps/values``, sondeado cada segundo.
       Su contraparte numérica, ``/gps/live``, es la que consulta la casilla
       *Usar GPS* de cada otra página para autocompletar sus propios campos de
       posición/movimiento (Station, IGate, Digi, Tracker, Weather); el script
       de cada página redondea la latitud, longitud y altitud recibidas a la
       precisión que aceptan sus propios campos (4 decimales para la posición,
       1 decimal para la altitud) antes de escribirlas.
   * - **Telegram**
     - *Habilitar bot de Telegram* gobierna todo el subsistema; con él apagado
       no se conecta nada a Telegram ni corre ninguna tarea de sondeo, y
       moverlo tiene efecto inmediato, sin reiniciar. Debajo, el token del bot
       (como campo de contraseña, con el mismo control de mostrar/ocultar que
       usa el passcode del IGate) y el identificador numérico del
       administrador, que se lleva como valor de 64 bits y se envía como texto
       porque los identificadores de usuario de Telegram ya no entran en 32
       bits. Debajo, la dirección de la Mini App y las tablas de tamaño fijo
       de usuarios autorizados y chats de grupo permitidos (hasta 8 usuarios y
       4 chats de grupo, cada uno con un identificador y un nombre para
       mostrar). Todo lo de esta página se guarda en ``/storage/telegram.json``,
       no en ``config.json``, así que también puede descargarse y subirse
       desde la página Almacenamiento. La tabla de estado bajo el
       formulario informa en qué punto está la conexión y, cuando no avanza,
       exactamente qué paso falló y qué hacer: falta el archivo de
       configuración o no se puede parsear, el token está vacío o no tiene la
       forma ``<números>:<secreto>``, el certificado raíz no está en la
       partición de almacenamiento, todavía no hay ruta a Internet, la memoria
       no alcanzó para una sesión TLS, o el propio Telegram respondió y
       rechazó, en cuyo caso se muestran su código de error y su texto sin
       traducir. Valores en vivo por ``/telegram/status``, cada dos segundos. El
       bot que esta página levanta responde ``/status`` con el interruptor de
       cada servicio - igate, digipetidor, tracker, meteorología, telemetría,
       mensajería, respondedor de consultas, BrandMeister, receptor GNSS,
       módem AFSK, limitador de ciclo de trabajo de TX y sincronización SNTP -
       y ``/sensors`` con cada campo meteorológico y cada canal de telemetría
       habilitado en las páginas Weather y Telemetry, con el driver de sensor
       al que está asignado y su lectura actual. Ambas respuestas se arman con
       la configuración vigente en el momento en que llega el comando, así que
       un Save surte efecto en el comando siguiente sin reiniciar.
   * - **Winlink**
     - Los dos roles Winlink de la estación, en una sola página. *Cuenta
       Winlink* contiene lo que necesita una sesión propia: el indicativo del
       servicio APRSLink, la identidad con la que se abre el buzón (el
       indicativo base, sin su SSID), la contraseña con la que se responde un
       desafío de acceso, y los interruptores que deciden si una sesión se abre
       sola, cuánto puede durar, si su tráfico se mantiene fuera del aire y si
       el comentario de la baliza anuncia a esta estación como lectora de
       Winlink. La contraseña se muestra como campo de contraseña con el mismo
       control de mostrar/ocultar que usa el passcode del IGate, y nunca se
       transmite: un desafío nombra tres posiciones de caracteres y solo esos
       caracteres se devuelven. *Pasarela para estaciones locales* contiene el
       único ajuste del otro rol, retransmitir la sesión propia de un vecino,
       junto con una vista de solo lectura de los tres ajustes del IGate que
       deciden la misma cuestión, para poder ver de un vistazo las cuatro
       entradas del pase de mensajes. Debajo del formulario, la terminal de
       sesión: en qué punto está la sesión y cuánto le queda, botones para
       entrar, salir y listar el correo, un campo de orden libre que admite
       todo el juego de órdenes de APRSLink, un asistente de tres pasos para
       escribir un mensaje, y las respuestas que devolvió el servicio. Cada
       respuesta guardada que empieza con un número de mensaje es una línea de
       un listado del buzón y lleva una fila *Leer* / *Responder* /
       *Reenviar* / *Eliminar* para ese mensaje; un campo *Número de mensaje*
       debajo del buzón lleva los mismos cuatro para un número escrito a mano.
       Valores en vivo por
       ``/winlink/status`` y ``/winlink/list``, consultados cada
       tres segundos; las acciones se envían a ``/winlink/cmd``, que es POST
       porque acciona el transmisor.
   * - **Logs**
     - Un visor de la consola serie, para poder leer lo que la estación imprime
       sin tener un cable conectado. No hay nada que configurar: un botón, que
       dice *Iniciar* mientras no se captura nada y *Detener* mientras se
       captura, y una ventana debajo que guarda las últimas 50 líneas. Una
       línea de consola de más de 255 caracteres continúa en la fila siguiente
       en vez de cortarse, y la ventana se desplaza en ambos sentidos:
       verticalmente porque guarda más filas de las que caben en pantalla, y
       horizontalmente porque cada línea se mantiene entera. *Iniciar* instala
       una copia sobre el escritor del registro; la salida serie en sí no
       cambia en ningún caso, y el anillo que la copia rellena solo se reserva
       mientras hay una captura en curso. La captura nunca sobrevive a la
       página: al cargar, la página le pide a la estación que detenga lo que
       hubiera quedado activo, así que el botón siempre aparece en su estado
       *Iniciar*; salir
       de ella detiene la captura desde el navegador; y una pestaña cerrada,
       dormida o cortada a media sesión no dice nada, y por eso la copia
       también se detiene sola cuando nadie la lee durante diez segundos. No
       se escribe nada en la flash ni se graba nada: solo se muestra lo que
       llega mientras la ventana está abierta. Líneas en vivo por
       ``/logs/read``, consultado cada segundo.
   * - **Bulletins**
     - Hasta cinco boletines (identificador y grupo de destinatario, texto,
       RF/INET, intervalo inicial, rampa de decaimiento, caducidad).
   * - **Objects and Items**
     - Hasta cinco objetos/ítems (nombre, posición, símbolo, rumbo/velocidad,
       comentario, RF/INET, intervalo, bandera permanente, kill).
   * - **Snd/Rcv Msg**
     - La interfaz de bandeja/redacción APRS (``/msgchat``): un solo hilo de
       mensajes enviados y recibidos, cinco visibles a la vez y diez
       guardados.
   * - **Message**
     - Configura el motor de mensajería (habilitación RF/INET, reintento, ruta
       de digipeteo, GPIO de alarma).
   * - **Query**
     - Habilitación del respondedor de consultas APRS, qué origen se responde
       (RF / APRS-IS — la respuesta siempre vuelve por el canal por el que llegó
       la pregunta), tipos de consulta general (``?APRS?``, y donde estén
       compilados,
       ``?WX?``/``?IGATE?``), habilitación de consultas dirigidas, conjunto
       extendido de consultas dirigidas, intervalo
       mínimo de respuesta (límite de seguridad frente a bucles/uso del
       canal), y la baliza periódica de capacidades de estación: habilitación,
       intervalo, selección de canal RF y APRS-IS, y los elementos de capacidad
       adicionales a agregar.
   * - **Radio / Modem**
     - Modo FX.25 (apagado / solo RX / RX+TX); habilitar módem de audio,
       modulación (300 / 1200 Bell202 / 1200 V.23 / 9600 G3RUH), LPF de audio
       (audio plano), ms de preámbulo, ms de ranura de tiempo TX, buffers TX,
       retención extra de des-activación de PTT, persistencia CSMA, y el
       limitador de ciclo de trabajo a largo plazo (habilitación más porcentaje
       de techo); y el botón
       **LOOP TEST**. Guardar reaplica el módem en vivo — sin reinicio.
   * - **Wireless**
     - Modo (off/STA/AP/AP+STA), SSID/pass/canal del AP, 5 ranuras STA cada una
       con su propia casilla Enable, potencia TX en dBm, más un escaneo en vivo.
   * - **System**
     - Login web, frecuencia de CPU (aplicada en vivo) y una sección *Time*:
       habilitación de NTP, hosts NTP ×3, intervalo de resincronización, y un
       selector de zona horaria que fija la fecha/hora local mostrada en el
       panel (el reloj en sí sigue en UTC). También el botón de reset de
       fábrica.
   * - **Storage**
     - Navegador LittleFS: descargar, borrar, subida multipart, uso, formatear.
   * - **About / Firmware**
     - Nombre del proyecto, versión, fecha/hora de compilación, versión de IDF,
       partición en ejecución, y el panel de **OTA Update**.

.. note::

   Todos los controles de estas páginas gobiernan conducta real: un ajuste que
   llega a ``config.json`` lo lee el servicio que lo posee. El digipeater
   siempre maneja WIDEn-N y repite sin retardo añadido, así que ninguna de las
   dos cosas se ofrece como opción.

   La supresión de duplicados tiene exactamente un par de controles, *Dup cache
   size* (``dupCacheSize``) y *Dup cache timeout* (``dupCacheTimeoutMs``) en la
   página *IGate*, y gobiernan tanto al digipeater como al IGate: ambos
   servicios comparten la única caché de ``components/igate``, cada uno con su
   propio ámbito.

Las estadísticas del panel
==========================

Las estadísticas vienen de ``aprs_service_get_stats()``, rastreadas de forma
**independiente** de ``igate_en``/``digi_en``:

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Contador
     - Significado
   * - ``radio_rx``
     - Cada trama que el módem decodificó de RF.
   * - ``radio_tx``
     - Cada trama transmitida con éxito por RF.
   * - ``rf2inet``
     - Tramas que el IGate realmente subió.
   * - ``inet2rf``
     - Líneas de APRS-IS realmente transmitidas por RF.
   * - ``digi``
     - Tramas digipeteadas (ruta reescrita + retransmitida).
   * - ``drop`` / ``err``
     - Tramas descartadas / que fallaron al decodificar, a nivel de RX/servicio.
   * - ``tx_queue_depth`` / ``tx_queue_limit``
     - El backlog actual del anillo de TX de RF y el tope efectivo de *TX
       buffers*, para que el panel se lea como la línea "n/n pendientes" de la
       consola.
   * - ``csma_busy_forced`` / ``csma_persist_forced``
     - Cuántas veces el piso anti-inanición de ocho ranuras forzó una
       transmisión, separando si alguna ranura vio el canal ocupado o si todas
       lo encontraron libre. Se muestra como *CSMA FORZADO (OCUP./PERSIST.)*.
       Son transmisiones, no descartes.
   * - ``tx_duty_cycle_pct`` / ``duty_cycle_limit_pct``
     - Ciclo de trabajo de transmisión medido sobre la ventana deslizante de 10
       minutos frente al techo configurado, como *CICLO DE TRABAJO TX*. El
       límite vale ``0`` cuando el limitador está apagado, mientras que la
       medición se rellena en cualquier caso.

Esto es deliberado. Con ambas funciones desactivadas (una configuración común de
solo-RX/monitor) el panel se quedaría clavado en cero por mucho tráfico que se
decodificara.

Feeds en vivo
=============

* ``/lastheard`` — la tabla LAST HEARD (JSON), alimentada tanto de RF como de
  APRS-IS. Una estación escuchada por última vez antes de que NTP sincronizara
  lleva el campo ``time`` vacío: cuando llegó la trama el reloj todavía contaba
  desde la época, así que no hay hora del día que indicar y no se inventa
  ninguna.
* ``/igate_traffic?since=<seq>`` — el delta del registro de tráfico (JSON). Cada
  entrada lleva una etiqueta de dirección (``RX``/``TX``/``DIGI``/``INET2RF``/``RX-IS``),
  el indicativo DX, el paquete crudo, el resumen de campos decodificados
  (``dec``, vacío cuando la carga no lleva ninguno), y el nivel de audio en mV
  RMS (o −1). Con *Registrar después de los filtros* activo en la página IGate,
  las entradas ``RX`` y ``RX-IS`` cubren solo el tráfico que aceptan los filtros
  de esta estación — ver :ref:`es-igate`. El
  cuerpo se transmite una entrada por fragmento HTTP, así que un cliente muy
  atrasado recibe igual todas las líneas guardadas: la respuesta no tiene tope
  de tamaño y el firmware nunca arma el documento completo en RAM. El ``seq``
  que devuelve es el número de secuencia de la última entrada realmente
  entregada, de modo que el cursor solo puede avanzar más allá de lo que el
  cliente recibió; un cursor por delante del anillo — el equipo se reinició y la
  numeración volvió a 1 — reenvía desde la entrada más antigua todavía
  guardada.
* ``/dashinfo``, ``/sidebarInfo``, ``/heapinfo`` — fragmentos compactos de info
  en vivo.

Véase :ref:`es-http-routes` para la tabla completa de rutas.

Política de bloqueo de inicio de sesión
========================================

``web_check_auth()`` lleva la cuenta de los intentos fallidos de Basic Auth por
dirección IPv4 de origen en una tabla pequeña de tamaño fijo
(``components/webconfig/web_common.c``). Solo cuenta como fallo una petición
que realmente presentó credenciales y fue rechazada — un payload Basic
malformado, o un usuario/contraseña incorrectos. Una petición sin cabecera
``Authorization``, o con una que no es ``Basic``, es la mitad del handshake de
Basic Auth que todo navegador realiza por sí solo, y se responde con ``401``
sin cargarse contra el presupuesto; esto es lo que permite que los pollers
autenticados del panel (``/dashinfo``, ``/sidebarInfo``, ``/heapinfo``,
``/lastheard``, ``/igate_traffic``) queden frente a una página de login nueva
sin disparar nunca un bloqueo por sí mismos.

Tras 5 credenciales rechazadas consecutivas desde el mismo origen, ese origen
queda bloqueado y toda petición posterior recibe ``429 Too Many Requests`` con
una cabecera ``Retry-After`` en lugar de un ``401``, durante una ventana que
empieza en 5 s y se duplica con cada nuevo intento rechazado mientras sigue
bloqueado, con un tope de 300 s. Una ventana que expira sin un login exitoso
se rearma un fallo por debajo del umbral en lugar de retomar el recuento
acumulado, de modo que un cliente que sigue reintentando las mismas
credenciales caducadas tras cada expiración solo vuelve a disparar el bloqueo
base de 5 s cada vez, en lugar de escalar directamente hasta el tope de 300 s.
Un login exitoso limpia por completo la entrada del origen.

Protección de mismo origen (CSRF)
==================================

``web_check_auth()`` también aplica una comprobación de mismo origen en toda
petición ``HTTP_POST``, independientemente de si ``g_config.http_username``
está configurado. La comprobación confirma que la cabecera ``Origin`` de la
petición (recurriendo a ``Referer`` si falta) nombra el propio ``Host`` de
este equipo antes de que se ejecute cualquier otra cosa, y falla de forma
cerrada: una petición sin ninguna de las dos cabeceras, o con una que no
coincide, se rechaza con ``403 Forbidden`` sin importar qué credenciales
lleve.

Esto es deliberadamente independiente de Basic Auth. Dejar el usuario en
blanco en la página System es una forma admitida de ejecutar el panel de
administración sin contraseña, pero solo elimina el aviso de inicio de
sesión — no relaja el requisito de mismo origen, porque una petición
entre sitios originada en el navegador es una amenaza con o sin contraseña
configurada: sin contraseña no hay credencial que robar, pero la página del
atacante puede seguir haciendo que el propio navegador del operador envíe
una petición que cambia el estado del equipo en su nombre. Toda ruta que
cambia el estado (``/ota_update``, ``/format``, ``/upload``, ``/delete``,
``/msgchat``, y el manejador de guardado de cada página de configuración)
está registrada como ``HTTP_POST`` precisamente por este motivo; ninguna
ruta ``GET`` registrada tiene efectos secundarios, así que esta
comprobación nunca tiene que actuar sobre una navegación normal, un
marcador o una URL escrita a mano.

.. seealso::

   :ref:`es-telegram` — el subsistema del bot de Telegram detrás de la página
   *Telegram*: su propio archivo de configuración, su arranque supervisado y
   su conjunto de comandos incorporados.
