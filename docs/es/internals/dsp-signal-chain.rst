.. _es-dsp-signal-chain:

======================
La cadena de señal DSP
======================

Este capítulo explica *cómo* el módem convierte audio de radio en tramas y de
vuelta, y — igual de importante — *por qué* los números son los que son. La
cabecera de configuración del módem está inusualmente bien documentada, y el
razonamiento importa si alguna vez la tocas.

La cadena, etapa por etapa
==========================

.. list-table::
   :header-rows: 1
   :widths: 44 22 34

   * - Etapa
     - Tasa
     - Dónde
   * - SAR-ADC1 continuo/DMA, tramas de conversión de 128 muestras
     - **76 800 Hz**
     - ISR del controlador en núcleo 0
   * - ingesta: des-intercambio de pares, eliminación de offset DC, AGC, medición RMS
     - 76 800 Hz
     - ``afsk.c``
   * - FIR de diezmado (ratio **8:1**)
     - → **9 600 Hz**
     - ``afsk.c``
   * - correlador (mark/space), paso-bajo, DPLL, decodificación NRZI
     - 9 600 Hz
     - ``modem.c``
   * - des-encuadre HDLC, des-stuffing de bits, comprobación FCS, decodificación RS FX.25
     - —
     - ``ax25.c`` / ``fx25.c``
   * - ⟵ TX ⟶ codificación AX.25, FCS, bit stuff, NRZI, acumulador de fase de 32 bits,
       LUT de seno de 512 entradas
     - **38 400 Hz**
     - ``ax25.c`` / ``modem.c`` / ``afsk.c``

Por qué los números son los que son
===================================

**ADC a 76 800 Hz, no 38 400.**
   38 400 da al perfil de 9600 Bd exactamente *cuatro* muestras de ADC por
   símbolo. El instante de muestreo del DPLL queda entonces cuantizado al 25 % de
   un símbolo y el voto por mayoría de tres muestras abarca el 75 % de un símbolo
   — la ventana de voto siempre alcanza una transición. La simulación en host del
   ``modem.c`` real, con relojes reales y **sin ruido**, produjo errores de bit
   duros en cada fase donde los instantes del ADC se alinean con los instantes de
   actualización del DAC; los dos relojes difieren en ~0,05 %, así que la
   alineación recorre esas fases cada ~55 ms. A 76 800 la misma simulación da cero
   errores de bit en cada fase y con hasta 30 µs de fluctuación de flanco de TX.
   Los perfiles AFSK nunca les importó (se demodulan a 9600 Hz a través de un
   correlador tras el diezmado) y miden idéntico a cualquier tasa. **Coste:** el
   doble de trabajo de RX DSP, y ``MODEM_RESAMPLE_RATIO`` pasa a 8, lo que
   requiere el FIR de diezmado más largo — un filtro de 8 taps cortado para 4:1
   no hace antialias de 8:1.

**El DAC se queda a 38 400 Hz** (= 32 × 1200, un múltiplo exacto de cada tasa de
   baudios soportada). El transmisor pone los flancos de símbolo exactamente en
   muestras del DAC sea cual sea la tasa; era el *receptor* el que necesitaba
   resolución.

**``MODEM_ADC_CONV_FRAME = 128``, no el tamaño de bloque.**
   La propia ISR del ADC del IDF llama a ``xRingbufferSendFromISR()``, que hace
   todo el ``memcpy`` **dentro** de ``portENTER_CRITICAL_ISR()``. En Xtensa eso
   sube ``PS.INTLEVEL`` a 3 — y el reloj de muestreo del DAC *es* una interrupción
   de nivel 3. Así que la ISR del DAC queda enmascarada durante la copia: 768
   muestras ≈ 11 µs (10 % de un símbolo a 9600 Bd — fatal); 128 muestras ≈ 2 µs
   (2 % — dentro del presupuesto). Ninguna cantidad de ``IRAM_ATTR`` de nuestro
   lado ayuda: el código bloqueante es del controlador, ya en IRAM, y simplemente
   largo. A 1200 Bd, 11 µs es el 1,3 % de un símbolo e invisible — que es
   exactamente por qué cada perfil AFSK pasaba mientras G3RUH perdía tramas.

**``MODEM_DAC_TIMER_CORE (1) ≠ MODEM_ADC_ISR_CORE (0)``.**
   ``portENTER_CRITICAL_ISR()`` enmascara nivel ≤ 3 solo en el núcleo *local*. Pon
   el reloj del DAC en el otro núcleo y la ISR del ADC solo gira esperando el
   cerrojo en lugar de enmascararlo. Forzado con ``#error``. Las dos correcciones
   (tramas pequeñas, núcleos separados) son independientes y ambas se aplican.

