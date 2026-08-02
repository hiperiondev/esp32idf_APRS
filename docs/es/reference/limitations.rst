.. _es-limitations:

===============================
Estado y limitaciones conocidas
===============================

El firmware es **trabajo en progreso**. La ruta de transmisión RF, el IGate, el
digipeater, las balizas, la meteorología, la telemetría, la mensajería y la
administración web son todos funcionales.

Tabla comparativa de funcionalidades
=======================================

La siguiente tabla compara la funcionalidad implementada en este proyecto con
la unión de funciones presentes en los programas APRS más populares (clientes
de escritorio/mapeo como Xastir, APRSIS32 y YAAC; TNC por software como
Direwolf y UZ7HO Soundmodem; y pilas de iGate/digipeater sin interfaz gráfica
como aprx y VP-Digi). Ningún programa de ese ecosistema implementa todas las
filas por sí solo — eso es normal y esperado. La leyenda es:

* ✅ — Implementado y funcionando
* ⚠️ — Implementación parcial / limitada
* ❌ — No implementado

Módem / Capa 2
----------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacidad específica
     - Habitual en software APRS popular
     - Aquí
     - Notas sobre la implementación de este proyecto
   * - AFSK 1200 Bd Bell 202 (APRS VHF estándar)
     - ✅ (Direwolf, UZ7HO, VP-Digi, TNC de hardware)
     - ✅
     - Perfil predeterminado; doble demodulador ejecutándose en paralelo para mejorar la probabilidad de decodificación
   * - AFSK 1200 Bd V.23
     - ⚠️ (Direwolf lo soporta; muchos clientes no)
     - ✅
     - Perfil de módem seleccionable n.º 2
   * - AFSK 300 Bd (APRS HF)
     - ✅ (Direwolf, UZ7HO)
     - ✅
     - Perfil de módem seleccionable n.º 0
   * - FSK G3RUH 9600 Bd
     - ✅ (Direwolf, TNC de paquete dedicados)
     - ✅
     - Perfil de módem seleccionable n.º 3
   * - Encuadre HDLC / codificación-decodificación AX.25 UI
     - ✅ (universal)
     - ✅
     - Ruta TX/RX completa por software, sobre ADC/DAC
   * - FEC Reed-Solomon FX.25
     - ⚠️ (Direwolf sí; la mayoría de TNC de hardware no)
     - ✅
     - Modos solo RX o RX+TX, compatible hacia atrás con AX.25 plano
   * - IL2P (alternativa a FX.25)
     - ⚠️ (solo Direwolf)
     - ❌
     - No implementado
   * - Protocolo KISS (serie o TCP) para actuar como TNC de software cliente externo
     - ✅ (Direwolf, UZ7HO, prácticamente todos los soundmodems)
     - ❌
     - No implementado. Sin servidor KISS/AGWPE serie ni de red — este proyecto no puede actuar como "back end" TNC para Xastir/APRSIS32/YAAC, etc.
   * - Protocolo AGWPE
     - ⚠️ (TNC centrados en Windows)
     - ❌
     - No implementado
   * - CSMA / detección de canal ocupado antes de transmitir
     - ✅
     - ✅
     - Ranura de tiempo TX (``tx_timeslot``) + control de preámbulo/TXDelay
   * - Activación de PTT (sin VOX, GPIO de hardware)
     - ✅
     - ✅
     - GPIO y polaridad en tiempo de compilación; tiempo mínimo de retención de desactivación ajustable en ejecución
   * - Herramienta integrada de bucle/autoprueba RF
     - ⚠️ (poco común)
     - ✅
     - "LOOP TEST" — transmite un paquete con token y verifica que toda la cadena RX lo decodifique de vuelta, con diagnóstico detallado por etapa

