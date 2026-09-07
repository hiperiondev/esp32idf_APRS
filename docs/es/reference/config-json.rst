.. _es-config-json:

===============================
Almacenamiento de configuración
===============================

La configuración residente persiste sobre LittleFS como **un archivo por
funcionalidad de la administración web**, cada uno con el nombre de la página
que lo posee. Esta referencia resume la mecánica de almacenamiento; para los
grupos de campos véase :ref:`es-configuration`.

Mecánica
========

* **Un archivo por funcionalidad**, bajo ``/storage``: ``system.json``,
  ``station.json``, ``wireless.json``, ``radio.json``, ``igate.json``,
  ``brandmeister.json``, ``digi.json``, ``tracker.json``, ``weather.json``,
  ``gps.json``, ``message.json``, ``winlink.json``, ``query.json``. No existe
  un archivo de configuración combinado.
* **Cargado** con cJSON, un archivo a la vez; **guardado** por un escritor en
  flujo token a token.
* **Guardado atómico:** escribe ``<nombre>.json.tmp``, luego renombra.
* **Cada página guarda solo su propio archivo.** La página *My Station* es la
  excepción que nombra varias, porque sus espejos de "Use My Station Data"
  escriben en campos de otros cinco servicios.
* Ausente, vacío o corrupto → ese archivo se reescribe desde los valores por
  defecto durante la carga, de modo que cada funcionalidad siempre tiene un
  archivo y el equipo siempre arranca con una administración web alcanzable.
* Sin memoria → **no** se escribe nada y la carga informa el fallo. Una lectura
  o un parseo que se quedó sin RAM no dice nada sobre el contenido del archivo,
  así que nunca debe tomar el camino anterior: el lector escanea el texto sin
  asignar nada para distinguir ambos casos, y solo los bytes genuinamente
  imparseables se sobrescriben.
* Los nombres de campo y las claves JSON se mantienen 1:1 con el proyecto de
  referencia, así que un operador que se mueva entre ambos los reconoce; las
  claves desconocidas se ignoran y una clave que un archivo no lleva conserva
  su valor por defecto documentado.

Otros archivos persistentes
===========================

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - Archivo
     - Contenido
   * - ``/storage/telemetry.json``
     - Configuración del canal 0 de telemetría (``telemetry_config_t``):
       analógicos A1–A5, digitales B1–B8, parámetros del informe, conmutadores
       de los mensajes de definición.
   * - ``/storage/bulletins.json``
     - Los cinco boletines APRS (identificador y grupo del destinatario, texto,
       RF/INET, intervalo inicial, rampa de decaimiento, caducidad).
   * - ``/storage/objitems.json``
     - Los cinco objetos/ítems APRS (nombre, posición, símbolo, rumbo/velocidad,
       comentario, intervalo, indicador permanente).
   * - ``/storage/telegram.json``
     - Toda la configuración del bot de Telegram: el interruptor de
       habilitación, el token del bot, el identificador del administrador, la
       dirección de la Mini App y las listas de usuarios y chats de grupo
       autorizados.
   * - ``/storage/winlink_mail.json``
     - Las respuestas que ha devuelto el servicio Winlink, de la más antigua a
       la más reciente. Los ajustes de la cuenta son las claves ``wl*`` de
       ``winlink.json``; aquí viven solo las respuestas, así que borrarlas nunca
       toca la configuración.

Todos los almacenes usan el mismo escritor en flujo, cada uno bajo su propio
mutex, cada uno con un ``setvbuf()`` explícito para evitar una asignación
perezosa de un búfer stdio grande a mitad de escritura. El búfer de
``setvbuf()`` es un único objeto estático compartido por todos, ya que el
cerrojo de escritura de todo el sistema de archivos impide que dos guardados se
solapen.

Cada uno de estos archivos se crea desde sus valores por defecto durante el
arranque si no existe, así que un primer arranque deja un conjunto completo en
flash sin que el operador visite una sola página.

Reset de fábrica
================

