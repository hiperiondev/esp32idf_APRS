.. _es-limitations:

===============================
Estado y limitaciones conocidas
===============================

El firmware es **trabajo en progreso**. La ruta de transmisión RF, el IGate, el
digipeater, las balizas, la meteorología, la telemetría, la mensajería y la
administración web son todos funcionales.

Esta página compara el proyecto con *otros programas APRS*. Para la vista
complementaria — cuánto de la *especificación APRS en sí* pone la estación al
aire, capítulo por capítulo — vea :ref:`es-aprs-coverage`.

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
     - Ruta TX/RX completa por software, sobre ADC/DAC. ``ax25_decode()`` lee
       solo los bytes de la trama que recibe: el campo de direcciones se recorre
       de a una dirección contra el largo de la trama, así que una cabecera cuyos
       bits de extensión reclaman más repetidoras de las que la trama transporta
       se rechaza en vez de decodificar lo que la siga en memoria
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
       (``csma_persist``, 1-255), tiempo de silencio previo al acceso
       (``tx_timeslot``) y preámbulo/TXDelay, más un piso anti-inanición de ocho
       ranuras para que un canal que nunca se libera no retenga indefinidamente
       una trama en cola. El panel informa cuántas veces actuó ese piso,
       separando canal ocupado de canal libre, como *CSMA FORZADO
       (OCUP./PERSIST.)*
   * - Techo de ciclo de trabajo de transmisión a largo plazo
     - ⚠️ (poco común fuera de equipos comerciales/regulados)
     - ✅
     - Techo opcional (``duty_cycle_en``, desactivado por defecto) de
       ``duty_cycle_pct`` por ciento (1-100, por defecto 25) medido sobre una
       ventana deslizante de 10 minutos, acumulado a partir del tiempo al aire
       estimado de cada trama realmente transmitida a la velocidad configurada.
       Solo se retiene el tráfico no crítico: los mensajes y las repeticiones
       del digipeater siempre salen. Una baliza retenida se difiere, no se
       pierde - la tarea periódica que la genera vuelve a ofrecerla en su
       siguiente intervalo -, aunque se contabiliza como ``DROP_TX_DUTY_CYCLE``
       para que sea visible. El panel muestra el porcentaje medido frente al
       configurado como *CICLO DE TRABAJO TX*, y se rellena incluso con el
       limitador apagado para poder valorar el techo antes de activarlo
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
     - Se decide por estación pasada, según QCON: ``qAR`` solo cuando esta
       IGate puede pasar mensajes a RF **y** esa estación no se ha visto en
       APRS-IS dentro de ``igate_local_window_sec``; ``qAO`` en caso contrario
       (IGates de solo recepción, una IGate bidireccional con el reenvío
       INET→RF desactivado, y cualquier estación conectada a Internet)
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
       pero con un intervalo de reintento fijo de 1 s (también 1 s mientras el
       equipo no tiene ruta a internet), no un backoff exponencial. Cada
       intento fallido pasa al siguiente servidor configurado en vez de repetir
       el mismo
   * - Login a APRS-IS basado en passcode
     - ✅
     - ✅
     - Línea de login estándar ``user/pass/vers/filter``; se muestra la respuesta verified/unverified del servidor
   * - Múltiples servidores APRS-IS / failover
     - ⚠️ (algunos soportan listas de servidores)
     - ✅
     - Cuatro ranuras de servidor (``APRS_SERVER_NUM``), cada una con su propia
       casilla Habilitar, host y puerto. Un fallo de DNS, de conexión o de login
       pasa a la siguiente ranura habilitada y da la vuelta circularmente,
       reintentando cada segundo hasta que una acepte; las ranuras
       deshabilitadas se saltan, incluso en el primer intento tras el arranque.
       Todas las ranuras comparten la misma identidad de login
       (indicativo/SSID/passcode/filtro). El panel indica la ranura en uso
   * - Estadísticas por motivo de descarte
     - ⚠️ (poco común, normalmente solo totales)
     - ✅
     - Contadores nombrados (``DROP_TOO_SHORT``, ``DROP_PATH_TOKEN``, ``DROP_RANGE_FILTER``, etc.)
   * - Lista de indicativos de puerta satelital/ISS
     - ⚠️ (aprx y algunas puertas satelitales dedicadas)
     - ✅
     - Hasta 8 indicativos de digipeater satelital; una trama repetida por uno
       de ellos sin la marca de repetido (``*``) se descarta antes de llegar a
       APRS-IS

   * - Criterios de filtrado de mensajes (localidad de destinatario/remitente)
     - ✅ (exigido a un IGate conforme)
     - ✅
     - Las cuatro condiciones se aplican antes de que un mensaje leído de
       APRS-IS llegue a RF: destinatario escuchado localmente dentro de la
       ventana, remitente no escuchado por RF, sin ``TCPXX``/``NOGATE``/
       ``RFONLY`` en la cabecera del remitente, destinatario no conectado a
       Internet. Cada fallo tiene su propio motivo de descarte
   * - Ventana de escucha local configurable
     - ⚠️ (a menudo fija)
     - ✅
     - ``igate_local_window_sec``, 60-3600 s, una hora por omisión
   * - Posición asociada tras un mensaje retransmitido
     - ⚠️ (poco común)
     - ✅
     - Anillo de ocho destinatarios; el siguiente reporte de posición o de boya
       que se vea de uno de ellos se retransmite una vez, en reemplazo de la
       práctica obsoleta de repetir posiciones históricas

