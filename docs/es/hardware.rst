.. _es-hardware:

========
Hardware
========

Objetivo soportado
==================

* **ESP32** (clásico, Xtensa de doble núcleo) — ``CONFIG_IDF_TARGET=esp32``,
  4 MB de flash.
* El doble núcleo **no es opcional**: la ISR del ADC y el reloj de muestreo del
  DAC se fijan a *núcleos distintos* a propósito (véase
  :ref:`es-dsp-signal-chain`).
* El ESP32-S2 tiene DAC en GPIO17/18 y requeriría ajustar la cabecera de
  configuración. **El ESP32-S3/C3/C6/H2 no tiene DAC** y no puede ejecutar la
  ruta de TX sin modificar.

Pinout / definición de placa
============================

La definición de placa vive en el ``CMakeLists.txt`` **de nivel superior**,
aplicada *antes* de ``project()`` mediante
``idf_build_set_property(COMPILE_DEFINITIONS … APPEND)``:

.. code-block:: cmake

   idf_build_set_property(COMPILE_DEFINITIONS "MODEM_ADC_GPIO=33"       APPEND)
   idf_build_set_property(COMPILE_DEFINITIONS "MODEM_DAC_GPIO=25"       APPEND)
   idf_build_set_property(COMPILE_DEFINITIONS "MODEM_PTT_GPIO=26"       APPEND)
   idf_build_set_property(COMPILE_DEFINITIONS "MODEM_PTT_ACTIVE_HIGH=1" APPEND)
   idf_build_set_property(COMPILE_DEFINITIONS "MODEM_LED_TX_GPIO=-1"    APPEND)
   idf_build_set_property(COMPILE_DEFINITIONS "MODEM_LED_RX_GPIO=-1"    APPEND)

.. list-table::
   :header-rows: 1
   :widths: 20 20 60

   * - Señal
     - Por defecto
     - Restricciones estrictas
   * - **Audio in (ADC)**
     - ``GPIO33`` (ADC1_CH5)
     - **Solo 32–39.** El ADC2 es inutilizable con Wi-Fi activo, y este firmware
       siempre tiene Wi-Fi activo. Forzado por un ``#error`` de compilación.
   * - **Audio out (DAC)**
     - ``GPIO25`` (DAC_CHAN_0)
     - **Solo 25 o 26.** El DAC del ESP32 está cableado a esos pads y no es
       enrutable por la matriz GPIO. Forzado por ``#error``.
   * - **PTT**
     - ``GPIO26``
     - Cualquier GPIO de salida distinto de los pines ADC/DAC; rechaza los
       GPIO34–39 solo-entrada y GPIO6–11 flash/PSRAM. ``-1`` lo deshabilita.
       Tanto el pin **como su polaridad** son constantes de compilación.
   * - **LEDs TX / RX**
     - deshabilitados (``-1``)
     - Cualquier GPIO de salida.
   * - **Recepción GNSS (UART RX)**
     - ``GPIO16``
     - Cableado a la salida TX del módulo. Cualquier GPIO de entrada que
       alcance la matriz UART. **Inutilizable en un ESP32-WROVER**, donde
       GPIO16/17 pertenecen al die de PSRAM SPI. Se fija en
       ``main/include/gps.h``.
   * - **Transmisión GNSS (UART TX)**
     - ``GPIO17``
     - Cableado a la entrada RX del módulo. Nunca se envía nada por él — el
       firmware jamás configura el receptor — pero el pin queda igualmente
       reservado, porque está físicamente conectado a esa entrada.

Cableado a una radio
====================

Ningún extremo del enlace de audio puede conectarse directamente al otro. El
lado del ESP32 es una interfaz de 3,3 V, polarizada en DC y de datos
muestreados; el lado de la radio es una interfaz analógica de AC, referida a
tierra y de nivel milivoltio. Tres cosas deben ocurrir en medio: **atenuar**
(TX), **desplazar y limitar** (RX) y **conmutar** (PTT).

Qué presenta cada extremo
-------------------------

Cada cifra del lado del ESP32 se deriva de las propias constantes de
compilación del componente de módem, no de un ideal de hoja de datos.

