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

**Una línea por minuto.** ``CONFIG_APRS_HEAP_REPORT`` viene activado y emite una
línea cada ``CONFIG_APRS_HEAP_REPORT_PERIOD_S`` segundos::

   I (3600123) heap_monitor: free=104512 largest=45056 minimum=41216

Tres cifras, que se leen juntas. El tamaño libre es cuánta memoria existe; el
mayor bloque libre es la mayor asignación individual todavía posible, y que
ambas se separen es un heap fragmentándose y no consumiéndose; el mínimo es la
marca de agua desde el arranque, así que un bajón que se recuperó antes de la
línea siguiente igual aparece ahí. Se informan sobre memoria interna de 8 bits,
la misma clase que imprime el transporte al fallar, de modo que los dos tipos de
línea se pueden leer uno contra otro. La línea cuesta tres consultas al
asignador por período y nada de memoria.

**Corchetes alrededor de los handshakes.** ``CONFIG_TELEGRAM_BOT_HEAP_BRACKET``,
bajo ``Telegram bot transport``, registra esas mismas dos cifras inmediatamente
antes e inmediatamente después de cada petición del bot: cada intento de una
llamada JSON, más la subida multipart y la descarga de archivos, que abren
conexiones propias. Un handshake TLS pide sus búferes de registro como
asignaciones individuales de unos pocos kilobytes, así que es el mayor evento
individual que este firmware ejecuta contra el heap. Si las muescas de la traza
minuto a minuto caen dentro de estos corchetes, son los handshakes los que
mueven el heap; si caen entre ellos, lo mueve otra cosa. Desactivado por
defecto: son dos líneas por llamada a la API y la ruta de sondeo hace una cada
pocos segundos.

**Atribuir la memoria a una tarea.** Activa ``CONFIG_HEAP_TASK_TRACKING``
(``Component config`` → ``Heap memory debugging``) y aparece
``CONFIG_APRS_HEAP_REPORT_TASKS``, que agrega la tabla de resumen por tarea
debajo de cada línea de heap, de modo que una fuga real nombra a su dueño en un
solo volcado en vez de una semana de bisección. El seguimiento cuesta RAM por
asignación viva y ralentiza cada asignación y cada liberación, así que
corresponde a una compilación de diagnóstico: vuelve a apagarlo después.

**Descartar corrupción.** ``CONFIG_APRS_HEAP_INTEGRITY_CHECK`` barre todos los
heaps cada ``CONFIG_APRS_HEAP_INTEGRITY_PERIOD_S`` segundos y registra un error,
a continuación de las direcciones que imprime el propio verificador, si algo
está mal. Las estructuras corruptas del asignador se presentan como conducta
inexplicable del heap y si no se persiguen como una fuga. El barrido sostiene el
lock de cada heap mientras lo recorre, así que otras tareas se bloquean si
asignan mientras tanto: de ahí que venga apagado y, encendido, con un
temporizador lento. Lo que puede ver depende del nivel de detección de
corrupción: con el valor por defecto (sin envenenamiento) solo se verifican las
estructuras propias del asignador; elige "Light impact" o "Comprehensive" para
verificar además los bytes canario alrededor de cada bloque asignado.

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
