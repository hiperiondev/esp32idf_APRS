.. _es-getting-started:

==============
Primeros pasos
==============

Requisitos previos
==================

* **ESP-IDF v6.0 o posterior** (fijado/probado en **6.0.2** — véase
  ``dependencies.lock``).
* Un ESP32 con **≥ 4 MB de flash**.
* El gestor de componentes de IDF descarga ``joltwallet/littlefs``,
  ``espressif/cjson`` y, a través
  del componente ``sensors_local``, ``esp-idf-lib/bmp280`` y
  ``esp-idf-lib/bmp180`` (que arrastran ``i2cdev`` +
  ``esp_idf_lib_helpers``) automáticamente.

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

Presupuesto de memoria
======================

El ESP32 de este diseño no lleva PSRAM, así que cada byte de DRAM interna que
la compilación reserva de forma estática es un byte que el heap nunca recibe.
``sdkconfig`` está ajustado para eso, y los valores de abajo son deliberados:
subir cualquiera de ellos baja la cifra *Min free heap* del panel.

.. list-table::
   :header-rows: 1
   :widths: 44 10 46

   * - Opción
     - Valor
     - Por qué
   * - ``CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM``
     - 6
     - ~1,6 KB cada uno, reservados en ``esp_wifi_init()`` y retenidos hasta
       que se desinicializa el WiFi. Seis coincide con
       ``CONFIG_ESP_WIFI_RX_BA_WIN``, que es el piso que pide AMPDU RX.
   * - ``CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM`` / ``..._TX_...``
     - 12
     - Acota el pico de heap que reclama el driver WiFi. El tráfico APRS son
       unos cientos de bytes por minuto; el caudal que compran esos búferes no
       se usa nunca.
   * - ``CONFIG_LWIP_TCP_SND_BUF_DEFAULT`` / ``CONFIG_LWIP_TCP_WND_DEFAULT``
     - 2880
     - Dos MSS por sentido y por conexión. La única transferencia sostenida es
       la subida de una imagen OTA, que con esta ventana sigue saturando una
       LAN.
   * - ``max_open_sockets`` en ``web_server_start()``
     - 4
     - httpd toma este número más 3 sockets propios del pool de
       ``CONFIG_LWIP_MAX_SOCKETS`` (10). Los 3 restantes son los que necesitan
       el enlace APRS-IS, DNS y SNTP para seguir en pie mientras alguien
       navega las páginas de administración.
   * - Capa TLS de mbedTLS, paquete de certificados, WiFi Enterprise
     - apagados
     - La administración web es HTTP plano y el enlace APRS-IS es TCP plano.
       mbedTLS se enlaza por una sola llamada, ``mbedtls_base64_decode()`` en
       la autenticación HTTP Basic, que no depende de
       ``CONFIG_MBEDTLS_TLS_ENABLED``.

.. note::

   Desde ESP-IDF v6.0 el port de mbedTLS llama a ``psa_crypto_init()`` desde
   un hook de arranque del sistema, así que PSA Crypto está vivo en toda
   compilación que enlace mbedTLS, incluida esta. Eso, junto con la mayor
   huella estática de mbedTLS 4.x, es la razón de que el mismo firmware
   informe menos heap libre bajo v6.0 que bajo v5.2 con una configuración por
   lo demás idéntica.

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
   * - Frecuencia de CPU
     - 240 MHz
   * - Reloj del sistema
     - siempre UTC (TZ=UTC0). El selector de zona horaria de la página System
       (por defecto ``UTC``) solo cambia la fecha/hora local mostrada en el
       panel; las marcas de tiempo al aire siguen siendo zulú
   * - Hosts NTP
     - ``pool.ntp.org``, ``time.google.com``, ``time.cloudflare.com``
   * - IGate
     - habilitado, ``rf2inet`` activo, ``inet2rf`` inactivo
   * - Indicativo / SSID
     - ``NOCALL`` / 10, passcode ``-1``
   * - Coordenadas de la estación
     - ``0.000`` / ``0.000``, se transmiten tal cual
   * - Servidores APRS-IS
     - cuatro ranuras de failover, todas preconfiguradas a ``aprs.dprns.com`` :
       14580, con solo la ranura 1 habilitada
   * - Lista de satélites digipetidores
     - ``RS0ISS``, ``YBOX``, ``YBSAT``, ``PSAT``, ``W3ADO``, ``BJ1SI`` (hasta 8, configurable desde la web)
   * - Caché / ventana de supresión de duplicados
     - 20 entradas / 30000 ms (configurable desde la web)
   * - Limitador de ciclo de trabajo de transmisión
     - deshabilitado; techo del 25 % de una ventana deslizante de 10 minutos
       cuando se habilita
   * - Preset de ruta 0
     - ``WIDE1-1,WIDE2-1``
   * - Selección de ruta
     - preset 0, igual para las balizas de IGate, digipetidor, tracker y meteo
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
     - habilitada, RF + INET, GPIO de alarma deshabilitado

.. danger::

   **Cambia** ``NOCALL`` **y establece un passcode real antes de transmitir.**
   Verifica que tienes licencia para la frecuencia y el ciclo de trabajo que vas
   a activar.

   **Configura también las coordenadas de la estación.** APRS no tiene una
   coordenada de "posición desconocida": una estación que baliza antes de
   fijarlas pone 0° N / 0° E en el aire, que es una posición real en el golfo
   de Guinea y no una posición ausente, y todos los mapas la dibujan allí.
