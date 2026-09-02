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
     - ``/bm``
     - impostazioni dell'interconnessione BrandMeister
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
     - ``/gps``
     - interruttore di abilitazione del ricevitore GNSS e vista in tempo reale
   * - GET
     - ``/gps/values``
     - tutti i valori riportati dal ricevitore GNSS (JSON)
   * - GET
     - ``/gps/live``
     - latitudine/longitudine/altitudine/velocità/rotta come numeri semplici
       (JSON), interrogato dalla casella *Usa GPS* di ogni pagina
   * - GET/POST
     - ``/telegram``
     - interruttore del bot Telegram, credenziali e diagnosi live della
       connessione
   * - GET
     - ``/telegram/status``
     - stato del bot, la sua causa e i suoi contatori (JSON), interrogato ogni
       2 s
   * - GET/POST
     - ``/winlink``
     - account Winlink, la politica di inoltro dei messaggi del servizio e il
       terminale di sessione
   * - POST
     - ``/winlink/cmd``
     - esegue un'azione di sessione: accedere, uscire, un comando, un passo di
       composizione o cancellare le risposte memorizzate (JSON
       ``{"ok":…,"error":…}``)
   * - GET
     - ``/winlink/status``
     - stato della sessione, tempo rimasto, comandi in attesa, dimensione
       della casella e ultimo errore (JSON), interrogato ogni 3 s
   * - GET
     - ``/winlink/list``
     - le risposte inviate dal servizio, dalla più vecchia (JSON)
   * - GET
     - ``/logs``
     - visore del registro di console; mostrarla ferma anche qualsiasi cattura
       ancora attiva
   * - POST
     - ``/logs/start``
     - attiva la copia della console (JSON ``{"ok":…,"seq":…}``)
   * - POST
     - ``/logs/stop``
     - ferma la copia della console e rilascia il suo anello (JSON)
   * - POST
     - ``/logs/read?since=<seq>``
     - righe di console catturate da ``seq`` (JSON), interrogato ogni 1 s;
       l'interrogazione riarma anche il tempo di inattività della copia, ed è
       per questo che è POST
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
     - login, freq. CPU, host/risincronizzazione NTP, selezione fuso orario
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

Politica di blocco del login
=============================

Le richieste senza intestazione ``Authorization``, o con una che non è
``Basic``, ricevono la sfida ``401`` senza essere contate come fallimento di
login — è la metà senza credenziali dell'handshake Basic Auth che ogni
browser esegue da sé. Conta solo una richiesta che ha presentato credenziali
ed è stata rifiutata. Dopo 5 rifiuti così dalla stessa origine IPv4, le
richieste successive ricevono ``429 Too Many Requests`` (con
``Retry-After``) per una finestra che parte da 5 s e raddoppia a ogni rifiuto
mentre il blocco è attivo, con tetto a 300 s; una finestra che scade senza
login riuscito viene riarmata un fallimento sotto la soglia, così credenziali
scadute ripetute fanno scattare solo il blocco base di 5 s ogni volta invece
di risalire fino al tetto. Vedi :ref:`it-web-admin` per i dettagli.