.. list-table::
   :header-rows: 1
   :widths: 26 50 24

   * - Nodo
     - Qué hay realmente
     - De
   * - **GPIO25 (DAC), transmitiendo**
     - 1,65 V DC con una oscilación ≈1,97 Vpp encima ⇒ ≈0,70 Vrms para una
       sinusoide, más imágenes de reconstrucción en torno a 38,4 kHz
     - ``DAC_MID=128``, ``AMPLITUDE_PCT=60``, ``DAC_SAMPLERATE=38400``
   * - **GPIO33 (ADC)**
     - Ventana 0–3,1 V; el AGC apunta a 310 mVrms en el pin, lo alcanza desde
       tan solo ≈39 mVrms, se mantiene por debajo de ≈16 mVrms, recorta por
       encima de ≈1,1 Vrms
     - ``ADC_ATTEN_DB_12``, ``AGC_TARGET_RMS=0.2``
   * - Radio **MIC IN**
     - 5–20 mVrms, a menudo con preénfasis y polarización DC para el electret
     - necesita ≈30–40 dB de atenuación
   * - Radio **DATA IN** (mini-DIN-6)
     - ≈40 mVpp ⇒ ≈14 mVrms, plano, sin preénfasis
     - necesita ≈35 dB de atenuación
   * - Radio **SPKR / AF OUT**
     - 0,1–3 Vrms, dependiente del volumen, con deénfasis
     - necesita atenuación + polarización
   * - Radio **DATA OUT / DISC**
     - 100–300 mVrms, nivel fijo, independiente del squelch, plano
     - solo necesita polarización — este es el bueno

Dos consecuencias que conviene interiorizar antes de soldar:

* **El DAC está ~35 dB demasiado caliente** para cualquier cosa de la radio.
  Una conexión directa no solo sobredesviará, sino que provocará *splatter*.
* **Un puerto DATA OUT ya está dentro de la ventana del AGC** (100–300 mVrms
  frente al rango útil 39 mVrms–1,1 Vrms). Si tu equipo tiene toma de datos, el
  lado de RX es solo una red de polarización — sin potenciómetro, sin ganancia.

Esquema funcional mínimo
------------------------

Pasivo, ~15 componentes, sin amplificadores operacionales.

.. image:: ../schematics/TX.png
   :alt: Esquema TX — DAC del ESP32 a la entrada de audio de la radio
   :align: center
   :width: 100%

.. image:: ../schematics/RX.png
   :alt: Esquema RX — salida de audio de la radio al ADC del ESP32
   :align: center
   :width: 100%

.. image:: ../schematics/PTT_opto.png
   :alt: Esquema de PTT, opción A — optoacoplador (PC817), aislado
   :align: center
   :width: 70%

.. image:: ../schematics/PTT_tr.png
   :alt: Esquema de PTT, opción B — transistor NPN (2N2222/BC547), no aislado
   :align: center
   :width: 70%

Funciones clave: R1/R2 + C2/C3 forman un paso-bajo de reconstrucción de dos
polos (fc ≈ 4,8 kHz) que elimina las imágenes del DAC a 38,4 kHz; C1 bloquea la
polarización de reposo de 1,65 V del DAC hacia la entrada de micrófono; R3/RV1
atenúan y ajustan el nivel de TX; R5/R6 fijan una polarización a media tensión
de 1,65 V para el ADC; R7/C5 amortiguan el pico de carga del condensador de
muestreo del SAR; D1/D2 limitan el pin del ADC a los raíles. Para **9600 Bd
G3RUH** reemplaza C2/C3 por 10 nF (fc ≈ 7,2 kHz) para mantener el audio plano
más allá de ~5 kHz.

.. warning::

   **El valor por defecto de PTT es una trampa.** La definición de placa que se
   distribuye es ``MODEM_PTT_ACTIVE_HIGH=1`` en esta revisión. Elige un
   controlador de PTT cuya polaridad coincida con la configuración, o cambia la
   configuración para que coincida con tu controlador. Un opto (opción A)
   invierte y encaja con ``ACTIVE_HIGH=0``; un simple NPN de conmutación por
   lado bajo (opción B) no invierte y necesita ``ACTIVE_HIGH=1``. Verifica
   siempre con un polímetro que la línea de PTT esté *abierta* durante el reset,
   durante todo el arranque de ~5 s, y en reposo **antes de conectar la radio**
   — ``modem_init()`` se bloquea ~5 s calibrando el reloj del ADC y las balizas
   transmiten al entrar, por lo que una polaridad de PTT equivocada te dará
   segundos de portadora sin modular.

