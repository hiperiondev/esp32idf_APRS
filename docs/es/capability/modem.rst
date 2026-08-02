.. _es-modem:

=====================
El módem por software
=====================

El componente ``esp32idf_radioamateur_modem`` (integrado bajo ``components/``,
GPL-3.0) es el corazón del proyecto: un módem por software AFSK/FSK completo que
demodula y modula audio APRS enteramente en el ESP32, usando únicamente el
SAR-ADC, el DAC y un GPTimer. Este capítulo cubre el módem como *capacidad* — sus
perfiles, su API pública y su configuración en ejecución. Para los internos del
DSP y el razonamiento tras las elecciones de tasa de muestreo y núcleo, véase
:ref:`es-dsp-signal-chain`.

Perfiles del módem
==================

Los perfiles seleccionables (``modem_mode_t``) están numerados de forma idéntica
al desplegable de *modulación* de la administración web, por lo que la aplicación
puede convertir el valor guardado directamente al enum:

.. list-table::
   :header-rows: 1
   :widths: 10 30 16 44

   * - Valor
     - Perfil
     - Baudios
     - Tonos
   * - 0
     - AFSK300
     - 300
     - 1600 / 1800 Hz
   * - 1
     - **Bell 202** (por defecto, APRS estándar)
     - 1200
     - 1200 / 2200 Hz
   * - 2
     - ITU V.23
     - 1200
     - 1300 / 2100 Hz
   * - 3
     - G3RUH FSK
     - 9600
     - —

El perfil de 1200 Bd ejecuta **dos demoduladores en paralelo**, sintonizados
ligeramente distinto, para elevar la probabilidad de decodificación
(``MODEM_MAX_DEMODULATOR_COUNT = 2``).

Corrección de errores hacia adelante FX.25
==========================================

FX.25 envuelve AX.25 en un código Reed–Solomon, permitiendo al receptor corregir
errores de bit que de otro modo fallarían el CRC. Es totalmente retrocompatible:
una trama FX.25 lleva una trama AX.25 normal dentro de un bloque RS con etiqueta
de correlación, así que los receptores de AX.25 puro siguen decodificando la
trama interior. El modo es seleccionable: ``0`` = desactivado, ``1`` = solo RX,
``2`` = RX+TX (requiere ``-DENABLE_FX25`` en compilación). La implementación RS
vive en ``lwfec/`` (``rs.c``, ``gf.c``).

API pública
===========

La cabecera pública del componente (``esp32idf_radioamateur_modem.h``) expone:

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - Función
     - Propósito
   * - ``modem_init(cfg)``
     - Levanta el hardware y arranca las tareas de servicio internas. Se bloquea
       ~5 s una vez por arranque calibrando el reloj real del ADC.
   * - ``modem_set_modem(cfg)``
     - Cambiar el perfil activo y ajustes relacionados en ejecución.
   * - ``modem_set_rx_callback(cb, ctx)``
     - Instalar el callback invocado por cada trama decodificada.
   * - ``modem_send_raw(frame, len)``
     - Encolar una trama AX.25 cruda (sin flags/stuffing/FCS — todo añadido
       automáticamente).
   * - ``modem_build_frame_tnc2(tnc2, out, out_len)``
     - Construir una trama cruda desde una cadena de monitor TNC2.
   * - ``modem_send_tnc2(tnc2)``
     - Construir + encolar en una sola llamada.
   * - ``modem_format_tnc2(msg, out, out_len)``
     - Renderizar una trama decodificada de vuelta a una cadena TNC2.
   * - ``modem_tx_queue_depth()``
     - Número de tramas todavía encoladas/en vuelo en TX de RF (0 = inactivo).
       Es el estado del anillo de TX que lee el tope de backlog de TX de RF.
   * - ``modem_persistence_missed_count()``
     - Cuántas veces el piso anti-inanición de la persistencia CSMA forzó una
       transmisión desde el arranque, para quien quiera exponerlo como
       estadística.
   * - ``modem_measure_adc_rate(ms)``
     - Medir la tasa real de muestreo del ADC; se bloquea durante la ventana
       pedida.

La cabecera lleva además ``MODEM_DEFAULT_CONFIG()`` (un inicializador de
``modem_config_t``), el ayudante ``MODEM_DELAY_TICKS(ms)``, ``modem_rx_frame_t``
y el tipo de callback ``modem_rx_cb_t``. Nótese que **no** hay punto de entrada
de desmontaje: el módem se levanta una vez por arranque y se reconfigura en su
sitio con ``modem_set_modem()``.

Configuración en ejecución (``modem_config_t``)
===============================================

