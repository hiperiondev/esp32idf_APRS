.. _es-getting-started:

==============
Primeros pasos
==============

Requisitos previos
==================

* **ESP-IDF v5.1 o posterior** (fijado/probado en **5.5.4** — véase
  ``dependencies.lock``).
* Un ESP32 con **≥ 4 MB de flash**.
* El gestor de componentes de IDF descarga ``joltwallet/littlefs`` y, a través
  del componente ``sensors_local``, ``esp-idf-lib/bmp180`` (que arrastra
  ``i2cdev`` + ``esp_idf_lib_helpers``) automáticamente.

Compilar y grabar
=================

.. code-block:: bash

   . $IDF_PATH/export.sh

   cd workspace-APRS/esp32_APRS_igate

   idf.py set-target esp32          # sdkconfig ya viene con target=esp32
   idf.py build
   idf.py -p /dev/ttyUSB0 flash monitor

Compilar en español o italiano en lugar de inglés (véase :ref:`es-localization`):

.. code-block:: bash

   idf.py build -DLANGUAGE=LANG_ES
   idf.py build -DLANGUAGE=LANG_IT

.. tip::

   ``sdkconfig`` viene con ``CONFIG_COMPILER_OPTIMIZATION_DEBUG`` (``-Og``) y
   las aserciones activadas. Cambia a ``-Os`` si andas justo de flash.

Primer arranque
===============

#. En una partición nueva, LittleFS se autoformatea y ``app_config_load()``
   escribe ``/storage/config.json`` con los valores de fábrica.
#. El ESP32 arranca como **AP Wi-Fi**: SSID ``esp32idf_APRS``, contraseña
   ``esp32idf_APRS``, canal 1, WPA2-PSK, máx. 4 clientes.
#. Únete a él y navega al dispositivo (por defecto ``http://192.168.4.1/``).
#. **Inicia sesión:** ``admin`` / ``admin`` — cámbialo en la página *System*.
#. En *Wireless*: elige **Station** o **AP+STA**, marca **Enable** en un bloque
   de Cliente Wi-Fi, introduce SSID/contraseña, Guarda.
#. En *IGate*: pon tu **indicativo**, **SSID**, **passcode**, **host**/**puerto**
   de APRS-IS, filtro, coordenadas, símbolo, comentario.
#. En *Radio / Modem*: habilita el módem de audio, elige la modulación,
   preámbulo, ranura de tiempo TX; usa **LOOP TEST** para verificar la ruta de
   audio.
#. Reinicia (o Guarda — la mayoría de los ajustes se aplican en vivo).

Valores de fábrica destacados
=============================

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Ajuste
     - Por defecto
   * - Modo Wi-Fi
     - AP (siempre accesible)
   * - SSID / contraseña del AP
     - ``esp32idf_APRS`` / ``esp32idf_APRS``
   * - Login web
     - ``admin`` / ``admin``
   * - Hostname
     - ``esp32idf_APRS``
   * - Frecuencia de CPU
     - 240 MHz
   * - Zona horaria
     - 0.0 (el reloj en sí siempre es UTC)
   * - Hosts NTP
     - ``pool.ntp.org``, ``time.google.com``, ``time.cloudflare.com``
   * - IGate
     - habilitado, ``rf2inet`` activo, ``inet2rf`` inactivo
   * - Indicativo / SSID
     - ``NOCALL`` / 10, passcode ``-1``
   * - Host / puerto de APRS-IS
     - ``aprs.dprns.com`` : 14580
   * - Lista de satélites digipetidores
     - ``RS0ISS``, ``YBOX``, ``YBSAT``, ``PSAT``, ``W3ADO``, ``BJ1SI`` (hasta 8, configurable desde la web)
   * - Caché / ventana de supresión de duplicados
     - 20 entradas / 30000 ms (configurable desde la web)
   * - Preset de ruta 0
     - ``WIDE1-1,WIDE2-1``
   * - Digipeater
     - deshabilitado, SSID 1
   * - Tracker
     - deshabilitado, SSID 9
   * - Módem de audio
     - habilitado, 1200 Bd Bell 202
   * - Preámbulo / ranura TX
     - 300 ms / 2000 ms
   * - Persistencia CSMA
     - 63 (~25 % de probabilidad de transmitir por ranura libre)
   * - Búferes de TX de RF
     - 1
   * - Respondedor de consultas
     - deshabilitado; RF activo, Internet apagado, intervalo mínimo de
       respuesta 30 s
   * - FX.25
     - desactivado
   * - PTT
     - GPIO26 (la polaridad es de compilación)
   * - Mensajería
     - habilitada, RF + INET, cifrado desactivado

.. danger::

   **Cambia** ``NOCALL`` **y establece un passcode real antes de transmitir.**
   Verifica que tienes licencia para la frecuencia y el ciclo de trabajo que vas
   a activar.
