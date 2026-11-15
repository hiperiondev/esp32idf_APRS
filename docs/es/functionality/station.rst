.. _es-station:

================================
Identidad de estación y símbolos
================================

La página *Estación* guarda la identidad que comparten todos los servicios: el
indicativo, la posición y las opciones de la estación que valen para todo lo
que sale al aire. Cada página de servicio (IGate, digipetidor, tracker, clima,
mensajería) tiene su propio indicativo, SSID y posición, y un interruptor
*Usar mis datos de estación* que los copia de aquí, así que normalmente una
estación se configura una sola vez en esta página.

.. figure:: /_static/webadmin/station_top.png
   :alt: La página Estación al abrirse.
   :align: center
   :width: 100%

   La página *Estación* al abrirse.

El grupo Estación
=================

.. figure:: /_static/webadmin/station_fs00.png
   :alt: Estación — indicativo, latitud, longitud y altitud, con Usar GPS, seguidos de las opciones que se aplican a todos los informes que envía la estación.
   :align: center
   :width: 90%

   **Estación** — indicativo, latitud, longitud y altitud, con *Usar GPS*, seguidos de las opciones que se aplican a todos los informes que envía la estación.

* **Usar GPS** deshabilita los tres campos de posición y los llena una vez por
  segundo desde el receptor GNSS (``/gps/live``) mientras está marcado,
  redondeados a la precisión que admiten los campos. Necesita el receptor
  activado en la página *GPS*.
* **Ambiguedad de posicion** borra los últimos dígitos de cada posición que
  envía la estación, desde precisión completa hasta alrededor de 60 millas
  náuticas.
* **Localizador Maidenhead en los reportes de estado** antepone el cuadrado de la
  estación a los textos de estado.
* **Marca de tiempo zulu en los reportes de estado** antepone una marca
  ``DDHHMMz`` a los textos de estado.
* **Extensión de precisión DAO en los reportes de posición** agrega el campo ``!DAO!``, que lleva un
  dígito más de latitud y longitud.
* **Solicitar a APRS-IS que no archive mis paquetes** agrega la marca ``!x!``.
* **Rumbo de antena** y **PRE** en los reportes de estado cierran los informes de estado con el sufijo
  de rumbo y potencia para meteor scatter.

Cómo se codifica cada uno está descrito en :ref:`es-beacons`.

El grupo PHG
============

.. figure:: /_static/webadmin/station_fs01.png
   :alt: PHG — potencia del transmisor, ganancia, altura y directividad de la antena de la estación, y el texto PHG resultante.
   :align: center
   :width: 90%

   **PHG** — potencia del transmisor, ganancia, altura y directividad de la antena de la estación, y el texto ``PHG`` resultante.

Las páginas del IGate y del digipetidor usan estos valores cuando su
*Extension de datos* es PHG con *Usar mis datos de estación*.

El selector de símbolos
=======================

Toda página que envía una posición tiene una fila *Símbolo de Estación*: el
ícono, la tabla y el código del símbolo. El enlace ``…`` junto a ella abre la
página *Símbolos APRS*, con las dos tablas completas.

.. figure:: /_static/webadmin/symbol_top.png
   :alt: La página Símbolos APRS al abrirse.
   :align: center
   :width: 100%

   La página *Símbolos APRS* al abrirse.

.. figure:: /_static/webadmin/symbol_fs00.png
   :alt: Selección Rápida — los símbolos más usados, a un clic.
   :align: center
   :width: 90%

   **Selección Rápida** — los símbolos más usados, a un clic.

.. figure:: /_static/webadmin/symbol_fs01.png
   :alt: Símbolos actualmente configurados — el símbolo que transmite cada servicio en este momento.
   :align: center
   :width: 90%

   **Símbolos actualmente configurados** — el símbolo que transmite cada servicio en este momento.

Debajo la página lista las tablas primaria (``/``) y alternativa (``\``)
completas. Elegir un símbolo escribe la tabla y el código en la página desde
la que se abrió el selector; no se guarda nada hasta guardar esa página. Igual
que en el panel, los íconos vienen de ``aprs.dprns.com`` y, si ese sitio no
está accesible, se muestra el carácter del símbolo.
