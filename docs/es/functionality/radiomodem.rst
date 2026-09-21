.. _es-radiomodem:

===========
Radiomódem
===========

*Radiomódem* es la página de la administración web que une el firmware con la
radio. Allí se elige la modulación en el aire, se temporiza el acceso al canal,
se acota el tiempo de transmisión propio de la estación y se describe la
relación eléctrica entre los pines ADC/DAC del ESP32 y el transceptor. Todas
las demás páginas deciden *qué* se transmite; esta decide *cómo*, *cuándo* y
*con cuánto nivel*.

Este capítulo documenta cada control de la página, uno por uno: qué es, qué
cambia realmente dentro del firmware, cómo elegir un valor, ejemplos de uso y
los errores que cada ajuste puede provocar. Para el DSP y el componente del
módem que hay detrás, véase :ref:`es-modem` y :ref:`es-dsp-signal-chain`.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Propiedad
     - Valor
   * - Entrada de menú
     - **Radiomódem**
   * - Rutas
     - ``GET /radio``, ``POST /radio``, más ``POST /radio/looptest``,
       ``POST /radio/level`` y ``POST /radio/txtest`` para los tres botones de
       diagnóstico.
   * - Manejador
     - ``components/webconfig/pages/page_radio.c``
   * - Almacenado en
     - ``/storage/radio.json`` (descargable y subible desde la página
       *Almacenamiento*)
   * - Se aplica
     - En vivo al *Guardar*, salvo **Activar módem ADC/DAC de audio** y
       **Frecuencia de muestreo de transmisión**, que necesitan reinicio.

Por qué algunos campos necesitan reinicio
==========================================

``page_radio_post()`` escribe todo el formulario en ``g_config`` bajo un solo
bloqueo, guarda ``radio.json`` y luego llama a
``aprs_service_apply_modem_config()``, que empuja los ajustes nuevos al módem
en marcha a través de ``modem_set_modem()``. Todo lo que el módem admite
mientras funciona surte efecto en el instante en que *Guardar* devuelve: sin
reinicio, sin tramas perdidas y sin reiniciar el servicio.

Dos ajustes quedan fuera de ese camino:

* **Activar módem ADC/DAC de audio** — ``modem_init()`` se ejecuta exactamente
  una vez, desde ``main.c``, y sólo si este interruptor ya estaba activo en el
  arranque. Activarlo guarda el ajuste pero no levanta ningún hardware hasta el
  siguiente reinicio.
* **Frecuencia de muestreo de transmisión** — el periodo del reloj de muestreo
  y cada incremento de fase derivado de él se programan con el hardware del
  módem detenido, así que ``afskSetDacSampleRate()`` se niega a actuar sobre un
  módem vivo y el valor lo aplica el siguiente ``modem_init()``.

En ambos casos el formulario sigue mostrando el valor guardado, que es el que
usará el próximo arranque, no aquel con el que el módem está funcionando en este
momento.

.. warning::

   Tres botones de esta página — **PRUEBA DE BUCLE**, **NIVEL RX** y **PRUEBA
   TX** — guardan el formulario antes de ejecutarse, exactamente como si se
   hubiera pulsado *Guardar*. No los use para "probar" un ajuste que no piensa
   conservar. Dos de los tres, además, activan el transmisor.

Protocolo
=========

FX.25 (AX.25 con corrección de errores)
---------------------------------------

**Qué es.** FX.25 envuelve una trama AX.25 corriente en un bloque
Reed–Solomon con una etiqueta de correlación por delante. Un receptor que
entiende FX.25 puede reparar errores de bit que de otro modo harían fallar el
CRC de la trama; un receptor que no lo entiende sigue encontrando dentro la
trama AX.25 sencilla y la decodifica con normalidad. Es una extensión
compatible, no un protocolo distinto.

**Qué hace aquí el desplegable.** Un selector de tres opciones guarda
``fx25Mode = 0``, ``1`` o ``2``, que ``Ax25Init()`` interpreta directamente:

* *Apagado* (``0``, el valor por defecto) — AX.25 sencillo en ambos sentidos
  (``Ax25Config.fx25 = 0``, ``Ax25Config.fx25Tx = 0``).
* *Solo recepción* (``1``) — *FX.25 en recepción, AX.25 sencillo en
  transmisión* (``Ax25Config.fx25 = 1``, ``Ax25Config.fx25Tx = 0``). Esto le
  compra a la estación una mejor decodificación de las estaciones FX.25 que
  oye, sin añadir redundancia a lo que transmite.
* *Recepción y transmisión* (``2``) — FX.25 en ambos sentidos
  (``Ax25Config.fx25 = 1``, ``Ax25Config.fx25Tx = 1``). Esta estación también
  envuelve sus propias tramas salientes en el bloque Reed–Solomon, de modo
  que cualquier vecino que entienda FX.25 recibe de ella la misma corrección
  de errores.

**Segundo efecto, fácil de pasar por alto.** Con la entrada de audio plana
seleccionada (más abajo), FX.25 también cambia qué prefiltro ejecuta el primer
demodulador de 1200 Bd: sin FX.25 aplica deénfasis para deshacer el preénfasis
de la estación transmisora; con FX.25 ejecuta en su lugar el paso banda inverso
sin más, por el razonamiento de que la redundancia del código ya cubre la
pequeña pérdida de SNR. Conmutar FX.25 altera, por tanto, el comportamiento en
recepción incluso en un canal donde nadie transmite FX.25.

**Cuándo activarlo.**

* *Casi siempre, en un canal APRS normal.* El coste es tiempo de CPU en el
  receptor y nada en el aire.
* *Sin duda*, en un trayecto débil o ruidoso donde se oyen paquetes parciales,
  si algún vecino transmite FX.25.
* *Déjelo apagado* mientras persigue un problema de recepción y quiere la
  cadena de señal más simple posible, o cuando esté comparando el
  comportamiento del deénfasis con audio plano.

.. warning::

   Activar FX.25 mientras se ajusta un trayecto de recepción marginal hace que
   los dos demoduladores se comporten de forma distinta a como lo hacían hace un
   momento. Ajuste primero el audio con FX.25 apagado y después actívelo y
   confirme que la tasa de decodificación mejoró, y no al revés.

Audio / AFSK
============

Activar módem ADC/DAC de audio
-------------------------------

**Qué es.** El interruptor maestro del módem por software integrado. Con él
activo, el propio ESP32 es el TNC: el SAR-ADC escucha el audio de recepción de
la radio, el DAC genera el audio de transmisión y un GPIO acciona el PTT. Con
él apagado, ``modem_init()`` no se llama nunca, no corre ninguna tarea de
audio, no se configura ADC ni DAC, y ``aprs_service_can_transmit()`` devuelve
falso: toda transmisión de RF que el firmware intente se descarta en el origen
y se contabiliza bajo ``DROP_MODEM_NOT_READY``.

**Usos.**

* *Activado* — el caso normal: una estación completa e independiente (IGate,
  digipeater, tracker, meteorología, telemetría) con una radio conectada.
* *Apagado* — una estación **sólo de internet**. Un IGate que sólo reenvía
  APRS-IS a Telegram, un cliente Winlink de sólo recepción por internet o una
  unidad de banco de pruebas sin radio alguna. Apagar el módem libera las tareas
  de audio y su cuota de CPU, y hace imposible activar nada por accidente.

