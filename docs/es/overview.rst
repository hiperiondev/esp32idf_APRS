.. _es-overview:

==============
Visión general
==============

Qué es esto
===========

``esp32idf_APRS`` es un proyecto ESP-IDF **v6.x** (probado y fijado en IDF
**6.0.2**) que convierte una placa ESP32 DevKit desnuda más una interfaz de
audio económica en una estación APRS completa y autónoma. Todo se ejecuta en el
propio ESP32 — no hay núcleo Arduino, ni ``String``, ni PlatformIO, ni
biblioteca DSP externa. Toda la cadena de señal, desde el demodulador por
correlación pasando por la recuperación de bits con DPLL, NRZI, el ensamblador
HDLC, el códec AX.25 y la corrección de errores hacia adelante Reed–Solomon
FX.25, se ejecuta en el microcontrolador usando únicamente el SAR-ADC en modo
continuo/DMA, el DAC y un temporizador de propósito general.

En una frase: el firmware

* **demodula** audio AFSK/FSK desde la salida de altavoz o discriminador de una
  radio en el **ADC1**,
* **decodifica** tramas HDLC/AX.25 (opcionalmente con corrección de errores
  FX.25),
* las **enruta (gate)** hacia APRS-IS por Wi-Fi (``qAR``/``qAO``),
* las **repite (digipeat)** de vuelta por RF (WIDEn-N, mediante una tabla de
  alias configurable por el operador),
* **baliza** su propia posición, meteorología y telemetría,
* **modula** y transmite tramas de vuelta a través del **DAC de 8 bits** del
  ESP32, activando la radio mediante un GPIO de PTT,
* y se configura enteramente a través de una **administración web HTTP**
  servida por el propio dispositivo — sin consola serie, sin recompilar para
  ajustes ordinarios.

Matriz de funciones
===================

.. list-table::
   :header-rows: 1
   :widths: 40 12 48

   * - Área
     - Estado
     - Notas
   * - AFSK 1200 Bd Bell 202 (APRS estándar)
     - ✅
     - demodulador dual, perfil por defecto
   * - AFSK 1200 Bd ITU V.23 (1300/2100 Hz)
     - ✅
     -
   * - AFSK 300 Bd (1600/1800 Hz)
     - ✅
     - estilo HF
   * - G3RUH FSK 9600 Bd
     - ✅
     - necesita audio plano/de discriminador
   * - Trama UI HDLC / AX.25 RX+TX
     - ✅
     - ``AX25_FRAME_MAX_SIZE = 329``
   * - FX.25 (FEC RS sobre AX.25)
     - ✅
     - modos solo-RX / RX+TX
   * - Activación de PTT (GPIO y polaridad en compilación)
     - ✅
     - GPIO validado; tiempo mínimo de **des-activación** en ejecución
   * - CSMA / ranura de tiempo TX / p-persistencia / preámbulo TXDelay
     - ✅
     - ``preamble``, ``tx_timeslot``, ``csma_persist``
   * - DCD (detección de portadora de datos)
     - ✅
     - derivado del demodulador; sin entrada de squelch por hardware
   * - IGate APRS-IS RF→INET
     - ✅
     - filtros, deduplicación, ``qAR``/``qAO``
   * - IGate APRS-IS INET→RF
     - ✅
     - filtro por tipo + budlist + opción de desempaquetado de terceros
   * - Interconexión APRS con BrandMeister (sin enlace DMR)
     - ✅
     - reconocimiento, filtrado y ruteo de mensajes sobre la sesión APRS-IS
       existente; página propia, apagada por defecto
   * - Filtro local de rango y prefijo RF→INET
     - ✅
     - distancia haversine + lista blanca de prefijos de indicativo
   * - Lista blanca / negra de indicativos (budlist)
     - ✅
     - por dirección, se compone (AND) con los filtros por tipo
   * - Lista de satélites digipetidores (ISS)
     - ✅
     - hasta 8 entradas, configurable desde la web (página IGate), sin recompilar
   * - Tamaño y ventana de la caché de supresión de duplicados
     - ✅
     - configurable desde la web (página IGate), compartido por IGate y Digipeater
   * - Digipeater
     - ✅
     - WIDEn-N mediante tabla de alias configurable, supresión de duplicados;
       los alias heredados ``RELAY``/``ECHO``/``GATE``/``TRACEn-N`` no vienen
       incorporados, pero pueden añadirse como filas de alias ordinarias
   * - Objetos / Ítems APRS propios
     - ✅
     - hasta 5, RF y/o INET, decaimiento de intervalo + repeticiones de kill
   * - Boletines APRS (BLN1..BLN5)
     - ✅
     - hasta 5, RF y/o INET, caducidad por boletín
   * - UI de chat de mensajes APRS (``/msgchat``)
     - ✅
     - página de bandeja/redacción sobre el motor de mensajería
   * - Respondedor de consultas APRS (APRS101 cap.15)
     - ✅
     - generales ``?APRS?``/``?WX?``/``?IGATE?``/``?QRU?`` + el conjunto dirigido
       (``?APRSD``/``?APRSH``/``?APRSM``/``?APRSO``/``?APRSP``/``?APRSS``/
       ``?APRST``/``?PING?``), con límites de tasa por tipo y por origen
   * - Balizas de posición fija (tracker / igate / digi)
     - ✅
     - una tarea planificadora de balizas compartida
   * - Receptor GNSS NMEA (UART propia) + página de vista en vivo
     - ✅
     - interruptor maestro; RMC/GGA/GSA/GSV/VTG, multiconstelación, vista web
       a 1 Hz
   * - Rastreo GPS en vivo + SmartBeaconing
     - ✅
     - solo baliza Tracker; intervalo adaptativo por velocidad y corner-pegging
   * - Mensajería APRS + ack/reintento
     - ✅
     - RF y/o INET
   * - Administración web (autenticación HTTP Basic)
     - ✅
     - 22 páginas de la barra lateral + selector de símbolo, panel en vivo
   * - Registro de tráfico en vivo + tabla de últimos escuchados
     - ✅
     - long-poll JSON (``?since=<seq>``)
   * - Almacenamiento LittleFS, subir/descargar/borrar/formatear
     - ✅
     - partición de 512 KB
   * - Sincronización horaria SNTP (3 hosts)
     - ✅
     - reloj siempre en UTC
   * - Control de frecuencia de CPU (80/160/240 MHz)
     - ✅
     - ``esp_pm_configure()``
   * - Wi-Fi AP / STA / AP+STA, escaneo, potencia TX
     - ✅
     - 5 ranuras STA (se usa la primera habilitada)
   * - Localización (EN / ES / IT)
     - ✅
     - en compilación, un idioma por imagen
   * - Actualización OTA
     - ✅
     - ranuras ``ota_0``/``ota_1``, reversión automática si falla el arranque
   * - Informe meteorológico APRS propio
     - ✅
     - refresco de sensores a 1 Hz, promediado opcional, baliza WX al aire
   * - Marco de controladores de sensores locales (``sensors_local``)
     - ✅
     - registro dinámico en ejecución, controladores autorregistrados
   * - Codificación/baliza de telemetría APRS al aire
     - ✅
     - analógicos A1–A5 + digitales B1–B8, informe ``T#nnn`` + metadatos

