<p align="center">
  <img src="https://github.com/hiperiondev/esp32idf_APRS/raw/main/images/logo.png" width="300">
</p>

<div align="center">

# esp32idf_APRS

### Una estación APRS completa en un solo ESP32 — ESP-IDF nativo, sin Arduino.

**IGate · Digipeater · Tracker · Meteorología · Telemetría**, con un panel de administración web integrado, un soft-módem AFSK/FSK en el propio chip, enlace a APRS-IS, un framework de drivers de sensores en tiempo de ejecución y actualizaciones de firmware OTA.

[![Docs](https://img.shields.io/badge/docs-readthedocs-blue)](https://esp32idf-aprs.readthedocs.io/)
[![License](https://img.shields.io/badge/license-GPLv3-green)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-ESP32-red)](#hardware)
[![Framework](https://img.shields.io/badge/framework-ESP--IDF%205.x-orange)](#)

**🌐 Idiomas:** [English](README.md) · **Español** · [Italiano](README.it.md)

</div>

---

## ¿Qué es esto?

`esp32idf_APRS` convierte una simple **placa ESP32 DevKit** más una interfaz de audio económica en una estación **APRS** completa y autónoma. Todo se ejecuta en el propio ESP32 — no hay núcleo Arduino, ni PlatformIO, ni bibliotecas DSP externas. Toda la cadena de señal, desde el demodulador por correlación pasando por la recuperación de bits DPLL, NRZI, el empaquetador HDLC, el códec AX.25 y la corrección de errores hacia adelante Reed–Solomon FX.25, se ejecuta en el microcontrolador usando únicamente el SAR-ADC en modo continuo/DMA, el DAC y un temporizador de propósito general.

En una frase, el firmware **demodula** audio AFSK/FSK desde el altavoz o la salida de discriminador de la radio, **decodifica** tramas HDLC/AX.25 (opcionalmente corregidas con FX.25), las **enlaza** a APRS-IS por Wi-Fi, las **repite (digipeat)** de vuelta por RF, **baliza** su propia posición, meteorología y telemetría, **modula** y transmite tramas a través del DAC de 8 bits del ESP32 — y se configura íntegramente mediante un panel web servido por el propio dispositivo. Sin consola serie, sin recompilar para los ajustes habituales.

> 📖 **La documentación completa y exhaustiva está en [esp32idf-aprs.readthedocs.io](https://esp32idf-aprs.readthedocs.io/)** — trilingüe (English / Español / Italiano), con guías de inicio, cableado del hardware, la cadena de señal DSP, el motor de configuración, las rutas HTTP y resolución de problemas. **Este README es solo una presentación. Para cualquier cosa más allá de un primer vistazo, consulte la documentación.**

---

## Lo más destacado

- **Soft-módem en el chip.** AFSK 1200 Bd Bell 202 (APRS estándar) con demodulador dual, más AFSK 1200 Bd V.23, AFSK 300 Bd y **G3RUH 9600 Bd FSK** — todo en C puro sobre el propio ADC/DAC del ESP32.
- **Corrección de errores FX.25.** FEC Reed–Solomon sobre AX.25, solo RX o RX+TX, para decodificaciones fiables con señal débil.
- **IGate APRS-IS completo.** Enlace bidireccional **RF→INET** e **INET→RF** con supresión de duplicados, construcción `qAR`/`qAO`, filtrado por tipo de carga útil, budlists de indicativos, un range gate local (distancia haversine) y lista blanca por prefijo. Se pueden listar hasta cuatro servidores APRS-IS, con failover automático entre los habilitados.
- **Digipeater.** Una tabla de alias n-N de cuatro filas (WIDE1-1 / WIDE2-2 / WIDE#-2 por defecto), cada fila con su propio límite de saltos y modo trace/flood, más trampa de contador de saltos, operación de solo relleno y supresión de duplicados.
- **Balizas, mensajería y chat.** Balizas de posición fija para tracker/igate/digi, mensajería de texto APRS con ack/reintentos (RF y/o INET) y una interfaz de chat de mensajes en el navegador.
- **Meteorología y telemetría.** Informes meteorológicos APRS al aire con refresco de sensores a 1 Hz y promediado por campo, más telemetría APRS (analógica A1–A5 + digital B1–B8) con informes `T#nnn` y metadatos.
- **Objetos, ítems y boletines.** Hasta cinco Objetos/Ítems APRS de la estación y cinco boletines (BLN1–BLN5), cada uno por RF y/o INET con control de expiración/decaimiento.
- **Framework de sensores en tiempo de ejecución.** Un registro de drivers dinámico y autorregistrable (`sensors_local`) — incluye de fábrica un driver BME280/BMP280 (I²C), más uno opcional para BMP180 en el mismo bus.
- **Panel web, ~30 páginas.** Autenticación HTTP Basic, un dashboard en vivo, un registro de tráfico en vivo y tabla de últimos escuchados (long-poll JSON), gestión de archivos LittleFS (subir/descargar/borrar/formatear), Wi-Fi AP/STA/AP+STA con escaneo y control de potencia de TX, y control de frecuencia de CPU (80/160/240 MHz).
- **Actualizaciones OTA con auto-rollback.** Dos ranuras de aplicación `ota_0`/`ota_1`; una imagen fallida revierte automáticamente en el siguiente arranque.
- **Interfaz trilingüe.** Inglés, español e italiano (en tiempo de compilación, un idioma por imagen).

---

## Matriz de funciones

| Área | Notas |
|---|---|
| AFSK 1200 Bd Bell 202 | Demodulador dual, perfil por defecto |
| AFSK 1200 Bd V.23 · AFSK 300 Bd · G3RUH 9600 Bd FSK | Varios perfiles de módem seleccionables |
| Tramas UI HDLC / AX.25 RX + TX | Cadena TX/RX completa del soft-módem |
| FX.25 (FEC Reed–Solomon sobre AX.25) | Modos solo RX / RX+TX |
| Control de PTT | GPIO y polaridad en compilación, retención mínima de des-keying |
| CSMA / time-slot de TX / preámbulo TXDelay | `preamble`, `tx_timeslot` |
| Limitador de ciclo de trabajo de TX | Techo opcional sobre una ventana deslizante de 10 minutos |
| IGate APRS-IS RF→INET e INET→RF | Filtros, dedup, budlist, desempaquetado third-party opcional |
| Failover multiservidor de APRS-IS | 4 ranuras de servidor, reintento circular sobre las habilitadas |
| Range gate y prefix gate locales | Distancia haversine + lista blanca por prefijo de indicativo |
| Digipeater | Tabla de alias n-N configurable (trace/flood), trampa de saltos, supresión de duplicados |
| Objetos / Ítems · Boletines | Hasta 5 de cada, RF y/o INET, expiración/decaimiento |
| Mensajería + ack/reintento · Chat | RF y/o INET |
| Informe meteorológico | Refresco de sensores a 1 Hz, promediado opcional |
| Telemetría | Analógica A1–A5 + digital B1–B8, `T#nnn` + metadatos |
| Framework de drivers de sensores | Registro dinámico, driver BME280/BMP280 incluido |
| Panel web | ~30 páginas, dashboard en vivo, tráfico + últimos escuchados |
| Almacenamiento | LittleFS 512 KB, subir/descargar/borrar/formatear |
| Red | Wi-Fi AP/STA/AP+STA, escaneo, potencia de TX, SNTP (reloj en UTC, zona horaria seleccionable para visualización) |
| Control de frecuencia de CPU | 80 / 160 / 240 MHz |
| Actualización OTA | Ranuras `ota_0`/`ota_1`, auto-rollback |
| Localización | EN / ES / IT, en compilación |

---

## Hardware

- **Objetivo:** ESP32 (clásico, Xtensa doble núcleo), 4 MB de flash. El doble núcleo es **obligatorio** — la ISR del ADC y el reloj de muestreo del DAC se fijan a núcleos distintos a propósito.
- **Entrada de audio (ADC):** por defecto `GPIO33` (ADC1). **Solo GPIO 32–39** — el ADC2 es inutilizable con el Wi-Fi activo.
- **Salida de audio (DAC):** por defecto `GPIO25`. **Solo GPIO 25 o 26** — el DAC del ESP32 está cableado a esos pads.
- **PTT:** por defecto `GPIO26`, polaridad seleccionable en compilación.
- **Nota:** ESP32-S3/C3/C6/H2 **no tienen DAC** y no pueden ejecutar la cadena de TX sin modificaciones.

El cableado de la placa (pines de audio, pin/polaridad de PTT, tasas de muestreo) se define como constantes de compilación en el `CMakeLists.txt` de nivel superior. Se incluye un esquemático KiCad de la interfaz de radio en `schematics/`.

> Las tablas completas de pines y las restricciones de cableado están en el [capítulo de Hardware de la documentación](https://esp32idf-aprs.readthedocs.io/en/latest/es/hardware.html).

---

## Inicio rápido

```bash
# Requiere ESP-IDF v6.x (probado y fijado en 6.0.2)
idf.py set-target esp32
idf.py build
idf.py -p PUERTO flash monitor
```

En el primer arranque el dispositivo levanta un AP Wi-Fi; conéctese y abra el panel web para configurar su indicativo, la radio y los servicios. Tras el flasheo inicial por USB/UART, todas las actualizaciones posteriores pueden hacerse desde la página **About / Firmware** del panel web mediante OTA.

> 📖 La guía paso a paso del primer arranque está en [Getting Started](https://esp32idf-aprs.readthedocs.io/en/latest/es/getting-started.html).

---

## Documentación

**Todo está documentado en detalle en 👉 [esp32idf-aprs.readthedocs.io](https://esp32idf-aprs.readthedocs.io/)**

La documentación es trilingüe y está organizada en *Funcionalidades* (lo que hace la estación), *Capacidades* (propiedades transversales), *Internos* (cómo está construida) y una sección de *Referencia*:

- 🇬🇧 [English](https://esp32idf-aprs.readthedocs.io/en/latest/en/index.html)
- 🇪🇸 [Español](https://esp32idf-aprs.readthedocs.io/en/latest/es/index.html)
- 🇮🇹 [Italiano](https://esp32idf-aprs.readthedocs.io/en/latest/it/index.html)

Este README es solo una presentación — **por favor, consulte la documentación para la instalación, el cableado, la configuración y los internos.**

---

## Créditos y licencia

Creado por **Emiliano Augusto González (LU3VEA)**.

Construido sobre ideas de proyectos anteriores — [VP-Digi](https://github.com/sq8vps/vp-digi), [ESP32APRS](https://github.com/nakhonthai/ESP32APRS_Audio) y [LibAPRS](https://github.com/markqvist/LibAPRS); consulte a sus autores para más información.

Publicado bajo la **Licencia Pública General de GNU v3**. Véase [LICENSE](LICENSE).
