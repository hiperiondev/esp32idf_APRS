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

Los perfiles de 1200 Bd ejecutan **hasta ocho demoduladores en paralelo**
(``MODEM_RX_MAX_DEMODULATORS = 8``). Un demodulador es un comparador sobre la
salida de un correlador (un prefiltro pasabanda y los correladores de marca y
espacio que lo siguen): o bien cada uno tiene su propio prefiltro, con distinta
inclinación entre los tonos, o bien varios comparadores comparten un prefiltro y
se diferencian en el peso que dan al tono de espacio. En ambos casos el juego
cubre un rango de desbalance de tonos mayor que cualquier demodulador por
separado; entrega la trama el primero que la completa y las copias se descartan
comparando el FCS. El juego de demoduladores, la banda del prefiltro, el umbral de recepción,
un pasa-altos para CTCSS, el control de ganancia y una reparación opcional de
bits del FCS son ajustes de ejecución (``modem_config_t.rx``, ver
:ref:`es-radiomodem`).

Corrección de errores hacia adelante FX.25
==========================================

FX.25 envuelve AX.25 en un código Reed–Solomon, permitiendo al receptor corregir
errores de bit que de otro modo fallarían el CRC. Es totalmente retrocompatible:
una trama FX.25 lleva una trama AX.25 normal dentro de un bloque RS con etiqueta
de correlación, así que los receptores de AX.25 puro siguen decodificando la
trama interior. El modo es seleccionable: ``0`` = desactivado, ``1`` = solo RX,
``2`` = RX+TX. El códec siempre se compila — el propio ``CMakeLists.txt`` del
componente define ``ENABLE_FX25`` de forma pública — así que cambiar de modo no
requiere recompilar. La implementación RS vive en ``lwfec/`` (``rs.c``,
``gf.c``).

El códec trabaja en sitio sobre un bloque Reed–Solomon completo de 255 bytes en
todos los modos, incluidos aquellos cuya carga útil ``K`` es de solo 32 bytes:
la paridad se traslada al final del bloque y el hueco se rellena con ceros. Por
tanto el búfer de quien llama debe medir 255 bytes sea cual sea la ``K`` que
pase. Por eso ``Fx25Encode()``/``Fx25Decode()`` y ``RsEncode()``/``RsDecode()``
reciben la capacidad del búfer como argumento explícito: se comprueba con
``assert`` en compilaciones de depuración y hace fallar la llamada de forma
segura en el resto, y ``ax25.c`` lo respalda con una comprobación en tiempo de
compilación sobre los dos búferes que entrega.

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
     - Cuántas veces el piso anti-inanición de CSMA forzó una transmisión tras
       una tanda de espera que encontró el canal libre en todas las ranuras y
       falló el sorteo de persistencia en todas ellas. Mide únicamente el
       ``persist`` configurado: con el valor por omisión de 63, alrededor de una
       de cada diez portadoras termina así. No se descarta nada, así que es una
       estadística de acceso al canal y no un descarte.
   * - ``modem_channel_busy_count()``
     - Cuántas veces ese mismo piso forzó una transmisión tras una tanda en la
       que al menos una ranura encontró la detección de portadora activa. Es un
       informe de congestión de la frecuencia: la trama sale por encima del
       tráfico que ya estaba allí. Cada tanda se carga a exactamente uno de los
       dos contadores, así que un canal ocupado nunca puede inflar la cifra de
       persistencia.
   * - ``modem_measure_adc_rate(ms)``
     - Medir la tasa real de muestreo del ADC; se bloquea durante la ventana
       pedida.

La cabecera lleva además ``MODEM_DEFAULT_CONFIG()`` (un inicializador de
``modem_config_t``), el ayudante ``MODEM_DELAY_TICKS(ms)``, ``modem_rx_frame_t``
y el tipo de callback ``modem_rx_cb_t``. Nótese que **no** hay punto de entrada
de desmontaje: el módem se levanta una vez por arranque y se reconfigura en su
sitio con ``modem_set_modem()``.

