.. _es-troubleshooting:

=======================
Resolución de problemas
=======================

"Cambié a modo Station, guardé, reinicié, y no pasa nada."
==========================================================

Lee el log de arranque — esta ruta está muy instrumentada:

* ``esp_wifi_connect()`` solo es legal una vez que la estación ha arrancado
  *realmente* (``WIFI_EVENT_STA_START``). La conexión se emite desde ese
  manejador y cada intento registra su resultado.
* Si ninguna ranura de Cliente Wi-Fi está **habilitada con un SSID**, el firmware
  vuelca cada ranura y te dice cuál es el error ("habilitada, pero el SSID está
  VACÍO" vs "tiene un SSID, pero 'Enable' no está marcado").
* Solo-STA sin nada a lo que unirse recurre a AP+STA para que la administración
  web siga arriba.

Los códigos de razón de desconexión se registran:

.. list-table::
   :header-rows: 1
   :widths: 44 56

   * - Razón
     - Significado
   * - 15, 204
     - contraseña equivocada
   * - 201
     - SSID no visible: nombre equivocado, fuera de rango, o solo 5 GHz
   * - 2 / 8 / 200
     - roaming ordinario / caídas del lado del AP

"El AP no asocia en absoluto."
==============================

Un ``wifi_config_t`` a cero deja ``pmf_cfg.capable = false``, y los AP
WPA3 / WPA2-con-PMF-requerido rechazan tal estación. El firmware pone *capable,
no required*, que funciona contra AP antiguos y nuevos.

"El arranque se cuelga ~5 segundos."
====================================

Esperado: ``modem_init()`` se bloquea mientras ``ModemCalibrateSampleRate()`` mide
el reloj real del ADC. Una vez por arranque.

"Las balizas al arrancar no transmiten."
========================================

Esperado: ``aprs_service_start()`` corre antes de ``modem_init()``, así que las
balizas tempranas se descartan con un log de depuración hasta ``s_modemReady``.

"El LOOP TEST falla con 'no se recibió paquete de vuelta'."
===========================================================

Comprueba la atenuación del ADC: el DAC oscila el raíl completo mientras una
atenuación de 0 dB solo mide ~0–1,1 V, recortando el tono más allá de la capacidad
del demodulador para enganchar. El componente codifica ``ADC_ATTEN_DB_12``, que es
correcto; si lo sobreescribiste, restáuralo. Confirma también el cable de bucle
GPIO25 → GPIO33.

"El IGate dice unverified."
===========================

``aprs_mycall`` / ``aprs_passcode`` equivocados. El banner se registra; también la
línea de login exacta, incluida la cadena de filtro, así que un filtro mal formado
es visible de inmediato.

"Todo funciona pero aprs.fi no muestra mi estación."
====================================================

Balizas: habilita la baliza de posición y al menos una de ``loc2rf`` /
``loc2inet``, y pon coordenadas reales. Retransmitir tráfico nunca te anuncia a ti.

"9600 Bd pierde tramas."
========================

Esa es la patología que la tasa del ADC, el tamaño de la trama de conversión y la
separación de núcleos se cambiaron para arreglar (véase :ref:`es-dsp-signal-chain`).
Si sobreescribiste ``MODEM_ADC_SAMPLERATE``, ``MODEM_ADC_CONV_FRAME``,
``MODEM_DAC_TIMER_CORE`` o ``MODEM_ADC_ISR_CORE``, reviértelos. Confirma también que
estás alimentando audio **plano/de discriminador**.

"El LED de PTT se queda encendido en reposo."
=============================================

La lógica de PTT es correcta pero su polaridad es una constante de compilación. Con
cableado activo-bajo, reposo/sin-activar acciona el pin **alto** (LED encendido) y
activado lo acciona bajo. Si tu hardware es activo-alto, pon
``MODEM_PTT_ACTIVE_HIGH=1`` en el ``CMakeLists.txt`` de nivel superior y haz una
recompilación limpia completa — el valor es una constante de compilación horneada
en ``afsk.c``.
