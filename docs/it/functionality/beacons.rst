.. _it-beacons:

=========================
Beacon e il pianificatore
=========================

I beacon della propria stazione sono ciò che fa apparire la stazione su
aprs.fi. L'IGate e il digipeater da soli si limitano a ritrasmettere il traffico
che sentono; non annunciano mai la propria posizione. Esistono tre beacon logici
— **tracker**, **igate** e **digi** — ciascuno con i propri flag di abilitazione,
intervallo, coordinate, simbolo, commento e instradamento RF/INET, salvati dalla
rispettiva pagina dell'amministrazione web (``g_config.trk_*``,
``g_config.igate_*``, ``g_config.digi_*``).

Il pianificatore di beacon condiviso
====================================

Revisioni precedenti eseguivano i beacon di tracker, igate e digi, il report
meteo e i bollettini ciascuno nel **proprio task FreeRTOS**. Ognuno di quei task
faceva la stessa cosa — dormire, svegliarsi, costruire un pacchetto, percorrere la
catena TX TNC2/AX.25 condivisa (carica di operazioni in virgola mobile), dormire
di nuovo — e quindi ognuno doveva trascinare uno stack grande (10–14 KB)
dimensionato per quell'albero di chiamate, anche se quasi mai vengono eseguiti
contemporaneamente e il modem semi-duplex serializza comunque le loro
trasmissioni.

Il componente ``beacon_scheduler`` **fonde quei cinque task in uno**. A ogni
passata chiama la funzione "service" di ogni sottosistema (``beacon_service()``,
``weather_beacon_service()``, ``bulletins_service()``, e i servizi di
oggetti/item e telemetria), ciascuna delle quali trasmette ciò che è dovuto e
riporta quanti secondi mancano prima che serva di nuovo; il pianificatore poi
dorme fino al più vicino di questi. I sottosistemi conservano i propri flag di
abilitazione e intervalli indipendenti — solo il task (e il suo stack) è
condiviso.

Effetto netto: cinque stack (~61 KB in totale) diventano uno (~14 KB),
liberando ~46 KB di heap interno in questa build senza PSRAM.

Funzioni di servizio
====================

Ogni sottosistema espone una ``*_service()`` che:

#. Controlla i propri flag di abilitazione. Un beacon disabilitato è un no-op a
   basso costo che restituisce un intervallo di ri-controllo breve, così che
   attivarlo dal web abbia comunque effetto senza riavvio.
#. Trasmette qualsiasi beacon attualmente dovuto, in RF
   (``aprs_service_send_tnc2()``) e/o ad APRS-IS (``igate_send_raw()``) secondo i
   flag ``loc2rf`` / ``loc2inet`` della pagina.
#. Restituisce il numero di secondi (sempre ≥ 1) fino al prossimo evento dovuto
   più vicino.

``beacon_service()`` gestisce i tre beacon di posizione in una singola passata.

Jitter anti-collisione
======================

La pianificazione dei beacon è altrimenti deterministica, quindi più stazioni che
scelgono tutte lo stesso intervallo tondo (es. WX ogni 600 s) tendono a
sincronizzarsi in fase e a collidere su un canale RF condiviso — una classica
patologia APRS. ``beacon_scheduler_jitter()`` disperde il momento dovuto di un
beacon di ± una piccola percentuale (seminato con ``esp_random()``, uniforme),
così che i beacon della propria stazione si decorrelano sia tra loro sia dalle
stazioni vicine, e i beacon dovuti simultaneamente derivano separandosi nel
tempo. Il jitter è applicato all'intervallo usato per calcolare il timestamp del
**prossimo dovuto** di un beacon — non semplicemente al sonno del pianificatore,
che lascerebbe la griglia temporale dei dovuti sottostante deterministica e le
permetterebbe di ri-sincronizzarsi al ciclo successivo.

Scaglionamento del TX dentro una passata
========================================

Quando più beacon della propria stazione diventano dovuti insieme, vengono
serviti consecutivamente nel task del pianificatore, molto più velocemente di
quanto un frame a 1200 Bd liberi l'etere. Con il valore di fabbrica *TX buffers =
1*, il 2° e 3° frame urterebbero un anello TX RF pieno e verrebbero scartati. Per
evitarlo, il task del pianificatore si registra tramite
``aprs_service_set_beacon_context()``, e **solo in quel task**
``aprs_service_send_tnc2()`` può attendere brevemente (fino a 4 s) che l'anello
scenda sotto il limite prima di arrendersi — così ogni beacon dovuto finisce per
attivare la radio, mentre tutti gli altri chiamanti (RX/digipeat, INET→RF, TX di
messaggi) mantengono il comportamento non bloccante di scarta-se-pieno e un ramo
RF occupato non ferma mai la decodifica RX né il socket APRS-IS.

I timestamp sono UTC
====================

I timestamp dei beacon sono zulu/UTC (``051200z``) secondo la specifica APRS —
per cui ``time_sync.c`` imposta l'orologio di sistema su ``TZ=UTC0``
indipendentemente da ``g_config.timeZone`` (il fuso configurato è solo per la
visualizzazione).
