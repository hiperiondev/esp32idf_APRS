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
       tabla de tráfico en vivo (DX / PACKET / AUDIO) alimentada por long-poll
       basado en secuencia.
   * - **Station**
     - La identidad compartida de la propia estación que leen cada baliza,
       objeto y mensaje: indicativo, latitud, longitud, altitud
       (``g_config.my_*``), más las dos opciones de precisión al aire de toda la
       estación: ambigüedad de posición y prefijo de localizador Maidenhead en
       los reportes de estado.
   * - **IGate**
     - Habilitar, RF→INET / INET→RF, ambas máscaras de filtro, budlist y guardas
       de rango/prefijo, indicativo/SSID/passcode, host/puerto, cadena de filtro
       de servidor, baliza on/off, posición, intervalo, selector de símbolo,
       objeto, comentario, estado, PHG.
   * - **Digi**
     - Habilitar digipeater, indicativo/SSID y ajustes de baliza (posición,
       símbolo, intervalo, comentario, estado, ruta). La ventana de supresión de
       duplicados es un único control, en la página *IGate*.
   * - **Tracker**
     - Habilitar tracker, indicativo/SSID, intervalo fijo, posición, símbolo de
       estación, comentario, opciones de posición comprimida, posición Mic-E y
       altitud.
   * - **Weather**
     - Habilitar, enviar-por-RF/-INET, marca de tiempo, indicativo/SSID/ruta WX,
       posición, nombre de objeto, comentario, casillas *Averaged* por campo, y
       — por cada campo WX al aire — un **desplegable de canal** rellenado en
       vivo desde el registro ``sensors_local`` y filtrado por las capacidades
       publicadas de cada controlador. Valores en vivo vía ``/wx/values``.
   * - **Telemetry**
     - Parámetros de baliza/informe, conmutadores de mensajes de definición,
       analógicos A1–A5 con selectores de origen y calibración, digitales B1–B8
       con selectores de origen y sentido. Valores en vivo vía ``/tlm/values``.
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
       canal).
   * - **Radio / Modem**
     - Modo FX.25 (apagado / solo RX / RX+TX); habilitar módem de audio,
       modulación (300 / 1200 Bell202 / 1200 V.23 / 9600 G3RUH), LPF de audio
       (audio plano), ms de preámbulo, ms de ranura de tiempo TX, buffers TX,
       retención extra de des-activación de PTT, persistencia CSMA; y el botón
       **LOOP TEST**. Guardar reaplica el módem en vivo — sin reinicio.
   * - **Wireless**
     - Modo (off/STA/AP/AP+STA), SSID/pass/canal del AP, 5 ranuras STA cada una
       con su propia casilla Enable, potencia TX en dBm, más un escaneo en vivo.
   * - **System**
     - Login web, frecuencia de CPU (aplicada en vivo), hosts NTP ×3, intervalo
       de resincronización, y los cuatro presets de ruta compartidos
       ``path[0..3]``.
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

Esto es deliberado. Con ambas funciones desactivadas (una configuración común de
solo-RX/monitor) el panel se quedaría clavado en cero por mucho tráfico que se
decodificara.

Feeds en vivo
=============

* ``/lastheard`` — la tabla LAST HEARD (JSON), alimentada tanto de RF como de
  APRS-IS.
* ``/igate_traffic?since=<seq>`` — el delta del registro de tráfico (JSON). Cada
  entrada lleva una etiqueta de dirección (``RX``/``TX``/``DIGI``/``INET2RF``/``RX-IS``),
  el indicativo DX, el paquete crudo, y el nivel de audio en mV RMS (o −1).
* ``/dashinfo``, ``/sidebarInfo``, ``/heapinfo`` — fragmentos compactos de info
  en vivo.

Véase :ref:`es-http-routes` para la tabla completa de rutas.
