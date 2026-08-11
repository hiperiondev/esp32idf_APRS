.. _es-aprs-coverage:

============================
Cobertura del protocolo APRS
============================

Esta página compara el firmware con el protocolo APRS en sí, capítulo por
capítulo, en lugar de compararlo con otros programas APRS. Donde
:ref:`es-limitations` pregunta *"¿cómo se compara este proyecto con Direwolf
o Xastir?"*, esta página pregunta *"¿cuánto de la especificación sale al
aire?"*.

La referencia usada en toda la página es el APRS Protocol Reference 1.2 — la
consolidación del APRS101 1.0.1 original (2000), el apéndice APRS 1.1
aprobado en 2004 y las incorporaciones de la 1.2 publicadas desde entonces —
junto con los archivos de especificación sueltos de ``aprs.org`` para las
funciones que solo existen ahí.

Leyenda:

* ✅ — Implementado y funcionando
* ⚠️ — Implementación parcial o limitada
* ❌ — No implementado

Un ❌ no es automáticamente un defecto. Varias filas describen formatos que la
propia especificación marca como obsoletos o "no recomendados", y algunas
describen convenciones que necesitan hardware de radio que este proyecto no
controla. La columna Notas aclara cuál es cuál.

En todas las tablas, *transmitir* significa que la estación puede originar el
formato, y *recibir* significa que el formato se decodifica lo suficiente como
para alimentar la caché de duplicados, el filtro de distancia, Last Heard y el
registro de tráfico — no simplemente retransmitirlo. Un paquete que el
firmware no sabe decodificar igual se digipetea y se pasa a APRS-IS, porque
ambos caminos trabajan sobre el campo de direcciones AX.25.

AX.25 y acceso al canal (cap. 3)
================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Tramas UI de AX.25, control 0x03 / PID 0xF0
     - ✅
     - Módem por software completo, en transmisión y recepción, sobre el propio ADC y DAC del ESP32. Solo tramas UI sin conexión, que es lo único que usa APRS.
   * - Campo de direcciones: destino, origen y 0-8 digipetidores
     - ✅
     - Se decodifica y se vuelve a codificar respetando el bit de retransmitido. El decodificador recorre el campo de direcciones contra el largo de la trama, así que una cabecera que promete más repetidoras de las que la trama trae se rechaza en vez de leerse de más. Los constructores de ruta aplican el límite de 8 direcciones.
   * - Acceso al canal CSMA p-persistente
     - ✅
     - Detección de portadora más un valor de persistencia y un TXDelay configurables, con un piso anti-inanición para que un canal ocupado no bloquee una trama para siempre. Las transmisiones forzadas se cuentan por separado según sea canal ocupado o sorteo de persistencia fallido.
   * - Corrección de errores FX.25
     - ✅
     - Tres modos: apagado, solo recepción, y recepción y transmisión. Las 11 etiquetas de correlación. Los bloques transmitidos siguen siendo legibles para un receptor AX.25 común.
   * - Interfaz de host KISS / AGWPE
     - ❌
     - La estación no puede actuar como TNC para software cliente externo. Es deliberado: el firmware es una estación completa, no un periférico módem.

Campos de dirección de destino y origen (cap. 4)
================================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Identificador de versión de software en el destino (TOCALL)
     - ⚠️
     - Todo paquete originado usa ``APZ32L``. Es un TOCALL experimental válido, pero no está registrado en la lista ``aprsorg/aprs-deviceid``, así que los clientes receptores muestran la estación como una aplicación experimental genérica en vez de nombrar el firmware.
   * - Datos Mic-E codificados en la dirección de destino
     - ✅
     - Los dígitos de latitud, norte/sur, este/oeste y el desplazamiento de longitud se codifican al transmitir y se reensamblan al recibir junto con la mitad que viaja en el campo de información.
   * - Ruta genérica de digipetidor en el SSID de destino
     - ✅
     - El digipetidor la reconoce como forma de ruteo heredada, detrás de un interruptor explícito apagado por omisión para que nunca cortocircuite una ruta explícita.
   * - Símbolo en la dirección de destino (GPSxyz / SYMxyz)
     - ✅
     - Se lee en recepción cuando el campo de información no aporta un símbolo propio, en todas las formas ``GPSxy``, ``SPCxy``, ``SYMxy``, ``GPSCnn`` y ``GPSEnn``, incluido el carácter de superposición sobre los símbolos de la tabla alternativa. Los paquetes Mic-E quedan excluidos, porque su dirección de destino lleva datos de posición. El tráfico originado mantiene el símbolo en el campo de información, así que nunca se escribe en esta forma.
   * - Símbolo desde el SSID de la dirección de origen (obsoleto)
     - ⚠️
     - Se aplica en recepción solo como último paso de la cadena de precedencia de símbolos, y solo a los paquetes NMEA crudos, que es el caso para el que se inventó la convención. En cualquier otro tipo de dato el SSID sigue significando el rol de la estación, que es lo que significa hoy.
   * - Redes alternativas
     - ❌
     - El tráfico originado siempre usa el TOCALL del proyecto; no hay ajuste para una dirección de destino de red alternativa.

