.. _it-http-routes:

==========
Route HTTP
==========

L'amministrazione web registra le seguenti route
(``components/webconfig/web_server.c``). Ogni gestore che serve dati di
configurazione o di traffico chiama ``web_check_auth()`` e richiede quindi
autenticazione HTTP Basic. Due route non lo fanno, e nessuna delle due espone
alcunché: ``GET /style.css`` è un foglio di stile statico che non porta dati di
configurazione o di traffico, e il browser lo richiede mentre disegna la stessa
richiesta di login; ``GET /logout`` risponde a ogni richiesta con il ``401`` che
fa scartare al browser le credenziali memorizzate, quindi non c'è nulla che un
controllo di autenticazione possa proteggere.

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
     - config del motore di messaggistica (RF/INET, ritentativo, GPIO di allarme)
   * - GET/POST
     - ``/query``
     - risponditore di query APRS (``?APRS?``/``?WX?``/``?IGATE?``, query
       dirette), intervallo di limitazione di frequenza
   * - GET/POST
     - ``/msgchat``
     - interfaccia casella/composizione stile chat
   * - GET
     - ``/msgchat/list``
     - frammento di lista messaggi (JSON)
   * - GET/POST
     - ``/radio``
     - modem AFSK audio (FX.25, modulazione, ritenzione PTT, loop test)
   * - POST
     - ``/radio/looptest``
     - esegui il loop test (risultato JSON)
   * - GET/POST
     - ``/wireless``
     - modalità Wi-Fi, AP, 5 slot STA, potenza TX
   * - POST
     - ``/wifiscan``
     - risultati della scansione AP (JSON)
   * - GET/POST
     - ``/system``
     - login, freq. CPU, NTP, preset di percorso
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
