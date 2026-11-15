.. _en-sensors:

=============
Local sensors
=============

Weather reports and telemetry need numbers to send, and those come from
sensors wired to the ESP32 itself. The firmware ships drivers for the usual
I2C environmental sensors; each one registers itself at start-up and then
appears as a choice in the *Channel* columns of the *Weather* and
*Telemetry* pages. How the registry works, and how to add a driver of your
own, is in :ref:`en-sensor-framework`.

Available drivers
=================

.. list-table::
   :header-rows: 1
   :widths: 20 30 22 28

   * - Driver
     - Measures
     - Default
     - Kconfig option
   * - ``bme280``
     - temperature, humidity, pressure (BME280); temperature and pressure
       (BMP280)
     - **on**
     - ``SENSORS_LOCAL_BME280_DRIVER``
   * - ``bmp180``
     - temperature, pressure
     - off
     - ``SENSORS_LOCAL_BMP180_DRIVER``
   * - ``wx-example``
     - random weather values, no hardware
     - off
     - ``SENSORS_LOCAL_WEATHER_EXAMPLE_DRIVER``
   * - ``tlm-example``
     - random telemetry values, no hardware
     - off
     - ``SENSORS_LOCAL_TELEMETRY_EXAMPLE_DRIVER``

Drivers are selected with ``idf.py menuconfig`` → *Sensors Local*. The
``bme280`` driver reads the chip ID at start-up: a BME280 offers temperature,
humidity and pressure, a BMP280 (the same part without the humidity element)
only temperature and pressure. Both real drivers may be enabled together; each
chip answers on its own I2C address.

The two example drivers exist for bench tests of the pages and the encoders.
Their values look exactly like real readings once they are mapped to a channel
and **are transmitted on air**, so leave them off on a station connected to a
radio.

Wiring
======

All sensors share one I2C bus:

.. list-table::
   :header-rows: 1
   :widths: 30 30 40

   * - Signal
     - ESP32 pin
     - Note
   * - SDA
     - GPIO21
     - ``SENSORS_LOCAL_I2C_SDA_GPIO``
   * - SCL
     - GPIO22
     - ``SENSORS_LOCAL_I2C_SCL_GPIO``
   * - VCC / GND
     - 3V3 / GND
     -

The bus pins and port are defined only in
``components/sensors_local/include/sensors_local_i2c.h``, and the web admin marks
them as taken (*Sensor I2C*) in its GPIO pickers, such as the message-alarm
pin, so they cannot be assigned twice. The BME280/BMP280 is expected at address 0x76
(``BME280.h``); a module with SDO pulled high answers on 0x77 and needs that
constant changed.

Mapping sensors to reports
==========================

.. figure:: /_static/webadmin/wx_fs02.png
   :alt: Sensor Mapping (enable / averaged / source channel) — the Weather page's mapping table. Each WX field picks the sensor channel it is read from; the list only offers drivers that can produce that field, and the Value column shows the live reading.
   :align: center
   :width: 90%

   **Sensor Mapping (enable / averaged / source channel)** — the Weather page's mapping table. Each WX field picks the sensor channel it is read from; the list only offers drivers that can produce that field, and the *Value* column shows the live reading.

.. figure:: /_static/webadmin/tlm_fs03.png
   :alt: Analog Channels (A1-A5) — the Telemetry page offers the same pickers for the analog and digital channels; with only the BME280 enabled, no telemetry source exists and the lists stay at (none).
   :align: center
   :width: 90%

   **Analog Channels (A1-A5)** — the Telemetry page offers the same pickers for the analog and digital channels; with only the BME280 enabled, no telemetry source exists and the lists stay at *(none)*.

Readings are refreshed once a second; for a weather field ticked in the *Avg*
column the beacon sends the average of those readings instead of the latest one. The mapping is stored by driver
name, so enabling or disabling another driver does not move the existing
assignments. The Telegram bot's ``/sensors`` command lists every mapped field
with its current value.