**``ModemCalibrateSampleRate()``.**
   ``modem_init()`` se bloquea ~5 s al arrancar midiendo la tasa *real* del ADC
   (``modem_measure_adc_rate()``), porque el paso del PLL de cada perfil se
   calcula a partir del ratio ADC/DAC *nominal* y la diferencia es de otro modo un
   error de estado estacionario que el DPLL debe seguir durante toda una
   transmisión. La tasa de alarma del DAC ya se conoce exactamente de la
   configuración del temporizador, así que solo el lado del ADC necesita
   medición. Ambos relojes derivan del mismo cristal, así que el ratio es una
   propiedad fija de la placa: medida **una vez por arranque**, reaplicada en cada
   cambio de perfil.

**``MODEM_RX_FIFO_SIZE = 4096`` muestras.**
   Dimensionado en *muestras*, así que encogió en *tiempo* cuando la tasa se
   duplicó (2048 eran 53 ms a 38,4 k, solo 26,7 ms a 76,8 k — apenas un bloque de
   20 ms). 4096 restaura el margen; debe contener ≥ 2 bloques, ya que
   ``AFSK_Poll()`` consume solo bloques enteros.

Guardas de compilación
======================

Guardas ``#error`` de compilación fuerzan: pin del DAC ∈ {25, 26}; pin del ADC ∈
32–39; ``MODEM_ADC_SAMPLERATE % 9600 == 0``; FIFO ≥ 2 bloques;
``MODEM_ADC_CONV_FRAME`` par, que divida ``MODEM_BLOCK_SIZE``, y alineado a bytes
a ``SOC_ADC_DIGI_DATA_BYTES_PER_CONV``; núcleo del temporizador DAC ≠ núcleo de la
ISR del ADC; prioridad del temporizador DAC ∈ 1..3.

Referencia de configuración en compilación
==========================================

Todo en
``components/esp32idf_radioamateur_modem/include/esp32idf_radioamateur_modem_config.h``,
cada macro protegida con ``#ifndef`` para que el sistema de compilación la pueda
sobreescribir.

.. list-table::
   :header-rows: 1
   :widths: 34 16 50

   * - Macro
     - Por defecto
     - Significado
   * - ``MODEM_DAC_GPIO``
     - 25
     - salida de audio; solo 25 o 26
   * - ``MODEM_ADC_GPIO``
     - 33
     - entrada de audio; solo 32–39
   * - ``MODEM_PTT_GPIO``
     - −1
     - pin de PTT (cableado de placa). El valor por defecto de la cabecera es −1
       (deshabilitado); el ``CMakeLists.txt`` de nivel superior de este proyecto
       lo sobrescribe a 26.
   * - ``MODEM_PTT_ACTIVE_HIGH``
     - 1
     - polaridad de PTT
   * - ``MODEM_LED_TX_GPIO`` / ``_RX_GPIO``
     - −1
     - LEDs de estado
   * - ``MODEM_DAC_SAMPLERATE``
     - 38400
     - = 32 × 1200
   * - ``MODEM_ADC_SAMPLERATE``
     - 76800
     - = 8 × 9600
   * - ``MODEM_DAC_AMPLITUDE_PCT``
     - 60
     - oscilación del DAC, % de 0–3,3 V
   * - ``MODEM_ADC_ATTEN``
     - ``ADC_ATTEN_DB_12``
     - ventana ≈ 0–3,1 V
   * - ``MODEM_RX_FIFO_SIZE``
     - 4096
     - muestras, potencia de dos
   * - ``MODEM_ADC_CONV_FRAME``
     - 128
     - muestras por trama DMA
   * - ``MODEM_RX_TASK_PRIO`` / ``_STACK`` / ``_CORE``
     - 10 / 4096 / 0
     - tarea RX DSP
   * - ``MODEM_ADC_ISR_CORE``
     - 0
     - núcleo de la ISR DMA del ADC
   * - ``MODEM_DAC_TIMER_CORE``
     - 1
     - **debe diferir del núcleo de la ISR del ADC**
   * - ``MODEM_DAC_TIMER_INTR_PRIO``
     - 3
     - 1..3
   * - *(derivado)* ``MODEM_DEMOD_SAMPLERATE``
     - 9600
     - fijo
   * - *(derivado)* ``MODEM_RESAMPLE_RATIO``
     - 8
     - ADC ÷ demod
   * - *(derivado)* ``MODEM_BLOCK_SIZE``
     - 1536
     - 20 ms a 76,8 kHz

Los archivos fuente del módem
=============================

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Archivo
     - Rol
   * - ``src/afsk.c`` (~1440 lín)
     - ingesta DMA del ADC, AGC, FIR de diezmado, ISR del DAC, PTT
   * - ``src/modem.c`` (~870 lín)
     - correladores, DPLL, tablas de tonos, DCD, calibración
   * - ``src/ax25.c`` (~1500 lín)
     - encuadrador HDLC, NRZI, bit-stuffing, códec AX.25, cola de TX
   * - ``src/fx25.c``, ``lwfec/rs.c``, ``lwfec/gf.c``
     - FEC Reed–Solomon FX.25
   * - ``src/crc_ccit.c``
     - FCS (secuencia de comprobación de trama)
