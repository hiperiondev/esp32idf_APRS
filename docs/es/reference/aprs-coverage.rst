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
     - ✅
     - Se leen las cuatro formas: las dos formas zulú de 7 bytes, la forma heredada de hora local y la marca mes/día/hora/minuto de un reporte meteorológico sin posición. Las formas zulú se resuelven a UTC absoluto contra el reloj, retrocediendo un día, un mes o un año cuando el valor caería en el futuro, y la forma local se reporta tal como la escribió el emisor, porque el paquete no nombra la zona horaria en la que está. El valor se muestra en la columna DECODIFICADO de la tabla de tráfico, que es lo que distingue un paquete retransmitido con minutos de latencia de uno recién escuchado.
   * - Ambigüedad de posición
     - ✅
     - De uno a cuatro dígitos en blanco al transmitir, a nivel de estación. Al recibir, los minutos en blanco se analizan dígito a dígito y se resuelven al centro de la caja de ambigüedad, así que una posición gruesa igual se filtra por distancia de forma razonable en vez de colapsar hacia el borde del grado.
   * - Altitud
     - ✅
     - La forma ``/A=`` de seis dígitos en los comentarios, por rol de estación, más la forma base-91 dentro de Mic-E.
   * - ``!DAO!`` de alta precisión y opción de datum
     - ✅
     - Se transmite en posiciones sin comprimir y dentro del campo de texto Mic-E, y se suprime cuando hay ambigüedad de posición o se eligió el formato comprimido, porque ambos ya declaran otra precisión. Un token recibido se aplica en sentido inverso, en sus dos formas de aire — los dígitos legibles y la forma base-91 que emite la mayoría de los trackers — así que una posición sin comprimir que llega se refina hasta unos 18 m antes de que el filtro de distancia la mida. Un reporte comprimido se deja como está, porque sus campos base-91 ya llevan esa precisión.
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
   * - Extensiones de datos en recepción
     - ✅
     - La ranura de 7 bytes de un reporte sin comprimir que llega se analiza en vez de leerse como los primeros siete caracteres del comentario: ``PHGphgd`` y su forma PHGR de nueve bytes, ``RNGrrrr``, ``DFSshgd`` y ``CSE/SPD``, que se reporta como dirección y velocidad del viento cuando el símbolo es una estación meteorológica. El comentario se toma después desde el primer byte posterior al token encontrado, así que la forma de nueve bytes ya no deja un carácter de tasa y una barra sueltos al frente.
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
     - Seleccionable por servicio — tracker, IGate, digipetidor y objetos — y se decodifica en recepción. La compresión se suprime automáticamente cuando hay una extensión PHG o DF o ambigüedad de posición, porque ninguna de ellas sobrevive al formato comprimido; el alcance de radio precalculado es la única extensión que sí lo hace, y se pliega en el campo de dos bytes.
   * - Rumbo/velocidad comprimidos y el byte de tipo de compresión
     - ✅
     - Un tracker en movimiento codifica rumbo y velocidad en el campo de dos bytes y pone el byte de tipo en rumbo/velocidad comprimidos con posicionamiento actual; una estación sin nada que reportar manda la codificación de tres espacios de "sin datos". El campo cuantiza el rumbo en pasos de 4 grados y la velocidad en pasos de alrededor del 8 por ciento, y el codificador mantiene ambos bytes dentro del rango que pertenece a la forma de rumbo/velocidad, así que un token nunca se lee como la forma de alcance de radio que comparte esos dos bytes. En recepción los tres bytes se leen del mismo modo: el byte de tipo decide entre altitud, alcance de radio y rumbo/velocidad, así que una estación en movimiento muestra su rumbo y su velocidad en vez de una coordenada pelada.
   * - Alcance de radio precalculado comprimido
     - ✅
     - Una baliza cuya extensión de datos es el alcance de radio precalculado lo pliega en el campo de dos bytes — el marcador reservado ``{`` seguido del dígito de alcance — en vez de recaer en el formato sin comprimir, así que el círculo de cobertura viaja con una posición comprimida y no queda ningún token ``RNGrrrr`` en el campo de información. El campo cuantiza el alcance en pasos de alrededor del 8 por ciento, desde un piso de 2 millas.
   * - Altitud comprimida
     - ✅
     - Una baliza comprimida que lleva altitud la pone en esos mismos dos bytes, con el byte de tipo declarando GGA como fuente, que es lo que selecciona esa lectura. Cuando lo hace, el token ``/A=`` del comentario se omite, así que la altitud se enuncia una sola vez y no cuesta nada en vez de nueve bytes. Si además hay un alcance de radio configurado, el alcance se queda con los dos bytes: no tiene otro lugar donde ir, y la altitud todavía cuenta con la forma del comentario. El paso es de alrededor del 0,2 %.

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
     - Una emergencia Mic-E recibida por radio o desde APRS-IS genera una línea de log de nivel warning y su propia entrada en el registro de tráfico, junto al paquete que la transportó. Los otros catorce comentarios de posición se registran a nivel informativo, porque el valor vive en la dirección de destino y de otro modo es invisible en el texto del paquete. Las formas entre corchetes del campo de comentario que usa una estación sin Mic-E para la misma señal (``aprs.org/aprs12/EmergencyCode.txt``) se reconocen del mismo modo: ``!EMERGENCY!`` al comienzo del comentario de una posición, objeto o ítem - después de la extensión de datos PHG/DFS/RNG/CSE-SPD cuando está presente, que es donde la propuesta la coloca - genera el mismo warning y la misma línea en el registro de tráfico, y las otras trece formas entre corchetes (``!TESTALARM!``, ``!PRIORITY!``, ``!WXALARM!`` y el resto de ese conjunto propuesto) se registran a nivel informativo como sus equivalentes Mic-E.
   * - Velocidades por encima de 670 nudos
     - ✅
     - La extensión de 1.2 se aplica en ambos sentidos, así que una trama digipeteada por una estación espacial reporta su velocidad orbital en vez de una recortada. Esa escala está cuantizada en pasos de 112 nudos y tiene un hueco entre 671 y 781 nudos que la propia regla publicada deja sin representación; por debajo de 671 nudos el campo sigue siendo exacto al nudo.
   * - PHG dentro del campo de texto Mic-E
     - ✅
     - La extensión de datos se escribe dentro del texto Mic-E, detrás del bloque de frecuencia y delante del comentario del operador, así que una estación que baliza en Mic-E anuncia su cobertura como habilita la incorporación de 1.2. El interruptor está en la página Tracker; sus cuatro subcampos son los datos de antena de la propia estación.
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
   * - Contador de lluvia crudo
     - ✅
     - La cuenta corrida del propio pluviómetro, cuatro dígitos detrás de un ``#``, transmitida sin escalar y sin que la estación la reinicie nunca, de modo que un receptor pueda restar dos reportes. Es una fila más de la tabla de mapeo de sensores, y se emite solo cuando hay un driver mapeado a ella.
   * - Campos de radiación y voltaje
     - ❌
     - Los dos campos propuestos para 1.2 no tienen token meteorológico propio y viajan como canales analógicos de telemetría, que es adonde los rutea el marco de sensores.
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
     - ✅
     - El contador de secuencia, los canales analógicos y el banco digital de ocho bits se codifican en el grupo delimitado por barras verticales. Como ese grupo es posicional —el n-ésimo par *es* el canal n, sin identificador por par—, el codificador se detiene en el primer canal deshabilitado o no resuelto en vez de dejar un hueco que correría un lugar a todos los canales siguientes, y el par digital solo se añade detrás de un conjunto completo de cinco pares analógicos, que es el único sitio donde la especificación lo permite. Una estación sin ningún canal que reportar no emite grupo alguno, ya que la extensión debe llevar el contador y al menos un canal.
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
     - ✅
     - Los mensajes dirigidos a los nombres de grupo integrados ``ALL``, ``QST`` y ``CQ``, o a cualquiera de los nombres de grupo definidos por el operador, se emparejan sin distinguir mayúsculas junto con el indicativo propio de la estación, con cualquier SSID. Un mensaje de grupo se mantiene distinto de un mensaje directo con el mismo texto, y se muestra pero nunca se acusa, ya que un grupo no tiene un único dueño que responda por él.
   * - OBJECT-in-MSG e ITEM-in-MSG
     - ❌
     - Las dos propuestas 1.2 que llevan un reporte completo de objeto o ítem dentro del cuerpo de un mensaje, pensadas para una estación que no puede digirepetir el paquete de objeto/ítem ordinario, no se reconocen como una clase propia. Esta estación no tiene mapa donde graficar uno y no origina ni necesita ese recurso; un mensaje con cualquiera de las dos formas igual llega al operador como texto plano.
   * - Codificación de texto UTF-8
     - ✅
     - Los campos de mensaje y demás texto libre son transparentes a 8 bits de punta a punta: nada aquí recodifica ni rechaza un byte no ASCII, que es la recomendación de la propia especificación (``aprs.org/aprs12/utf-8.txt``). Todo recorte por longitud de un campo de texto libre - la ruta de mensajes salientes, el texto de estado y comentario, y el texto de objetos/ítems y boletines, ya sea ingresado en las páginas de administración web o cargado desde la configuración almacenada - cae en un límite de carácter en vez de partir uno por la mitad.
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
     - ✅
     - Los dos caracteres cierran el texto de estado detrás de un ``^``, a partir de un rumbo y una potencia de alcance global de la estación que se fijan en la página Station. El rumbo avanza de a diez grados y la potencia se ajusta a la entrada más cercana de la tabla de la especificación, que va de 10 a 7290 vatios. Hacen falta las dos mitades para que el bloque aparezca, y es el único bloque que el presupuesto de longitud nunca descarta: una estación que trabaja meteor scatter manda el reporte justamente por esos tres bytes.

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
     - ✅
     - La cadena ``!x!`` que pide a las bases de datos detrás de APRS-IS no almacenar un paquete puede anteponerse a cada comentario o texto de estado propio con una única casilla a nivel de estación (página Station), desactivada por defecto. Se dirige a los archivos, no a las pasarelas, así que nunca decide por dónde puede viajar una trama ni si llega a RF/APRS-IS; los paquetes retransmitidos no se ven afectados por esta opción y conservan el marcador que ya traía la estación de origen, si lo traía, porque la carga se pasa byte a byte.
   * - Informe de ruta IGate→RF
     - ❌
     - El envoltorio experimental ``{IP-`` que permitiría a esta estación anunciar por APRS-IS la ruta AX.25 usada para pasar un paquete a RF no se genera. A diferencia de la mayoría de las otras propuestas 1.2 de esta tabla, la estación está justo dentro del alcance de esta: es una IGate bidireccional y sí pasa tráfico a RF. Sigue siendo una propuesta, no una adición ratificada, ha tenido poca adopción entre las IGates en general, y añadiría una segunda transmisión por cada paquete pasado a RF, a costa de un tiempo de canal que este diseño deliberadamente ligero no gasta en ningún otro sitio. A reconsiderar si la propuesta llega a ratificarse o si algún operador la pide.

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
   * - Tono, desplazamiento y alcance
     - ✅
     - El tono CTCSS de tres dígitos, el desplazamiento en unidades de diez kilohercios y el alcance de cobertura de dos dígitos en millas o kilómetros, en el orden que define la especificación de frecuencia. El alcance es un subcampo de Objetos/Ítems (la cobertura propia que anuncia un repetidor fijo), no un ajuste por servicio del tracker, IGate o digirepetidor.
   * - Tono en banda angosta, código DCS y frecuencia TX/RX separada
     - ❌
     - Otros tres subcampos opcionales que define la especificación no se construyen: el indicador de modulación en banda angosta (minúscula), el código DCS que puede sustituir al tono CTCSS, y la forma de frecuencia de transmisión/recepción separada. Ninguno de los tres es un defecto en el bloque que transmite esta estación, que sigue siendo válido y auto-sintonizable sin ellos - son carencias de capacidad, dejadas de lado porque el firmware no tiene un ajuste de radio en banda angosta/DCS que informar y objitem_t modela una única frecuencia de monitoreo en vez de TX/RX independientes.
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

Ninguno de los cinco identificadores de arriba lleva una carga que este
firmware decodifique, pero los cinco se clasifican para el enrutado bajo un
único bit de filtro compartido, la casilla "Otros" de la página IGate Filter,
así que un paquete de cualquiera de esas clases puede reenviarse a APRS-IS en
vez de descartarse haga lo que haga el operador. El tráfico de terceros y los
datos de prueba quedan sin clasificar a propósito y nunca se retransmiten:
re-enrutar tráfico de terceros es como empiezan los lazos de IGate, y los datos
de prueba no están pensados para salir del canal en el que se mandaron.

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
   * - Identidad de ingreso
     - ✅
     - La estación ingresa con su indicativo-SSID, la misma identidad que llevan como origen sus propios paquetes y la misma que sigue al constructo q en las tramas pasarela, escrita en un solo lugar para que las tres no puedan divergir. Es la dirección a la que hay que mandar un mensaje desde APRS-IS, ya que el servidor compara el destinatario con el ingreso de forma exacta.
   * - Ruta del tráfico originado localmente
     - ✅
     - Todo lo que esta estación pone por sí misma en APRS-IS lleva ``TCPIP*`` como ruta completa y ningún alias de digipetidor. Cada originador arma un paquete por pata, así la transmisión de radio conserva la ruta de digipetidores del operador mientras que la de internet declara que el paquete fue inyectado y no repetido.
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
