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
   * - ingesta: des-intercambio de pares, supresor de impulsos
       (``impulse_blanker.h``: mediana de cinco, picos reemplazados por
       interpolación, retardo de dos muestras), eliminación de offset DC,
       medición RMS de banda ancha
     - 76 800 Hz
     - ``afsk.c``
   * - FIR de diezmado (48 coeficientes, ratio **8:1**), pasa-altos de
       CTCSS y graves (300 Hz por omisión), medidor de nivel de la banda de
       tonos y decisión del umbral de recepción, AGC o ganancia fija, anillo de
       retención del umbral
     - → **9 600 Hz**
     - ``afsk.c``
   * - por correlador (hasta tres): prefiltro pasabanda, correladores de
       mark/space, magnitudes de los tonos, paso-bajo, seguimiento del
       desbalance de tonos
     - 9 600 Hz
     - ``modem.c``
   * - por demodulador (hasta ocho): comparador sobre las magnitudes de un
       correlador, DCD, DPLL, decodificación NRZI
     - 9 600 Hz
     - ``modem.c``
   * - des-encuadre HDLC, des-stuffing de bits, comprobación FCS (reparación de
       bits opcional), decodificación RS FX.25, supresión de duplicados entre
       demoduladores
     - —
     - ``ax25.c`` / ``fx25.c``
   * - ⟵ TX ⟶ codificación AX.25, FCS, bit stuff, NRZI, acumulador de fase de 32 bits,
       LUT de seno de 512 entradas
     - **38 400 Hz**
     - ``ax25.c`` / ``modem.c`` / ``afsk.c``

Qué da por bueno el decodificador de tramas
===========================================

``ax25_decode()`` es un punto de entrada público del componente del módem, así
que valida su propia entrada en vez de confiar en el productor que llena el
búfer. Más allá del largo mínimo de cabecera (destino + origen + control + PID =
16 bytes) no lee nada sin medirlo antes contra ``len``: el campo de direcciones
se recorre de a una dirección, y tanto el octeto de extensión como los siete
bytes de la repetidora que promete tienen que seguir dentro de la trama. Una
recepción truncada o corrupta cuyos bits de extensión nunca terminan se rechaza,
en vez de decodificar bytes que quedan más allá de la trama — en la ruta de RX
esos bytes son la cola de la trama recibida anteriormente, que si no se
convertirían en indicativos de repetidora verosímiles dentro de una
decodificación por lo demás válida.

Qué garantiza el codificador de tramas
======================================

En transmisión, ``Ax25GetTxBit()`` aplica el bit stuffing a la trama y a su FCS
como un único campo HDLC continuo: tras cada racha de cinco bits en 1 se inserta
un 0, y el contador de la racha atraviesa el límite entre el último byte de datos
y la FCS. Cuando una racha de cinco 1 termina justo en el último bit de la FCS,
el 0 de relleno se envía igual, *antes* del flag de cierre. Si faltara, el
receptor tomaría el 0 inicial del flag como el bit de relleno y el flag de cierre
se completaría con solo seis bits de datos desde el último límite de byte en vez
de siete. El decodificador propio no cuenta esos bits, pero otros más estrictos
sí — ``hdlc_rec.c`` de Direwolf descarta esa trama — y con contenido aleatorio el
caso se da en alrededor de una trama de cada sesenta. Los bloques FX.25 no se ven
afectados: ``writeFx25Frame()`` rellena la FCS por su cuenta antes de la
codificación Reed–Solomon.

El campo de direcciones termina siempre en la última dirección en uso.
``ax25_encode()`` y ``hdlcFrame()`` ponen el bit de fin de dirección (bit 0 del
octeto SSID) en esa dirección y lo borran en todas las demás, incluido el caso
completo de destino, origen y ocho digipetidores, en el que no queda ninguna
ranura vacía detrás de la última. Una trama sin ese bit la rechazan Direwolf y
los TNC KISS, y el receptor propio tomaría el byte de control como el final del
campo de direcciones; como el digipetidor vuelve a codificar desde texto TNC2
cada trama que repite, una trama legal de ocho digipetidores sale tan legal como
entró.

