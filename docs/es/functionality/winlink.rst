.. _es-winlink:

=====================================
Correo por radio Winlink (APRSLink)
=====================================

`APRSLink <https://winlink.org/APRSLink>`__ es la pasarela alojada en el CMS
entre APRS y el sistema mundial de correo por radio Winlink. Se llega a él con
mensajes de texto APRS normales dirigidos a un indicativo de servicio —
``WLNK-1`` — y responde con mensajes de texto APRS. No hay un protocolo aparte,
ni una capa de sesión, ni un entramado propio: en el aire, una sesión de
APRSLink se parece exactamente a una conversación con un operador humano muy
literal.

Con «pasarela Winlink» suelen quererse decir dos cosas distintas, y esta
estación hace las dos. Son independientes entre sí y se configuran por
separado, así que conviene tener claro cuál es cuál antes de tocar la página
*Winlink*.

Dos roles
=========

**Cliente.** Esta estación lleva su propia sesión, lee y escribe su propio
correo, y guarda las respuestas del servicio donde la administración web pueda
mostrarlas. Es el componente ``winlink``, el grupo *Cuenta Winlink* y la
terminal de sesión. Necesita una cuenta Winlink y su contraseña.

**Pasarela.** Una estación vecina del canal de RF local se dirige ella misma al
servicio. Sus órdenes viajan a APRS-IS por el camino RF→INET normal de esta
estación, y las respuestas del servicio vuelven por el camino INET→RF. Nada
suyo interviene: el indicativo del vecino abre su propio buzón y su propia
contraseña responde su propio desafío de acceso. Este rol es casi por completo
el IGate haciendo lo que ya hace, y toda su configuración es el único
interruptor del grupo *Pasarela para estaciones locales*.

Si el objetivo es que la gente de alrededor pueda usar Winlink, el rol que
importa es el de pasarela y el de cliente es opcional. Si el objetivo es leer
el correo propio desde la estación, es al revés.

El cliente
==========

Identidad y buzón
-----------------

El servicio abre el buzón según el **indicativo base** de quien envía la orden,
ignorando cualquier sufijo ``-SSID``, así que la cuenta alcanzada es siempre
``<INDICATIVOBASE>@winlink.org``. El indicativo que la trama saliente lleva
realmente es el del servicio de mensajes (``msgMycall``, la página *Mensaje*),
que es la razón de que *Usar el indicativo del servicio de mensajes* venga
encendido; el campo aparte existe solo para una estación cuya cuenta Winlink
está bajo otro indicativo.

Sesiones
--------

Enviar cualquier orden al servicio abre una sesión. Si la cuenta tiene el
acceso seguro activado, el servicio responde con un desafío de la forma
``Login [NNN]``, donde cada dígito es una posición de carácter en la contraseña
contando desde 1. La respuesta son esos caracteres más otros tres cualesquiera,
en cualquier orden — así la contraseña nunca viaja por el aire, y una respuesta
observada en el canal revela solo tres de sus caracteres y no de dónde salen.
Con el acceso seguro apagado no hay desafío alguno y el servicio simplemente
responde la orden; el cliente acepta eso igual de bien y pasa directo al modo
de órdenes.

Una sesión caduca del lado del servicio a las dos horas aproximadamente.
``wlSessionMaxMin`` (110 minutos por omisión) queda por debajo, de modo que
esta estación abandona la sesión un poco antes que el servicio, en lugar de
descubrir que ya no existe al enviarle una orden.

Ritmo
-----

Hay una orden pendiente por vez. La siguiente sale solo después de que el
servicio confirme la anterior, y además se exigen tres segundos de silencio
tras cualquier respuesta antes de empezar el siguiente intercambio. Es
deliberadamente pausado: ancla cada paso de la sesión a algo que el servicio
dijo de verdad y no a un temporizador local, y evita que una sesión se
convierta en una ráfaga de tramas sobre un canal compartido. Una orden sin
confirmar se retransmite dos veces y después la sesión se abandona, con el
motivo a la vista en la página.

Órdenes
-------

Todo el juego de órdenes de APRSLink es texto, así que el campo *Orden* admite
cualquiera de ellas directamente. Las más frecuentes son:

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Orden
     - Significado
   * - ``L``
     - Lista los mensajes pendientes. El servicio devuelve hasta cinco.
   * - ``R<n>``
     - Lee el mensaje *n* tal como apareció en la lista.
   * - ``Y<n>``
     - Empieza una respuesta al mensaje *n*.
   * - ``F<n> <destinatario>``
     - Reenvía el mensaje *n* a otra dirección.
   * - ``K<n>``
     - Borra el mensaje *n*.
   * - ``SP <destinatario> <asunto>``
     - Empieza un mensaje. Todo lo enviado después es el cuerpo.
   * - ``/EX``
     - Termina el cuerpo y entrega el mensaje al servicio.
   * - ``A <alias>=<dirección>``
     - Crea o actualiza un alias. ``A <alias>=`` lo borra, ``AL`` los lista.
   * - ``B``
     - Sale de la sesión.
   * - ``?``
     - Ayuda. ``?L`` da la ayuda de una orden concreta.

