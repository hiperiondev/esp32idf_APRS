.. _en-troubleshooting:

===============
Troubleshooting
===============

"I switched to Station mode, saved, rebooted, and nothing happens."
===================================================================

Read the boot log — this path is heavily instrumented:

* ``esp_wifi_connect()`` is only legal once the station has *actually* started
  (``WIFI_EVENT_STA_START``). The connect is issued from that handler and every
  attempt logs its result.
* If no Wi-Fi Client slot is **enabled with an SSID**, the firmware dumps every
  slot and tells you which mistake it is ("enabled, but the SSID is EMPTY" vs
  "has an SSID, but 'Enable' is not ticked").
* STA-only with nothing to join falls back to AP+STA so the web admin stays up.

Disconnect reason codes are logged:

.. list-table::
   :header-rows: 1
   :widths: 44 56

   * - Reason
     - Meaning
   * - 15, 204
     - wrong password
   * - 201
     - SSID not visible: wrong name, out of range, or 5 GHz-only
   * - 2 / 8 / 200
     - ordinary roaming / AP-side drops

"AP won't associate at all."
============================

A zeroed ``wifi_config_t`` leaves ``pmf_cfg.capable = false``, and
WPA3 / WPA2-with-PMF-required APs refuse such a station. The firmware sets
*capable, not required*, which works against both old and new APs.

"Boot hangs for ~5 seconds."
============================

Expected: ``modem_init()`` blocks while ``ModemCalibrateSampleRate()`` measures
the real ADC clock. Once per boot.

"Beacons at boot don't transmit."
=================================

Expected: ``aprs_service_start()`` runs before ``modem_init()``, so early
beacons are dropped with a debug log until ``s_modemReady``.

"LOOP TEST fails with 'no packet received back'."
=================================================

Check the ADC attenuation: the DAC swings the full rail while a 0 dB attenuation
only measures ~0–1.1 V, clipping the tone beyond the demodulator's ability to
lock. The component hard-codes ``ADC_ATTEN_DB_12``, which is correct; if you
overrode it, put it back. Also confirm the GPIO25 → GPIO33 loopback wire.

"IGate says unverified."
========================

Wrong ``aprs_mycall`` / ``aprs_passcode``. The banner is logged; so is the exact
login line, including the filter string, so a malformed filter is visible
immediately.

"Everything works but aprs.fi doesn't show my station."
=======================================================

Beacons: enable the position beacon and at least one of ``loc2rf`` /
``loc2inet``, and set real coordinates. Relaying traffic never announces you.

"9600 Bd loses frames."
=======================

That is the pathology the ADC rate, conversion-frame size and core split were
changed to fix (see :ref:`en-dsp-signal-chain`). If you overrode
``MODEM_ADC_SAMPLERATE``, ``MODEM_ADC_CONV_FRAME``, ``MODEM_DAC_TIMER_CORE`` or
``MODEM_ADC_ISR_CORE``, revert them. Also confirm you are feeding
**flat/discriminator** audio.

"The PTT LED stays on when idle."
=================================

The PTT logic is correct; its polarity is a compile-time constant, and the
shipped board definition is ``MODEM_PTT_ACTIVE_HIGH=1`` (active-high) in the
top-level ``CMakeLists.txt``. Active-high means idle/unkeyed drives the pin
**low** and keyed drives it high; active-low is the mirror image, so idle leaves
the pin high and an LED on that pin stays lit. If the LED tracks the opposite of
what you expect, your driver stage inverts (an optocoupler does; a plain NPN
low-side switch does not): flip the macro to the other value and do a full clean
rebuild — the value is baked into ``afsk.c``, so an incremental build will not
pick it up.
