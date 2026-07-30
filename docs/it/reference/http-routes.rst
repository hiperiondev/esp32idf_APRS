.. _it-http-routes:

==========
Route HTTP
==========

L'amministrazione web registra le seguenti route
(``components/webconfig/web_server.c``). Tutte richiedono autenticazione HTTP
Basic salvo dove indicato.

.. list-table::
   :header-rows: 1
   :widths: 16 34 50

   * - Metodo
     - Route
     - Scopo
   * - GET
     - ``/``
     - radice / landing di login
   * - GET
     - ``/logout``
     - scarta l'auth Basic
   * - GET
     - ``/dashboard``
     - dashboard in tempo reale
   * - GET/POST
     - ``/station``
     - identità della propria stazione: indicativo, lat/lon/alt
   * - GET/POST
     - ``/igate``
     - impostazioni dell'IGate
   * - GET/POST
     - ``/digi``
     - impostazioni del digipeater
   * - GET/POST
     - ``/tracker``
     - impostazioni del tracker
   * - GET/POST
     - ``/wx``
     - impostazioni del report meteo
   * - GET
     - ``/wx/values``
     - valori WX di sensore per canale in tempo reale (JSON)
   * - GET/POST
     - ``/tlm``
     - impostazioni di telemetria + selettori di sensore per canale
   * - GET
     - ``/tlm/values``
     - valori di telemetria per canale in tempo reale (JSON)
   * - GET/POST
     - ``/bulletins``
     - bollettini APRS BLN1..BLN5
   * - GET/POST
     - ``/objects``
     - Oggetti / Item APRS
   * - GET/POST
     - ``/msg``
     - config del motore di messaggistica (RF/INET, ritentativo, cifratura)
   * - GET/POST
     - ``/msgchat``
     - interfaccia casella/composizione stile chat
   * - GET
     - ``/msgchat/list``
     - frammento di lista messaggi (JSON)
   * - GET/POST
     - ``/radio``
     - modem AFSK audio (FX.25, modulazione, ritenzione PTT, loop test)
   * - GET
     - ``/radio/looptest``
     - esegui il loop test (risultato JSON)
   * - GET/POST
     - ``/wireless``
     - modalità Wi-Fi, AP, 5 slot STA, potenza TX
   * - GET
     - ``/wifiscan``
     - risultati della scansione AP (JSON)
   * - GET/POST
     - ``/system``
     - login, hostname, freq. CPU, NTP, preset di percorso, timeout di reset
   * - POST
     - ``/default``
     - reset di fabbrica
   * - GET
     - ``/storage``
     - navigatore di file
   * - GET
     - ``/download?file=…``
     - scarica da LittleFS
   * - POST
     - ``/delete``
     - elimina un file
   * - POST
     - ``/upload``
     - upload multipart
   * - POST
     - ``/format``
     - riformatta LittleFS
   * - GET
     - ``/about``
     - versione firmware/IDF, partizione, form OTA
   * - POST
     - ``/ota_update``
     - upload multipart firmware → scrivi slot OTA inattivo → riavvia
   * - GET
     - ``/symbol``
     - riferimento/selettore di simboli APRS
   * - GET
     - ``/test``
     - riepilogo di autotest della config
   * - GET
     - ``/lastheard``
     - tabella LAST HEARD (JSON)
   * - GET
     - ``/igate_traffic?since=<seq>``
     - delta del log di traffico (JSON)
   * - GET
     - ``/dashinfo``
     - striscia compatta di info in tempo reale (JSON)
   * - GET
     - ``/sidebarInfo``
     - frammento di stats della barra laterale
   * - GET
     - ``/heapinfo``
     - uso dell'heap in tempo reale (JSON)
   * - GET
     - ``/style.css``
     - foglio di stile condiviso