Construida en exactamente un lugar — ``aprs_service_build_modem_config()`` —
compartida por el arranque, el Guardar de la página Radio (reaplicación en vivo,
sin reinicio) y el test de bucle:

.. list-table::
   :header-rows: 1
   :widths: 24 30 46

   * - Campo
     - Origen
     - Notas
   * - ``modem``
     - ``afsk_modem_type``
     - conversión directa; la página fija 0–3
   * - ``flat_audio``
     - ``audio_lpf``
     - pese al nombre, siempre es la bandera de entrada de audio plano
   * - ``full_duplex``
     - ``false`` normalmente
     - LOOP TEST pasa ``true`` (un cable DAC→ADC significa que CSMA nunca ve el
       canal libre)
   * - ``allow_non_aprs``
     - ``false``
     - ¿aceptar Control/PID distintos de 0x03/0xF0?
   * - ``preamble_ms``
     - ``preamble`` (300)
     - TXDelay
   * - ``slot_time_ms``
     - ``tx_timeslot`` (2000)
     - tiempo de silencio CSMA; ignorado en full duplex
   * - ``persist``
     - ``csma_persist`` (63)
     - p-persistencia CSMA (el *Persist* estándar de AX.25/KISS): una vez que el
       canal se oye libre, el módem transmite con probabilidad ``persist``/256
       por ranura y si no espera otro ``slot_time_ms`` antes de volver a tirar.
       255 = transmitir siempre en la primera ranura libre; valores más bajos
       separan a las estaciones que compiten. Ignorado en full duplex.
   * - ``fx25_mode``
     - ``fx25_mode``
     - 0=off, 1=solo RX, 2=RX+TX
   * - ``ptt_active_high``
     - ``MODEM_PTT_ACTIVE_HIGH``
     - cableado de placa en compilación, no un campo de configuración
   * - ``min_unkey_ms``
     - ``ptt_min_unkey_ms``
     - tiempo mínimo extra de PTT-desactivado entre transmisiones

.. note::

   El GPIO de PTT **no** es un campo de ``modem_config_t`` — es una elección de
   cableado de placa fija en compilación (``MODEM_PTT_GPIO``), como los pines
   ADC/DAC. Solo el *nivel* activo se pasa en ejecución, y también viene
   directamente de la macro de compilación. Explícitamente **no** mapeados en
   ejecución (sin equivalente en el componente): pines y atenuación ADC/DAC,
   squelch por hardware, conmutador de potencia RF, squelch por software,
   volumen de RX y el techo del AGC.

El LOOP TEST
============

La herramienta de puesta en marcha más útil del proyecto. Cablea
**GPIO25 → GPIO33**, abre *Radio / Modem*, pulsa **LOOP TEST**.
``aprs_loop_test_run()``:

#. Construye un pequeño paquete APRS que lleva un **token aleatorio de un solo
   uso** (``>LOOPTEST <token>``).
#. **Desvía** las tramas decodificadas a su propio gancho para que la trama de
   prueba nunca se digipetee, suba, ni se registre como tráfico real.
#. Conmuta el módem a **full dúplex** — un cable DAC→ADC significa que el nodo
   siempre oye su propia portadora y CSMA nunca activaría la radio.
#. Transmite, luego espera hasta **4000 ms** a que la cadena ADC → demodulador →
   HDLC → AX.25 devuelva la misma trama.
#. **Siempre restaura** el gancho real y el modo dúplex configurado antes de
   volver.

Mientras tanto una tarea de monitor captura diagnósticos que el componente solo
expone instantáneamente: una instantánea del ADC crudo pasiva a mitad de
preámbulo, RMS de pico, ganancia de AGC de pico, un mapa de bits de DCD, y la
etapa RX de HDLC más lejana alcanzada por demodulador. El mensaje de resultado
distingue:

.. list-table::
   :header-rows: 1
   :widths: 46 54

   * - Síntoma
     - Diagnóstico
   * - ADC crudo mín ≈ máx
     - ADC muerto / sin cablear
   * - el crudo oscila, RMS ~0
     - ningún tono llega al ADC
   * - RMS bien, DCD nunca activo
     - el PLL nunca enganchó → desajuste de baudios/tipo de módem o audio malo
   * - DCD activo, etapa < FRAME
     - flags vistos pero ninguna trama comenzó — problema de recuperación de
       bits, no ruido
   * - DCD activo, etapa = FRAME, sin trama
     - tramas ensambladas pero fallaron el CRC — nivel/SNR marginal
   * - trama de vuelta, token no coincide
     - distorsión, recorte, o cableado de bucle equivocado
   * - PASS
     - reporta el nivel de RX en mV RMS