.. warning::

   Este es el único campo de la página cuyo cambio **no hace nada** hasta que se
   reinicia el dispositivo. Márquelo, *Guardar* y reinicie desde la página
   *Acerca de / Firmware*. Los botones PRUEBA DE BUCLE y PRUEBA TX detectan este
   estado y lo dicen explícitamente en lugar de fallar de forma oscura.

Modulación
----------

**Qué es.** La modulación en el aire y la velocidad en baudios usadas tanto en
recepción como en transmisión.

.. list-table::
   :header-rows: 1
   :widths: 8 26 10 22 34

   * - Valor
     - Perfil
     - Baudios
     - Tonos
     - Dónde se usa
   * - 0
     - AFSK300
     - 300
     - 1600 / 1800 Hz
     - Packet en HF (30 m, 20 m). Suficientemente estrecho para un canal de SSB.
   * - 1
     - **Bell 202** (predeterminado)
     - 1200
     - 1200 / 2200 Hz
     - **APRS estándar en todo el mundo** — 144.390 MHz en Norteamérica,
       144.800 MHz en la mayor parte de Europa, 145.175 MHz en Australia, etc.
   * - 2
     - ITU V.23
     - 1200
     - 1300 / 2100 Hz
     - Enlaces packet europeos heredados. No interopera con Bell 202.
   * - 3
     - G3RUH FSK
     - 9600
     - FSK directo
     - Enlaces de alta velocidad, trabajo por satélite. Exige un camino de audio
       plano en ambos sentidos.

**Qué cambia internamente.** ``ModemInit()`` reconstruye toda la cadena de
demodulación: coeficientes de los filtros, paso del PLL, umbrales de DCD y el
número de demoduladores. Ambos perfiles de 1200 Bd ejecutan **dos
demoduladores en paralelo** con prefiltros distintos, de modo que una trama que
un camino pierde puede recuperarla el otro; los de 300 Bd y 9600 Bd ejecutan un
solo demodulador.

**Ejemplos.**

* Un IGate doméstico en la frecuencia APRS nacional → **1200 Bd Bell 202**. No
  lo cambie. Cualquier otra cosa deja la estación sorda y muda en ese canal.
* Un enlace packet a 9600 Bd con un emplazamiento vecino en una frecuencia
  símplex dedicada → **9600 Bd G3RUH**, con *Entrada de audio plana / de
  discriminador* marcada y conexión por puerto de datos en ambos extremos.
* Una pasarela de HF en 10.147,6 MHz USB → **300 Bd AFSK**.

.. warning::

   La modulación debe coincidir con la de todas las estaciones con las que
   pretenda trabajar. No hay detección automática ni respaldo: un desajuste
   significa que no se decodifica nada en ninguna dirección, y sus
   transmisiones las oirán como ruido todos los demás en el canal. Si una
   estación que funcionaba deja de decodificar de repente, este es el primer
   campo que hay que revisar.

.. warning::

   9600 Bd G3RUH no pasa por los conectores de micrófono y altavoz de una radio.
   El preénfasis, el deénfasis y el ancho de banda de audio de un camino de voz
   lo destruyen. Requiere un auténtico puerto de datos plano (una conexión de
   "packet 9600" o de discriminador) tanto en transmisión como en recepción.

Hardware de audio (en tiempo de compilación)
--------------------------------------------

Debajo del selector de modulación se muestra un único bloque de sólo lectura. No
es un ajuste: es la definición de placa con la que se compiló el firmware, de
modo que una elección de cableado pueda distinguirse de un valor guardado de un
vistazo. Informa del pin de salida del DAC (``MODEM_DAC_GPIO``, por omisión
GPIO25), el pin de entrada del ADC (``MODEM_ADC_GPIO``, por omisión GPIO33), el
pin de PTT (``MODEM_PTT_GPIO``, ``Desactivado`` cuando es -1), PTT activo en
alto (``MODEM_PTT_ACTIVE_HIGH``), la atenuación del ADC (``MODEM_ADC_ATTEN``) y
la frecuencia de muestreo del ADC (``MODEM_ADC_SAMPLERATE``) con la que se
compiló el firmware. La frecuencia de muestreo de transmisión no aparece: es un
ajuste guardado, que se elige en *Frecuencia de muestreo de transmisión* más
abajo en esta misma página.

Todos los valores de compilación proceden del ``CMakeLists.txt`` de nivel
superior y sólo pueden cambiarse recompilando el firmware — por ejemplo
``idf.py build -DMODEM_ADC_GPIO=32``. El pin de PTT está registrado en la tabla
de propiedad de GPIO de la administración, de modo que aparece como *usado —
PTT* en cualquier otro selector de GPIO de la web (la salida de alarma de
mensajes, por ejemplo), que es lo que impide que dos funciones reclamen el
mismo pin.

.. note::

   El pin del DAC sólo puede ser GPIO25 (DAC1) o GPIO26 (DAC2); el DAC del
   ESP32 no es enrutable a ningún otro pin, y la compilación falla con un error
   explícito si se indica otro número.

Entrada de audio plana / de discriminador
------------------------------------------

**Qué es.** Una declaración sobre de dónde procede el audio de recepción, no un
filtro que se activa por gusto. Le dice al demodulador si el audio que recibe ya
ha sido deenfatizado.

* **Apagado** (por omisión) — el audio viene de un **conector de altavoz o de
  auriculares**. La salida de audio de un receptor de voz ya está deenfatizada y
  limitada en banda. El primer demodulador aplica por tanto preénfasis y un
  paso banda normal.
* **Activado** — el audio viene de un **puerto de datos o directamente del
  discriminador**. Esa señal es plana y sin filtrar, y sigue llevando el
  preénfasis de la estación transmisora. El primer demodulador ejecuta un paso
  banda inverso y (salvo que FX.25 esté activo) deénfasis para deshacerlo.

En ambos casos el segundo demodulador de 1200 Bd permanece en un camino
distinto, así que la pareja cubre siempre dos ecualizaciones diferentes.

**Ejemplos.**

* Baofeng UV-5R, audio tomado del conector de altavoz de 3,5 mm → **apagado**.
* Un transceptor móvil con conector mini-DIN de datos de 6 pines, usando el pin
  4 (packet 1200 Bd) → normalmente **apagado**; usando el pin de discriminador
  de 9600 Bd → **activado**.
* Cualquier enlace G3RUH de 9600 Bd → **activado**, siempre.
* Un SDR que entrega audio de FM demodulado sin deénfasis en la cadena →
  **activado**.

.. warning::

   Poner esto al revés no impide la decodificación del todo: reduce en silencio
   a la mitad la sensibilidad de la estación. El síntoma es una estación que
   decodifica perfectamente los paquetes locales fuertes y pierde todo lo débil.
   Si su tasa de decodificación parece pobre y los niveles son correctos,
   conmute esto y compare.

.. tip::

   Este es un ajuste que tiene sentido probar empíricamente. Ejecute **NIVEL
   RX** con cada opción mientras hay tráfico real en el canal y observe cuál
   informa ``DCD sí`` más a menudo y produce más decodificaciones en la tabla de
   tráfico del panel.

Preámbulo (ms)
--------------