Formatos de hora y posición (cap. 6)
====================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Latitud y longitud sin comprimir
     - ✅
     - Se transmiten y se analizan en la forma ``DDMM.mmN`` / ``DDDMM.mmW``, con un formateador compartido para que todo servicio que origina tráfico produzca coordenadas idénticas.
   * - Marca de tiempo zulú día/hora/minuto
     - ✅
     - Es la única forma que esta estación origina, en posiciones, objetos, reportes de estado y meteorología. El reloj corre siempre en UTC, así que no interviene ninguna conversión de zona horaria.
   * - Marcas de tiempo local día/hora/minuto y hora/minuto/segundo en recepción
     - ⚠️
     - Las tres formas de marca de tiempo de 7 bytes se saltean correctamente al ubicar el campo de posición, así que ningún paquete se analiza mal. El valor de la marca en sí no se decodifica ni se muestra: se usa la hora de recepción.
   * - Ambigüedad de posición
     - ✅
     - De uno a cuatro dígitos en blanco al transmitir, a nivel de estación. Al recibir, los minutos en blanco se analizan dígito a dígito y se resuelven al centro de la caja de ambigüedad, así que una posición gruesa igual se filtra por distancia de forma razonable en vez de colapsar hacia el borde del grado.
   * - Altitud
     - ✅
     - La forma ``/A=`` de seis dígitos en los comentarios, por rol de estación, más la forma base-91 dentro de Mic-E.
   * - ``!DAO!`` de alta precisión y opción de datum
     - ✅
     - Se transmite en posiciones sin comprimir y dentro del campo de texto Mic-E, y se suprime cuando hay ambigüedad de posición o se eligió el formato comprimido, porque ambos ya declaran otra precisión. Los paquetes recibidos no se refinan con su ``!DAO!``, lo que cuesta unos 18 m de resolución en el filtro de distancia y nada más.
   * - Reportes de posición NMEA crudos (``$``)
     - ✅
     - Las sentencias ``RMC``, ``GGA`` y ``GLL`` se decodifican en recepción, con cualquier identificador de emisor de dos letras, de modo que quedan cubiertos tanto los receptores multiconstelación como los de solo GPS. La suma de verificación opcional se exige cuando está presente, y una sentencia que declara una fijación inválida se rechaza, así el filtro de distancia del IGate nunca evalúa una estación sobre una coordenada vieja. ``$GPWPL`` nombra un punto de ruta y no la fijación propia del emisor, y se deja deliberadamente sin decodificar; ``$ULTW`` es un registro meteorológico y se rutea como tal.
   * - Posición nula por omisión
     - ❌
     - Siempre se transmiten las coordenadas configuradas tal como están; no hay convención para señalar "posición desconocida" cuando el operador no las cargó.

Extensiones de datos (cap. 7)
=============================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Rumbo y velocidad
     - ✅
     - En la ranura de 7 bytes sin comprimir, en la forma comprimida de dos bytes y en Mic-E.
   * - Dirección y velocidad del viento
     - ✅
     - Ocupa la misma ranura de 7 bytes en los reportes meteorológicos con posición, con marcadores de ausencia cuando no hay sensor asignado.
   * - Potencia / altura / ganancia / directividad (PHG)
     - ✅
     - Se arma a partir de vatios, pies, dBi y una dirección en la página Station, y se espeja a las balizas por rol y a los objetos.
   * - Sondas PHGR (carácter de tasa de baliza)
     - ✅
     - La forma de nueve bytes de 1.2 ("PHGphgd" más un carácter de tasa de balizas por hora y su barra final obligatoria) se transmite siempre que se conoce el intervalo propio de la baliza IGate, lo cual siempre ocurre, y se analiza en recepción: el carácter de tasa y la barra se reconocen y se descartan, de modo que el comentario que sigue se lee correctamente en lugar de empezar con una barra suelta.
   * - Alcance de radio precalculado (RNG)
     - ✅
     - Seleccionable como extensión de datos para cualquier rol de baliza, en millas terrestres.
   * - Intensidad de señal omni-DF (DFS)
     - ✅
     - Seleccionable como extensión de datos, con la intensidad en puntos S más los mismos códigos de altura, ganancia y directividad que usa PHG.
   * - Marcación y número/alcance/calidad (BRG/NRQ)
     - ✅
     - Disponible en objetos e ítems, en la forma ``000/000`` que exige la especificación.
   * - Descriptor de objeto de área
     - ✅
     - Codificación completa de forma, color y tamaño, incluida la regla que reemplaza la barra por un dígito para valores de color de diez en adelante.