IGate (RF <-> APRS-IS)
------------------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacidad específica
     - Habitual en software APRS popular
     - Aquí
     - Notas sobre la implementación de este proyecto
   * - Reenvío RF -> APRS-IS
     - ✅ (universal)
     - ✅
     - Pipeline completo: duplicados -> guarda de longitud mínima -> filtro de tokens de ruta -> regla de sat-gate -> filtro por tipo de carga -> guarda de rango -> guarda de prefijo -> budlist
   * - Reenvío APRS-IS -> RF (IGate bidireccional)
     - ✅ (modo igate de Direwolf, aprx, VP-Digi)
     - ✅
     - Supresión de eco de informes propios, filtro por tipo de carga, desempaquetado restringido de terceros, budlist
   * - Supresión de paquetes duplicados
     - ✅
     - ✅
     - Caché de 10 entradas / 30 s, compartida con el digipeater
   * - Inserción de Q-construct ``qAR``/``qAO``
     - ✅
     - ✅
     - ``qAR`` estándar; también soportada la forma ``qAO`` de sat-gate
   * - Cadena de filtro APRS-IS del lado servidor (``r/``, ``p/``, ``t/``, ``b/``...)
     - ✅
     - ✅
     - Enviada tal cual en la línea de login, con validación local de gramática antes de enviarla
   * - Guarda de rango local (distancia ortodrómica)
     - ⚠️ (algunos, p. ej. ``filter`` de aprx)
     - ✅
     - Distancia haversine respecto a "Mi Estación"; soporta posiciones comprimidas y no comprimidas
   * - Lista blanca de prefijos de indicativo local
     - ⚠️ (poco común como función de primera clase)
     - ✅
     - Lista de prefijos separados por comas (p. ej. ``EA,EB,EC``)
   * - Budlist de indicativos (lista blanca/negra)
     - ✅
     - ✅
     - Modo por dirección: desactivado / lista blanca / lista negra
   * - Filtrado por tipo de carga (msg/status/tlm/wx/obj/item/query/buoy/position)
     - ✅ (mayormente vía filtros de APRS-IS)
     - ✅
     - Local, basado en máscara de bits, aplicado en ambas direcciones independientemente del filtro del servidor
   * - Manejo de paquetes de terceros (``}``) / protección contra bucles
     - ✅ (crítico, a menudo manual)
     - ✅
     - Desactivado por defecto; el desempaquetado opcional requiere modo lista blanca exclusivamente para evitar bucles de IGate
   * - Reconexión automática a APRS-IS con backoff
     - ✅
     - ✅
     - Reconexión TCP automática, relee la configuración en cada reconexión
   * - Login a APRS-IS basado en passcode
     - ✅
     - ✅
     - Línea de login estándar ``user/pass/vers/filter``; se muestra la respuesta verified/unverified del servidor
   * - Múltiples servidores APRS-IS / failover
     - ⚠️ (algunos soportan listas de servidores)
     - ❌
     - Solo un host/puerto configurado
   * - Estadísticas por motivo de descarte
     - ⚠️ (poco común, normalmente solo totales)
     - ✅
     - Contadores nombrados (``DROP_TOO_SHORT``, ``DROP_PATH_TOKEN``, ``DROP_RANGE_FILTER``, etc.)

Digipeater
-----------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacidad específica
     - Habitual en software APRS popular
     - Aquí
     - Notas sobre la implementación de este proyecto
   * - Digipeating de inundación WIDEn-N
     - ✅ (universal)
     - ✅
     - Decremento del contador de saltos + inserción del propio indicativo
   * - Digipeating de traza explícita TRACEn-N
     - ✅
     - ✅
     - Cada salto inserta su indicativo
   * - Alias heredados RELAY / ECHO / GATE
     - ✅
     - ✅
     - Todos sustituidos por el indicativo propio del digipeater
   * - Contador de saltos codificado en el SSID de destino (heredado)
     - ⚠️ (TNC más antiguos)
     - ✅
     - Reconocido y manejado
   * - Supresión de duplicados / ping-pong de digipeating
     - ✅
     - ✅
     - Comparte la caché de duplicados del IGate
   * - Filtrado de digipeating por indicativo (solo repetir ciertas fuentes)
     - ⚠️ (algunos, p. ej. VP-Digi)
     - ❌
     - No se expone como un filtro específico del digipeater (la budlist del IGate no es lo mismo que un filtro del digi)
   * - Digipeating viscoso/preventivo
     - ⚠️ (poco común, TNC avanzados)
     - ❌
     - No implementado

Seguimiento / Balizamiento
------------------------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacidad específica
     - Habitual en software APRS popular
     - Aquí
     - Notas sobre la implementación de este proyecto
   * - Entrada de posición GPS en vivo (NMEA)
     - ✅ (universal en trackers móviles)
     - ❌
     - No implementado. Las balizas son solo de posición fija — no hay entrada de posición en vivo ni configuración relacionada con GPS
   * - Balizamiento de posición fija (estación base)
     - ✅
     - ✅
     - Posición/intervalo/símbolo/comentario independientes por rol (tracker, IGate, digi)
   * - Smart Beaconing (intervalo adaptativo por velocidad/rumbo)
     - ✅ (clientes móviles, OpenTracker)
     - ❌
     - Sin GPS, no aplica
   * - Rumbo/velocidad en informes de posición
     - ✅
     - ⚠️
     - Soportado en Objetos/Items, pero la baliza de tracker propia no tiene fuente de rumbo/velocidad en vivo (sin GPS)
   * - Codificación de posición comprimida (Base-91)
     - ✅
     - ✅
     - La página Tracker ofrece una opción de posición comprimida; el decodificador también la entiende
   * - Codificación de posición Mic-E (TX)
     - ⚠️ (sobre todo firmware de tracker móvil)
     - ✅
     - La página del beacon Tracker ofrece una opción Mic-E (``aprs_mice_encode()``); solo posición fija, por lo que el curso/velocidad se envía siempre como "desconocido" y el código de mensaje queda fijo en Off Duty
   * - PHG / potencia-altura-ganancia-directividad
     - ✅
     - ✅
     - Expuesto en la página de baliza del IGate
   * - Altitud en balizas
     - ✅
     - ✅
     - Campo de altitud a nivel de estación, usado por las balizas

