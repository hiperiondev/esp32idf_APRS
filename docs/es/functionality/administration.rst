.. _es-administration:

=============================
Administración de la estación
=============================

Cinco páginas se ocupan del equipo más que de los servicios APRS:
*Inalámbrico*, *Sistema*, *Almacenamiento de archivos*, *Firmware* y
*Registros*.

Inalámbrico
===========

.. figure:: /_static/webadmin/wireless_top.png
   :alt: La página Inalámbrico al abrirse.
   :align: center
   :width: 100%

   La página *Inalámbrico* al abrirse.

.. figure:: /_static/webadmin/wireless_fs00.png
   :alt: Modo WiFi — modo Wi-Fi - apagado, cliente (STA), punto de acceso (AP) o ambos - y la potencia de transmisión, hasta 20 dBm.
   :align: center
   :width: 90%

   **Modo WiFi** — modo Wi-Fi - apagado, cliente (STA), punto de acceso (AP) o ambos - y la potencia de transmisión, hasta 20 dBm.

.. figure:: /_static/webadmin/wireless_fs01.png
   :alt: Punto de acceso (AP) — el punto de acceso propio de la estación: nombre de red, contraseña (al menos 8 caracteres, o vacía para un punto de acceso abierto; la de fábrica se genera por equipo y se imprime en la consola serie al arrancar) y canal (1 a 13).
   :align: center
   :width: 90%

   **Punto de acceso (AP)** — el punto de acceso propio de la estación: nombre de red, contraseña (al menos 8 caracteres, o vacía para un punto de acceso abierto; la de fábrica se genera por equipo y se imprime en la consola serie al arrancar) y canal (1 a 13).

.. figure:: /_static/webadmin/wireless_fs02.png
   :alt: Cliente WiFi n.º 1 — uno de los cinco espacios de cliente, cada uno con su interruptor Habilitar, nombre de red y contraseña.
   :align: center
   :width: 90%

   **Cliente WiFi n.º 1** — uno de los cinco espacios de cliente, cada uno con su interruptor *Habilitar*, nombre de red y contraseña.

Los espacios activados que tienen nombre de red forman una lista de respaldo.
Al arrancar se prueba el primero; un espacio que no se conectó tras tres
intentos (unos quince segundos) deja paso al siguiente espacio activado, y el
recorrido vuelve a empezar al llegar al final, de modo que un equipo que pasa
de la red de casa al hotspot de un teléfono encuentra el que esté al alcance
sin cambiar nada. Un espacio que se conecta conserva sus tres intentos para la
próxima vez que caiga el enlace. *AP + STA* mantiene el punto de acceso
mientras el cliente está conectado, que es lo seguro mientras se configura una
estación: la administración web sigue accesible en ``192.168.4.1`` aunque las
credenciales del cliente estén mal. El botón *ESCANEAR WIFI* sobre los
espacios busca las redes al alcance y muestra el resultado, de la más fuerte a
la más débil, en una lista bajo cada campo SSID; elegir una red ahí escribe su
nombre en ese espacio. Los nombres también se ofrecen como sugerencias al
escribir. La red está descrita en :ref:`es-networking`.

Sistema
=======

.. figure:: /_static/webadmin/system_top.png
   :alt: La página Sistema al abrirse.
   :align: center
   :width: 100%

   La página *Sistema* al abrirse.

.. figure:: /_static/webadmin/system_fs00.png
   :alt: Chip — modelo de chip, núcleos, revisión y tamaño de flash, y la frecuencia de la CPU (80, 160 o 240 MHz), aplicada en el acto y en cada arranque.
   :align: center
   :width: 90%

   **Chip** — modelo de chip, núcleos, revisión y tamaño de flash, y la frecuencia de la CPU (80, 160 o 240 MHz), aplicada en el acto y en cada arranque.

El módem de audio necesita 240 MHz: a menor frecuencia la cadena de recepción
no alcanza a procesar las muestras.