Formatos de reporte de posición y DF (cap. 8)
=============================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Los cuatro identificadores de tipo de dato de posición
     - ✅
     - En transmisión y recepción. La elección entre los identificadores con y sin capacidad de mensajería sigue el ajuste real de mensajería de la estación, no si casualmente hay una marca de tiempo.
   * - Campo de comentario
     - ✅
     - Va en toda posición originada, con el bloque de frecuencia, ``!DAO!`` y la telemetría en comentario reservando sus bytes antes de que el texto libre llene el campo, así un comentario largo se recorta en vez de tirar una extensión.
   * - Formato de reporte DF
     - ⚠️
     - Los campos de marcación y NRQ se producen en objetos e ítems, que es lo que cubre reportar una marcación sobre otra estación. No hay un rol de reporte DF propio para la baliza de la estación.

Reportes de posición comprimidos (cap. 9)
=========================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Latitud y longitud comprimidas en base-91
     - ✅
     - Seleccionable por servicio — tracker, IGate, digipetidor y objetos — y se decodifica en recepción. La compresión se suprime automáticamente cuando hay una extensión de datos o ambigüedad de posición, porque ninguna de las dos sobrevive al formato comprimido.
   * - Rumbo/velocidad comprimidos y el byte de tipo de compresión
     - ✅
     - Un tracker en movimiento codifica rumbo y velocidad en el campo de dos bytes y pone el byte de tipo en rumbo/velocidad comprimidos con posicionamiento actual; una estación sin nada que reportar manda la codificación de tres espacios de "sin datos". El campo cuantiza el rumbo en pasos de 4 grados y la velocidad en pasos de alrededor del 8 por ciento, y el codificador mantiene ambos bytes dentro del rango que pertenece a la forma de rumbo/velocidad, así que un token nunca se lee como la forma de alcance de radio que comparte esos dos bytes.
   * - Alcance de radio y altitud comprimidos
     - ❌
     - El campo de dos bytes solo lleva rumbo y velocidad. Una estación que necesite anunciar alcance o altitud junto con la posición usa el formato sin comprimir, donde ambos tienen campo propio.

Formato de datos Mic-E (cap. 10)
================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Mic-E en transmisión y recepción
     - ✅
     - Codificador y decodificador completos, cubriendo la mitad de la dirección de destino, la longitud, la velocidad y el rumbo, y ambos identificadores de tipo de dato, el actual y el antiguo.
   * - Código de tipo e identificador de fabricante
     - ✅
     - El byte de tipo va después del byte de tabla de símbolos y refleja la capacidad de mensajería de la estación; el par de fabricante y versión cierra el campo de texto. Sin ellos una baliza Mic-E es anónima para todo cliente, porque la dirección de destino lleva la posición y no puede además identificar el firmware.
   * - Altitud, bloque de frecuencia y ``!DAO!`` en el campo de texto Mic-E
     - ✅
     - La altitud encabeza el campo de texto y desplaza el comentario en vez de reemplazarlo, y el bloque de frecuencia y la extensión de datum se emiten en el orden canónico que muestran los propios ejemplos de la especificación.
   * - Códigos de comentario de posición (Off Duty, En Route … Emergency)
     - ✅
     - El receptor decodifica los quince valores, incluido el conjunto personalizado y el patrón Emergency de todos ceros, y reporta correctamente como indefinido un patrón mixto estándar/personalizado. La página Tracker permite elegir cualquiera de los catorce valores estándar y personalizados para transmitir. Emergency queda deliberadamente fuera de esa lista: pide a otros operadores que respondan a una emergencia real, algo que una página de configuración no debería poder activar con un clic equivocado y dejar activado para todas las balizas siguientes.
   * - Indicación de emergencia
     - ✅
     - Una emergencia Mic-E recibida por radio o desde APRS-IS genera una línea de log de nivel warning y su propia entrada en el registro de tráfico, junto al paquete que la transportó. Los otros catorce comentarios de posición se registran a nivel informativo, porque el valor vive en la dirección de destino y de otro modo es invisible en el texto del paquete.
   * - Velocidades por encima de 670 nudos
     - ✅
     - La extensión de 1.2 se aplica en ambos sentidos, así que una trama digipeteada por una estación espacial reporta su velocidad orbital en vez de una recortada. Esa escala está cuantizada en pasos de 112 nudos y tiene un hueco entre 671 y 781 nudos que la propia regla publicada deja sin representación; por debajo de 671 nudos el campo sigue siendo exacto al nudo.
   * - PHG dentro del campo de texto Mic-E
     - ❌
     - No se produce la incorporación de 1.2 que permite un campo de comentario de posición normal —notablemente PHG— dentro del texto Mic-E. Importa sobre todo para digipetidores por hardware que balizan en Mic-E.
   * - Telemetría Mic-E
     - ❌
     - Ausente a propósito: la versión 1.2 deprecia este formato en favor de los códigos de tipo de fabricante y de la telemetría base-91 en comentario, que este firmware sí implementa.

