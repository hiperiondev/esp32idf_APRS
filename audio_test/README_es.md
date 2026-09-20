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
7. [Ajustar el nivel de audio](#7-ajustar-el-nivel-de-audio) ([calibración automática de volumen](#71-calibración-automática-de-volumen))
8. [Primera ejecución: verificar toda la cadena con paquetes sintéticos](#8-primera-ejecución-verificar-toda-la-cadena-con-paquetes-sintéticos)
9. [Ejecutar la prueba real](#9-ejecutar-la-prueba-real)
10. [Referencia de la línea de comandos](#10-referencia-de-la-línea-de-comandos)
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

Instalar todo de una vez:

```bash
sudo apt install python3-serial multimon-ng sox libsox-fmt-all alsa-utils
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
agrega tiempo antes de la prueba que en realidad quería ver:

* Reproduce el conjunto de WAV (volviendo al primer archivo si hace falta) en
  lotes cortos de `--auto_volume_batch` paquetes (predeterminado 50, contando
  tanto los paquetes de multimon-ng como los "extra" que solo decodificó el
  ESP32), empezando en `--volume` (predeterminado 1.0).
* Después de cada lote comprueba el propio aviso de **sobrerrango** del ESP32
  (el mismo descrito en la sección 4.1) y el porcentaje decodificado — los
  paquetes que el ESP32 respondió, contando por igual los **OK** y los
  **DIFFERENT**, sobre el total de multimon-ng de ese intento:
  * **Sobrerrango en algún momento** → el nivel fue demasiado alto en ese
    intento; el volumen baja un 15 % para el siguiente intento, y ese intento
    no puede quedar registrado como el mejor.
  * **Sin sobrerrango** → se compara el porcentaje decodificado con el del
    intento anterior; el volumen sube un 15 % para el siguiente intento, y este
    se vuelve el nuevo mejor si superó a todos los anteriores.
* Sigue gastando todo su presupuesto de intentos — hasta
  `--auto_volume_max_rounds` (predeterminado 10) — incluso después de llegar al
  100 % o de que el porcentaje deje de moverse entre dos intentos, para no
  perderse un volumen aún mejor más adelante en la búsqueda solo porque uno
  anterior ya se veía bien.
* El volumen que haya obtenido el mayor porcentaje decodificado **entre los
  intentos que no recortaron** es el que se usa para la prueba real que sigue,
  la cual siempre vuelve a empezar desde el primer archivo.
* El volumen se mantiene dentro de **0,05 – 8,0**, y solo cambia entre intentos:
  cada intento se reproduce siempre con una ganancia fija.
* Si **todos** los intentos mostraron sobrerrango, ninguno queda habilitado y la
  ejecución vuelve al `--volume` inicial. Baje RV1 (o el volumen de la PC) y
  vuelva a ejecutar.
* Si un intento no decodifica nada con multimon-ng, la búsqueda se detiene ahí y
  lo informa: eso es un problema de archivos o de enrutamiento del audio, no de
  nivel.
* Interrumpir la calibración con **Ctrl-C** no detiene el programa: pasa a la
  prueba real con el volumen del intento que estaba corriendo en ese momento —que
  no es necesariamente el de mejor puntaje—, así que lea el valor impreso en el
  resumen final antes de citar el resultado.

El volumen elegido se imprime al terminar cada intento, y otra vez al final del
resumen final como `Playback volume used for this test`. Anote ese número junto
con el nivel que fijó en RV1: si está comparando ejecuciones a lo largo del
tiempo (sección 13) y quiere que la cadena de audio sea idéntica entre ellas,
pase el mismo valor con `--volume X --no_auto_volume` en lugar de recalibrar
cada vez.

`--no_play` (la prueba en seco) también omite la calibración: nunca toca la
placa de sonido.

Esto es también lo que hace práctica la alternativa con resistencias fijas de
la sección 3.1: sin un trimmer que girar, la ganancia por software de la
calibración es lo que absorbe la diferencia entre placas de sonido y ajustes de
volumen de la PC.

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

Aun así deben estar instalados pyserial, multimon-ng, sox y `play`: el programa
importa pyserial y comprueba los tres programas antes de mirar `--no_play`.

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
   **calibración automática de volumen** de la sección 7.1: varias pasadas
   cortas por el conjunto de WAV para hallar la mejor ganancia por software,
   que se van imprimiendo a medida que ocurren. Esto agrega tiempo antes de
   que empiece la prueba que se informa; sáltela con `--no_auto_volume` si ya
   sabe qué volumen quiere.
4. Para cada archivo, en el mismo instante:
   * **reproduce** el WAV hacia la placa de sonido → ESP32 (en tiempo real), y
   * envía el mismo audio a **multimon-ng**, también a ritmo de tiempo real, de
     modo que los paquetes de ambos decodificadores aparezcan lado a lado, y
   * **lee la consola del ESP32** buscando líneas `RX:`.
5. Cada paquete de multimon-ng se imprime con su veredicto en cuanto se conoce
   (sección 11).
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

| Opción | Valor predeterminado | Significado |
|---|---|---|
| `--wav_dir DIR` | directorio actual | Directorio con los archivos `.wav` (no recursivo). |
| `--serial_port PORT` | `/dev/ttyUSB0` | Puerto serie de la consola del ESP32. |
| `--baud N` | `115200` | Velocidad serie (8N1 es fijo). |
| `--audio_device DEV` | predeterminado del sistema | Dispositivo ALSA conectado al ESP32, p. ej. `hw:1,0` (ver `--list_audio`). |
| `--volume X` | `1.0` | Ganancia por software aplicada solo al audio enviado al ESP32. También es el punto de partida de la calibración automática de volumen (ver abajo), salvo que se dé `--no_auto_volume`. Durante la calibración se mantiene dentro de 0,05 – 8,0. |
| `--no_auto_volume` | desactivado | Omite la pasada de calibración automática de volumen (sección 7.1) y usa `--volume` tal cual durante toda la ejecución. |
| `--auto_volume_batch N` | `50` | Paquetes por intento durante la calibración automática de volumen (cuenta juntos los paquetes de multimon-ng y los "extra" solo del ESP32). |
| `--auto_volume_max_rounds N` | `10` | Número de intentos usados para buscar el mejor volumen antes de la prueba real. |
| `--match_window S` | `5` | Un paquete del ESP32 responde a un paquete de multimon-ng solo si llega dentro de ±S segundos de este. Un paquete que el ESP32 no informó tras S segundos es **NOT DECODED**. Ver secciones 12 y 13. |
| `--tail S` | `3` | Segundos que se sigue escuchando después de que termina el audio. El programa siempre espera al menos `--match_window` segundos. |
| `--settle S` | `4` | Segundos de espera tras abrir el puerto serie (reinicio/arranque del ESP32). Aumente el valor si el ESP32 arranca lento. |
| `--pause S` | `1` | Pausa entre archivos. |
| `--no_play` | desactivado | **Prueba en seco:** sin sonido y sin puerto serie; solo se ejecuta multimon-ng. También omite la calibración automática de volumen. Los paquetes se listan como líneas `000001 [multimon …]`, sin veredicto. |
| `--mm_args "…"` | ninguno | Argumentos adicionales para multimon-ng, entre comillas, p. ej. `--mm_args "-A"` (rara vez necesarios). |
| `--list_audio` | — | Imprime los dispositivos de reproducción ALSA (`aplay -l`) y sale. |
| `-h`, `--help` | — | Muestra la ayuda incorporada. |

**Código de salida** (útil en scripts):

| Código | Significado |
|---|---|
| `0` | Todos los paquetes de multimon-ng fueron decodificados correctamente por el ESP32. |
| `1` | Al menos un paquete fue DIFFERENT o NOT DECODED. (Con tráfico real y concurrido este es el resultado normal; lea los porcentajes.) |
| `2` | Problema de configuración (falta un programa, no hay archivos WAV, no se puede abrir el puerto) o multimon-ng no decodificó ningún paquete. |

Ejemplos:

```bash
# todos los WAV del directorio actual, puerto predeterminado
./test_aprs_wavs.py

# un directorio, otro puerto serie y una placa de sonido USB
./test_aprs_wavs.py --wav_dir ./Audio-Tracks --serial_port /dev/ttyUSB1 --audio_device hw:1,0

# un solo archivo: póngalo en un directorio propio
mkdir one && cp Audio-Tracks/03_*.wav one/
./test_aprs_wavs.py --wav_dir one --audio_device hw:1,0

# pista 3 (paquetes idénticos cada 3 s): ventana más angosta
./test_aprs_wavs.py --wav_dir one --audio_device hw:1,0 --match_window 1.5

# solo verificación del software, sin hardware
./test_aprs_wavs.py --wav_dir Audio-Tracks --no_play

# reutilizar un volumen ya conocido, sin pasada de calibración
./test_aprs_wavs.py --wav_dir Audio-Tracks --audio_device hw:1,0 --volume 0.85 --no_auto_volume

# que la calibración busque más a fondo (más intentos, lotes más grandes) en un conjunto grande
./test_aprs_wavs.py --wav_dir Audio-Tracks --audio_device hw:1,0 --auto_volume_max_rounds 15 --auto_volume_batch 80

# guardar el resultado y luego listar solo los problemas
./test_aprs_wavs.py --wav_dir Audio-Tracks --audio_device hw:1,0 2>&1 | tee run.log
grep -E "NOT DECODED|DIFFERENT" run.log
```

---

## 11. Interpretar los resultados

### 11.1 Líneas de paquetes

Cada paquete de **multimon-ng** se imprime, en el orden en que se oyó, seguido
inmediatamente por la línea propia del ESP32 para ese paquete (si existe) y un
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
```

* `000012` — un **contador de impresión** de seis dígitos. Avanza de a uno por
  cada paquete impreso, en el orden en que se resuelven los veredictos, y también
  numera los paquetes EXTRA, así que no es la cuenta de paquetes de multimon-ng.
  Vuelve a `000001` en cada archivo.
* `03:41.2` — minutos:segundos dentro del archivo en que multimon-ng lo decodificó.
* El texto es el paquete en **formato TNC2**: `ORIGEN>DESTINO,RUTA:contenido`.
  multimon-ng escribe `-0` después de los indicativos sin SSID (`LU1ABC-0`) y nunca
  escribe el `*` de digipeteado; el firmware hace lo contrario. Esas
  diferencias se normalizan antes de comparar (sección 12.1), por lo que las dos
  líneas de un par **OK** suelen verse algo distintas.
* El **OK** se imprime *al comienzo de la línea del ESP32*; los otros dos
  veredictos se imprimen en una línea propia debajo del par.
* Un paquete que **solo el ESP32** decodificó también se imprime como un par, pero
  al revés: primero una línea `[multimon  --:--.-] NOT DECODED` (el que falló es
  multimon-ng) y después la línea del ESP32 como `[esp32 only ...]`. La palabra
  EXTRA no aparece en estas líneas en vivo — esos paquetes se cuentan como
  `EXTRA(esp only)` en los conteos por archivo y en el resumen final.

| Veredicto | Significado |
|---|---|
| **OK** | El ESP32 decodificó el mismo paquete (mismo origen, destino, ruta y contenido), cercano en el tiempo. |
| **NOT DECODED** | El ESP32 no lo informó. Es el caso de "faltante". |
| **! DECODED BUT DIFFERENT** | El ESP32 decodificó un paquete con el mismo origen/destino/ruta pero con un **contenido distinto**: decodificado, pero no correctamente. |

Tiempos: un **OK** aparece enseguida (normalmente en uno o dos segundos). **NOT
DECODED** y **! DECODED BUT DIFFERENT** aparecen unos `--match_window` segundos
(5 s por defecto) después del paquete, porque el ESP32 podría estar a punto de
informarlo.

La línea propia del ESP32 y los paquetes EXTRA siempre se muestran — no hace
falta ninguna opción para verlos.

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
  -> OK: 12   DIFFERENT: 1   NOT DECODED: 1   EXTRA(esp only): 0
    ! DIFFERENT
        multimon: LU3AAA-0>APRS-0:>hello
        esp32   : LU3AAA>APRS:>hellX
    ! NOT DECODED by ESP32: LU2XYZ-0>APRS-0:>some status
```

Después de los conteos se vuelve a listar cada paquete DIFFERENT y NOT DECODED
de ese archivo, de modo que los problemas de una grabación larga se leen todos
juntos en lugar de tener que buscarlos entre las líneas en vivo.

### 11.4 Resumen final

```
========================================================================
SUMMARY
========================================================================
  file                                 mm     ok   diff  n/dec  extra
  01_40-Mins-Traffic-on-144.39.wav    412    371      2     39      6
  03_D700-Mic-E-100-bursts.wav        100     97      0      3      0
  ----------------------------------------------------------------------
  Playback volume used for this test : 1.150
  Files tested                      : 2
  Total packets (multimon-ng)       : 512
  Packets seen by ESP32             : 476
  Decoded correctly                 : 468  (91.41%)
  Decoded with different content    : 2  (0.39%)
  Missing (not decoded)             : 42  (8.20%)
  Extra (ESP32 only, not an error)  : 6
```

*(los números anteriores son solo una ilustración del formato)*

Cómo se define cada cifra:

| Cifra | Definición |
|---|---|
| **Total packets** | paquetes decodificados por multimon-ng (la referencia) |
| **Decoded correctly** | cantidad de OK y su porcentaje sobre el total |
| **Decoded with different content** | cantidad de DIFFERENT y su porcentaje |
| **Missing (not decoded)** | cantidad de NOT DECODED y su porcentaje |
| **Extra** | paquetes que solo decodificó el ESP32. **No** entran en los porcentajes. |

Los tres porcentajes suman 100 %.

`Playback volume used for this test` es la ganancia por software (sección 7.1)
que realmente se usó para reproducir todos los archivos de esta ejecución: el
valor que fijó la calibración automática de volumen, o `--volume` sin cambios si
se dio `--no_auto_volume`. Anótelo junto con los demás datos de la ejecución si
piensa comparar registros más adelante.

### 11.5 Cómo interpretarlo

**Importante:** multimon-ng es una *referencia*, no la verdad. Ninguno de los dos
decodificadores es perfecto. En un canal concurrido (pista 1 de WA8LMF) algunos
paquetes los decodifica uno y el otro no. Por lo tanto:

* Un porcentaje alto de **OK** es bueno. No hay un valor de aprobación oficial: lo
  que importa es **comparar ejecuciones** — los mismos archivos antes y después de
  un cambio de firmware, o antes y después de tocar el nivel. Guarde los archivos
  `.log` y anote la fecha, la versión del firmware, el nivel de RX y la placa de
  sonido.
* **NOT DECODED** = multimon-ng lo encontró y el ESP32 no. En un canal saturado son
  de esperar grupos de faltantes alrededor de las colisiones.
* **EXTRA** = el ESP32 encontró algo que multimon-ng no vio. Un decodificador mejor
  que la referencia muestra extras; es una buena señal, no un error.
* **DIFFERENT** debería ser raro (las tramas AX.25 llevan CRC). Mire la línea propia
  del ESP32 impresa bajo el paquete para ver exactamente qué decodificó; la causa
  suele ser un texto de contenido truncado o alterado en el log del ESP32.

Patrones típicos:

| Qué se ve | Causa probable |
|---|---|
| `ok=0` desde el principio, todo NOT DECODED | El audio no llega al ESP32 (cable, placa de sonido, nivel, módem desactivado). |
| Un archivo entero casi todo NOT DECODED, pero el sintético dio OK | Nivel demasiado bajo/alto para esa grabación, o una grabación con desénfasis (pista 2) que necesita otro nivel. |
| Faltantes solo en tramos densos | Normal en tráfico saturado (colisiones, paquetes consecutivos). |
| Ráfaga repentina de NOT DECODED en medio de un archivo | Algo perturbó el audio (un sonido del sistema, un cambio de volumen) o la placa de sonido tuvo un fallo. |
| Muchos EXTRA | El ESP32 es más sensible que multimon-ng con este material. |

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

Una grabación puede contener el mismo paquete muchas veces (una estación que emite
una baliza cada 30 s). Para no atribuirle al ESP32 la transmisión equivocada, solo
se forma un par si la línea del ESP32 llegó dentro de **±`--match_window` segundos
(5 por defecto)** del paquete de multimon-ng. Cada paquete del ESP32 se usa **una
sola vez**: un paquete enviado tres veces debe decodificarse tres veces para sumar
tres OK. Cuando hay dos candidatos, gana el **más cercano en el tiempo**.

### 12.3 Qué significa la ventana de tiempo para paquetes idénticos (pista 3 de WA8LMF)

La pista 3 tiene 100 paquetes **idénticos** separados solo **3 segundos**. Es el
caso más difícil para el emparejamiento, así que esto es exactamente lo que cabe
esperar (verificado por simulación):

* Los **totales son correctos con cualquier ventana razonable** (por ejemplo 97 OK
  cuando el ESP32 perdió 3 de 100).
* Con la ventana predeterminada de 5 s, *qué números de paquete* se marcan como NOT
  DECODED puede quedar desplazado en uno cuando la línea del ESP32 llega
  ligeramente **antes** que la de multimon-ng. La cantidad es correcta; la
  numeración de los paquetes marcados puede correrse.
* Regla: elija una ventana **mayor que el retardo real** entre los dos
  decodificadores y **menor que la mitad de la separación** entre paquetes
  idénticos. Para la pista 3: **1,5 s** (`--match_window 1.5`).
* Si la ventana es *menor* que el retardo real, los paquetes que el ESP32
  decodificó correctamente se cuentan mal. Por eso conviene **medir primero el
  retardo**: ejecute la pista 3 y compare las dos marcas de tiempo impresas en los
  primeros paquetes OK (la línea `[multimon ...]` y la línea `[esp32 ...]` que se
  imprime justo debajo). Si difieren en más de aproximadamente 1 s, mantenga una
  ventana más ancha (y recuerde que solo los totales son exactos).

Para todas las demás pistas (sin paquetes idénticos más cercanos que 10 s) la
ventana predeterminada de 5 s está bien.

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

---

## 16. Limitaciones

* **La referencia no es la verdad.** multimon-ng pierde algunos paquetes y el ESP32
  puede decodificarlos (se informan como EXTRA); y viceversa. Los porcentajes miden
  la coincidencia con multimon-ng, no el rendimiento absoluto.
* **La coincidencia es por texto.** Un paquete es OK si el texto de origen,
  destino, ruta y contenido es igual; el programa no mira los bits en crudo.
* **Solo AFSK 1200 baudios** (el `AFSK1200` de multimon-ng). No se prueban otros
  modos.
* **El camino de sonido de la PC forma parte de la prueba.** Su filtrado, la
  exactitud de su reloj y su ruido se suman al resultado (ver los consejos sobre la
  placa de sonido en la sección 5.3).
* **Solo recepción.** No se prueba la cadena de transmisión del ESP32.
* **La calibración automática de volumen (sección 7.1) está activada de forma
  predeterminada** y puede elegir un volumen ligeramente distinto de una
  ejecución a otra cuando dos niveles decodifican de forma casi igual de bien.
  Para comparaciones estrictas de antes/después, fije el volumen con
  `--volume X --no_auto_volume` en lugar de dejar que se recalibre cada vez.
* Las opciones de audio suponen **Linux con ALSA**.
* El programa se desarrolló y verificó con una **consola de ESP32 simulada**, con
  los multimon-ng, sox y pyserial reales. Con hardware real, cuente con ajustar el
  nivel y posiblemente `--match_window`; la sección 15 cubre los problemas
  habituales.

---

## 17. Hoja de referencia rápida

```bash
# ── configuración única ───────────────────────────────────────────
sudo apt install python3-serial multimon-ng sox libsox-fmt-all alsa-utils
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
grep -E "NOT DECODED|DIFFERENT" run.log
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
| **Calibración automática de volumen** | La pasada predeterminada del programa, antes de la prueba que se informa, que busca la ganancia de reproducción por software (`--volume`) que da el mejor porcentaje decodificado sin sobrerrango. Ver sección 7.1. |