**Qué es.** El *TXDelay* de AX.25: cuánto tiempo permanece el transmisor
activado, enviando sólo bytes de bandera HDLC, antes del primer bit de datos de
la trama. Da tiempo al squelch de las estaciones receptoras a abrirse, a su AGC
a estabilizarse y a los PLL de sus demoduladores a engancharse.

**Rango 50–2000 ms, por omisión 300 ms.**

``Ax25TxDelay()`` convierte los milisegundos en un número de bytes de bandera
según la velocidad en baudios actual. A 1200 Bd, 8 bits tardan unos 6,7 ms, de
modo que 300 ms son unos 45 bytes de bandera y cada 1000 ms adicionales añaden
unos 150 bytes más de portadora muerta **delante de cada una de las tramas que
transmite**.

**Cómo elegir.**

* 300 ms es correcto para la mayoría de los transceptores modernos en símplex.
* Súbalo a 400–600 ms si su transmisor tarda en alcanzar plena potencia, si pasa
  por un repetidor o si estaciones lejanas informan de que oyen su portadora
  pero sólo decodifican algunas de sus tramas.
* Súbalo más, hacia 800–1000 ms, sólo para un camino de PTT realmente lento: un
  amplificador conmutado por relé, un transvertidor, un equipo antiguo con
  conmutación T/R lenta.
* Bájelo hacia 150–200 ms sólo con una radio moderna, rápida y conectada
  directamente, y sólo si ha confirmado con un vecino que sus tramas siguen
  decodificándose.

.. warning::

   Un preámbulo largo es un impuesto puro sobre un canal compartido. A 1200 Bd,
   un informe de posición APRS suele estar en el aire alrededor de medio
   segundo; un preámbulo de 2000 ms significa que cinco sextas partes de cada
   activación son portadora muerta. En una frecuencia concurrida, esta es una de
   las formas más rápidas de convertirse en la estación de la que todos se
   quejan.

.. warning::

   Un preámbulo demasiado *corto* es la causa clásica de "algunas estaciones
   nunca me oyen". La trama empieza antes de que su squelch se haya abierto, de
   modo que sus primeros bytes — incluido el campo de dirección — se pierden, y
   la trama falla sin contabilizarse como error en ningún sitio visible. Si le
   digipitean de forma inconsistente, pruebe a subir esto antes que nada.

Intervalo de tiempo TX (ms)
----------------------------

**Qué es.** Pese al nombre, este campo fija el **tiempo de silencio** del
módem: ``Ax25TimeSlot()`` lo escribe en ``Ax25Config.quietTime``, el periodo de
asentamiento obligatorio que el planificador de transmisión observa después de
una transmisión (y al arrancar el módem) antes de considerar volver a activar el
PTT. Cuando se fija un valor distinto de cero, se añade una fluctuación
aleatoria de 100–1000 ms al primer plazo, para que dos estaciones configuradas
igual que arranquen a la vez no queden sincronizadas.

**Rango 0–10000 ms, por omisión 2000 ms.** Ponerlo a 0 elimina por completo el
plazo: el módem transmitirá en cuanto oiga el canal libre y la tirada de
persistencia tenga éxito.

.. note::

   El intervalo entre tiradas de persistencia — el clásico *SlotTime* de AX.25 —
   está fijado en 100 ms dentro del módem y no se expone en esta página. Este
   campo es el tiempo de silencio que se suma a aquél.

**Ejemplos.**

* Canal APRS metropolitano concurrido, estación que además digipitea →
  mantenga 2000 ms o súbalo a 3000 ms. La estación seguirá respondiendo
  mensajes con prontitud, porque el tiempo de silencio sólo retrasa el *inicio*
  de un nuevo ciclo de activación.
* Un canal rural tranquilo con un puñado de estaciones → 1000 ms resulta
  cómodo.
* Un enlace punto a punto dedicado en una frecuencia privada con sólo dos
  estaciones → 0 es razonable; no hay contienda que repartir.

.. warning::

   0 en un canal compartido elimina por completo el periodo de asentamiento y
   convierte a esta estación en la más agresiva de la frecuencia. Todas las
   demás se retiran después de transmitir; la suya no.

.. warning::

   Los valores grandes retrasan *todo*, incluidos los acuses de recibo de
   mensajes y las repeticiones del digipeater, que el limitador de ciclo de
   trabajo de más abajo exime deliberadamente. Por encima de unos 5000 ms la
   estación empieza a parecer poco receptiva a quien intente enviarle mensajes.

Buffers de TX
-------------

**Qué es.** Cuántas tramas pueden estar en el anillo de transmisión de RF — en
cola esperando canal libre, o en el aire en ese momento — antes de que una
trama recién ofrecida se descarte en lugar de encolarse.

**Rango 1–11, por omisión 1.** El techo es la profundidad útil real del anillo
de transmisión (``AX25_TX_FRAME_RING_MAX``), de modo que el desplegable nunca
puede ofrecer un valor que el anillo no pudiera contener.

**De qué protege.** Sin tope, una ráfaga — un IGate retransmitiendo APRS-IS a RF
más rápido de lo que el canal se libera, o una pasada del planificador en la que
varios informes periódicos vencen a la vez — encolaría tramas mucho más rápido
de lo que un canal de 1200 Bd puede drenar. Esas tramas saldrían al aire minutos
después, mucho más tarde de dejar de ser ciertas, o se descartarían igualmente
al llenarse el anillo. Poner tope aquí descarta el exceso de inmediato y registra
el motivo.

**Ejemplos.**

* El valor **1** por omisión — la respuesta correcta para un tracker o una
  estación que sólo baliza. Los informes de posición sólo interesan cuando son
  actuales; uno rancio encolado detrás de otros tres es peor que ninguno.
* **2–3** — un digipeater concurrido o un IGate con pasarela INET→RF activa, en
  un canal que se libera a ráfagas. Absorbe una acumulación breve sin dejar que
  la cola envejezca.
* **8–11** — raramente apropiado. Sólo en un enlace dedicado donde el canal está
  esencialmente siempre libre y cada trama debe entregarse de verdad.

.. warning::

   Subir esto no hace el canal más rápido. Una cola que está llena porque el
   canal está ocupado seguirá llena; las tramas simplemente serán más viejas
   cuando por fin se transmitan. Si se descartan tramas por acumulación, reduzca
   lo que la estación transmite (intervalos de baliza más largos, filtros de
   IGate más estrictos) en vez de profundizar la cola.

.. note::

   Esto afecta sólo al anillo de transmisión de RF. El socket de APRS-IS tiene
   su propio buffer independiente, así que una rama de RF congestionada nunca
   bloquea ni descarta la rama de internet del mismo paquete.

Limitador de ciclo de trabajo y Límite de ciclo de trabajo (%)
---------------------------------------------------------------

**Qué es.** Un techo sobre el *tiempo de transmisión acumulado propio* de esta
estación, medido en una **ventana deslizante de 10 minutos** (dividida
internamente en cuarenta cubos de 15 segundos). Es totalmente independiente de
CSMA: CSMA le impide transmitir *encima* de alguien y no recuerda lo que
transmitió hace un minuto; esto tiene memoria y ninguna opinión sobre el canal.

**Apagado por omisión. Rango del techo 1–100 %, por omisión 25 %.**