Reportes de objeto e ítem (cap. 11)
===================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Reportes de objeto
     - ✅
     - Cinco ranuras configurables, nombres rellenados a nueve caracteres, estados vivo y anulado, posición comprimida o sin comprimir, con todo el conjunto en transmisión y recepción.
   * - Reportes de ítem
     - ✅
     - Nombres variables de tres a nueve caracteres con los marcadores de vivo y anulado. Vale notar que la especificación marca el formato de ítem como no recomendado en RF; los objetos son la mejor opción para algo de larga vida.
   * - Anular un objeto o ítem
     - ✅
     - Un objeto anulado se retransmite unas cuantas veces como reporte de anulación antes de que se limpie su bandera de habilitado, así los receptores efectivamente ven el retiro en vez de que el objeto simplemente envejezca en sus listas.
   * - Objetos permanentes
     - ✅
     - Se emite la pseudo-marca de tiempo fija de todos unos para un objeto marcado como permanente, que es lo que necesita un objeto fijo de repetidora o punto de referencia.
   * - Objetos de área
     - ✅
     - Forma, color, ancho de línea y tamaño, en las mismas ranuras de objeto.
   * - Objetos e ítems tipo cartel
     - ✅
     - Hasta tres caracteres de texto de cartel entre llaves, sobre el símbolo de cartel.
   * - Ruteo proporcional para objetos
     - ✅
     - Un preajuste de ruta por transmisión, rotando entre ellos, para que un objeto de larga vida no inunde todos los saltos en cada ciclo. A cada preajuste se le cuentan los saltos y se lo saca de la rotación si supera el límite de AX.25.

Reportes meteorológicos (cap. 12)
=================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Reporte meteorológico completo, con y sin marca de tiempo
     - ✅
     - La forma recomendada, con el símbolo meteorológico, la extensión de datos de viento y el bloque completo de datos meteorológicos. El identificador de tipo con capacidad de mensajería sigue el ajuste de mensajería de la estación, igual que en las posiciones comunes.
   * - Reporte meteorológico como objeto
     - ✅
     - Un objeto meteorológico con nombre en una posición distinta de la de la estación.
   * - Reporte meteorológico sin posición
     - ⚠️
     - Se emite cuando la estación no tiene coordenadas configuradas, con la marca de tiempo mes/día/hora/minuto que el formato exige. La nieve se omite en esta forma a propósito, porque la letra que usaría ya es la velocidad del viento ahí; el firmware avisa por log cuando eso descarta una lectura configurada. La especificación marca este formato como no recomendado.
   * - Campos meteorológicos obligatorios
     - ✅
     - La dirección y velocidad del viento, la ráfaga y la temperatura siempre se emiten, cayendo a los marcadores de puntos cuando no hay sensor asignado, así el reporte sigue siendo una trama meteorológica válida y no una truncada.
   * - Lluvia, humedad y presión barométrica
     - ✅
     - Lluvia de la última hora, de las últimas 24 horas y desde la medianoche; humedad con la codificación de dos ceros para 100 %; presión en décimas de milibar en el campo completo de seis caracteres que confirma la revisión 1.2.
   * - Luminosidad y nevada
     - ✅
     - La luminosidad usa la letra mayúscula por debajo de 1000 W/m² y la minúscula por encima; la nevada usa la forma fraccionaria por debajo de diez pulgadas y la entera rellenada con ceros por encima.
   * - Medidor de agua / altura de inundación
     - ✅
     - Las dos formas, en pies y en metros, de la propuesta de medidor de agua de 1.2, con resolución de décimas y sin relleno, como muestra el ejemplo de ese mismo documento.
   * - Identificadores de tipo de software y de unidad meteorológica
     - ⚠️
     - Ambos se emiten, como último token del campo de información para que un analizador estricto no absorba el comentario del operador dentro de la cadena de unidad. El código de unidad es libre y está bien; el carácter único de tipo de software es uno que la especificación no asigna.
   * - Contador de lluvia crudo, radiación y voltaje
     - ❌
     - El contador de lluvia crudo de la especificación original y los campos de radiación y voltaje propuestos para 1.2 no tienen codificador. El marco de sensores mapea los drivers a campos por nombre, así que agregarlos es extender la tabla de campos y no armar plomería nueva.
   * - Reportes meteorológicos crudos Peet Bros y Ultimeter
     - ⚠️
     - El clasificador de pasarela los reconoce como meteorología, así que se rutean bien, pero las cargas crudas nunca se decodifican a lecturas. De todos modos la especificación dice que quien transmite debería convertir al formato completo, así que esto es amplitud del lado de recepción y no un hueco de transmisión.
   * - Datos de tormenta
     - ❌
     - El modelo de datos para reportes de ciclón tropical existe en las cabeceras, pero nada codifica ni decodifica la forma al aire. Una estación de este tipo no tiene de dónde sacar esa información —viene de servicios meteorológicos—, así que retransmitir esos paquetes intactos, que es lo que ocurre hoy, es la conducta sensata.