Digipeater
-----------

.. list-table::
   :header-rows: 1
   :widths: 30 24 10 36

   * - Capacidad específica
     - Habitual en software APRS popular
     - Aquí
     - Notas sobre la implementación de este proyecto
   * - Digipeating WIDEn-N, Nuevo Paradigma n-N (con traza)
     - ✅ (universal)
     - ✅
     - Decremento del contador de saltos **e** inserción del propio indicativo
       marcado como usado, así cada salto de una ruta repetida es identificable
   * - Tabla de alias configurable
     - ⚠️ (varía; a menudo una lista fija)
     - ✅
     - Cuatro filas de {alias, N máximo, modo} en la página Digi; ``#`` en un
       alias equivale a un dígito, así una fila cubre toda una familia
       (``WIDE#``). Tabla de fábrica: ``WIDE1`` 1 salto, ``WIDE2`` 2 saltos,
       ``WIDE#`` 2 saltos, todas con traza
   * - Atrapado de N grande
     - ✅ (se espera de todo digipeater moderno)
     - ✅
     - ``N máximo`` por alias; un contador de saltos mayor se limita al tope
       (por omisión) o se descarta (``DROP_DIGI_N_TRAPPED``), a elección del
       operador
   * - Rol de digipeater de relleno (solo ``WIDE1-1``)
     - ✅
     - ✅
     - Una sola casilla; restringe la estación a las filas de alias de un salto
   * - Ruteo regional ``SSn-N``
     - ⚠️ (convención regional)
     - ✅
     - Una fila de alias más, típicamente en modo Inundación con el límite de
       saltos propio de la región
   * - Inundación ``WIDEn-N`` no rastreable (NOID)
     - ⚠️ (conducta heredada)
     - ❌
     - No se produce para ``WIDEn-N``: el paradigma lo movió al mecanismo de
       trazado. El modo Inundación existe, pero solo para una fila de alias que
       el operador decida usar sin traza
   * - Alias heredados ``TRACEn-N`` / ``RELAY`` / ``ECHO`` / ``GATE``
     - ⚠️ (obsoletos)
     - ❌
     - Abandonados como rutas y no incorporados. Un operador que aún necesite
       alguno para un vecino heredado lo agrega como una fila de alias más
   * - Contador de saltos codificado en el SSID de destino (heredado)
     - ⚠️ (TNC más antiguos)
     - ✅
     - Apagado por omisión (*Digipetir por SSID de destino*). Rutea antes que la
       tabla de alias y por ese solo SSID, así que una ruta explícita nunca se
       leería; apagado, el SSID de destino queda intacto y decide la tabla de
       alias
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
   * - Digipeteo preventivo
     - ⚠️ (poco común, TNC avanzados)
     - ✅
     - Apagado por omisión; dos modos indicadores (conservar las direcciones
       salteadas marcadas como usadas, o descartarlas), con escaneo desde la
       primera dirección sin usar hasta el final de la ruta y sin reclamar
       nunca un alias ``n-N`` genérico
   * - Digipeteo viscoso (esperar y repetir solo si nadie más lo hizo)
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
       extensión PHG/DFS en uso, porque el formato comprimido no tiene espacio
       para ninguna de las dos; un alcance de radio precalculado, en cambio, se
       pliega en la ranura de dos bytes propia del campo comprimido
   * - Codificación de posición Mic-E (TX)
     - ⚠️ (sobre todo firmware de tracker móvil)
     - ✅
     - La página del beacon Tracker ofrece una opción Mic-E
       (``aprs_mice_encode()``); solo posición fija, por lo que el
       curso/velocidad se envía siempre como "desconocido". El comentario de
       posición se elige en la misma página, entre los siete valores estándar y
       los siete personalizados; Emergency no se ofrece, porque transmitirlo
       pide una respuesta del mundo real. El campo de información sigue el orden
       canónico de ``mic-e-examples.txt``: byte TYPE, altitud, bloque de
       frecuencia, extensión de datos, comentario, ``!DAO!`` y el par
       Fabricante/Versión que identifica al firmware (la dirección de destino
       lleva datos de posición, así que el TOCALL ``APxxxx`` no puede)
   * - PHG / potencia-altura-ganancia-directividad
     - ✅
     - ✅
     - Expuesto en la página de baliza del IGate, con sus propios subcampos, y
       como un único interruptor en la página Tracker que reutiliza los datos
       de antena de la estación. En el formato Mic-E el token viaja en el campo
       de texto, que es donde APRS 1.2 ubica un campo de comentario de posición
       normal
   * - RNG / alcance de radio precalculado
     - ⚠️
     - ✅
     - Seleccionable como extensión de datos de la baliza del IGate
       (``RNGrrrr``), o como la forma de alcance de dos bytes propia del campo
       comprimido cuando además se pide compresión
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
   * - Extensión de precisión/datum ``!DAO!``
     - ⚠️ (algunos clientes/trackers)
     - ✅
     - Opción a nivel de estación en la página Estación; añade la forma
       legible WGS-84 a los formatos sin comprimir y Mic-E, solo cuando la
       ambigüedad es 0 y el formato no es el comprimido
   * - Localizador Maidenhead en los reportes de estado
     - ⚠️
     - ✅
     - Opción a nivel de estación; emite la forma ``>IO91SX/G`` de APRS101
       cap.16
   * - Rumbo de antena y PRE en los reportes de estado
     - ⚠️ (operación de meteor scatter)
     - ✅
     - Rumbo y potencia a nivel de estación en la página Estación, emitidos
       como el par ``^HP`` que cierra el texto de estado; hacen falta las dos
       mitades y el par nunca se descarta para entrar en el presupuesto de
       longitud
   * - Localizador Maidenhead en el destino AX.25 (``[IO91SX]``, obsoleto)
     - ⚠️ (software antiguo)
     - ❌
     - La propia especificación lo marca como obsoleto; no se produce
   * - Altitud en balizas
     - ✅
     - ✅
     - Altitud por rol (tracker, IGate, digipeater), cada una copiada del valor
       de "Mi Estación" cuando se marca *Usar datos de Mi Estación*. Se envía
       como el token ``/A=`` del comentario, o gratis dentro de la ranura de
       dos bytes propia del campo comprimido cuando la baliza está comprimida y
       esa ranura no lleva ya un alcance de radio. Los reportes meteorológicos
       no llevan campo de altitud
   * - Ruta de digipeteo configurable por servicio
     - ✅
     - ✅
     - Cuatro presets de ruta compartidos; cada servicio que transmite (tracker,
       IGate, digipeater, meteorología, telemetría, mensajes, objetos,
       boletines) elige entre ellos con su propia máscara de bits

   * - Identificador de tipo de datos con capacidad de mensajería (``=`` / ``@``)
     - ✅ (universal)
     - ✅
     - Se elige según *Habilitar mensajería*: ``!``/``/`` con la mensajería
       apagada, ``=``/``@`` con ella encendida, así los clientes receptores
       ofrecen una vía de respuesta. Al formato Mic-E no le sobra identificador
       y declara lo mismo con su byte TYPE (`` ` `` / ``'``), leído de la misma
       marca

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
   * - Reply-ACK (APRS 1.1, ``{MM}AA``)
     - ⚠️ (APRSdos, APRS+SA, Xastir, APRSIS32)
     - ✅
     - En ambos sentidos. Los números salientes son ``{MM}`` o ``{MM}AA``, con
       la confirmación gratuita agregada en el instante de la transmisión, así
       que un reintento lleva la última adeudada; un ``AA`` entrante cierra el
       mensaje saliente que nombra, y el ``MM`` del remitente pasa a ser lo que
       se le adeuda. La numeración se limita a dos dígitos para que el
       identificador completo entre en los cinco caracteres que permite APRS101
   * - Reintento de mensajes con cantidad/intervalo configurables
     - ✅
     - ✅
     - ``msg_retry`` / ``msg_interval``, evaluado a 1 Hz
   * - UI de chat/bandeja de entrada integrada
     - ✅ (Xastir, YAAC, APRSIS32)
     - ✅
     - Página ``/msgchat`` en el navegador, sondeada vía JSON; un solo hilo de mensajes enviados y recibidos, 5 visibles, se guardan los últimos 10
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
       metadatos. El rango crudo por canal acota el valor que sale al aire, así
       que una sonda que lee más allá de su propia escala informa el extremo del
       rango declarado en vez de una cifra que ningún receptor puede graficar;
       un rango invertido o vacío no declara nada y se ignora
   * - Mapeo de sensores en vivo por canal de telemetría
     - ⚠️ (habitualmente fijo en código, o alimentado por un script externo)
     - ✅
     - Cada canal analógico A1-A5 y digital B1-B8 elige su fuente del registro
       ``sensors_local``, guardada por nombre de driver, así que habilitar o
       deshabilitar un driver nunca reapunta un canal a otro sensor en silencio
   * - Telemetría en comentario base-91 APRS 1.2 (``|ss..|``)
     - ⚠️ (un puñado de clientes/trackers)
     - ✅
     - Opcional, junto al reporte ``T#nnn``; viaja en el comentario de posición
       de la baliza (Tracker/IGate/Digipeater) que esté transmitiendo con el
       indicativo/SSID configurado en la página Telemetry, compartiendo el
       contador de secuencia de ese reporte. Lleva los canales analógicos y,
       detrás de un conjunto completo de cinco, el banco digital de ocho bits
       como un par más
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
     - Mismo pool de 5 ranuras; un control de Tipo elige Objeto vs. Item
   * - Objetos permanentes (``111111z``)
     - ✅
     - ✅
     - Casilla exclusiva de Objeto; emite la marca de tiempo ficticia fija
       ``111111z`` de freqspec.txt en vez de la marca en vivo
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
       (``*_sts_interval``) y texto (``*_status``); ver ``main/beacon.c``. El
       campo de información se mantiene dentro del tope de 70 bytes del cap.16
       (``>`` + marca de tiempo de 7 bytes + 62 caracteres de texto): cuando los
       bloques opcionales no entran, se descarta primero el localizador
       Maidenhead y después el bloque de frecuencia, y ni el texto del operador
       ni el par rumbo/PRE del final se recortan nunca
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
       pasada del planificador, y ``?APRSM`` reenvía como mucho
       ``MSG_QUERY_BURST_MAX`` (3) mensajes retenidos por consulta, dejando el
       resto a la planificación de reintentos de mensajería
   * - Gráfico de historial de escucha de ``?APRSH``
     - ⚠️
     - ✅
     - La estación guarda un histograma de escucha de 18 horas por indicativo
       (ver ``components/lastheard``), así que la respuesta es el gráfico
       ``Hrd: h0 h1 ... h17`` que define APRS101 cap.15, seis conteos por
       período separados por ``.``, siendo la hora 0 la hora reloj actual. El
       histograma pertenece a la fila de la estación y viaja con ella cuando la
       fila pasa al frente de la tabla, y sólo una trama recibida cuenta en él
       —responder la consulta adelanta el gráfico hasta la hora actual pero deja
       intactos los conteos guardados, así que se puede preguntar por una
       estación tantas veces como se quiera
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
       ``?APRSH``. Los indicativos se guardan en mayúsculas y se comparan sin
       distinguir mayúsculas de minúsculas, así las dos fuentes que alimentan la
       tabla —direcciones AX.25 crudas del aire y texto TNC2 crudo de APRS-IS—
       no pueden darle dos filas a una misma estación
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
   * - Limitación de intentos de acceso / bloqueo tras fallos repetidos
     - ⚠️ (poco común en paneles web embebidos)
     - ✅
     - Backoff por IP de origen, empezando en 5 s y doblando en cada fallo
       adicional mientras dura el bloqueo (tope de 300 s) tras 5 credenciales
       rechazadas; ``429 Too Many Requests`` con ``Retry-After`` en lugar de
       ``401`` mientras dura el bloqueo. Solo en RAM, así que se borra al
       reiniciar