Baofeng UV-5R y HT de conector K
--------------------------------

El UV-5R (y los clones de dos pines estilo Kenwood-K1: UV-82, BF-888, GT-3,
RT-5R…) expone dos conectores en lugar de una toma combinada:

.. list-table::
   :header-rows: 1
   :widths: 22 22 56

   * - Conector
     - Tamaño
     - Señal
   * - Grande
     - 3,5 mm TS
     - Punta = salida de audio SPKR, Manguito = GND
   * - Pequeño
     - 2,5 mm TRS
     - Punta = MIC in, Anillo = PTT (cortocircuitar a manguito para transmitir),
       Manguito = GND

**No hay circuito específico para el UV-5R** — construye el esquema mínimo tal
cual; solo cambia dónde aterrizan los tres cables externos, porque el equipo
divide la "radio" en dos conectores. Estos HT **no tienen toma de
discriminador**, así que el 9600 Bd G3RUH queda fuera de alcance; **AFSK 1200 Bd
Bell 202 es el techo realista** por el conector de 2 pines de serie. Verifica el
pinout del conector con un polímetro antes de soldar — los cables de terceros
baratos a veces intercambian mic y PTT.

Orden de puesta en marcha
=========================

#. **Primero el test de bucle, sin radio.** Cablea GPIO25 → GPIO33 con un cable
   simple (véase :ref:`es-web-admin` y el LOOP TEST). Si eso falla, ningún
   circuito externo ayudará.
#. **Luego el RX, aún sin TX.** Abre el squelch, inyecta tráfico real y observa
   la columna **AUDIO** de la tabla de tráfico en vivo (los mVrms del propio
   módem en el pin). Ajusta RV2 para **≈300 mVrms en los paquetes** — el
   objetivo del AGC, donde el lazo queda en la unidad con el máximo margen.
#. **Por último el TX, sobre una carga fantasma.** Ajusta RV1 para **≈3,0 kHz de
   desviación** (2,5–3,5 kHz). La sobredesviación es la causa más común de "mi
   igate escucha a todos pero nadie me escucha a mí".
#. **9600 Bd G3RUH** necesita la ruta plana/de discriminador en ambos extremos:
   DATA IN/DATA OUT, 10 nF en C2/C3, y la casilla *filtro paso-bajo de audio*
   marcada para audio plano.

Aislamiento y bucles de tierra
==============================

El circuito pasivo comparte tierra con la radio, la fuente habitual de zumbido,
ruido del alternador y "funciona hasta que transmito". Si oyes algo de eso, usa
transformadores de aislamiento de audio 600:600 Ω en lugar de C1 y C4, mantén el
opto (opción A) para que el retorno de PTT no recree la tierra que acabas de
romper, y combate la entrada de RF con cable apantallado, cables cortos, una
ferrita de pinza en el conector del equipo y 47–100 pF de cada línea de audio al
chasis de la radio.

.. _es-loop-test-tuning:

Procedimiento de ajuste
=========================

Puntos de referencia
----------------------

.. list-table::
   :header-rows: 1
   :widths: 34 20 46

   * - Señal
     - GPIO
     - Función
   * - DAC (salida de audio TX)
     - GPIO25
     - Audio de transmisión AFSK
   * - ADC (entrada de audio RX)
     - GPIO33
     - Audio de recepción AFSK
   * - Salida PTT
     - GPIO26
     - Conmutación del equipo de radio

.. list-table::
   :header-rows: 1
   :widths: 12 12 30 46

   * - Trimmer
     - Valor
     - Ubicación
     - Función
   * - **RV1**
     - 1 kΩ
     - Entre el DAC y el terminal de salida ``MIC`` de la interfaz
     - **Nivel de audio TX** — ajusta la fuerza de excitación hacia la
       entrada de micrófono/audio del equipo de radio
   * - **RV2**
     - 10 kΩ
     - Entre el terminal de entrada ``SPKR`` de la interfaz y el ADC
     - **Nivel de audio RX** — atenúa la salida de altavoz/audio del equipo
       de radio hasta el nivel que espera el ADC