Los tres puntos de entrada de transmisión — ``modem_send_raw()``,
``modem_build_frame_tnc2()`` y ``modem_send_tnc2()`` — se pueden llamar desde
cualquier tarea. Comparten un mutex interno, porque comparten el acumulador de
CRC saliente, el anillo de transmisión de productor único y la máquina de
estados que keyea desde él. ``modem_send_tnc2()`` sostiene ese mutex a lo largo
del armado y del encolado, así que una trama siempre llega al anillo con la
suma de verificación acumulada para ella, incluso cuando una baliza sale justo
en el momento en que el IGate retransmite una línea de APRS-IS. Un llamador que
no consigue el camino en un segundo recibe ``ESP_ERR_TIMEOUT`` (o ``0`` del
constructor) en vez de quedarse esperando detrás indefinidamente.

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
     - entrada plana/de discriminador: activada para una toma de datos o de
       discriminador, desactivada para una salida de altavoz; elige la tabla
       de inclinaciones de prefiltro de los preajustes
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
     - tiempo de silencio CSMA: cuánto espera una trama encolada antes de que
       empiece siquiera el acceso al canal. El intervalo entre los sorteos de
       persistencia que vienen después es el *SlotTime* fijo de AX.25 que el
       módem mantiene internamente, no este valor. Ignorado en full duplex.
   * - ``persist``
     - ``csma_persist`` (63)
     - p-persistencia CSMA (el *Persist* estándar de AX.25/KISS): una vez que el
       canal se oye libre, el módem transmite con probabilidad ``persist``/256
       por ranura y si no espera otra ranura antes de volver a tirar. 255 =
       transmitir siempre en la primera ranura libre; valores más bajos separan
       a las estaciones que compiten. Ocho sorteos fallidos transmiten de todas
       formas, de modo que una trama nunca queda retenida indefinidamente.
       Ignorado en full duplex.
   * - ``fx25_mode``
     - ``fx25_mode``
     - 0=off, 1=solo RX, 2=RX+TX
   * - ``ptt_active_high``
     - ``MODEM_PTT_ACTIVE_HIGH``
     - cableado de placa en compilación, no un campo de configuración
   * - ``min_unkey_ms``
     - ``ptt_min_unkey_ms``
     - tiempo mínimo extra de PTT-desactivado entre transmisiones
   * - ``adc_self_bias``
     - ``adc_self_bias`` (apagado)
     - polariza el pad del ADC con su propio pull-up y pull-down en serie, para
       una entrada acoplada por capacitor sin red de polarización externa. Se
       aplica después de que el controlador continuo configura el pad, que
       desconecta ambas resistencias. Solo GPIO32/33
   * - ``rx_clip_warn``
     - ``rx_clip_warn`` (apagado)
     - registra un aviso con límite de frecuencia cuando un bloque procesado
       llega a los extremos del rango de conversión
   * - ``dac_amplitude_pct``
     - ``dac_amplitude_pct`` (``MODEM_DAC_AMPLITUDE_PCT``)
     - amplitud de salida, aplicada por muestra. Con piso en 20 %: el DAC es de
       8 bits, así que la atenuación que necesita una entrada de micrófono
       corresponde a un atenuador externo
   * - ``dac_samplerate``
     - ``dac_samplerate`` (``MODEM_DAC_SAMPLERATE``)
     - 38400 o 76800 Hz. El único campo que ``modem_set_modem()`` **no** aplica:
       el período del reloj de muestreo y todos los pasos de fase derivados de
       él se programan con el hardware detenido, así que lo aplica
       ``modem_init()`` y el cambio surte efecto en el próximo reinicio
   * - ``tx_max_keyed_ms``
     - ``tx_max_keyed_ms`` (0)
     - tiempo máximo de transmisión, 0 = desactivado. Pasado ese tiempo, la
       tarea de servicio del módem libera el PTT, detiene el modulador y
       descarta la transmisión
   * - ``rx``
     - ``rx_tuning`` (``MODEM_RX_TUNING_DEFAULT()``)
     - cadena de recepción: preajuste de demoduladores e inclinaciones
       personalizadas, banda y longitud del prefiltro, umbral de recepción,
       pasa-altos de CTCSS, ganancia automática o fija, reparación de bits;
       acotada por ``modem_rx_tuning_sanitize()``

.. note::

   El GPIO de PTT **no** es un campo de ``modem_config_t`` — es una elección de
   cableado de placa fija en compilación (``MODEM_PTT_GPIO``), como los pines
   ADC/DAC. Solo el *nivel* activo se pasa en ejecución, y también viene
   directamente de la macro de compilación. Explícitamente **no** mapeados en
   ejecución (sin equivalente en el componente): pines y atenuación ADC/DAC,
   squelch por hardware, conmutador de potencia RF, volumen de RX y el techo
   del AGC. El umbral de recepción por software y una ganancia fija de
   recepción forman parte de ``rx``.

NIVEL RX y PRUEBA TX
====================

El loop test de más abajo necesita un cable entre el DAC y el ADC, así que
deja de ser utilizable en cuanto un equipo reemplaza ese puente: no hay nada
que devuelva la trama. Dos botones a su lado cubren el mismo terreno con un
equipo conectado, uno por sentido.

**NIVEL RX** (``aprs_rx_level_sample()``, ``POST /radio/level``) observa la
etapa de recepción durante alrededor de un segundo e informa el nivel de la
banda de tonos (``afskGetBandRms()``) y el nivel RMS de banda ancha con sus
picos, un veredicto en una palabra (saturado, sin señal, bajo por debajo de
100 mV RMS de tonos, bueno), el offset de continua de la entrada, la ganancia
del AGC, los extremos crudos de conversión en toda la ventana y el estado de la
detección de portadora, seguidos de las
estadísticas de recepción de ``modem_get_rx_stats()`` (tramas decodificadas y
exclusivas de cada demodulador, tramas entregadas y reparadas, muestras
perdidas). No transmite
nada ni cambia el estado del módem, así que puede ejecutarse mientras se
decodifica tráfico real. Es contra lo que se ajusta el trimmer de recepción —
apunte a 250 a 350 mV RMS con el rango crudo lejos de 0 y 4095 — y lo que
distingue una entrada polarizada por ``adc_self_bias`` (1200 a 2000 mV) de una
sin polarización alguna.

**PRUEBA TX** (``aprs_tx_test_run()``, ``POST /radio/txtest``) activa el
transmisor y modula una trama de estado corta por la ruta de transmisión no
crítica habitual, de modo que se aplican tanto el acceso al canal en
semidúplex como el techo de ciclo de trabajo. No espera nada de vuelta: la
desviación que produce se lee en otro instrumento y se ajusta a 2,5 a 3,5 kHz.

Ambos comparten la bandera de reserva del loop test, así que solo uno de los
tres se ejecuta a la vez.

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
#. Espera a que se libere la detección de portadora del demodulador antes de
   activar el PTT, como mucho ``LOOP_TEST_CHANNEL_WAIT_MS`` (**3000 ms**), para
   no transmitir el tono de autoprueba encima de una estación que esté al aire
   en ese momento — la lectura es independiente del indicador de dúplex recién
   fijado, que solo condiciona el CSMA. Un canal que siga ocupado al llegar al
   tope se registra y la prueba transmite igualmente.
#. Transmite, luego espera hasta ``LOOP_TEST_TIMEOUT_MS`` (**4000 ms**) a que la
   cadena ADC → demodulador → HDLC → AX.25 devuelva la misma trama.
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