Datos de telemetría (cap. 13)
=============================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Reporte de telemetría
     - ✅
     - Número de secuencia, cinco canales analógicos y el banco digital de ocho bits, con los canales mapeados por nombre a los drivers de sensores locales para que habilitar o deshabilitar un driver no los reapunte en silencio.
   * - Rango analógico extendido 000-999
     - ✅
     - El campo de tres dígitos acepta el rango completo de 1.2 en vez de la ventana original 000-255, y las lecturas fuera de rango se acotan en vez de envolverse. Antes se aplica un mínimo y un máximo crudos por canal, así el operador puede mantener los valores dentro de la ventana que entienda su software receptor.
   * - Definiciones de parámetros al aire
     - ✅
     - Los cuatro mensajes de definición —nombres, unidades y etiquetas, coeficientes de ecuación, y sentido de bits con título de proyecto— se mandan como mensajes dirigidos, como exige la especificación. El reporte lleva el valor crudo y el receptor aplica los coeficientes.
   * - Telemetría base-91 en comentario
     - ⚠️
     - Los canales analógicos y el contador de secuencia se codifican en el grupo delimitado por barras verticales. Como ese grupo es posicional —el n-ésimo par *es* el canal n, sin identificador por par—, el codificador se detiene en el primer canal deshabilitado o no resuelto en vez de dejar un hueco que correría un lugar a todos los canales siguientes. El banco digital de ocho bits no se incluye en el grupo; viaja solo en el reporte de telemetría común.
   * - Forma alternativa del número de secuencia
     - ❌
     - El identificador de secuencia de tres letras que algunos codificadores usan en lugar de un número no se produce ni se reconoce de forma especial en recepción.

Mensajes, boletines y anuncios (cap. 14)
========================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Mensajes de texto con acuse y rechazo
     - ✅
     - Campo de destinatario de nueve caracteres, coincidencia de acuse y rechazo restringida a los de uno a cinco caracteres alfanuméricos que permite la especificación, y un temporizador de reintento para los mensajes salientes sin acusar.
   * - Reply-ACK
     - ✅
     - Las siete reglas del algoritmo, incluido armar el sufijo en el instante de la transmisión y llevar una pequeña tabla por estación de acuses adeudados. Es lo que hace que un intercambio de mensajes fluya a velocidad de conversación en vez de dos paquetes por turno.
   * - Formato del número de mensaje
     - ⚠️
     - Se emparejan los números entrantes de cualquier largo legal. Los mensajes salientes se numeran con dos dígitos, envolviendo en 99, para que un sufijo Reply-ACK siga entrando en los cinco caracteres que la especificación permite para el identificador completo.
   * - Grupos de mensajes
     - ❌
     - Los mensajes dirigidos a los nombres de grupo generales, o a un grupo definido por el operador, no se leen: solo se empareja el indicativo propio de la estación, con cualquier SSID. Por eso el tráfico de red dirigido a un grupo es invisible en el panel de mensajes. Vale notar que los mensajes de grupo nunca deben acusarse, solo mostrarse.
   * - Boletines generales, anuncios y boletines de grupo
     - ✅
     - Cinco ranuras configurables con las formas de destinatario correctas para los tres: identificador numérico para boletines, identificador con letra para anuncios, y sufijo de nombre de grupo para boletines de grupo. Cada ranura tiene su propio vencimiento.
   * - Cadencia de transmisión de boletines
     - ⚠️
     - Los boletines salen a intervalo fijo con un tiempo de vencimiento. La especificación recomienda en cambio una agenda decreciente —frecuente al principio y espaciándose a lo largo de horas—, que carga menos un canal compartido para el mismo efecto.
   * - Boletines del servicio meteorológico nacional
     - ❌
     - Los boletines dirigidos a los prefijos del servicio meteorológico se manejan como mensajes comunes en vez de reconocerse como una clase propia. Se retransmiten correctamente y nunca se acusan, porque el destinatario no es esta estación, así que el efecto práctico se limita a cómo se los rotula en la interfaz.
   * - Radiogramas NTS
     - ❌
     - La convención de prefijos de línea no se analiza. La especificación dice explícitamente que una aplicación no necesita entenderla, porque las líneas son mensajes comunes y se leen correctamente como texto plano, que es lo que ocurre acá.