Los botones *Empezar mensaje* / *Agregar línea* / *Enviar mensaje* recorren la
secuencia ``SP`` … ``/EX``, la única cuyo orden importa, y no hacen nada que el
campo de órdenes no pueda hacer también a mano.

Por dónde sale el tráfico
-------------------------

``wlInetOnly`` viene encendido y mantiene fuera del aire el tráfico Winlink de
esta estación. El razonamiento es que esta estación *es* un IGate: una orden
que transmita ocupa el canal local con tráfico que ninguna estación de ese
canal necesita oír, y la respuesta del servicio llega por el mismo enlace de
Internet de todas formas. El interruptor solo puede quitar la pata de RF — con
*Enviar/recibir por Internet* de la mensajería apagado no hay nada a lo que
recurrir, y la orden sale por RF como antes, que es justo lo que quiere una
estación sin enlace a APRS-IS.

Que le avisen del correo pendiente
----------------------------------

APRSLink mira los comentarios de los informes de posición que ve buscando la
palabra ``winlink`` y la usa para decidir a qué estaciones avisar, sin que se
lo pidan, cuando tienen correo esperando. *Anunciar esta estación como lectora
de Winlink* agrega esa palabra al comentario de la baliza. Se agrega, no
sustituye, y solo mientras siga entrando, así que un comentario que ya llena el
campo conserva cada carácter escrito — y la página lo dice si no queda sitio.

*Consultar el correo cada* es la otra mitad de la misma idea y no depende de
que el servicio se dé cuenta de nada: abre una sesión y envía ``L`` a intervalo
fijo. Solo actúa desde el reposo, así que nunca interrumpe una sesión que está
haciendo algo.

Respuestas guardadas
--------------------

Todo lo que el servicio devuelve se guarda en ``/storage/winlink.json``, de lo
más antiguo a lo más reciente, hasta las últimas 24 respuestas. Borrarlas desde
la página elimina ese archivo y nada más — los ajustes de la cuenta viven en
``config.json`` y quedan intactos.

La pasarela
===========

Un IGate no pone en el aire todos los mensajes que lee de APRS-IS. El pase de
mensajes (``main/aprs_service.c``, ``messageGatePass()``) exige cuatro cosas de
un mensaje antes de transmitirlo:

* el destinatario fue oído por RF dentro de ``igateLocalWindow`` segundos;
* fue oído a no más de ``igateMsgMaxHops`` saltos de digipetidor;
* el destinatario **no** fue oído en APRS-IS dentro de la misma ventana;
* el remitente no fue oído por RF él mismo.

Una respuesta del servicio Winlink cumple la primera, la segunda y la cuarta
condiciones de forma natural. La tercera es el problema: esta estación pasa la
orden del vecino a APRS-IS, el servidor la devuelve como eco por el flujo, y
ese eco puede bastar para que el vecino parezca conectado a Internet — momento
en el que la respuesta que estaba esperando se descarta como
``DROP_MSG_ADDRESSEE_INET`` y la sesión simplemente se atasca sin que nada
parezca ir mal.

*Dejar que las respuestas del servicio lleguen por RF a las estaciones locales*
(``wlGateExempt``, encendido por omisión) levanta esa única condición, y solo
para los mensajes cuyo remitente sea el indicativo de servicio configurado. La
premisa en la que se apoya la regla — que un destinatario que está en APRS-IS
puede leer el mensaje allí por su cuenta — es falsa por construcción para una
respuesta de APRSLink: existe únicamente porque el destinatario se la pidió a
la pasarela de esta estación, y tiene exactamente un camino de entrega. Las
demás condiciones, las comprobaciones de cabecera que mantienen el tráfico
``TCPXX``/``NOGATE``/``RFONLY`` fuera del aire y el descarte de
boletines/difusiones siguen aplicándose sin cambios.

Los tres ajustes del IGate que consulta el pase se muestran de solo lectura en
la página *Winlink*, junto a ese interruptor, para poder ver de un vistazo las
cuatro entradas de la decisión. Se editan en la página *IGate*.
``igateLocalWindow`` merece una mirada en particular: el servicio puede tardar
un minuto o más en responder una lectura, y una ventana más corta que ese viaje
de ida y vuelta descartará la respuesta por el primer motivo y no por el
tercero.

Configuración
=============

Cada ajuste es una clave ``wl*`` de ``config.json`` — véase
:ref:`es-config-json`. La contraseña se guarda allí en claro, exactamente igual
que ya ocurre con el passcode de APRS-IS; quien pueda leer la partición de
almacenamiento puede leer las dos.