1. Método A — Prueba de bucle del módem sin interfaz
--------------------------------------------------------

Úsalo para verificar de forma aislada la propia cadena DAC/ADC/AX.25 del
ESP32, antes de involucrar la placa de interfaz.

1.1 Esquema mínimo
^^^^^^^^^^^^^^^^^^^^

.. image:: /_static/tuning/tuning_1_1_es.png
   :alt: Esquema mínimo de bucle — GPIO25 (DAC1) puenteado a GPIO33 (ADC1) en la placa ESP32
   :align: center
   :width: 80%

Un solo puente, GPIO25 → GPIO33. Ambos pines son de 0–3,3 V asimétricos y
comparten el plano de masa de la placa, por lo que no hace falta ningún
condensador de acoplo ni atenuador para obtener un resultado apto/no apto.

1.2 Procedimiento
^^^^^^^^^^^^^^^^^^^

#. Cablea el puente como se indica arriba (apaga la alimentación primero).
#. Activa el módem de audio en la interfaz web (página Radio/Módem →
   "Habilitar módem ADC/DAC de audio" → Guardar → reiniciar).
#. Abre la consola serie para observar los resultados.
#. Ejecuta la autoprueba desde el botón **LOOP TEST** de la página
   Radio/Módem (o el endpoint web equivalente).
#. Lee la línea resultante ``PASS``/``FAIL``, que informa del nivel RX (mV
   RMS), la excursión bruta del ADC y la ganancia del AGC, además de — en
   caso de fallo — un diagnóstico específico por etapas.
#. Repite la prueba de 3 a 5 veces para descartar una conexión intermitente
   antes de sacar conclusiones.

1.3 Interpretación del resultado
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - Resultado
     - Significado
   * - ``PASS``
     - La cadena completa TX→RX→HDLC→CRC funciona correctamente
   * - ``FAIL``, el ADC nunca muestreó
     - Problema del controlador/inicialización del ADC, no del puente
   * - ``FAIL``, línea plana / casi continua
     - No llega señal al ADC — revisa el puente y las masas
   * - ``FAIL``, hay señal pero no hay enganche
     - El tono llega al ADC, pero el PLL del demodulador nunca se sincroniza
       — revisa la ganancia del AGC (los puntos cercanos a 1,0× indican la
       ruta del AGC) o el ajuste de velocidad en baudios/tipo de módem
   * - ``FAIL``, PLL enganchado, sin trama (etapa < FRAME)
     - La sincronización de bits nunca inicia una trama — problema más
       profundo de recuperación de bits
   * - ``FAIL``, PLL enganchado, sin trama (etapa = FRAME)
     - El encuadre funciona pero falla el CRC — señal marginal o recorte
   * - ``FAIL``, contenido no coincide
     - Se recibió la trama pero está corrupta — distorsión, recorte o un
       puente de bucle flojo

2. Método B — Bucle a través de la placa de interfaz (por RV1/RV2)
------------------------------------------------------------------------

Úsalo para validar — y ajustar — la propia placa de interfaz de audio,
incluidos ambos trimmers de nivel, sin ningún equipo de radio conectado.

2.1 Esquema mínimo
^^^^^^^^^^^^^^^^^^^^

.. image:: /_static/tuning/tuning_2_1_es.png
   :alt: Esquema mínimo de bucle por la placa de interfaz — ESP32 DAC/ADC/PTT conectados a la placa APRS_Radio_Interface, con un puente de MIC out a SPKR in
   :align: center
   :width: 100%

Cablea la placa de interfaz al ESP32 exactamente como en el funcionamiento
normal (DAC/ADC/PTT), y añade **un puente adicional en la propia placa de
interfaz, desde su terminal de salida** ``MIC`` **hasta su terminal de
entrada** ``SPKR``. No se conecta ningún equipo de radio. Esto cierra el
lazo a través de toda la cadena analógica — ambos trimmers, condensadores de
acoplo y redes de nivel.

