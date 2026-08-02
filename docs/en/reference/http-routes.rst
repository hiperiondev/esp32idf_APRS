.. _en-http-routes:

===========
HTTP Routes
===========

The web admin registers the following routes (``components/webconfig/web_server.c``).
Every handler calls ``web_check_auth()`` and therefore requires HTTP Basic auth,
with one exception: ``GET /style.css`` is served unauthenticated, because it is a
static stylesheet carrying no configuration or traffic data and the browser
requests it while rendering the login challenge itself.

.. list-table::
   :header-rows: 1
   :widths: 16 34 50

   * - Method
     - Route
     - Purpose
   * - GET
     - ``/``
     - root / login landing
   * - GET
     - ``/logout``
     - drop Basic auth
   * - GET
     - ``/dashboard``
     - live dashboard
   * - GET/POST
     - ``/station``
     - own-station identity: callsign, lat/lon/alt
   * - GET/POST
     - ``/igate``
     - IGate settings
   * - GET/POST
     - ``/digi``
     - digipeater settings
   * - GET/POST
     - ``/tracker``
     - tracker settings
   * - GET/POST
     - ``/wx``
     - Weather Report settings
   * - GET
     - ``/wx/values``
     - live per-channel WX sensor values (JSON)
   * - GET/POST
     - ``/tlm``
     - telemetry settings + per-channel sensor pickers
   * - GET
     - ``/tlm/values``
     - live per-channel telemetry sensor values (JSON)
   * - GET/POST
     - ``/bulletins``
     - APRS bulletins BLN1..BLN5
   * - GET/POST
     - ``/objects``
     - APRS Objects / Items
   * - GET/POST
     - ``/msg``
     - messaging engine config (RF/INET, retry, encryption)
   * - GET/POST
     - ``/query``
     - APRS query responder (``?APRS?``/``?WX?``/``?IGATE?``, directed
       queries), rate-limit interval
   * - GET/POST
     - ``/msgchat``
     - chat-style inbox/compose UI
   * - GET
     - ``/msgchat/list``
     - message list fragment (JSON)
   * - GET/POST
     - ``/radio``
     - audio AFSK modem (FX.25, modulation, PTT hold, loop test)
   * - POST
     - ``/radio/looptest``
     - run the loop test (JSON result)
   * - GET/POST
     - ``/wireless``
     - Wi-Fi mode, AP, 5 STA slots, TX power
   * - POST
     - ``/wifiscan``
     - AP scan results (JSON)
   * - GET/POST
     - ``/system``
     - login, hostname, CPU freq, NTP, path presets, reset timeout
   * - POST
     - ``/default``
     - factory reset
   * - GET
     - ``/storage``
     - file browser
   * - GET
     - ``/download?file=…``
     - download from LittleFS
   * - POST
     - ``/delete``
     - delete a file
   * - POST
     - ``/upload``
     - multipart upload
   * - POST
     - ``/format``
     - reformat LittleFS
   * - GET
     - ``/about``
     - firmware/IDF version, partition, OTA update form
   * - POST
     - ``/ota_update``
     - multipart firmware upload → flash inactive OTA slot → reboot
   * - GET
     - ``/symbol``
     - APRS symbol reference/picker
   * - GET
     - ``/lastheard``
     - LAST HEARD table (JSON)
   * - GET
     - ``/igate_traffic?since=<seq>``
     - traffic log delta (JSON)
   * - GET
     - ``/dashinfo``
     - compact live info strip (JSON)
   * - GET
     - ``/sidebarInfo``
     - sidebar stats fragment
   * - GET
     - ``/heapinfo``
     - live heap usage (JSON)
   * - GET
     - ``/style.css``
     - shared stylesheet