Capacidades de estación, consultas y respuestas (cap. 15)
=========================================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Consultas generales
     - ✅
     - La consulta a todas las estaciones, incluida la huella opcional de latitud, longitud y radio, más las consultas de difusión de meteorología, IGate y QRU. Una consulta que llega de internet se responde por internet y una que llega por RF se responde por RF, así que una consulta de internet nunca puede keyear el transmisor.
   * - Consultas dirigidas a una estación
     - ✅
     - El conjunto completo: posición, estado, estaciones oídas en directo, historial de una estación oída, mensajes pendientes, objetos y traza de ruta, incluido el alias ping para la consulta de traza. Las respuestas las arma el planificador de balizas y no la tarea de recepción, así que una ráfaga de consultas no puede desbordar una pila de recepción.
   * - Historial de estaciones oídas
     - ✅
     - El histograma de ocho horas que pide la especificación, llevado por estación y arrastrado con ella cuando su fila se mueve al frente de la tabla.
   * - Paquete de capacidades de estación
     - ⚠️
     - Se manda en respuesta a la consulta de IGate con el token de pasarela y los contadores de mensajes y de estaciones locales, donde los contadores significan lo que dice la especificación y no totales crudos de tramas. El modelo de capacidades es abierto, así que la estación también podría anunciar sus roles de digipetidor, meteorología y telemetría; no lo hace.

Reportes de estado (cap. 16)
============================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Reporte de estado con y sin marca de tiempo
     - ✅
     - Texto de estado por rol con marca de tiempo zulú opcional.
   * - Presupuesto de largo del texto de estado
     - ✅
     - El límite de 62 caracteres se aplica descartando los bloques opcionales en un orden definido —primero el localizador, después el bloque de frecuencia— y nunca se toca el texto propio del operador. Si aun así no entra, el reporte se rechaza en vez de recortarse a una trama mal formada.
   * - Reporte de estado con localizador Maidenhead
     - ⚠️
     - Se producen el localizador de cuatro o seis caracteres y su símbolo, pero no se cumplen dos reglas de ubicación: la especificación exige que el localizador siga inmediatamente al identificador de tipo de dato, y prohíbe combinarlo con una marca de tiempo. Acá la marca de tiempo y el bloque de frecuencia pueden precederlo, así que un receptor estricto no reconocerá la forma con localizador.
   * - Rumbo de antena y potencia radiada efectiva
     - ❌
     - No se produce la codificación de dos caracteres para meteor scatter al final del texto de estado. Es de uso acotado, y agregarla requiere una tabla de búsqueda y dos ajustes.

Tunelizado de red y tráfico de terceros (cap. 17)
=================================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Tramas de terceros
     - ✅
     - Se arman al retransmitir tráfico de internet hacia RF y se desenvuelven en el sentido de entrada, usando el indicativo de la estación interna para la supresión de duplicados y Last Heard, no el de la pasarela. Una trama que no entra se rechaza en vez de recortarse.
   * - Tokens de ruta que prohíben la pasarela
     - ✅
     - Se respetan todos los tokens clásicos de no-pasarela y los q-constructs de solo internet, y solo en la cabecera, que es donde tienen sentido: un paquete cuyo texto de comentario contenga por casualidad una de esas palabras no se confunde con uno ruteado con ella.
   * - Marcador de no archivar
     - ❌
     - La convención que pide a las bases de datos de APRS-IS no archivar un paquete no se genera para las balizas propias ni recibe tratamiento especial al retransmitir. Los paquetes retransmitidos la conservan porque la carga se pasa byte a byte.

