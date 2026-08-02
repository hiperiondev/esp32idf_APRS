:orphan:

.. _es-index:

.. image:: /_static/welcome_logo.png
   :align: center
   :width: 300px

.. raw:: html

   <h1 style="text-align: center;">esp32idf_APRS</h1>
   <h2 style="text-align: center;">Una estación APRS completa en un único ESP32 — ESP-IDF nativo, sin Arduino.</h2>
   <p style="text-align: center;"><strong>IGate · Digipeater · Tracker · Meteo · Telemetría, con administración web integrada, un módem software AFSK/FSK en el propio chip, enlace a APRS-IS, un framework de controladores de sensores en tiempo de ejecución y actualizaciones de firmware por OTA.</strong></p>

============
Bienvenidos
============

Bienvenido a la documentación de **esp32idf_APRS** — una estación APRS
IGate, Digipeater, Tracker, Meteo y Telemetría nativa en ESP-IDF (C, sin
Arduino) que se ejecuta enteramente en un único microcontrolador ESP32.

Qué es este proyecto, en términos sencillos
=============================================

La mayoría de las estaciones APRS que se encuentran en el terreno se
construyen a partir de dos piezas separadas: una computadora (o placa de
cómputo de placa única) que ejecuta software APRS como Direwolf o Xastir,
más un TNC por hardware o por software que escucha una radio.
``esp32idf_APRS`` reduce toda esa pila —el módem de audio, el motor de
paquetes, la lógica de gateway, el digipeater y la interfaz del operador— al
firmware de un único ESP32, sin PC, sin Raspberry Pi y sin tarjeta de sonido
externa en el circuito.

El propio conversor analógico-digital del ESP32 escucha la salida de
altavoz o de discriminador de una radio, y el firmware demodula el audio
AFSK o FSK por software, enteramente en el chip. A partir de ahí decodifica
paquetes AX.25, decide si los enruta hacia internet, los repite de vuelta
por RF, o ambas cosas, y con la misma facilidad puede originar su propio
tráfico: balizas de posición, informes meteorológicos, telemetría, mensajes,
boletines y objetos. Todo esto se configura a través de una página web
servida por el propio dispositivo, por Wi-Fi, desde el navegador de un
teléfono o una laptop —no hay terminal serie, no hay software especial que
instalar y no se necesita recompilar el firmware para los ajustes del día a
día.

En resumen: una placa pequeña y económica, más una interfaz de audio simple
hacia una radio, se convierte en un IGate, digipeater y tracker APRS
completo y autónomo que se configura desde un navegador y luego se deja
funcionando.

¿Qué es APRS?
=============

**APRS — el Sistema Automático de Reporte de Paquetes (Automatic Packet
Reporting System)** — es un protocolo digital de radioafición para el
intercambio en tiempo real y de muchos a muchos de pequeñas piezas de
información táctica: la posición de una estación, su estado, mensajes de
texto cortos, lecturas meteorológicas, valores de telemetría y anuncios de
propósito general. A diferencia de un enlace de datos punto a punto, APRS es
fundamentalmente un sistema de *difusión* — cualquiera que escuche en la
frecuencia compartida, o que observe la red por internet, ve el tráfico en
el momento en que ocurre.

Una breve historia
--------------------

APRS fue creado por Bob Bruninga, indicativo **WB4APR**, ingeniero de
investigación senior en la Academia Naval de los Estados Unidos. Sus
primeros experimentos de mapeo de posición datan de 1982 en una Apple II, y
para 1984 ya tenía una versión específica —el Connectionless Emergency
Traffic System— funcionando en una Commodore VIC-20 para rastrear caballos y
jinetes durante una carrera de resistencia de 100 millas. Durante finales de
los años 80 el software pasó a la PC IBM y, una vez combinado con el
protocolo de radio-paquete AX.25 y, más tarde, con receptores GPS
asequibles, se convirtió en un sistema general de reporte táctico en tiempo
real. Se presentó formalmente a la comunidad de radioaficionados y se
nombró APRS —un acrónimo construido a partir del propio indicativo de
Bruninga— en un artículo de 1992 en la Conferencia de Redes de Computadoras
de la ARRL. Bruninga siguió manteniendo el protocolo y su sitio de
referencia hasta su fallecimiento en 2022, tras lo cual se formó la APRS
Foundation para continuar el desarrollo del protocolo.

Dos avances convirtieron a APRS de un experimento de nicho en radio-paquete
a un servicio de radioafición ampliamente usado: el GPS económico hizo
práctico el reporte de posición automático y continuo, y la aparición de
**APRS-IS** —la red troncal conectada a internet formada por estaciones
receptoras (IGates) que reenvían el tráfico de RF a la internet pública—
significó que la actividad de una estación pudiera verse en todo el mundo
en cuestión de segundos, no solo por quien estuviera al alcance de la
radio.

Uso actual
-----------

Hoy en día APRS se usa en todo el mundo para el rastreo de vehículos y
excursionistas, el reporte de estaciones meteorológicas, la mensajería de
texto de corto alcance entre operadores, el apoyo a eventos y redes, la
logística de búsqueda y rescate, y simplemente para monitorear quién está
activo localmente. El tráfico se intercambia sobre una frecuencia VHF
compartida en cada región (comúnmente 144.390 MHz en Norteamérica; en otras
regiones aplican otras frecuencias), se retransmite localmente mediante
**digipeaters**, y se conecta con la red global **APRS-IS** mediante
**IGates** —los mismos dos roles que implementa este firmware. Sitios
agregadores como `aprs.fi <https://aprs.fi>`__ permiten a cualquiera
observar ese tráfico global en un mapa desde el navegador. Muchas radios
comerciales de radioafición ya incorporan APRS de fábrica, y ha crecido un
gran ecosistema de software de código abierto —cubierto en detalle al final
de la página siguiente— en torno al protocolo, tanto en plataformas de
escritorio como móviles y embebidas.

Cómo está organizada esta documentación
==========================================

Esta documentación se organiza en tres partes, cada una con su propio conjunto
de capítulos:

* **Funcionalidades** — lo que *hace* la estación desde el punto de vista del
  operador: gateway, digipeating, balizas, mensajería, meteorología,
  telemetría, boletines, objetos y la administración web.
* **Capacidades** — las *propiedades* del firmware que son transversales a las
  funciones: los perfiles del módem, el filtrado, la localización, el
  almacenamiento, OTA, la red y el soporte de hardware.
* **Internos** — cómo está *construido*: la secuencia de arranque, el mapa de
  tareas, el flujo de datos, la cadena de señal DSP, el motor de configuración
  y el registro de sensores.

.. toctree::
   :maxdepth: 2
   :caption: Primeros pasos

   hardware
   getting-started

.. toctree::
   :maxdepth: 2
   :caption: Funcionalidades

   functionality/igate
   functionality/digipeater
   functionality/beacons
   functionality/messaging
   functionality/query
   functionality/weather
   functionality/telemetry
   functionality/bulletins-objects
   functionality/web-admin

.. toctree::
   :maxdepth: 2
   :caption: Capacidades

   capability/modem
   capability/filtering
   capability/networking
   capability/storage-ota
   capability/localization

.. toctree::
   :maxdepth: 2
   :caption: Internos

   internals/architecture
   internals/dsp-signal-chain
   internals/configuration
   internals/sensor-framework
   internals/source-map

.. toctree::
   :maxdepth: 1
   :caption: Referencia

   reference/config-json
   reference/http-routes
   reference/troubleshooting
   reference/limitations
   reference/credits
