.. _it-telemetry:

==========
Telemetria
==========

Il sottosistema ``telemetry`` (``main/telemetry.c``) raccoglie canali analogici e
digitali dal registro ``sensors_local`` e trasmette come beacon un report di dati
di telemetria APRS standard (``T#nnn``) in RF e/o APRS-IS, insieme ai messaggi di
metadati PARM/UNIT/EQNS/BITS che etichettano quei canali per le stazioni
riceventi. Rispecchia lo schema usato dal sottosistema meteo, ma per la
telemetria.

Archiviazione separata
======================

A differenza della maggior parte delle impostazioni, la configurazione di
telemetria deliberatamente **non** vive in ``g_config``/``config.json``. Persiste
nel suo piccolo file LittleFS, ``/storage/telemetry.json``, allo stesso modo in
cui bollettini e oggetti/item mantengono i propri file. Al primo avvio, o quando
il file manca, viene creato un insieme vuoto predefinito così che
``/storage/telemetry.json`` esista sempre una volta che il sottosistema è
avviato. Lo schema completo è ``telemetry_config_t``
(``main/include/telemetry.h``).

Canali
======

Secondo il capitolo 13 di APRS101, un report di telemetria porta:

* **5 canali analogici** ``A1``–``A5`` (``TLM_CH = 5``).
* **8 bit digitali** ``B1``–``B8`` (``TLM_BIT_NUM = 8``).

Ogni canale analogico ha un flag di abilitazione, un indice di canale di sensore
di origine (``tlm_ana_channel[]``, ``0xFF`` = nessuno), una calibrazione
quadratica (``valore = a·x² + b·x + c``), un intervallo di ingresso grezzo
atteso che limita il valore trasmesso, e un numero di decimali. Ogni bit digitale ha un flag di abilitazione, un
canale di origine, un senso (Normale / Invertito), instradamento RF/INET per bit,
e un'etichetta orientata all'operatore usata nel messaggio BITS.

Cosa va in onda
===============

``build_tlm_data_packet()`` (in ``telemetry.c``) risolve ogni canale mappato dal
registro (via ``sensors_local_save_one()``) una volta al secondo e codifica il
report di dati periodico:

.. code-block:: text

   T#sss,a1,a2,a3,a4,a5,bbbbbbbb

I campi analogici portano la lettura **grezza** del sensore, limitata
all'intervallo grezzo dichiarato del canale e scritta con la larghezza di campo
e i decimali per canale; gli otto caratteri ``b`` sono i bit digitali. La
calibrazione *non* viene applicata qui: APRS101 separa il report dai metadati,
quindi il messaggio ``EQNS.`` porta i coefficienti a/b/c e ogni stazione
ricevente ricava da sé il valore ingegneristico. Il report **non** porta mai
nomi di canale — secondo la specifica APRS, nomi, unità ed equazioni viaggiano
separatamente.

I messaggi di metadati
======================

A una cadenza più lenta (``info_interval``), il modulo emette i messaggi di
definizione come messaggi APRS diretti alla propria stazione:

.. code-block:: text

   :MYCALL   :PARM.<nomi analogici>,<nomi dei bit>
   :MYCALL   :UNIT.<unità analogiche>,<etichette di stato-attivo dei bit>
   :MYCALL   :EQNS.<a,b,c per canale analogico>
   :MYCALL   :BITS.<mappa di bit di senso>,<titolo del progetto>

La generazione di ciascuno è commutabile individualmente (``gen_parm``,
``gen_unit``, ``gen_eqns``, ``gen_bits``).

Parametri del report
====================

La configurazione porta anche opzioni di incapsulamento del capitolo 13 di
APRS101: un percorso di digipeater a testo libero (``report_path``), TOCALL di
destinazione (``tocall``), numero di sequenza auto-incrementante (``auto_seq``),
larghezza del campo analogico (``field_width``), un'opzione per omettere i canali
finali non usati (``omit_trailing``), un commento a testo libero in coda
(``trail_comment``), e il numero di canali analogici/digitali effettivamente
inviati (``analog_count`` / ``digital_count``).

Impostare ``field_width`` a 3 riempie con zeri ogni valore analogico a tre
cifre, 000-999 - l'intervallo che APRS 1.2 consente per questo campo, esteso
rispetto alla finestra originale 000-255 di APRS101. Un canale la cui
stazione ricevente si aspetti ancora il vecchio intervallo 0-255 può essere
mantenuto al suo interno impostando di conseguenza ``ana_raw_min``/
``ana_raw_max`` di quel canale.

Telemetria nel commento (APRS 1.2 base-91)
============================================

Accanto al report ``T#nnn``, l'opzione *Comment Telemetry*
(``comment_telemetry`` / ``cmtTlm``) fa sì che
``telemetry_build_comment_tlm()`` aggiunga una seconda codifica, compatta,
dello stesso campione al commento di posizione di una stazione:

.. code-block:: text

   |ss1122|

Il gruppo si apre e si chiude con ``|``. La prima coppia base-91 è il numero
di sequenza; ogni coppia successiva è un canale analogico, in ordine (``A1``
per primo). Porta solo canali analogici - i bit digitali non hanno una
posizione base-91 nel gruppo APRS 1.2 e restano esclusivi del report
``T#nnn``.

Non è una baliza a sé stante. Viaggia dentro il commento di posizione della
baliza attualmente in trasmissione - Tracker, IGate o Digipeater - con il
nominativo/SSID configurato nella pagina *Telemetry*; una baliza di posizione
trasmessa con un nominativo/SSID diverso non riceve mai il gruppo, perché una
stazione ricevente lo leggerebbe come la telemetria di quell'altra stazione. I
report di stato, gli oggetti e gli item non lo portano mai: solo un report di
posizione identifica un'unica stazione segnalante in modo abbastanza univoco
perché il gruppo abbia senso.

Il numero di sequenza è lo stesso contatore usato dal report ``T#nnn``, preso
dallo stesso istante di lettura dei canali, così i due non sono mai in
disaccordo su quale campione descrivono. La codifica base-91 dà a quel
contatore una finestra di 0-8280 (91×91 valori), che si azzera in modo
indipendente dal campo decimale 0-999 proprio del report.

Ogni coppia analogica viene emessa solo per un canale abilitato e attualmente
risolto dal registro sensori, e solo finché lo sono anche tutti i canali
precedenti nell'ordine A1-A5: il gruppo non ha un identificatore di canale per
coppia, quindi una stazione ricevente ricava il canale di ogni valore solo
dalla sua posizione nella sequenza. Il codificatore si ferma al primo vuoto
invece di saltarlo, mantenendo il gruppo come un prefisso ininterrotto A1,
A2, ... An.

Un gruppo che non entra nel buffer di uscita proprio di
``telemetry_build_comment_tlm()`` viene scartato invece che troncato - una
coppia base-91 troncata si decodifica come un valore sbagliato, non come uno
assente. Una volta risolto, i byte del gruppo (e quelli dell'estensione
``!DAO!`` finale, se abilitata) vengono riservati prima del testo del
commento dell'operatore, cosicché un rapporto di posizione il cui commento
ecceda il campo tronca il *commento*, mai il gruppo di telemetria né
l'estensione DAO che lo segue.

All'interno del campo di testo del rapporto di posizione l'ordine di
emissione è fisso: blocco di frequenza (se presente), commento
dell'operatore, gruppo di telemetria nel commento e infine ``!DAO!`` (se
abilitato) - in accordo con il capitolo 13 di APRS101 e con la regola di
posizionamento propria dell'estensione DAO (``aprs12/datum.txt``). Questo
ordine vale sia per il formato non compresso sia per il Mic-E.

Selettori della pagina web
==========================

La pagina *Telemetry* (``page_tlm.c``) riempie un menu a tendina *Source* per
ogni canale analogico e un menu a tendina *Channel* per ogni bit digitale dal
registro ``sensors_local`` in tempo reale, filtrato per i canali di telemetria
annunciati di ogni driver. I valori per canale in tempo reale sono mostrati
tramite ``/tlm/values``. Vedi :ref:`it-sensor-framework`.