Especificación de frecuencia (cap. 18)
======================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Bloque de frecuencia en posiciones, objetos y estado
     - ✅
     - Se producen las tres formas fijas de diez bytes: la de diez kilohercios por debajo de 100 MHz, la forma común en VHF y UHF, y la de prefijo con letra para las bandas de microondas; se trabaja en kilohercios enteros para que ningún dígito derive, y se rechaza emitir cualquier cosa que no mida exactamente diez bytes.
   * - Tono y desplazamiento
     - ✅
     - El tono CTCSS de tres dígitos y el desplazamiento en unidades de diez kilohercios, en el orden que define la especificación de frecuencia.
   * - Pedidos de frecuencia y QSY dentro de mensajes
     - ❌
     - No se generan ni se atienden las formas de mensaje propuestas que piden u ordenan un cambio de frecuencia de operación. Son propuestas de 1.2 con despliegue escaso en el aire.

Formatos definidos por el usuario y otros tipos de paquete (cap. 19-20)
=======================================================================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Formato de datos definido por el usuario
     - ❌
     - El espacio de extensión privada que la especificación reserva para experimentadores está sin usar. Sería el lugar correcto para cualquier diagnóstico propio del firmware que el proyecto quisiera poner al aire, en vez de sobrecargar el texto de estado.
   * - Paquetes de datos inválidos y de prueba
     - ❌
     - El identificador de datos de prueba no se reconoce como clase propia, así que esos paquetes caen al camino general. Nunca deberían pasarse a internet.
   * - Formato de radiogoniometría Agrelo
     - ❌
     - No se decodifica el formato de marcación y calidad del radiogoniómetro autónomo. Quedan muy pocas unidades en servicio.
   * - Baliza de localizador Maidenhead
     - ❌
     - No se decodifica el identificador de baliza de localizador independiente. La propia especificación lo marca como obsoleto; el localizador ahora vive en los reportes de estado.
   * - Identificadores reservados (elemento de mapa, datos de refugio, clima espacial)
     - ❌
     - Están reservados por la especificación y nunca se definieron más, así que no hay nada que implementar.

Símbolos (cap. 21)
==================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Tablas de símbolos primaria y alternativa
     - ✅
     - Un selector visual en la administración web cubre ambas tablas, con un símbolo por rol para el tracker, el IGate, el digipetidor, la estación meteorológica y cada objeto.
   * - Caracteres de superposición
     - ✅
     - Se puede poner un carácter de superposición en la posición de la tabla para los símbolos que lo aceptan, que es como un digipetidor anuncia su propia política de ruteo en el mapa.
   * - Precedencia de símbolos
     - ⚠️
     - Solo se lee el símbolo del campo de información, así que la cuestión de la precedencia no se plantea en la práctica; pero también significa que las fuentes de respaldo que describe la regla nunca se consultan para un paquete que no traiga símbolo ahí.
   * - Convenciones de SSID
     - ✅
     - Los SSID de rol recomendados son los valores de fábrica de cada servicio, y todo campo de SSID se valida por rango tanto en el formulario como en el cargador de configuración.

Digipeteo y el paradigma New-N
==============================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Digipeteo New-N trazado
     - ✅
     - Una tabla de alias definida por el operador, con forma comodín, un máximo de saltos por fila y un modo de traza o relleno por fila. Los alias heredados que el paradigma New-N reemplazó no se honran, a propósito.
   * - Rol de digipetidor de relleno (hogareño)
     - ✅
     - Un solo interruptor restringe la estación a las filas de un salto, que es la configuración correcta para un digipetidor hogareño que sirve a estaciones que no alcanzan al de área amplia.
   * - Trampa de conteo de saltos
     - ✅
     - Un conteo de saltos por encima del máximo de la fila que coincidió se acota y se repite, o se descarta, a elección del operador, con el descarte contado bajo su propio motivo en el panel.
   * - Supresión de duplicados
     - ✅
     - Una caché compartida con profundidad y ventana de tiempo configurables, usada tanto por el digipetidor como por la pasarela, así una trama no puede repetirse por un camino después de que el otro ya la vio.
   * - Digipeteo preventivo
     - ✅
     - Apagado por omisión y seleccionable en dos modos indicadores: las direcciones salteadas se conservan marcadas como usadas, o se descartan para que salga solo lo que queda por hacer. El escaneo va desde la primera dirección sin usar hasta el final de la ruta y solo reclama una identidad fija, así que las dos exclusiones que enuncia la propuesta -los alias n-N genéricos y un alias escrito con contador de saltos- quedan garantizadas por construcción.
   * - Ruteo heredado por SSID de destino
     - ✅
     - Disponible detrás de un interruptor apagado por omisión. Cuando está apagado, un paquete que use esa convención cae a la lógica de ruta explícita en vez de descartarse.
   * - Señalización de precedencia y de operador presente con los bits RR
     - ❌
     - No se implementan en ninguno de los dos sentidos las propuestas que reutilizan los bits reservados del octeto de SSID. El modo de marcado del digipeteo preventivo enciende el bit reservado bajo en las direcciones que saltea, pero la trama se recodifica desde su representación TNC2, que no lleva esos bits al canal.

