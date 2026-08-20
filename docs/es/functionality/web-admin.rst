.. _es-web-admin:

==================
Administración web
==================

El componente ``webconfig`` (``components/webconfig/``) es una administración
basada en ``esp_http_server`` construida con un archivo por página
(``pages/*.c``), una tabla de rutas (``web_server.c``) y un conjunto de ayudantes
compartidos (``web_common.c``). Usa **autenticación HTTP Basic** contra
``g_config.http_username`` / ``http_password`` en cada página — con la única
excepción del ``/style.css`` estático, que no lleva datos de configuración ni de
tráfico —, además de coincidencia de URI con comodines, una pila de manejador de
20 KB y purga LRU.

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
       desde ``GET /gps/live`` una vez por segundo mientras está marcado.
   * - **IGate**
     - Habilitar, RF→INET / INET→RF, ambas máscaras de filtro, budlist y guardas
       de rango/prefijo, indicativo/SSID/passcode, cuatro recuadros *APRS-IS
       Server* (cada uno con casilla Habilitar más host y puerto, usados como
       rotación de failover), cadena de filtro
       de servidor, nueve casillas de tipo de carga por dirección (la novena,
       *Otros*, cubre capacidades de estación, formatos definidos por el
       usuario, radiogoniometría Agrelo, balizas de localizador Maidenhead y el
       elemento de mapa reservado), baliza on/off, posición, intervalo, selector de símbolo,
       objeto, comentario, estado, PHG. *Filtrado de Mensajes* lleva el
       interruptor de criterios de mensajes INET→RF, el límite de saltos del
       destinatario y la ventana de escucha
       local. La posición puede escribirse, reflejar *Usar Datos de Mi
       Estación* o tomarse en vivo del receptor GNSS mediante *Usar GPS*; las
       tres opciones son mutuamente excluyentes.
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
       (con su selector de comentario de posición) y altitud. La posición fija
       puede escribirse, reflejar *Usar Datos de Mi Estación* o tomarse en
       vivo del receptor GNSS mediante *Usar GPS*; las tres opciones son
       mutuamente excluyentes.
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
       vivo de solo lectura del receptor: estado del
       enlace, estado de navegación, calidad del fix y modo 2D/3D, posición,
       altitud y separación del geoide, velocidad sobre el suelo, rumbo y
       variación magnética, fecha y hora UTC, satélites usados y a la vista,
       HDOP/PDOP/VDOP, los contadores de sentencias aceptadas y descartadas y
       la antigüedad de la última sentencia y del último fix. El puerto serie
       y sus pines son cableado de placa fijado en compilación y se muestran
       como texto. Valores en vivo vía ``/gps/values``, sondeado cada segundo.
       Su contraparte numérica, ``/gps/live``, es la que consulta la casilla
       *Usar GPS* de cada otra página para autocompletar sus propios campos de
       posición/movimiento (Station, IGate, Digi, Tracker, Weather).
   * - **Bulletins**
     - Hasta cinco boletines (identificador y grupo de destinatario, texto,
       RF/INET, intervalo, caducidad).
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
  RMS (o −1). El
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
