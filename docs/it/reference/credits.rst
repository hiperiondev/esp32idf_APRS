.. _it-credits:

========================
Riconoscimenti e licenza
========================

Paternità
=========

* **Questo progetto e il componente modem:** Emiliano Augusto González —
  **LU3VEA** — ``lu3vea @ gmail . com`` · https://github.com/hiperiondev

Lignaggio
=========

Il modem si basa su, e deve il suo lignaggio DSP a, tre progetti precedenti. Ti
preghiamo di contattare i loro autori per informazioni su quei progetti:

* **VP-Digi** — SQ8VPS — https://github.com/sq8vps/vp-digi
* **ESP32APRS_Audio** — nakhonthai — https://github.com/nakhonthai/ESP32APRS_Audio
* **LibAPRS** — Mark Qvist — https://github.com/markqvist/LibAPRS

Lo schema di configurazione, la disposizione dell'amministrazione web e la
semantica della dashboard seguono il progetto di riferimento **esp32idf_APRS /
ESP32APRS** così che i file ``config.json`` esistenti e le aspettative
dell'utente siano mantenuti.

Componenti integrati
====================

* **littlefs** — ARM / joltwallet (BSD-3-Clause), tramite il registro dei
  componenti di ESP.
* **esp-idf-lib** ``bmp280`` / ``bmp180`` / ``i2cdev`` / ``esp_idf_lib_helpers``
  — tramite il registro dei componenti di ESP, per i driver dei sensori BME280/BMP280
  e BMP180.

Licenza
=======

**GNU General Public License v3.0** — vedi il file ``LICENSE`` nel repository.

Il ``managed_components/joltwallet__littlefs`` integrato porta la propria licenza
(BSD-3-Clause per littlefs stesso).

Avviso legale radioamatoriale
=============================

Trasmettere su frequenze radioamatoriali richiede una licenza valida per il tuo
paese e banda. **Imposta un indicativo reale** (il predefinito è ``NOCALL``), usa
un passcode APRS-IS legittimo, rispetta il tuo piano di banda locale e le
convenzioni di digipeating (``WIDE1-1,WIDE2-1`` *non* è sempre appropriato), e non
inoltrare traffico ``NOGATE``/``RFONLY``. Sei responsabile di tutto ciò che questo
dispositivo trasmette.