Cada trama se cierra con una bandera seguida de la cola de TX
(``Ax25TxTail()``, 20 ms por defecto, redondeada hacia arriba a banderas
enteras), y solo entonces la ISR libera el PTT. La desactivación publica su
estado en un orden fijo - desmontaje pendiente, luego transmisor inactivo y PTT
liberado, luego la etapa de activación de vuelta a reposo - y
``Ax25TransmitCheck()`` no activa la trama siguiente mientras el modulador
funcione o el desmontaje siga pendiente. Así una nueva activación nunca puede
empezar dentro de la desactivación anterior, lo que en dúplex completo (sin
tiempo de silencio) dejaría el transmisor bloqueado hasta reiniciar.

``Ax25GetTxBit()`` corre en la ISR del temporizador del DAC, que es segura
frente a la caché (``CONFIG_GPTIMER_ISR_CACHE_SAFE``) y por eso sigue corriendo
mientras una escritura de LittleFS o una actualización OTA tienen la caché de la
flash deshabilitada. Todo lo que esa ISR lee está en IRAM o DRAM. En particular
nunca toca ``Fx25ModeList``, la tabla de modos FX.25 en flash:
``writeFx25Frame()`` copia la etiqueta de correlación, ``K`` y ``T`` del modo
elegido en el propio descriptor de la trama, en contexto de tarea, y la ISR
envía la etiqueta desde esa copia. La misma regla cubre el resto de la cadena de
llamadas (``MODEM_BAUDRATE_TIMER_HANDLER()``, ``txRetireFrame()``,
``ModemTransmitStop()``, ``setTransmit()``, ``setPtt()``, ``LED_Status2()``),
cuyas funciones son ``IRAM_ATTR`` y cuyas tablas y constantes (``sin_table``, el
número de pin del PTT) son ``DRAM_ATTR``.

Esto lo fija un test de regresión en el host,
``components/esp32idf_radioamateur_modem/test/host/``. Compila los fuentes reales
``ax25.c``, ``crc_ccit.c`` y los de FX.25 contra pequeñas cabeceras sustitutas,
con AddressSanitizer y UndefinedBehaviorSanitizer, y envía 20 000 tramas UI
aleatorias (30–180 bytes) por ``Ax25WriteTxFrame()`` / ``Ax25TransmitCheck()`` /
``Ax25GetTxBit()``. Cada transmisión se compara bit a bit con un codificador HDLC
de referencia y se entrega tanto al receptor propio como a un receptor que sigue
las reglas de Direwolf; una segunda pasada envía 2 000 tramas como bloques FX.25
por el receptor propio, con las páginas que contienen ``Fx25ModeList`` sin
mapear mientras se sacan los bits, así que cualquier lectura de la tabla desde
la ruta de bits de transmisión falla igual que con la caché deshabilitada. Una
tercera pasada arma tramas con 0 a 8 digipetidores desde texto TNC2 y verifica
el bit de fin de dirección, ``ax25_decode()`` y una transmisión completa por el
receptor propio; una cuarta corre en semidúplex y verifica que una ranura de
tiempo 0 transmite cada trama de inmediato mientras que una distinta de cero
retiene cada trama al menos ese tiempo; una quinta verifica que cada longitud de
cola da exactamente una bandera de cierre más las banderas de la cola, y que una
trama encolada no se activa mientras el modulador sigue funcionando o su
desmontaje sigue pendiente. La corrida informa además cuántas tramas
terminaron su FCS en cinco 1 (unos cientos), así que un PASS siempre cubre el
caso.

.. code-block:: bash

   cd components/esp32idf_radioamateur_modem/test/host
   make              # semilla por defecto
   make SEED=0x2a    # cualquier otra semilla