**Qué ocurre al alcanzar el techo.** El tráfico se divide en dos:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Clase
     - Comportamiento una vez alcanzado el techo
   * - **Crítico** — mensajes APRS y sus acuses de recibo, repeticiones del
       digipeater
     - Se transmiten igualmente. Su tiempo en el aire sigue contando para la
       ventana; sólo se omite la compuerta para ellos.
   * - **No crítico** — balizas de posición propias, objetos e ítems, informes
       meteorológicos, telemetría, boletines, retransmisión masiva INET→RF del
       IGate
     - Se retienen. Cada uno de ellos es una tarea periódica que volverá a
       ofrecer el mismo informe en su siguiente intervalo, así que es un
       aplazamiento y no una pérdida, aunque se contabilice bajo
       ``DROP_TX_DUTY_CYCLE`` en el panel para que sea visible.

**Usos.**

* **Estaciones desatendidas.** En varios planes de banda, un techo de ciclo de
  trabajo en una estación automática no es una cortesía sino una condición de
  licencia. Aquí es donde se impone.
* **Proteger el amplificador de potencia.** Un portátil o un pequeño
  amplificador diseñado para uso intermitente de voz se sobrecalentará si un
  planificador mal configurado lo activa repetidamente. 20–25 % es una cifra
  segura habitual para ese hardware.
* **Emplazamientos solares o a batería.** La corriente de transmisión domina el
  presupuesto de energía; un techo convierte "cuánta energía consumirá esta
  estación" en un número calculable.
* **Una red de seguridad frente a sus propios errores.** Un intervalo de baliza
  puesto por accidente en 10 segundos, una tormenta de definiciones de
  telemetría o un filtro INET→RF desbocado no pueden monopolizar la frecuencia
  con el limitador activo.

**Ejemplos.**

* Digipeater en cima de montaña con red eléctrica y amplificador adecuado →
  activado al **50 %**: generoso, pero sigue siendo un tope duro contra un
  descontrol.
* IGate doméstico sobre un portátil → activado al **20 %**. Los finales del
  portátil se lo agradecerán.
* Digipeater de relleno alimentado por energía solar → activado al **10 %**, con
  intervalos de baliza lo bastante largos como para que el limitador sea un
  respaldo y no algo cotidiano.
* Una estación en pruebas sobre carga artificial → apagado.

.. warning::

   Como el tráfico de mensajes y las repeticiones del digipeater están exentos,
   el limitador no es una garantía de tiempo máximo en el aire. Un digipeater en
   mitad de una red de emergencia superará su techo configurado, por diseño. No
   trate el número como una garantía reglamentaria si las condiciones de su
   licencia son absolutas.

.. warning::

   Un techo muy bajo combinado con balizas frecuentes significa que las balizas
   se aplazan en silencio, a veces durante mucho tiempo. Si aprs.fi muestra su
   estación actualizándose mucho menos a menudo que su intervalo configurado,
   revise el contador de descartes del panel antes de sospechar de la radio.

Tiempo mínimo de PTT liberado (ms)
-----------------------------------

**Qué es.** Un hueco **sin PTT** garantizado adicional entre una transmisión y
la siguiente, por encima del retardo fijo de liberación de un tick de servicio
(unos 10 ms) que el módem aplica siempre.

**Rango 0–5000 ms, por omisión 0 (sin retención adicional).**

**Por qué existe.** El suelo propio del módem garantiza que el PTT se libera de
forma visible entre tramas, pero cierto equipamiento necesita bastante más:

* Un repetidor cuyo tono de cortesía y cola de squelch deben terminar antes de
  que el siguiente paquete active el PTT, o el comienzo de su trama caerá encima.
* Un transceptor con conmutación T/R por relé que necesita tiempo de
  asentamiento, o recortará el comienzo de la siguiente transmisión.
* Un amplificador lineal con secuenciador, donde volver a activar demasiado
  rápido conmuta el relé en caliente.
* Una radio cuya propia lógica interna de temporización o "antirrebote de PTT"
  ignora una activación que llega demasiado pronto tras una liberación.

**Ejemplos.**

* Conexión directa a un portátil o móvil moderno → **0**.
* Trabajando a través de un repetidor con pitido de cortesía → **500–1000 ms**,
  lo bastante para que se despeje la cola.
* Amplificador conmutado por relé en la cadena → **200–300 ms**.

.. warning::

   Este retardo se aplica entre *todas* las transmisiones consecutivas,
   incluidos los reintentos de un mensaje APRS sin acuse. Poner segundos aquí
   hace que un intercambio de varias tramas resulte notablemente lento.

Persistencia CSMA (p, 1–255)
-----------------------------

**Qué es.** El parámetro *Persist* estándar de AX.25/KISS. Una vez que se oye
el canal libre, el módem transmite en esa ranura con probabilidad ``p/256``; si
falla, espera una ranura más y vuelve a tirar.

**Rango 1–255, por omisión 63** (≈ 24,6 % por ranura libre, el valor
convencional de AX.25).

**Por qué la aleatoriedad.** Si todas las estaciones activasen el PTT en el
instante en que el canal queda en silencio, todas las que estuvieran esperando
colisionarían exactamente en el mismo momento — y cuantas más esperasen, peor la
colisión. Tirar los dados separa esas activaciones en el tiempo sin ninguna
coordinación entre estaciones.

.. list-table::
   :header-rows: 1
   :widths: 16 84

   * - Valor
     - Comportamiento
   * - 255
     - Transmite en la primera ranura libre, siempre. Equivale al CSMA no
       persistente sencillo. Apropiado sólo donde uno es el único transmisor.
   * - 128
     - ~50 % por ranura. Asertivo; sensato en un canal poco usado donde importa
       la latencia.
   * - 63
     - ~25 % por ranura. El estándar. Correcto para prácticamente cualquier
       canal APRS compartido.
   * - 20–32
     - ~8–12 %. Para un canal urbano muy congestionado, o para una estación que
       debe ceder ante las demás (una baliza de telemetría de baja prioridad, un
       digi de relleno en una zona saturada).
   * - 1
     - ~0,4 %. La estación esperará muchísimo por cada transmisión.

**Antiinanición.** El planificador no deja que una trama espere para siempre:
tras ocho ranuras consecutivas perdidas por canal ocupado o por tirada fallida,
fuerza la transmisión de todos modos. Así, incluso una persistencia muy baja
tiene un retardo de peor caso acotado en vez de ilimitado.

.. warning::

   El 0 se rechaza y se lleva a 1 en lugar de aceptarse, porque una persistencia
   de 0 significaría que la tirada nunca puede acertar y la estación no volvería
   a transmitir jamás: un modo de fallo que se parece exactamente a un hardware
   averiado.

.. warning::

   Subir la persistencia no hace que sus paquetes atraviesen un canal ocupado;
   hace que *colisionen* en un canal ocupado, lo que cuesta tiempo de aire a
   todos y no entrega nada. Si sus paquetes no llegan a un digipeater, la
   respuesta es más preámbulo, mejores niveles de audio o mejor antena, no una
   ``p`` más alta.

Interfaz de audio
=================

Este conjunto describe lo que hay eléctricamente entre los pines del ESP32 y el
transceptor. Los valores por omisión corresponden a una placa de interfaz que
lleva su propia red de polarización, atenuadores y filtro de reconstrucción — el
diseño que muestran los esquemáticos del proyecto. Los ajustes de aquí son lo
que necesita, en cambio, una interfaz reducida a un condensador de acoplo y un
ajuste de nivel por sentido.