Mensajería
-----------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacidad específica
     - Habitual en software APRS popular
     - Aquí
     - Notas sobre la implementación de este proyecto
   * - Mensajería APRS dirigida
     - ✅ (universal)
     - ✅
     - Enrutamiento RF y/o APRS-IS por mensaje
   * - Confirmación de mensaje (``ackNNN``)
     - ✅
     - ✅
     - Auto-ack al recibir, auto-reintento hasta confirmar
   * - Reintento de mensajes con cantidad/intervalo configurables
     - ✅
     - ✅
     - ``msg_retry`` / ``msg_interval``, evaluado a 1 Hz
   * - UI de chat/bandeja de entrada integrada
     - ✅ (Xastir, YAAC, APRSIS32)
     - ✅
     - Página ``/msgchat`` en el navegador, sondeada vía JSON
   * - Alerta de mensaje recibido (sonido/visual/GPIO)
     - ⚠️ (clientes de escritorio: sonido/popup)
     - ✅
     - Alerta por GPIO (LED/zumbador) en lugar de un popup de escritorio, adecuado para un dispositivo sin pantalla
   * - Mensajería masiva/difusión a un grupo
     - ⚠️ (algunos mediante boletines en su lugar)
     - ❌
     - Usar Boletines para difusión; la mensajería directa es solo 1 a 1

Meteorología
-------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacidad específica
     - Habitual en software APRS popular
     - Aquí
     - Notas sobre la implementación de este proyecto
   * - Generación de informe meteorológico APRS propio
     - ✅ (Xastir, aprx, muchos firmwares de TNC con kit WX)
     - ✅
     - Conjunto completo del cap. 12 + adiciones de APRS 1.2 (nieve, luminosidad, inundación)
   * - Marco de sondeo de sensores en vivo (drivers conectables)
     - ⚠️ (poco común como marco genérico; suele estar fijado a una sola placa WX)
     - ✅
     - Registro dinámico y autorregistrable ``sensors_local``; incluye driver BMP180, extensible
   * - Promediado por campo durante el intervalo de informe
     - ⚠️
     - ✅
     - Casilla opcional "Promediado" por campo
   * - Recepción/registro de informes WX de otras estaciones
     - ✅ (superposiciones de mapa de Xastir, aprs.fi)
     - ⚠️
     - Decodificado/enrutado/digipeado como cualquier paquete, pero no hay una vista de historial WX dedicada en la administración web

Telemetría
-----------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacidad específica
     - Habitual en software APRS popular
     - Aquí
     - Notas sobre la implementación de este proyecto
   * - Generación de telemetría propia (``T#nnn``)
     - ✅ (algunos TNC/clientes)
     - ✅
     - 5 canales analógicos + 8 digitales
   * - Mensajes de metadatos PARM/UNIT/EQNS/BITS
     - ⚠️ (a menudo configurados manualmente)
     - ✅
     - Generación activable individualmente
   * - Calibración cuadrática (EQNS) por canal analógico
     - ⚠️
     - ✅
     - ``valor = a*x^2 + b*x + c`` por canal
   * - Recepción/graficado de telemetría de otros
     - ✅ (gráficos de Xastir, aprs.fi)
     - ❌
     - No implementado — sin vista de historial/gráficos de telemetría recibida

Objetos, Items, Boletines, Estado
------------------------------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacidad específica
     - Habitual en software APRS popular
     - Aquí
     - Notas sobre la implementación de este proyecto
   * - Objetos propios (con marca de tiempo)
     - ✅
     - ✅
     - Hasta 5, RF y/o INET, con intervalo/decaimiento
   * - Items propios (sin marca de tiempo)
     - ✅
     - ✅
     - Mismo pool de 5 ranuras; bandera "permanente" al estilo YAAC elige Objeto vs. Item
   * - "Matar" un objeto/item
     - ✅
     - ✅
     - Transmite la eliminación varias veces extra y luego se autodesactiva
   * - Boletines (``BLN1``-``BLNn``)
     - ✅
     - ✅
     - 5 ranuras, texto/intervalo/caducidad propios, ``BLN1``-``BLN5``
   * - Informes de estado (texto libre de la propia estación)
     - ✅
     - ✅
     - Baliza de estado en texto libre por rol (DTI ``>``, APRS101 cap.16) para
       tracker, IGate y digi, cada una con su propio intervalo
       (``*_sts_interval``) y texto (``*_status``); ver ``main/beacon.c``
   * - Respuesta a consultas (``?APRS?``, ``?WX?``, etc.)
     - ⚠️
     - ✅
     - Consultas broadcast (``?APRS?``/``?WX?``/``?IGATE?``) y dirigidas
       (``CALL:?query?``), con limitación de tasa; ver el componente ``query``
       y la página *Query* del panel web

Mapeo / Visualización
------------------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacidad específica
     - Habitual en software APRS popular
     - Aquí
     - Notas sobre la implementación de este proyecto
   * - Mapa en vivo de estaciones recibidas
     - ✅ (Xastir, APRSIS32, YAAC, aprs.fi — central en la mayoría de clientes)
     - ❌
     - No implementado. La administración web tiene una tabla Last-Heard, no un mapa
   * - Renderizado de símbolos/iconos según la tabla de símbolos APRS
     - ✅
     - ⚠️
     - Existe un selector de símbolo para configurar balizas/objetos propios; Last-Heard y el Traffic Log muestran iconos de símbolo para los cuatro formatos de posición sin comprimir (``!``/``=`` y, con marca de tiempo, ``/``/``@``; ver ``aprs_extract_symbol()`` en ``main/aprs_coord.c``). El formato de posición comprimido Base-91 y los reportes de Objeto/Ítem (``;``/``)``) aún no se analizan para su símbolo, por lo que en esos casos el icono sigue en blanco
   * - Reproducción de historial de trazas
     - ✅ (clientes de escritorio)
     - ❌
     - No implementado
   * - Graficado de meteorología/telemetría a lo largo del tiempo
     - ✅ (aprs.fi, plugins de Xastir)
     - ❌
     - No implementado