El ajuste de recepción
======================

Todo lo siguiente se fija en ejecución mediante ``modem_config_t.rx`` (el grupo
*Demodulador de recepción* de :ref:`es-radiomodem`) y lo aplica
``afskSetModem()`` con la tarea de recepción detenida, así que ningún bloque se
procesa con una cadena a medio construir.

**Correladores y comparadores.**
   Cada correlador mide la magnitud verdadera ``sqrt(I² + Q²)`` del tono de
   marca y del de espacio; ``|I| + |Q|`` oscilaría entre 1 y 1,41 veces ese
   valor según la fase del tono, lo que equivale a 3 dB de ruido sobre la
   decisión. Luego un comparador enfrenta la magnitud de marca con la de
   espacio multiplicada por su propio peso. En el aire el desbalance entre
   tonos va de nulo (transmisor plano por puerto de datos sobre una salida de
   discriminador) a 5-12 dB a favor del tono de espacio (transmisor con
   preénfasis sobre una salida de discriminador), y una salida de altavoz
   desplaza ambos por el deénfasis del receptor, así que una única decisión sin
   peso falla más allá de unos ±12 dB de desbalance.

   Las dos mitades lo compensan. Los prefiltros se diseñan en ``ModemInit()``
   (muestreo en frecuencia, ventana de Hamming, fase lineal, pico de la banda
   de paso escalado a la unidad para que los caminos int16 e int32 no se
   desborden) a partir de los bordes de banda, la longitud y una
   *inclinación*; la inclinación que alcanzan de verdad, medida sobre los
   coeficientes, se escribe en el registro y la usa la estimación del
   desbalance. El peso del comparador es exacto y casi gratuito, porque el
   paso-bajo posterior a la detección es lineal y puede correr sobre las dos
   magnitudes antes de ponderarlas: varios comparadores comparten un
   correlador a cambio de una multiplicación y una resta cada uno. Lo que un
   comparador no puede hacer es mantener un tono de espacio fuerte fuera del
   correlador de marca: el correlador dura un símbolo, así que su respuesta es
   lo bastante ancha como para que el otro tono se cuele, y sólo un filtro
   previo lo quita. Por eso el juego por omisión ``MODEM_RX_EQ_MULTISLICE``
   usa tres prefiltros, cada uno inclinado hacia el centro de su parte del rango
   (+6, −5 y −14 dB con audio plano, +13, +2 y −7 dB con audio de altavoz), con
   tres, tres y dos comparadores; mantener cada peso a no más de unos 4 dB de
   la unidad importa con señales débiles, donde un peso alejado cuesta
   sensibilidad. Las tablas de ``modem.c`` contienen la compensación
   con la que debe quedar cada comparador — inclinación del prefiltro más peso
   del comparador — y cada peso se calcula a partir de la inclinación que su
   prefiltro alcanzó de verdad, así que los ocho demoduladores avanzan en pasos
   de 3,5 dB de +9 a −15,5 dB (plano) o de +16 a −8,5 dB (altavoz) sea cual sea
   la longitud del prefiltro. ``ModemLogConfig()`` escribe el resultado cuando
   la tarea de recepción vuelve a correr. ``MODEM_RX_EQ_MULTISLICE2`` dispone los
   mismos ocho comparadores sobre dos prefiltros, cuatro cada uno (+5/−9 dB
   plano, +9/−4 dB altavoz), para comparar las dos disposiciones en una
   estación real. Los juegos de filtros dan a cada
   demodulador su propio prefiltro y un comparador sin peso, y el juego clásico
   conserva las tablas fijas de 8 coeficientes.

**Supresión de duplicados y estadísticas.**
   Una trama con FCS válido abre una ventana de 32 periodos de bit × el número
   de demoduladores activos; las copias con el mismo CRC de los demás
   demoduladores dentro de ella se descartan (tramas simples y FX.25 por
   igual). La ventana anota qué demoduladores produjeron la trama, así que al
   cerrarse atribuye una trama que sólo uno obtuvo a ese demodulador — el
   contador ``exclusivos`` que indica lo que aporta cada prefiltro.