.. figure:: /_static/webadmin/system_fs01.png
   :alt: Acceso al administrador web — la cuenta de administrador (admin / admin de fábrica). Cámbiala primero: hasta entonces solo la página Sistema acepta cambios.
   :align: center
   :width: 90%

   **Acceso al administrador web** — la cuenta de administrador (``admin`` / ``admin`` de fábrica). Cámbiala primero: hasta entonces solo la página Sistema acepta cambios.

.. figure:: /_static/webadmin/system_fs02.png
   :alt: Acceso web de solo lectura — una segunda cuenta opcional que puede ver todas las páginas pero no cambiar nada.
   :align: center
   :width: 90%

   **Acceso web de solo lectura** — una segunda cuenta opcional que puede ver todas las páginas pero no cambiar nada.

Los dos roles están descritos en :ref:`es-web-admin`.

.. figure:: /_static/webadmin/system_fs03.png
   :alt: Hora — sincronización de hora por NTP: activación, tres servidores, intervalo de resincronización y la zona horaria con que el panel muestra la fecha.
   :align: center
   :width: 90%

   **Hora** — sincronización de hora por NTP: activación, tres servidores, intervalo de resincronización y la zona horaria con que el panel muestra la fecha.

Todas las marcas de tiempo que la estación envía al aire son UTC sea cual sea
la zona horaria. Al pie de la página está el botón de **restablecimiento de
fábrica**, que devuelve todos los ajustes a sus valores por defecto y
reinicia.

Almacenamiento de archivos
==========================

.. figure:: /_static/webadmin/storage_main.png
   :alt: El explorador de LittleFS: uso, subida, formateo y una fila por archivo con Descargar y Eliminar.
   :align: center
   :width: 90%

   El explorador de LittleFS: uso, subida, formateo y una fila por archivo con *Descargar* y *Eliminar*.

Cada servicio guarda sus ajustes en su propio archivo JSON, así que una página
se puede respaldar, restaurar o copiar a otra estación descargando y subiendo
su archivo. El certificado raíz de Telegram también se sube aquí. Ver
:ref:`es-storage-ota`.

Firmware y OTA
==============

.. figure:: /_static/webadmin/about_top.png
   :alt: La página Firmware al abrirse.
   :align: center
   :width: 100%

   La página *Firmware* al abrirse.

.. figure:: /_static/webadmin/about_fs00.png
   :alt: Firmware — proyecto, versión, fecha de compilación, versión de ESP-IDF y la partición en ejecución.
   :align: center
   :width: 90%

   **Firmware** — proyecto, versión, fecha de compilación, versión de ESP-IDF y la partición en ejecución.

.. figure:: /_static/webadmin/about_fs01.png
   :alt: Actualización OTA — la actualización OTA: elegir un .bin y Cargar y grabar.
   :align: center
   :width: 90%

   **Actualización OTA** — la actualización OTA: elegir un ``.bin`` y *Cargar y grabar*.

La imagen se escribe en la ranura inactiva mientras la estación sigue
funcionando, y la estación cambia de ranura solo cuando está completa y
verificada. Una imagen que no logra arrancar se revierte automáticamente en el
siguiente reinicio. Solo la primera grabación hay que hacerla por USB.

Registros de consola
====================

.. figure:: /_static/webadmin/logs_fs00.png
   :alt: Registro de consola — la consola serie reflejada en el navegador: Iniciar / Detener y las últimas 50 líneas.
   :align: center
   :width: 90%

   **Registro de consola** — la consola serie reflejada en el navegador: *Iniciar* / *Detener* y las últimas 50 líneas.

La página muestra lo que la estación imprime por su puerto serie, así que una
estación en una torre se puede diagnosticar sin cable. No se graba nada ni se
escribe en flash; la captura se detiene al salir de la página. Además de las
líneas de cada servicio (tramas recibidas, balizas, APRS-IS), una compilación
normal imprime una línea de heap cada 10 segundos y el margen de pila de cada
tarea una vez por hora; ambas, y los desgloses opcionales por heap y por
asignación, se eligen en ``menuconfig`` → *APRS heap instrumentation*. Son lo
primero que hay que mirar cuando una estación falla tras horas de
funcionamiento (ver :ref:`es-troubleshooting`).
