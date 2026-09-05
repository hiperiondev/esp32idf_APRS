.. _en-http-routes:

===========
HTTP Routes
===========

The web admin registers the following routes (``components/webconfig/web_server.c``).
Every handler that serves configuration or traffic data calls
``web_check_auth()`` and therefore requires HTTP Basic auth. Three routes do not,
and none of them exposes anything: ``GET /style.css`` is a static stylesheet
carrying no configuration or traffic data, and the browser requests it while
rendering the login challenge itself; ``GET /logo.png`` is the brand image
embedded in the firmware, equally free of station data; ``GET /logout`` answers
every request with the ``401`` that makes the browser drop its cached
credentials, so there is nothing for an auth check to guard.

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
     - ``/bm``
     - BrandMeister interconnect settings
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
     - live per-channel WX sensor values (JSON); each distinct channel is read
       once per request, however many rows point at it
   * - GET/POST
     - ``/tlm``
     - telemetry settings + per-channel sensor pickers
   * - GET
     - ``/tlm/values``
     - live per-channel telemetry sensor values (JSON); each distinct channel
       is read once per request, however many rows point at it
   * - GET/POST
     - ``/gps``
     - GNSS receiver enable switch and live view
   * - GET
     - ``/gps/values``
     - every value the GNSS receiver reports (JSON)
   * - GET
     - ``/gps/live``
     - latitude/longitude/altitude/speed/course as plain numbers (JSON),
       polled by every page's *Use GPS* checkbox
   * - GET/POST
     - ``/telegram``
     - Telegram bot enable switch, credentials and live connection diagnosis
   * - GET
     - ``/telegram/status``
     - bot state, the reason for it and its counters (JSON), polled every 2 s
   * - GET/POST
     - ``/winlink``
     - Winlink account, the message-gating policy for the service, and the
       session terminal
   * - POST
     - ``/winlink/cmd``
     - run one session action: log in, log off, a command, a compose step, an
       action on one listed message (``n`` holds its number) or clearing the
       stored replies (JSON ``{"ok":…,"error":…}``)
   * - GET
     - ``/winlink/status``
     - session state, remaining lifetime, queue depth, mailbox size and the
       last failure (JSON), polled every 3 s
   * - GET
     - ``/winlink/list``
     - the replies the service has sent, oldest first (JSON)
   * - GET
     - ``/logs``
     - console log viewer; rendering it has no effect on the mirror, the
       page's script posts ``/logs/stop`` as it loads
   * - POST
     - ``/logs/start``
     - switch the console mirror on (JSON ``{"ok":…,"seq":…}``)
   * - POST
     - ``/logs/stop``
     - switch the console mirror off and release its ring (JSON)
   * - POST
     - ``/logs/read?since=<seq>``
     - captured console lines since ``seq`` (JSON), polled every 1 s; the poll
       is also what rearms the mirror's idle timeout, which is why it is POST
   * - GET/POST
     - ``/bulletins``
     - APRS bulletins BLN1..BLN5
   * - GET/POST
     - ``/objects``
     - APRS Objects / Items
   * - GET/POST
     - ``/msg``
     - messaging engine config (RF/INET, retry, alarm GPIO)
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
     - login, CPU freq, NTP hosts/resync, timezone selection
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
   * - GET
     - ``/logo.png``
     - top bar brand logo (embedded PNG)

Login lockout policy
=====================

Requests with no ``Authorization`` header, or a non-``Basic`` one, receive the
``401`` challenge without being counted as a login failure — this is the
credential-less half of the Basic Auth handshake every browser performs on
its own. Only a request that presented credentials and was rejected counts.
After 5 such rejections from the same source IPv4 address, further requests
get ``429 Too Many Requests`` (with ``Retry-After``) for a window starting at
5 s and doubling on each further rejection while locked out, capped at 300 s;
a window that elapses without a successful login rearms one failure below the
threshold, so repeated stale credentials re-trigger only the base 5 s window
each time rather than climbing back to the cap. See :ref:`en-web-admin` for
details.