Polarización interna de la entrada del ADC
-------------------------------------------

**Qué es.** El ADC del ESP32 mide una tensión entre 0 V y unos 3,1 V. El audio
es una señal que oscila en positivo y en negativo alrededor de cero, así que
debe elevarse hasta el centro de ese rango antes de poder muestrearse; de lo
contrario, la mitad negativa se pierde sin más. Esa elevación se llama
polarización.

* **Apagado** (por omisión) — la placa de interfaz proporciona la polarización,
  normalmente con dos resistencias formando un divisor en el pin del ADC. Es lo
  que muestran los esquemáticos del proyecto.
* **Activado** — el firmware habilita las resistencias internas de pull-up *y*
  pull-down del pad del ADC, que juntas llevan el pin a aproximadamente la mitad
  de la alimentación. Es lo que necesita una entrada acoplada únicamente a
  través de un condensador.

**Cuándo activarlo.** Ha construido una interfaz mínima: un condensador desde la
salida de altavoz de la radio (a través de un ajuste de nivel) directamente al
pin del ADC, sin resistencias de polarización propias. Ejecute **NIVEL RX**: si
el ``DC`` informado está cerca de 0 mV o cerca del raíl de alimentación en vez
de alrededor de 1500–1600 mV, la entrada no está polarizada y este es el
interruptor que lo arregla.

.. warning::

   **Limitación de hardware.** Las resistencias internas sólo existen en GPIO32
   y GPIO33. Si el firmware se compiló con el ADC en GPIO34–GPIO39 — que son
   pads sólo de entrada sin resistencia de pull alguna — activar esto registra
   un error y no cambia nada. Una entrada así sólo puede polarizarse
   externamente.

.. warning::

   **No** lo active cuando la placa de interfaz ya fija la polarización. Las
   resistencias internas son débiles pero no despreciables, y cargan el divisor
   externo, desplazando el punto de trabajo y reduciendo la excursión útil. Si
   usa la placa de interfaz del proyecto, deje esto apagado.

Avisar cuando el audio recibido se sale de rango
-------------------------------------------------

**Qué es.** Un diagnóstico. Con él activo, el firmware registra un aviso siempre
que los resultados de conversión en bruto alcanzan alguno de los extremos del
rango del conversor (por debajo del código 15 o por encima del 4080, sobre
0–4095), como mucho una vez cada cinco segundos para que un problema sostenido
no inunde la consola.

**Apagado por omisión**, porque en una interfaz bien construida no debería
dispararse nunca y la comprobación no cuesta nada cuando está deshabilitada.

**Cuándo activarlo.**

* Mientras ajusta por primera vez el nivel de recepción — combínelo con la
  página *Registros* para ver los avisos según ocurren.
* De forma permanente, en una interfaz **sin diodos de recorte en la entrada**.
  Allí, una lectura fuera de rango no significa sólo "demasiado fuerte":
  significa que el pin se está excitando más allá de los raíles de alimentación,
  lo que es un camino para dañar el ESP32.
* Cuando una estación decodifica bien a volumen moderado y deja de decodificar
  al subir el volumen de la radio — la firma clásica del recorte.

.. warning::

   El audio recortado no suena roto al oído humano, pero una forma de onda AFSK
   recortada pierde la relación de amplitud de la que depende el demodulador.
   Una estación con este problema suele decodificar *peor* a las estaciones
   fuertes que a las débiles, algo tan contraintuitivo que hay quien cambia de
   antena por ello.

Amplitud de salida de transmisión (%)
--------------------------------------

**Qué es.** La amplitud pico a pico del audio de transmisión, como porcentaje
del rango completo de 0–3,3 V del DAC. Internamente, cada muestra se escala
alrededor del código medio del DAC antes de escribirse, de modo que el tono
sigue centrado sea cual sea el porcentaje.

**Rango 20–100 %, por omisión 60 %.** Se aplica en vivo, y en la siguiente
muestra: no hay que detener el modulador.

**Por qué el suelo es el 20 %.** El DAC del ESP32 es de 8 bits. El porcentaje
decide con cuántos de sus 256 códigos se dibuja realmente un periodo de la
senoide: al 20 % un periodo completo abarca unos 50 códigos, y por debajo de eso
la cuantización convierte el tono en una escalera visible cuyos armónicos caen
dentro de la banda de audio y degradan la desviación que ve el receptor. Los
30–40 dB de atenuación que necesita una entrada de micrófono corresponden a un
**atenuador resistivo externo**, no a este campo.

**Cómo ajustarlo.** Use el botón **PRUEBA TX** con un medidor de desviación,
otro receptor o un SDR sobre la señal transmitida, y ajuste para **2,5–3,5 kHz**
de desviación en un canal de FM de 5 kHz de desviación. Si no consigue bajar lo
suficiente al 20 %, añada o aumente el atenuador externo. Si no consigue subir
lo suficiente al 100 %, el atenuador es demasiado agresivo.

.. warning::

   La sobredesviación es el fallo de transmisión más común en APRS. Salpica al
   canal adyacente y —porque el discriminador del receptor recorta— el paquete a
   menudo decodifica *peor* en el extremo lejano de lo que lo habría hecho con
   la desviación correcta. Más fuerte no es mejor.

.. warning::

   La subdesviación es un fallo igual de real: la trama queda enterrada en el
   suelo de ruido del receptor y sólo la decodifican estaciones muy cercanas. Si
   le oye el digipeater de enfrente y nadie más, mida su desviación antes de
   culpar a la propagación.

Frecuencia de muestreo de transmisión
--------------------------------------

**Qué es.** La frecuencia a la que el DAC genera la forma de onda de
transmisión: **38400 Hz** (por omisión) o **76800 Hz**.

Ambas son múltiplos exactos de 1200 y 9600, lo cual es un requisito estricto: el
modulador deriva su temporización de símbolo por división entera, así que una
frecuencia que no sea múltiplo de ambas desincroniza una velocidad u otra. Un
valor fuera de estas dos recae en la frecuencia estándar en lugar de guardarse y
ser rechazado más tarde.

**Qué aporta la frecuencia más alta.** Cualquier salida muestreada arrastra
imágenes de reconstrucción: copias de la señal deseada reflejadas alrededor de
los múltiplos de la frecuencia de muestreo. Duplicar la frecuencia aleja esas
imágenes una octava más, donde un filtro mucho más suave — o el propio ancho de
banda de audio del transmisor — puede eliminarlas.

**Cuándo elegir 76800 Hz.**

* La placa de interfaz **no lleva filtro de reconstrucción**: el pin del DAC va
  por un condensador y un ajuste directamente a la radio.
* Observa audio espurio inexplicable, un tono transmitido de sonido áspero o un
  receptor descontento en un montaje por lo demás correcto.

**Cuándo quedarse en 38400 Hz.**

* La interfaz tiene un filtro paso bajo adecuado después del DAC, que es lo que
  muestran los esquemáticos del proyecto.
* El margen de CPU es escaso: la frecuencia más alta duplica la carga de
  interrupciones del DAC, y en una estación que además ejecuta el IGate, la
  administración web, Telegram y el sondeo de sensores esa carga no es gratis.

.. warning::

   Este ajuste surte efecto **sólo tras un reinicio**. El formulario muestra el
   valor guardado, que es el que usará el próximo arranque; hasta entonces el
   módem sigue transmitiendo a la frecuencia con la que se inició.

