.. _es-brandmeister:

======================================
Interconexión BrandMeister (sin DMR)
======================================

Esta estación puede intercambiar tráfico APRS con la red DMR BrandMeister por
Internet sin implementar ninguna parte de DMR. No hay enlace Homebrew/MMDVM, ni
OpenBridge, ni cuenta de BrandMeister, ni contraseña de máster.

Por qué no hace falta ningún enlace DMR
=======================================

El lado APRS de BrandMeister **es un cliente APRS-IS**. Cada máster de
BrandMeister corre un proceso de pasarela que inicia sesión en un servidor
APRS-IS público con una línea ``user``/``pass``/``filter`` común e inyecta como
líneas TNC2 corrientes el tráfico de posición, telemetría y mensajes originado
en DMR. En el otro sentido se suscribe a APRS-IS y convierte lo que recibe en
mensajes de texto DMR.

Por lo tanto el transporte que este firmware necesita es la sesión APRS-IS que
el IGate ya mantiene. Lo que agrega la página *BrandMeister* es el
reconocimiento, el control de seguridad y el ruteo de mensajes sobre ella.

Reconocer el tráfico BrandMeister
=================================

``main/include/aprs_bm.h`` es la única fuente de verdad. A cada línea leída de
APRS-IS se le aplican tres pruebas independientes, y gana la primera que
coincide:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Prueba
     - Qué reconoce
   * - TOCALL
     - Una dirección de destino ``APBM`` más exactamente dos caracteres.
       ``APBMxx`` es el bloque asignado a BrandMeister; ``APBMnD`` es el
       software principal del servidor y ``APBMnS`` sus servicios
       suplementarios.
   * - Alias DMR
     - Un elemento de la ruta igual a ``DMR`` que aparece antes del q
       construct, con o sin la marca de usado.
   * - Estación de entrada
     - El indicativo inmediatamente posterior a ``qAS``/``qAR`` que coincide
       con una de las pasarelas configuradas en la página. Un ``*`` final
       compara por prefijo. Se omite cuando la lista está vacía, que es el
       estado de fábrica.