Gestión de estación / Operación
-----------------------------------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacidad específica
     - Habitual en software APRS popular
     - Aquí
     - Notas sobre la implementación de este proyecto
   * - UI de configuración basada en web
     - ⚠️ (VP-Digi y algunos proyectos ESP32 la tienen; la mayoría de clientes de escritorio usan GUI nativas)
     - ✅
     - ~30 páginas, autenticación HTTP Basic, reaplicación en vivo de la mayoría de ajustes sin reiniciar
   * - Panel en vivo (estado, contadores)
     - ⚠️
     - ✅
     - Indicadores de estado de red, panel de estadísticas, log de tráfico en vivo, tabla last-heard (long-poll JSON)
   * - Registro de tráfico/paquetes con vista de trama cruda
     - ✅
     - ✅
     - Etiquetado por dirección (RX/TX/DIGI/INET2RF/RX-IS), incluye nivel de audio RMS
   * - UI multilenguaje
     - ⚠️ (poco común; la mayoría son solo en inglés o localizados por el SO)
     - ✅
     - EN/ES/IT, solo en tiempo de compilación — sin cambio en ejecución
   * - Actualización de firmware OTA/remota
     - ⚠️ (poco común en TNC embebidos; común en IoT de consumo)
     - ✅
     - Doble partición (``ota_0``/``ota_1``) con reversión automática ante una imagen defectuosa
   * - Almacenamiento de configuración local persistente y versionado
     - ✅
     - ✅
     - LittleFS, escrituras atómicas (``.tmp`` + renombrado), tolerante a claves desconocidas/faltantes
   * - Gestión de archivos (subir/descargar/explorar)
     - ❌ (no aplicable a la mayoría del software APRS; relevante aquí por tratarse de un FS embebido)
     - ✅
     - Explorador LittleFS completo (listar/descargar/borrar/subir/formatear)
   * - Gestión Wi-Fi AP/STA con escaneo, potencia TX
     - N/A (el software de escritorio no lo necesita)
     - ✅
     - AP/STA/AP+STA, 5 perfiles STA, escaneo en vivo, control de potencia TX
   * - Sincronización NTP/hora
     - ⚠️ (el SO de escritorio se encarga; relevante en entornos embebidos)
     - ✅
     - 3 hosts NTP configurables, fijado a UTC para marcas de tiempo zulu correctas
   * - Ajuste de rendimiento/CPU
     - N/A para software de escritorio
     - ✅
     - Selección en tiempo de ejecución de 80/160/240 MHz
   * - Acceso remoto/por consola serie para diagnóstico
     - ✅ (la mayoría de TNC tienen consola serie)
     - ⚠️
     - Sin consola serie para operación ordinaria (por diseño); el diagnóstico vive en el panel web y en LOOP TEST
   * - Control de acceso multiusuario / basado en roles
     - ⚠️ (poco común)
     - ❌
     - Un único usuario/contraseña HTTP Basic, sin roles