Tiempo máximo de transmisión (ms)
----------------------------------

**Qué es.** Un perro guardián de seguridad sobre una única activación. Si el
transmisor lleva activado de forma continua más tiempo que este, el firmware
libera el PTT, aborta la trama en curso, vacía el FIFO del modulador y registra
un error.

**Rango 0–60000 ms. 0 deshabilita el límite — y 0 es el valor por omisión.**

**Por qué existe.** No es un planificador ni un control de ciclo de trabajo.
Existe para el caso en que el camino de transmisión se ha *atascado*: una tarea
que nunca termina, una máquina de estados del módem encallada a mitad de trama,
un fallo que de otro modo dejaría una portadora en el aire hasta que alguien se
dé cuenta. En una estación desatendida en la cima de una montaña, esa es la
diferencia entre una mala hora y una frecuencia bloqueada más un coordinador
enfadado.

**Cómo elegir un valor.** Calcule la activación más larga que esta estación
puede producir legítimamente y deje un margen generoso por encima. Ese peor caso
es el preámbulo máximo más una trama de longitud máxima a la velocidad más lenta
en uso: unos segundos a 1200 Bd, más con la redundancia de FX.25. Ajustes
prácticos:

* **5000–10000 ms** para una estación de 1200 Bd con preámbulo de 300 ms. Muy
  por encima de cualquier transmisión real y muy por debajo de lo que alguien
  llamaría una portadora atascada.
* **15000–20000 ms** para una estación de HF a 300 Bd, donde las tramas son
  realmente largas.
* **0** en un montaje de banco de pruebas donde prefiera ver ocurrir un fallo
  antes que verlo limpiado a sus espaldas.

.. warning::

   Ajustarlo cerca de la duración de una transmisión real corta tramas reales.
   La estación parecerá transmitir basura que nadie decodifica, y el registro
   mostrará el límite disparándose repetidamente. Si ve ese mensaje, el ajuste
   es demasiado bajo, no la radio está rota.

.. tip::

   Activar esto es muy recomendable en cualquier estación que se deje
   desatendida, especialmente con amplificador. No cuesta nada cuando nada va
   mal.

Los tres botones de diagnóstico
================================

Los tres están junto a la casilla *Activar módem ADC/DAC de audio*. Los tres
**guardan primero el formulario**, así que lo que se prueba es siempre lo que
hay en pantalla. Los tres son rutas ``POST``, deliberadamente: dos de ellos
activan el transmisor, y la comprobación de mismo origen que protege esta
administración se aplica sólo a ``POST`` — una ruta ``GET`` aquí podría
dispararse desde cualquier otra página que un operador autenticado tuviera
abierta, sin más que apuntarle una etiqueta de imagen.

Sólo uno de los tres puede ejecutarse a la vez; una segunda petición se rechaza
con un mensaje claro en lugar de encolarse.

PRUEBA DE BUCLE
---------------

**Qué hace.** Construye una pequeña trama de estado APRS con un testigo
aleatorio de un solo uso (``SELFTST>APLT1T:>LOOPTEST <testigo>``), desvía las
tramas decodificadas a un gancho privado propio para que la trama de prueba no
se digipitee ni se suba nunca a APRS-IS, conmuta el módem a dúplex completo
mientras dura, la transmite y espera hasta 4 segundos a que la cadena
ADC / demodulador / decodificador le devuelva la misma trama. El gancho real y
el modo dúplex configurado se restauran sea cual sea el resultado.

Antes de activar el PTT espera hasta 3 segundos a que el canal quede en
silencio, para que una estación real en el aire no cause un fallo espurio. Si el
canal no se libera nunca, transmite igualmente en vez de quedarse colgado.

**Qué requiere.** Un **bucle de audio** físico: el pin del DAC cableado al pin
del ADC (a través de los atenuadores de la propia placa de interfaz, o
directamente), con masa común. También puede ejecutarse a través de un
transceptor en dúplex completo real, o mediante una radio que se escuche a sí
misma en otra radio, pero en su forma normal es una prueba de banco de la placa,
no de la radio.

.. warning::

   El dúplex completo se fuerza durante la prueba porque un bucle de cable hace
   que el módem oiga permanentemente su propia portadora, y CSMA no encontraría
   nunca un canal libre. Es una anulación deliberada y temporal, pero implica que
   la prueba transmite sin atender a lo que hay en el canal. No la ejecute con
   una antena conectada en una frecuencia concurrida.

**Cómo leer un PASS.** Un resultado correcto informa del nivel de RX en mV RMS,
de la excursión del ADC en bruto con los raíles del conversor (0/4095) como
referencia, y de la ganancia de pico del AGC. Esos números importan tanto como
la palabra PASS: una excursión sana y centrada debería quedarse bastante lejos
de cualquiera de los raíles. Un PASS con la excursión casi tocando 0 o 4095
significa que está decodificando *y* recortando, y dejará de decodificar en
cuanto algo suba de nivel.

Una coincidencia de testigo por sí sola no se acepta como PASS. La ejecución
debe haber capturado además al menos 50 cuentas de excursión real del ADC y un
nivel de RX no nulo, de modo que nunca pueda informarse un PASS sobre un bucle
abierto, una entrada flotante o una decodificación rancia de otra procedencia.

**Cómo leer un FAIL.** Los mensajes de fallo están graduados y cada uno apunta a
una parte distinta de la cadena:

.. list-table::
   :header-rows: 1
   :widths: 42 58

   * - Qué informa
     - Qué significa
   * - El ADC no entregó ni una sola muestra
     - El driver continuo del ADC o su temporizador no están funcionando. Un
       fallo de inicialización, no un problema de cableado ni de nivel.
       Reinicie; si persiste, la compilación o la asignación de pines es
       incorrecta.
   * - Muestrea, pero el código en bruto apenas se movió
     - El ADC está vivo pero no ve ningún tono. El cable de bucle falta, está
       roto, o las masas no son comunes. El desplazamiento de continua informado
       dice más: una línea clavada cerca del raíl apunta a un cortocircuito o a
       un cableado equivocado.
   * - Llegó una señal real, pero ningún demodulador se enganchó
     - El tono llega al ADC pero el correlador/PLL no logra interpretarlo.
       Compruebe que *Modulación* coincide con lo transmitido y pruebe a conmutar
       *Entrada de audio plana / de discriminador* — un bucle directo DAC-a-ADC
       nunca pasa por la red de deénfasis de una radio real. Si la ganancia del
       AGC nunca subió por encima de la unidad, el problema está en el camino del
       AGC y no en la velocidad en baudios.
   * - El PLL se enganchó, pero no volvió ninguna trama válida
     - El mensaje informa de hasta dónde llegó la máquina de estados HDLC: no
       empezar nunca una trama apunta a la recuperación de bits; empezar tramas
       que fallan el CRC apunta a un nivel o una SNR marginales antes que a un
       desajuste de modulación.
   * - Volvió una trama, pero no coincidía
     - Distorsión de audio, recorte, o un bucle que está captando algo distinto
       de la propia transmisión de esta estación.

NIVEL RX
--------

