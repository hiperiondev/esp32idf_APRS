.. _en-credits:

===================
Credits and License
===================

Authorship
==========

* **This project and the modem component:** Emiliano Augusto González —
  **LU3VEA** — ``lu3vea @ gmail . com`` · https://github.com/hiperiondev

Lineage
=======

The modem is based on, and owes its DSP lineage to, three earlier projects.
Please contact their authors for information about those projects:

* **VP-Digi** — SQ8VPS — https://github.com/sq8vps/vp-digi
* **ESP32APRS_Audio** — nakhonthai — https://github.com/nakhonthai/ESP32APRS_Audio
* **LibAPRS** — Mark Qvist — https://github.com/markqvist/LibAPRS

The configuration schema, web-admin layout and dashboard semantics follow the
reference **esp32idf_APRS / ESP32APRS** project so that existing ``config.json``
files and user expectations carry over.

Bundled components
==================

* **littlefs** — ARM / joltwallet (BSD-3-Clause), via the ESP component
  registry.
* **esp-idf-lib** ``bmp180`` / ``i2cdev`` / ``esp_idf_lib_helpers`` — via the
  ESP component registry, for the BMP180 sensor driver.

License
=======

**GNU General Public License v3.0** — see the ``LICENSE`` file in the
repository.

Bundled ``managed_components/joltwallet__littlefs`` carries its own license
(BSD-3-Clause for littlefs itself).

Amateur radio disclaimer
========================

Transmitting on amateur radio frequencies requires a valid licence for your
country and band. **Set a real callsign** (the default is ``NOCALL``), use a
legitimate APRS-IS passcode, respect your local band plan and digipeating
conventions (``WIDE1-1,WIDE2-1`` is *not* always appropriate), and do not gate
``NOGATE``/``RFONLY`` traffic. You are responsible for everything this device
transmits.