2.2 Qué leer
^^^^^^^^^^^^^^

Cada ejecución de la autoprueba (§1.2, paso 4) informa ahora, tanto si pasa
como si falla:

* **Nivel RX**, en mV RMS
* **Excursión bruta del ADC** (mín-máx, sobre un rango de 12 bits, 0–4095) —
  el indicador de margen frente al recorte
* **Ganancia de pico del AGC**

Estos tres valores son los que sustituyen a una traza de osciloscopio a la
hora de ajustar los trimmers a continuación.

3. Procedimiento de ajuste de los trimmers (primero RV1, luego RV2)
--------------------------------------------------------------------------

Realiza esto con el bucle cableado como en el §2.1, ejecutando la autoprueba
después de cada ajuste.

#. **Empieza bajo.** Pon RV1 (nivel TX) cerca del mínimo y RV2 (nivel RX) a
   aproximadamente la mitad de su recorrido.
#. **Barre RV1 hacia arriba**, ejecutando la prueba en cada paso:

   * **Demasiado bajo:** ``FAIL`` — enganche del PLL débil o nulo, excursión
     bruta del ADC pequeña, RMS bajo.
   * **Demasiado alto:** la excursión bruta del ADC se acerca a los raíles
     (hacia 0 y/o 4095), provocando finalmente un fallo de contenido no
     coincidente o de CRC.
   * Anota el **rango de posiciones de RV1 que dan un** ``PASS`` **limpio**
     con la excursión claramente alejada de ambos raíles.

#. **Fija RV1 en el centro de ese rango válido** — no en un extremo — para
   tener el mismo margen frente a una conexión débil por un lado y frente al
   recorte por el otro.
#. **Con RV1 fijo, barre RV2** de la misma forma:

   * **Demasiado bajo:** RMS bajo, enganche DCD marginal o ausente.
   * **Demasiado alto:** la excursión se acerca a los raíles, fallos de
     distorsión/CRC.
   * Fija RV2 en el centro de su propio rango válido.

#. **Vuelve a verificar.** Ejecuta la autoprueba de 3 a 5 veces seguidas con
   los ajustes elegidos. Todas las ejecuciones deben dar ``PASS``, con la
   excursión manteniéndose alejada de ambos raíles y el RMS cómodamente por
   encima del umbral de "sin enganche" observado durante el barrido.
#. **Registra las posiciones finales** (por ejemplo, "RV1: 40% desde el
   mínimo, RV2: 55% desde el mínimo") para referencia futura, ya que los
   trimmers pueden desajustarse o recibir golpes.

No hace falta osciloscopio en ningún momento: las lecturas de
excursión/RMS/AGC de cada ejecución de la autoprueba lo sustituyen, y el
propio límite entre PASS y FAIL marca los bordes de la ventana utilizable de
cada trimmer.

Tabla de particiones
====================

El firmware distribuye una disposición de 4 MB con OTA habilitado
(``partitions.csv``):

.. list-table::
   :header-rows: 1
   :widths: 20 12 12 16 14 26

   * - Nombre
     - Tipo
     - SubTipo
     - Offset
     - Tamaño
     - Notas
   * - ``nvs``
     - data
     - nvs
     - 0x9000
     - 24 K
     -
   * - ``otadata``
     - data
     - ota
     - 0xF000
     - 8 K
     -
   * - ``phy_init``
     - data
     - phy
     - 0x11000
     - 4 K
     -
   * - ``ota_0``
     - app
     - ota_0
     - 0x20000
     - **1728 K**
     - primera ranura de app
   * - ``ota_1``
     - app
     - ota_1
     - 0x1D0000
     - **1728 K**
     - segunda ranura de app
   * - ``storage``
     - data
     - spiffs
     - 0x380000
     - **512 K**
     - montada como **LittleFS** en ``/storage``

Dos ranuras de app permiten que la actualización OTA de la administración web
grabe la ranura que no está en ejecución y revierta automáticamente si la nueva
imagen falla la autocomprobación posterior al arranque. Un dispositivo aún en la
antigua tabla de ``factory`` único necesita una regrabación por serie para migrar
a esta disposición; todas las actualizaciones posteriores pueden ir por la web.