**Qué hace.** Observa la etapa de entrada de recepción durante un segundo
aproximadamente e informa de lo que vio. **No transmite nada y no toca ningún
estado del módem**, de modo que —al contrario que la prueba de bucle— puede
ejecutarse con el transceptor conectado, la antena arriba y tráfico real
decodificándose.

Esta es la medida contra la que se ajusta el lado de recepción de la interfaz de
audio.

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Campo
     - Cómo leerlo
   * - ``mVrms`` / ``peak``
     - Nivel de audio medio y de pico en la ventana. Con la radio sin squelch en
       un canal inactivo está leyendo ruido; con un paquete llegando está
       leyendo señal. Un nivel sano deja margen claro por debajo de los raíles.
   * - ``DC``
     - Dónde está polarizada la entrada. Debería situarse cerca del centro del
       rango del conversor, alrededor de 1500–1600 mV. Cerca de 0 mV o del raíl
       significa que la entrada no está polarizada — véase *Polarización interna
       de la entrada del ADC*.
   * - ``AGC``
     - La ganancia automática que aplica el módem. Una ganancia que se queda en
       1,00x con señal real significa que la entrada ya está en el objetivo o por
       encima. Una ganancia muy grande significa que la señal es demasiado floja
       y el módem está amplificando ruido junto con ella.
   * - ``raw``
     - Los extremos de conversión en bruto, frente a raíles de 0 y 4095. Este es
       su margen de recorte: llegar a 0 o a 4095 es salirse de rango.
   * - ``DCD``
     - Si algún demodulador estuvo enganchado durante la ventana. ``sí`` mientras
       llega un paquete es exactamente lo correcto; ``sí`` en un canal en
       silencio apunta a una entrada ruidosa o a un enganche falso.

**Procedimiento típico.** Quite el squelch de la radio, pulse **NIVEL RX** y
ajuste el potenciómetro de recepción hasta que los extremos en bruto usen buena
parte del rango sin acercarse a los raíles y el desplazamiento de continua esté
centrado. Después ponga el squelch normal, espere tráfico real y confirme que
aparece ``DCD sí`` y que el panel muestra decodificaciones.

.. tip::

   Es también la forma más rápida de responder a "¿está siquiera conectada la
   radio?". Una lectura plana con el contador de muestras atascado dice que el
   audio no está llegando en absoluto, y ningún ajuste en otro sitio cambiará
   eso.

PRUEBA TX
---------

**Qué hace.** Activa el transmisor, modula una trama corta de estado APRS
(``SELFTST>APLT1T:>TXTEST``) y libera el PTT, sin esperar nada de vuelta. Es la
contrapartida de transmisión de NIVEL RX: aquello contra lo que se ajusta el
nivel de transmisión cuando hay un transceptor conectado en lugar de un bucle de
cable.

La ráfaga pasa por el **camino normal de acceso al canal**: espera canal libre
como cualquier otra trama y la retiene el techo de ciclo de trabajo si está
activo y ya alcanzado. La página espera a que la ráfaga termine, con un límite de
unos tres segundos, para que el resultado signifique algo cuando se lee.

Si tiene éxito, informa de la longitud de preámbulo y de la amplitud de salida
usadas, y recuerda el objetivo: mida la desviación con otro equipo y ajuste el
potenciómetro de nivel de transmisión para **2,5–3,5 kHz**.

.. warning::

   **Esto pone una señal real en el aire.** Antes de pulsarlo, confirme la
   frecuencia, confirme la antena o la carga artificial y confirme que tiene
   licencia para transmitir ahí. En un canal APRS compartido, no lo pulse
   repetidamente mientras ajusta un potenciómetro: use una carga artificial para
   el ajuste y una única ráfaga en el aire para confirmar.

Si se niega, el mensaje dice por qué: el módem no está activado, hay otro
diagnóstico en curso, o el camino de acceso al canal descartó la trama — lo que
en la práctica significa un techo de ciclo de trabajo ya alcanzado o una cola de
transmisión llena. El registro de eventos indica cuál.

Puesta en marcha de una estación nueva, en orden
=================================================

Los campos de esta página interactúan, así que hay una secuencia que evita
perseguirse la cola.

.. list-table::
   :header-rows: 1
   :widths: 6 34 60

   * - #
     - Paso
     - Notas
   * - 1
     - Marque **Activar módem ADC/DAC de audio**, *Guardar*, **reinicie**.
     - Nada más de esta página puede probarse hasta que el hardware del módem
       esté levantado.
   * - 2
     - Ponga **Modulación** para que coincida con el canal.
     - 1200 Bd Bell 202 para APRS estándar. Si se equivoca aquí, todos los pasos
       posteriores carecen de sentido.
   * - 3
     - Con los pines del DAC y del ADC unidos en bucle, ejecute **PRUEBA DE
       BUCLE**.
     - Demuestra que la placa funciona de extremo a extremo antes de implicar a
       una radio. Anote las cifras de excursión, no sólo el PASS.
   * - 4
     - Conecte la radio. Ajuste **Entrada de audio plana / de discriminador**
       según el conector que haya usado.
     - Conector de altavoz → apagado. Puerto de datos/discriminador → activado.
   * - 5
     - Ejecute **NIVEL RX** y ajuste el potenciómetro de recepción.
     - Revise primero el desplazamiento de continua; active **Polarización
       interna de la entrada del ADC** si la entrada no está polarizada. Después
       ajuste el nivel para buena excursión con margen.
   * - 6
     - Active **Avisar cuando el audio recibido se sale de rango** y vigile la
       página *Registros* mientras llega tráfico real.
     - Confirma que no está recortando con las estaciones locales fuertes.
   * - 7
     - Sobre carga artificial, ejecute **PRUEBA TX** y ajuste **Amplitud de
       salida de transmisión** para 2,5–3,5 kHz de desviación.
     - Añada un atenuador externo si el 20 % sigue siendo demasiado fuerte.
   * - 8
     - Ajuste **Preámbulo** al comportamiento de PTT de su radio.
     - 300 ms salvo que tenga un motivo. Confirme con un vecino que le están
       digipiteando.
   * - 9
     - Ajuste **Persistencia CSMA** e **Intervalo de tiempo TX** según la
       congestión del canal.
     - Los valores por omisión (63 / 2000 ms) son correctos para un canal
       compartido normal.
   * - 10
     - Active el **limitador de ciclo de trabajo** y fije el **Tiempo máximo de
       transmisión**.
     - Haga esto antes de dejar la estación desatendida, no después.

Perfiles sugeridos
==================

.. list-table::
   :header-rows: 1
   :widths: 22 13 13 10 10 10 11 11

   * - Estación
     - Modulación
     - Preámbulo
     - Silencio
     - Persist.
     - Buffers
     - Ciclo
     - Máx. TX
   * - IGate doméstico, radio portátil, canal compartido
     - 1200 Bell 202
     - 300 ms
     - 2000 ms
     - 63
     - 1
     - sí, 20 %
     - 8000 ms
   * - Digipeater en cima, red eléctrica + amplificador
     - 1200 Bell 202
     - 400 ms
     - 2000 ms
     - 63
     - 2
     - sí, 50 %
     - 10000 ms
   * - Digipeater de relleno, canal urbano saturado
     - 1200 Bell 202
     - 300 ms
     - 3000 ms
     - 32
     - 1
     - sí, 15 %
     - 8000 ms
   * - Tracker / baliza solar
     - 1200 Bell 202
     - 300 ms
     - 2000 ms
     - 63
     - 1
     - sí, 10 %
     - 8000 ms
   * - Enlace punto a punto dedicado de 9600 Bd
     - 9600 G3RUH
     - 150 ms
     - 0 ms
     - 255
     - 3
     - no
     - 5000 ms
   * - Pasarela de HF, 300 Bd
     - 300 AFSK
     - 500 ms
     - 3000 ms
     - 63
     - 1
     - sí, 25 %
     - 20000 ms

