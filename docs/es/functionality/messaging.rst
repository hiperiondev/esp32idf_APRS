.. _es-messaging:

===============
Mensajería APRS
===============

El componente ``message`` (``components/message/``) implementa la mensajería
APRS con acuse de recibo y reintento, cifrado AES opcional, y enrutamiento por RF
y/o APRS-IS. La administración web expone dos páginas distintas: la página
**Message** *configura* el motor (habilitación RF/INET, reintento, cifrado),
mientras que la página **Snd/Rcv Msg** (``/msgchat``) es la propia interfaz de
bandeja/redacción.

El motor de mensajes
====================

* **Cola en RAM.** ``s_queue[]`` guarda hasta ``MSG_QUEUE_SIZE`` (20) entradas,
  compartidas por los mensajes **recibidos y salientes** por igual — cada
  entrada lleva una bandera ``rxtx`` que dice cuál es. ``MSG_TEXT_MAX`` (200) es
  el tope de almacenamiento en memoria del texto de una entrada; el límite de
  protocolo al aire es el ``APRS_MSG_TEXT_STD_MAX`` (67 caracteres) aparte, que
  es contra el que validan el cuadro de composición y el respondedor de
  consultas, de modo que un campo de información ``":ADDRESSEE:texto{id"``
  completo queda dentro del presupuesto TNC2 clásico de 256 bytes.
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
  mensaje encolado correspondiente.

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
Refresca su lista de mensajes vía ``/msgchat/list`` (un fragmento JSON). Está
condicionada por el interruptor de compilación ``ENABLE_MSG_CHAT``.

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