**El umbral de recepción guarda lo que retiene.**
   Un bloque llega a los demoduladores mientras su nivel en la banda de tonos
   (``afskGetBandRms()``: dos pasabandas en cascada en torno a 900–2600 Hz
   sobre la señal diezmada y filtrada por el pasa-altos, en mV en el pin) ha
   superado ``rx.gate_mv`` durante más de tres bloques, y hasta que cae por
   debajo de la mitad. El zumbido, el CTCSS y los graves no lo abren ni lo
   mantienen abierto. El diezmador y el pasa-altos se ejecutan en todos los bloques, y los
   tres últimos bloques retenidos se guardan (diezmados, 3 × 192 floats); al
   abrirse el umbral se demodulan primero, así que el preámbulo gastado en
   decidir la apertura no se pierde. ``gate_mv = 0`` alimenta todos los bloques.

**Control de ganancia sobre la señal dentro de banda.**
   El AGC mide el bloque diezmado y no el flujo de 76,8 kHz, así que el ruido de
   discriminador por encima de 5 kHz no fija la ganancia, y la nueva ganancia se
   aplica al mismo bloque sobre el que se midió. Ataque 0,25 y liberación 0,002
   por bloque de 20 ms, con el paso por bloque acotado a ×2 / ÷2 y la ganancia
   entre 0,1 y 8. El nivel se toma sobre toda la banda diezmada, graves
   incluidos, así que una entrada con muchos graves mantiene la ganancia por
   debajo del recorte de la entrada del demodulador. Un bloque cuya entrada,
   medida antes de la ganancia, está por debajo de 0,002 del semirrango del
   ADC (unos 3,3 mV RMS en el pin, el propio ruido del conversor) deja la
   ganancia quieta; la prueba se hace sobre la entrada misma, así que toda
   señal real se lleva al nivel de trabajo sea cual sea la ganancia. La ley
   está en el encabezado ``rx_agc.h``, solo de cabecera, que el replay en PC de
   ``audio_test/rx_replay`` compila sin cambios. Una ganancia fija lo reemplaza
   cuando ``rx.agc_mode`` lo pide.

**Reparación de bits por síndrome del CRC.**
   CRC-16/X.25 es afín sobre GF(2): el registro tras una trama y su FCS es
   ``0xF0B8`` XOR un síndrome que sólo depende del patrón de error. El síndrome
   de un único bit erróneo en la posición *p* es un paso de CRC con entrada cero
   del síndrome en *p* + 1, así que cada bit suelto y cada pareja contigua (el
   patrón de un símbolo erróneo tras NRZI) se comprueban en una sola pasada. Una
   corrección sólo se acepta cuando exactamente un candidato coincide y la trama
   supera una prueba estricta de plausibilidad APRS, y nunca mientras la copia
   intacta de otro demodulador está dentro de la ventana de duplicados.

