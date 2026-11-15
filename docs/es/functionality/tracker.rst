.. _es-tracker:

=======
Tracker
=======

El tracker envía la posición propia de la estación mientras se mueve (o desde
un punto fijo cuando no hay receptor). Funciona sobre el planificador de
balizas compartido como todas las demás balizas; las opciones de codificación
que ofrece - posición comprimida y Mic-E, SmartBeaconing, PHG, bloques de
estado y de frecuencia - están descritas en detalle en :ref:`es-beacons`.
Esta página es la vista del operador.

.. figure:: /_static/webadmin/tracker_top.png
   :alt: La página Tracker al abrirse.
   :align: center
   :width: 100%

   La página *Tracker* al abrirse.

.. figure:: /_static/webadmin/tracker_fs00.png
   :alt: Tracker — activación, Usar mis datos de estación, transmisión por RF y/o APRS-IS, la marca de tiempo DDHHMMz y el intervalo fijo de baliza.
   :align: center
   :width: 90%

   **Tracker** — activación, *Usar mis datos de estación*, transmisión por RF y/o APRS-IS, la marca de tiempo ``DDHHMMz`` y el intervalo fijo de baliza.

.. figure:: /_static/webadmin/tracker_fs01.png
   :alt: Estación — indicativo, SSID (9 por defecto, el habitual de un móvil), ruta y la posición fija con Usar GPS y Usar posición GPS en vivo.
   :align: center
   :width: 90%

   **Estación** — indicativo, SSID (9 por defecto, el habitual de un móvil), ruta y la posición fija con *Usar GPS* y *Usar posición GPS en vivo*.

Los dos interruptores de GPS hacen cosas distintas:

* **Usar GPS** copia la posición del receptor en los campos fijos de latitud,
  longitud y altitud mientras la página está abierta; lo que se guarda es una
  posición fija.
* **Usar posición GPS en vivo** deja la posición fija como respaldo y hace que
  cada transmisión lea el receptor en ese momento. Es lo que necesita un
  tracker móvil, y lo que necesita SmartBeaconing para tener velocidad y rumbo
  con los que trabajar. Cuando el receptor no tiene posición, la baliza vuelve
  a la posición y al intervalo fijos.

.. figure:: /_static/webadmin/tracker_fs02.png
   :alt: SmartBeaconing — los parámetros de SmartBeaconing: intervalos lento y rápido, velocidades baja y alta, ángulo de giro, pendiente de giro y tiempo mínimo entre giros.
   :align: center
   :width: 90%

   **SmartBeaconing** — los parámetros de SmartBeaconing: intervalos lento y rápido, velocidades baja y alta, ángulo de giro, pendiente de giro y tiempo mínimo entre giros.

.. figure:: /_static/webadmin/tracker_fs03.png
   :alt: Opciones — posición comprimida, codificación Mic-E con su comentario de posición, altitud, PHG, el símbolo de la estación y el comentario.
   :align: center
   :width: 90%

   **Opciones** — posición comprimida, codificación Mic-E con su comentario de posición, altitud, PHG, el símbolo de la estación y el comentario.

.. figure:: /_static/webadmin/tracker_fs04.png
   :alt: Baliza de estado — texto e intervalo de la baliza de estado.
   :align: center
   :width: 90%

   **Baliza de estado** — texto e intervalo de la baliza de estado.

.. figure:: /_static/webadmin/tracker_fs05.png
   :alt: Parámetros de Radio del Repetidor — frecuencia, dúplex, desplazamiento y tono escritos al principio del comentario, para una estación que anuncia la frecuencia en la que escucha.
   :align: center
   :width: 90%

   **Parámetros de Radio del Repetidor** — frecuencia, dúplex, desplazamiento y tono escritos al principio del comentario, para una estación que anuncia la frecuencia en la que escucha.