Son puntos de partida, no prescripciones. Las cifras de ciclo de trabajo en
particular deben conciliarse con las condiciones de su propia licencia.

Referencia de campos
====================

.. list-table::
   :header-rows: 1
   :widths: 30 14 14 42

   * - Campo
     - Rango
     - Por omisión
     - Se aplica
   * - FX.25
     - apagado / solo recepción / recepción y transmisión
     - apagado
     - En vivo
   * - Activar módem ADC/DAC de audio
     - sí / no
     - sí
     - **Siguiente reinicio**
   * - Modulación
     - 0–3
     - 1 (Bell 202)
     - En vivo
   * - Entrada de audio plana / de discriminador
     - sí / no
     - sí
     - En vivo
   * - Preámbulo
     - 50–2000 ms
     - 300 ms
     - En vivo
   * - Intervalo de tiempo TX
     - 0–10000 ms
     - 2000 ms
     - En vivo
   * - Buffers de TX
     - 1–11
     - 1
     - En vivo (se lee en cada transmisión)
   * - Limitador de ciclo de trabajo
     - sí / no
     - no
     - En vivo (se lee en cada transmisión)
   * - Límite de ciclo de trabajo
     - 1–100 %
     - 25 %
     - En vivo (se lee en cada transmisión)
   * - Tiempo mínimo de PTT liberado
     - 0–5000 ms
     - 0
     - En vivo
   * - Persistencia CSMA
     - 1–255
     - 63
     - En vivo
   * - Polarización interna de la entrada del ADC
     - sí / no
     - no
     - En vivo
   * - Avisar cuando el audio recibido se sale de rango
     - sí / no
     - no
     - En vivo
   * - Amplitud de salida de transmisión
     - 20–100 %
     - 60 %
     - En vivo (siguiente muestra)
   * - Frecuencia de muestreo de transmisión
     - 38400 / 76800 Hz
     - 38400 Hz
     - **Siguiente reinicio**
   * - Tiempo máximo de transmisión
     - 0–60000 ms
     - 0 (desactivado)
     - En vivo

Cada campo numérico se acota en tres sitios contra las mismas constantes de
``main/include/aprs_service.h``: los atributos ``min``/``max`` del propio campo,
el manejador que analiza el formulario enviado y el cargador que lee
``radio.json`` desde la flash. Un archivo de configuración editado a mano o un
POST malformado no pueden, por tanto, poner en servicio un valor fuera de rango.

Lo que deliberadamente *no* está en esta página
================================================

Varios ajustes que un operador podría esperar aquí no existen, porque el
firmware no tiene equivalente en tiempo de ejecución para ellos. Se muestran de
sólo lectura, o no se muestran, en lugar de ofrecerse como controles que
guardarían en flash sin cambiar nada.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Ajuste ausente
     - Por qué
   * - Nivel de squelch
     - No hay squelch por software. Todas las muestras llegan al demodulador, y
       el decodificador AX.25 se apoya en el propio DCD del demodulador. Use el
       squelch de la radio, o déjelo abierto, que a menudo decodifica mejor.
   * - Volumen / ganancia de recepción
     - No hay etapa de ganancia de RX que ajustar. El AGC se autolimita. Fije el
       nivel con el potenciómetro de la interfaz, guiado por **NIVEL RX**.
   * - Ganancia máxima del AGC
     - El AGC se acota a sí mismo; no hay nada que configurar.
   * - Atenuación del ADC
     - Una constante de compilación (``MODEM_ADC_ATTEN``). Se muestra de sólo
       lectura.
   * - Pines de audio (ADC/DAC)
     - Cableado de placa fijado en compilación (``MODEM_ADC_GPIO`` /
       ``MODEM_DAC_GPIO``). Se muestra de sólo lectura.
   * - Pin y polaridad de PTT
     - Cableado de placa fijado en compilación (``MODEM_PTT_GPIO`` /
       ``MODEM_PTT_ACTIVE_HIGH``). Se muestra de sólo lectura; el pin está
       registrado en la tabla de propiedad de GPIO para que ninguna otra función
       pueda reclamarlo.
   * - Supresión de duplicados
     - Un único interruptor y un único par de controles para todo el firmware,
       en la página *IGate*.
   * - Intervalo de ranura CSMA
     - Fijado en 100 ms dentro del módem. *Intervalo de tiempo TX* en esta
       página es el tiempo de silencio, que es el parámetro que merece ajustarse.

Resolución de problemas
=======================

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - Síntoma
     - Dónde mirar
   * - No se decodifica nada, en ninguna dirección
     - **Modulación** primero. Después ejecute **PRUEBA DE BUCLE** sobre un
       bucle de cable para separar un problema de placa de uno de radio.
   * - PRUEBA DE BUCLE dice que el módem no está activado
     - **Activar módem ADC/DAC de audio** está apagado, o se activó sin
       reiniciar.
   * - Las estaciones locales fuertes decodifican, las débiles nunca
     - Nivel de recepción demasiado bajo, o **Entrada de audio plana / de
       discriminador** puesta al revés. Ejecute **NIVEL RX**.
   * - Las estaciones débiles decodifican, las fuertes no
     - Recorte. Active **Avisar cuando el audio recibido se sale de rango**,
       ejecute **NIVEL RX** y baje el potenciómetro de recepción.
   * - Otras estaciones oyen mi portadora pero sólo decodifican algunas tramas
     - **Preámbulo** demasiado corto, o desviación incorrecta. Suba el preámbulo
       y después mida la desviación con **PRUEBA TX**.
   * - Nadie me oye en absoluto, pero la radio transmite
     - Desviación demasiado baja, o el audio de transmisión no llega a la entrada
       de micrófono. Fije la **Amplitud de salida de transmisión** contra una
       medida de desviación.
   * - Las balizas aparecen mucho menos a menudo de lo configurado
     - El **limitador de ciclo de trabajo** las está aplazando. Revise el
       contador de descartes del panel y suba el techo o alargue los intervalos.
   * - Se descartan tramas por acumulación
     - La estación ofrece más tráfico del que el canal puede transportar. Reduzca
       lo que transmite antes de subir **Buffers de TX**.
   * - El registro informa de que se dispara el tiempo máximo de transmisión
     - **Tiempo máximo de transmisión** está por debajo de la duración de una
       transmisión real. Súbalo muy por encima del peor caso.
   * - La frecuencia de muestreo de transmisión guardada no parece aplicarse
     - Necesita un reinicio. El formulario muestra el valor guardado, no el que
       está en marcha.

.. seealso::

   :ref:`es-modem` para los perfiles del módem y la API del componente,
   :ref:`es-dsp-signal-chain` para los caminos de señal de recepción y
   transmisión, :ref:`es-web-admin` para la administración en su conjunto, y el
   capítulo *Hardware* para los esquemáticos de interfaz que asumen los valores
   por omisión de esta página.