Filosofía de diseño
===================

Varias decisiones arquitectónicas deliberadas se repiten a lo largo del código
y conviene interiorizarlas de antemano:

**Una configuración residente, una copia viva.**
   Una única instancia ``app_config_t g_config`` es la fuente de verdad que lee
   cada subsistema. Persiste en ``/storage/config.json``. Los subsistemas nunca
   duplican el estado de configuración; leen ``g_config`` directamente. Dos
   subsistemas que necesitan un estado propio más grande y específico de página
   lo guardan en archivos LittleFS separados en lugar de inflar ``g_config``:
   telemetría (``/storage/telemetry.json``), boletines
   (``/storage/bulletins.json``) y objetos/ítems (``/storage/objitems.json``).

**Cableado de placa en compilación, todo lo demás en ejecución.**
   Los tres pines de audio (ADC, DAC, PTT), la polaridad de PTT, la atenuación
   del ADC y las tasas de muestreo son *constantes de compilación* definidas en
   el ``CMakeLists.txt`` de nivel superior. Son decisiones de cableado físico,
   por lo que no se exponen en la administración web. Todo lo que un operador
   ajusta legítimamente sin recablear — perfil de modulación, preámbulo,
   ranura de tiempo, modo FX.25, filtros, indicativos, intervalos — es editable
   en ejecución y, en la mayoría de los casos, se aplica en vivo sin reiniciar.

**Estadísticas que reflejan la realidad, no la configuración.**
   Los contadores del panel (RF RX/TX, RF→INET, INET→RF, digi, descartes,
   errores) se rastrean en los puntos donde las tramas fluyen realmente,
   independientemente de si las funciones de IGate o digipeater están
   habilitadas — de modo que una configuración de monitor solo-RX sigue
   mostrando actividad de decodificación real en lugar de un muro de ceros.