``POST /default`` (el botón de *reset de fábrica* de la página Sistema) llama a
``app_config_factory_reset()``, que devuelve la configuración a
``app_config_set_defaults()`` y reescribe **todos** los archivos de sección. Por
sí solo no elimina los archivos separados de telemetría/boletines/objitems/
telegram — esos regeneran sus valores por defecto en el siguiente acceso si se
borran desde la página Almacenamiento.

Claves de la interconexión BrandMeister
=======================================

.. list-table::
   :header-rows: 1
   :widths: 25 15 60

   * - Clave
     - Tipo
     - Significado
   * - ``bmEn``
     - bool
     - Interruptor principal de la interconexión BrandMeister. Apagado por
       omisión.
   * - ``bmMonitor``
     - bool
     - Intención de correr la suscripción mundial ``u/APBM*``. Se fuerza a
       apagado al cargar cuando ``inet2rf`` está activo e ``inet2rfRangeEn``
       apagado, así un archivo editado a mano no puede saltear el
       enclavamiento.
   * - ``bmMsgInetOnly``
     - bool
     - Rutear los mensajes a destinatarios BrandMeister solo por APRS-IS.
       Habilitado por omisión; solo puede quitar la pata de RF.
   * - ``bmGateways``
     - arreglo de 4 cadenas
     - Indicativos opcionales de estación de entrada para la tercera prueba del
       clasificador. Un ``*`` final compara por prefijo.
   * - ``inet2rfRangeEn``
     - bool
     - Habilita el filtro de rango INET→RF. Apagado por omisión.
   * - ``inet2rfRangeKm``
     - número
     - Radio del filtro de rango INET→RF en km, 0 = sin límite. Se acota a
       0…20038 al cargar.

Claves de Winlink (APRSLink)
============================

.. list-table::
   :header-rows: 1
   :widths: 25 15 60

   * - Clave
     - Tipo
     - Significado
   * - ``wlEnable``
     - bool
     - Interruptor maestro del cliente Winlink. Apagado por omisión: el cliente
       necesita una cuenta y una contraseña antes de poder hacer nada.
   * - ``wlServiceCall``
     - string
     - Indicativo del servicio APRSLink. ``WLNK-1`` por omisión; un valor vacío
       carga ese valor por omisión.
   * - ``wlPassword``
     - string
     - Contraseña de la cuenta Winlink, hasta 16 caracteres. Nunca se
       transmite: un desafío de acceso nombra posiciones de caracteres y solo
       esos caracteres se devuelven.
   * - ``wlUseMsgCall``
     - bool
     - Usar ``msgMycall`` como identidad Winlink. Encendido por omisión, porque
       ese indicativo es el que lleva la trama saliente y por lo tanto el que
       ve el servicio.
   * - ``wlMyCall``
     - string
     - Identidad Winlink cuando ``wlUseMsgCall`` está apagado. El servicio abre
       el buzón según su indicativo base, sin el SSID.
   * - ``wlAutoLogin``
     - bool
     - Abrir una sesión por sí solo cuando se encola una orden estando
       inactivo. Encendido por omisión.
   * - ``wlSessionMaxMin``
     - number
     - Duración local de la sesión en minutos, 5…180, 110 por omisión. Se
       mantiene por debajo de la caducidad de dos horas del propio servicio,
       para que esta estación abandone la sesión primero. Se acota al cargar.
   * - ``wlPollMin``
     - number
     - Minutos entre consultas espontáneas del correo pendiente, 0…1440. 0 no
       consulta nunca, que es el valor por omisión. Se acota al cargar.
   * - ``wlCommentEn``
     - bool
     - Agregar la marca de notificación Winlink al comentario de la baliza,
       para que el servicio sepa que esta estación lee su correo. Apagado por
       omisión.
   * - ``wlInetOnly``
     - bool
     - Mantener fuera del aire el tráfico Winlink propio mientras haya enlace
       con APRS-IS. Encendido por omisión; solo puede quitar la pata de RF.
   * - ``wlGateExempt``
     - bool
     - Dejar que una respuesta del servicio llegue a RF aunque su destinatario
       también se vea en APRS-IS. Encendido por omisión; levanta esa única
       condición del pase de mensajes y ninguna otra, y solo para
       ``wlServiceCall``.
