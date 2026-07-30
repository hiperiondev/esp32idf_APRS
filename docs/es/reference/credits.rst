.. _es-credits:

===================
Créditos y licencia
===================

Autoría
=======

* **Este proyecto y el componente de módem:** Emiliano Augusto González —
  **LU3VEA** — ``lu3vea @ gmail . com`` · https://github.com/hiperiondev

Linaje
======

El módem se basa en, y debe su linaje DSP a, tres proyectos anteriores. Por favor
contacta con sus autores para información sobre esos proyectos:

* **VP-Digi** — SQ8VPS — https://github.com/sq8vps/vp-digi
* **ESP32APRS_Audio** — nakhonthai — https://github.com/nakhonthai/ESP32APRS_Audio
* **LibAPRS** — Mark Qvist — https://github.com/markqvist/LibAPRS

El esquema de configuración, la disposición de la administración web y la
semántica del panel siguen el proyecto de referencia **esp32idf_APRS /
ESP32APRS** para que los archivos ``config.json`` existentes y las expectativas
del usuario se mantengan.

Componentes integrados
======================

* **littlefs** — ARM / joltwallet (BSD-3-Clause), vía el registro de componentes
  de ESP.
* **esp-idf-lib** ``bmp180`` / ``i2cdev`` / ``esp_idf_lib_helpers`` — vía el
  registro de componentes de ESP, para el controlador del sensor BMP180.

Licencia
========

**Licencia Pública General GNU v3.0** — véase el archivo ``LICENSE`` en el
repositorio.

El ``managed_components/joltwallet__littlefs`` integrado lleva su propia licencia
(BSD-3-Clause para el propio littlefs).

Aviso legal de radioafición
===========================

Transmitir en frecuencias de radioafición requiere una licencia válida para tu
país y banda. **Establece un indicativo real** (el valor por defecto es
``NOCALL``), usa un passcode legítimo de APRS-IS, respeta tu plan de bandas local
y las convenciones de digipeating (``WIDE1-1,WIDE2-1`` *no* siempre es
apropiado), y no enrutes tráfico ``NOGATE``/``RFONLY``. Eres responsable de todo
lo que transmita este dispositivo.