La segunda prueba no es redundante con la primera. Existe tráfico BrandMeister
genuino que lleva el tocall genérico ``APRS`` y solamente el salto DMR::

   PA0WCH>APRS,DMR*,qAS,PI1DMR-10:@043258h5123.03N/00526.95E(036/000
   DC6RN-9>APRS,DB0CJ,DMR*,qAR,DB0CJ:@043233h4925.11N/01152.85Ev148/000

Un clasificador basado solo en el tocall no vería ninguno de los dos. Desde la
versión de máster 20170909 el indicativo del repetidor de origen también
aparece en el digipath, y por eso la segunda muestra lleva ``DB0CJ`` delante
del alias.

Una trama decodificada por el aire **nunca** se clasifica como tráfico
BrandMeister, diga lo que diga su ruta: la pregunta que responde el
clasificador es "¿la red inyectó esto en APRS-IS?", y una trama escuchada por
la radio no llegó así.

Dos formas de usarlo
====================

**BrandMeister local** — el modo por omisión, y lo que la mayoría de los
operadores realmente quiere. Deje el filtro de servidor como una suscripción
local común. El tráfico BrandMeister dentro de su radio llega solo, porque es
tráfico APRS-IS corriente; el clasificador lo marca, la tabla LAST HEARD lo
muestra con el prefijo ``BM:`` en lugar de ``INET:``, y el reenvío de Internet
a RF lo trata como a cualquier otra estación de Internet. Esto no cuesta ancho
de banda ni tiempo de aire adicional.

**Monitor mundial** — opcional. Agregar ``u/APBM*`` al filtro de servidor de la
página IGate suscribe al tráfico BrandMeister de toda la red.

.. warning::

   Los términos del filtro de servidor APRS-IS se combinan con O, nunca con Y:
   pasa todo paquete que coincida con cualquiera de ellos. Por eso
   ``u/APBM* r/lat/lon/150`` pide tráfico BrandMeister **mundial** *o*
   cualquier cosa dentro de 150 km: la intersección no se le puede expresar al
   servidor. La restricción a un radio local tiene que ocurrir en esta
   estación, y de eso se encarga el filtro de rango Internet a RF de la página
   IGate.

Por ese motivo, habilitar el interruptor de monitor mientras el reenvío de
Internet a RF está activo y el filtro de rango Internet a RF está apagado se
**rechaza**. La explicación permanece debajo del interruptor mientras no se
cumpla la condición previa, y no solo en la página que sigue a un guardado
rechazado: la página vuelve a deducir la condición de la configuración
almacenada en cada carga, así que dice lo mismo si al operador acaban de
rechazarlo, si está por ocurrirle, o si llegó a una estación donde el filtro de
rango se apagó después desde la página IGate. Deducirla en lugar de recordarla
también hace que el mensaje llegue al navegador al que le corresponde: varios
operadores pueden tener abierta la administración web a la vez. La misma regla
se vuelve a aplicar cuando la página IGate apaga el filtro de rango, y otra vez
al cargar un ``config.json``, así que no se puede evitar editando el archivo a
mano.

La página nunca edita por sí misma la cadena del filtro de servidor. El filtro
es del operador, y una página que lo reescribiera en silencio haría que la
página IGate informara mal lo que realmente se envió al servidor; en su lugar
se muestra el término exacto que hay que agregar, y la tabla de estado informa
si el filtro en uso ya lo lleva.

Enviar hacia BrandMeister
=========================

Mensaje privado a un usuario DMR
--------------------------------

Envíe un mensaje APRS común dirigido al indicativo-SSID que el usuario asoció a
su ID DMR en SelfCare. La radio muestra::

   <INDICATIVO DEL REMITENTE> <texto del mensaje>

El informe de entrega DMR vuelve como un acuse de recibo APRS normal.

.. note::

   La entrega no está garantizada y la falla es silenciosa. La pasarela de cada
   máster aplica su propio patrón al destinatario de los mensajes entrantes: una
   expresión regular acotada por país que esta estación no puede ver ni
   predecir. Un mensaje filtrado por ella simplemente no produce acuse. Por lo
   tanto la ausencia de acuse no es prueba de que no se entregó, y la página de
   chat no presenta como entregado un mensaje sin acusar.

Con *Enviar mensajes a estaciones BrandMeister solo por Internet* habilitado
(el valor por omisión), un mensaje dirigido a una estación escuchada por última
vez como tráfico BrandMeister sale solo por APRS-IS. Esa estación está en la
red, no en el canal local, así que cada copia por RF es tiempo de aire gastado
en un receptor que no está ahí, multiplicado por la cantidad de reintentos
mientras el mensaje siga sin acuse. Esto solo puede quitar la pata de RF: con
*Enviar a Internet* apagado en la página Mensajes, no se envía nada.

Mensaje a un talkgroup DMR
--------------------------

Use la página *Boletines* existente. Un boletín cuyo identificador sea ``0``–
``9`` y cuyo nombre de grupo sea el ID de talkgroup de 1 a 5 dígitos forma el
destinatario que BrandMeister lee, con *Enviar a Internet* habilitado:

.. list-table::
   :header-rows: 1
   :widths: 25 25 50

   * - Identificador
     - Grupo
     - Destinatario enviado, y dónde llega
   * - ``0``
     - ``2509``
     - ``BLN02509`` → talkgroup DMR 2509

Los destinatarios ven ``<INDICATIVO DEL REMITENTE> BLN02509 <texto>``. Un
nombre de grupo no numérico sigue siendo un boletín de grupo APRS perfectamente
válido: simplemente no llegará a ningún talkgroup.

Consultas
---------

``?APRSP`` (posición) y ``?APRSS`` (estado) dirigidas a una estación DMR
funcionan en radios con ARS/RRS/LRRP configurado.

Configuración
=============

Todo está en la página *BrandMeister*, ubicada inmediatamente después de
*IGate* en el menú:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Ajuste
     - Significado
   * - Habilitar la interconexión BrandMeister
     - Interruptor principal. Con él apagado no se clasifica ninguna línea, no
       se marca ninguna estación y el ruteo de mensajes queda intacto.
   * - Suscribirse al tráfico BrandMeister mundial
     - Registra la intención de correr la suscripción ``u/APBM*`` y aplica el
       requisito del filtro de rango descrito arriba.
   * - Enviar mensajes a estaciones BrandMeister solo por Internet
     - Habilitado por omisión. Suprime la pata de RF para un destinatario
       BrandMeister.
   * - Pasarela 1–4
     - Indicativos opcionales de estación de entrada para la tercera prueba del
       clasificador. Un ``*`` final compara por prefijo.

El filtro de rango Internet a RF vive en la página *IGate*, junto a su gemelo
de RF a Internet, porque gobierna toda línea que el flujo le ofrece al
transmisor y no solo el tráfico BrandMeister. La tabla de estado de la página
*BrandMeister* informa su condición.

Lo que deliberadamente no se implementa
=======================================

* Ninguna conexión DMR, Homebrew/MMDVM ni OpenBridge.
* La API REST de BrandMeister ni el flujo LastHeard. Ambos son solo HTTPS/WSS y
  este firmware se compila sin pila TLS; además lo que devuelven son metadatos
  de sesiones DMR e inventario de red, no APRS.
* Ninguna reproducción local del patrón de destinatario de un máster. Es propio
  de cada máster y no se puede descubrir desde aquí.
