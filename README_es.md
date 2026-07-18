# esp32idf_APRS

**IGate / Digipeater / Tracker APRS nativo en ESP-IDF (C, sin Arduino) para el ESP32, con administración web integrada, módem por software AFSK/FSK en el propio chip (ADC + DAC) y enlace a APRS-IS.**

> ⚠️ **Trabajo en curso.** La cadena de transmisión de RF, el IGate, el digipeater, los beacons y la administración web funcionan; varias páginas de configuración existen pero sus funciones aún no están implementadas (ver [Estado y limitaciones conocidas](#estado-y-limitaciones-conocidas)).

---

## Índice

- [Qué es esto](#qué-es-esto)
- [Matriz de funcionalidades](#matriz-de-funcionalidades)
- [Hardware](#hardware)
  - [Target soportado](#target-soportado)
  - [Pinout / definición de placa](#pinout--definición-de-placa)
  - [Cableado típico a un equipo de radio](#cableado-típico-a-un-equipo-de-radio)
    - [Qué hay realmente en cada extremo](#qué-hay-realmente-en-cada-extremo)
    - [Esquemático mínimo funcional](#esquemático-mínimo-funcional)
    - [Por qué el default del PTT es una trampa](#por-qué-el-default-del-ptt-es-una-trampa)
    - [Aislación y lazos de masa](#aislación-y-lazos-de-masa)
    - [Orden de puesta en marcha](#orden-de-puesta-en-marcha)
  - [Cableado de loopback de banco](#cableado-de-loopback-de-banco)
- [Estructura del repositorio](#estructura-del-repositorio)
- [Arquitectura](#arquitectura)
  - [Secuencia de arranque](#secuencia-de-arranque)
  - [Mapa de tareas](#mapa-de-tareas)
  - [Flujo de datos](#flujo-de-datos)
- [El componente módem](#el-componente-módem-esp32idf_radioamateur_modem)
  - [Cadena de señal](#cadena-de-señal)
  - [Por qué los números son los que son](#por-qué-los-números-son-los-que-son)
  - [Referencia de configuración en tiempo de compilación](#referencia-de-configuración-en-tiempo-de-compilación)
  - [Configuración en tiempo de ejecución (`modem_config_t`)](#configuración-en-tiempo-de-ejecución-modem_config_t)
- [Componentes de la aplicación](#componentes-de-la-aplicación)
  - [`igate` — pasarela APRS-IS](#igate--pasarela-aprs-is)
  - [`digirepeater` — digipeater](#digirepeater--digipeater)
  - [`beacon` — beacons de la propia estación](#beacon--beacons-de-la-propia-estación)
  - [`message` — mensajería APRS](#message--mensajería-aprs)
  - [`lastheard` / `trafficlog` — alimentación del dashboard](#lastheard--trafficlog--alimentación-del-dashboard)
  - [`webconfig` — administración web](#webconfig--administración-web)
- [Compilación y grabación](#compilación-y-grabación)
- [Primer arranque y configuración](#primer-arranque-y-configuración)
- [Referencia de la administración web](#referencia-de-la-administración-web)
  - [Rutas HTTP](#rutas-http)
  - [Página por página](#página-por-página)
- [Almacenamiento de configuración (`config.json`)](#almacenamiento-de-configuración-configjson)
- [Presets de path y la máscara de bits de path](#presets-de-path-y-la-máscara-de-bits-de-path)
- [El LOOP TEST](#el-loop-test)
- [Localización](#localización)
- [Resolución de problemas](#resolución-de-problemas)
- [Estado y limitaciones conocidas](#estado-y-limitaciones-conocidas)
- [Notas de portabilidad](#notas-de-portabilidad)
- [Créditos](#créditos)
- [Licencia](#licencia)

---

## Qué es esto

`esp32idf_APRS` es un proyecto ESP-IDF **v5.x** que convierte un ESP32 DevKit pelado más una interfaz de audio barata en una estación APRS completa:

* **demodula** audio AFSK/FSK proveniente de la salida de altavoz/discriminador de un equipo de radio en el **ADC1**,
* **decodifica** tramas HDLC/AX.25 (opcionalmente con corrección de errores FX.25),
* las **gatea** hacia APRS-IS por Wi-Fi (`qAR`/`qAO`),
* las **digipitea** de vuelta por RF (WIDEn-N / TRACEn-N / RELAY / ECHO / GATE),
* **emite beacons** con su propia posición,
* **modula** y transmite tramas a través del **DAC de 8 bits** del ESP32, activando el equipo mediante un GPIO de PTT,
* y se configura íntegramente desde una **administración web HTTP** servida por el propio dispositivo — sin consola serie ni recompilación para los ajustes ordinarios.

Todo es C puro. No hay núcleo de Arduino, ni `String`, ni PlatformIO. Toda la cadena DSP — demodulador por correlador, recuperación de bits por DPLL, NRZI, tramador HDLC, códec AX.25, FEC Reed–Solomon de FX.25 — corre en el propio ESP32, usando únicamente el SAR-ADC en modo DMA/continuo, el DAC y un GPTimer.

---

## Matriz de funcionalidades

| Área | Estado | Notas |
|---|---|---|
| AFSK 1200 Bd Bell 202 (APRS estándar) | ✅ | demodulador doble, perfil por defecto |
| AFSK 1200 Bd ITU V.23 (1300/2100 Hz) | ✅ | |
| AFSK 300 Bd (1600/1800 Hz) | ✅ | estilo HF |
| G3RUH FSK 9600 Bd | ✅ | requiere audio plano/discriminador |
| RX+TX de tramas HDLC / AX.25 UI | ✅ | `AX25_FRAME_MAX_SIZE = 329` |
| FX.25 (FEC RS sobre AX.25) | ✅ | `-DENABLE_FX25`, modos solo-RX / RX+TX |
| Tramado KISS | 🟡 | el código está en el componente, **no está conectado a ningún puerto serie en esta app** |
| Activación de PTT (GPIO y polaridad seleccionables en runtime) | ✅ | validado contra los pines de ADC/DAC/flash |
| CSMA / TX time-slot / preámbulo TXDelay | ✅ | `preamble`, `tx_timeslot` |
| DCD (detección de portadora) | ✅ | derivado del demodulador; sin entrada de squelch por hardware |
| IGate APRS-IS RF→INET | ✅ | filtros, deduplicación, `qAR`/`qAO` |
| IGate APRS-IS INET→RF | ✅ | filtrado por tipo de paquete con `inet2rfFilter` (`aprs_filter.c`) |
| Digipeater | ✅ | WIDEn-N, TRACEn-N, RELAY/ECHO/GATE, supresión de duplicados |
| Beacons de posición fija (tracker / igate / digi) | ✅ | tres tareas FreeRTOS independientes |
| SmartBeaconing / tracker con GPS | ❌ | los campos de configuración existen, la lógica no |
| Mensajería APRS + ack/reintentos | ✅ | por RF y/o INET |
| Cifrado AES-128-CBC de mensajes APRS | ✅ | `mbedtls`, IV derivado por MD5, payload en base64 |
| Administración web (autenticación HTTP Basic) | ✅ | ~25 páginas, dashboard en vivo |
| Log de tráfico en vivo + tabla de últimos escuchados | ✅ | long-poll JSON (`?since=<seq>`) |
| Almacenamiento LittleFS: subir/descargar/borrar/formatear | ✅ | partición de 400 KB |
| Sincronización horaria SNTP (3 hosts) | ✅ | el reloj siempre se mantiene en UTC |
| Control de frecuencia de CPU (80/160/240 MHz) | ✅ | `esp_pm_configure()` |
| Wi-Fi AP / STA / AP+STA, escaneo, potencia de TX | ✅ | 5 slots STA (se usa el primero habilitado) |
| Localización (EN / ES) | ✅ | en tiempo de compilación, un idioma por imagen |
| Actualización OTA | ❌ | la tabla de particiones tiene un único `factory`; la página About lo aclara |
| Módulo RF LoRa / SX127x-SX128x | ❌ | solo UI + configuración, `ENABLE_RF_MODULE` está comentado |
| VPN WireGuard, MQTT, GNSS, meteorología, telemetría, sensores | ❌ | las páginas/configuración existen; los módulos están deshabilitados en `app_config.h` |
| Bluetooth, PPP/GSM, pantalla OLED, Modbus | ❌ | campos de configuración conservados solo por compatibilidad |

Leyenda: ✅ implementado · 🟡 parcial · ❌ no implementado (solo andamiaje)

---

## Hardware

### Target soportado

* **ESP32** (clásico, Xtensa doble núcleo) — `CONFIG_IDF_TARGET=esp32`, 4 MB de flash.
* El doble núcleo **no es opcional**: la ISR del ADC y el reloj de muestreo del DAC están fijados a núcleos *distintos* a propósito (ver [Por qué los números son los que son](#por-qué-los-números-son-los-que-son)).
* El ESP32-S2 tiene DAC en GPIO17/18 y requeriría ajustar el header de configuración. **El ESP32-S3/C3/C6/H2 no tiene DAC en absoluto** y no puede ejecutar la cadena de TX sin modificaciones.

### Pinout / definición de placa

La definición de placa vive en el **`CMakeLists.txt` de nivel superior**, aplicada *antes* de `project()` mediante `idf_build_set_property(COMPILE_DEFINITIONS ... APPEND)`:

```cmake
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_ADC_GPIO=33"      APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_DAC_GPIO=25"      APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_PTT_GPIO=26"      APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_PTT_ACTIVE_HIGH=0" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_LED_TX_GPIO=-1"   APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MODEM_LED_RX_GPIO=-1"   APPEND)
```

| Señal | Por defecto | Restricciones estrictas |
|---|---|---|
| **Audio de entrada (ADC)** | `GPIO33` (ADC1_CH5) | **Solo 32–39.** El ADC2 es inutilizable mientras el Wi-Fi está activo, y este firmware siempre tiene el Wi-Fi activo. Forzado con `#error`. |
| **Audio de salida (DAC)** | `GPIO25` (DAC_CHAN_0) | **Solo 25 o 26.** El DAC del ESP32 está cableado a esos pads y no es enrutable por la matriz GPIO. Forzado con `#error`. |
| **PTT** | `GPIO26`, activo **bajo** | El valor de compilación es solo el *default de reserva*; el pin/polaridad efectivos de PTT son **seleccionables en runtime desde la página Radio**, validados por `afsk_ptt_gpio_is_valid()` (rechaza GPIO34–39 solo-entrada, GPIO6–11 flash/PSRAM, y los propios pines de ADC/DAC). `-1` = deshabilitado. |
| **LEDs de TX / RX** | deshabilitados (`-1`) | Cualquier GPIO capaz de salida. |

> ⚠️ Con los valores por defecto, **`MODEM_DAC_GPIO=25` y `MODEM_PTT_GPIO=26` están ambos en el par de pads del DAC.** Eso está bien (el 26 solo es DAC_CHAN_1 si lo *seleccionás*), pero si movés la salida de audio al GPIO26 tenés que mover el PTT a otro lado. El validador rechazará el solapamiento en runtime.

> **Nota sobre los campos de GPIO de la página "Mod".** `g_config.adc_gpio`, `dac_gpio`, `rf_sql_gpio`, `rf_pwr_gpio`, `adc_atten`, `sql_level`, `volume`, `agc_max_gain` se siguen cargando, guardando y editando — pero **ya nada en la cadena de audio del módem los lee** desde el cambio de `esp32_IDF_libAPRS` a `esp32idf_radioamateur_modem`. Se conservan únicamente para que los `config.json` existentes hagan round-trip sin cambios. Para mover los pines de audio, editá el bloque de `CMakeLists.txt` de arriba y recompilá.

### Cableado típico a un equipo de radio

Ninguno de los dos extremos de este enlace se puede conectar directamente al otro. El lado del ESP32 es una interfaz de datos muestreados, a 3,3 V y con polarización de continua; el lado de la radio es una interfaz analógica de alterna, referida a masa y de nivel de milivoltios. En el medio tienen que pasar tres cosas: **atenuar** (TX), **desplazar y limitar** (RX), y **conmutar** (PTT).

#### Qué hay realmente en cada extremo

Todas las cifras del lado ESP32 salen de las propias constantes de compilación del componente, no de un ideal de hoja de datos — ver [Referencia de configuración en tiempo de compilación](#referencia-de-configuración-en-tiempo-de-compilación).

| Nodo | Qué hay realmente | De dónde sale |
|---|---|---|
| **GPIO25 (DAC), transmitiendo** | 1,65 V de continua con una excursión de **≈1,97 Vpp** encima (códigos 52…204 → 0,67–2,64 V) ⇒ **≈0,70 Vrms** para una senoidal, más las **imágenes de reconstrucción alrededor de 38,4 kHz** | `DAC_MID = 128`, `MODEM_DAC_AMPLITUDE_PCT = 60`, `MODEM_DAC_SAMPLERATE = 38400` |
| **GPIO25 (DAC), en reposo / antes de `modem_init()`** | ~1,65 V una vez inicializado; **indefinido y flotante durante el reset y los primeros ~5 s del arranque** (la calibración del reloj del ADC) | `modem_init()` bloquea ~5 s |
| **GPIO33 (ADC)** | Ventana **0–3,1 V**, normalizada como `(raw − dc_avg)/2048`, o sea ±1,0 ≙ ±1,55 V. El AGC apunta a **310 mVrms** en el pin, lo alcanza desde apenas **≈39 mVrms** (`AGC_MAX_GAIN = 8`), congela la ganancia por debajo de **≈16 mVrms** (piso de ruido) y **recorta por encima de ≈1,1 Vrms** | `MODEM_ADC_ATTEN = ADC_ATTEN_DB_12`, `AGC_TARGET_RMS = 0.2` |
| **GPIO26 (PTT)** | Salida CMOS común de 3,3 V, **activa en BAJO con la definición de placa que viene de fábrica**, y **entrada flotante durante el reset** | `MODEM_PTT_GPIO=26`, `MODEM_PTT_ACTIVE_HIGH=0` |
| **MIC IN** del equipo (jack del micrófono de mano) | 5–20 mVrms, normalmente con preénfasis y una polarización de continua para el electret | necesita ≈30–40 dB de atenuación |
| **DATA IN** del equipo (mini-DIN-6 pin 1, "PKT IN") | ≈40 mVpp ⇒ **≈14 mVrms**, plano, sin preénfasis | necesita ≈35 dB de atenuación |
| **SPKR / AF OUT** del equipo | 0,1–3 Vrms, depende de la perilla de volumen, con deénfasis | necesita atenuador + polarización |
| **DATA OUT / DISC** del equipo (mini-DIN-6 pin 4) | 100–300 mVrms, **nivel fijo, independiente del squelch, plano** | solo necesita polarización — este es el bueno |

Dos consecuencias que conviene tener claras antes de soldar:

* **El DAC está ~35 dB por encima** de lo que acepta cualquier entrada de la radio. Una conexión directa no solo sobredesvía: ensucia todo el canal.
* **Un puerto `DATA OUT` ya está dentro de la ventana del AGC** (100–300 mVrms contra el rango útil de 39 mVrms–1,1 Vrms). Si tu equipo tiene jack de datos, el lado de RX es una red de polarización y nada más — sin potenciómetro, sin ganancia.

#### Esquemático mínimo funcional

Pasivo, ~15 componentes, sin operacionales. Esto es todo.

```
 ── TX ── ESP32 ────────────────────────────────────────────────► equipo ──

  GPIO25 ──[R1 2k2]──┬──[R2 2k2]──┬──[C1 10µ]──[R3 10k]──┬─ RV1 extremo
   (DAC)             │            │      +               │
                   [C2 15n]     [C3 15n]              [RV1 1k]  ajuste de nivel
                     │            │                      ├─ cursor ──► MIC / DATA IN
                    GND          GND                     │
                                                         └─ extremo ─► GND de audio del equipo

 ── RX ── equipo ───────────────────────────────────────────────► ESP32 ──

                                       nodo de polarización
  SPKR/DISC ─[RV2 10k]─ cursor ─[C4 10µ]──┬──────[R7 1k]───┬──► GPIO33 (ADC)
                 │                 +      │                │
   GND equipo ───┘                   [R5 10k]─► 3V3    [D1]─► 3V3   BAT54S
   (omitir RV2 con un DATA OUT fijo:      │            [D2]─► GND   (o 2×1N4148)
    ir directo a C4)                 [R6 10k]─► GND        │
                                         │              [C5 1n]
                                        GND                │
                                                          GND

 ── PTT ── opción A: opto, aislado, coincide con el default activo en BAJO ──

                      ┌─ PC817 ─┐
   3V3 ──[R8 470]──[A]│▶      C│────► PTT del equipo
   GPIO26 ─────────[K]│        E│────► GND de PTT del equipo
                      └─────────┘
   GPIO26 en BAJO → LED encendido → transmite.   Flotante en el reset → LED apagado → sin transmitir.

 ── PTT ── opción B: MOSFET de lado bajo, sin aislar, requiere ACTIVE_HIGH=1 ──

                       ┌─ 2N7000 / BS170 ─┐
   GPIO26 ──[R9 1k]────┤ G              D ├────► PTT del equipo
                       │                S ├────► GND (común)
            [R10 10k]──┤ G→S              │
                       └──────────────────┘
   GPIO26 en ALTO → transmite.   R10 lo mantiene sin transmitir durante el reset y el deep sleep.
```

| Ref | Valor | Función | Si lo cambiás |
|---|---|---|---|
| **R1, R2 / C2, C3** | 2k2 / **15 nF** | Pasabajos de reconstrucción de dos polos, **fc ≈ 4,8 kHz**. Mata las imágenes del DAC de 38,4 kHz a **≈−36 dB** costando solo −0,3 dB a 1200 Hz y −0,8 dB a 2200 Hz (≈0,5 dB de *twist*) | **22 nF** (fc 3,3 kHz, −43 dB a 38,4 kHz) si solo vas a usar AFSK y querés matar más las imágenes; **10 nF** (fc 7,2 kHz, −29 dB) es **obligatorio para 9600 Bd G3RUH**, que necesita respuesta plana más allá de ~5 kHz |
| **C1** | 10 µF | Bloqueo de continua. La polarización de reposo de 1,65 V del DAC nunca debe llegar a una entrada de micrófono | Contra R3+RV1 = 11 kΩ da fc ≈ 1,4 Hz — no bajes de 1 µF |
| **R3 / RV1** | 10k / 1k preset | Atenuador + nivel. El divisor fijo 11:1 hace que el preset trabaje en **0–64 mVrms** con ≈32 mV a media vuelta, en vez de vivir en el 4 % inferior de un potenciómetro de 10k pelado | La impedancia del cursor es ≤250 Ω, así que ataca cualquier entrada de micrófono o de datos sin cargarla más |
| **RV2** | 10k | Nivel de RX. **Omitilo por completo con un puerto `DATA OUT`** — ya está en el nivel correcto | |
| **C4** | 10 µF | Bloqueo de continua + pasaaltos. Contra el Thévenin de 5 kΩ de la polarización: fc ≈ 3 Hz | |
| **R5, R6** | 10k / 10k | Polarización a medio riel, **1,65 V**, justo en el centro de la ventana de 0–3,1 V del ADC. Thévenin 5 kΩ | Bajalas a **4k7/4k7** (2,35 kΩ) si ves errores de nivel — el SAR del ESP32 quiere impedancia de fuente baja |
| **R7 / C5** | 1k / 1 nF | Amortiguador para la patada de carga del capacitor de muestreo del SAR. **No** es un filtro de audio: fc ≈ 159 kHz | **Nunca** pongas 100 nF acá, el reflejo típico de "desacoplar el ADC" — con R7 eso es un pasabajos de 1,6 kHz y se come el tono de marca de 2200 Hz |
| **D1, D2** | BAT54S | Recortan GPIO33 contra los rieles. R7 limita la corriente de falla. Seguro barato contra una perilla de volumen a 3 Vrms | |
| **R8** | 470 Ω | ≈4,5 mA por el LED del PC817, drenados por GPIO26 — bien dentro del presupuesto de 12 mA cómodos / 20 mA absolutos | |
| **R10** | 10k | **La pieza que todo el mundo omite.** Sin ella la compuerta del MOSFET queda flotante durante el reset y el equipo puede transmitir al encender | |

#### Por qué el default del PTT es una trampa

La definición de placa que viene es `MODEM_PTT_ACTIVE_HIGH=0` — **GPIO26 se pone en BAJO para transmitir**. El circuito reflejo de "NPN con la base al GPIO y el colector al PTT" es un driver **activo en ALTO** y va a dejar el transmisor al aire todo el tiempo en que el ESP32 *no* está transmitiendo, o sea, permanentemente.

Entonces: elegí un driver cuya polaridad coincida con la configuración, o cambiá la configuración para que coincida con tu driver.

* La **opción A (opto)** invierte, así que coincide con el `ACTIVE_HIGH=0` de fábrica tal cual, y de paso regala aislación galvánica.
* La **opción B (MOSFET)** no invierte. Poné `MODEM_PTT_ACTIVE_HIGH=1` en el `CMakeLists.txt` de nivel superior, o cambiá la polaridad en runtime desde la página **Radio** (el pin y la polaridad son ambos seleccionables en runtime — ver [Pinout / definición de placa](#pinout--definición-de-placa)).

En cualquier caso, **verificá antes de conectar la radio**: alimentá la placa y confirmá con un tester que la línea de PTT queda abierta durante el reset, durante los ~5 s completos del arranque y en reposo. `modem_init()` bloquea unos 5 segundos calibrando el reloj del ADC, y las tareas de beacon transmiten apenas arrancan — un PTT con la polaridad al revés te da cinco segundos de portadora sin modular antes de que el firmware siquiera llegue al módem.

Los equipos portátiles con "K-plug" (Baofeng y compañía) son otra cosa: ahí el PTT es un interruptor entre el anillo y la manga del micrófono, normalmente a través de una resistencia, y el audio de micrófono comparte el mismo conductor. La salida del opto va en paralelo a ese interruptor, y el atenuador de micrófono ataca el mismo nodo.

#### Aislación y lazos de masa

El circuito de arriba comparte masa con la radio, que es la fuente habitual de zumbido, ruido de alternador y del clásico "funciona hasta que transmito". Si escuchás algo de eso:

* **Transformadores de aislación de audio**, 600:600 Ω (Bourns 42TL022, Tamura MET-01, o cualquier transformador 1:1 de telefonía), en lugar de C1 y C4. La red de polarización queda del lado del ESP32 del transformador de RX.
* **Mantené el opto** (opción A) para que el retorno del PTT no vuelva a crear la masa que acabás de cortar.
* La **entrada de RF** en TX aparece como PTT trabado o como un módem que solo falla a plena potencia. Cable blindado, puntas cortas, un ferrite de clip en el conector del equipo, y 47–100 pF de cada línea de audio al chasis *del equipo*, en el conector.

#### Orden de puesta en marcha

1. **Primero el loop test, sin radio.** GPIO25 → GPIO33 con un cable pelado (ver [Cableado de loopback de banco](#cableado-de-loopback-de-banco)). Si eso falla, no hay circuitería externa que lo arregle.
2. **Después RX, todavía sin TX.** Abrí el squelch, metele tráfico real y mirá la columna **AUDIO** de la tabla de tráfico en vivo (es el propio `mVrms` que mide el módem en el pin). Movés RV2 hasta **≈300 mVrms en los paquetes** — es el objetivo del AGC, así que el lazo queda en ganancia unitaria y con el máximo margen para los dos lados. Cualquier cosa entre ~50 mV y ~800 mV decodifica; por debajo de 40 mV te estás quedando sin ganancia de AGC, por encima de 1,1 Vrms estás recortando. En este firmware **no hay entrada de squelch por hardware** — el DCD sale del demodulador — así que dejar el squelch abierto es lo correcto, no un parche.
3. **TX al final, contra una carga fantasma.** Ajustá RV1 para **≈3,0 kHz de desviación** (2,5–3,5 kHz) con un medidor de desviación, o comparando con el audio de una estación conocida en un segundo receptor. La sobredesviación es la causa número uno de "mi igate escucha a todos pero nadie me escucha a mí".
4. **9600 Bd G3RUH** necesita el camino plano/de discriminador en los dos extremos: `DATA IN`/`DATA OUT`, 10 nF en C2/C3, y la casilla *Audio low-pass filter* puesta para `flat_audio`. Una salida de altavoz y una entrada de micrófono no lo van a llevar, sin importar cómo ajustes los niveles.

### Cableado de loopback de banco

Para el [LOOP TEST](#el-loop-test), simplemente cableá **`GPIO25` → `GPIO33`** (salida del DAC directo a la entrada del ADC). Sin radio, sin PTT. El test transmite una trama APRS y espera que la misma placa la decodifique de vuelta.

---

## Estructura del repositorio

```
workspace-APRS/esp32_APRS_igate/
├── CMakeLists.txt                  ← definición de placa (pines ADC/DAC/PTT/LED) + project()
├── partitions.csv                  ← nvs / phy_init / factory(1500K) / storage(400K, LittleFS)
├── sdkconfig                       ← target=esp32, flash 4MB, particiones personalizadas
├── dependencies.lock               ← idf 5.5.4, joltwallet/littlefs 1.22.1
├── LICENSE                         ← GPL-3.0
│
├── main/                                  (la aplicación)
│   ├── main.c                      ← app_main, arranque/reconexión Wi-Fi, orden de boot
│   ├── app_config.c/.h             ← app_config_t, valores de fábrica, carga/guardado JSON
│   ├── storage.c                   ← montaje/formateo/uso de LittleFS
│   ├── aprs_service.c/.h           ← el pegamento: dispatch de RX, helper de TX, config del módem, stats, loop test
│   ├── beacon.c/.h                 ← 3 tareas de beacon independientes (trk / igate / digi)
│   ├── net_state.c/.h              ← flag "¿realmente tenemos internet?"
│   ├── time_sync.c/.h              ← SNTP (siempre UTC)
│   └── cpu_freq.c/.h               ← esp_pm_configure() desde la página System
│
├── components/
│   ├── esp32idf_radioamateur_modem/       (el módem por software — el corazón del proyecto)
│   │   ├── esp32idf_radioamateur_modem.h  ← API pública + capa de conveniencia APRS
│   │   ├── include/…_config.h             ← TODAS las constantes de placa/DSP de compilación
│   │   ├── src/afsk.c    (1447 ln)        ← ingesta DMA del ADC, AGC, FIR de diezmado, ISR del DAC, PTT
│   │   ├── src/modem.c   (899 ln)         ← correladores, DPLL, tablas de tonos, DCD, calibración
│   │   ├── src/ax25.c    (1326 ln)        ← tramador HDLC, NRZI, bit-stuffing, códec AX.25, cola de TX
│   │   ├── src/fx25.c, lwfec/rs.c, gf.c   ← FEC Reed–Solomon de FX.25
│   │   ├── src/kiss.c                     ← tramado KISS (sin uso en esta app)
│   │   └── src/crc_ccit.c                 ← FCS
│   │
│   ├── igate/          ← cliente TCP de APRS-IS, login, filtros, dedup, RF→INET / INET→RF
│   ├── digirepeater/   ← lógica de path WIDEn-N / TRACEn-N / RELAY / ECHO / GATE
│   ├── message/        ← mensajería APRS, ack/reintentos, AES-128-CBC + base64
│   ├── lastheard/      ← anillo en RAM de estaciones escuchadas → JSON del dashboard
│   ├── trafficlog/     ← anillo en RAM de líneas de tráfico → JSON del dashboard (long-poll por seq)
│   └── webconfig/      ← administración con esp_http_server
│       ├── web_server.c            ← tabla de rutas
│       ├── web_common.c            ← auth, parseo de formularios, esqueleto HTML, helpers de campos
│       ├── pages/*.c               ← un archivo por página de administración
│       └── translations/           ← translations.h + lang_en.h + lang_es.h
│
└── managed_components/joltwallet__littlefs/   (traído por el gestor de componentes)
```

**Tamaño del código:** ~18,2 k líneas de C propio entre `main/` y `components/` (sin contar `managed_components/`), de las cuales ~4,9 k son el núcleo DSP del módem y ~3,4 k la administración web.

---

## Arquitectura

### Secuencia de arranque

`app_main()` corre en la tarea principal del sistema, cuyo stack está fijado en `CONFIG_ESP_MAIN_TASK_STACK_SIZE` (3584 B) — muy poco para `esp_netif` + `esp_wifi` + `esp_http_server` + cJSON. Así que `app_main()` hace solo las dos cosas que deben preceder a todo, y delega:

```
app_main()
 ├─ nvs_flash_init()          (borra y reintenta ante NO_FREE_PAGES / NEW_VERSION_FOUND)
 ├─ storage_init()            (monta LittleFS en /storage, autoformatea en el primer arranque)
 └─ xTaskCreate(app_task, 8192 B, prio 5)   ── y retorna; FreeRTOS recupera la tarea principal

app_task()
 ├─ app_config_load()                  ← /storage/config.json, o escribe+carga valores de fábrica
 ├─ cpu_freq_apply()                   ← 80/160/240 MHz desde la página System
 ├─ net_state_init()                   ← "todavía no hay internet"
 ├─ wifi_init()                        ← AP / STA / AP+STA según g_config.wifi_mode
 ├─ vTaskDelay(10 ms)                  ← cede CPU para que corra IDLE; evita un falso disparo del TWDT
 ├─ time_sync_start()                  ← SNTP, no bloqueante
 ├─ web_server_start()                 ← esp_http_server, 64 handlers de URI, stack de 8 KB
 ├─ aprs_service_start()               ← ⚠ DEBE preceder a modem_init(): instala el callback de RX
 │    ├─ trafficlog_init / lastheard_init / message_init
 │    ├─ message_set_tx_handler / igate_set_inet2rf_handler
 │    ├─ modem_set_rx_callback(on_rx_frame)
 │    ├─ igate_start()                 ← siempre arranca; se queda ocioso solo si nada necesita APRS-IS
 │    ├─ beacon_start()                ← 3 tareas
 │    └─ xTaskCreate(serviceTickTask)  ← tick de 1 Hz para reintentos de mensajes
 ├─ if (audio_modem_en) modem_init()   ← ⏳ BLOQUEA ~5 s calibrando el reloj real del ADC (una vez por arranque)
 │      └─ aprs_service_notify_modem_ready()
 └─ APRS_setCallsign(...)
```

Dos reglas de orden son críticas y están comentadas como tales en el código:

1. **`aprs_service_start()` antes de `modem_init()`** — el módem empieza a entregar tramas *desde dentro* de `modem_init()`; el callback ya tiene que estar instalado.
2. **Los beacons arrancan antes de que el módem esté listo** — transmiten inmediatamente al entrar, así que `aprs_service_send_tnc2()` descarta tramas con un log de depuración hasta que `s_modemReady` se pone en true, en lugar de llegar a `Ax25WriteTxFrame()` antes de que `Ax25Init()` haya corrido.

### Mapa de tareas

| Tarea | Stack | Prio | Núcleo | Creada por | Rol |
|---|---|---|---|---|---|
| `app_task` | 8192 B | 5 | cualquiera | `app_main` | arranque + ocio |
| DSP de RX del módem | `MODEM_RX_TASK_STACK` 4096 B | 10 | **0** (`MODEM_RX_TASK_CORE`) | `AFSK_init()` | drena el anillo del ADC, corre los demoduladores |
| `modem_svc` | 6144 B | — | cualquiera | `modem_init()` | conduce `AFSK_ServiceTx()` / `Ax25TransmitCheck()`, entrega tramas RX al callback |
| ISR de DMA del ADC | — | — | **0** (`MODEM_ADC_ISR_CORE`) | driver | tramas de conversión → ring buffer |
| Reloj de muestreo del DAC (GPTimer, nivel 3) | — | — | **1** (`MODEM_DAC_TIMER_CORE`) | `AFSK_init()` | una muestra de DAC cada 1/38400 s |
| `igate_task` | — | — | cualquiera | `igate_start()` | socket APRS-IS, login, bombeo de RX, reconexión |
| `trk_beacon_task` / `igate_beacon_task` / `digi_beacon_task` | `BEACON_TASK_STACK_WORDS` | 4 | cualquiera | `beacon_start()` | beacons de posición propia |
| `aprs_svc_tick` | 3072 B | 4 | cualquiera | `aprs_service_start()` | reintento de mensajes a 1 Hz |
| `httpd` | 8192 B | — | cualquiera | `web_server_start()` | administración web |
| `esp_timer` | — | — | — | IDF | back-off de reconexión Wi-Fi |

### Flujo de datos

```
                        ┌──────────────── RX de RF ────────────────┐
  audio de la radio ─► ADC1 (DMA, 76800 Hz)
                   │
                   ├─ adc_ingest()  (desintercala pares del DMA, bloqueo de DC, AGC, RMS)
                   ├─ FIR de diezmado ─► flujo de demodulación a 9600 Hz
                   ├─ correlador ×1–2  ─► recuperación de bits DPLL ─► NRZI ─► HDLC
                   ├─ decodificación RS de FX.25 (opcional)
                   └─ trama AX.25 ──► modem_rx_frame_t ──► on_rx_frame()          [aprs_service.c]
                                                              │ ax25_decode()
                                                              ▼
                                                       s_rxHook ──► aprs_msg_callback()
                                                              │
             ┌────────────────────────────┬──────────────────┼───────────────────┐
             ▼                            ▼                  ▼                   ▼
   trafficlog_add_pkt("RX")     lastheard_add(RF)    digiProcess()        igateProcess()
                                                        │ =2 → reescribe     │ → línea qAR/qAO
                                                        ▼                    ▼
                                            aprs_service_send_tnc2()   socket APRS-IS
                                                                            │
                        ┌──────────────── INET → RF ────────────────────────┘
                        ▼
                inet2rfHandler(line) ─► lastheard_add(INET)
                                     ├─ handleIncomingAPRS()  (mensajes/acks)
                                     └─ aprs_service_send_tnc2(line)      [si inet2rf]

                        ┌──────────────── TX de RF ────────────────┐
  aprs_service_send_tnc2(text,len)
        ├─ descarta si !s_modemReady  o  len ≥ AX25_FRAME_MAX_SIZE (329)
        ├─ modem_send_tnc2() ─► ax25_encode() ─► cola de TX
        └─ espera CSMA (salvo full_duplex) ─► PTT on ─► preámbulo (TXDelay)
                 ─► HDLC + bit-stuffing + FCS ─► NRZI ─► acumulador de fase
                 ─► LUT senoidal de 512 entradas ─► ISR del DAC @ 38400 Hz ─► GPIO25 ─► radio
```

---

## El componente módem (`esp32idf_radioamateur_modem`)

Incluido bajo `components/`, GPL-3.0, de **Emiliano Augusto González (LU3VEA)** — upstream: <https://github.com/hiperiondev/esp32idf_radioamateur_modem>. Deriva de **VP-Digi** (SQ8VPS), **ESP32APRS_Audio** (nakhonthai) y **LibAPRS** (Mark Qvist).

### Cadena de señal

| Etapa | Tasa | Dónde |
|---|---|---|
| SAR-ADC1 continuo/DMA, tramas de conversión de 128 muestras | **76 800 Hz** | ISR del driver en el núcleo 0 |
| ingesta: desintercalado de pares, eliminación de offset de DC, AGC, medición de RMS | 76 800 Hz | `afsk.c` |
| FIR de diezmado (relación **8:1**) | → **9 600 Hz** | `afsk.c` |
| correlador (mark/space), pasabajos, DPLL, decodificación NRZI | 9 600 Hz | `modem.c` |
| desentramado HDLC, des-stuffing de bits, verificación de FCS, decodificación RS de FX.25 | — | `ax25.c` / `fx25.c` |
| ⟵ TX ⟶ codificación AX.25, FCS, bit stuffing, NRZI, acumulador de fase de 32 bits, LUT senoidal de 512 entradas | **38 400 Hz** | `ax25.c` / `modem.c` / `afsk.c` |

Perfiles (`modem_mode_t` / `enum ModemType`, con la misma numeración en ambos, por eso la app puede hacer un cast directo):

| Valor | Perfil | Baudios | Tonos |
|---|---|---|---|
| 0 | AFSK300 | 300 | 1600 / 1800 Hz |
| 1 | **Bell 202** (por defecto, APRS estándar) | 1200 | 1200 / 2200 Hz |
| 2 | ITU V.23 | 1200 | 1300 / 2100 Hz |
| 3 | G3RUH FSK | 9600 | — |

El perfil de 1200 Bd corre **dos demoduladores en paralelo**, sintonizados de forma ligeramente distinta, para elevar la probabilidad de decodificación (`MODEM_MAX_DEMODULATOR_COUNT = 2`).

### Por qué los números son los que son

El header de configuración de este componente está inusualmente bien documentado, y el razonamiento importa si lo tocás:

* **ADC a 76 800 Hz, no a 38 400.** 38 400 le da al perfil de 9600 Bd exactamente *cuatro* muestras de ADC por símbolo. El instante de muestreo del DPLL queda entonces cuantizado al 25 % de un símbolo y el voto por mayoría de tres muestras de `decode()` abarca el 75 % de un símbolo — la ventana del voto siempre alcanza una transición. La simulación en host del `modem.c` real, con relojes reales y **sin ruido**, producía errores de bit duros en cada fase donde los instantes del ADC se alinean con los de actualización del DAC; los dos relojes difieren en ~0,05 %, así que la alineación recorre esas fases cada ~55 ms. A 76 800 la misma simulación da cero errores de bit en todas las fases e incluso con hasta 30 µs de jitter en los flancos de TX. A los perfiles AFSK nunca les importó (se demodulan a 9600 Hz por un correlador tras el diezmado) y miden idéntico a cualquiera de las dos tasas. **Costo:** el doble de trabajo en el DSP de RX y `MODEM_RESAMPLE_RATIO` pasa a 8, lo que exige el FIR de diezmado más largo — un filtro de 8 taps calculado para 4:1 no hace antialias de uno de 8:1.
* **El DAC se queda en 38 400 Hz** (= 32 × 1200, múltiplo exacto de cada tasa de baudios soportada). El transmisor pone los flancos de símbolo exactamente sobre muestras del DAC sea cual sea la tasa; era el *receptor* el que necesitaba resolución.
* **`MODEM_ADC_CONV_FRAME = 128`, no el tamaño de bloque.** La propia ISR del ADC del IDF llama a `xRingbufferSendFromISR()`, que hace todo el `memcpy` **dentro de `portENTER_CRITICAL_ISR()`**. En Xtensa eso eleva `PS.INTLEVEL` a 3 — y el reloj de muestreo del DAC *es* una interrupción de nivel 3. Así que la ISR del DAC queda enmascarada durante toda la copia: 768 muestras ≈ 11 µs (10 % de un símbolo a 9600 Bd — fatal), 128 muestras ≈ 2 µs (2 % — dentro del presupuesto). Ninguna cantidad de `IRAM_ATTR` de nuestro lado ayuda: el código que bloquea es el del driver, ya está en IRAM, y simplemente es largo. A 1200 Bd, 11 µs es el 1,3 % de un símbolo y resulta invisible — que es exactamente por qué todos los perfiles AFSK pasaban mientras G3RUH perdía tramas.
* **`MODEM_DAC_TIMER_CORE (1) ≠ MODEM_ADC_ISR_CORE (0)`.** `portENTER_CRITICAL_ISR()` enmascara nivel ≤3 solo en el núcleo *local*. Poné el reloj del DAC en el otro núcleo y la ISR del ADC simplemente girará esperando el lock en vez de enmascararlo. Forzado con `#error`. Los dos arreglos (tramas chicas, núcleos separados) son independientes y ambos están aplicados.
* **`ModemCalibrateSampleRate()`** — `modem_init()` bloquea ~5 s en el arranque midiendo la tasa *real* del ADC (`modem_measure_adc_rate()`), porque el paso del PLL de cada perfil se computa a partir de la relación *nominal* ADC/DAC y la diferencia es, si no, un error de régimen permanente que el DPLL debe seguir durante toda una transmisión. La tasa de alarma del DAC ya se conoce exactamente por la configuración del timer (`afskGetDacAlarmRate()`), así que solo hay que medir el lado del ADC. Ambos relojes derivan del mismo cristal, así que la relación es una propiedad fija de la placa: se mide **una vez por arranque** y se reaplica en cada cambio de perfil.
* **`MODEM_RX_FIFO_SIZE = 4096` muestras** — dimensionado en *muestras*, así que se encogió en *tiempo* cuando la tasa se duplicó (2048 eran 53 ms a 38,4 k, pero solo 26,7 ms a 76,8 k — apenas un bloque de 20 ms). 4096 restaura el margen. Verificado: debe alojar ≥ 2 bloques, ya que `AFSK_Poll()` solo consume bloques enteros.

Las guardas `#error` de compilación imponen: pin de DAC ∈ {25, 26}; pin de ADC ∈ 32–39; `MODEM_ADC_SAMPLERATE % 9600 == 0`; FIFO ≥ 2 bloques; `MODEM_ADC_CONV_FRAME` par, que divida a `MODEM_BLOCK_SIZE`, y alineado en bytes a `SOC_ADC_DIGI_DATA_BYTES_PER_CONV`; núcleo del timer del DAC ≠ núcleo de la ISR del ADC; prioridad del timer del DAC ∈ 1..3.

### Referencia de configuración en tiempo de compilación

Todo en `components/esp32idf_radioamateur_modem/include/esp32idf_radioamateur_modem_config.h`, con cada macro protegida con `#ifndef` para que el sistema de compilación pueda sobreescribirla.

| Macro | Por defecto | Significado |
|---|---|---|
| `MODEM_DAC_GPIO` | 25 | salida de audio; solo 25 o 26 |
| `MODEM_ADC_GPIO` | 33 | entrada de audio; solo 32–39 |
| `MODEM_PTT_GPIO` | −1 | default de reserva del PTT (sobreescrito en runtime) |
| `MODEM_PTT_ACTIVE_HIGH` | 1 | polaridad de reserva |
| `MODEM_LED_TX_GPIO` / `MODEM_LED_RX_GPIO` | −1 | LEDs de estado |
| `MODEM_DAC_SAMPLERATE` | 38400 | = 32 × 1200 |
| `MODEM_ADC_SAMPLERATE` | 76800 | = 8 × 9600 |
| `MODEM_ADC_RATE_NUM` / `_DEN` | 1 / 1 | factor de corrección sobre la tasa de ADC solicitada |
| `MODEM_DAC_AMPLITUDE_PCT` | 60 | excursión del DAC, % de 0–3,3 V |
| `MODEM_ADC_ATTEN` | `ADC_ATTEN_DB_12` | ventana de ≈0–3,1 V |
| `MODEM_RX_FIFO_SIZE` | 4096 | muestras, potencia de dos |
| `MODEM_ADC_CONV_FRAME` | 128 | muestras por trama DMA |
| `MODEM_ADC_POOL_FRAMES` | 32 | profundidad del pool del driver (= 53 ms) |
| `MODEM_RX_TASK_PRIO` / `_STACK` / `_CORE` | 10 / 4096 / 0 | tarea DSP de RX |
| `MODEM_ADC_ISR_CORE` | 0 | núcleo de la ISR de DMA del ADC |
| `MODEM_DAC_TIMER_CORE` | 1 | **debe diferir del núcleo de la ISR del ADC** |
| `MODEM_DAC_TIMER_INTR_PRIO` | 3 | 1..3 |
| *(derivado)* `MODEM_DEMOD_SAMPLERATE` | 9600 | fijo |
| *(derivado)* `MODEM_RESAMPLE_RATIO` | 8 | ADC ÷ demodulación |
| *(derivado)* `MODEM_BLOCK_SIZE` | 1536 | 20 ms a 76,8 kHz |

### Configuración en tiempo de ejecución (`modem_config_t`)

Se construye en un único lugar — `aprs_service_build_modem_config()` — compartido por el arranque, el Guardar de la página Radio (reaplicación en vivo, sin reinicio) y el loop test:

| Campo | Origen | Notas |
|---|---|---|
| `modem` | `afsk_modem_type` | cast directo; la página lo acota a 0–3 |
| `flat_audio` | `audio_lpf` | pese al nombre, siempre fue el flag de entrada de audio plano |
| `full_duplex` | `false` normalmente | el LOOP TEST pasa `true` — un cable DAC→ADC hace que CSMA nunca vea un canal libre |
| `allow_non_aprs` | `false` | ¿aceptar Control/PID distintos de `0x03`/`0xF0`? |
| `preamble_ms` | `preamble` (300) | TXDelay |
| `slot_time_ms` | `tx_timeslot` (2000) | tiempo de silencio de CSMA |
| `fx25_mode` | `fx25_mode` | 0=off, 1=solo RX, 2=RX+TX |
| `ptt_gpio` | `rf_ptt_gpio` | vía `afsk_ptt_gpio_is_valid()`, si no −1 |
| `ptt_active_high` | `rf_ptt_active` | |

**Explícitamente *no* mapeados en runtime** (sin equivalente en el nuevo componente): pines y atenuación de ADC/DAC (tiempo de compilación), squelch por hardware (`rf_sql_*` — la RX se controla con el DCD real), conmutador de potencia de RF (`rf_pwr_*`), squelch por software (`sql_level`), volumen de RX, techo de AGC (`agc_max_gain` — el AGC se autolimita).

**Superficie de la API pública** (`esp32idf_radioamateur_modem.h`): `modem_init`, `modem_deinit`, `modem_set_modem`, `modem_set_rx_callback`, `modem_send_raw`, `modem_build_frame_tnc2`, `modem_send_tnc2`, `modem_format_tnc2`, `modem_tx_busy`, `modem_measure_adc_rate`, más una capa de conveniencia al estilo LibAPRS (`APRS_setCallsign`, `APRS_setPath1/2`, `APRS_setSymbol`, `APRS_setPower/Height/Gain/Directivity`, `APRS_sendLoc`, `APRS_sendMsg`, `APRS_sendPkt`, `APRS_printSettings`).

---

## Componentes de la aplicación

### `igate` — pasarela APRS-IS

* **Cliente TCP** sobre sockets LWIP con reconexión automática; relee `g_config` en cada reconexión, así que la mayoría de los cambios del panel web se aplican tras el siguiente ciclo de reconexión, sin reiniciar.
* **Condicionado a conectividad real**, no a "el Wi-Fi está arriba": consulta `net_state_is_connected()`, que solo pasa a true con `IP_EVENT_STA_GOT_IP` y vuelve a false ante una desconexión o en modo solo-AP.
* **Línea de login:** `user <mycall> pass <passcode> vers ESP32APRS 1.0 filter <filter>` — se loguea textual para que un filtro mal formado sea visible. El banner del servidor y la línea `# logresp … verified/unverified` se muestran; `unverified` levanta una advertencia nombrando `aprs_mycall`/`aprs_passcode`.
* **RF→INET** (`igateProcess()`): descarta tramas cuyo path contenga `RFONLY`, `TCPIP`, `qA*` o `NOGATE`; aplica la máscara `rf2inetFilter` (mensaje/estado/telemetría/meteorología/objeto/ítem/consulta/boya/posición); construye una cabecera `,qAR,<mycall>-<ssid>` — o la forma de objeto/gate satelital `,<mycall>-<ssid>*,qAO,<object>`; deduplica contra una caché de 10 entradas / 30 s.
* **INET→RF**: cada línea que no empiece con `#` incrementa `isRxCount`, se pasa a `handleIncomingAPRS()` si la mensajería está activa, y se retransmite si `inet2rf` está puesto **y** el bit del tipo de paquete está en `inet2rfFilter`. El tipo lo decide `aprs_filter_classify_tnc2()` (`main/aprs_filter.c`) a partir del identificador de tipo de dato APRS y, para posición/objeto/item, del símbolo (`_` → meteorología, `/N` → boya). Los paquetes que no se pueden clasificar —sobre todo el tráfico de terceros `}`, origen clásico de los bucles de IGate— clasifican como 0 y nunca se retransmiten. Las líneas filtradas se registran solo con `ESP_LOGD`; la entrada `RX-IS` del log de tráfico ya las muestra.
* **Enlace compartido:** la tarea siempre corre, porque el socket también lo usa el componente de mensajería (`igate_send_raw()`) y el "beacon a internet". Queda ociosa a bajo costo cuando nadie la necesita.
* **Contadores** (`igate_stats_t`): `rxCount`, `txCount`, `dropCount`, `dupCount`, `isRxCount` (todas las líneas de APRS-IS), `isTxCount` (todas las escrituras al socket).

### `digirepeater` — digipeater

`digiProcess(ax25_msg_t*)` reescribe el path **en el lugar** y devuelve:

| Retorno | Significado |
|---|---|
| `0` | no repetir (descartar / no es para nosotros / ya relevado) |
| `1` | repetir tal cual (el path ya lleva nuestro indicativo usado, p. ej. bypass `*`) |
| `2` | repetir con el path modificado — quien llama recodifica y transmite |

Maneja **WIDEn-N**, **TRACEn-N**, **RELAY / GATE / ECHO**, y WIDEn-N codificado en el campo SSID de destino. Contadores: `rxPkts`, `txPkts`, `dropRx`, `erPkts` (malformadas: demasiado cortas / sin path). El indicativo/SSID salen de `g_config.digi_mycall` / `digi_ssid`.

### `beacon` — beacons de la propia estación

Tres **tareas FreeRTOS independientes** (`trk_beacon_task`, `igate_beacon_task`, `digi_beacon_task`), cada una con su propio flag de habilitación, intervalo, coordenadas, símbolo, comentario y ruteo `loc2rf`/`loc2inet`. Cada una queda ociosa y revisa periódicamente cuando está deshabilitada, de modo que activarlas desde el panel web surte efecto **sin reiniciar**. Las marcas de tiempo son zulú/UTC (`051200z`) según la especificación APRS — que es por lo que `time_sync.c` fija el reloj del sistema en `TZ=UTC0` independientemente de `g_config.timeZone`.

Esto es lo que hace que la estación aparezca en aprs.fi: el IGate y el digipeater por sí solos solo relevan tráfico que escuchan; nunca se anuncian a sí mismos.

**No implementado:** GPS/posición en vivo y SmartBeaconing. Son beacons de estación fija a partir de las coordenadas guardadas en cada página.

### `message` — mensajería APRS

* Cola en RAM de 20 entradas (`MSG_QUEUE_SIZE`), máximo 200 caracteres de texto.
* `sendAPRSMessage()`, `sendAPRSAck()`, `sendAPRSMessageRetry()` (con tick a 1 Hz desde `aprs_svc_tick`), `handleIncomingAPRS()` parsea cualquier línea TNC2 venga de RF *o* de APRS-IS.
* **Cifrado:** AES-128-CBC opcional (`mbedtls`) con el IV derivado por MD5 del indicativo + ID de mensaje, payload codificado en base64. La clave es `g_config.msg_key`.
* Ruteo por máscara de canal: `MSG_CHANNEL_RF (1<<0)` → `aprs_service_send_tnc2()`, `MSG_CHANNEL_INET (1<<1)` → `igate_send_raw()`.

### `lastheard` / `trafficlog` — alimentación del dashboard

* **`lastheard`** — anillo de estaciones escuchadas con un contador de paquetes por indicativo, marcas de tiempo de reloj de pared (una vez que NTP sincronizó), y tabla/código de símbolo APRS parseados de los reportes de posición `!`/`=` (las formas con marca de tiempo `/` y `@` se dejan sin parsear — el ícono queda en blanco). Se alimenta **tanto** de RF como de APRS-IS, así hay algo que mirar mientras se verifica el enlace antes de que la radio decodifique nada. JSON: `[{"time":"HH:MM:SS","call":"…","path":"RF: WIDE1-1","sym":"91-1","packets":3}, …]`.
* **`trafficlog`** — anillo de líneas de tráfico con marcas de `esp_timer` y un **número de secuencia** siempre creciente, para que el navegador consulte por long-poll solo lo que no vio: `GET /igate_traffic?since=<seq>`. Las entradas estructuradas llevan `dir` (`RX`/`TX`/`DIGI`/`INET2RF`/`RX-IS`), `dx`, el `pkt` crudo y `au` (nivel de audio en mV RMS, o −1).

Ambos son thread-safe y se pueden llamar desde cualquier tarea.

### `webconfig` — administración web

`esp_http_server`, 64 handlers de URI, coincidencia de URI con comodines, 8 KB de stack por handler, purga LRU. **Autenticación HTTP Basic** contra `g_config.http_username` / `http_password` en cada página. El HTML se emite mediante pequeños helpers por campo (`web_field_text`, `web_field_int`, `web_field_checkbox`, `web_select_*`, `web_field_symbol`, …) en lugar de un `snprintf` gigante — deliberadamente, para evitar `-Werror=format-truncation`.

---

## Compilación y grabación

### Prerrequisitos

* **ESP-IDF v5.1 o superior** (fijado/probado en **5.5.4** — ver `dependencies.lock`).
* Un ESP32 con **≥ 4 MB de flash**.
* El gestor de componentes del IDF trae **`joltwallet/littlefs ^1.14`** automáticamente (fijado en 1.22.1).

### Compilar

```bash
. $IDF_PATH/export.sh

cd workspace-APRS/esp32_APRS_igate

idf.py set-target esp32          # el sdkconfig ya viene con target=esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Compilar en español en lugar de inglés:

```bash
idf.py build -DLANGUAGE=LANG_ES
```

> `espressif/esp-dsp` **no** es una dependencia, a propósito: solo lo arrastraba el viejo componente `esp32_IDF_libAPRS`. El módem actual implementa sus propios filtros y nada en el proyecto llama a `dsps_*`. Si venís de un checkout más viejo, borrá `dependencies.lock` y dejá que `idf.py` lo regenere.

### Tabla de particiones (`partitions.csv`)

| Nombre | Tipo | SubTipo | Offset | Tamaño |
|---|---|---|---|---|
| `nvs` | data | nvs | 0x9000 | 24 K |
| `phy_init` | data | phy | 0xF000 | 4 K |
| `factory` | app | factory | 0x10000 | **1500 K** |
| `storage` | data | spiffs | (auto) | **400 K** → montada como **LittleFS** en `/storage` |

Una sola ranura de aplicación `factory` → **sin OTA**. El `sdkconfig` viene con `CONFIG_COMPILER_OPTIMIZATION_DEBUG=y` (`-Og`) y las aserciones activadas; pasá a `-Os` si andás justo de flash.

---

## Primer arranque y configuración

1. Con una partición nueva, LittleFS se autoformatea y `app_config_load()` escribe `/storage/config.json` con los valores de fábrica.
2. El ESP32 levanta como **AP Wi-Fi**:
   * SSID **`esp32idf_APRS`**, contraseña **`esp32idf_APRS`**, canal 1, máximo 4 clientes, WPA2-PSK.
3. Conectate y navegá al dispositivo (por defecto `http://192.168.4.1/`).
4. **Iniciá sesión: `admin` / `admin`** — cambialo en la página *System*.
5. En *Wireless*: elegí **Station** o **AP+STA**, tildá **Enable** en un bloque de WiFi Client, poné SSID/contraseña, Guardar.
6. En *IGate*: poné tu **indicativo**, **SSID**, **passcode**, **host**/**puerto** de APRS-IS, filtro, coordenadas, símbolo, comentario.
7. En *Radio / Modem*: habilitá el módem de audio, elegí la modulación, el pin y polaridad de PTT, el preámbulo y el time slot de TX.
8. Reiniciá (o Guardar — casi todo se reaplica en vivo).

### Valores de fábrica destacados

| Ajuste | Por defecto |
|---|---|
| Modo Wi-Fi | AP (`2`) — siempre alcanzable |
| SSID / contraseña del AP | `esp32idf_APRS` / `esp32idf_APRS` |
| Login web | `admin` / `admin` |
| Hostname | `ESP32APRS` |
| Frecuencia de CPU | 160 MHz |
| Zona horaria | 7.0 (el reloj en sí siempre es UTC) |
| Hosts NTP | `pool.ntp.org`, `time.google.com`, `time.cloudflare.com` |
| IGate | **habilitado**, `rf2inet` on, `inet2rf` off |
| Indicativo / SSID | `NOCALL` / 10, passcode `-1` |
| Host / puerto APRS-IS | `aprs.dprns.com` : 14580 |
| Posición del IGate | 13.7563 / 100.5018 (Bangkok), intervalo 30 |
| Símbolo | `N&` |
| Preset de path 0 | `WIDE1-1,WIDE2-1` |
| Digipeater | deshabilitado, SSID 1 |
| Tracker | deshabilitado, SSID 9 |
| Módem de audio | **habilitado**, 1200 Bd Bell 202 |
| Preámbulo / slot de TX | 300 ms / 2000 ms |
| FX.25 | off |
| PTT | GPIO26, activo bajo |
| Mensajería | habilitada, RF + INET, sin cifrado |

> 🔴 **Cambiá `NOCALL` y poné un passcode real antes de transmitir.** Verificá también que estás licenciado para la frecuencia y el ciclo de trabajo con el que vas a salir al aire.

---

## Referencia de la administración web

### Rutas HTTP

| Método | Ruta | Propósito |
|---|---|---|
| GET | `/` | raíz / aterrizaje de login |
| GET | `/logout` | cerrar la sesión Basic |
| GET | `/dashboard` | dashboard en vivo |
| GET | `/style.css` | hoja de estilos compartida |
| GET | `/sidebarInfo` | fragmento de estadísticas de la barra lateral |
| GET | `/sysinfo` | información del sistema |
| GET | `/dashinfo` | franja compacta de info en vivo (JSON) |
| GET | `/lastheard` | tabla LAST HEARD (JSON) |
| GET | `/igate_traffic?since=<seq>` | delta del log de tráfico (JSON) |
| GET/POST | `/wireless` | modo Wi-Fi, AP, 5 slots STA, potencia de TX |
| GET | `/wifiscan` | resultados del escaneo de APs (JSON) |
| GET/POST | `/system` | login, hostname, frecuencia de CPU, NTP, presets de path, timeout de reset |
| POST | `/default` | reset de fábrica |
| GET/POST | `/igate` | ajustes del IGate |
| GET/POST | `/digi` | ajustes del digipeater |
| GET/POST | `/tracker` | ajustes del tracker |
| GET/POST | `/wx` | meteorología (andamiaje) |
| GET/POST | `/tlm` | telemetría (andamiaje) |
| GET/POST | `/sensor` | sensores (andamiaje) |
| GET/POST | `/radio` | módulo RF + módem AFSK de audio |
| GET | `/radio/looptest` | ejecutar el loop test (resultado JSON) |
| GET/POST | `/vpn` | WireGuard (andamiaje) |
| GET/POST | `/mqtt` | MQTT (andamiaje) |
| GET/POST | `/msg` | mensajería |
| GET/POST | `/gnss` | GNSS (andamiaje) |
| GET/POST | `/mod` | mapeo de GPIO / hardware |
| GET | `/symbol` | referencia/selector de símbolos APRS |
| GET | `/test` | resumen de autodiagnóstico de configuración |
| GET | `/storage` | explorador de archivos |
| GET | `/download?file=…` | descargar desde LittleFS |
| GET | `/delete?file=…` | borrar un archivo |
| POST | `/upload` | subida multipart |
| POST | `/format` | reformatear LittleFS |
| GET | `/about` | versión de firmware/IDF, partición, nota sobre OTA |

### Página por página

**Dashboard** — píldoras de Network Status (Wi-Fi, APRS-IS vía `igate_is_connected()`), panel de STATISTICS, tabla LAST HEARD con íconos de símbolo, y una tabla de tráfico en vivo (columnas DX / PACKET / AUDIO) alimentada por long-poll basado en secuencia.

Las estadísticas vienen de `aprs_service_get_stats()`, contabilizadas **con independencia** de `igate_en`/`digi_en`:

| Contador | Significado |
|---|---|
| `radio_rx` | cada trama que el módem decodificó de RF |
| `radio_tx` | cada trama transmitida con éxito por RF |
| `rf2inet` | tramas que el IGate realmente subió |
| `inet2rf` | líneas de APRS-IS realmente transmitidas por RF |
| `digi` | tramas digipiteadas (path reescrito + retransmitido) |

> Esto es deliberado. Los contadores antes se improvisaban a partir de `digi_get_stats()`/`igate_get_stats()`, que solo se mueven desde dentro de `digiProcess()`/`igateProcess()` — así que con ambas funciones apagadas (un montaje solo-RX/monitor muy común) el dashboard se quedaba en cero para siempre por más tráfico que se decodificara.

**Radio / Modem** — *Protocol*: interruptor de FX.25. *RF module* (solo con `ENABLE_RF_MODULE`): tipo SX127x/SX126x/SX128x, LoRa/G3RUH/GFSK/D-PRS, MHz de RX/TX, CTCSS/DCS. *Audio / AFSK*: habilitar, modulación (300 / 1200 Bell202 / 1200 V.23 / 9600 G3RUH), **desplegable de GPIO de PTT** (solo se ofrecen pines válidos, más *Disabled*), PTT activo alto, audio LPF (audio plano), preámbulo en ms, time slot de TX en ms, y el botón **LOOP TEST**. Guardar reaplica el módem en vivo vía `aprs_service_apply_modem_config()` — sin reinicio.

**IGate** — habilitar, RF→INET / INET→RF, ambas máscaras de filtro, indicativo/SSID/passcode, host/puerto, cadena de filtro del servidor, beacon on/off, lat/lon/alt, intervalo, selector de símbolo, objeto, comentario, estado, PHG (calculado del lado del cliente a partir de potencia/ganancia/altura/dirección, persistido para que el formulario lo vuelva a mostrar).

**Wireless** — modo (off/STA/AP/AP+STA), SSID/contraseña/canal del AP, 5 slots STA cada uno con su propia casilla **Enable**, potencia de TX en dBm (convertida ×4 a cuartos de dBm para `esp_wifi_set_max_tx_power()`), más un escaneo en vivo. El escaneo pasa temporalmente una radio solo-AP a AP+STA — que es por lo que `s_staEnabled` condiciona cada `esp_wifi_connect()` automático, para que el manejador de eventos no pelee con el escaneo.

**System** — login web, hostname, frecuencia de CPU (aplicada en vivo), 3 hosts NTP, intervalo de resincronización, timeout de reset, y los **cuatro presets de path** `path[0..3]`.

**Storage** — explorador de LittleFS: descargar, borrar, subida multipart, uso, formatear.

**About** — nombre del proyecto, versión, fecha/hora de compilación, versión del IDF, etiqueta/offset/tamaño de la partición en ejecución, y una nota explícita de que OTA no está disponible con esta tabla de particiones.

---

## Almacenamiento de configuración (`config.json`)

* Ruta: **`/storage/config.json`** en LittleFS.
* Serializado con **cJSON**; los nombres de campo y claves JSON se mantienen **1:1 con los `config.h`/`config.cpp` originales**, así cada valor que muestra el panel web tiene su lugar y los archivos viejos cargan sin cambios.
* **Guardado atómico**: escribe `/storage/config.json.tmp` y luego renombra.
* Si falta o está corrupto → se aplican los valores por defecto **y se guardan inmediatamente**, de modo que el archivo siempre existe y es consistente.
* API: `app_config_set_defaults()`, `app_config_load()`, `app_config_save()`, `app_config_factory_reset()`. Instancia viva: `extern app_config_t g_config`.

### Interruptores de módulo en tiempo de compilación (`main/include/app_config.h`)

```c
#define ENABLE_DASHBOARD          #define ENABLE_IGATE
#define ENABLE_RADIO_MODEM        #define ENABLE_DIGIPEATER
//#define ENABLE_RF_MODULE        #define ENABLE_TRACKER
//#define ENABLE_VPN              //#define ENABLE_WEATHER
//#define ENABLE_MQTT             //#define ENABLE_TELEMETRY
#define ENABLE_MESSAGE            //#define ENABLE_SENSORS
//#define ENABLE_MOD_GPIO         #define ENABLE_SYSTEM
#define ENABLE_WIRELESS           //#define ENABLE_GNSS
#define ENABLE_FILE_STORAGE       #define ENABLE_ABOUT_FIRMWARE
```

Comentar uno elimina su entrada de la barra lateral y su página de la imagen.

---

## Presets de path y la máscara de bits de path

Cada servicio (tracker / igate / digi / wx / …) guarda una **máscara de bits**, no una cadena de path. El bit *N* selecciona **`g_config.path[N]`**, el preset de texto libre que se edita en la página *System*. `buildPathSuffix()` concatena cada slot seleccionado que no esté vacío; los slots seleccionados pero vacíos simplemente se saltean.

Los flags de activación sirven también como valores por defecto de la máscara:

```
ACTIVATE_OFF 0 · TRACKER 1<<0 · IGATE 1<<1 · DIGI 1<<2 · WX 1<<3
ACTIVATE_TELEMETRY 1<<4 · QUERY 1<<5 · STATUS 1<<6 · WIFI 1<<7
```

Bits de filtro del IGate (compartidos por `rf2inetFilter` e `inet2rfFilter`):

```
MESSAGE 1<<0 · STATUS 1<<1 · TELEMETRY 1<<2 · WEATHER 1<<3 · OBJECT 1<<4
ITEM 1<<5 · QUERY 1<<6 · BUOY 1<<7 · POSITION 1<<8
```

---

## El LOOP TEST

La herramienta de puesta en marcha más útil del proyecto. Cableá **GPIO25 → GPIO33**, abrí *Radio / Modem* y apretá **LOOP TEST**.

Lo que hace (`aprs_loop_test_run()`):

1. Arma un paquete APRS chico que lleva un **token aleatorio de un solo uso** (`>LOOPTEST <token>`).
2. **Desvía** las tramas decodificadas a su propio hook (`s_rxHook`) para que la trama de prueba nunca sea digipiteada, subida a APRS-IS ni registrada como tráfico real.
3. Pasa el módem a **full duplex** — un cable DAC→ADC hace que el nodo siempre escuche su propia portadora y CSMA nunca activaría el PTT.
4. Transmite y espera hasta **4000 ms** a que la cadena ADC → demodulador → HDLC → AX.25 devuelva la misma trama.
5. **Siempre restaura** el hook real y el modo dúplex configurado antes de retornar.

Mientras tanto, una tarea monitor engancha diagnósticos que el componente solo expone de forma instantánea: una instantánea pasiva del ADC crudo a mitad del preámbulo (`afskDiagCaptureRaw()`, tomada directamente de la ISR de conversión, sin perturbar la tarea de RX en vivo), el pico de RMS (`afskGetRms()`), el pico de ganancia del AGC (`afskGetAgcGain()`), un mapa de bits de DCD (`ModemDcdState()`) y la etapa de RX HDLC más avanzada alcanzada por cada demodulador (`Ax25GetRxStage()`).

Los mensajes de falla entonces distinguen, del mismo modo que lo hacían los diagnósticos con enganche del componente viejo:

| Síntoma | Diagnóstico |
|---|---|
| min≈max del ADC crudo | ADC muerto / sin cablear |
| el crudo oscila, RMS ~0 | no llega tono al ADC |
| RMS bien, DCD nunca se activa | hay tono, el PLL nunca enganchó → desajuste de baudios/tipo de módem o audio malo |
| DCD enganchado, `rxStageMax < RX_STAGE_FRAME` | se vieron flags pero nunca arrancó una trama — bug de recuperación de bits, no ruido |
| DCD enganchado, `rxStageMax = FRAME`, sin trama | se ensamblaron tramas pero todas fallaron el CRC — nivel/SNR marginal |
| vuelve una trama, token distinto | distorsión, recorte, o loopback mal cableado |
| PASS | informa el nivel de RX en mV RMS |

Dos distinciones que hacían los diagnósticos viejos **ya no existen, por diseño**: no hay squelch por software (así que no hay "el squelch nunca abrió"), y no hay contadores de fallas de CRC (la etapa HDLC más avanzada los reemplaza).

---

## Localización

**Un idioma por imagen de firmware.** Sin conmutador en runtime; las cadenas de ningún otro idioma se compilan.

* `app_config.h` define `LANG_EN 0`, `LANG_ES 1` y el `LANGUAGE` activo (por defecto `LANG_EN`).
* `translations/translations.h` es el *único* lugar que decide qué `lang_xx.h` se incluye.
* Cada cadena visible por el usuario pasa por una macro `TR_xxx`.

**Agregar un idioma:**

1. Copiá `translations/lang_en.h` → `lang_xx.h`, traducí cada literal, mantené idéntico cada nombre de macro.
2. `#define LANG_XX <siguiente número libre>` en `app_config.h`.
3. Agregá una rama `#elif LANGUAGE == LANG_XX` en `translations.h`.
4. Compilá con `-DLANGUAGE=LANG_XX`.

Que falte un `TR_xxx` en un idioma es un **error de compilación en la build de ese idioma** — intencional, para que no se cuelen cadenas sin traducir.

---

## Resolución de problemas

**"Cambié a modo Station, guardé, reinicié, y no pasa nada."**
Leé el log de arranque — este camino está instrumentado a fondo a propósito:

* `esp_wifi_connect()` solo es legal una vez que la estación *realmente* arrancó, lo cual el driver señaliza con `WIFI_EVENT_STA_START`. Llamarlo justo después de `esp_wifi_start()` pierde esa carrera y devuelve `ESP_ERR_WIFI_NOT_STARTED`; sin asociación, sin `STA_DISCONNECTED`, por lo tanto sin reintento. La conexión se lanza **desde el manejador de STA_START** y cada intento loguea su resultado.
* Si ningún slot de WiFi Client está **habilitado con un SSID**, el firmware vuelca **todos los slots** y te dice cuál es el error ("enabled, but the SSID is EMPTY" vs "has an SSID, but 'Enable' is not ticked").
* Solo-STA sin nada a lo que unirse dejaría el dispositivo inalcanzable, así que **cae a AP+STA** y lo avisa — el panel web sigue arriba.

Los **códigos de razón de desconexión** se loguean (antes se descartaban):

| Razón | Significado |
|---|---|
| 15 (`4WAY_HANDSHAKE_TIMEOUT`), 204 (`NOT_AUTHED`) | contraseña incorrecta |
| 201 (`NO_AP_FOUND`) | SSID no visible: nombre equivocado, fuera de alcance, o solo 5 GHz |
| 2 / 8 / 200 | roaming ordinario / caídas del lado del AP |

Las reconexiones usan un **back-off creciente** (500 ms por falla consecutiva, con techo de 8 s), armado con un `esp_timer` — **no** con `vTaskDelay()` dentro del manejador de eventos, lo cual estancaría el bucle de eventos compartido (incluyendo el propio `IP_EVENT_STA_GOT_IP` que está esperando) y, en un bucle cerrado de desconexiones, mataría de hambre a la tarea idle hasta que saltara el watchdog de tareas.

**"El AP no asocia para nada"** — un `wifi_config_t` en cero deja `pmf_cfg.capable = false`, y los APs WPA3 / WPA2-con-PMF-requerido simplemente rechazan una estación así. El firmware pone *capable, not required*, que funciona tanto con APs viejos como nuevos.

**"El arranque se cuelga ~5 segundos"** — esperado: `modem_init()` bloquea mientras `ModemCalibrateSampleRate()` mide el reloj real del ADC. Una vez por arranque.

**"Los beacons del arranque no transmiten"** — esperado: `aprs_service_start()` corre antes de `modem_init()`, así que los beacons tempranos se descartan con un log de depuración hasta que `s_modemReady`.

**"El LOOP TEST falla con 'no packet received back'"** — revisá la historia de la atenuación del ADC: el DAC recorre todo el riel mientras una atenuación de 0 dB solo mide ~0–1,1 V, recortando el tono más allá de lo que el demodulador puede enganchar. El componente fija `ADC_ATTEN_DB_12`, que es lo correcto; si lo sobreescribiste, volvelo atrás.

**"El IGate dice unverified"** — `aprs_mycall` / `aprs_passcode` incorrectos. El banner se loguea; también la línea exacta de login, incluida la cadena de filtro, así que un filtro mal formado se ve enseguida.

**"Todo funciona pero aprs.fi no muestra mi estación"** — beacons: habilitá `igate_bcn` y al menos uno de `igate_loc2rf` / `igate_loc2inet`, y poné coordenadas reales. Relevar tráfico nunca te anuncia a vos.

**"9600 Bd pierde tramas"** — esa es la patología que motivó los cambios de tasa del ADC, tamaño de trama de conversión y separación de núcleos. Si sobreescribiste `MODEM_ADC_SAMPLERATE`, `MODEM_ADC_CONV_FRAME`, `MODEM_DAC_TIMER_CORE` o `MODEM_ADC_ISR_CORE`, releé [Por qué los números son los que son](#por-qué-los-números-son-los-que-son). Confirmá además que estás alimentando **audio plano/de discriminador**.

---

## Estado y limitaciones conocidas

* **Trabajo en curso.** Lo dice el README original, y lo dice también este.
* **Sin OTA** — una sola partición `factory`; se graba por serie.
* **`rf2inetFilter` no se aplica.** `igateProcess()` sigue aplicando solo las reglas RFONLY/TCPIP/qA/NOGATE/satélite y el antiduplicados, no la máscara de tipos de paquete. `aprs_filter_classify_tnc2()` / `aprs_filter_pass()` no dependen de la dirección, así que engancharlo ahí son dos líneas.
* **Solo se usa el primer slot STA habilitado.** El failover multi-AP figura como "se puede agregar más adelante".
* **Sin GPS, sin SmartBeaconing.** Los campos de configuración existen; los beacons son solo de posición fija.
* **Sin driver de LoRa / módulo RF.** `ENABLE_RF_MODULE` está comentado; la UI y la configuración de SX12xx son andamiaje.
* **VPN / MQTT / GNSS / meteorología / telemetría / sensores / Bluetooth / PPP / OLED / Modbus**: existen campos de configuración y (algunas) páginas, sin implementación.
* **KISS se compila pero es inalcanzable** — no hay front-end de TNC KISS por serie/TCP en esta app.
* **El parseo de símbolos** solo cubre los formatos de posición sin marca de tiempo `!` / `=`; `/` y `@` dejan el ícono en blanco.
* **Archivo duplicado:** `components/webconfig/page_symbol.c` existe junto a `components/webconfig/pages/page_symbol.c`, pero solo el segundo está en `CMakeLists.txt`. El primero es peso muerto y además difiere del que está en uso — conviene borrarlo.
* **`agc_max_gain`, `sql_level`, `volume`, `adc_gpio`, `dac_gpio`, `rf_sql_*`, `rf_pwr_*`, `adc_atten`** están inertes desde el cambio de módem; se conservan solo por compatibilidad de `config.json`.
* El `sdkconfig` viene con `-Og` + aserciones, no con un perfil de release.

---

## Notas de portabilidad

Migrar del viejo **`esp32_IDF_libAPRS`** al **`esp32idf_radioamateur_modem`** cambió varios contratos. Si estás trasladando parches:

| Antes | Ahora |
|---|---|
| Pines de ADC/DAC/PTT en `aprs_modem_config_t` en **runtime** desde `g_config` | **Tiempo de compilación** `MODEM_*_GPIO` vía `idf_build_set_property()` en el `CMakeLists.txt` de nivel superior (el PTT es la excepción: volvió a runtime, validado) |
| La app bombeaba `AFSK_Poll()` / `APRS_poll()` desde su propia tarea | El componente es dueño de ambos: `AFSK_init()` arranca una tarea DSP de RX fijada a un núcleo; `modem_init()` arranca `modem_svc`. Llamar `AFSK_Poll()` vos mismo ahora **compite** con esa tarea por el mismo FIFO. |
| El componente decodificaba tramas y llamaba a un `ax25_callback_t _hook` global con un `AX25Msg` ya armado | El componente devuelve **bytes AX.25 crudos**; la app hace `ax25_decode()` en `on_rx_frame()` y despacha por su propia indirección `s_rxHook` |
| `APRS_sendTNC2Pkt(raw, len)` | `modem_send_tnc2(const char*)` — terminado en NUL; `aprs_service_send_tnc2()` hace la conversión de puntero+longitud y la verificación de `AX25_FRAME_MAX_SIZE` de forma centralizada |
| `ax25ToTnc2()` local | envoltorio fino sobre `modem_format_tnc2()` para que las dos representaciones no puedan divergir |
| Diagnósticos con enganche (`AFSK_getAdcDiag`, `AFSK_getSquelchDiag`, `Ax25GetFrameDiag`, …) | Getters instantáneos (`afskGetRms`, `afskGetAgcGain`, `afskGetDcOffset`, `ModemDcdState`, `Ax25GetRxStage`, `ModemGetSignalLevel`) + una toma pasiva (`afskDiagCaptureRaw`); el enganche ahora lo hace la tarea monitor del loop test de la app |
| Dependencia de `espressif/esp-dsp` | eliminada — el módem implementa sus propios filtros |
| Squelch por software, volumen de RX, techo de AGC, conmutador de potencia de RF | **desaparecidos**, sin equivalente. La RX se controla con el DCD real del demodulador. |
| `MODEM_DEFAULT_CONFIG()` viene con `full_duplex = true` | apunta a la demo de loopback por cable; **el uso real al aire debe poner `full_duplex = false`** o transmitirá encima de alguien que ya esté transmitiendo |

---

## Créditos

* **Este proyecto y el componente módem:** Emiliano Augusto González — **LU3VEA** — `lu3vea @ gmail . com` · <https://github.com/hiperiondev>
* El módem se basa en, y debe su linaje DSP a:
  * **VP-Digi** — SQ8VPS — <https://github.com/sq8vps/vp-digi>
  * **ESP32APRS_Audio** — nakhonthai — <https://github.com/nakhonthai/ESP32APRS_Audio>
  * **LibAPRS** — Mark Qvist — <https://github.com/markqvist/LibAPRS>

  Por favor contactá a sus autores para información sobre esos proyectos.
* **littlefs** — ARM/joltwallet (BSD-3-Clause), vía el registro de componentes de ESP.
* El esquema de configuración, la disposición del panel web y la semántica del dashboard siguen al proyecto de referencia **esp32idf_APRS / ESP32APRS**, para que los `config.json` existentes y las expectativas de los usuarios se trasladen sin fricción.

---

## Licencia

**GNU General Public License v3.0** — ver [`LICENSE`](LICENSE).

El `managed_components/joltwallet__littlefs` incluido lleva su propia licencia (BSD-3-Clause para littlefs en sí).

---

### Descargo de responsabilidad de radioafición

Transmitir en frecuencias de radioafición requiere una licencia válida para tu país y banda. **Poné un indicativo real** (el valor por defecto es `NOCALL`), usá un passcode de APRS-IS legítimo, respetá tu plan de bandas local y las convenciones de digipeo (`WIDE1-1,WIDE2-1` *no* siempre es apropiado), y no gatees tráfico `NOGATE`/`RFONLY`. Sos responsable de todo lo que este dispositivo transmita.
