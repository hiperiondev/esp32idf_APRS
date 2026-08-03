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
     - Perfil de módem seleccionable n.º 2; como Bell 202, ejecuta dos
       demoduladores en paralelo
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
     - Tres modos en la página Radiomódem: apagado, solo RX (decodifica FX.25 y
       transmite AX.25 plano) y RX+TX. Los bloques transmitidos siguen siendo
       compatibles hacia atrás — un receptor de AX.25 plano ignora la etiqueta
       de correlación y los bytes de paridad, y decodifica la trama que llevan
       dentro
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
     - Acceso p-persistente condicionado por DCD: persistencia configurable
       (``csma_persist``, 1-255), ranura de tiempo (``tx_timeslot``) y
       preámbulo/TXDelay, más un piso anti-inanición de ocho ranuras para que
       un canal que nunca se libera no retenga indefinidamente una trama en
       cola
   * - Activación de PTT (sin VOX, GPIO de hardware)
     - ✅
     - ✅
     - GPIO y polaridad en tiempo de compilación; tiempo mínimo de retención de desactivación ajustable en ejecución
   * - Herramienta integrada de bucle/autoprueba RF
     - ⚠️ (poco común)
     - ✅
     - "LOOP TEST" — transmite un paquete con token y verifica que toda la cadena RX lo decodifique de vuelta, con diagnóstico detallado por etapa
   * - Entrada de audio plana/discriminador frente a audio con deénfasis
     - ✅ (Direwolf, UZ7HO)
     - ✅
     - Indica al demodulador si recibe audio de altavoz o audio sin filtrar del
       discriminador; se aplica en vivo al guardar
   * - Control de profundidad de la cola de TX
     - ⚠️ (normalmente una cola interna fija)
     - ✅
     - ``rf_tx_buffers``: cuántas tramas pueden esperar en el anillo de TX de RF
       antes de descartar los paquetes nuevos en vez de encolarlos; se lee en
       cada transmisión, así que surte efecto sin reiniciar
   * - Tiempo mínimo de PTT liberado entre tramas
     - ⚠️ (TXTAIL en algunos TNC)
     - ✅
     - ``ptt_min_unkey_ms``, 0-5000 ms sobre la liberación fija de un tick que
       el módem siempre aplica — para radios o repetidores que necesitan un
       hueco garantizado más largo entre transmisiones

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
     - Caché compartida; profundidad y ventana configurables por web en la
       página IGate (4-40 entradas, por defecto 20; ventana 1-120 s, por
       defecto 30 s)
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
     - ⚠️
     - Reconexión TCP automática, relee la configuración en cada reconexión,
       pero con un intervalo de reintento fijo (5 s tras una conexión fallida,
       1 s mientras el equipo no tiene ruta a internet), no un backoff
       exponencial
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
   * - Lista de indicativos de puerta satelital/ISS
     - ⚠️ (aprx y algunas puertas satelitales dedicadas)
     - ✅
     - Hasta 8 indicativos de digipeater satelital; una trama realmente repetida
       por uno de ellos se envía con el constructo ``qAO`` en lugar de ``qAR``

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
     - Ventana propia de 30 s en la caché de duplicados compartida
       (``DUP_SCOPE_DIGI``), con clave de origen y payload solamente, probada
       antes de cualquier trabajo sobre la ruta
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
     - Opción por servicio en las páginas Tracker, IGate, Digipeater y
       Objetos/Ítems; el decodificador también la entiende. Se omite
       automáticamente cuando la ambigüedad de posición no es cero o hay una
       extensión de datos en uso, porque el formato comprimido no tiene espacio
       para ninguna de las dos
   * - Codificación de posición Mic-E (TX)
     - ⚠️ (sobre todo firmware de tracker móvil)
     - ✅
     - La página del beacon Tracker ofrece una opción Mic-E (``aprs_mice_encode()``); solo posición fija, por lo que el curso/velocidad se envía siempre como "desconocido" y el código de mensaje queda fijo en Off Duty
   * - PHG / potencia-altura-ganancia-directividad
     - ✅
     - ✅
     - Expuesto en la página de baliza del IGate
   * - RNG / alcance de radio precalculado
     - ⚠️
     - ✅
     - Seleccionable como extensión de datos de la baliza del IGate
       (``RNGrrrr``)
   * - DFS / intensidad de señal omni-DF
     - ⚠️ (software específico de DF)
     - ✅
     - Seleccionable como extensión de datos de la baliza del IGate
       (``DFSshgd``)
   * - Ambigüedad de posición en los reportes transmitidos
     - ⚠️
     - ✅
     - Nivel 0-4 a nivel de estación en la página Estación; se aplica a los
       formatos sin comprimir y Mic-E, y fuerza el formato sin comprimir cuando
       es distinto de cero
   * - Localizador Maidenhead en los reportes de estado
     - ⚠️
     - ✅
     - Opción a nivel de estación; emite la forma ``>IO91SX/G`` de APRS101
       cap.16
   * - Localizador Maidenhead en el destino AX.25 (``[IO91SX]``, obsoleto)
     - ⚠️ (software antiguo)
     - ❌
     - La propia especificación lo marca como obsoleto; no se produce
   * - Altitud en balizas
     - ✅
     - ✅
     - Altitud por rol (tracker, IGate, digipeater), cada una copiada del valor
       de "Mi Estación" cuando se marca *Usar datos de Mi Estación*. Los
       reportes meteorológicos no llevan campo de altitud
   * - Ruta de digipeteo configurable por servicio
     - ✅
     - ✅
     - Cuatro presets de ruta compartidos; cada servicio que transmite (tracker,
       IGate, digipeater, meteorología, telemetría, mensajes, objetos,
       boletines) elige entre ellos con su propia máscara de bits

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
     - Coeficientes a/b/c por canal transmitidos en el mensaje ``EQNS.``. El
       reporte de datos lleva la lectura cruda del sensor y es el receptor quien
       aplica la conversión — la división estándar de APRS101 entre reporte y
       metadatos. Los campos de rango crudo por canal se guardan y se muestran,
       pero no escalan el valor transmitido
   * - Mapeo de sensores en vivo por canal de telemetría
     - ⚠️ (habitualmente fijo en código, o alimentado por un script externo)
     - ✅
     - Cada canal analógico A1-A5 y digital B1-B8 elige su fuente del registro
       ``sensors_local``, guardada por nombre de driver, así que habilitar o
       deshabilitar un driver nunca reapunta un canal a otro sensor en silencio
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
     - Consultas generales (``?APRS?``/``?WX?``/``?IGATE?``) y dirigidas, cada
       una con su propio limitador de tasa. Se reciben en las tareas de RF/APRS-IS
       y se responden desde la tarea del planificador de balizas. Cada origen
       tiene su propio interruptor y sus respuestas vuelven por el canal por el
       que llegó la pregunta, con el origen APRS-IS apagado por defecto para que
       el tráfico de la red troncal no pueda activar el transmisor; véase
       :ref:`es-query`
   * - Conjunto de consultas dirigidas (``?APRSD``/``?APRSH``/``?APRSM``/
       ``?APRSO``/``?APRSP``/``?APRSS``/``?APRST``/``?PING?``)
     - ⚠️ (APRSISCE/32, YAAC)
     - ✅
     - Se responden cuando *Consultas dirigidas extendidas* está habilitado. Las
       respuestas tipo lista vuelven como mensajes APRS a la estación que
       consulta; ``?APRSO`` reanuncia los Objetos/Ítems más adelante en esa misma
       pasada del planificador
   * - Gráfico de historial de escucha de ``?APRSH``
     - ⚠️
     - ✅
     - La estación guarda un histograma de escucha de 18 horas por indicativo
       (ver ``components/lastheard``), así que la respuesta es el gráfico
       ``Hrd: h0 h1 ... h17`` que define APRS101 cap.15, seis conteos por
       período separados por ``.``, siendo la hora 0 la hora reloj actual. El
       histograma pertenece a la fila de la estación y viaja con ella cuando la
       fila pasa al frente de la tabla
   * - Capacidades de estación (DTI ``<``)
     - ✅
     - ✅
     - Se emite como respuesta a ``?IGATE?``
       (``<IGATE,MSG_CNT=n,LOC_CNT=n>``), donde ``MSG_CNT`` es la cuenta
       acumulada de paquetes de mensaje APRS enrutados en cualquiera de los dos
       sentidos y ``LOC_CNT`` la cantidad viva de estaciones que hay en la lista
       de escuchadas locales (por RF)

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
     - ✅
     - Existe un selector de símbolo para configurar balizas/objetos propios; Last-Heard y el Traffic Log muestran iconos de símbolo tanto para los reportes de posición sin comprimir (``!``/``=`` y, con marca de tiempo, ``/``/``@``) como para el formato comprimido Base-91, y también para los reportes de Objeto (``;``) e Ítem (``)``) con cualquiera de los dos formatos de posición (ver ``aprs_extract_symbol()`` en ``main/aprs_coord.c``)
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
     - 17 páginas de la barra lateral + selector de símbolo, autenticación HTTP Basic, reaplicación en vivo de la mayoría de ajustes sin reiniciar
   * - Panel en vivo (estado, contadores)
     - ⚠️
     - ✅
     - Indicadores de estado de red, panel de estadísticas, log de tráfico en vivo, tabla last-heard (long-poll JSON)
   * - Registro de tráfico/paquetes con vista de trama cruda
     - ✅
     - ✅
     - Etiquetado por dirección (RX/TX/DIGI/INET2RF/RX-IS), incluye nivel de audio RMS
   * - Tabla de últimas estaciones escuchadas
     - ✅
     - ✅
     - Una fila por estación en vez de por paquete, la más reciente primero y
       con desalojo LRU, más el histograma horario de 18 horas que responde
       ``?APRSH``
   * - Restauración a los valores de fábrica compilados
     - ⚠️
     - ✅
     - Un botón en la página Sistema reescribe ``config.json`` con los valores
       de fábrica
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
