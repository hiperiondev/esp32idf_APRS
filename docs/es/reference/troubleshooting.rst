.. _es-troubleshooting:

=======================
Resolución de problemas
=======================

"Cambié a modo Station, guardé, reinicié, y no pasa nada."
==========================================================

Lee el log de arranque — esta ruta está muy instrumentada:

* ``esp_wifi_connect()`` solo es legal una vez que la estación ha arrancado
  *realmente* (``WIFI_EVENT_STA_START``). La conexión se emite desde ese
  manejador y cada intento registra su resultado.
* Si ninguna ranura de Cliente Wi-Fi está **habilitada con un SSID**, el firmware
  vuelca cada ranura y te dice cuál es el error ("habilitada, pero el SSID está
  VACÍO" vs "tiene un SSID, pero 'Enable' no está marcado").
* Solo-STA sin nada a lo que unirse recurre a AP+STA para que la administración
  web siga arriba.

Los códigos de razón de desconexión se registran:

.. list-table::
   :header-rows: 1
   :widths: 44 56

   * - Razón
     - Significado
   * - 15, 204
     - contraseña equivocada
   * - 201
     - SSID no visible: nombre equivocado, fuera de rango, o solo 5 GHz
   * - 2 / 8 / 200
     - roaming ordinario / caídas del lado del AP

"El AP no asocia en absoluto."
==============================

Un ``wifi_config_t`` a cero deja ``pmf_cfg.capable = false``, y los AP
WPA3 / WPA2-con-PMF-requerido rechazan tal estación. El firmware pone *capable,
no required*, que funciona contra AP antiguos y nuevos.

"El arranque se cuelga ~5 segundos."
====================================

Esperado: ``modem_init()`` se bloquea mientras ``ModemCalibrateSampleRate()`` mide
el reloj real del ADC. Una vez por arranque.

"Las balizas al arrancar no transmiten."
========================================

Esperado: ``aprs_service_start()`` corre antes de ``modem_init()``, así que las
balizas tempranas se descartan con un log de depuración hasta ``s_modemReady``.

"El LOOP TEST falla con 'no se recibió paquete de vuelta'."
===========================================================

Comprueba la atenuación del ADC: el DAC oscila el raíl completo mientras una
atenuación de 0 dB solo mide ~0–1,1 V, recortando el tono más allá de la capacidad
del demodulador para enganchar. El componente codifica ``ADC_ATTEN_DB_12``, que es
correcto; si lo sobreescribiste, restáuralo. Confirma también el cable de bucle
GPIO25 → GPIO33.

"El IGate dice unverified."
===========================

``aprs_mycall`` / ``aprs_passcode`` equivocados. El banner se registra; también la
línea de login exacta, incluida la cadena de filtro, así que un filtro mal formado
es visible de inmediato.

"Todo funciona pero aprs.fi no muestra mi estación."
====================================================

Balizas: habilita la baliza de posición y al menos una de ``loc2rf`` /
``loc2inet``, y pon coordenadas reales. Retransmitir tráfico nunca te anuncia a ti.

"9600 Bd pierde tramas."
========================

Esa es la patología que la tasa del ADC, el tamaño de la trama de conversión y la
separación de núcleos se cambiaron para arreglar (véase :ref:`es-dsp-signal-chain`).
Si sobreescribiste ``MODEM_ADC_SAMPLERATE``, ``MODEM_ADC_CONV_FRAME``,
``MODEM_DAC_TIMER_CORE`` o ``MODEM_ADC_ISR_CORE``, reviértelos. Confirma también que
estás alimentando audio **plano/de discriminador**.

"El LED de PTT se queda encendido en reposo."
=============================================

La lógica de PTT es correcta; su polaridad es una constante de compilación, y la
definición de placa que se distribuye es ``MODEM_PTT_ACTIVE_HIGH=1``
(activo-alto) en el ``CMakeLists.txt`` de nivel superior. Activo-alto significa
que reposo/sin-activar acciona el pin **bajo** y activado lo acciona alto;
activo-bajo es la imagen espejo, así que en reposo el pin queda alto y un LED en
ese pin se queda encendido. Si el LED sigue lo contrario de lo que esperas, tu
etapa de excitación invierte (un optoacoplador sí; un simple NPN de lado bajo
no): cambia la macro al otro valor y haz una recompilación limpia completa — el
valor queda horneado en ``afsk.c``, así que una compilación incremental no lo
tomará.
"Telegram deja de responder tras un rato funcionando, con 'mbedtls_ssl_fetch_input' o 'Socket is not connected' en el log."
=============================================================================================================================

El camino de sondeo mantiene abierta su conexión HTTPS con la API de Telegram
entre ciclos, para que un *long poll* que no devuelve nada no pague un nuevo
*handshake* TLS cada diez segundos. Si esa conexión queda inactiva el tiempo
suficiente, el extremo remoto o un NAT intermedio puede cerrarla en silencio;
el socket queda entonces obsoleto aunque nada localmente lo haya notado.
``telegram_bot_client_call()`` trata un fallo de transporte como señal
exactamente de eso: cierra la conexión a la fuerza y reintenta la solicitud
sobre un socket recién abierto, hasta tres intentos en total con una espera
que crece entre ellos, de modo que una única sesión obsoleta se recupera sola
dentro de la misma llamada. Si el error sigue repitiéndose en todos los
intentos, lo que falla es la red y no un socket puntual; revisa la
conectividad Wi-Fi/Internet y el token del bot.

"sendMessage falla con 'ESP_ERR_HTTP_CONNECT' justo después de llegar una actualización, precedido de 'Dynamic Impl: alloc(...) failed'."
=========================================================================================================================================

Un *handshake* TLS nuevo pide a la memoria heap sus búferes de registro como
asignaciones únicas de unos pocos kilobytes cada una, así que lo que decide
si sale adelante es el mayor bloque **contiguo** libre, no el total libre. El
asignador del ESP-IDF registra el rechazo como ``Dynamic Impl: alloc(...)
failed``, mbedTLS lo convierte en ``mbedtls_ssl_handshake returned -0x008D``
y el transporte ve ``ESP_ERR_HTTP_CONNECT``.

Una sesión TLS viva retiene un bloque de tamaño comparable mientras se
mantiene, así que el firmware nunca sostiene dos a la vez. El manejador de
transmisión funciona sin *keep-alive* y por tanto queda vacío en cuanto
retorna la llamada, y la conexión de sondeo la libera
``telegram_release_poll_connection()`` justo antes de cualquier solicitud
saliente, que es el momento que importa: una respuesta se envía justo después
de llegar un lote de actualizaciones, con la carga útil y el árbol
decodificado todavía en memoria. El sondeo paga un *handshake* extra en su
ciclo siguiente y nada más.

Cada intento fallido se registra con la memoria heap libre y el mayor bloque
libre en ese instante. Si el mayor bloque está holgadamente por encima de
cuatro kilobytes y la llamada aún así falla, el problema es el enlace y no la
memoria. Si no lo está, al dispositivo le falta realmente memoria contigua:
reduce ``rx_buffer_size`` en los manejadores del cliente, o reduce lo que el
resto del firmware retiene en ese momento.

``CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN`` no es la palanca que parece. Es un tope
sobre el registro que el otro extremo puede enviar, y la cadena de
certificados que presenta Telegram ronda los cuatro kilobytes en un solo
registro, así que bajarlo por debajo de esa cifra no ahorra memoria: hace que
el *handshake* falle de plano, en todos los intentos y con cualquier estado
de la memoria.

``CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA`` sí lo es, y no sale gratis. Con
``CONFIG_MBEDTLS_DYNAMIC_BUFFER`` ya activo, libera las estructuras de
certificado y clave ya parseadas en cuanto termina el *handshake*, en vez de
retenerlas mientras dure la sesión: son unos pocos kilobytes recuperados en una
placa cuyo pozo principal corre con un margen de un solo dígito de kilobytes.
Además selecciona ``CONFIG_MBEDTLS_DYNAMIC_FREE_CA_CERT`` por defecto, y ahí
está la trampa: un objeto de sesión que tenga que rehacer el *handshake*
necesita que se le vuelva a registrar la CA antes. Este firmware construye una
conexión TLS nueva para cada una, así que aquí nada reutiliza una sesión entre
*handshakes*, pero el síntoma a vigilar es una primera petición que funciona
seguida de otras que fallan en el *handshake* en vez de al azar. Si aparece,
desactiva la opción antes de mirar en ningún otro sitio.

"Esta mañana el heap libre está más bajo que anoche y el log no dice nada al respecto."
=======================================================================================

Cualquier otra cifra de heap que registre este firmware se imprime *después* de
un fallo: las dos líneas del transporte de Telegram, el detalle de diagnóstico
que acompaña a un arranque fallido, la ruta de error de escritura de los
almacenes JSON. Describen el heap una vez hecho el daño y no dicen nada de cómo
se llegó ahí. Una marca de agua que cayó a las tres de la madrugada no deja,
por tanto, ningún rastro de lo que estaba corriendo.

La instrumentación permanente que responde a esa pregunta vive en
``main/heap_monitor.c``, la acciona el tick de 1 Hz del servicio APRS y se
configura bajo ``APRS heap instrumentation`` en ``idf.py menuconfig``.

**Una línea por período.** ``CONFIG_APRS_HEAP_REPORT`` está activa por defecto
y emite una línea cada ``CONFIG_APRS_HEAP_REPORT_PERIOD_S`` segundos::

   I (3600123) heap_monitor: free=104512 largest=45056 min_sum=41216

Tres cifras que se leen juntas. El tamaño libre es cuánta memoria existe; el
bloque libre más grande es la mayor asignación individual todavía posible, y que
ambas cifras se separen indica un heap que se fragmenta en vez de consumirse; el
mínimo es la marca de agua más baja desde el arranque, así que una caída que se
recuperó antes de la línea siguiente sigue apareciendo ahí. Las tres se leen
para memoria interna de 8 bits, la misma clase que imprime el transporte al
fallar, de modo que ambos tipos de línea se pueden contrastar. La línea cuesta
tres consultas al asignador por período y nada de memoria.

El período vale diez segundos por defecto porque tiene que ser más corto que los
eventos que pretende capturar, y en este firmware son cortos: un *handshake*
TLS, una reconexión a APRS-IS o un guardado de ajustes alcanzan su pico y se
recuperan en pocos segundos. Un muestreador más lento que eso es
estructuralmente ciego a todos ellos — registra el heap antes y después, nunca
durante, y cada línea que imprime es compatible con un pico que nunca ocurrió.
El refresco a 1 Hz del panel es aún más fino, pero solo existe mientras hay un
navegador abierto en la página, que no es cuando ocurren el arranque ni una
reconexión desatendida.

.. warning::

   ``min_sum`` es una suma de mínimos, no el mínimo de la suma. El asignador
   mantiene una marca de agua por cada heap registrado y esta cifra las suma,
   tomando cada término en el peor instante de *ese* heap. Un ESP32 sin PSRAM no
   tiene un único heap DRAM: las reservas de la ROM y del PHY parten la DRAM
   interna en tres o cuatro regiones no contiguas, cada una registrada por
   separado.

   Como los términos son independientes, la suma es una *cota inferior* de lo
   más bajo que ha llegado a estar realmente el total de heap libre, y la cota
   se afloja a medida que crece el número de heaps. Por eso un ``min_sum``
   diminuto admite dos lecturas que esta línea por sí sola no distingue: que
   todas las regiones estuvieran casi vacías en un mismo momento, o que cada
   región tocara fondo por separado en un momento propio y el total nunca
   corriera peligro. Ambas importan — una región atascada cerca de cero es un
   fallo real de fragmentación, porque ``heap_caps_malloc()`` la salta desde ese
   momento y el asignador se comporta como si no existiera — pero exigen
   trabajos distintos. Resuelve cuál de las dos tienes antes de tocar ninguna
   asignación, con el desglose de abajo.

**Qué heap se quedó realmente sin memoria.**
``CONFIG_APRS_HEAP_REPORT_PER_HEAP`` acompaña cada línea con la tabla que
imprime el propio componente de heap: una fila por heap registrado, con su
dirección de inicio, tamaño, libre actual, bloque libre más grande y mínimo
libre histórico. Esto es lo que separa las dos lecturas anteriores. Si el
mínimo del pozo principal está en decenas de kilobytes y solo las regiones
D/IRAM pequeñas marcan casi cero, el resumen era un artefacto de la suma de
mínimos; si el mínimo del pozo principal también está cerca de cero, la
estación estuvo de verdad a punto de quedarse sin memoria. Desactivada por
defecto porque son varias líneas por período: actívala para la ejecución que
responda la pregunta y desactívala después.

**Corchetes alrededor de los caminos pesados.** ``CONFIG_APRS_HEAP_BRACKET``
registra el heap libre y el bloque libre más grande justo antes y justo después
de cada pasaje que se sabe que da un mordisco grande o duradero: el arranque de
Telegram, la conexión a APRS-IS, la carga de la configuración, el anillo del
espejo de consola, el escaneo WiFi, la subida OTA y cada búfer de formulario
web. ``CONFIG_TELEGRAM_BOT_HEAP_BRACKET``, bajo ``Telegram bot transport``, hace
lo mismo con cada petición del bot — cada intento de una llamada JSON, más la
subida multiparte y la descarga de fichero, que abren conexiones propias. Ambos
se anidan, llevan las mismas cifras en la misma clase de memoria y están
pensados para activarse juntos.

La línea periódica dice *cuándo* se movió el heap; un corchete dice *qué* lo
movió, porque sus cifras se toman a ambos lados de un pasaje con nombre y no en
el instante en que tocaba el período. Si las muescas de la traza periódica caen
dentro de un corchete, ese pasaje es lo que mueve el heap; si caen entre
corchetes, lo mueve otra cosa. Ambos están desactivados por defecto: son dos
líneas por evento y el camino de *polling* genera uno cada pocos segundos.

**Atribuir la memoria a una tarea.** Activa ``CONFIG_HEAP_TASK_TRACKING``
(``Component config`` → ``Heap memory debugging``) y aparecerá
``CONFIG_APRS_HEAP_REPORT_TASKS``, que añade la tabla resumen por tarea bajo
cada línea de heap, de modo que una fuga real nombra a su dueño en un solo
volcado en vez de en una semana de bisección. El seguimiento cuesta RAM por cada
asignación viva y ralentiza cada asignación y liberación, así que pertenece a
una compilación de diagnóstico: desactívalo después.

**Margen de pila.** ``CONFIG_APRS_STACK_REPORT`` está activa por defecto y emite
una línea por tarea cada ``CONFIG_APRS_STACK_REPORT_PERIOD_S`` segundos (una
hora) con la marca de agua de pila de esa tarea::

   I (3600130) heap_monitor: stack igate_task: 2712 bytes free at its worst
   I (3600131) heap_monitor: stack wifi: 1544 bytes free at its worst

Las pilas de tarea son el mayor bloque individual de RAM que reserva este
firmware y el que nadie mide: cada tamaño de pila del proyecto es un
presupuesto fijado con margen deliberado, no una cifra ajustada a lo que la
tarea resultó necesitar. Sin esta línea, la única forma en que una pila avisa de
que es pequeña es desbordándose, y no hay forma alguna de enterarse de que otra
es enormemente grande. Una marca de agua solo baja, así que no se pierde nada
entre líneas: cada una informa de lo peor que ha visto esa tarea desde que
arrancó. Solo aparecen las tareas vivas: una que el operador haya apagado
simplemente no tiene fila esa hora.

**Descartar la corrupción.** ``CONFIG_APRS_HEAP_INTEGRITY_CHECK`` barre todos
los heaps cada ``CONFIG_APRS_HEAP_INTEGRITY_PERIOD_S`` segundos y registra un
error, después de las direcciones que imprime el propio verificador, si algo va
mal. Las estructuras del asignador corrompidas se presentan como comportamiento
inexplicable del heap y si no se persiguen como una fuga. El barrido mantiene el
cerrojo de cada heap mientras lo recorre, así que otras tareas se bloquean si
asignan mientras tanto: de ahí que esté desactivado por defecto y con un
temporizador lento cuando se activa. Lo que puede ver depende del nivel de
detección de corrupción: sin envenenamiento solo se verifican las estructuras
propias del asignador, así que elige "Light impact" o "Comprehensive" en
``Heap memory debugging`` para verificar además los bytes canario alrededor de
cada bloque asignado. Sin eso, una marca de agua inverosímil no se puede
distinguir de una corrompida.

**Capturar un desbordamiento de pila donde ocurre.**
``CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK`` (``Component config`` →
``FreeRTOS`` → ``Port``) apunta el último watchpoint de hardware a los 32 bytes
finales de la pila de la tarea en curso, de modo que un desbordamiento produce
un panic en la instrucción que lo causó. La comprobación de canario que viene
por defecto solo corre en un cambio de contexto, lo que informa del daño mucho
después de que el código responsable haya retornado; y si el desbordamiento es
una variable local grande que salta por encima de la zona del canario, no se
informa nunca y aparece más tarde como corrupción sin relación. Eso importa
cuando un crash cae en un sitio imposible, como el planificador sin encontrar
ninguna tarea ejecutable. El coste es un watchpoint menos bajo gdb y hasta 60
bytes menos en cada pila de tarea, así que pertenece a una compilación de
diagnóstico junto con las opciones anteriores. Solo captura escrituras que
caigan dentro de esos últimos 32 bytes.

**Serializar las dos operaciones de red más pesadas.** El mismo módulo también
tiene un pequeño cerrojo no bloqueante, independiente del muestreo anterior y
presente siempre, sin importar qué opciones ``CONFIG_APRS_HEAP_*`` estén
activas. El handshake TLS del bot de Telegram (:ref:`es-telegram`) y el
``connect()`` TCP del enlace APRS-IS (:ref:`es-igate`) lo toman cada uno
alrededor de su propia comprobación de mínimo de heap y lo liberan en cuanto
termina ese trabajo de arranque, de modo que nunca se ejecutan en el mismo
instante compitiendo por la misma memoria contigua. Quien encuentra el cerrojo
ya tomado simplemente lo reintenta en su propio ciclo — nada aquí se bloquea
esperando al otro lado.

"Los botones del menú siguen girando y el log muestra 'query is too old and response timeout expired or query ID is invalid'."
==============================================================================================================================

Telegram invalida una *callback query* pocos segundos después de pulsar el
botón. Responderla es una solicitud en sí misma, y en este dispositivo una
solicitud puede costar un *handshake* TLS, así que el orden en que se hace el
trabajo decide si la respuesta llega todavía a tiempo.

Tres cosas la mantienen dentro del plazo. La consulta se responde antes de
ejecutar el manejador del botón, no después, así que construir y enviar un
informe nunca retrasa la respuesta. La conexión de transmisión permanece
abierta durante un lote de actualizaciones, así que una ráfaga de pulsaciones
paga un *handshake* entre todas en vez de uno cada una. Y un único ciclo de
sondeo fallido ya no añade su propia pausa de cinco segundos encima de los
reintentos que el transporte ya gastó, porque esa pausa es tiempo que las
consultas en cola pasan envejeciendo; la pausa vuelve en cuanto los fallos se
repiten, que es cuando la red está realmente caída.

Una consulta genuinamente vencida la rechaza Telegram con un 400 y el mensaje
de arriba, y el lote al que pertenecía se procesa igualmente. Si esto aparece
una vez tras un fallo de sondeo o una reconexión, la cola simplemente
sobrevivió a sus actualizaciones. Si aparece de forma sostenida, el
dispositivo no está siguiendo el ritmo del sondeo: busca los fallos de sondeo
por encima en el log.

"Las solicitudes fallan al azar con 'mbedtls_ssl_handshake returned -0x2700', con memoria heap de sobra."
=========================================================================================================

``-0x2700`` es ``MBEDTLS_ERR_X509_CERT_VERIFY_FAILED``: el *handshake* TLS
llegó al servidor, intercambió mensajes y después rechazó el certificado que
le mostraron. Ni el enlace ni la memoria tenían nada malo, y por eso las
cifras impresas junto al fallo se ven sanas.

A ``api.telegram.org`` lo atiende más de un *front-end* y no todos encadenan
a la misma autoridad certificadora. Cuando el transporte valida contra un
archivo PEM en vez del *bundle* del ESP-IDF, ese archivo solo confía en las
autoridades que realmente lleva, así que un archivo con una única raíz valida
las conexiones que caen en un *front-end* compatible y falla las demás. Qué
*front-end* entrega el DNS varía entre intentos, que es exactamente por qué
el fallo parece aleatorio y por qué un reintento suele funcionar.

El transporte lo informa de forma explícita. Una cadena rechazada se registra
como ``Peer certificate refused, verification flags 0x…, validating against
<ruta>``, y el arranque registra cuántos anclajes de confianza dio el archivo
(``Loaded N trust anchors from …``). Un solo anclaje con ``-0x2700``
intermitente es la firma de este problema.

Hay dos soluciones. Concatenar las raíces que faltan en el archivo PEM: cada
certificado que contenga pasa a ser un anclaje de confianza, y el archivo se
puede reemplazar desde la página File Storage del admin web sin recompilar. O
seleccionar ``TELEGRAM_BOT_CERT_BUNDLE`` en menuconfig y validar contra el
*bundle* de certificados que trae ESP-IDF, que cubre las autoridades públicas
y sigue funcionando cuando Telegram rota su cadena, a costa de llevar el
*bundle* en la imagen.

Ten en cuenta que ``CONFIG_MBEDTLS_HAVE_TIME_DATE`` no está habilitado en
este firmware, así que no se comprueban las fechas de validez de los
certificados. Un reloj sin sincronizar nunca es aquí la causa de ``-0x2700``.