**Fallar de forma ruidosa, fallar de forma segura.**
   La ruta de arranque de Wi-Fi está muy instrumentada: los códigos de razón de
   desconexión se registran, un dispositivo solo-STA sin nada a lo que unirse
   recurre a AP+STA para que la administración web siga accesible, y las
   reconexiones usan un retroceso creciente armado en un temporizador en lugar
   de una espera bloqueante dentro del bucle de eventos.

Linaje y créditos
=================

El proyecto y su componente de módem son de **Emiliano Augusto González
(LU3VEA)**. El linaje DSP del módem por software procede de tres proyectos
anteriores: **VP-Digi** (SQ8VPS), **ESP32APRS_Audio** (nakhonthai) y
**LibAPRS** (Mark Qvist). El esquema de configuración, la disposición de la
administración web y la semántica del panel siguen el proyecto de referencia
**ESP32APRS** para que los archivos ``config.json`` existentes y las
expectativas del operador se mantengan. Véase :ref:`es-credits` para la
atribución y la licencia completas.

El firmware se distribuye bajo la **Licencia Pública General GNU v3.0**.

.. warning::

   **Aviso legal de radioafición.** Transmitir en frecuencias de radioafición
   requiere una licencia válida para tu país y banda. Establece un indicativo
   real — el valor por defecto es ``NOCALL`` — usa un passcode legítimo de
   APRS-IS, respeta el plan de bandas local y las convenciones de digipeating
   (``WIDE1-1,WIDE2-1`` *no* siempre es apropiado), y no enrutes tráfico
   ``NOGATE``/``RFONLY``. Eres responsable de todo lo que transmita este
   dispositivo.

Comparación con el software APRS más popular
===============================================

Normalmente, una estación APRS se arma con piezas de software separadas,
cada una cubriendo una parte del trabajo: un módem/TNC por tarjeta de
sonido, un cliente con mapa e interfaz de mensajería y, a veces, un programa
dedicado de digipeater o IGate que corre en una PC o en una placa de cómputo
de placa única. La tabla siguiente compara los paquetes más usados frente a
``esp32idf_APRS`` función por función, para dejar claro qué reemplaza —y qué
no— un único ESP32 con este firmware.

La comparación cubre **Direwolf** (WB2OSZ — el módem/TNC de facto por
software para Linux/Windows/macOS, con digipeater e IGate incluidos),
**Xastir** (un cliente de escritorio X11/Linux maduro y muy configurable con
mapeo extenso), **YAAC** ("Yet Another APRS Client", KA2DDO — un cliente
Java multiplataforma con una interfaz moderna), **APRSIS32 / UI-View**
(clientes de escritorio solo para Windows, históricamente dominantes,
UI-View hoy sin mantenimiento/legado) y **APRSdroid** (el cliente móvil
Android más común). La mayoría de estos programas se suelen combinar entre
sí —por ejemplo, Direwolf como módem/TNC alimentando a Xastir o YAAC como
cliente— en lugar de usarse completamente solos; ``esp32idf_APRS`` es
inusual porque provee el equivalente del módem, la lógica de
gateway/digipeater *y* la interfaz de operador en un único firmware de
placa única.

