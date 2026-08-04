.. _es-messaging:

===============
Mensajería APRS
===============

El componente ``message`` (``components/message/``) implementa la mensajería
APRS con acuse de recibo y reintento, y enrutamiento por RF y/o APRS-IS. La
administración web expone dos páginas distintas: la página **Message**
*configura* el motor (habilitación RF/INET, reintento, GPIO de alarma),
mientras que la página **Snd/Rcv Msg** (``/msgchat``) es la propia interfaz de
bandeja/redacción.

El motor de mensajes
====================

* **Cola en RAM.** ``s_queue[]`` guarda hasta ``MSG_QUEUE_SIZE`` (10) entradas,
  compartidas por los mensajes **recibidos y salientes** por igual — cada
  entrada lleva una bandera ``rxtx`` que dice cuál es. La cola *es* la
  conversación: un mensaje enviado y uno recibido ocupan cada uno una ranura
  propia y la conservan hasta que tráfico más nuevo los desplaza, y una vez
  ocupadas las diez ranuras, guardar el siguiente mensaje descarta la entrada
  más antigua del hilo, sea RX o TX. ``MSG_TEXT_MAX`` (200) es el tope de
  almacenamiento en memoria del texto de una entrada; el límite de protocolo al
  aire es el ``APRS_MSG_TEXT_STD_MAX`` (67 caracteres) aparte, que es contra el
  que validan el cuadro de composición y el respondedor de consultas, de modo
  que un campo de información ``":ADDRESSEE:texto{id"`` completo queda dentro
  del presupuesto TNC2 clásico de 256 bytes.
* **Orden de la conversación.** Cada mensaje guardado lleva un contador de
  inserción (``msg_entry_t::seq``) que se asigna una sola vez y no cambia, y es
  ese contador — no el reloj de pared — el que ordena el hilo y elige la entrada
  a descartar. Así, un mensaje saliente conserva el lugar en que se escribió
  mientras se lo reintenta (``last_tx`` lleva la agenda de reintentos y ``time``
  sigue siendo el momento de creación), y el orden sobrevive a un salto del
  reloj del sistema, como la primera sincronización NTP tras el arranque.
* **Enviar / ack / reintento.** ``sendAPRSMessage()`` encola un mensaje,
  ``sendAPRSAck()`` responde a uno recibido, y ``sendAPRSMessageRetry()`` —
  invocada a 1 Hz por la tarea de tick del servicio APRS — reenvía cualquier
  mensaje cuyo acuse aún no ha llegado, hasta ``msg_retry`` veces cada
  ``msg_interval`` segundos.
* **Respuesta a ``?APRSM``.** ``message_send_pending_to()`` reenvía lo que esta
  estación tiene retenido para el operador que consulta, sin gastar ninguno de
  los reintentos propios del mensaje ni mover su próximo reintento, y se detiene
  tras ``MSG_QUERY_BURST_MAX`` (3) tramas: una consulta dirigida vale un puñado
  de tramas, no una cola entera con el transmisor activo. Lo que el tope deja
  fuera sigue pendiente, así que la pasada de reintentos de arriba lo entrega
  separado un ``msg_interval``.
* **Análisis de entrantes.** ``handleIncomingAPRS()`` analiza cualquier línea
  TNC2 — de RF *o* de APRS-IS — reconoce los mensajes dirigidos a esta estación,
  responde con un ack, y reconoce los acks entrantes (``ackNNN``) para limpiar el
  mensaje encolado correspondiente. Cada mensaje aceptado es una línea nueva de
  la conversación y recibe su propia ranura, incluidos los que no traen ningún
  ``{id`` y los que repiten un número que la estación remitente ya había usado
  (la numeración se reinicia cuando esa estación se reinicia). La única línea que
  no ocupa ranura es una retransmisión que la cola ya tiene — mismo remitente,
  mismo número de mensaje, texto idéntico —, que se responde con un ack nuevo,
  porque un repetido significa que el remitente nunca escuchó el primero, sin
  aparecer dos veces en el historial. Los indicativos de origen se normalizan a
  mayúsculas al analizarlos, de modo que una estación ocupa un solo nombre en el
  hilo y un ack se empareja con el mensaje saliente que reconoce.

Enrutamiento
============

Cada mensaje se enruta por una máscara de bits de canal vía un manejador de TX
registrado:

* ``MSG_CHANNEL_RF`` (``1 << 0``) → ``aprs_service_send_tnc2()`` (pata de RF).
* ``MSG_CHANNEL_INET`` (``1 << 1``) → ``igate_send_raw()`` (pata de APRS-IS).

``g_config.msg_rf`` y ``g_config.msg_inet`` deciden qué patas están activas.

La interfaz de chat de mensajes (``/msgchat``)
==============================================

La página ``/msgchat`` presenta un panel desplazable de mensajes enviados y
recibidos por esta estación, un campo de indicativo de destino, un cuadro de
texto de mensaje (limitado a la longitud de mensaje APRS) y un botón de envío.
Refresca su lista de mensajes vía ``/msgchat/list`` (un fragmento JSON), que
devuelve el hilo guardado completo, del más antiguo al más nuevo. Está
condicionada por el interruptor de compilación ``ENABLE_MSG_CHAT``.

El panel se lee como una sola conversación, con los mensajes enviados y
recibidos intercalados en el orden en que ocurrieron y el más nuevo abajo. Su
script mide las burbujas de mensaje una vez maquetadas y dimensiona el panel a
las ``MSGCHAT_VISIBLE_MESSAGES`` (5) más nuevas, así se ven cinco mensajes sin
desplazar y el resto del hilo guardado — hasta ``MSG_QUEUE_SIZE`` (10) mensajes
— queda a un desplazamiento de distancia. La medición se repite al cambiar el
tamaño de la ventana, porque un texto más angosto se reparte en más líneas y
hace más altas las burbujas. El desplazamiento sigue a la conversación solo
mientras el operador ya está al final de ella, de modo que un mensaje que llega
nunca arranca el panel de las líneas viejas que se están leyendo.

Ambos números son constantes de compilación: ``MSGCHAT_VISIBLE_MESSAGES`` en
``page_msgchat.c`` decide únicamente qué tan alto es el panel, mientras que
``MSG_QUEUE_SIZE`` en ``message.h`` decide cuánta conversación guarda el
firmware.

.. seealso::

   :ref:`es-query` — el respondedor de consultas comparte el manejador de TX de
   este componente y se alcanza desde ``handleIncomingAPRS()`` cuando el texto de
   un mensaje dirigido empieza con ``?``.

GPIO de alarma de mensaje
=========================

Opcionalmente (``msg_alarm_enable``), un mensaje entrante puede accionar un GPIO
(``msg_alarm_gpio``; ``-1`` = deshabilitado), validado por
``message_alarm_gpio_is_valid()`` — útil para encender un LED o hacer sonar un
zumbador al recibir un mensaje.
