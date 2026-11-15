.. _es-sensors:

================
Sensores locales
================

Los informes de clima y la telemetría necesitan números para enviar, y esos
vienen de sensores conectados al propio ESP32. El firmware trae drivers para
los sensores ambientales I2C habituales; cada uno se registra solo al arrancar
y aparece como opción en las columnas *Canal* de las páginas *Clima* y
*Telemetría*. Cómo funciona el registro, y cómo agregar un driver propio,
está en :ref:`es-sensor-framework`.

Drivers disponibles
===================

.. list-table::
   :header-rows: 1
   :widths: 20 30 22 28

   * - Driver
     - Mide
     - Por defecto
     - Opción de Kconfig
   * - ``bme280``
     - temperatura, humedad, presión (BME280); temperatura y presión (BMP280)
     - **activado**
     - ``SENSORS_LOCAL_BME280_DRIVER``
   * - ``bmp180``
     - temperatura, presión
     - desactivado
     - ``SENSORS_LOCAL_BMP180_DRIVER``
   * - ``wx-example``
     - valores de clima aleatorios, sin hardware
     - desactivado
     - ``SENSORS_LOCAL_WEATHER_EXAMPLE_DRIVER``
   * - ``tlm-example``
     - valores de telemetría aleatorios, sin hardware
     - desactivado
     - ``SENSORS_LOCAL_TELEMETRY_EXAMPLE_DRIVER``

Los drivers se eligen con ``idf.py menuconfig`` → *Sensors Local*. El driver
``bme280`` lee el ID del chip al arrancar: un BME280 ofrece temperatura,
humedad y presión, un BMP280 (la misma pieza sin el elemento de humedad) solo
temperatura y presión. Los dos drivers reales pueden estar activados a la vez;
cada chip responde en su propia dirección I2C.

Los dos drivers de ejemplo existen para pruebas de banco de las páginas y los
codificadores. Sus valores no se distinguen de lecturas reales una vez
asignados a un canal y **se transmiten al aire**, así que hay que dejarlos
apagados en una estación conectada a una radio.

Cableado
========

Todos los sensores comparten un mismo bus I2C:

.. list-table::
   :header-rows: 1
   :widths: 30 30 40

   * - Señal
     - Pin del ESP32
     - Nota
   * - SDA
     - GPIO21
     - ``SENSORS_LOCAL_I2C_SDA_GPIO``
   * - SCL
     - GPIO22
     - ``SENSORS_LOCAL_I2C_SCL_GPIO``
   * - VCC / GND
     - 3V3 / GND
     -

Los pines y el puerto del bus se definen solo en
``components/sensors_local/include/sensors_local_i2c.h``, y la administración
web los marca como ocupados (*Sensor I2C*) en sus selectores de GPIO, como el
del pin de alarma de mensajes, para que no se puedan asignar dos veces. El
BME280/BMP280 se espera en la dirección 0x76 (``BME280.h``); un módulo con SDO
a positivo responde en 0x77 y necesita cambiar esa constante.

Asignar sensores a los informes
===============================

.. figure:: /_static/webadmin/wx_fs02.png
   :alt: Mapeo de sensores (habilitar / promediado / canal de origen) — la tabla de asignación de la página Clima. Cada campo WX elige el canal de sensor del que se lee; la lista ofrece solo drivers capaces de producir ese campo, y la columna Valor muestra la lectura en vivo.
   :align: center
   :width: 90%

   **Mapeo de sensores (habilitar / promediado / canal de origen)** — la tabla de asignación de la página Clima. Cada campo WX elige el canal de sensor del que se lee; la lista ofrece solo drivers capaces de producir ese campo, y la columna *Valor* muestra la lectura en vivo.

.. figure:: /_static/webadmin/tlm_fs03.png
   :alt: Canales analógicos (A1-A5) — la página Telemetría ofrece los mismos selectores para los canales analógicos y digitales; con solo el BME280 activado no hay ninguna fuente de telemetría y las listas quedan en (ninguno).
   :align: center
   :width: 90%

   **Canales analógicos (A1-A5)** — la página Telemetría ofrece los mismos selectores para los canales analógicos y digitales; con solo el BME280 activado no hay ninguna fuente de telemetría y las listas quedan en *(ninguno)*.

Las lecturas se actualizan una vez por segundo; para un campo de clima marcado en la
columna *Prom* la baliza envía el promedio de esas lecturas en lugar de la
última. La asignación se guarda por nombre de driver, así que activar o
desactivar otro driver no mueve las asignaciones existentes. El comando
``/sensors`` del bot de Telegram lista cada campo asignado con su valor
actual.