.. list-table::
   :header-rows: 1
   :widths: 26 15 12 12 12 12 27

   * - Función
     - esp32idf_APRS
     - Direwolf
     - Xastir
     - YAAC
     - APRSIS32 / UI-View
     - Notas
   * - Funciona de forma autónoma, sin PC anfitrión
     - ✅
     - ❌ (necesita un SO anfitrión)
     - ❌
     - ❌
     - ❌
     - Diferenciador central de este proyecto: módem + lógica + UI en un
       único MCU.
   * - Módem AFSK/FSK por software (tarjeta de sonido)
     - ✅ (ADC/DAC en el chip)
     - ✅ (tarjeta de sonido de PC)
     - ➖ (normalmente vía Direwolf)
     - ➖ (normalmente vía Direwolf/AGW)
     - ➖ (vía TNC o AGW)
     - Solo Direwolf y este proyecto *son* el módem; los demás consumen uno.
   * - Soporte de TNC por hardware / KISS
     - ❌
     - ✅
     - ✅
     - ✅
     - ✅
     - Este firmware es su propio módem; no habla con un TNC externo.
   * - Tramas UI AX.25 RX/TX
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - Base común de todo el software APRS.
   * - FX.25 (FEC Reed–Solomon)
     - ✅
     - ✅
     - ❌
     - ➖ (solo del lado del cliente)
     - ❌
     - Direwolf y este proyecto codifican/decodifican FX.25 directamente.
   * - IGate (RF → APRS-IS)
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - Común a casi todo el software APRS.
   * - IGate (APRS-IS → RF, "bidireccional")
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - Normalmente filtrado por filtros locales/de duplicados/de tipo en
       todos los casos.
   * - Digipeater (WIDEn-N / TRACEn-N)
     - ✅
     - ✅
     - ✅
     - ✅
     - ➖ (limitado)
     -
   * - Administración web integrada
     - ✅
     - ❌ (archivo de config + interfaces web de terceros opcionales)
     - ❌ (GUI nativa X11)
     - ❌ (GUI nativa Java Swing)
     - ❌ (GUI nativa de Windows)
     - Este firmware es el único configurado enteramente desde un navegador.
   * - Mapa en vivo
     - ❌
     - ❌ (solo texto/registro)
     - ✅ (extenso)
     - ✅
     - ✅
     - Fuera de alcance de forma deliberada — esto es una estación, no un
       cliente de mapeo.
   * - Rastreo en vivo por GPS
     - ✅ (receptor NMEA + SmartBeaconing)
     - ➖ (vía GPS/tracker conectado)
     - ✅
     - ✅
     - ✅
     - Un módulo GNSS en su propia UART alimenta la baliza Tracker; las balizas
       de IGate y digipetidor siguen siendo de posición fija. Véase
       :ref:`es-beacons`.
   * - Baliza de posición (estación fija)
     - ✅
     - ✅ (según configuración)
     - ✅
     - ✅
     - ✅
     -
   * - Mensajería APRS (chat, ack/reintento)
     - ✅ (UI de chat web)
     - ➖ (vía cliente conectado)
     - ✅
     - ✅
     - ✅
     -
   * - Boletines / anuncios
     - ✅
     - ➖ (retransmite, no compone)
     - ✅
     - ✅
     - ✅
     -
   * - Objetos / ítems
     - ✅ (hasta 5, de la propia estación)
     - ➖ (retransmite, no compone)
     - ✅
     - ✅
     - ✅
     -
   * - Reporte de estación meteorológica
     - ✅ (marco de sensores nativo)
     - ➖ (vía software meteorológico externo)
     - ✅ (vía feed externo)
     - ✅ (vía feed externo)
     - ✅ (vía feed externo)
     - Este firmware lee sensores y codifica los reportes WX por sí mismo,
       en el chip.
   * - Telemetría (canales analógicos/digitales)
     - ✅ (A1–A5, B1–B8, EQNS/UNIT/BITS)
     - ❌
     - ➖ (solo visualización)
     - ➖ (solo visualización)
     - ➖ (solo visualización)
     -
   * - APRStt (gateway DTMF a APRS)
     - ❌
     - ✅
     - ❌
     - ❌
     - ❌
     - No implementado en este proyecto.
   * - Actualización de firmware/software OTA
     - ✅ (doble slot OTA, auto-rollback)
     - ➖ (gestor de paquetes del SO)
     - ➖ (gestor de paquetes del SO)
     - ➖ (gestor de paquetes del SO)
     - ➖ (instalador manual)
     - "OTA" aquí es específico del modelo de actualización de firmware
       embebido.
   * - Interfaz multi-idioma
     - ✅ (EN/ES/IT, en tiempo de compilación)
     - ❌ (solo inglés)
     - ➖ (traducciones parciales)
     - ❌ (solo inglés)
     - ❌ (solo inglés)
     -
   * - Costo / huella de hardware
     - Una sola placa ESP32 (~USD 5–10) + interfaz de audio
     - PC/RPi + tarjeta de sonido + radio
     - PC/RPi + TNC + radio
     - PC/teléfono + TNC + radio
     - PC Windows + TNC + radio
     -

Leyenda: ✅ implementado / nativo · ➖ parcial, o disponible solo a través de
otro programa en la cadena · ❌ no implementado / no aplicable.

**Lo que este proyecto sí implementa deliberadamente**, igualando el núcleo
de lo que ofrece una estación APRS de escritorio completa: el módem en sí,
el encuadre AX.25/FX.25, el IGate bidireccional, el digipeating, las
balizas, la mensajería, los boletines, los objetos, la
meteorología y la telemetría, todo accesible desde una interfaz web
autoalojada sin software complementario en una PC.

**Lo que este proyecto deliberadamente no implementa**: no tiene
visualización de mapa, y no incluye un gateway APRStt de DTMF a APRS. Estos elementos están fuera de alcance de forma
intencional para una estación embebida sin cabeza, configurada por
navegador — un cliente de mapeo complementario como YAAC, Xastir o
`aprs.fi <https://aprs.fi>`__ sigue siendo la forma natural de *visualizar*
el tráfico que este firmware genera y retransmite.
