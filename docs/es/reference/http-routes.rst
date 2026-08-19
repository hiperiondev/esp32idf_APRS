.. _es-http-routes:

==========
Rutas HTTP
==========

La administración web registra las siguientes rutas
(``components/webconfig/web_server.c``). Cada manejador llama a
``web_check_auth()`` y por lo tanto requiere autenticación HTTP Basic todo
manejador que sirva datos de configuración o de tráfico. Dos rutas no lo hacen, y
ninguna expone nada: ``GET /style.css`` es una hoja de estilos estática que no
lleva datos de configuración ni de tráfico, y el navegador la pide mientras
dibuja el propio desafío de login; ``GET /logout`` responde a toda petición con
el ``401`` que hace al navegador descartar sus credenciales guardadas, así que no
hay nada que una comprobación de autenticación pueda proteger.

.. list-table::
   :header-rows: 1
   :widths: 16 34 50

   * - Método
     - Ruta
     - Propósito
   * - GET
     - ``/``
     - raíz / landing de login
   * - GET
     - ``/logout``
     - descartar auth Basic
   * - GET
     - ``/dashboard``
     - panel en vivo
   * - GET/POST
     - ``/station``
     - identidad de la propia estación: indicativo, lat/lon/alt
   * - GET/POST
     - ``/igate``
     - ajustes del IGate
   * - GET/POST
     - ``/digi``
     - ajustes del digipeater
   * - GET/POST
     - ``/tracker``
     - ajustes del tracker
   * - GET/POST
     - ``/wx``
     - ajustes del informe meteorológico
   * - GET
     - ``/wx/values``
     - valores WX de sensor por canal en vivo (JSON)
   * - GET/POST
     - ``/tlm``
     - ajustes de telemetría + selectores de sensor por canal
   * - GET
     - ``/tlm/values``
     - valores de telemetría por canal en vivo (JSON)
   * - GET
     - ``/gps``
     - vista en vivo del receptor GNSS NMEA (solo lectura)
   * - GET
     - ``/gps/values``
     - todos los valores que informa el receptor GNSS (JSON)
   * - GET/POST
     - ``/bulletins``
     - boletines APRS BLN1..BLN5
   * - GET/POST
     - ``/objects``
     - Objetos / Ítems APRS
   * - GET/POST
     - ``/msg``
     - config del motor de mensajería (RF/INET, reintento, GPIO de alarma)
   * - GET/POST
     - ``/query``
     - respondedor de consultas APRS (``?APRS?``/``?WX?``/``?IGATE?``,
       consultas dirigidas), intervalo de limitación de tasa
   * - GET/POST
     - ``/msgchat``
     - interfaz de bandeja/redacción estilo chat
   * - GET
     - ``/msgchat/list``
     - fragmento de lista de mensajes (JSON)
   * - GET/POST
     - ``/radio``
     - módem AFSK de audio (FX.25, modulación, retención PTT, loop test)
   * - POST
     - ``/radio/looptest``
     - ejecutar el loop test (resultado JSON)
   * - GET/POST
     - ``/wireless``
     - modo Wi-Fi, AP, 5 ranuras STA, potencia TX
   * - POST
     - ``/wifiscan``
     - resultados del escaneo de AP (JSON)
   * - GET/POST
     - ``/system``
     - login, frec. CPU, hosts/resincronización NTP, selección de zona horaria
   * - POST
     - ``/default``
     - reset de fábrica
   * - GET
     - ``/storage``
     - navegador de archivos
   * - GET
     - ``/download?file=…``
     - descargar de LittleFS
   * - POST
     - ``/delete``
     - borrar un archivo
   * - POST
     - ``/upload``
     - subida multipart
   * - POST
     - ``/format``
     - reformatear LittleFS
   * - GET
     - ``/about``
     - versión firmware/IDF, partición, formulario de OTA
   * - POST
     - ``/ota_update``
     - subida multipart de firmware → grabar ranura OTA inactiva → reiniciar
   * - GET
     - ``/symbol``
     - referencia/selector de símbolos APRS
   * - GET
     - ``/lastheard``
     - tabla LAST HEARD (JSON)
   * - GET
     - ``/igate_traffic?since=<seq>``
     - delta del registro de tráfico (JSON)
   * - GET
     - ``/dashinfo``
     - tira compacta de info en vivo (JSON)
   * - GET
     - ``/sidebarInfo``
     - fragmento de stats de barra lateral
   * - GET
     - ``/heapinfo``
     - uso de heap en vivo (JSON)
   * - GET
     - ``/style.css``
     - hoja de estilos compartida

Política de bloqueo de inicio de sesión
========================================

Las peticiones sin cabecera ``Authorization``, o con una que no sea
``Basic``, reciben el desafío ``401`` sin contarse como fallo de login — es la
mitad sin credenciales del handshake de Basic Auth que todo navegador realiza
por sí solo. Solo cuenta una petición que presentó credenciales y fue
rechazada. Tras 5 rechazos así desde el mismo origen IPv4, las peticiones
siguientes reciben ``429 Too Many Requests`` (con ``Retry-After``) durante una
ventana que empieza en 5 s y se duplica con cada rechazo mientras sigue
bloqueado, con tope de 300 s; una ventana que expira sin login exitoso se
rearma un fallo por debajo del umbral, de modo que credenciales caducadas
repetidas solo disparan el bloqueo base de 5 s cada vez en lugar de escalar
hasta el tope. Véase :ref:`es-web-admin` para más detalles.