**El componente del módem se compila con ``-O2``.**
   El DSP de recepción se ejecuta en cada muestra de cada demodulador; el
   ``CMakeLists.txt`` del componente lo compila optimizado para velocidad sea
   cual sea el nivel que use el resto del proyecto.

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
   baudios soportada). Los perfiles AFSK construyen sus pasos de tono y un
   acumulador de fase de símbolo Q32 a partir de la tasa *real* de alarma del
   DAC, así que tonos y velocidad son exactos aunque el periodo de alarma se
   redondee a ticks enteros del temporizador; un flanco de símbolo cae en la
   muestra del DAC más cercana, como mucho un 3 % de símbolo a 1200 Bd y oculto
   por el tono de fase continua. G3RUH mantiene en cambio cada símbolo durante
   un número entero de muestras del DAC, con todos los flancos en la misma
   rejilla: un acumulador fraccionario a cuatro muestras por símbolo movería de
   vez en cuando un flanco un cuarto de símbolo. Era el *receptor* el que
   necesitaba resolución.

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
   (``modem_measure_adc_rate()``), porque el paso del PLL de cada perfil supone
   la tasa *nominal* del ADC y la diferencia es de otro modo un error de estado
   estacionario que el DPLL debe seguir durante toda una transmisión. Las
   estaciones en el aire transmiten a su propia velocidad nominal, así que la
   corrección de recepción es sólo el error del ADC. La tasa real de alarma del
   DAC, conocida exactamente de la configuración del temporizador, también se
   registra, pero sólo se usa cuando un receptor G3RUH oye el propio
   transmisor del nodo (dúplex completo, la autoprueba con bucle de cable).
   Ambos relojes derivan del mismo cristal, así que los ratios son propiedades
   fijas de la placa: medidos **una vez por arranque**, reaplicados en cada
   cambio de perfil.

**El FIR de diezmado filtra en el mismo arreglo.**
   La muestra de salida *i* se escribe en ``buf[i]`` mientras los coeficientes
   leen la ventana que termina en ``buf[i × MODEM_RESAMPLE_RATIO]``, así que con
   cualquier ratio ≥ 2 el puntero de escritura queda por detrás de la ventana de
   lectura salvo en las primeras ``FILTER_TAPS − 1`` posiciones. Esas pocas
   muestras iniciales, junto con la cola del bloque anterior, se preparan en un
   arreglo corto estático antes de que empiece el bucle; el resto del bloque se
   lee crudo del propio ``buf[]``. Ese es todo el motivo por el que la ruta de RX
   no guarda una segunda copia del bloque de 20 ms.

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
ISR del ADC; prioridad del temporizador DAC ∈ 1..3. Dos ``_Static_assert`` en ``afsk.c``
fijan el invariante de trabajo en sitio del diezmador: ``MODEM_RESAMPLE_RATIO``
≥ 2 salvo que el filtro sea de un solo coeficiente, y ``MODEM_BLOCK_SIZE`` ≥ 2 ×
(``FILTER_TAPS`` − 1) para que los tramos de historia inicial y final no puedan
solaparse.

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
   * - ``MODEM_RX_MAX_DEMODULATORS``
     - 8
     - demoduladores (comparadores) de 1200 Bd en paralelo,
       ``MODEM_RX_SLICER_COUNT``..8
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
   * - ``src/afsk.c`` (~1710 lín)
     - ingesta DMA del ADC, umbral de recepción y anillo de retención, FIR de
       diezmado, pasa-altos, AGC, ISR del DAC, PTT
   * - ``src/modem.c`` (~1070 lín)
     - diseño de prefiltros y juegos de demoduladores, correladores, DPLL,
       tablas de tonos, DCD, estimación del desbalance, calibración
   * - ``src/ax25.c`` (~1940 lín)
     - encuadrador HDLC, NRZI, bit-stuffing, supresión de duplicados,
       reparación de bits, códec AX.25, cola de TX
   * - ``src/esp32idf_radioamateur_modem.c`` (~590 lín)
     - la API pública del componente: ``modem_init()``/``modem_set_modem()``,
       los ayudantes TNC2 y la tarea ``modem_svc`` que acciona el TX y entrega
       las tramas decodificadas al callback de RX
   * - ``src/fx25.c``, ``lwfec/rs.c``, ``lwfec/gf.c``
     - FEC Reed–Solomon FX.25
   * - ``src/crc_ccit.c``
     - FCS (secuencia de comprobación de trama)
   * - ``include/impulse_blanker.h``, ``include/rx_agc.h``
     - etapas del frente de recepción solo de cabecera (supresor de impulsos,
       control de ganancia), compartidas con el replay en PC de
       ``audio_test/rx_replay``