Pasarela APRS-IS
================

.. list-table::
   :header-rows: 1
   :widths: 32 8 60

   * - Función APRS
     - Estado
     - Notas
   * - Ingreso e identificación de software
     - ✅
     - La línea de ingreso lleva el indicativo, el passcode, el nombre y la versión del software, y el filtro del lado del servidor solo cuando hay uno configurado: nunca se manda la palabra clave de filtro pelada sin argumento.
   * - Pasarela de RF a internet
     - ✅
     - Casillas de pasarela por tipo, un filtro de distancia alrededor de la estación, filtros de prefijo de indicativo y lista negra, y una lista de pasarela satelital para estaciones oídas a través de digipetidores espaciales.
   * - q-construct por estación
     - ✅
     - La pasarela elige entre los dos constructos de recepción por estación de origen y no con una bandera global, así una estación a la que esta pasarela no le entregaría un mensaje queda marcada como tal, que es justo lo que significa el constructo.
   * - Pasarela de internet a RF
     - ✅
     - Una máscara configurable decide qué categorías pueden cruzar, y los mensajes además tienen que cumplir las tres condiciones que establece la documentación de pasarelas, seguidas por el paquete de posición de la estación destinataria.
   * - Validación del filtro del lado del servidor
     - ✅
     - La cadena de filtro se chequea gramaticalmente antes de enviarse —forma del término y tipo de argumento—, mientras que el rango y la sensatez de los valores quedan para el servidor, que es la autoridad.
   * - Conmutación entre servidores
     - ⚠️
     - Cuatro ranuras de servidor con reconexión automática. La rotación ocurre ante un fallo al establecer la sesión; un servidor que acepta la conexión y después la corta se reintenta en vez de saltarse, así un servidor en mantenimiento puede retener a la estación hasta que se recupere.

Resumen
=======

Contando las filas anteriores: la estación implementa por completo el núcleo
de posición, objetos, mensajes, consultas y estado del protocolo, en los dos
sentidos, más los dos roles de red (digipetidor e IGate) y las incorporaciones
posteriores al año 2000 que más pesan en el tráfico diario — posiciones
comprimidas, ``!DAO!``, Mic-E con identificación de equipo, el campo de
frecuencia, Reply-ACK, telemetría base-91 en comentario y el paradigma New-N.

Los huecos se agrupan en tres lugares, y conviene decirlo sin rodeos:

* **Amplitud del lado de recepción.** La estación transmite más formatos de
  los que decodifica. Los registros meteorológicos crudos de Peet Bros y
  Ultimeter se reconocen como *categoría* — lo suficiente para pasarlos por
  los filtros de pasarela — pero su contenido nunca se analiza. En un IGate
  esto se ve como estaciones que pasan el filtro de tipo pero cuyas medidas
  no quedan disponibles localmente.
* **Propuestas posteriores a 2004.** Falta la propuesta de señalización con
  los bits RR, aunque las sondas PHGR sí son compatibles. Son adiciones reales a la especificación, no
  folclore, pero su despliegue en el aire es desparejo. El digipeteo preventivo, el más
  consecuente del grupo, está implementado y apagado por omisión.
* **Formatos sin origen local.** Los datos de tormenta, los boletines del NWS
  y los radiogramas NTS son, para una estación de este tipo, asuntos de puro
  transporte: el firmware no tiene de dónde sacar esa información. Se listan
  por completitud, y la conducta sensata — retransmitirlos intactos — es lo
  que ya ocurre.

Ninguna de las filas ❌ impide que la estación funcione como un IGate,
digipetidor, tracker, estación meteorológica o de telemetría plenamente
conforme.
