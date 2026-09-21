# Banco de pruebas de audio de esp32idf_APRS — manual de usuario

🌐 [English](README.md) · **Español** · [Italiano](README_it.md)

Pruebe el receptor APRS AFSK de 1200 baudios del firmware **esp32idf_APRS** con
**tráfico APRS real grabado del aire** y mida cuántos paquetes decodifica el
ESP32 en comparación con un decodificador de referencia (**multimon-ng**).

---

## Contenido

1. [Qué hace esta prueba](#1-qué-hace-esta-prueba)
2. [Qué necesita](#2-qué-necesita)
3. [Construir el cable de audio (hardware)](#3-construir-el-cable-de-audio-hardware)
4. [Preparar el ESP32 (ajustes del firmware)](#4-preparar-el-esp32-ajustes-del-firmware)
5. [Preparar la PC](#5-preparar-la-pc)
6. [Obtener archivos de audio con tráfico APRS real](#6-obtener-archivos-de-audio-con-tráfico-aprs-real)
7. [Ajustar el nivel de audio](#7-ajustar-el-nivel-de-audio) ([calibración automática de volumen](#71-calibración-automática-de-volumen), [normalización](#72-normalización-y-ganancias-por-encima-de-10))
8. [Primera ejecución: verificar toda la cadena con paquetes sintéticos](#8-primera-ejecución-verificar-toda-la-cadena-con-paquetes-sintéticos)
9. [Ejecutar la prueba real](#9-ejecutar-la-prueba-real)
10. [Referencia de la línea de comandos](#10-referencia-de-la-línea-de-comandos) ([idioma](#101-idioma), [interfaz gráfica](#102-interfaz-gráfica---gui), [autoprueba](#103-autoprueba---selftest))
11. [Interpretar los resultados](#11-interpretar-los-resultados)
12. [Cómo se comparan los paquetes](#12-cómo-se-comparan-los-paquetes)
13. [Plan de pruebas sugerido con las pistas de WA8LMF](#13-plan-de-pruebas-sugerido-con-las-pistas-de-wa8lmf)
14. [`gen_test_wav.py` en detalle](#14-gen_test_wavpy-en-detalle)
15. [Solución de problemas](#15-solución-de-problemas)
16. [Limitaciones](#16-limitaciones)
17. [Hoja de referencia rápida](#17-hoja-de-referencia-rápida)
18. [Glosario](#18-glosario)

---

## 1. Qué hace esta prueba

El firmware del ESP32 decodifica paquetes APRS a partir de una señal de audio en
su entrada ADC e imprime cada paquete decodificado en su consola serie
(`RX: SRC>DST,PATH:payload`). La pregunta es: **de todos los paquetes que
realmente hay en el audio, ¿cuántos decodificó el ESP32 y los decodificó
correctamente?**

El programa lo responde enviando **el mismo audio a dos decodificadores al mismo
tiempo**:

```
                       ┌─► sox (remuestreo a 22050 Hz, mono) ─► multimon-ng ───────────► paquetes A  (referencia)
                       │                                                                            │
 Archivo WAV ──────────┤                                                                            ▼
 (tráfico APRS real)   │                                                                   test_aprs_wavs.py
                       │                                                                     compara A y B
                       │                                                                            ▲
                       └─► play ─► placa de sonido de la PC ─► RV1 + C1 ─► ADC del ESP32 (GPIO33)   │
                                                                   │                                │
                                                          módem AFSK del ESP32                      │
                                                                   │                                │
                                                           log de consola ─► serie USB ─────────────┴► paquetes B
```

* **multimon-ng** es un conocido decodificador por software. Lo que decodifica
  del archivo se usa como *la lista de paquetes que realmente están allí*.
* El **ESP32** oye el mismo audio a través de la placa de sonido y de un pequeño
  circuito atenuador/de acoplamiento, e informa lo que decodificó por el puerto serie.
* Para cada paquete que decodifica multimon-ng, el programa busca el mismo paquete
  en el log del ESP32. Imprime el paquete junto con un veredicto:
  **OK**, **DIFFERENT** (distinto) o **NOT DECODED** (no decodificado).
* Al final imprime un resumen: total de paquetes, porcentaje decodificado
  correctamente, porcentaje decodificado con contenido distinto y porcentaje faltante.

**Se puede usar cualquier archivo WAV con tráfico APRS real (AFSK 1200 baudios)**:
una grabación de un equipo de radio, de una SDR o una pista de prueba publicada. La
sección 6 explica dónde conseguir buenos archivos y propone las pistas de prueba
gratuitas de **WA8LMF**.

La prueba es **solo de recepción**: no se transmite nada y no hace falta conectar
nada del ESP32 hacia la PC.

---

## 2. Qué necesita

### Hardware

| Elemento | Notas |
|---|---|
| Placa ESP32 con el firmware esp32idf_APRS | Alimentada y conectada a la PC por USB (este enlace USB es también la consola serie). |
| PC con salida de placa de sonido | Salida de auriculares o de línea. Una placa de sonido USB económica y dedicada es una buena idea (ver sección 5). |
| Cable de audio, plug de 3,5 mm | Punta = canal izquierdo, manga = masa. Sirve cualquiera de los dos canales: el programa envía el mismo audio a ambos. |
| RV1: trimmer multivuelta de 2 kΩ | Ajusta el nivel. El multivuelta permite un ajuste fino. **Opcional** — la sección 3.1 da una alternativa con resistencias fijas si no tiene un trimmer. |
| C1: condensador de 1 µF o mayor | Puede ser electrolítico (¡cuide la polaridad!), de 10 V o más. |
| Cables de conexión | |

### Software (Linux)

El programa está escrito para **Linux con ALSA** y necesita:

| Software | Para qué | Instalación (Debian/Ubuntu) |
|---|---|---|
| Python 3.7+ | ejecuta el programa | normalmente ya está instalado |
| pyserial | lee la consola del ESP32 | `sudo apt install python3-serial` |
| multimon-ng | decodificador de referencia | `sudo apt install multimon-ng` |
| sox (con `play`) | reproduce el WAV y convierte el audio | `sudo apt install sox libsox-fmt-all` |
| alsa-utils | `aplay -l` para encontrar la placa de sonido | `sudo apt install alsa-utils` |
| coreutils (`stdbuf`) | **muy recomendable.** Sin él la salida de multimon-ng queda con búfer de bloque en la tubería, sus paquetes llegan a ráfagas y se marcan con una hora tardía, lo que produce veredictos *NOT DECODED* falsos. El programa avisa al arrancar si falta. | normalmente ya está instalado |
| python3-tk | solo para la interfaz gráfica (`--gui`, sección 10.2) | `sudo apt install python3-tk` |

Instalar todo de una vez:

```bash
sudo apt install python3-serial multimon-ng sox libsox-fmt-all alsa-utils coreutils python3-tk
```

(Si prefiere pip: `pip install pyserial`, dentro de un entorno virtual en las
distribuciones recientes.)

---

## 3. Construir el cable de audio (hardware)

![Circuito mínimo de entrada de audio](esp32_audio_input.png)

*La imagen es `esp32_audio_input.png`, en este mismo directorio. Sus rótulos están
en inglés: Tip = punta, Sleeve = manga, wiper = cursor, Ground = masa.*

### Qué hace el circuito

* La salida de la placa de sonido de la PC es una señal que oscila **tanto por
  encima como por debajo de 0 V** (puede llegar a más o menos 1 V o más). El ADC
  del ESP32 solo acepta **de 0 a 3,3 V** y nunca debe recibir una tensión negativa.
* **RV1** (el trimmer) es un **control de volumen / divisor de tensión** sobre la
  salida de la PC. Su cursor toma solo una fracción de la señal.
* **C1** (el condensador) **bloquea la continua** y deja pasar el audio. Del lado
  del ESP32 se conecta a GPIO33, cuyo nivel de continua lo fija el propio ESP32.
* El ESP32 puede polarizar su propio pin de ADC: conecta las **resistencias
  internas pull-up y pull-down** del pad para que el pin quede en reposo en
  aproximadamente **1,65 V** (la mitad de 3,3 V). El audio viaja entonces sobre
  ese nivel de continua. Esta es la opción del firmware **ADC input self-bias**
  (sección 4). Por eso todo el circuito no necesita resistencias propias.
* La **masa** del jack (manga) va a **GND** del ESP32.

### Conexión, paso a paso

Apague todo primero.

1. **Punta del jack** → extremo superior de **RV1**.
2. **Manga del jack** → extremo inferior de **RV1** **y** **GND** del ESP32.
3. **Cursor de RV1** (el terminal del medio) → **C1, lado menos (−)**.
4. **C1, lado más (+)** → **GPIO33** del ESP32.

### Polaridad del condensador (importante si C1 es electrolítico)

Un condensador electrolítico tiene **polaridad**. GPIO33 queda en reposo en
aproximadamente 1,65 V y el lado de la PC en aproximadamente 0 V, por lo que el
lado del ESP32 es el positivo:

| Terminal de C1 | Va a | Cómo reconocerlo |
|---|---|---|
| **+** (más) | **GPIO33** (ESP32) | el terminal **más largo** de una pieza nueva |
| **−** (menos) | **cursor de RV1** (lado PC) | el terminal bajo la **franja con signos menos** impresa en el cuerpo |

En el dibujo, la placa recta (marcada +) mira hacia GPIO33 y la placa curva
(marcada −) mira hacia el cursor del trimmer. Conectarlo al revés hace que tenga
fugas y agregue ruido. Un condensador cerámico o de película de 1 µF no tiene
polaridad, si dispone de uno.

### Reglas para los componentes

* **C1 debe ser de 1 µF o mayor.** Con un condensador pequeño (por ejemplo 100 nF)
  el circuito se convierte en un filtro pasa-altos que empieza a cortar alrededor de
  700 Hz y debilita el tono de 1200 Hz, lo que perjudica la decodificación. Uno
  mayor está bien (de 1 µF a 10 µF).
* **El trimmer debe estar *antes* del condensador**, como en el dibujo. Si RV1 se
  pusiera entre el condensador y el pin, su pata inferior conectaría GPIO33 a masa
  y destruiría la polarización.
* **RV1 = 2 kΩ** es un valor bajo. Presenta una carga de 2 kΩ a la salida de la PC,
  que la mayoría de las salidas tipo auricular soportan sin problema. Si el sonido
  de la salida de la PC se distorsiona, use un trimmer de 10 kΩ (la documentación
  del firmware recomienda 10 kΩ, sobre todo para mantener liviana la carga sobre la
  fuente).
* **El pin del ESP32 no tiene diodos de protección aquí.** RV1 es lo único que
  limita lo que llega a GPIO33. Antes de conectar, ponga el volumen de la PC bajo y
  RV1 cerca del extremo de masa (sección 7). Una salida de auriculares a volumen
  máximo puede oscilar muy por encima de los 0 – 3,3 V que tolera el pin.
* El esquema de referencia del firmware (la interfaz RX completa de la
  documentación del proyecto) agrega dos **diodos de fijación, D1/D2**, que
  mantienen el pin del ADC dentro de los rieles de alimentación, además de un
  pequeño snubber R/C. Este circuito mínimo los omite a propósito: el firmware se
  apoya entonces en la opción **Warn on receive over-range**. Si va a dejar el
  banco conectado mucho tiempo, o usa una fuente fuerte, considere agregar los
  diodos de fijación como se dibujan en ese esquema.
* Solo **GPIO32 y GPIO33** pueden usar la autopolarización incorporada. La entrada
  predeterminada del firmware es GPIO33; si cambió el pin del ADC en su
  compilación, use ese pin (debe ser GPIO32 o GPIO33 para este circuito).

### 3.1 Alternativa: resistencias fijas en lugar del trimmer

Si no dispone de un trimmer de 2 kΩ, RV1 puede reemplazarse por **dos
resistencias fijas** conectadas como un divisor de tensión permanente. Se
pierde la posibilidad de girar una perilla, pero la sección 7.1 muestra cómo la
**calibración automática de volumen** del propio programa compensa eso por
software.

![Alternativa con resistencias fijas a RV1](esp32_audio_input_fixed.png)

*La imagen es `esp32_audio_input_fixed.png`, en este mismo directorio: el mismo
circuito de la sección 3, con el trimmer reemplazado por el par fijo R1/R2. Sus
rótulos están en inglés.*

Dicho de forma más simple: **R1** va desde la **punta** del jack hasta un nodo
intermedio; **R2** va desde ese mismo nodo intermedio hasta la **manga** del
jack (masa, unida a GND del ESP32). El nodo intermedio —donde se juntan R1 y
R2— reemplaza al cursor del trimmer y va al **lado menos (−) de C1**,
exactamente como en los pasos de conexión de más arriba. Todo lo demás (C1, la
conexión a GPIO33, la masa compartida) queda igual.

* **Valores sugeridos para empezar: R1 = 4,7 kΩ, R2 = 1 kΩ.** Esto divide la
  salida de la PC por aproximadamente 5,7×, presentando una carga liviana de
  ≈5,7 kΩ. Es solo un punto de partida: el nivel de salida de línea varía mucho
  entre placas de sonido, así que la tensión que realmente llega a GPIO33
  sigue dependiendo del volumen que fije la PC.
* **Este divisor es fijo — no se puede ajustar como un trimmer.** Use el
  control de volumen de la PC para el ajuste grueso (como en la sección 7), y
  deje que la ganancia por software `--volume` (aplicada automáticamente por la
  calibración automática de volumen descrita en la sección 7.1, salvo que
  indique `--no_auto_volume`) se encargue del ajuste fino. Es exactamente la
  situación para la que existe esa función de calibración.
* Si, incluso con el volumen de la PC bajo, el nivel siempre resulta demasiado
  alto (over-range) o siempre demasiado bajo (valores en bruto pegados a 0 o a
  4095, algo que `--volume` por sí solo no puede corregir), cambie a un divisor
  con más o menos atenuación — por ejemplo R1 = 10 kΩ / R2 = 1 kΩ (más
  atenuación) o R1 = 2,2 kΩ / R2 = 1 kΩ (menos) — y vuelva a comprobar con RX
  LEVEL (sección 7) o con otra pasada de calibración automática.
* Un trimmer sigue siendo la opción más cómoda si piensa reutilizar el banco de
  pruebas con distintas placas de sonido o grabaciones: permite fijar el nivel
  analógico una sola vez, en hardware, en lugar de depender cada vez de la
  ganancia por software.

---

## 4. Preparar el ESP32 (ajustes del firmware)

Conéctese al punto de acceso Wi-Fi del ESP32 (SSID de fábrica `esp32idf_APRS`,
contraseña `esp32idf_APRS`) y abra `http://192.168.4.1/` en un navegador (usuario
de fábrica `admin` / `admin`, salvo que lo haya cambiado). La interfaz puede
mostrarse en varios idiomas; abajo se dan las etiquetas en español.

### 4.1 Ajustes de audio — página *Radiomódem*

| Etiqueta | Ajustar a |
|---|---|
| Activar módem ADC/DAC de audio | **ON** (activado) |
| Interfaz de audio → Polarización interna de la entrada del ADC | **ON** (esto es lo que hace funcionar el circuito acoplado con condensador) |
| Interfaz de audio → Avisar cuando el audio recibido se sale de rango | **ON** (avisa cuando la señal es demasiado fuerte; el circuito no tiene diodos de fijación) |

Guarde (reinicie el ESP32 si la página lo pide).

### 4.2 ⚠ Ajustes de seguridad — hágalos ANTES de reproducir cualquier tráfico grabado

El tráfico grabado del aire contiene **indicativos y posiciones reales**. Las
pistas de prueba de WA8LMF incluyen incluso su propio indicativo en las balizas, y
él advierte expresamente que, si se envían a un igate, generan falsos reportes de
dónde estuvo hace décadas. Su ESP32 tiene un firmware de **IGate/digirepetidor**,
así que asegúrese de que nada de lo decodificado a partir de una grabación pueda
salir del ESP32:

| Página del menú | Etiqueta | Ajustar a |
|---|---|---|
| IGate | **Habilitar IGate** | **OFF** (desactivado) |
| IGate | **RF a Internet** | **OFF** |
| Digirepetidor (*Digipeater*) | **Habilitar Digipeater** | **OFF** |
| Páginas de balizas | habilitación de baliza / tracker / clima / telemetría | **OFF** |

Seguridad adicional, recomendada:

* Deje el ESP32 en su modo de fábrica de **solo punto de acceso Wi-Fi** (sin
  conexión Station/cliente), de modo que **no tenga ningún camino a Internet**
  durante la prueba.
* **No conecte un transmisor ni un equipo de radio** al ESP32 durante la prueba.

Las líneas de recepción que necesita en la consola (`RX: …`) se siguen imprimiendo
con el IGate desactivado: la documentación indica que el log de una estación solo
de recepción queda restringido por los filtros, no vacío. Si alguna vez no ve
líneas `RX:` con el IGate desactivado, consulte la
[Solución de problemas](#15-solución-de-problemas).

### 4.3 Ajustes del log

| Etiqueta | Ajustar a | Por qué |
|---|---|---|---|
| Página IGate → **Registrar después de los filtros** | **OFF** (el valor predeterminado) | Si está en ON, la consola solo imprime los paquetes que pasan los filtros del IGate, y todo paquete filtrado se contaría como *NOT DECODED*. |

El nivel de log de la consola debe ser **INFO** (el predeterminado del firmware),
que es el nivel que imprime las líneas `RX:`.

> **Requisito del firmware:** el firmware debe **escapar los bytes no imprimibles
> —sobre todo LF (0x0A)— antes de registrar la línea `RX:`**. El banco parte el
> flujo de la consola solo por LF (partir por CR truncaría las cargas útiles que
> legítimamente contienen CR), así que un LF crudo dentro de un campo de
> información corta la línea de consola en dos y la trama se puntúa como
> diferencia de contenido aunque el demodulador haya acertado. Las cargas Mic-E,
> de telemetría y las casi binarias son las que caen en esto.

### 4.4 La consola

El puerto USB del ESP32 es también la consola serie: **115200 baudios, 8 bits de
datos, sin paridad, 1 bit de parada (8N1)**. Solo un programa puede usar el puerto
serie a la vez, así que **cierre** `idf.py monitor`, minicom, screen, PuTTY, etc.
antes de ejecutar la prueba.

---

## 5. Preparar la PC

### 5.1 Permiso del puerto serie

En Debian/Ubuntu su usuario debe pertenecer al grupo `dialout`:

```bash
sudo usermod -aG dialout $USER
# luego cierre sesión y vuelva a iniciarla
```

Encuentre el puerto del ESP32:

```bash
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
```

El programa usa `/dev/ttyUSB0` por defecto; use `--serial_port` para otro.

### 5.2 Encontrar la placa de sonido que alimenta al ESP32

```bash
./test_aprs_wavs.py --list_audio      # igual que: aplay -l
```

Ejemplo de salida:

```
card 0: PCH [HDA Intel PCH], device 0: ALC3246 Analog [ALC3246 Analog]
card 1: Device [USB Audio Device], device 0: USB Audio [USB Audio]
```

La placa de sonido USB de arriba es la **tarjeta 1, dispositivo 0**, que en ALSA se
llama `hw:1,0`. La pasará como `--audio_device hw:1,0`. Sin `--audio_device` se
usa la salida predeterminada del sistema.

### 5.3 Consejos sobre la placa de sonido (influyen en los resultados)

* **Use una placa de sonido USB dedicada** si es posible. Una económica sirve.
  Mantiene los sonidos del escritorio fuera de la prueba.
* **No deje que suenen sonidos del sistema** durante una prueba: un aviso de
  notificación pasa por la misma salida y entra en el ESP32, y puede corromper un
  paquete. Desactive los sonidos de notificación o use una placa dedicada que el
  escritorio no utilice.
* Desactive cualquier **mejora de audio**, ecualizador, sonoridad (loudness) o
  normalización de volumen en la configuración de sonido.
* Ajuste el volumen del mezclador **una sola vez** y no lo toque durante una
  ejecución. El nivel de audio en el ESP32 depende de él (sección 7). `alsamixer`
  (elija la placa de sonido con F6) es la herramienta habitual.
* Reproduzca solo audio **sin pérdidas** (WAV/FLAC). WA8LMF señala que la
  compresión con pérdidas, como MP3, deforma las formas de onda de los datos, y que
  los sistemas de sonido económicos basados en software pueden introducir errores
  de temporización o una frecuencia de muestreo de reproducción incorrecta. Si los
  resultados se ven extraños, cambiar de placa de sonido es lo primero que conviene
  probar.
* No use audio Bluetooth.

---

## 6. Obtener archivos de audio con tráfico APRS real

### 6.1 Qué archivos se pueden usar

**Cualquier archivo WAV que contenga tráfico APRS en AFSK a 1200 baudios** (la
modulación normal de APRS en VHF, tonos Bell 202 de 1200/2200 Hz). El programa
acepta cualquier:

* frecuencia de muestreo (8000, 11025, 22050, 44100, 48000 Hz …),
* mono o estéreo,
* profundidad de bits (8, 16, 24 bits).

`sox` convierte el audio para multimon-ng (22050 Hz, mono, 16 bits) de forma
automática.

Reglas:

* Si el WAV es **estéreo**, solo su **canal izquierdo** se envía al ESP32 (el
  decodificador de referencia recibe la mezcla de ambos). Convierta primero a mono
  las grabaciones estéreo cuyos dos canales difieran: `sox in.wav -c 1 mono.wav`.
* La extensión debe ser `.wav` (también se aceptan `.WAV` y `.Wav`). El programa
  mira solo esos archivos **directamente dentro** del directorio que se le indique
  (no busca en subdirectorios). FLAC, MP3, etc. deben convertirse antes
  (sección 6.3).
* El audio debe ser **sin pérdidas** y **sin recortes** (clipping).
* El archivo puede tener cualquier duración. Los archivos se procesan **uno tras
  otro, en orden alfabético** (mayúsculas y minúsculas se ordenan juntas), y cada uno se reproduce en **tiempo real** (un
  archivo de 25 minutos tarda 25 minutos).
* Mejor audio: tomado directamente de la **salida del discriminador / de "datos"**
  de un receptor. El audio tomado del **parlante** está desenfatizado; también
  funciona, pero la decodificación es algo distinta. Probar ambos es útil.

### 6.2 Fuente recomendada: las pistas de prueba de TNC de WA8LMF

**WA8LMF** publica en **<http://www.wa8lmf.net/TNCtest/>** un "TNC Test CD"
gratuito, creado para comparar TNC de paquetes de 1200 baudios "bajo fuego" en
condiciones reales. Contiene grabaciones reales del aire, lo que lo hace un
conjunto de pruebas ideal para este banco.

**Se ofrecen dos versiones; ambas tienen audio idéntico:**

| Versión | Archivo | Tamaño | Formato |
|---|---|---|---|
| 2.0 (recomendada) | <http://www.wa8lmf.net/TNCtest/TNC_Test_2.iso> | ≈ 250 MB | Imagen de CD-ROM con archivos **FLAC** sin pérdidas |
| 1.1 | <http://www.wa8lmf.net/TNCtest/TNC_Test_CD_Ver-1.1.zip> | ≈ 530 MB | Imagen de CD de audio (BIN/CUE): hay que grabarla o extraerla |

Use la **versión 2.0**: el audio ya está en archivos.

**Las pistas** (según la descripción de esa página):

| Pista | Qué es | Duración | Paquetes | ¿Usarla aquí? |
|---|---|---|---|---|
| **1** | Tráfico real en **144,39 MHz, Los Ángeles**, en la hora pico de la tarde: el canal está saturado. 40 minutos de actividad con las pausas eliminadas, comprimidos a ≈ 25 minutos. Audio del discriminador, **sin** desénfasis. Tiene señales con sobre y subdesviación, colisiones, paquetes consecutivos casi sin pausa, trackers con NMEA en crudo, TinyTraks, identificación en CW dentro de paquetes … | ≈ 25 min | muchos (cientos) | **Sí — la prueba de esfuerzo principal** |
| **2** | Mismo contenido que la pista 3 pero **con desénfasis** (simula el audio tomado del parlante / control de volumen de un receptor). | ≈ 5 min | 100 | **Sí** — compárela con la pista 3 |
| **3** | Un reporte de posición **Mic-E de un Kenwood D700**, limpio, tomado de un monitor de servicio, copiado 100 veces: **20 ráfagas por minuto durante 5 minutos = exactamente 100 paquetes idénticos**. | ≈ 5 min | exactamente **100** | **Sí — da un porcentaje exacto** |
| **4** | 25 minutos de **un D700 móvil emitiendo una baliza cada 12 s** mientras conduce, en un canal tranquilo, a 8–10 millas del receptor: flutter, multitrayecto, señales débiles. La página dice que varios paquetes se oyen pero no se decodifican con el motor de paquetes AGW. | ≈ 25 min | hasta ≈ 125 enviados | **Sí — prueba de señal débil** |
| 5, 6, 7 | Tonos alternados de 1200/2200 Hz del modo "CAL" de un KPC3+: planos, con desénfasis, con preénfasis. Sirven para *alinear* TNC (relación de niveles de los tonos). | ≈ 1 min cada una | **0** | **No** — no contienen paquetes. Manténgalas fuera del directorio de pruebas. |

También, según la página: cada pista empieza y termina con tonos DTMF de
referencia (la pista 1 empieza con "1" y termina con "6", la pista 2 con "2"/"7",
y así sucesivamente). Son inofensivos.

> **⚠ Advertencia de WA8LMF:** *no reproduzca estas pistas por el aire.* Su
> indicativo está incrustado en las balizas y serían enviadas a un igate,
> generando falsos reportes de posición. **Este banco nunca transmite**, pero
> mantenga desactivado el IGate del ESP32 como se describe en la sección 4.2,
> exactamente por el mismo motivo.

Estas pistas son material de terceros publicado por WA8LMF para pruebas; no se
distribuyen con este banco. Cite la fuente si publica resultados.

### 6.3 Descargar, extraer y convertir (versión 2.0)

```bash
mkdir -p ~/audio_test && cd ~/audio_test

# 1) descargar (≈ 250 MB)
wget http://www.wa8lmf.net/TNCtest/TNC_Test_2.iso

# 2) abrir la ISO — o bien montarla (requiere sudo) ...
mkdir -p TNC_Test_2
sudo mount -o loop,ro TNC_Test_2.iso TNC_Test_2
#    ... o extraerla sin root:  sudo apt install p7zip-full
# 7z x TNC_Test_2.iso -oTNC_Test_2

# 3) ver los archivos FLAC
find TNC_Test_2 -iname '*.flac'
```

Los archivos FLAC de la ISO llevan el nombre de las pistas y empiezan con el
número de pista, así que el comando `find` de arriba muestra sus nombres reales
(el primero es la pista de 25 minutos de tráfico de Los Ángeles). Luego
conviértalos todos a WAV:

```bash
mkdir -p Audio-Tracks
find TNC_Test_2 -iname '*.flac' | while IFS= read -r f; do
    sox "$f" "Audio-Tracks/$(basename "$f" .flac).wav"
done
ls -l Audio-Tracks
```

Alternativas a `sox`: `ffmpeg -i in.flac out.wav` o `flac -d in.flac`.

Desmonte al terminar: `sudo umount TNC_Test_2`.

Como los archivos se procesan en orden alfabético y empiezan con el número de
pista, se ejecutan en el orden de las pistas. **Saque las pistas 5, 6 y 7** de
`Audio-Tracks/` (por ejemplo a `Audio-Tracks/skip/`, que el programa ignora): no
tienen paquetes.

### 6.4 Grabar su propio tráfico (opcional)

Sirve cualquier grabación de tráfico APRS real. Dos formas habituales:

* **Desde un equipo de radio:** conecte la salida del discriminador/de datos del
  equipo (o la salida del parlante a través de un atenuador) a la entrada de la
  placa de sonido de la PC y grabe con Audacity: **mono, 22050 o 44100 Hz, 16
  bits**, con el nivel de entrada ajustado para que los picos queden bien por
  debajo del máximo (sin recortes) y cualquier control automático de ganancia /
  reducción de ruido **desactivado**. Exporte como WAV.
* **Desde una RTL-SDR** (no probado en este proyecto — verifique las opciones de
  su versión de `rtl_fm`). Use la frecuencia de APRS de **su región** (144,390 MHz
  en Norteamérica; otras regiones difieren):

  ```bash
  rtl_fm -M fm -f 144.390M -s 22050 - | sox -t raw -r 22050 -e signed -b 16 -c 1 - capture.wav
  ```

Grabe sesiones largas (una hora o más) en horarios de mucho tráfico; cuantos más
paquetes, más significativos son los porcentajes.

---

## 7. Ajustar el nivel de audio

El ESP32 necesita la cantidad correcta de señal. **Muy poca** y no puede
decodificar; **demasiada** y la señal se recorta (y la entrada no tiene diodos de
protección).

**Antes de conectar el cable:** volumen de la PC bajo (alrededor del 30 %) y RV1
girado por completo hacia el **extremo de masa** (señal mínima).

Necesita un segundo dispositivo en la página web del ESP32 mientras suena el audio:
un teléfono o la propia PC, conectados al punto de acceso del ESP32 (sección 4). La
página web funciona al mismo tiempo que la conexión serie USB.

**Paso a paso**

1. Abra la página **Radiomódem** del ESP32. Junto al botón **PRUEBA DE BUCLE**
   (interfaz en inglés: **LOOP TEST**) encontrará **NIVEL RX** (en inglés:
   **RX LEVEL**). **Mide sin transmitir**.
2. Reproduzca una grabación real **de forma continua**. Use una con mucha
   actividad, como la pista 1 de WA8LMF (sus paquetes van casi seguidos, así que el
   nivel es estable). Puede usar el propio programa de prueba (los veredictos no
   importan ahora; deténgalo con Ctrl-C cuando termine el ajuste):
   ```bash
   ./test_aprs_wavs.py --wav_dir one1 --audio_device hw:1,0
   ```
   o un reproductor simple:
   ```bash
   AUDIODRIVER=alsa AUDIODEV=hw:1,0 play -q Audio-Tracks/01_*.wav remix 1 1
   ```
   ¿Todavía no tiene una grabación real? Reproduzca en bucle el archivo sintético
   (sección 8) durante un rato (el `repeat 50` de sox lo reproduce 50 veces
   adicionales, unos 5 minutos en total):
   ```bash
   AUDIODRIVER=alsa AUDIODEV=hw:1,0 play -q Synthetic/sample.wav remix 1 1 repeat 50
   ```
3. Mientras suena, pulse **NIVEL RX**. El botón primero **guarda la página tal
   como se muestra** (para que el módem use los ajustes en pantalla), luego observa
   el receptor durante **1 segundo** y muestra el resultado en verde junto a los
   botones, por ejemplo:
   ```
   312 mV RMS (peak 640), DC 1650 mV, AGC 1.00x, raw 1810..2390, DCD no
   ```
   Es una instantánea de un segundo, así que púlselo **varias veces**; con audio en
   ráfagas una lectura puede caer en una pausa entre paquetes. Use los valores más
   altos y estables.
4. Suba RV1 en pasos pequeños (unas pocas vueltas del trimmer multivuelta),
   pulsando **NIVEL RX** después de cada uno, hasta cumplir los tres objetivos:

   | Lectura | Objetivo |
   |---|---|
   | Nivel de RX (`mV RMS`) | **250 – 350 mV RMS** |
   | rango bruto (`raw min..max`) | cómodamente **lejos de 0 y 4095** (los extremos del ADC de 12 bits) |
   | nivel de continua (`DC … mV`) | **1200 – 2000 mV** (confirma que la autopolarización funciona; unos 1650 mV es lo ideal) |

5. **No toque el volumen de la PC ni RV1** durante el resto de la sesión. Si cambia
   la placa de sonido, el volumen, o reproduce una grabación con un nivel muy
   distinto (por ejemplo la pista 2, con desénfasis), vuelva a comprobarlo.

Si aparece el **aviso de sobrerrango**, el nivel es demasiado alto: baje RV1 (o el
volumen de la PC). Si el nivel de continua está cerca de 0 mV o de 3300 mV, la
autopolarización no está activada (sección 4.1) o C1 está conectado al revés / no
está.

El programa también tiene `--volume`, una ganancia por software solo para el
camino hacia el ESP32 (predeterminado 1.0). Prefiera RV1 para el ajuste principal y
use `--volume` para correcciones pequeñas (por ejemplo `--volume 0.8`); valores
mayores que 1.0 pueden recortar.

### 7.1 Calibración automática de volumen

`--volume` no es solo un número fijo que se ajusta una vez: salvo que pase
`--no_auto_volume`, **toda ejecución real de la prueba empieza con una búsqueda
automática de la mejor ganancia por software**, antes incluso de reproducir los
paquetes que terminarán en su informe. Esto está activado de manera
predeterminada, así que ocurre lo haya pedido o no — conviene saberlo, porque
agrega tiempo antes de la prueba que en realidad quería ver.

**No es una escalada de colina y no busca el "mejor" porcentaje.** La tasa de
decodificación frente al nivel de entrada no es un pico, es una **meseta**:
demasiado bajo y el demodulador pelea contra el piso de ruido y la propia
cuantización del ADC; demasiado alto y el ADC recorta (y el propio firmware lo
avisa); entre esas dos rodillas la tasa es plana. Quedarse con el puntaje más
alto de una curva plana y ruidosa es quedarse con el ruido: con 50 paquetes, el
intervalo del 95 % alrededor del 90 % es de unos ±8 puntos porcentuales, así que
un volumen "2 % mejor" es medio paquete de suerte. Lo que vale la pena encontrar
es el **centro de la meseta**, porque es el nivel con más margen a ambos lados.

La búsqueda tiene cuatro fases, gobernadas por una regla estricta:

> **El sobrerrango es un techo.** En cuanto el firmware imprime `afsk: RX audio is
> over-range` durante un sondeo —**basta una sola advertencia** por defecto—, esa
> ganancia pasa a ser un techo: **ningún sondeo vuelve a reproducirse en ella ni
> por encima**, y la búsqueda solo se mueve **hacia abajo**, en pasos pequeños de
> `--clip_step_db` (0,5 dB por defecto). Además, la reproducción del sondeo
> sobreexcitado se corta a un cuarto de segundo de la advertencia, para no seguir
> sobreexcitando el ADC —que no tiene diodos de protección— el resto del archivo.


| Fase | Qué hace |
|---|---|
| **1 — subir** | Solo mientras el firmware **nunca** informó sobrerrango: sube la ganancia en pasos de 6 dB, con sondeos baratos de **8 paquetes** (el recorte es una señal binaria que el propio firmware informa, así que no necesita un lote completo). Un sondeo que no decodifica nada también detiene la subida: no hay ninguna evidencia de que un nivel más alto sea seguro. |
| **2 — bajar en pasos** | Desde el primer sobrerrango: nunca más hacia arriba. Baja `--clip_step_db` (0,5 dB) por sondeo hasta que uno vuelva limpio. Ese nivel —el *nivel más alto sin sobrerrango*— es la referencia de las dos fases siguientes. Si ya se empieza con sobrerrango (un `--volume` demasiado alto), la búsqueda pasa directamente a esta fase. |
| **3 — puntuar** | Puntúa lotes completos de `--auto_volume_batch` paquetes (50 por defecto) a **3, 6, 9, 12 y 18 dB por debajo de ese nivel**, y se detiene en cuanto un puntaje cae más de 15 puntos por debajo del mejor (ya pasó la rodilla inferior: no hace falta seguir bajando). Un sondeo de puntuación que informa sobrerrango —advertencias demasiado raras para aparecer en 8 paquetes pueden aparecer en 50— vuelve a bajar el techo y se descarta de la meseta. |
| **4 — centrar** | Todos los puntos estadísticamente empatados con el mejor —su intervalo de [Wilson](#18-glosario) todavía se solapa— forman la meseta. El **centro geométrico** de la meseta (el centro aritmético en dB) es la ganancia que se usa en la ejecución, limitada a estar al menos **3 dB por debajo del techo de sobrerrango**. |

**Cómo se puntúa un sondeo:** aciertos = **OK + EXTRA**, intentos = paquetes de
multimon-ng + paquetes EXTRA.

* **EXTRA cuenta como acierto.** Una trama que el ESP32 decodificó y multimon-ng
  perdió por completo es la prueba más fuerte que puede dar un nivel: el firmware
  le ganó al decodificador de referencia en esa trama.
* **DIFFERENT y HDR-CORRUPT cuentan como fallos**, no como aciertos. El objetivo
  del banco es la igualdad de contenido, así que un nivel que produce cargas
  útiles corruptas no debe puntuar como uno que las produce correctas.

Otras cosas que conviene saber:

* **El presupuesto se cuenta en lotes de paquetes, no en llamadas de sondeo.**
  `--auto_volume_max_rounds` (10 por defecto) es cuántos lotes del tamaño de
  `--auto_volume_batch` puede gastar toda la búsqueda; un sondeo de recorte de 8
  paquetes cuesta 8/50 de ronda y un sondeo de puntuación completo cuesta una. Como
  mucho la **mitad** del presupuesto va a la subida y al descenso, de modo que los
  sondeos baratos nunca pueden dejar sin recursos al barrido que realmente elige el
  nivel. Pasos muy pequeños desde un inicio muy alto pueden agotar el presupuesto de
  descenso: la búsqueda lo informa y recurre a techo − `--headroom_db`, que sigue
  por debajo de todo nivel que haya informado sobrerrango.
* `--clip_rate` (**0** por defecto) es cuántas advertencias por paquete puede
  producir un nivel y seguir contando como limpio. Súbalo solo si su firmware emite
  advertencias espurias aisladas; con cualquier valor mayor que 0 se desactiva el
  corte a mitad de archivo, porque entonces la tasa hay que medirla en todo el sondeo.
* Las ganancias se manejan **en dB** (el nivel se aplica con el `gain` de sox) y se
  mantienen dentro de `--volume_min` … `--volume_max` (**0,02 – 4,0** por defecto).
  Las ganancias por encima de 1,0 solo tienen sentido junto con `--normalise`
  (sección 7.2).
* Cada medición se **cachea por dB redondeado**, así que ningún nivel se sondea dos
  veces.
* Los sondeos avanzan por el conjunto de WAV **en turnos rotativos**, en vez de
  reiniciar siempre en el primer archivo, para que un conjunto cuyo primer archivo
  contenga más de un lote no acabe con el nivel ajustado sobre una sola grabación.
  Un sondeo se rinde tras `--max_passes` pasadas (3 por defecto) por el conjunto e
  informa `probe incomplete: n/N packet(s) after 3 pass(es)`: eso es un problema de
  enrutamiento del audio o de los archivos, no de nivel.
* Si **no se decodifica nada a ningún nivel**, la ejecución cae en
  umbral − `--headroom_db` (6 dB por defecto). Si tampoco se pudo *puntuar* nada, se
  usa ese mismo valor de reserva.
* Si hizo falta más de 6 dB de atenuación **o** de amplificación, un `NOTE:` lo
  dice: eso es una afirmación sobre el hardware de la interfaz, no solo un número.
  Baje (o suba) RV1 y vuelva a ejecutar, para que el banco pueda trabajar cerca de
  0 dB — amplificar digitalmente también amplifica el piso de ruido de la placa de
  sonido.
* Interrumpir la calibración con **Ctrl-C** no detiene el programa: pasa a la
  prueba real con el **mejor nivel ya conocido sin sobrerrango** (nunca el
  `--volume` inicial si ese recortó) —que no es necesariamente el que elegiría una
  búsqueda completa—, así que lea el valor impreso en el resumen final antes de
  citar el resultado.
* La prueba real que sigue siempre **vuelve a empezar desde el primer archivo**.
* `--no_play` (la prueba en seco) también omite la calibración: nunca toca la placa
  de sonido.

Una pasada de calibración se ve así:

```
========================================================================
AUTO-VOLUME CALIBRATION (over-range ceiling + plateau centre)
========================================================================
  Start gain 1.000 (+0.0 dB), range 0.020..4.000, budget 10 probe(s), 50 packet(s) per scoring probe, 0.50 dB steps below over-range
  [probe  1, budget 0.2/10] gain=1.000 ( +0.0 dB)  mm=3 ok=2 diff=0 hdr=0 miss=1 extra=0  score=66.7%  clip=0.33/pkt  <- OVER-RANGE
  Over-range at +0.0 dB (gain 1.000): no probe will go that high again; stepping down in 0.50 dB steps
  [probe  2, budget 0.3/10] gain=0.944 ( -0.5 dB)  mm=2 ok=2 diff=0 hdr=0 miss=0 extra=0  score=100.0%  clip=0.50/pkt  <- OVER-RANGE
  [probe  3, budget 0.5/10] gain=0.891 ( -1.0 dB)  mm=8 ok=8 diff=0 hdr=0 miss=0 extra=0  score=100.0%  clip=0.00/pkt
  Highest level without over-range: -1.0 dB (gain 0.891)
  [probe  4, budget 1.5/10] gain=0.631 ( -4.0 dB)  mm=50 ok=49 diff=0 hdr=0 miss=1 extra=0  score=98.0%  clip=0.00/pkt
  [probe  5, budget 2.5/10] gain=0.447 ( -7.0 dB)  mm=49 ok=48 diff=0 hdr=0 miss=1 extra=1  score=98.0%  clip=0.00/pkt
  [probe  6, budget 3.5/10] gain=0.316 (-10.0 dB)  mm=50 ok=48 diff=1 hdr=0 miss=1 extra=0  score=96.0%  clip=0.00/pkt
  [probe  7, budget 4.5/10] gain=0.224 (-13.0 dB)  mm=50 ok=47 diff=0 hdr=0 miss=3 extra=0  score=94.0%  clip=0.00/pkt
  [probe  8, budget 5.5/10] gain=0.112 (-19.0 dB)  mm=50 ok=30 diff=0 hdr=0 miss=20 extra=0  score=60.0%  clip=0.00/pkt
      score fell 38 points below the best - the lower knee is past, no need to go quieter
  Plateau: -13.0 .. -4.0 dB (4 tied point(s) of 5 probed); best raw score 98.0%
  Chosen gain: 0.376 (-8.5 dB), 7.5 dB below the highest level without over-range
  NOTE: more than 6 dB of attenuation was needed. The hardware level into the ESP32 ADC is too hot - turn the RX trimmer (or the radio's volume) down and re-run, so the bench can work near 0 dB.
========================================================================
```

Los sondeos 1 y 2 se detienen tras unos pocos paquetes: la reproducción se corta en
cuanto el firmware se queja. Después del sondeo 1 nada vuelve a reproducirse a
0 dB o más, y después del sondeo 2 nada a −0,5 dB o más.

La ganancia elegida se vuelve a imprimir al final del resumen como
`Playback gain used for this test`. Anote ese número junto con el nivel que fijó
en RV1: si está comparando ejecuciones a lo largo del tiempo (sección 13) y quiere
que la cadena de audio sea idéntica entre ellas, pase el mismo valor con
`--volume X --no_auto_volume` en lugar de recalibrar cada vez.

Esto es también lo que hace práctica la alternativa con resistencias fijas de
la sección 3.1: sin un trimmer que girar, la ganancia por software de la
calibración es lo que absorbe la diferencia entre placas de sonido y ajustes de
volumen de la PC.

### 7.2 Normalización y ganancias por encima de 1,0

La ganancia se aplica **dentro de sox**, después de `remix 1 1`, como
`gain <dB>`. Eso importa cuando la ganancia supera 1,0: si `ganancia × el pico
propio del archivo` excede la escala completa, sox recorta *antes de que la placa
de sonido vea el audio*, y `-V0` oculta el propio aviso de recorte de sox. Por eso
el programa mide el pico de cada archivo (`sox <wav> -n stat`) y avisa:

```
  ! 01_40-Mins-Traffic-on-144.39.wav: gain 2.000 (+6.0 dB) on a file peaking at 0.800 would clip inside sox (max usable gain 1.250). Use --normalise, or lower the gain and raise the hardware level instead.
```

`--normalise` (también se acepta `--normalize`) inserta primero `gain -n -1`, de
modo que **cada archivo sale de la cadena a −1 dBFS** antes de aplicar la ganancia
de reproducción. De ahí se siguen dos cosas:

* **Una sola ganancia vale para grabaciones hechas a niveles distintos.** Es la
  opción a usar cuando el conjunto de WAV mezcla niveles —por ejemplo las pistas de
  WA8LMF, donde la pista 2 con deénfasis es mucho más débil que la 1.
* **Las ganancias por encima de 1,0 dejan de significar "recortar dentro de sox"**,
  que es lo que hace utilizable la mitad superior del rango de `--volume_max` (4,0
  por defecto).

Dos advertencias: normalizar cambia el nivel absoluto de cada archivo, así que una
lectura de NIVEL RX tomada en la sección 7 se tomó sobre otra señal — vuelva a
comprobarla una vez después de activarlo. Y la ganancia digital nunca sustituye a
un nivel analógico correcto: amplifica el piso de ruido de la placa de sonido junto
con la señal. El arreglo real para "muy bajo" es RV1.

---

## 8. Primera ejecución: verificar toda la cadena con paquetes sintéticos

Antes de dedicar 25 minutos a una grabación real, verifique que todo funciona
(cable, nivel, ajustes del ESP32, puerto serie) con un archivo de prueba diminuto y
**limpio**. El `gen_test_wav.py` incluido genera un archivo AFSK 1200 perfecto con
3 paquetes conocidos:

```bash
mkdir -p Synthetic
python3 gen_test_wav.py Synthetic/sample.wav
```

### 8.1 Prueba en seco — no requiere hardware

Esto usa solo multimon-ng y confirma que la parte de software funciona:

```bash
./test_aprs_wavs.py --wav_dir Synthetic --no_play
```

Resultado esperado: multimon-ng lista los 3 paquetes y el programa termina con
`DRY RUN finished: multimon-ng decoded 3 packet(s) in 1 file(s).` En este modo no
se usan ni el puerto serie ni la placa de sonido, y se omite la calibración
automática de volumen. Los paquetes se listan como líneas `000001 [multimon …]`,
sin veredicto, porque no hay respuesta del ESP32 con la cual compararlos. El
código de salida es 0 si se decodificó algún paquete y 2 si no se decodificó
ninguno.

Aun así deben estar instalados pyserial, multimon-ng y sox: pyserial se importa
al arrancar el programa, y multimon-ng y sox se comprueban antes que nada.
`play` **no** hace falta en una prueba en seco: el programa solo lo busca cuando
va a reproducir audio de verdad.

### 8.2 Ejecución completa con el ESP32

Con el cable conectado y el nivel ajustado (sección 7):

```bash
./test_aprs_wavs.py --wav_dir Synthetic --serial_port /dev/ttyUSB0 --audio_device hw:1,0
```

Una cadena que funciona muestra los tres paquetes, cada uno con la etiqueta **OK**:

```
[1/1] sample.wav  (5.5 s)
000001 [multimon 00:01.1] N0CALL-9>APRS-0,WIDE1-1,WIDE2-1:!4903.50N/07201.75W-Test one
    OK [esp32    00:01.3] N0CALL-9>APRS,WIDE1-1,WIDE2-1:!4903.50N/07201.75W-Test one
000002 [multimon 00:02.8] LU1ABC-0>APDW17-0,WIDE1-1:=3450.12S/05812.34W>Movil en ruta
    OK [esp32    00:03.0] LU1ABC>APDW17,WIDE1-1*:=3450.12S/05812.34W>Movil en ruta
000003 [multimon 00:04.4] EA4XYZ-7>APRS-0::LU1ABC   :Hola que tal{12
    OK [esp32    00:04.6] EA4XYZ-7>APRS::LU1ABC   :Hola que tal{12
  multimon-ng decoded 3 packet(s)
  ESP32 decoded 3 packet(s)
  -> OK: 3   DIFFERENT: 0   NOT DECODED: 0   EXTRA(esp only): 0
```

y un resumen final con `Decoded correctly : 3 (100.00%)`. (Los tiempos pueden
diferir unas décimas de segundo en su sistema.)

No se espera que las dos líneas de un par se lean idénticas: multimon-ng escribe
`-0` cuando falta el SSID y nunca escribe el `*` de digipeteado, mientras que el
firmware hace lo contrario. La sección 12.1 detalla qué diferencias se normalizan
antes de comparar.

Si aparecen como **NOT DECODED**, el problema está en la cadena, no en el
decodificador: vaya a la sección 15 (solución de problemas). **No continúe con
tráfico real hasta que esto funcione.**

---

## 9. Ejecutar la prueba real

Lista de verificación previa:

- [ ] IGate, RF a Internet, Digipeater y balizas están en **OFF** (sección 4.2)
- [ ] Registrar después de los filtros está en **OFF**; el módem de audio, la autopolarización y el aviso de sobrerrango están en **ON**
- [ ] Ningún otro programa usa el puerto serie
- [ ] No pueden sonar sonidos del sistema en la placa de sonido
- [ ] Nivel ajustado con NIVEL RX (sección 7)
- [ ] El archivo sintético de la sección 8 dio **OK** en todos los paquetes
- [ ] Las pistas 5–7 no están en el directorio de pruebas

Ejecute, guardando al mismo tiempo un archivo de registro:

```bash
./test_aprs_wavs.py --wav_dir Audio-Tracks --serial_port /dev/ttyUSB0 --audio_device hw:1,0 \
    2>&1 | tee results_$(date +%Y-%m-%d_%H%M).log
```

Sin opciones, el programa usa los archivos WAV del **directorio actual** y
`/dev/ttyUSB0`:

```bash
cd Audio-Tracks
../test_aprs_wavs.py
```

### Qué ocurre durante una ejecución

1. El programa lista los archivos WAV que encontró y abre el puerto serie
   (115200 8N1).
2. **Abrir el puerto normalmente reinicia el ESP32** (el chip USB-serie activa
   DTR/RTS). El programa espera `--settle` segundos (4 por defecto) a que arranque.
3. Salvo que se haya dado `--no_auto_volume`, a continuación ejecuta la
   **calibración automática de volumen** de la sección 7.1: sondeos baratos de
   8 paquetes para hallar el umbral de recorte y después lotes completos para
   hallar el centro de la meseta que queda por debajo, que se van imprimiendo a
   medida que ocurren. Esto agrega tiempo antes de que empiece la prueba que se
   informa; sáltela con `--no_auto_volume` si ya sabe qué volumen quiere.
4. Para cada archivo, en el mismo instante:
   * **reproduce** el WAV hacia la placa de sonido → ESP32 (en tiempo real), y
   * envía el mismo audio a **multimon-ng**, también a ritmo de tiempo real, de
     modo que los paquetes de ambos decodificadores aparezcan lado a lado, y
   * **lee la consola del ESP32** buscando líneas `RX:`.
5. Cada paquete de multimon-ng se imprime con su veredicto en cuanto se conoce
   (sección 11). Tras las 10 primeras coincidencias confirmadas, el programa
   fija la **latencia** medida entre los dos decodificadores y la compensa en
   todas las comparaciones posteriores (sección 12.4).
6. Cada 30 segundos se imprime una **línea de progreso**, para que los archivos
   largos nunca parezcan colgados.
7. Cuando termina el audio, el programa espera unos segundos para que los últimos
   paquetes tengan la misma oportunidad que el resto, imprime los conteos del
   archivo, hace una pausa y continúa con el siguiente.
8. Al final imprime el **resumen** de todos los archivos.

Tiempo total ≈ la pasada de calibración automática de volumen (evítela con
`--no_auto_volume`) + la suma de las duraciones de los archivos + unos 6 s por
archivo (+ 4 s al inicio). Para las pistas 1–4 del conjunto de WA8LMF, sin
calibración, son unas **una hora**.

**Ctrl-C** detiene la prueba de forma segura: se conserva todo lo que ya tiene un
veredicto y se imprime el resumen. Los paquetes que todavía esperaban su veredicto
(el ESP32 no tuvo su chance completo de `--match_window` para responder) se
descartan en silencio: no se imprimen ni se cuentan de ninguna forma.

---

## 10. Referencia de la línea de comandos

```
./test_aprs_wavs.py [opciones]
```

### Archivos, puertos y audio

| Opción | Valor predeterminado | Significado |
|---|---|---|
| `--wav_dir DIR` | directorio actual | Directorio con los archivos `.wav` (no recursivo). |
| `--serial_port PORT` | `/dev/ttyUSB0` | Puerto serie de la consola del ESP32. |
| `--baud N` | `115200` | Velocidad serie (8N1 es fijo). |
| `--audio_device DEV` | predeterminado del sistema | Dispositivo ALSA conectado al ESP32, p. ej. `hw:1,0` (ver `--list_audio`). |
| `--list_audio` | — | Imprime los dispositivos de reproducción ALSA (`aplay -l`) y sale. Necesita `alsa-utils`. |
| `--no_play` | desactivado | **Prueba en seco:** sin sonido y sin puerto serie; solo se ejecuta multimon-ng. También omite la calibración automática de volumen. Los paquetes se listan como líneas `000001 [multimon …]`, sin veredicto. |
| `--mm_args "…"` | ninguno | Argumentos adicionales para multimon-ng, entre comillas, p. ej. `--mm_args "-A"` (rara vez necesarios). |

### Nivel

| Opción | Valor predeterminado | Significado |
|---|---|---|
| `--volume X` | `1.0` | Ganancia por software aplicada solo al audio enviado al ESP32, como `gain` de sox en dB. También es el punto de partida de la calibración automática de volumen, salvo que se dé `--no_auto_volume`. Debe estar dentro de `--volume_min` … `--volume_max`. |
| `--normalise`, `--normalize` | desactivado | Lleva primero cada WAV a −1 dBFS en la cadena de reproducción (`sox gain -n -1`), para que una sola ganancia valga para grabaciones hechas a niveles distintos y las ganancias por encima de 1,0 dejen de significar "recortar dentro de sox" (sección 7.2). |
| `--no_auto_volume` | desactivado | Omite la pasada de calibración automática de volumen (sección 7.1) y usa `--volume` tal cual durante toda la ejecución. |
| `--auto_volume_batch N` | `50` | Paquetes puntuados por sondeo de meseta (cuenta juntos los paquetes de multimon-ng y los EXTRA solo del ESP32). Debe ser ≥ 1. |
| `--auto_volume_max_rounds N` | `10` | Presupuesto de búsqueda, **en lotes de `--auto_volume_batch` paquetes**, no en llamadas de sondeo. Un sondeo de recorte barato de 8 paquetes cuesta una fracción de ronda; un sondeo de puntuación completo cuesta una. Debe ser ≥ 1. |
| `--volume_min X` | `0.02` | Ganancia más baja que puede usar la búsqueda. Debe ser > 0 y < `--volume_max`. |
| `--volume_max X` | `4.0` | Ganancia más alta que puede usar la búsqueda. Por encima de 1,0 solo tiene sentido junto con `--normalise`. |
| `--clip_rate X` | `0` | Advertencias de sobrerrango por paquete que un nivel puede producir y seguir contando como limpio. El 0 por defecto significa que **una sola advertencia marca el nivel como recorte** y nada se vuelve a reproducir en él ni por encima. Debe estar en [0, 1). |
| `--clip_step_db X` | `0.5` | Una vez informado un sobrerrango, la búsqueda no vuelve a subir la ganancia y **baja** esta cantidad de dB por sondeo hasta que cesan las advertencias. Debe ser > 0 y ≤ 6. |
| `--headroom_db X` | `6` | dB por debajo del techo de sobrerrango (o del nivel limpio más alto) a los que recurrir cuando no se pudo puntuar ninguna meseta o el descenso agotó su presupuesto. |
| `--max_passes N` | `3` | Pasadas por el conjunto de WAV antes de que un sondeo de calibración se rinda. Debe ser ≥ 1. |

### Tiempos y emparejamiento

| Opción | Valor predeterminado | Significado |
|---|---|---|
| `--match_window S` | `5` | Un paquete del ESP32 responde a un paquete de multimon-ng solo si llega dentro de ±S segundos de este, **una vez restada la latencia medida**. Un paquete que el ESP32 no informó para entonces es **NOT DECODED**. Ver secciones 12 y 13. Debe ser > 0. |
| `--no_offset_auto` | desactivado | No estima el desfase de latencia ESP32 vs multimon-ng; compara las marcas de tiempo en bruto (sección 12.4). |
| `--tail S` | `3` | Segundos que se sigue escuchando después de que termina el audio. El programa siempre espera al menos `--match_window` segundos. |
| `--settle S` | `4` | Segundos de espera tras abrir el puerto serie (reinicio/arranque del ESP32). Aumente el valor si el ESP32 arranca lento. |
| `--pause S` | `1` | Pausa entre archivos. |

### Interfaz y mantenimiento

| Opción | Valor predeterminado | Significado |
|---|---|---|
| `--lang en\|es\|it` | idioma del sistema | Idioma de los mensajes, de los textos de `--help` y de la interfaz gráfica (sección 10.1). |
| `--gui` | — | Abre la interfaz gráfica (sección 10.2). Necesita `python3-tk`. |
| `--selftest` | — | Ejecuta las pruebas unitarias incorporadas —sin hardware ni audio— y sale (sección 10.3). |
| `-h`, `--help` | — | Muestra la ayuda incorporada, en el idioma actual. |

**Código de salida** (útil en scripts):

| Código | Significado |
|---|---|
| `0` | El ESP32 decodificó correctamente todos los paquetes de multimon-ng (ningún DIFFERENT, ningún HDR-CORRUPT, ningún NOT DECODED). También es el código de un `--selftest` exitoso y de una salida normal de la interfaz gráfica. |
| `1` | Al menos un paquete resultó DIFFERENT, HDR-CORRUPT o NOT DECODED. (Con tráfico real y cargado este es el resultado normal; mire los porcentajes.) También es el código de un `--selftest` fallido. |
| `2` | Problema de configuración (falta un programa, no hay archivos WAV, no se puede abrir el puerto, una opción fuera de rango, sin pantalla o sin tkinter para `--gui`) o multimon-ng no decodificó ningún paquete. |

Ejemplos:

```bash
# todos los WAV del directorio actual, puerto predeterminado
./test_aprs_wavs.py

# un directorio, otro puerto serie y una placa de sonido USB
./test_aprs_wavs.py --wav_dir ./Audio-Tracks --serial_port /dev/ttyUSB1 --audio_device hw:1,0

# un solo archivo: póngalo en un directorio propio
mkdir one && cp Audio-Tracks/03_*.wav one/
./test_aprs_wavs.py --wav_dir one --audio_device hw:1,0

# pista 3 (paquetes idénticos cada 3 s): ventana más estrecha
./test_aprs_wavs.py --wav_dir one --audio_device hw:1,0 --match_window 1.5

# solo comprobación de software, sin hardware
./test_aprs_wavs.py --wav_dir Audio-Tracks --no_play

# pruebas unitarias, sin hardware ni audio
./test_aprs_wavs.py --selftest

# interfaz gráfica
./test_aprs_wavs.py --gui

# reutilizar un volumen ya confiable, omitiendo la calibración
./test_aprs_wavs.py --wav_dir Audio-Tracks --audio_device hw:1,0 --volume 0.85 --no_auto_volume

# un conjunto de grabaciones a niveles distintos: normalizarlas primero
./test_aprs_wavs.py --wav_dir Audio-Tracks --audio_device hw:1,0 --normalise

# dejar que la calibración busque más (más presupuesto, lotes mayores)
./test_aprs_wavs.py --wav_dir Audio-Tracks --audio_device hw:1,0 --auto_volume_max_rounds 15 --auto_volume_batch 80

# guardar el resultado y luego listar solo los problemas
./test_aprs_wavs.py --wav_dir Audio-Tracks --audio_device hw:1,0 2>&1 | tee run.log
grep -E "NOT DECODED|DIFFERENT|HEADER CORRUPT" run.log
```

### 10.1 Idioma

Cada mensaje, texto de `--help` y etiqueta de la interfaz gráfica existe en
**inglés, español e italiano**. El idioma se elige en este orden:

1. `--lang en|es|it` (o el selector **Language** de la propia interfaz gráfica);
2. el idioma del sistema: `LANGUAGE`, `LC_ALL`, `LC_MESSAGES`, `LANG`, el módulo
   `locale` de Python o el idioma de la interfaz de Windows;
3. inglés, cuando el idioma del sistema no es ninguno de los tres.

```bash
./test_aprs_wavs.py --lang es --wav_dir Audio-Tracks   # todo en español
LANGUAGE=it ./test_aprs_wavs.py --help                 # ayuda en italiano
```

El idioma se resuelve *antes* de construir el analizador de argumentos, y por eso
hasta `--help` sale traducido.

### 10.2 Interfaz gráfica (`--gui`)

```bash
./test_aprs_wavs.py --gui        # Debian/Ubuntu: sudo apt install python3-tk
```

Una ventana con todo el formulario arriba y dos consolas debajo:

```
+----------------------------------------------------------------+
|  todas las opciones de la línea de comandos, como formulario    |
|  [Start] [Stop] [Clear consoles] [Reset defaults]  [Language v] |
+-------------------------------+--------------------------------+
|  CONSOLA (izquierda)          |  SERIE (derecha)               |
|  todo lo que imprime el       |  cada byte leído del puerto    |
|  programa (stdout + stderr)   |  del ESP32, sin filtrar        |
+-------------------------------+--------------------------------+
```

* **El formulario se genera a partir del mismo analizador que la línea de
  comandos**, así que nunca puede faltarle una opción. Al pasar el puntero por un
  campo aparece el texto de `--help` de esa opción, y `--wav_dir` tiene además un
  botón `…` que abre un selector de directorios. Un campo en blanco significa
  "usar el valor predeterminado".
* **Start** imprime en la consola la línea de comandos equivalente
  (`$ test_aprs_wavs.py --wav_dir … --audio_device …`), de modo que una ejecución
  hecha desde la interfaz puede repetirse en una terminal.
* **Stop** equivale a Ctrl-C: mata de inmediato los procesos de audio y de los
  decodificadores, y el resumen parcial se imprime igual. Cerrar la ventana con una
  prueba en marcha pide confirmación.
* **Clear consoles** vacía ambos paneles; **Reset defaults** devuelve cada campo a
  su valor predeterminado.
* La **consola izquierda** lleva todo lo que imprime el programa, con stderr en
  rojo; la **derecha** lleva el flujo serie crudo y sin filtrar del ESP32 —mensajes
  de arranque incluidos—, que es la forma más rápida de ver si la placa habla. Ambas
  conservan las últimas 20 000 líneas y solo siguen el final automáticamente
  mientras ya esté abajo del todo, así que desplazarse hacia arriba para leer no
  pelea contra la salida.
* Los botones de **zoom** (`−` / `+`, arriba a la derecha, o Ctrl+`+` / Ctrl+`−` /
  Ctrl-0) escalan toda la ventana entre el 75 % y el 300 %.
* El selector **Language** reconstruye la ventana en el idioma elegido sin perder
  lo que esté escrito en el formulario ni lo que muestren las consolas. Se niega
  mientras hay una prueba en marcha: deténgala primero.

Si falta tkinter el programa lo dice y sale con el código 2; lo mismo ocurre
cuando no hay pantalla (una sesión SSH sin reenvío de X).

### 10.3 Autoprueba (`--selftest`)

```bash
./test_aprs_wavs.py --selftest
```

Ejecuta las pruebas unitarias incorporadas: **sin hardware, sin audio, sin puerto
serie y sin esperas** (alrededor de un segundo). Comprueba

* la normalización y el análisis: SSID `-0`, el `*` de digipetido, el CR final, un
  LF de carga útil convertido en `.`, dos puntos dentro de la carga útil y un
  prefijo de log de ESP-IDF;
* los cuatro veredictos de `LiveMatcher`, incluido que una trama con cabecera
  corrupta dé **un** veredicto en lugar de un perdido más un extra, y que dos
  balizas idénticas se emparejen con la transmisión más cercana;
* que el desfase de latencia se aprenda de las coincidencias confirmadas;
* el intervalo de Wilson y la ida y vuelta a dB;
* que la búsqueda de volumen converja al centro de una meseta **simulada** desde
  tres ganancias iniciales distintas, dentro del presupuesto, y mantenga su margen
  por debajo del umbral de recorte;
* el **techo de sobrerrango**: ningún sondeo se reproduce en un nivel que informó
  sobrerrango ni por encima; el descenso avanza exactamente de a `--clip_step_db`;
  una sola advertencia en un lote de puntuación baja el techo y descarta ese nivel;
  un descenso que agota el presupuesto igual termina por debajo del techo; y una
  búsqueda interrumpida vuelve por debajo del techo, no a la ganancia inicial;
* que un sondeo sobre un conjunto de WAV que no decodifica nada termine en vez de
  quedarse en bucle para siempre, y que nunca suba la ganancia.

Cada comprobación imprime `PASS` o `FAIL`; el código de salida es 0 cuando todo
pasó y 1 en caso contrario. Vale la pena ejecutarla tras editar el script, y es lo
primero que hay que ejecutar si el banco se comporta de forma extraña.

---

## 11. Interpretar los resultados

### 11.1 Líneas de paquetes

Cada paquete de **multimon-ng** se imprime, en el orden en que se escuchó,
seguido inmediatamente por la línea propia del ESP32 para él (si la hay) y un
veredicto:

```
000012 [multimon 03:41.2] LU1ABC-0>APDW17-0,WIDE1-1:=3450.12S/05812.34W>Movil
    OK [esp32    03:41.4] LU1ABC>APDW17,WIDE1-1:=3450.12S/05812.34W>Movil
000013 [multimon 03:52.0] LU2XYZ-0>APRS-0:>some status
       [esp32     --:--.-] NOT DECODED
000014 [multimon 04:10.5] LU3AAA-0>APRS-0:>hello
       [esp32    04:11.0] LU3AAA>APRS:>hellX
      ! DECODED BUT DIFFERENT
000015 [multimon  --:--.-] NOT DECODED
       [esp32 only      04:20.1] LU9ZZZ>APRS:>heard only by the ESP32
000016 [multimon 04:31.7] LU3AAA-0>APRS-0,WIDE1-1:>hello
       [esp32    04:31.9] LU3AAB>APRS,WIDE1-1:>hello
      ! PAYLOAD OK BUT HEADER CORRUPT
```

* `000012` — un **contador de impresión** de seis dígitos. Avanza de uno en uno
  por cada paquete impreso, en el orden en que se conocen los veredictos, y también
  numera los paquetes EXTRA, así que no es la cuenta de paquetes de multimon-ng. Se
  reinicia en `000001` en cada archivo.
* `03:41.2` — minutos:segundos dentro del archivo cuando multimon-ng lo decodificó.
* El texto es el paquete en **formato TNC2**: `ORIGEN>DESTINO,RUTA:carga útil`.
  multimon-ng escribe `-0` tras los indicativos sin SSID (`LU1ABC-0`) y nunca
  escribe el `*` de digipetido; el firmware hace lo contrario. Esas diferencias se
  normalizan antes de comparar (sección 12.1), así que las dos líneas de un par
  **OK** suelen verse algo distintas.
* **OK** se imprime al *principio de la línea del ESP32*; los demás veredictos se
  imprimen en una línea propia debajo del par.
* Un paquete que decodificó **solo el ESP32** también se imprime como par, al
  revés: primero una línea `[multimon  --:--.-] NOT DECODED` (multimon-ng es el que
  lo perdió) y después la línea del ESP32 como `[esp32 only ...]`. La palabra EXTRA
  no aparece en estas líneas en vivo: esos paquetes se cuentan como
  `EXTRA(esp only)` en los conteos por archivo y finales.

| Veredicto | Significado |
|---|---|
| **OK** | El ESP32 decodificó el mismo paquete (mismo origen, destino, ruta y carga útil), cerca en el tiempo. |
| **NOT DECODED** | El ESP32 no lo informó. Este es el caso "perdido". |
| **! DECODED BUT DIFFERENT** | El ESP32 decodificó un paquete con la misma cabecera (origen, destino, ruta) pero con una **carga útil distinta**: decodificado, pero no correctamente. Se cuenta como DIFFERENT. |
| **! PAYLOAD OK BUT HEADER CORRUPT** | Lo contrario: la **carga útil coincidió exactamente** pero la cabecera no — un indicativo o un elemento de la ruta salió dañado. Se cuenta como HDR-CORRUPT y, como DIFFERENT, como "decodificado, pero no correctamente". |

Cada paquete de multimon-ng recibe **exactamente un** veredicto, y la línea del
ESP32 usada para llegar a un veredicto negativo queda consumida por él: nunca puede
informarse además como EXTRA (sección 12.4).

Tiempos: un **OK** aparece enseguida (normalmente en uno o dos segundos). Los
veredictos negativos aparecen unos `--match_window` segundos después del paquete
(5 s por defecto, más la latencia medida), porque el ESP32 todavía podría estar a
punto de informarlo.

La línea propia del ESP32 y los paquetes EXTRA se muestran siempre: no hace falta
ninguna opción para verlos.

### 11.2 Línea de progreso

```
       [progress 04:00.0 / 25:49.3] multimon=13  ok=12  not-decoded=1  different=0  (serial lines seen: 240)
```

Tiempo reproducido / duración del archivo, y los conteos acumulados. El valor
**`serial lines seen`** debe seguir creciendo: demuestra que el enlace serie está vivo. Si
`multimon` crece pero `ok` se queda en 0 y `not-decoded` crece, el ESP32 no está
oyendo el audio (sección 15).

### 11.3 Conteos por archivo

```
  multimon-ng decoded 14 packet(s)
  ESP32 decoded 13 packet(s)
  -> OK: 11   DIFFERENT: 1   HDR-CORRUPT: 1   NOT DECODED: 1   EXTRA(esp only): 0
  -> measured esp32 latency vs multimon-ng: +0.42 s
    ! DIFFERENT
        multimon: LU3AAA-0>APRS-0:>hello
        esp32   : LU3AAA>APRS:>hellX
    ! HEADER CORRUPT (payload matched)
        multimon: LU3AAA-0>APRS-0,WIDE1-1:>hello
        esp32   : LU3AAB>APRS,WIDE1-1:>hello
    ! NOT DECODED by ESP32: LU2XYZ-0>APRS-0:>some status
```

Después de los conteos se vuelve a listar cada paquete DIFFERENT, HDR-CORRUPT y
NOT DECODED de ese archivo, para poder leer los problemas de una grabación larga
en un solo lugar en vez de buscarlos entre las líneas en vivo. La línea de latencia
solo aparece cuando realmente se midió un desfase (sección 12.4).

### 11.4 Resumen final

```
========================================================================
SUMMARY
========================================================================
  file                                 mm     ok   diff    hdr  n/dec  extra
  01_40-Mins-Traffic-on-144.39.wav    412    371      2      1     38      6
  03_D700-Mic-E-100-bursts.wav        100     97      0      0      3      0
  ----------------------------------------------------------------------
  Playback gain used for this test  : 0.299  (-10.5 dB)
  ESP32 latency vs multimon-ng      : +0.42 s (median of 2 file(s))
  Files tested                      : 2
  Total packets (multimon-ng)       : 512
  Packets seen by ESP32             : 476
  Decoded correctly                 : 468  (91.41%)
  Decoded with different content    : 2  (0.39%)
  Decoded with corrupt header       : 1  (0.20%)
  Missing (not decoded)             : 41  (8.01%)
  Extra (ESP32 only, not an error)  : 6
========================================================================
```

*(los números de arriba son solo una ilustración del formato)*

Cómo se define cada cifra:

| Cifra | Definición |
|---|---|
| **Total packets** | paquetes decodificados por multimon-ng (la referencia) |
| **Decoded correctly** | cuenta de OK y su porcentaje sobre el total |
| **Decoded with different content** | cuenta de DIFFERENT (cabecera correcta, carga útil incorrecta) y su porcentaje |
| **Decoded with corrupt header** | cuenta de HDR-CORRUPT (carga útil correcta, cabecera incorrecta) y su porcentaje |
| **Missing (not decoded)** | cuenta de NOT DECODED y su porcentaje |
| **Extra** | paquetes que decodificó solo el ESP32. **No** forman parte de los porcentajes. |

Los cuatro porcentajes suman 100 % (salvo redondeo).

`Playback gain used for this test` es la ganancia por software (sección 7.1) que se
usó realmente para reproducir cada archivo de esta ejecución —el valor al que llegó
la calibración automática, o `--volume` sin cambios si se dio `--no_auto_volume`—,
impresa como factor lineal y en dB. `ESP32 latency vs multimon-ng` es el desfase
medido entre los dos decodificadores (sección 12.4); solo aparece cuando se midió
alguno. Anote ambos junto con el resto de los detalles de la ejecución si piensa
comparar registros más adelante.

### 11.5 Cómo interpretarlo

**Importante:** multimon-ng es una *referencia*, no la verdad. Ninguno de los dos
decodificadores es perfecto. En un canal cargado (pista 1 de WA8LMF) algunos
paquetes los decodifica uno y no el otro. Entonces:

* Un porcentaje alto de **OK** es bueno. No hay nota de aprobación oficial: lo que
  importa es **comparar ejecuciones** — los mismos archivos antes y después de un
  cambio de firmware, o antes y después de tocar el nivel. Guarde los archivos
  `.log` y anote la fecha, la versión del firmware, el nivel RX, la ganancia de
  reproducción y la placa de sonido.
* **NOT DECODED** = multimon-ng lo encontró y el ESP32 no. En un canal saturado son
  de esperar grupos de pérdidas alrededor de las colisiones.
* **EXTRA** = el ESP32 encontró algo que multimon-ng perdió. Un decodificador mejor
  que la referencia produce extras; es buena señal, no un error.
* **DIFFERENT** y **HDR-CORRUPT** deberían ser raros (las tramas AX.25 llevan CRC).
  Mire la línea propia del ESP32 impresa bajo el paquete para ver exactamente qué
  decodificó. Un DIFFERENT cuya carga útil simplemente está cortada a menudo no es
  culpa del demodulador, sino de un **LF crudo en la carga útil que parte la línea
  de la consola**: vea el requisito del firmware en la sección 4.3.

Patrones típicos:

| Lo que ve | Causa probable |
|---|---|
| `ok=0` desde el principio, todo NOT DECODED | El audio no llega al ESP32 (cable, placa de sonido, nivel, módem desactivado). |
| Un archivo entero casi todo NOT DECODED, pero el archivo sintético dio OK | Nivel demasiado bajo o alto para esa grabación, o una grabación con deénfasis (pista 2) que necesita otro nivel. Pruebe `--normalise` (sección 7.2). |
| Pérdidas solo en los tramos densos | Normal con tráfico saturado (colisiones, paquetes espalda con espalda). |
| Ráfaga súbita de NOT DECODED en mitad de un archivo | Algo perturbó el audio (un sonido del sistema, un cambio de volumen) o la placa de sonido falló. |
| Muchos EXTRA | El ESP32 es más sensible que multimon-ng con este material. |
| Unos pocos HDR-CORRUPT entre paquetes débiles o con colisión | Un byte dañado en un campo de dirección. Es esperable en pequeñas cantidades en las pistas 1 y 4. |

---

## 12. Cómo se comparan los paquetes

Solo puede confiar en los números si sabe cómo se hace la comparación.

### 12.1 Qué cuenta como "el mismo paquete"

Los dos decodificadores describen el paquete con estilos distintos, así que el
programa primero los normaliza. Hay cinco diferencias entre multimon-ng y el
firmware, y todas están contempladas:

| Diferencia | multimon-ng | Firmware del ESP32 | Tratamiento |
|---|---|---|---|
| SSID 0 | imprime `LU1ABC-0` | imprime `LU1ABC` | se elimina un `-0` final |
| Marca de digirepetición | nunca imprime `*` | imprime `WIDE1-1*` después de que un digi lo repitió | se ignora el `*` |
| Bytes no imprimibles (los paquetes Mic-E contienen bytes de control y de 8 bits) | los muestra como `.` | escribe los bytes en crudo | el contenido del ESP32 se convierte de la misma manera antes de comparar |
| Retorno de carro final | lo descarta | lo escribe en crudo | se ignora un CR/LF/NUL final |
| Mayúsculas/minúsculas de las direcciones | tal como se oyeron | tal como se oyeron | el origen, el destino y la ruta se pasan a mayúsculas antes de comparar, así que la caja por sí sola nunca produce un DIFFERENT |

Lo que **no** se ignora: los **espacios y puntos** finales son contenido real, de
modo que un contenido truncado (`>hello.` frente a `>hello`) se informa
correctamente como DIFFERENT. Los SSID reales (`-9`, `-10`) siempre se comparan.

Para ser OK deben coincidir **el origen, el destino, cada elemento de la ruta y todo
el contenido**.

### 12.2 La ventana de tiempo

Una grabación puede contener el mismo paquete muchas veces (una estación que
baliza cada 30 s). Para no acreditarle al ESP32 la transmisión equivocada, solo se
forma un par si la línea del ESP32 llegó dentro de **±`--match_window` segundos
(5 por defecto)** del paquete de multimon-ng, **una vez restada la latencia
medida** (sección 12.4). Cada paquete del ESP32 se usa **una sola vez**: un paquete
enviado tres veces debe decodificarse tres veces para sumar tres OK. Cuando hay dos
candidatos, gana el **más cercano en el tiempo**.

Un veredicto negativo solo se emite una vez transcurridos `--match_window` segundos
*más* la latencia medida, de modo que la compensación nunca acorta la oportunidad
que se le da al ESP32 de responder.

### 12.3 Qué significa la ventana de tiempo para paquetes idénticos (pista 3 de WA8LMF)

La pista 3 tiene 100 paquetes **idénticos** separados solo **3 segundos**. Es el
caso más difícil para el emparejamiento, así que esto es exactamente lo que se
puede esperar (verificado por simulación):

* Los **totales son correctos con cualquier ventana razonable** (por ejemplo 97 OK
  cuando el ESP32 perdió 3 de 100).
* Con la ventana predeterminada de 5 s, *qué números de paquete* quedan marcados
  como NOT DECODED puede desplazarse en uno cuando la línea del ESP32 llega un poco
  **antes** que la de multimon-ng. La cuenta es correcta; la numeración de los
  paquetes marcados puede correrse.
* Regla: elija una ventana **mayor que el retardo residual** entre los dos
  decodificadores y **menor que la mitad del espaciado** entre paquetes idénticos.
  Para la pista 3: **1,5 s** (`--match_window 1.5`). La compensación automática de
  latencia de la sección 12.4 elimina la mayor parte de ese retardo, que es lo que
  hace utilizable una ventana tan estrecha.
* Si la ventana es *menor* que el retardo residual, paquetes que el ESP32 decodificó
  correctamente se cuentan mal. Así que **compruebe antes el retardo**: ejecute la
  pista 3 y lea la línea `ESP32 latency vs multimon-ng` del resumen, o compare las
  dos marcas de tiempo impresas para los primeros paquetes OK. Con
  `--no_offset_auto` es el retardo completo el que tiene que caber en la ventana.

Para todas las demás pistas (sin paquetes idénticos a menos de 10 s) la ventana
predeterminada de 5 s está bien.

### 12.4 Latencia entre los dos decodificadores, y un veredicto por paquete

Las dos ramas **no** son igual de rápidas. La de multimon-ng va ajustada al tiempo
real, mientras que la del ESP32 pasa por el búfer de salida de ALSA, el ADC, el
demodulador y la consola UART: habitualmente unos cientos de milisegundos, a veces
más de un segundo. Sin corregir, ese desfase se come la ventana de emparejamiento y
fabrica veredictos NOT DECODED que son artefactos del banco, no fallos del
firmware.

Por eso el programa lo mide:

* se recoge la diferencia de tiempo (ESP32 − multimon-ng) de cada coincidencia
  **confirmada**; las diferencias por encima de ±3 s se ignoran, porque no son
  latencia de placa de sonido;
* tras **10** muestras se adopta su **mediana** (no la media, para que una línea de
  consola tardía no arrastre la estimación) como desfase, y se **fija** para el
  resto de la ejecución;
* el desfase aprendido durante la calibración automática de volumen se traslada a
  la prueba real, de modo que la ejecución informada ya empieza compensada;
* se informa por archivo (`measured esp32 latency vs multimon-ng`) y en el resumen,
  como la mediana entre los archivos;
* `--no_offset_auto` desactiva todo esto y compara las marcas de tiempo en bruto:
  entonces el retardo completo tiene que caber dentro de `--match_window`.

Cada paquete de multimon-ng recibe **exactamente un** veredicto, decidido en este
orden una vez vencido su plazo: un paquete del ESP32 con la misma cabecera pero
distinta carga útil → **DIFFERENT**; si no, uno con la misma carga útil pero
distinta cabecera → **HDR-CORRUPT**; si no, **NOT DECODED**. El paquete del ESP32
usado para llegar a un veredicto DIFFERENT o HDR-CORRUPT queda marcado como
consumido, así que una única trama dañada nunca puede contarse dos veces (una como
fallo y otra como EXTRA). Los paquetes del ESP32 que ningún paquete de multimon-ng
reclama se convierten en **EXTRA**.

Una consecuencia de **Ctrl-C**: los paquetes de multimon-ng cuyo plazo aún no había
vencido se descartan por completo —no se imprimen ni se cuentan de ninguna forma—
porque el ESP32 no tuvo su oportunidad completa de responderlos.

---

## 13. Plan de pruebas sugerido con las pistas de WA8LMF

Haga los pasos en orden; cada uno da confianza para el siguiente.

| Paso | Archivo | Comando (agregue `--audio_device hw:X,0`) | Propósito / qué mirar |
|---|---|---|---|
| 0 | `sample.wav` sintético | `--wav_dir Synthetic` | Verificación de la cadena: los 3 paquetes deben ser **OK**. |
| 1 | Pista 3 (100 ráfagas Mic-E) | `--wav_dir one3 --match_window 1.5` | **Porcentaje exacto**: el ESP32 debería decodificar cerca de 100 de 100. También ejercita contenidos Mic-E con caracteres de control. Mida aquí el desfase de tiempo entre decodificadores. |
| 2 | Pista 2 (los mismos 100, con desénfasis) | `--wav_dir one2 --match_window 1.5` | Los mismos 100 paquetes con la curva tipo parlante: compare con el paso 1. Ajuste el nivel con NIVEL RX si hace falta (la señal es distinta). |
| 3 | Pista 4 (móvil, débil) | `--wav_dir one4` | Señal débil, flutter y multitrayecto. Compare los faltantes con lo que logra multimon-ng. |
| 4 | Pista 1 (25 min saturada) | `--wav_dir one1` | La prueba de esfuerzo: colisiones, paquetes consecutivos. Espere un porcentaje menor que en las pistas 3/4 y algunos EXTRA. |
| 5 | Las cuatro | `--wav_dir Audio-Tracks` | La batería completa, un solo resumen con los totales generales. |

Cree una vez los directorios de un solo archivo:

```bash
for n in 1 2 3 4; do mkdir -p one$n; cp Audio-Tracks/0${n}_*.wav one$n/; done
```

Repita todo el plan después de cada cambio de firmware y compare los logs. Para
juzgar la variación natural, ejecute primero **tres veces** el mismo archivo: la
ganancia automática del ESP32 y el reloj de la placa de sonido hacen que los
resultados difieran ligeramente entre ejecuciones.

Valores de referencia de la página de origen, a modo de orientación: la pista 3
contiene exactamente **100** paquetes, así que "cantidad de OK ÷ 100" es
directamente la tasa de éxito del ESP32 con audio limpio.

---

## 14. `gen_test_wav.py` en detalle

Un pequeño generador de **audio APRS sintético perfecto**, usado para verificar la
instalación y para crear archivos de prueba reproducibles. Construye tramas AX.25
reales (CRC-16, bit stuffing, NRZI) moduladas como AFSK Bell 202 a 1200 baudios a
22050 Hz, mono, 16 bits, con 40 bytes de bandera de preámbulo (≈ 0,27 s), 8
banderas de cola y 1 s de silencio entre paquetes. El archivo además empieza con
0,5 s de silencio, y el mismo intervalo de 1 s sigue al último paquete, así que
nada queda cortado en los extremos. Los tonos se escriben a alrededor del 60 %
de la escala completa, lo que deja margen y mantiene el archivo sin recortes.

### Uso como programa

```bash
python3 gen_test_wav.py output.wav
```

escribe un archivo con tres paquetes e imprime `ok`. Sin nombre de archivo
escribe `sample1.wav` en el directorio actual. No crea directorios, así que cree
antes el de destino (`mkdir -p Synthetic`). Los paquetes son:

| Origen | Destino | Ruta | Contenido |
|---|---|---|---|
| N0CALL-9 | APRS | WIDE1-1, WIDE2-1 | `!4903.50N/07201.75W-Test one` |
| LU1ABC | APDW17 | WIDE1-1* | `=3450.12S/05812.34W>Movil en ruta` |
| EA4XYZ-7 | APRS | (ninguna) | `:LU1ABC   :Hola que tal{12` |

(`N0CALL` es el indicativo estándar de marcador de posición; los otros son
ejemplos.)

### Uso como módulo (sus propios paquetes)

```python
from gen_test_wav import write_wav

packets = [
    # (origen,     destino,  [ruta],                   contenido)
    ("N0CALL-9",  "APRS",   ["WIDE1-1", "WIDE2-1"], "!4903.50N/07201.75W-Test"),
    ("LU1ABC",    "APDW17", ["WIDE1-1*"],           "=3450.12S/05812.34W>Mobile"),
    # un contenido estilo Mic-E con caracteres de control / de 8 bits:
    ("LU2XYZ-9",  "T2SP0W", ["WIDE1-1"],            "`c2Bl\x1c>/\x1d]mice test\xe9"),
    # un contenido que termina en retorno de carro:
    ("LU4CR",     "APRS",   [],                     ">status\r"),
]
write_wav("my_test.wav", packets, rate=22050, gap_s=1.0)
```

* `rate` — frecuencia de muestreo del WAV (se recomiendan 22050 Hz).
* `gap_s` — segundos de silencio entre paquetes.
* Un `*` después de un elemento de la ruta (`"WIDE1-1*"`) significa "ya repetido por
  ese digi".

Usos: comprobar que la cadena funciona (sección 8), probar contenidos especiales
(bytes Mic-E, CR) y producir una cantidad conocida de paquetes para una ejecución
rápida de regresión.

---

## 15. Solución de problemas

| Síntoma | Causa y qué hacer |
|---|---|
| `Cannot open serial port … Permission denied` | Su usuario no está en el grupo `dialout` (sección 5.1). |
| `Cannot open serial port … No such file or directory` | Puerto equivocado. `ls /dev/ttyUSB* /dev/ttyACM*`, revise `dmesg \| tail`, use `--serial_port`. |
| `Cannot open serial port … busy` | Otro programa (idf.py monitor, minicom, screen…) tiene el puerto. Ciérrelo. |
| `WARNING: no data received from the serial port yet` | Puerto o velocidad equivocados; el ESP32 todavía está arrancando (aumente `--settle 8`); el cable USB es solo de carga. |
| `Missing required program(s): …` | Instale los paquetes de la sección 2. |
| `No .wav files in …` | `--wav_dir` equivocado, o los archivos son FLAC/MP3 (conviértalos, sección 6.3). |
| `[audio] player failed` | `play` no puede abrir el dispositivo de sonido: `--audio_device` equivocado, la placa está ocupada (PulseAudio/PipeWire puede tenerla) o falta `libsox-fmt-alsa`. Pruebe sin `--audio_device`, o ejecute `aplay -l`. |
| multimon-ng decodifica paquetes pero **todos** dan NOT DECODED | El ESP32 no está oyendo el audio. Revise en este orden: placa de sonido correcta (`--audio_device`) → volumen/silencio del mezclador (`alsamixer`) → cable y conexión de C1/RV1 y polaridad → **Activar módem ADC/DAC de audio** y **Polarización interna de la entrada del ADC** en ON → pulse **NIVEL RX** mientras suena el audio. |
| La consola del ESP32 no muestra ninguna línea `RX:`, ni siquiera con buen audio | El módem está desactivado; **Registrar después de los filtros** está en ON; el nivel de log no es INFO; o (raro) el IGate desactivado las oculta: active **Habilitar IGate** pero deje **RF a Internet** en OFF y el Wi-Fi solo como punto de acceso, y pruebe de nuevo. |
| Aviso de sobrerrango en la consola / nivel de RX muy por encima de 350 mV | Nivel demasiado alto: baje RV1 o el volumen de la PC. No lo deje así: el pin no tiene diodos de protección. |
| Nivel de RX muy bajo (< 100 mV) | Suba RV1, o suba un poco el volumen de la PC. |
| Nivel de continua cerca de 0 mV o de 3300 mV | La autopolarización está desactivada, o C1 falta o está conectado al revés, o RV1 está entre C1 y el pin. |
| Muchos NOT DECODED en un archivo pero no en otros | El nivel difiere entre grabaciones (sobre todo las desenfatizadas); vuelva a comprobarlo con NIVEL RX para ese archivo. |
| Los resultados cambian entre ejecuciones | Normal en pequeña medida (ganancia automática, reloj de la placa de sonido). Repita 3 veces y compare. Si el cambio es grande, revise la estabilidad de la placa de sonido USB y los sonidos del sistema; considere también si la calibración automática de volumen (sección 7.1) eligió un volumen distinto cada vez — fíjelo con `--volume X --no_auto_volume` para una comparación justa. |
| La ejecución tarda notablemente más de lo que sugiere la duración de los archivos | Es normal: de forma predeterminada, toda ejecución empieza con la pasada de calibración automática de volumen (sección 7.1), que reproduce el conjunto de WAV varias veces antes de que empiece la prueba que se informa. Use `--no_auto_volume` para saltarla una vez que conozca un buen `--volume`. |
| La calibración automática de volumen informa "no packets decoded by multimon-ng at all" y se detiene | multimon-ng no encontró nada a ningún volumen — es un problema de archivo/ruteo de audio, no de nivel (ver la fila "multimon-ng decodificó 0 paquetes" de más arriba). |
| El ESP32 se reinicia cuando empieza la prueba | Abrir el puerto reinicia la placa mediante DTR/RTS. Es normal; `--settle` espera al arranque. |
| El programa parece trabado | Los archivos largos se reproducen en tiempo real; mire la línea de progreso cada 30 s. Ctrl-C detiene de forma segura. |
| multimon-ng decodificó 0 paquetes en un archivo | El archivo no tiene paquetes (las pistas 5–7 son solo tonos), está demasiado bajo o no es AFSK 1200. El programa sale con código 2 si *ningún* archivo produce paquetes. |
| Pistas 5–7 en el directorio | Contienen tonos, no paquetes: hacen perder tiempo y no aportan nada. Sáquelas. |
| Paquetes DIFFERENT | Compare las líneas de multimon-ng y del ESP32 impresas para ese paquete; el contenido difiere (ver sección 11.5). Esos mismos paquetes se vuelven a listar bajo los conteos del archivo. |
| Pares de líneas `[multimon  --:--.-] NOT DECODED` seguidos de `[esp32 only …]` | No es un fallo: así se imprime en vivo un paquete EXTRA —decodificado por el ESP32 y perdido por multimon-ng—. Se cuenta en `EXTRA(esp only)`. |
| Los números de seis dígitos no coinciden con la cuenta de paquetes de multimon-ng | Son un contador de impresión que también numera los paquetes EXTRA y se reinicia en cada archivo (sección 11.1). |
| Pista 3: los números de los paquetes marcados parecen desplazados en uno | Efecto de ventana/temporización descrito en la sección 12.3. Use `--match_window 1.5`; los totales son correctos de todos modos. |
| `WARNING: \`stdbuf\` not found (package coreutils)` | La salida de multimon-ng queda con búfer de bloque en la tubería, sus paquetes llegan a ráfagas y se marcan con una hora tardía: veredictos NOT DECODED falsos. `sudo apt install coreutils`. |
| `--gui needs tkinter` | `sudo apt install python3-tk`. |
| `Cannot open a display for --gui` | No hay pantalla X/Wayland: está en una consola de texto o en una sesión SSH sin reenvío de X. Use la línea de comandos, o `ssh -X`. |
| `! …would clip inside sox (max usable gain …)` | La ganancia de reproducción por el pico propio del archivo supera la escala completa, así que sox recortaría antes de la placa de sonido. Agregue `--normalise`, o baje la ganancia y suba el nivel en RV1 (sección 7.2). |
| `probe incomplete: n/N packet(s) after 3 pass(es) over the wav set` | La calibración no pudo reunir un lote completo: el audio no se está decodificando en absoluto. Es enrutamiento del audio o los propios archivos, no el nivel. Revise `--audio_device`, el mezclador y que los WAV contengan de verdad paquetes AFSK 1200. |
| `Still over-range at the lowest gain allowed` | Hasta `--volume_min` sobreexcita el ADC. El nivel analógico es demasiado alto: baje RV1 (o el volumen de la PC) antes de volver a ejecutar. |
| `Descent budget spent while still over-range at … dB` | La búsqueda empezó tan alta que los pasos pequeños de descenso agotaron el presupuesto. Recurrió a techo − `--headroom_db`. Mejor: baje RV1, o empiece más abajo (`--volume 0.3`); si no, aumente `--auto_volume_max_rounds` o `--clip_step_db`. |
| `<- OVER-RANGE` en una línea de sondeo | Ese sondeo hizo que el firmware informara sobrerrango; desde entonces ningún sondeo sube hasta ahí. Es normal mientras se localiza el techo. |
| `No clipping seen up to +12.0 dB` durante la calibración | Ni siquiera la ganancia más alta permitida hizo que el firmware se quejara: el nivel de hardware hacia el ADC es demasiado bajo. Suba RV1 (o el volumen de la PC) y vuelva a ejecutar. |
| `NOTE: more than 6 dB of attenuation/boost was needed` | El nivel analógico está mal y la ganancia por software solo lo disimula. Baje RV1 (atenuación) o súbalo (amplificación) para que el banco pueda trabajar cerca de 0 dB. Amplificar digitalmente también amplifica el piso de ruido de la placa de sonido. |
| `multimon-ng did not exit within 60 s - killing it` | Inofensivo: el decodificador seguía reteniendo la tubería tras terminar la reproducción. Los resultados del archivo se conservan y la ejecución continúa. |
| Paquetes HDR-CORRUPT (`! PAYLOAD OK BUT HEADER CORRUPT`) | La carga útil coincidió pero un campo de dirección no. Unos pocos con tráfico débil o con colisiones son esperables; muchos apuntan al nivel (vuelva a comprobar con NIVEL RX) o a un problema del demodulador. |
| Paquetes DIFFERENT cuya carga útil simplemente está cortada | A menudo no es el demodulador: un LF crudo dentro de la carga útil parte la línea de la consola en dos. El firmware debe escapar los bytes no imprimibles antes de registrar la línea `RX:` (sección 4.3). |
| Cada ejecución da un volumen distinto | La meseta es plana por naturaleza, así que dos niveles pueden empatar. Fíjelo con `--volume X --no_auto_volume` para comparaciones antes/después (secciones 7.1 y 16). |
| Algo parece mal en el propio programa | Ejecute `./test_aprs_wavs.py --selftest` (sección 10.3): ejercita el análisis, el emparejamiento, la estimación de latencia y la búsqueda de volumen sin ningún hardware. |

---

## 16. Limitaciones

* **La referencia no es la verdad.** multimon-ng pierde algunos paquetes y el ESP32
  puede decodificarlos (se informan como EXTRA), y viceversa. Los porcentajes miden
  la concordancia con multimon-ng, no el rendimiento absoluto.
* **La concordancia es por texto.** Un paquete es OK si el origen, el destino, la
  ruta y el texto de la carga útil son iguales; el programa no mira los bits crudos.
* **Solo AFSK 1200 baudios** (el `AFSK1200` de multimon-ng). No se prueban otros
  modos.
* **La cadena de sonido de la PC forma parte de la prueba.** Su filtrado, la
  exactitud de su reloj y su ruido se suman al resultado (ver los consejos de la
  sección 5.3).
* **Solo recepción.** No se prueba la cadena de transmisión del ESP32.
* **La calibración automática de volumen (sección 7.1) está activada por defecto**
  y es estadística: apunta al centro de la meseta, y dos ejecuciones pueden quedarse
  a un paso de distancia cuando varios niveles decodifican igual de bien. Para
  comparaciones estrictas antes/después, fije la ganancia con
  `--volume X --no_auto_volume` en lugar de dejar que recalibre cada vez.
* **La calibración mide lo que reproduce.** Usa el conjunto de WAV que se le dio; un
  conjunto cuyos archivos difieren mucho en nivel se ejecuta mejor con
  `--normalise` (sección 7.2), porque si no la ganancia elegida se ajusta a las
  grabaciones sobre las que cayeron los sondeos.
* **La compensación de latencia supone un desfase aproximadamente constante**
  (sección 12.4): se mide una vez, con 10 coincidencias confirmadas, y luego queda
  fija. Una cadena de sonido cuyo retardo varíe mucho durante una ejecución no está
  modelada; `--no_offset_auto` vuelve a las marcas de tiempo en bruto.
* **El firmware debe escapar los bytes no imprimibles en la línea de log `RX:`**
  (sección 4.3). Un LF crudo en una carga útil parte la línea de la consola y se
  puntúa como error de contenido aunque el demodulador haya acertado.
* Las opciones de audio suponen **Linux con ALSA**; `--gui` necesita además tkinter
  y una pantalla, y la interfaz existe solo en tres idiomas.
* El programa se desarrolló y comprobó contra una **consola de ESP32 simulada**, con
  multimon-ng, sox y pyserial reales, más la autoprueba incorporada `--selftest`. En
  hardware real, cuente con ajustar el nivel y quizá `--match_window`; la sección 15
  cubre los problemas habituales.

---

## 17. Hoja de referencia rápida

```bash
# ── configuración única ───────────────────────────────────────────
sudo apt install python3-serial multimon-ng sox libsox-fmt-all alsa-utils coreutils python3-tk
sudo usermod -aG dialout $USER            # luego cierre sesión / vuelva a entrar
./test_aprs_wavs.py --list_audio          # encontrar la placa de sonido → hw:X,0

# ── interfaz web del ESP32 ────────────────────────────────────────
#   Radiomódem  : Activar módem ADC/DAC de audio = ON, Polarización interna de la
#                 entrada del ADC = ON, Avisar cuando el audio recibido se sale de rango = ON
#   IGate       : Habilitar IGate = OFF, RF a Internet = OFF, Registrar después de los filtros = OFF
#   Digirepetidor: Habilitar Digipeater = OFF     (balizas OFF, Wi-Fi solo como punto de acceso)

# ── ajustar el nivel: reproducir una grabación con mucha actividad (pista 1 de
#    WA8LMF), pulsar NIVEL RX en la página Radiomódem varias veces; objetivo
#    250–350 mV RMS, raw lejos de 0/4095, nivel de continua 1200–2000 mV; luego no tocar nada
./test_aprs_wavs.py --wav_dir one1 --audio_device hw:1,0     # Ctrl-C al terminar el ajuste

# ── verificar la cadena (3 paquetes limpios, se espera todo OK) ───
mkdir -p Synthetic && python3 gen_test_wav.py Synthetic/sample.wav
./test_aprs_wavs.py --wav_dir Synthetic --audio_device hw:1,0

# ── la prueba real (empieza con una pasada de calibración automática de
#    volumen por defecto, ver 7.1; agregue --no_auto_volume --volume X para
#    saltarla y fijar un valor conocido) ───────────────────────────────
./test_aprs_wavs.py --wav_dir Audio-Tracks --audio_device hw:1,0 2>&1 | tee run.log
grep -E "NOT DECODED|DIFFERENT|HEADER CORRUPT" run.log

# ── grabaciones hechas a niveles distintos ───────────────────────
./test_aprs_wavs.py --wav_dir Audio-Tracks --audio_device hw:1,0 --normalise

# ── otros puntos de entrada ──────────────────────────────────────
./test_aprs_wavs.py --selftest            # pruebas unitarias: sin hardware ni audio
./test_aprs_wavs.py --gui                 # interfaz gráfica (necesita python3-tk)
./test_aprs_wavs.py --lang en --help      # ayuda y mensajes en inglés
```

| Conexión | |
|---|---|
| Punta del jack | → parte superior de RV1 (o R1, sección 3.1) |
| Manga del jack | → parte inferior de RV1 **y** GND del ESP32 (o el extremo lejano de R2, sección 3.1) |
| Cursor de RV1 | → **−** de C1 (lado de la franja) (o el nodo R1/R2, sección 3.1) |
| **+** de C1 | → GPIO33 del ESP32 |

---

## 18. Glosario

| Término | Significado |
|---|---|
| **APRS** | Automatic Packet Reporting System: paquetes de radioaficionados de posición, mensajes y telemetría. |
| **AFSK 1200** | Audio Frequency Shift Keying a 1200 baudios: dos tonos de audio (1200 Hz y 2200 Hz) transportan los bits (Bell 202). |
| **AX.25** | El formato de paquete de la capa de enlace que usa APRS en VHF. |
| **Formato TNC2** | La forma de texto de un paquete: `ORIGEN>DESTINO,RUTA:contenido`. |
| **Mic-E** | Una codificación compacta de posición APRS que pone datos en la dirección de destino y usa caracteres de control y de 8 bits en el contenido. |
| **Salida del discriminador** | El audio demodulado en crudo de un receptor de FM, antes del desénfasis: la mejor fuente para datos. |
| **Desénfasis** | La atenuación de agudos que aplica el receptor al audio del parlante; los datos tomados del parlante están desenfatizados. |
| **IGate** | Pasarela que reenvía a la red APRS-IS de Internet los paquetes oídos por radio (y viceversa). |
| **Digirepetidor (digipeater)** | Una estación que repite paquetes por radio para ampliar el alcance. |
| **APRS-IS** | La red de Internet que reúne los paquetes APRS. |
| **RMS** | Valor eficaz (root-mean-square): el tamaño efectivo de una señal alterna; el ESP32 informa el nivel de RX en mV RMS. |
| **Nivel de continua (DC offset)** | La tensión continua media en la que reposa el pin del ADC; con autopolarización debería rondar los 1650 mV. |
| **SSID** | El número después de un indicativo (`-9`) que distingue varias estaciones de un mismo operador. |
| **Calibración automática de volumen** | La pasada predeterminada del programa, antes de la prueba que se informa, que busca el nivel más alto sin sobrerrango (sin volver a subir nunca una vez que el firmware se quejó) y luego el centro de la meseta que queda por debajo, y usa esa ganancia para toda la ejecución. Ver sección 7.1. |
| **Sobrerrango (over-range)** | El aviso del propio firmware de que el audio que llega al ADC excede su rango de entrada (recorta). La calibración lo usa como señal de recorte. |
| **Techo de sobrerrango** | La ganancia de reproducción más baja que hizo que el firmware informara sobrerrango. Nada se vuelve a reproducir en ella ni por encima; la calibración baja desde ahí en pasos de `--clip_step_db` y al final se mantiene al menos 3 dB por debajo. |
| **Meseta** | El rango de niveles en el que la tasa de decodificación es plana, entre el piso de ruido por debajo y el recorte por arriba. Su centro es el nivel con más margen a ambos lados. |
| **Intervalo de Wilson** | Un intervalo de confianza para una proporción que se comporta bien con muestras pequeñas. Dos sondeos cuyos intervalos se solapan se tratan como empatados, no como mejor y peor. |
| **Normalización** | `--normalise`: llevar cada WAV a −1 dBFS antes de la ganancia de reproducción, para que una sola ganancia sirva a grabaciones hechas a niveles distintos (sección 7.2). |
| **Desfase de latencia** | El retardo de la rama del ESP32 (placa de sonido → ADC → demodulador → consola) respecto de la de multimon-ng, medido y compensado automáticamente (sección 12.4). |
| **HDR-CORRUPT** | Un veredicto: la carga útil del ESP32 coincidió exactamente pero su cabecera (origen, destino o ruta) no. Se cuenta junto con DIFFERENT como "decodificado, pero no correctamente". |
