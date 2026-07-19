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
    - [Interfaz funcional completa para un Baofeng UV-5R](#interfaz-funcional-completa-para-un-baofeng-uv-5r)
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
- [Sensores](#sensores)
  - [Por qué un framework de drivers en vez de una lista fija](#por-qué-un-framework-de-drivers-en-vez-de-una-lista-fija)
  - [Las dos familias de datos](#las-dos-familias-de-datos)
  - [Anatomía de un driver (`sensor_local_driver_t`)](#anatomía-de-un-driver-sensor_local_driver_t)
  - [El registro: cómo se encuentra y se llama a un driver](#el-registro-cómo-se-encuentra-y-se-llama-a-un-driver)
  - [Flujo de datos de punta a punta, del sensor al APRS](#flujo-de-datos-de-punta-a-punta-del-sensor-al-aprs)
  - [Los dos drivers de ejemplo incluidos](#los-dos-drivers-de-ejemplo-incluidos)
  - [Añadir un sensor nuevo, paso a paso](#añadir-un-sensor-nuevo-paso-a-paso)
    - [1. Decida qué produce su driver](#1-decida-qué-produce-su-driver)
    - [2. Copie un esqueleto y renómbrelo](#2-copie-un-esqueleto-y-renómbrelo)
    - [3. Complete `init()`](#3-complete-init)
    - [4. Complete `save()`](#4-complete-save)
    - [5. Declare el descriptor y auto-regístrelo](#5-declare-el-descriptor-y-auto-regístrelo)
    - [6. Compile — nada más que conectar](#6-compile--nada-más-que-conectar)
    - [7. Mapéelo en la página Weather](#7-mapéelo-en-la-página-weather)
    - [8. Ejemplo trabajado: un BME280 I2C real](#8-ejemplo-trabajado-un-bme280-i2c-real)
  - [Varias instancias del mismo tipo de sensor](#varias-instancias-del-mismo-tipo-de-sensor)
  - [Manejo de errores y fallo del driver](#manejo-de-errores-y-fallo-del-driver)
  - [Seguridad entre tareas (thread safety)](#seguridad-entre-tareas-thread-safety)
  - [Añadir un tipo (kind) de sensor completamente nuevo](#añadir-un-tipo-kind-de-sensor-completamente-nuevo)
  - [La página legada `/sensor` — no es lo mismo](#la-página-legada-sensor--no-es-lo-mismo)
  - [Resumen de referencia de Sensores](#resumen-de-referencia-de-sensores)
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
| Almacenamiento LittleFS: subir/descargar/borrar/formatear | ✅ | partición de 512 KB |
| Sincronización horaria SNTP (3 hosts) | ✅ | el reloj siempre se mantiene en UTC |
| Control de frecuencia de CPU (80/160/240 MHz) | ✅ | `esp_pm_configure()` |
| Wi-Fi AP / STA / AP+STA, escaneo, potencia de TX | ✅ | 5 slots STA (se usa el primero habilitado) |
| Localización (EN / ES / IT) | ✅ | en tiempo de compilación, un idioma por imagen |
| Actualización OTA | ✅ | página web About / Firmware, ranuras `ota_0`/`ota_1`, rollback automático si falla el arranque |
| Módulo RF LoRa / SX127x-SX128x | ❌ | solo UI + configuración, `ENABLE_RF_MODULE` está comentado |
| VPN WireGuard, MQTT, GNSS | ❌ | las páginas/configuración existen; los módulos están deshabilitados en `app_config.h` |
| Informe meteorológico APRS de la propia estación | ✅ | `weather.c`, refresco de sensores a 1 Hz, promediado opcional por campo, baliza WX real en el aire (RF y/o APRS-IS) — ver [Sensores](#sensores) |
| Framework de drivers de sensores locales (`sensors_local`) | ✅ | registro dinámico en tiempo de ejecución, drivers auto-registrados, alimenta el selector de canal de la página Weather — ver [Sensores](#sensores) |
| Codificación/baliza de Telemetría APRS en el aire | 🟡 | `sensors_local` ya puede recolectar valores de canales analógicos/digitales en `weather_telemetry_data_t`; todavía no existe un codificador ni una tarea de baliza `T#nnn`, por lo que la página Telemetría es solo configuración — ver [Sensores](#sensores) |
| Página legada por-slot `/sensor` (`g_config.sensor[]`) | ❌ | campos de configuración conservados solo por compatibilidad; nada en el firmware los lee — no confundir con el framework de drivers `sensors_local`, ver [Sensores](#sensores) |
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

> ⚠️ Con los valores por defecto, **`MODEM_DAC_GPIO=25` y `MODEM_PTT_GPIO=26` están ambos en el par de pads del DAC.** Eso está bien (el 26 solo es DAC_CHAN_1 si lo *seleccionas*), pero si mueves la salida de audio al GPIO26 tienes que mover el PTT a otro lado. El validador rechazará el solapamiento en runtime.

> **Nota sobre los campos de GPIO de la página "Mod".** `g_config.adc_gpio`, `dac_gpio`, `rf_sql_gpio`, `rf_pwr_gpio`, `adc_atten`, `sql_level`, `volume`, `agc_max_gain` se siguen cargando, guardando y editando — pero **ya nada en la cadena de audio del módem los lee** desde el cambio de `esp32_IDF_libAPRS` a `esp32idf_radioamateur_modem`. Se conservan únicamente para que los `config.json` existentes hagan round-trip sin cambios. Para mover los pines de audio, edita el bloque de `CMakeLists.txt` de arriba y recompila.

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

| Ref | Valor | Función | Si lo cambias |
|---|---|---|---|
| **R1, R2 / C2, C3** | 2k2 / **15 nF** | Pasabajos de reconstrucción de dos polos, **fc ≈ 4,8 kHz**. Mata las imágenes del DAC de 38,4 kHz a **≈−36 dB** costando solo −0,3 dB a 1200 Hz y −0,8 dB a 2200 Hz (≈0,5 dB de *twist*) | **22 nF** (fc 3,3 kHz, −43 dB a 38,4 kHz) si solo vas a usar AFSK y quieres matar más las imágenes; **10 nF** (fc 7,2 kHz, −29 dB) es **obligatorio para 9600 Bd G3RUH**, que necesita respuesta plana más allá de ~5 kHz |
| **C1** | 10 µF | Bloqueo de continua. La polarización de reposo de 1,65 V del DAC nunca debe llegar a una entrada de micrófono | Contra R3+RV1 = 11 kΩ da fc ≈ 1,4 Hz — no bajes de 1 µF |
| **R3 / RV1** | 10k / 1k preset | Atenuador + nivel. El divisor fijo 11:1 hace que el preset trabaje en **0–64 mVrms** con ≈32 mV a media vuelta, en vez de vivir en el 4 % inferior de un potenciómetro de 10k pelado | La impedancia del cursor es ≤250 Ω, así que ataca cualquier entrada de micrófono o de datos sin cargarla más |
| **RV2** | 10k | Nivel de RX. **Omítelo por completo con un puerto `DATA OUT`** — ya está en el nivel correcto | |
| **C4** | 10 µF | Bloqueo de continua + pasaaltos. Contra el Thévenin de 5 kΩ de la polarización: fc ≈ 3 Hz | |
| **R5, R6** | 10k / 10k | Polarización a medio riel, **1,65 V**, justo en el centro de la ventana de 0–3,1 V del ADC. Thévenin 5 kΩ | Bajalas a **4k7/4k7** (2,35 kΩ) si ves errores de nivel — el SAR del ESP32 quiere impedancia de fuente baja |
| **R7 / C5** | 1k / 1 nF | Amortiguador para la patada de carga del capacitor de muestreo del SAR. **No** es un filtro de audio: fc ≈ 159 kHz | **Nunca** pongas 100 nF aquí, el reflejo típico de "desacoplar el ADC" — con R7 eso es un pasabajos de 1,6 kHz y se come el tono de marca de 2200 Hz |
| **D1, D2** | BAT54S | Recortan GPIO33 contra los rieles. R7 limita la corriente de falla. Seguro barato contra una perilla de volumen a 3 Vrms | |
| **R8** | 470 Ω | ≈4,5 mA por el LED del PC817, drenados por GPIO26 — bien dentro del presupuesto de 12 mA cómodos / 20 mA absolutos | |
| **R10** | 10k | **La pieza que todo el mundo omite.** Sin ella la compuerta del MOSFET queda flotante durante el reset y el equipo puede transmitir al encender | |

#### Interfaz funcional completa para un Baofeng UV-5R

El Baofeng UV-5R (y la mayoría de sus clones de dos pines estilo Kenwood-K1 — UV-82, BF-888, GT-3, RT-5R, etc.) **no** tiene un único jack combinado de mic/parlante/PTT. Tiene dos:

| Conector | Tamaño | Contactos | Señal |
|---|---|---|---|
| **Plug grande** | 3,5 mm, TS (mono) | Punta / Manga | Punta = **salida de audio de SPKR**, Manga = **GND** |
| **Plug chico** | 2,5 mm, TRS | Punta / Anillo / Manga | Punta = **entrada de MIC**, Anillo = **PTT** (corto contra la Manga para transmitir), Manga = **GND** |

Eso es toda la interfaz: el TX es una señal de nivel de micrófono hacia la punta del plug chico, el RX es una señal de nivel de parlante que sale de la punta del plug grande, y el PTT es un **cierre de contacto** entre el anillo y la manga del plug chico — no es un nivel lógico que la radio lea, es solo un corto. Esto calza exacto con el [esquemático mínimo funcional](#esquemático-mínimo-funcional) de arriba; lo único que cambia son los destinos:

```
 ── TX ── ESP32 ─────────────────────────────────────────► plug chico del UV-5R ──

  GPIO25 ──[R1 2k2]──┬──[R2 2k2]──┬──[C1 10µ]──[R3 10k]──┬─ RV1 extremo
   (DAC)             │            │      +               │
                   [C2 15n]     [C3 15n]              [RV1 1k]  ajuste de nivel
                     │            │                      ├─ cursor ──► PUNTA 2,5 mm (MIC)
                    GND          GND                     │
                                                         └─ extremo ─► MANGA 2,5 mm (GND)

 ── RX ── plug grande del UV-5R ────────────────────────────────► ESP32 ──

                                       nodo de polarización
  PUNTA 3,5mm (SPKR) ─[RV2 10k]─ cursor ─[C4 10µ]──┬──────[R7 1k]───┬──► GPIO33 (ADC)
                 │                       +         │                │
   MANGA 3,5mm ──┘                            [R5 10k]─► 3V3     [D1]─► 3V3   BAT54S
   (GND, común con la manga del plug chico)        │             [D2]─► GND   (o 2×1N4148)
                                              [R6 10k]─► GND        │
                                                   │             [C5 1n]
                                                  GND               │
                                                                   GND

 ── PTT ── cortocircuita el anillo del plug chico contra su manga — opción A, B o C, sin cambios ──

                      ┌─ PC817 ─┐
   3V3 ──[R8 470]──[A]│▶      C│──────────► ANILLO 2,5 mm
   GPIO26 ─────────[K]│        E│──────────► MANGA 2,5 mm (GND)
                      └─────────┘
   GPIO26 en BAJO → LED encendido → anillo en corto con la manga → transmite.

 ── PTT ── opción C: transistor NPN pelado, sin aislar, requiere ACTIVE_HIGH=1 ──

                       ┌─ 2N2222 / BC547 ─┐
   GPIO26 ──[R9 1k]────┤ B              C ├───► ANILLO 2,5 mm
                       │                E ├───► MANGA 2,5 mm (GND)
          [R10 10k]────┤ B→E              │
                       └──────────────────┘
   GPIO26 en ALTO → circula corriente de base → C-E conduce → anillo en corto con la manga → transmite.
   R10 mantiene la base en bajo (sin transmitir) durante el reset y el deep sleep — la misma función que cumple R10 en la opción MOSFET.
```

Todo lo que queda a la izquierda de los conectores — R1–R3, RV1, C1–C4, R5–R7, D1–D2, C5, R8 (o R9/R10 para la opción C) — es idéntico a la [tabla de componentes](#esquemático-mínimo-funcional) de arriba; lo único que se mueve son los destinos, de "MIC/DATA IN" y "SPKR/DISC del equipo" a la punta del plug chico y del plug grande del UV-5R.

La opción C cambia la aislación del opto por comodidad de cajón de repuestos: cualquier NPN de señal chica sirve (2N2222, BC547, PN2200, S8050 — con `hFE` ≥ 100 sobra, porque la corriente de colector aquí es de apenas unos mA a través de los contactos del interruptor de PTT del K-plug), y es un armado de un transistor y dos resistencias en vez de tener que conseguir un optoacoplador. Igual que la opción MOSFET, **no aísla** la masa del ESP32 de la de la radio, y es **activa en ALTO** — pon `MODEM_PTT_ACTIVE_HIGH=1` (default de compilación o el interruptor en runtime de la página **Radio**) para que coincida, exactamente igual que en la opción B.

Algunas cosas específicas de esta radio:

* **No hay DATA IN / DATA OUT.** El UV-5R no tiene jack de discriminador, así que no hay forma de llegar a la vía plana y de nivel fijo que necesita el modo 9600 Bd G3RUH de este proyecto. A través del conector estándar de 2 pines, **AFSK 1200 Bd Bell 202 es el techo realista.**
* **El nivel de micrófono cae dentro de la banda genérica "Rig MIC IN"** de la [tabla de qué hay en cada extremo](#qué-hay-realmente-en-cada-extremo) — de pocos mV a unas pocas decenas de mV — así que la red atenuadora R3/RV1 se usa tal cual está especificada; arranca con RV1 cerca de la media vuelta y ajusta para ≈3 kHz de desviación según el [orden de puesta en marcha](#orden-de-puesta-en-marcha).
* **La salida de parlante depende de la perilla de volumen.** Fija el volumen del UV-5R en un valor bajo a moderado y repetible (marca la perilla) y haz el ajuste de nivel con RV2, no con el control de volumen de la radio — el AGC tiene menos margen en los extremos de su rango.
* **No se usa VOX.** El PTT lo maneja directamente el opto, el MOSFET o el transistor, así que deja el VOX de la radio apagado; el VOX peleando contra un corto de PTT directo es una buena forma de perder los primeros caracteres o de quedarte trabado transmitiendo.
* **Elegir entre A, B o C:** la opción A (opto) es la única de las tres que aísla la masa del ESP32 de la de la radio, y no requiere cambiar la polaridad respecto del default de fábrica — es la primera a probar. Las opciones B y C son ambas interruptores de lado bajo, sin aislar y activos en ALTO, que solo difieren en qué componente tienes a mano (MOSFET o NPN de señal chica); cualquiera de las dos sirve en un banco de pruebas donde el zumbido y la entrada de RF no sean un problema.
* **Verifica el pinout antes de soldar.** No todos los cables K-plug de dos pines genéricos están cableados igual — algunos cables de terceros invierten cuál contacto del plug chico es mic y cuál es PTT. Mide el plug con un tester contra la tabla de arriba antes de soldar definitivamente; un par invertido deja el micrófono flotando (sin audio de TX) o cortocircuita el PTT de forma permanente (la radio transmite apenas la enchufas).
* La guía de aislación y lazos de masa de [Aislación y lazos de masa](#aislación-y-lazos-de-masa) aplica sin cambios — las mangas del plug chico y del plug grande son el mismo nodo dentro de la radio, así que trátalas como una sola referencia de masa.

#### Por qué el default del PTT es una trampa

La definición de placa que viene es `MODEM_PTT_ACTIVE_HIGH=0` — **GPIO26 se pone en BAJO para transmitir**. El circuito reflejo de "NPN con la base al GPIO y el colector al PTT" es un driver **activo en ALTO** y va a dejar el transmisor al aire todo el tiempo en que el ESP32 *no* está transmitiendo, o sea, permanentemente.

Entonces: elige un driver cuya polaridad coincida con la configuración, o cambia la configuración para que coincida con tu driver.

* La **opción A (opto)** invierte, así que coincide con el `ACTIVE_HIGH=0` de fábrica tal cual, y de paso regala aislación galvánica.
* La **opción B (MOSFET)** no invierte. Pon `MODEM_PTT_ACTIVE_HIGH=1` en el `CMakeLists.txt` de nivel superior, o cambia la polaridad en runtime desde la página **Radio** (el pin y la polaridad son ambos seleccionables en runtime — ver [Pinout / definición de placa](#pinout--definición-de-placa)).

En cualquier caso, **verifica antes de conectar la radio**: alimenta la placa y confirma con un tester que la línea de PTT queda abierta durante el reset, durante los ~5 s completos del arranque y en reposo. `modem_init()` bloquea unos 5 segundos calibrando el reloj del ADC, y las tareas de beacon transmiten apenas arrancan — un PTT con la polaridad al revés te da cinco segundos de portadora sin modular antes de que el firmware siquiera llegue al módem.

Los equipos portátiles con "K-plug" (Baofeng y compañía) son otra cosa: ahí el PTT es un interruptor entre el anillo y la manga del micrófono, normalmente a través de una resistencia, y el audio de micrófono comparte el mismo conductor. La salida del opto va en paralelo a ese interruptor, y el atenuador de micrófono ataca el mismo nodo.

#### Aislación y lazos de masa

El circuito de arriba comparte masa con la radio, que es la fuente habitual de zumbido, ruido de alternador y del clásico "funciona hasta que transmito". Si escuchas algo de eso:

* **Transformadores de aislación de audio**, 600:600 Ω (Bourns 42TL022, Tamura MET-01, o cualquier transformador 1:1 de telefonía), en lugar de C1 y C4. La red de polarización queda del lado del ESP32 del transformador de RX.
* **Mantén el opto** (opción A) para que el retorno del PTT no vuelva a crear la masa que acabas de cortar.
* La **entrada de RF** en TX aparece como PTT trabado o como un módem que solo falla a plena potencia. Cable blindado, puntas cortas, un ferrite de clip en el conector del equipo, y 47–100 pF de cada línea de audio al chasis *del equipo*, en el conector.

#### Orden de puesta en marcha

1. **Primero el loop test, sin radio.** GPIO25 → GPIO33 con un cable pelado (ver [Cableado de loopback de banco](#cableado-de-loopback-de-banco)). Si eso falla, no hay circuitería externa que lo arregle.
2. **Después RX, todavía sin TX.** Abre el squelch, metele tráfico real y mira la columna **AUDIO** de la tabla de tráfico en vivo (es el propio `mVrms` que mide el módem en el pin). Ajusta RV2 hasta **≈300 mVrms en los paquetes** — es el objetivo del AGC, así que el lazo queda en ganancia unitaria y con el máximo margen para los dos lados. Cualquier cosa entre ~50 mV y ~800 mV decodifica; por debajo de 40 mV te estás quedando sin ganancia de AGC, por encima de 1,1 Vrms estás recortando. En este firmware **no hay entrada de squelch por hardware** — el DCD sale del demodulador — así que dejar el squelch abierto es lo correcto, no un parche.
3. **TX al final, contra una carga fantasma.** Ajusta RV1 para **≈3,0 kHz de desviación** (2,5–3,5 kHz) con un medidor de desviación, o comparando con el audio de una estación conocida en un segundo receptor. La sobredesviación es la causa número uno de "mi igate escucha a todos pero nadie me escucha a mí".
4. **9600 Bd G3RUH** necesita el camino plano/de discriminador en los dos extremos: `DATA IN`/`DATA OUT`, 10 nF en C2/C3, y la casilla *Audio low-pass filter* puesta para `flat_audio`. Una salida de altavoz y una entrada de micrófono no lo van a llevar, sin importar cómo ajustes los niveles.

### Cableado de loopback de banco

Para el [LOOP TEST](#el-loop-test), simplemente cablea **`GPIO25` → `GPIO33`** (salida del DAC directo a la entrada del ADC). Sin radio, sin PTT. El test transmite una trama APRS y espera que la misma placa la decodifique de vuelta.

---

## Estructura del repositorio

```
workspace-APRS/esp32_APRS_igate/
├── CMakeLists.txt                  ← definición de placa (pines ADC/DAC/PTT/LED) + project()
├── partitions.csv                  ← nvs / otadata / phy_init / ota_0(1728K) / ota_1(1728K) / storage(512K, LittleFS)
├── sdkconfig                       ← target=esp32, flash 4MB, particiones personalizadas
├── dependencies.lock               ← idf 5.5.4, joltwallet/littlefs 1.22.1
├── LICENSE                         ← GPL-3.0
│
├── main/                                  (la aplicación)
│   ├── main.c                      ← app_main, arranque/reconexión Wi-Fi, orden de boot
│   ├── app_config.c/.h             ← app_config_t, valores de fábrica, carga/guardado JSON
│   ├── storage.c                   ← montaje/formateo/uso de LittleFS
│   ├── aprs_service.c/.h           ← el pegamento: dispatch de RX, helper de TX, config del módem, stats, loop test
│   ├── aprs_filter.c/.h            ← clasificador de tipo de payload TNC2 (mensaje/estado/telemetría/clima/…)
│   ├── beacon.c/.h                 ← 3 tareas de beacon independientes (trk / igate / digi)
│   ├── weather.c/.h                ← informe meteorológico APRS de la propia estación: refresco vía sensors_local + baliza WX (ver Sensores)
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
│   │   └── src/crc_ccit.c                 ← FCS
│   │
│   ├── igate/          ← cliente TCP de APRS-IS, login, filtros, dedup, RF→INET / INET→RF
│   ├── digirepeater/   ← lógica de path WIDEn-N / TRACEn-N / RELAY / ECHO / GATE
│   ├── message/        ← mensajería APRS, ack/reintentos, AES-128-CBC + base64
│   ├── lastheard/      ← anillo en RAM de estaciones escuchadas → JSON del dashboard
│   ├── trafficlog/     ← anillo en RAM de líneas de tráfico → JSON del dashboard (long-poll por seq)
│   ├── weather_telemetry/  ← solo structs a nivel de protocolo: weather_telemetry_data_t, aprs_weather_report_t,
│   │                          aprs_telemetry_report_t (definiciones de campos APRS101 WX + Telemetría, sin lógica)
│   ├── sensors_local/      ← EL framework de drivers de sensores (ver Sensores)
│   │   ├── include/sensors_local.h        ← API pública: registrar / desregistrar / save / recorrer el registro
│   │   ├── sensors_local.c                ← el registro dinámico en sí
│   │   └── drivers/*.c                    ← un archivo por driver de sensor, auto-compilado + auto-registrado
│   │       ├── sensor_local_weather_example.c    ← esqueleto WEATHER con datos aleatorios para copiar
│   │       └── sensor_local_telemetry_example.c  ← esqueleto TELEMETRY con datos aleatorios para copiar
│   └── webconfig/      ← administración con esp_http_server
│       ├── web_server.c            ← tabla de rutas
│       ├── web_common.c            ← auth, parseo de formularios, esqueleto HTML, helpers de campos
│       ├── pages/*.c               ← un archivo por página de administración (incl. page_wx.c, page_tlm.c, page_sensor.c)
│       └── translations/           ← translations.h + lang_en.h + lang_es.h + lang_it.h
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

El header de configuración de este componente está inusualmente bien documentado, y el razonamiento importa si lo tocas:

* **ADC a 76 800 Hz, no a 38 400.** 38 400 le da al perfil de 9600 Bd exactamente *cuatro* muestras de ADC por símbolo. El instante de muestreo del DPLL queda entonces cuantizado al 25 % de un símbolo y el voto por mayoría de tres muestras de `decode()` abarca el 75 % de un símbolo — la ventana del voto siempre alcanza una transición. La simulación en host del `modem.c` real, con relojes reales y **sin ruido**, producía errores de bit duros en cada fase donde los instantes del ADC se alinean con los de actualización del DAC; los dos relojes difieren en ~0,05 %, así que la alineación recorre esas fases cada ~55 ms. A 76 800 la misma simulación da cero errores de bit en todas las fases e incluso con hasta 30 µs de jitter en los flancos de TX. A los perfiles AFSK nunca les importó (se demodulan a 9600 Hz por un correlador tras el diezmado) y miden idéntico a cualquiera de las dos tasas. **Costo:** el doble de trabajo en el DSP de RX y `MODEM_RESAMPLE_RATIO` pasa a 8, lo que exige el FIR de diezmado más largo — un filtro de 8 taps calculado para 4:1 no hace antialias de uno de 8:1.
* **El DAC se queda en 38 400 Hz** (= 32 × 1200, múltiplo exacto de cada tasa de baudios soportada). El transmisor pone los flancos de símbolo exactamente sobre muestras del DAC sea cual sea la tasa; era el *receptor* el que necesitaba resolución.
* **`MODEM_ADC_CONV_FRAME = 128`, no el tamaño de bloque.** La propia ISR del ADC del IDF llama a `xRingbufferSendFromISR()`, que hace todo el `memcpy` **dentro de `portENTER_CRITICAL_ISR()`**. En Xtensa eso eleva `PS.INTLEVEL` a 3 — y el reloj de muestreo del DAC *es* una interrupción de nivel 3. Así que la ISR del DAC queda enmascarada durante toda la copia: 768 muestras ≈ 11 µs (10 % de un símbolo a 9600 Bd — fatal), 128 muestras ≈ 2 µs (2 % — dentro del presupuesto). Ninguna cantidad de `IRAM_ATTR` de nuestro lado ayuda: el código que bloquea es el del driver, ya está en IRAM, y simplemente es largo. A 1200 Bd, 11 µs es el 1,3 % de un símbolo y resulta invisible — que es exactamente por qué todos los perfiles AFSK pasaban mientras G3RUH perdía tramas.
* **`MODEM_DAC_TIMER_CORE (1) ≠ MODEM_ADC_ISR_CORE (0)`.** `portENTER_CRITICAL_ISR()` enmascara nivel ≤3 solo en el núcleo *local*. Pon el reloj del DAC en el otro núcleo y la ISR del ADC simplemente girará esperando el lock en vez de enmascararlo. Forzado con `#error`. Los dos arreglos (tramas chicas, núcleos separados) son independientes y ambos están aplicados.
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

## Sensores

Esta sección cubre el componente **`sensors_local`**: el framework en tiempo de ejecución que permite que sensores de hardware reales (o simulados) alimenten el Informe Meteorológico APRS de la propia estación y, en el futuro, el subsistema de Telemetría, sin que el núcleo necesite jamás una lista fija de "los sensores que soporta esta build". Si llegó aquí para conectar un BME280, un DS18B20, un ADS1115, una sonda de humedad de suelo, un divisor de voltaje de batería, o cualquier otra cosa a este firmware, esta es la sección a leer — explica exactamente cómo funciona el mecanismo y recorre paso a paso cómo agregar un driver nuevo de principio a fin.

### Por qué un framework de drivers en vez de una lista fija

Firmwares APRS anteriores de este linaje (y la página legada `/sensor` que todavía se incluye aquí, ver [más abajo](#la-página-legada-sensor--no-es-lo-mismo)) tomaban el enfoque opuesto: un arreglo de tamaño fijo de "slots de sensor" en `g_config`, cada uno descrito por un `type`/`port`/`address` numérico que algún `switch` central debía interpretar. Cada sensor nuevo implicaba editar ese switch central, recompilar, y esperar que los IDs numéricos no chocaran con los de otra build.

`sensors_local` invierte esto:

* El núcleo (`sensors_local.c`) no sabe **nada** sobre ningún sensor específico. Solo sabe cómo mantener una lista de structs "driver" opacos y llamar a un puñado de punteros a función sobre ellos.
* Cada sensor real vive en su **propio archivo `.c`** bajo `components/sensors_local/drivers/`, y se agrega a la lista **automáticamente al arrancar**, incluso antes de que `app_main()` corra, usando un atributo constructor de C escondido detrás de la macro `SENSORS_LOCAL_DRIVER_AUTOREGISTER`.
* El sistema de build (`components/sensors_local/CMakeLists.txt`) compila **todos** los archivos `.c` que encuentra en `drivers/` con un `file(GLOB …)` — no hay ninguna línea por driver que agregar a ningún `CMakeLists.txt`, ni ninguna entrada por driver que agregar a ningún header.

El resultado práctico: **agregar un sensor es "soltar un archivo nuevo en `drivers/`, recompilar"** — nada en `sensors_local.c`, `weather.c`, `sensors_local.h`, ni ningún `CMakeLists.txt` necesita cambiar para un driver nuevo ordinario.

### Las dos familias de datos

Un driver no devuelve un flujo de bytes crudo; llena **campos a nivel de aplicación ya agrupados por tipo de payload APRS**, definidos en el componente separado `weather_telemetry` (`weather_telemetry.h`, una transcripción directa del spec APRS101 más los addenda 1.1/1.2):

| Familia | Bit (`sensor_local_data_kind_t`) | Struct destino | Consumido hoy por |
|---|---|---|---|
| **Weather** | `SENSOR_LOCAL_DATA_WEATHER` (`1u << 0`) | `aprs_weather_report_t` (viento, temperatura, humedad, presión, lluvia ×3, nieve, luminosidad, altura de inundación ×2, …) | `weather.c` → baliza APRS WX real en el aire |
| **Telemetry** | `SENSOR_LOCAL_DATA_TELEMETRY` (`1u << 1`) | `aprs_telemetry_report_t` (5 canales analógicos `A1..A5`, 8 canales digitales `B1..B8`) | `weather.c` llena el contenedor compartido desde `sensors_local`, pero **todavía no hay ningún codificador/baliza que lo lea de vuelta al aire** — ver [limitaciones](#7-mapéelo-en-la-página-weather) |
| *(reservado para el futuro)* | ej. `SENSOR_LOCAL_DATA_GPS = 1u << 2` | *(nuevo struct, ej. una posición fija)* | aún no definido — ver [Añadir un tipo de sensor completamente nuevo](#añadir-un-tipo-kind-de-sensor-completamente-nuevo) |

`SENSOR_LOCAL_DATA_ALL` es simplemente el OR de cada bit definido actualmente, y es lo que `weather.c` pasa cuando le pide al registro que refresque todo una vez por segundo.

Un único driver es libre de anunciar **una o ambas** banderas en su campo `capabilities` — por ejemplo, una placa combinada con un sensor barométrico *y* un canal ADC libre podría reportar Weather **y** Telemetry desde la misma llamada a `save()`.

### Anatomía de un driver (`sensor_local_driver_t`)

Cada driver es una instancia de este struct (declarado en `components/sensors_local/include/sensors_local.h`):

```c
struct sensor_local_driver {
    const char *name;      // id estable, único, legible por humanos, ej. "bme280", "ads1115-batt"
    uint32_t capabilities; // OR de SENSOR_LOCAL_DATA_WEATHER / _TELEMETRY (no puede ser cero)

    sensor_local_init_fn_t   init;   // puesta en marcha opcional única (puede ser NULL)
    sensor_local_save_fn_t   save;   // REQUERIDO: la única entrada que realmente lee el sensor
    sensor_local_deinit_fn_t deinit; // apagado opcional (puede ser NULL)

    void *ctx; // estado privado del driver, opaco para el registro

    // --- propiedad del registro; un driver nunca debe tocar esto por sí mismo ---
    bool initialized;
    bool failed;
};
```

Tres roles de puntero a función, cada uno con un contrato preciso:

* **`init(self)`** — llamado **a lo sumo una vez**, de forma perezosa, la primera vez que el driver realmente se necesita (o de forma eager, para cada driver, cuando `weather_start()` llama a `sensors_local_init_all()` al arrancar). Aquí es donde se abre un bus I2C/SPI/UART, se sondea el registro de ID del chip, se asignan buffers privados, y se siembra lo que necesite sembrarse (ej. `srand()`). Devolver `ESP_OK` en éxito; cualquier otro valor **marca al driver como `failed` permanentemente** durante toda la vida del registro (hasta que se desregistre y se vuelva a registrar), y se lo salta a partir de entonces.
* **`save(self, data, kind)`** — LA entrada común, llamada en cada ciclo de refresco (1 Hz, impulsado por `weatherSensorTask` de `weather.c`). `kind` ya viene **enmascarado** a solo los bits que tanto el llamador quiere como el driver anunció, así que un driver solo-Weather nunca tiene que revisar Telemetry por sí mismo. El driver lee su sensor y escribe directamente en el contenedor `data` propiedad del llamador — sin asignación, sin colas. Debe tocar **solo** la familia que anunció en `capabilities`, y debe **tolerar un destino vacío** (ej. `data->weather_qty == 0`) sin hacer nada para esa familia en vez de desreferenciar un arreglo nulo.
* **`deinit(self)`** — espejo opcional de `init()`, llamado desde `sensors_local_unregister()` o `sensors_local_deinit()`. Cerrar lo que `init()` abrió.

`ctx` es suyo: apúntelo a un struct `static` (como hacen ambos drivers de ejemplo) si el driver no tiene razón para soportar más de una instancia, o a almacenamiento de heap/pool si sí la tiene (ver [Varias instancias](#varias-instancias-del-mismo-tipo-de-sensor)).

### El registro: cómo se encuentra y se llama a un driver

`sensors_local.c` implementa el registro como un pequeño arreglo de **punteros** a driver, protegido por mutex y crecible en heap (nunca copia — el almacenamiento de su struct `static` es lo que realmente vive en la tabla):

```
sensors_local_init()          // crea el mutex del registro; seguro de llamar más de una vez
sensors_local_register(drv)   // agrega a la tabla; rechaza save NULL, nombre vacío, nombre
                               // duplicado, o capabilities == SENSOR_LOCAL_DATA_NONE
sensors_local_unregister(name)// elimina por nombre, llamando a deinit() si el driver estaba inicializado
sensors_local_count()         // cuántos drivers están registrados actualmente
sensors_local_get(index)      // obtiene por posición 0..count-1 (usado por el dropdown de la página Weather)
sensors_local_find(name)      // obtiene por nombre
sensors_local_init_all()      // inicializa (init()) eagerly cada driver aún no inicializado
sensors_local_save(data,kind) // recorre la tabla; para cada driver cuyas capabilities intersecten
                               // con kind, lo inicializa perezosamente si hace falta, luego llama a su save()
sensors_local_deinit()        // deinit() + descarta todo; libera el arreglo subyacente
```

`sensors_local_register()` se puede llamar **incluso antes de que el scheduler de FreeRTOS esté corriendo**, porque `SENSORS_LOCAL_DRIVER_AUTOREGISTER` se dispara desde una función `__attribute__((constructor))`, que el runtime de C invoca durante la inicialización estática, antes de `app_main()`. En ese punto `s_lock` (el mutex del registro) todavía no existe — `registry_lock()`/`registry_unlock()` son no-ops mientras `s_lock == NULL`, lo cual es seguro solo porque toda esa fase es de un único hilo. La primera llamada real a `sensors_local_init()` (desde `weather_start()`, una vez que el scheduler está arriba) crea el mutex y hace que todo acceso posterior al registro sea thread-safe.

Un driver que falla su `init()` o devuelve un error desde `save()` se registra en el log (`ESP_LOGW`) y **se salta**; nunca aborta la pasada para los demás drivers, y nunca hace fallar la baliza Weather.

### Flujo de datos de punta a punta, del sensor al APRS

```
 arranque (antes de app_main)
   └─ corre el constructor de cada archivo drivers/*.c
        └─ SENSORS_LOCAL_DRIVER_AUTOREGISTER → sensors_local_register(&my_driver)

 weather_start()  (llamado desde aprs_service.c, una vez, al arrancar)
   ├─ conecta weather_telemetry_data.weather/.telemetry_report al almacenamiento estático
   ├─ sensors_local_init()          ← crea el mutex del registro (thread-safe desde aquí)
   ├─ sensors_local_init_all()      ← corre init() en cada driver auto-registrado
   └─ arranca weatherSensorTask (1 Hz) y weatherBeaconTask (cada wx_interval segundos)

 weatherSensorTask   (1 Hz, para siempre)
   ├─ limpia las banderas "enabled" en weather_telemetry_data (para que un driver que deja
   │    de reportar un campo este ciclo no deje un valor obsoleto pareciendo válido)
   ├─ sensors_local_save(&weather_telemetry_data, SENSOR_LOCAL_DATA_ALL)
   │    └─ para cada driver registrado cuyas capabilities coincidan:
   │         lo inicializa perezosamente si aún no, luego llama a su save()
   │         → el driver escribe directo en aprs_weather_report_t / aprs_telemetry_report_t
   └─ acumula cualquier campo "Promediado" (checkbox de la página Weather) en una suma/cuenta corriente

 weatherBeaconTask   (cada g_config.wx_interval segundos, solo si wx_en)
   ├─ resolve_fields(): para cada token WX en el aire, lee el valor en vivo directamente de
   │    weather_telemetry_data, o el valor promediado acumulado arriba, según el
   │    checkbox "Promediado" por campo — NO directamente del sensor, así que un reportero
   │    intermitente igual contribuye a un promedio razonable
   ├─ build_wx_packet(): renderiza la línea TNC2 estándar "!lat/lon_WIND/SPDgGUSTtTTTrRRRhHHbBBBBB…"
   └─ la transmite por RF (aprs_service_send_tnc2()) y/o APRS-IS (igate_send_raw()), según la config

 Página de administración web Weather (/wx)
   └─ wx_channel_select() de page_wx.c recorre sensors_local_get(0..count-1) y lista solo
        los drivers cuyas capabilities incluyen SENSOR_LOCAL_DATA_WEATHER, para que el operador
        pueda mapear "canal N: <nombre del driver>" a un campo específico en el aire (viento, temperatura, …)
```

El punto clave para quien agrega un sensor: **nunca llama a nada desde `weather.c` ni desde la administración web usted mismo.** Registrar el driver es toda la integración; el refresco a 1 Hz, el promediado, la codificación WX en el aire y el selector de canal lo descubren todos por sí solos a través del registro.

### Los dos drivers de ejemplo incluidos

Dos drivers vienen compilados por defecto, únicamente para poder ejercitar todo el pipeline (registro → refresco a 1 Hz → codificador/baliza WX → selector de canal de la página Weather) **sin ningún hardware real conectado**:

* **`components/sensors_local/drivers/sensor_local_weather_example.c`** (nombre de driver `wx-example`) — anuncia solo `SENSOR_LOCAL_DATA_WEATHER`. En cada `save()` llena viento (dirección/sostenido/ráfaga), temperatura, humedad, presión barométrica, lluvia de la última hora y luminosidad con valores **aleatorios** plausibles (`rnd(lo, hi)`) y marca la bandera `enabled[...]` de cada campo. Está condicionado por `CONFIG_SENSORS_LOCAL_WEATHER_EXAMPLE_DRIVER`, que el `CMakeLists.txt` del componente define incondicionalmente hoy.
* **`components/sensors_local/drivers/sensor_local_telemetry_example.c`** (nombre de driver `tlm-example`) — anuncia solo `SENSOR_LOCAL_DATA_TELEMETRY`. En cada `save()` llena cada canal analógico **asignado** con un valor aleatorio entre `0..255` y cada canal digital con un `0`/`1` aleatorio, tocando de nuevo solo los canales que el llamador realmente pidió (`analog_count`/`digital_count`). Condicionado por `CONFIG_SENSORS_LOCAL_TELEMETRY_EXAMPLE_DRIVER`.

Ambos están pensados para ser **copiados, no conservados**: son el esqueleto documentado para un driver real de la familia correspondiente. Bórrelos o póngalos en `#if 0` cuando tenga hardware real, o simplemente déjelos registrados junto a su(s) driver(s) real(es) — el registro no tiene problema en mantener ambos a la vez, y `sensors_local_unregister("wx-example")` elimina uno limpiamente si prefiere no recompilar.

### Añadir un sensor nuevo, paso a paso

#### 1. Decida qué produce su driver

Elija la familia de payload (o ambas): un BME280 o DS18B20 es Weather; un divisor de voltaje de batería en un pin ADC, un interruptor de puerta/reed, o una sonda de humedad de suelo es naturalmente Telemetry (canal analógico o digital); una placa combinada puede ser ambas.

#### 2. Copie un esqueleto y renómbrelo

Copie el driver de ejemplo que corresponda (`sensor_local_weather_example.c` para Weather, `sensor_local_telemetry_example.c` para Telemetry, o parta de ambos si necesita ambas) a un archivo nuevo bajo `components/sensors_local/drivers/`, ej. `sensor_local_bme280.c`. El **glob** de archivos en `CMakeLists.txt` lo recoge automáticamente — no lo agrega a ninguna lista de fuentes.

#### 3. Complete `init()`

Reemplace la puesta en marcha con `srand()` por su configuración real única: configure y sondee el bus I2C/SPI/UART, lea y verifique un registro de chip-ID, asigne cualquier almacenamiento de coeficientes de calibración, y devuelva `ESP_OK` solo cuando esté seguro de que las lecturas siguientes tendrán éxito. Guarde lo que la llamada a `save()` necesitará después (un handle, constantes de calibración, un número de GPIO, …) en `self->ctx`.

#### 4. Complete `save()`

Lea el sensor, convierta a las unidades de ingeniería que `weather_telemetry.h` documenta para cada campo (Fahrenheit para temperatura, mph para viento, décimas de milibar para presión, centésimas de pulgada para lluvia, etc. — el header detalla la unidad y el rango en el aire de cada campo), escriba el/los valor(es), y marque la(s) bandera(s) `enabled[...]` correspondiente(s) — de otro modo el campo se trata como "no reportado este ciclo" sin importar qué valor haya en el struct. Siempre verifique `kind` y que los punteros/`*_qty` de destino no sean NULL/cero antes de escribir, exactamente como hacen ambos ejemplos; un driver invocado con `data->weather_qty == 0` (porque el llamador solo quería Telemetry este ciclo) debe devolver `ESP_OK` sin haber tocado nada.

#### 5. Declare el descriptor y auto-regístrelo

```c
static sensor_local_driver_t bme280_driver = {
    .name         = "bme280",
    .capabilities = SENSOR_LOCAL_DATA_WEATHER,
    .init         = bme280_init,
    .save         = bme280_save,
    .deinit       = bme280_deinit, // o NULL si no hay nada que apagar
    .ctx          = &s_bme280_ctx,
};
SENSORS_LOCAL_DRIVER_AUTOREGISTER(bme280_driver);
```

`name` debe ser único entre todos los drivers registrados (el registro falla con `ESP_ERR_INVALID_STATE` de lo contrario) — también es lo que aparece, textualmente, en el dropdown de canal de la página Weather ("`0: bme280`"), así que elija algo que un operador de estación reconozca.

#### 6. Compile — nada más que conectar

`idf.py build`. Como `components/sensors_local/CMakeLists.txt` hace un glob de `drivers/*.c` y enlaza todo el componente con `WHOLE_ARCHIVE` (para que el `--gc-sections` del linker no pueda descartar un objeto cuya única referencia es su propio constructor), su archivo nuevo se compila, enlaza, y se auto-registra al arrancar con **cero ediciones** a `sensors_local.c`, `sensors_local.h`, `weather.c`, o cualquier `CMakeLists.txt` fuera del propio archivo del driver.

#### 7. Mapéelo en la página Weather

Grabe, abra la página **Weather** de la administración web, y el nombre de su driver ahora aparece como opción en el dropdown de canal de cada campo (`wx_channel_select()` de `page_wx.c` lo lista automáticamente porque recorre el registro en vivo). Elija qué campo(s) en el aire debería alimentar y guarde. **Los canales de Telemetry todavía no tienen selector equivalente ni codificador en el aire** — los valores de un driver de Telemetry llegan a `weather_telemetry_data.telemetry_report[0]` y quedan ahí, leídos solo por el código futuro que agregue la baliza `T#nnn`; hoy nada los transmite (ver la [matriz de funcionalidades](#matriz-de-funcionalidades)).

#### 8. Ejemplo trabajado: un BME280 I2C real

Un patrón recortado pero completo (el manejo de errores y la aritmética real de registros se dejan al datasheet/biblioteca de driver del sensor — el punto aquí es la forma de integración con `sensors_local`, no un driver de BME280 desde cero):

```c
#include "esp_log.h"
#include "sensors_local.h"
#include "driver/i2c_master.h"   // o su driver I2C preferido

typedef struct {
    i2c_master_dev_handle_t dev;
    // ... coeficientes de calibración leídos durante init() ...
} bme280_ctx_t;

static bme280_ctx_t s_ctx;

static esp_err_t bme280_init(sensor_local_driver_t *self) {
    bme280_ctx_t *c = (bme280_ctx_t *)self->ctx;
    // abrir el bus I2C / agregar el dispositivo en su dirección de 7 bits, sondear chip-id (0x60), ...
    // leer los registros de calibración en c-> ...
    if (/* sondeo falló */ false)
        return ESP_FAIL; // -> el driver se marca como fallido y se salta a partir de entonces
    return ESP_OK;
}

static esp_err_t bme280_save(sensor_local_driver_t *self, weather_telemetry_data_t *data, sensor_local_data_kind_t kind) {
    bme280_ctx_t *c = (bme280_ctx_t *)self->ctx;

    if (!(kind & SENSOR_LOCAL_DATA_WEATHER) || data->weather == NULL || data->weather_qty < 1)
        return ESP_OK; // nada que hacer este ciclo

    aprs_weather_report_t *wx = &data->weather[0];

    float temp_c, pressure_pa, humidity_pct;
    // ... disparar una medición en modo forzado y leer + compensar los registros crudos en
    //     temp_c / pressure_pa / humidity_pct usando los coeficientes de calibración de c ...

    wx->temperature_f = (int16_t)lroundf(temp_c * 9.0f / 5.0f + 32.0f);
    wx->enabled[APRS_WX_SENSOR_TEMPERATURE] = true;

    wx->barometric_pressure_tenths_mb = (uint32_t)lroundf(pressure_pa / 10.0f);
    wx->enabled[APRS_WX_SENSOR_BAROMETRIC_PRESSURE] = true;

    wx->humidity_percent = (uint8_t)lroundf(humidity_pct);
    wx->enabled[APRS_WX_SENSOR_HUMIDITY] = true;

    return ESP_OK;
}

static sensor_local_driver_t bme280_driver = {
    .name = "bme280",
    .capabilities = SENSOR_LOCAL_DATA_WEATHER,
    .init = bme280_init,
    .save = bme280_save,
    .deinit = NULL,
    .ctx = &s_ctx,
};
SENSORS_LOCAL_DRIVER_AUTOREGISTER(bme280_driver);
```

Guarde esto como `components/sensors_local/drivers/sensor_local_bme280.c`, conecte la lectura I2C real donde los comentarios lo indican, `idf.py build`, y la página Weather ofrecerá `"N: bme280"` como fuente para Temperatura, Presión y Humedad.

### Varias instancias del mismo tipo de sensor

Nada impide que coexistan dos sensores físicos del mismo tipo (ej. un BME280 interior y otro exterior): dele a cada uno su propia unidad de traducción (o el mismo archivo `.c` con dos descriptores), un `name` **distinto** (`"bme280-indoor"` / `"bme280-outdoor"`), su propia instancia de struct `ctx`, y su propia dirección I2C / bus / GPIO horneada en ese `ctx`. Cada uno se registra independientemente y aparece como su propia fila en el selector de canal de la página Weather.

### Manejo de errores y fallo del driver

* Un `init()` que devuelve algo distinto de `ESP_OK` marca `failed = true` **permanentemente** para ese registro — el driver se salta en cada `sensors_local_save()` futura, se registra en el log una vez (`ESP_LOGW`) en el momento en que falló, hasta que algo llame explícitamente a `sensors_local_unregister()` seguido de un `sensors_local_register()` nuevo (que resetea tanto `initialized` como `failed`).
* Un `save()` que devuelve un error se registra en el log (`ESP_LOGW`) y simplemente se salta **para ese único ciclo** — `initialized`/`failed` quedan intactos, así que el próximo tick a 1 Hz lo vuelve a intentar. Esto importa para sensores con un hipo de bus ocasional: una única transacción I2C fallida no deshabilita permanentemente al driver como sí lo hace un `init()` fallido.
* Cualquiera de los dos tipos de fallo está aislado a ese único driver; `sensors_local_save()` siempre continúa con los drivers restantes en el registro.

### Seguridad entre tareas (thread safety)

`sensors_local_register()`/`unregister()`/`save()`/`get()`/`find()`/`count()` toman todos el mutex interno del registro, así que son seguros de llamar desde cualquier tarea una vez que `sensors_local_init()` ha corrido. La única excepción, por diseño, son los propios constructores de auto-registro: corren antes de que exista el scheduler, de un único hilo, sin que el mutex exista todavía — que es exactamente por qué `registry_lock()`/`registry_unlock()` están escritos como no-ops mientras `s_lock == NULL`.

El propio `init()`/`save()`/`deinit()` de un driver **no** están envueltos en ningún lock por el framework — si el estado privado de su driver (`ctx`) alguna vez se toca desde algo más que el `weatherSensorTask` a 1 Hz (por ejemplo, un ISR actualizando un contador compartido), el propio driver es responsable de la sincronización que eso necesite.

### Añadir un tipo (kind) de sensor completamente nuevo

Weather y Telemetry no son las únicas familias de payload que el framework puede llegar a transportar — `sensors_local.h` documenta exactamente cómo extenderlo, justo al lado del enum:

```c
/*
 * To add a new sensor family in the future (e.g. GPS, power/battery, air
 * quality, ...), append a new bit here:
 *
 *     SENSOR_LOCAL_DATA_GPS = 1u << 2,
 *
 * and OR it into SENSOR_LOCAL_DATA_ALL. Nothing else in the registry needs to
 * change - sensors_local_register() only requires that a driver's
 * capabilities be non-zero, and any UI page (like the Weather "Sensor
 * Mapping" table) that wants only its own kind of sensor filters the
 * registry by testing `driver->capabilities & SENSOR_LOCAL_DATA_xxx`.
 */
```

Concretamente, agregar por ejemplo un "kind" de GPS significa:

1. Agregar `SENSOR_LOCAL_DATA_GPS = 1u << 2` a `sensor_local_data_kind_t` en `sensors_local.h`, y hacerle OR en `SENSOR_LOCAL_DATA_ALL`.
2. Agregar el struct destino donde debería aterrizar un fix de GPS (un campo nuevo en `weather_telemetry_data_t`, o un struct completamente nuevo, en `weather_telemetry.h`) — el registro en sí nunca necesita conocer su forma, ya que los drivers escriben directamente en él.
3. Escribir driver(s) cuyas `capabilities` incluyan el bit nuevo y cuyo `save()` llene el struct nuevo.
4. Dondequiera que un consumidor necesite el kind nuevo, filtre el registro con `driver->capabilities & SENSOR_LOCAL_DATA_GPS`, exactamente como `wx_channel_select()` de `page_wx.c` filtra hoy sobre `SENSOR_LOCAL_DATA_WEATHER`. El registro, `sensors_local_save()`, y cada driver existente quedan completamente sin afectar.

### La página legada `/sensor` — no es lo mismo

La administración web también tiene una ruta **`/sensor`** (`components/webconfig/pages/page_sensor.c`) con campos por slot — `enable`, `type`, `port`, `address`, `samplerate`, `averagerate`, tres coeficientes de ecuación lineal (`A`/`B`/`C`), un nombre y una unidad — almacenados en `g_config.sensor[0..SENSOR_NUMBER-1]`. **Esto no es el framework `sensors_local`.** Lo precede, no tiene entrada de menú en la barra lateral actual, y — de manera crítica — **nada en el firmware lee jamás `g_config.sensor[]` de vuelta**: no hay ningún camino de código que convierta un slot guardado en una lectura real o un valor en el aire. Se mantiene solo por compatibilidad con `config.json` (ver [Estado y limitaciones conocidas](#estado-y-limitaciones-conocidas)). Si está conectando hardware real, use `sensors_local` (esta sección), no esta página.

### Resumen de referencia de Sensores

| Concepto | Dónde | Propósito |
|---|---|---|
| `sensor_local_driver_t` | `components/sensors_local/include/sensors_local.h` | descriptor de un driver: nombre, capabilities, init/save/deinit, ctx |
| `sensors_local_register()` / `_unregister()` | `sensors_local.h` / `.c` | agregar/quitar un driver del registro en tiempo de ejecución |
| `SENSORS_LOCAL_DRIVER_AUTOREGISTER(sym)` | `sensors_local.h` | macro constructor de C: auto-registra un driver `static` antes de `app_main()` |
| `sensors_local_save(data, kind)` | `sensors_local.h` / `.c` | LA entrada agregada: pide a cada driver capaz y saludable que llene `data` |
| `weather_telemetry_data_t` | `components/weather_telemetry/include/weather_telemetry.h` | el contenedor compartido en el que escriben los drivers (`weather[]` + `telemetry_report[]`) |
| `weather.c` | `main/weather.c` | posee el contenedor, impulsa el refresco a 1 Hz, codifica y balicea el informe WX APRS real |
| `page_wx.c` | `components/webconfig/pages/page_wx.c` | página Weather; el selector de canal se llena en vivo desde el registro |
| `drivers/*.c` | `components/sensors_local/drivers/` | dónde agregar un sensor nuevo — un archivo, ninguna otra edición |
| página `/sensor` + `g_config.sensor[]` | `components/webconfig/pages/page_sensor.c` | **andamiaje legado no relacionado** — solo configuración, no conectado a nada |

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

Compilar en español o italiano en lugar de inglés:

```bash
idf.py build -DLANGUAGE=LANG_ES
idf.py build -DLANGUAGE=LANG_IT
```

> `espressif/esp-dsp` **no** es una dependencia, a propósito: solo lo arrastraba el viejo componente `esp32_IDF_libAPRS`. El módem actual implementa sus propios filtros y nada en el proyecto llama a `dsps_*`. Si vienes de un checkout más viejo, borra `dependencies.lock` y deja que `idf.py` lo regenere.

### Tabla de particiones (`partitions.csv`)

| Nombre | Tipo | SubTipo | Offset | Tamaño |
|---|---|---|---|---|
| `nvs` | data | nvs | 0x9000 | 24 K |
| `otadata` | data | ota | 0xF000 | 8 K |
| `phy_init` | data | phy | 0x11000 | 4 K |
| `ota_0` | app | ota_0 | 0x20000 | **1728 K** |
| `ota_1` | app | ota_1 | 0x1D0000 | **1728 K** |
| `storage` | data | spiffs | 0x380000 | **512 K** → montada como **LittleFS** en `/storage` |

Dos ranuras de aplicación OTA (`ota_0` / `ota_1`) → **la actualización OTA está disponible** desde la página web **About / Firmware**: subís un `.bin`, se escribe en la ranura que no está corriendo, y el dispositivo reinicia hacia ella una vez verificada la escritura. `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` está activado, así que una imagen nueva que no logre levantar el admin web vuelve automáticamente a la ranura anterior en el siguiente reinicio (ver `esp_ota_mark_app_valid_cancel_rollback()` en `main.c`).

> **Migrando un dispositivo existente:** esta tabla de particiones reemplaza el esquema anterior de una sola partición `factory`. Un dispositivo que todavía corre la tabla vieja no tiene `ota_0`/`ota_1` a donde actualizar, así que su primer paso a este firmware debe ser una regrabación única por USB/UART (`idf.py -p PUERTO flash`, o un `.bin` combinado). Cada actualización posterior puede hacerse desde el admin web.

El `sdkconfig` viene con `CONFIG_COMPILER_OPTIMIZATION_DEBUG=y` (`-Og`) y las aserciones activadas; pasa a `-Os` si andas justo de flash.

---

## Primer arranque y configuración

1. Con una partición nueva, LittleFS se autoformatea y `app_config_load()` escribe `/storage/config.json` con los valores de fábrica.
2. El ESP32 levanta como **AP Wi-Fi**:
   * SSID **`esp32idf_APRS`**, contraseña **`esp32idf_APRS`**, canal 1, máximo 4 clientes, WPA2-PSK.
3. Conéctate y navega al dispositivo (por defecto `http://192.168.4.1/`).
4. **Inicia sesión: `admin` / `admin`** — cámbialo en la página *System*.
5. En *Wireless*: elige **Station** o **AP+STA**, tilda **Enable** en un bloque de WiFi Client, pon SSID/contraseña, Guardar.
6. En *IGate*: pon tu **indicativo**, **SSID**, **passcode**, **host**/**puerto** de APRS-IS, filtro, coordenadas, símbolo, comentario.
7. En *Radio / Modem*: habilita el módem de audio, elige la modulación, el pin y polaridad de PTT, el preámbulo y el time slot de TX.
8. Reinicia (o Guardar — casi todo se reaplica en vivo).

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

> 🔴 **Cambia `NOCALL` y pon un passcode real antes de transmitir.** Verifica también que estás licenciado para la frecuencia y el ciclo de trabajo con el que vas a salir al aire.

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
| GET | `/about` | versión de firmware/IDF, partición, formulario de actualización OTA |
| POST | `/ota_update` | subida multipart de firmware → grabar ranura OTA inactiva → reiniciar |

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

**About** — nombre del proyecto, versión, fecha/hora de compilación, versión del IDF, etiqueta/offset/tamaño de la partición en ejecución, y el panel de **Actualización OTA**: elegís un `.bin`, se sube (con barra de progreso, transmitido directo a la ranura `ota_0`/`ota_1` inactiva vía `esp_ota_write()`, sin bufferearlo entero en RAM), y el dispositivo reinicia hacia esa imagen una vez escrita y verificada. Una imagen defectuosa que nunca se confirma (ver `esp_ota_mark_app_valid_cancel_rollback()` en `main.c`) se revierte automáticamente a la ranura anterior en el siguiente reinicio. Si el dispositivo todavía tiene la tabla de particiones vieja de un solo `factory`, este panel lo indica y pide una regrabación manual por USB/UART primero.

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

La herramienta de puesta en marcha más útil del proyecto. Cablea **GPIO25 → GPIO33**, abre *Radio / Modem* y aprieta **LOOP TEST**.

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

* `app_config.h` define `LANG_EN 0`, `LANG_ES 1`, `LANG_IT 2` y el `LANGUAGE` activo (por defecto `LANG_EN`).
* `translations/translations.h` es el *único* lugar que decide qué `lang_xx.h` se incluye.
* Cada cadena visible por el usuario pasa por una macro `TR_xxx`.

**Agregar un idioma:**

1. Copia `translations/lang_en.h` → `lang_xx.h`, traduce cada literal, mantén idéntico cada nombre de macro.
2. `#define LANG_XX <siguiente número libre>` en `app_config.h`.
3. Agrega una rama `#elif LANGUAGE == LANG_XX` en `translations.h`.
4. Compila con `-DLANGUAGE=LANG_XX`.

Que falte un `TR_xxx` en un idioma es un **error de compilación en la build de ese idioma** — intencional, para que no se cuelen cadenas sin traducir.

---

## Resolución de problemas

**"Cambié a modo Station, guardé, reinicié, y no pasa nada."**
Lee el log de arranque — este camino está instrumentado a fondo a propósito:

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

**"El LOOP TEST falla con 'no packet received back'"** — revisa la historia de la atenuación del ADC: el DAC recorre todo el riel mientras una atenuación de 0 dB solo mide ~0–1,1 V, recortando el tono más allá de lo que el demodulador puede enganchar. El componente fija `ADC_ATTEN_DB_12`, que es lo correcto; si lo sobreescribiste, vuélvelo atrás.

**"El IGate dice unverified"** — `aprs_mycall` / `aprs_passcode` incorrectos. El banner se loguea; también la línea exacta de login, incluida la cadena de filtro, así que un filtro mal formado se ve enseguida.

**"Todo funciona pero aprs.fi no muestra mi estación"** — beacons: habilita `igate_bcn` y al menos uno de `igate_loc2rf` / `igate_loc2inet`, y pon coordenadas reales. Relevar tráfico nunca te anuncia a ti.

**"9600 Bd pierde tramas"** — esa es la patología que motivó los cambios de tasa del ADC, tamaño de trama de conversión y separación de núcleos. Si sobreescribiste `MODEM_ADC_SAMPLERATE`, `MODEM_ADC_CONV_FRAME`, `MODEM_DAC_TIMER_CORE` o `MODEM_ADC_ISR_CORE`, relee [Por qué los números son los que son](#por-qué-los-números-son-los-que-son). Confirma además que estás alimentando **audio plano/de discriminador**.

---

## Estado y limitaciones conocidas

* **Trabajo en curso.** Lo dice el README original, y lo dice también este.
* **OTA disponible** — página About / Firmware, ranuras `ota_0`/`ota_1` con rollback automático. Los dispositivos con la tabla de particiones vieja (`factory` única) necesitan una regrabación por serie, una sola vez, para pasar a este esquema.
* **`rf2inetFilter` no se aplica.** `igateProcess()` sigue aplicando solo las reglas RFONLY/TCPIP/qA/NOGATE/satélite y el antiduplicados, no la máscara de tipos de paquete. `aprs_filter_classify_tnc2()` / `aprs_filter_pass()` no dependen de la dirección, así que engancharlo ahí son dos líneas.
* **Solo se usa el primer slot STA habilitado.** El failover multi-AP figura como "se puede agregar más adelante".
* **Sin GPS, sin SmartBeaconing.** Los campos de configuración existen; los beacons son solo de posición fija.
* **Sin driver de LoRa / módulo RF.** `ENABLE_RF_MODULE` está comentado; la UI y la configuración de SX12xx son andamiaje.
* **VPN / MQTT / GNSS / meteorología / telemetría / sensores / Bluetooth / PPP / OLED / Modbus**: existen campos de configuración y (algunas) páginas, sin implementación.
* **El parseo de símbolos** solo cubre los formatos de posición sin marca de tiempo `!` / `=`; `/` y `@` dejan el ícono en blanco.
* **`agc_max_gain`, `sql_level`, `volume`, `adc_gpio`, `dac_gpio`, `rf_sql_*`, `rf_pwr_*`, `adc_atten`** están inertes desde el cambio de módem; se conservan solo por compatibilidad de `config.json`.
* El `sdkconfig` viene con `-Og` + aserciones, no con un perfil de release.

---

## Notas de portabilidad

Migrar del viejo **`esp32_IDF_libAPRS`** al **`esp32idf_radioamateur_modem`** cambió varios contratos. Si estás trasladando parches:

| Antes | Ahora |
|---|---|
| Pines de ADC/DAC/PTT en `aprs_modem_config_t` en **runtime** desde `g_config` | **Tiempo de compilación** `MODEM_*_GPIO` vía `idf_build_set_property()` en el `CMakeLists.txt` de nivel superior (el PTT es la excepción: volvió a runtime, validado) |
| La app bombeaba `AFSK_Poll()` / `APRS_poll()` desde su propia tarea | El componente es dueño de ambos: `AFSK_init()` arranca una tarea DSP de RX fijada a un núcleo; `modem_init()` arranca `modem_svc`. Llamar `AFSK_Poll()` tú mismo ahora **compite** con esa tarea por el mismo FIFO. |
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

  Por favor contacta a sus autores para información sobre esos proyectos.
* **littlefs** — ARM/joltwallet (BSD-3-Clause), vía el registro de componentes de ESP.
* El esquema de configuración, la disposición del panel web y la semántica del dashboard siguen al proyecto de referencia **esp32idf_APRS / ESP32APRS**, para que los `config.json` existentes y las expectativas de los usuarios se trasladen sin fricción.

---

## Licencia

**GNU General Public License v3.0** — ver [`LICENSE`](LICENSE).

El `managed_components/joltwallet__littlefs` incluido lleva su propia licencia (BSD-3-Clause para littlefs en sí).

---

### Descargo de responsabilidad de radioafición

Transmitir en frecuencias de radioafición requiere una licencia válida para tu país y banda. **Pon un indicativo real** (el valor por defecto es `NOCALL`), usa un passcode de APRS-IS legítimo, respeta tu plan de bandas local y las convenciones de digipeo (`WIDE1-1,WIDE2-1` *no* siempre es apropiado), y no gatees tráfico `NOGATE`/`RFONLY`. Eres responsable de todo lo que este dispositivo transmita.
